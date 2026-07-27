// WebTransport (HTTP/3 over QUIC) listener. Bridges each browser session to the
// C++ server through the shared Relay, exactly like the WS path.
//
// Browser<->sidecar mapping over WebTransport:
//   unreliable  <->  QUIC datagrams
//   reliable    <->  one bidirectional stream per session, opened by the
//                    browser, carrying every reliable message as a
//                    [u32 LE length][payload] frame.
//
// It has to be one persistent stream rather than one stream per message: QUIC
// only orders bytes *within* a stream, not across sibling streams from the
// same sender. A scene reset fires a burst of SpawnEntity/DespawnEntity in
// close succession, and under real network jitter the streams for two of
// them can resolve out of order at the receiver even though each one
// individually arrived intact — the client would then apply them in the
// wrong order (e.g. a Spawn for an id landing after a later Despawn of that
// same id), which is exactly the kind of thing that "mostly works, then
// glitches" under load. A single stream with explicit framing gives the same
// in-order, reliable delivery Protocol.hpp's Channel::Reliable already
// promises the rest of the engine.

use std::net::SocketAddr;
use std::sync::Arc;
use std::time::Duration;

use anyhow::Result;
use base64::Engine;
use tokio::sync::{mpsc, Notify};
use wtransport::endpoint::{IncomingSession, SessionRequest};
use wtransport::tls::Sha256DigestFmt;
use wtransport::{Endpoint, Identity, ServerConfig};

use crate::{
    query_param, Relay, CH_RELIABLE, KIND_CONNECT, KIND_DISCONNECT, KIND_RELIABLE, KIND_UNRELIABLE,
};

pub async fn run(bind_addr: String, relay: Arc<Relay>) -> Result<()> {
    let addr: SocketAddr = bind_addr.parse()?;

    // Production: load the same CA-signed PEM the wss listener uses (COW_TLS_CERT
    // fullchain + COW_TLS_KEY). A publicly-trusted cert means the browser needs no
    // serverCertificateHashes — it validates normally, so no ?certhash= is needed.
    // Dev fallback: an in-memory self-signed cert whose SHA-256 the client pins.
    let identity = match (std::env::var("COW_TLS_CERT").ok(), std::env::var("COW_TLS_KEY").ok()) {
        (Some(cert), Some(key)) => {
            let id = Identity::load_pemfiles(&cert, &key)
                .await
                .map_err(|e| anyhow::anyhow!("WT: load cert '{cert}' / key '{key}': {e}"))?;
            println!("webtransport: https://{addr} (CA cert from {cert})");
            id
        }
        _ => {
            let id = Identity::self_signed(["localhost", "127.0.0.1", "::1"])?;
            let hash = id.certificate_chain().as_slice()[0].hash();
            let b64 = base64::engine::general_purpose::STANDARD.encode(hash.as_ref());
            println!("webtransport: https://{addr} (self-signed dev cert)");
            println!("webtransport: cert sha-256 base64 = {b64}");
            println!("webtransport: cert sha-256 hex    = {}", hash.fmt(Sha256DigestFmt::DottedHex));
            println!("  (pass ?certhash=<base64> to the client for the self-signed dev cert)");
            id
        }
    };

    let config = ServerConfig::builder()
        .with_bind_address(addr)
        .with_identity(identity)
        .keep_alive_interval(Some(Duration::from_secs(3)))
        .build();

    let server = Endpoint::server(config)?;
    loop {
        let incoming = server.accept().await;
        let relay = relay.clone();
        tokio::spawn(async move {
            if let Err(e) = handle(incoming, relay).await {
                eprintln!("wt session error: {e:?}");
            }
        });
    }
}

async fn handle(incoming: IncomingSession, relay: Arc<Relay>) -> Result<()> {
    let session_request = incoming.await?;
    let ip = session_request.remote_address().ip();

    // Same access gate as the WS path: Origin allow-list, join key, per-IP cap.
    if !relay.cfg.origin_allowed(session_request.origin()) {
        println!("wt refused {ip}: origin not allowed ({:?})", session_request.origin());
        session_request.forbidden().await;
        return Ok(());
    }
    if !relay.cfg.key_ok(Some(session_request.path())) {
        println!("wt refused {ip}: missing/invalid join key");
        session_request.forbidden().await;
        return Ok(());
    }
    // Same room lookup as the WS path — `path()` carries the query string, and
    // query_param splits on '?' and '&' so it reads either.
    let room = query_param(Some(session_request.path()), "room");
    let upstream = match relay.rooms.resolve(room) {
        Some(addr) => addr,
        None => {
            println!("wt refused {ip}: unknown room {room:?}");
            session_request.forbidden().await;
            return Ok(());
        }
    };
    if !relay.acquire_ip(ip).await {
        println!("wt refused {ip}: per-IP connection cap reached");
        session_request.too_many_requests().await;
        return Ok(());
    }

    let result = handle_accepted(session_request, &relay, upstream).await;
    relay.release_ip(ip).await;
    result
}

async fn handle_accepted(
    session_request: SessionRequest,
    relay: &Arc<Relay>,
    upstream: SocketAddr,
) -> Result<()> {
    let connection = Arc::new(session_request.accept().await?);

    // The browser opens the one bidi stream used for every reliable message
    // right after connecting (see CowNet in GameTemplate.html). Wait for it
    // before registering the session, so nothing can try to use it early.
    let (mut reliable_tx, mut reliable_rx) = connection.accept_bi().await?;

    let (out_tx, mut out_rx) = mpsc::unbounded_channel::<(u8, Vec<u8>)>();
    let (session, kill) = relay.register(out_tx, upstream).await;
    relay.to_server(session, KIND_CONNECT, &[], upstream).await;
    println!("wt connect session={session} room={upstream}");

    // server -> browser
    let conn_out = connection.clone();
    let out_task = tokio::spawn(async move {
        while let Some((channel, payload)) = out_rx.recv().await {
            if channel == CH_RELIABLE {
                // Framed onto the one shared reliable stream -- see the
                // ordering note at the top of this file for why it can't be
                // a fresh stream per message.
                let len = (payload.len() as u32).to_le_bytes();
                if reliable_tx.write_all(&len).await.is_err() {
                    break;
                }
                if reliable_tx.write_all(&payload).await.is_err() {
                    break;
                }
            } else if conn_out.send_datagram(&payload).is_err() {
                // Datagram too large or connection gone; drop (unreliable).
            }
        }
    });

    // browser -> server
    let result = pump_incoming(&connection, &mut reliable_rx, relay, session, upstream, &kill).await;

    relay.to_server(session, KIND_DISCONNECT, &[], upstream).await;
    relay.deregister(session).await;
    out_task.abort();
    println!("wt disconnect session={session}");
    result
}

async fn pump_incoming(
    connection: &wtransport::Connection,
    reliable_rx: &mut wtransport::RecvStream,
    relay: &Arc<Relay>,
    session: u32,
    upstream: SocketAddr,
    kill: &Notify,
) -> Result<()> {
    let mut len_buf = [0u8; 4];
    loop {
        tokio::select! {
            // A room that stopped answering takes its sessions down with it;
            // see Relay::sweep_upstreams.
            _ = kill.notified() => return Ok(()),
            dgram = connection.receive_datagram() => {
                let dgram = dgram?;
                relay.to_server(session, KIND_UNRELIABLE, &dgram, upstream).await;
            }
            r = reliable_rx.read_exact(&mut len_buf) => {
                r?;
                let len = u32::from_le_bytes(len_buf) as usize;
                let mut payload = vec![0u8; len];
                reliable_rx.read_exact(&mut payload).await?;
                relay.to_server(session, KIND_RELIABLE, &payload, upstream).await;
            }
        }
    }
}
