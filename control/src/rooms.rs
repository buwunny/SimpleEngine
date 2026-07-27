// Room orchestration: one CowEngineServer child process per live game world.
//
// Spawning a room is nearly free because the server already takes its scene path
// as argv — a room is that binary with the bundle directory as its cwd, so
// scripts/ and models/ resolve through the engine's normal path ladder with no
// engine change at all.
//
// Liveness and player counts come from the status frame (kind 4 -> kind 5) the
// server answers on its normal port. That beats scraping stdout, which is
// block-buffered under Docker and would tie us to a log format forever.

use std::collections::HashMap;
use std::net::SocketAddr;
use std::path::PathBuf;
use std::process::Stdio;
use std::sync::Arc;
use std::time::{Duration, Instant};

use tokio::io::{AsyncBufReadExt, BufReader};
use tokio::net::UdpSocket;
use tokio::process::{Child, Command};
use tokio::sync::Mutex;

const KIND_STATUS: u8 = 4;
const KIND_STATUS_REPLY: u8 = 5;

/// How long a room may sit empty before it is stopped.
pub const DEFAULT_IDLE_SECS: u64 = 300;

#[derive(Debug, Clone)]
pub struct RoomConfig {
    pub server_bin: PathBuf,
    pub port_min: u16,
    pub port_max: u16,
    pub max_rooms: usize,
    pub idle_secs: u64,
    /// How long a freed port stays unusable. Stops a browser still holding a
    /// stale room token from having its packets land in whatever new world
    /// happened to reuse the port.
    pub port_cooldown: Duration,
    /// Longest we will block a join request waiting for a new room to come up.
    pub start_timeout: Duration,
}

impl Default for RoomConfig {
    fn default() -> Self {
        RoomConfig {
            server_bin: PathBuf::from("CowEngineServer"),
            port_min: 4500,
            port_max: 4599,
            max_rooms: 8,
            idle_secs: DEFAULT_IDLE_SECS,
            port_cooldown: Duration::from_secs(60),
            start_timeout: Duration::from_secs(5),
        }
    }
}

pub struct Room {
    pub token: String,
    pub game_id: String,
    pub version: i64,
    pub port: u16,
    pub max_players: u32,
    child: Child,
    pub players: u32,
    /// Last time the room had anyone in it — the clock idle reaping runs on.
    last_occupied: Instant,
}

impl Room {
    pub fn addr(&self) -> SocketAddr {
        SocketAddr::from(([127, 0, 0, 1], self.port))
    }
}

pub struct RoomManager {
    cfg: RoomConfig,
    rooms: Mutex<HashMap<String, Room>>,
    /// Ports currently in use or cooling down (port -> free again at).
    ports: Mutex<HashMap<u16, Option<Instant>>>,
}

#[derive(Debug)]
pub enum JoinError {
    AtCapacity,
    StartFailed(String),
}

impl std::fmt::Display for JoinError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            JoinError::AtCapacity => write!(f, "server is at room capacity"),
            JoinError::StartFailed(e) => write!(f, "could not start the world: {e}"),
        }
    }
}

impl RoomManager {
    pub async fn new(cfg: RoomConfig) -> std::io::Result<Arc<Self>> {
        Ok(Arc::new(RoomManager {
            cfg,
            rooms: Mutex::new(HashMap::new()),
            ports: Mutex::new(HashMap::new()),
        }))
    }

    pub fn config(&self) -> &RoomConfig {
        &self.cfg
    }

    /// Port for a room token — what the sidecar's `/internal/rooms/:token` asks.
    pub async fn port_of(&self, token: &str) -> Option<u16> {
        self.rooms.lock().await.get(token).map(|r| r.port)
    }

    /// Live player count per game id, for decorating the catalog.
    pub async fn players_by_game(&self) -> HashMap<String, u32> {
        let mut out: HashMap<String, u32> = HashMap::new();
        for room in self.rooms.lock().await.values() {
            *out.entry(room.game_id.clone()).or_insert(0) += room.players;
        }
        out
    }

    pub async fn room_count(&self) -> usize {
        self.rooms.lock().await.len()
    }

    /// An existing room for this exact game+version that still has space.
    async fn find_joinable(&self, game_id: &str, version: i64) -> Option<String> {
        self.rooms
            .lock()
            .await
            .values()
            .find(|r| r.game_id == game_id && r.version == version && r.players < r.max_players)
            .map(|r| r.token.clone())
    }

    async fn claim_port(&self) -> Option<u16> {
        let mut ports = self.ports.lock().await;
        let now = Instant::now();
        ports.retain(|_, free_at| match free_at {
            Some(t) => *t > now, // still cooling down
            None => true,        // in use
        });
        for p in self.cfg.port_min..=self.cfg.port_max {
            if !ports.contains_key(&p) {
                ports.insert(p, None);
                return Some(p);
            }
        }
        None
    }

    async fn release_port(&self, port: u16) {
        self.ports
            .lock()
            .await
            .insert(port, Some(Instant::now() + self.cfg.port_cooldown));
    }

    /// Join an existing room for this game+version, or start one.
    pub async fn join(
        &self,
        game_id: &str,
        version: i64,
        bundle_dir: PathBuf,
        max_players: u32,
    ) -> Result<String, JoinError> {
        if let Some(token) = self.find_joinable(game_id, version).await {
            return Ok(token);
        }
        if self.room_count().await >= self.cfg.max_rooms {
            return Err(JoinError::AtCapacity);
        }
        let port = match self.claim_port().await {
            Some(p) => p,
            None => return Err(JoinError::AtCapacity),
        };

        let token = random_token();
        match self.spawn(&token, game_id, version, port, bundle_dir, max_players).await {
            Ok(room) => {
                self.rooms.lock().await.insert(token.clone(), room);
                Ok(token)
            }
            Err(e) => {
                self.release_port(port).await;
                Err(JoinError::StartFailed(e))
            }
        }
    }

    async fn spawn(
        &self,
        token: &str,
        game_id: &str,
        version: i64,
        port: u16,
        bundle_dir: PathBuf,
        max_players: u32,
    ) -> Result<Room, String> {
        // --idle-exit is belt-and-braces against *this* process dying or losing
        // track of the child: the reaper below is the normal path, but an orphan
        // must still free its port on its own.
        let mut child = Command::new(&self.cfg.server_bin)
            .arg(port.to_string())
            .arg("scenes/scene.json")
            .arg("--idle-exit")
            .arg((self.cfg.idle_secs + 60).to_string())
            .current_dir(&bundle_dir)
            .stdout(Stdio::piped())
            .stderr(Stdio::piped())
            .kill_on_drop(true)
            .spawn()
            .map_err(|e| format!("spawn {:?}: {e}", self.cfg.server_bin))?;

        // Forward the child's output with a room prefix. Without this every room
        // writes into the same container log with nothing to tell them apart,
        // which this repo has already learned the hard way costs debugging cycles.
        for (stream, tag) in [
            (child.stdout.take().map(Either::Out), "out"),
            (child.stderr.take().map(Either::Err), "err"),
        ] {
            if let Some(stream) = stream {
                let label = format!("room {} {}", &token[..8.min(token.len())], tag);
                tokio::spawn(async move {
                    let reader: Box<dyn tokio::io::AsyncRead + Unpin + Send> = match stream {
                        Either::Out(s) => Box::new(s),
                        Either::Err(s) => Box::new(s),
                    };
                    let mut lines = BufReader::new(reader).lines();
                    while let Ok(Some(line)) = lines.next_line().await {
                        println!("[{label}] {line}");
                    }
                });
            }
        }

        let addr = SocketAddr::from(([127, 0, 0, 1], port));
        match self.await_ready(addr).await {
            Ok(()) => {}
            Err(e) => {
                let _ = child.kill().await;
                return Err(e);
            }
        }

        println!(
            "room {token} started: game={game_id} v{version} port={port} dir={}",
            bundle_dir.display()
        );
        Ok(Room {
            token: token.to_string(),
            game_id: game_id.to_string(),
            version,
            port,
            max_players,
            child,
            players: 0,
            last_occupied: Instant::now(),
        })
    }

    /// Poll the status frame until the room answers, or give up.
    async fn await_ready(&self, addr: SocketAddr) -> Result<(), String> {
        let deadline = Instant::now() + self.cfg.start_timeout;
        while Instant::now() < deadline {
            if self.status(addr, Duration::from_millis(150)).await.is_some() {
                return Ok(());
            }
        }
        Err(format!("room at {addr} did not become ready in {:?}", self.cfg.start_timeout))
    }

    /// One status round-trip: returns (sessions, players, uptime).
    ///
    /// Binds a fresh socket per probe rather than sharing one. A shared socket
    /// has to discard replies addressed to other in-flight probes, and a
    /// discarded reply is a *lost* reply — the probe waiting for it then times
    /// out and its room is misreported as unresponsive. Readiness checks during
    /// a join run concurrently with the reaper, so that race is reachable. One
    /// ephemeral socket per probe (a few per room per minute) removes it
    /// entirely and is cheaper than correlating replies by hand.
    async fn status(&self, addr: SocketAddr, timeout: Duration) -> Option<(u32, u32, u32)> {
        let sock = UdpSocket::bind(("127.0.0.1", 0)).await.ok()?;
        let mut frame = Vec::with_capacity(5);
        frame.extend_from_slice(&0u32.to_le_bytes());
        frame.push(KIND_STATUS);
        sock.send_to(&frame, addr).await.ok()?;

        let mut buf = [0u8; 64];
        let deadline = Instant::now() + timeout;
        loop {
            let left = deadline.checked_duration_since(Instant::now())?;
            let (n, from) = match tokio::time::timeout(left, sock.recv_from(&mut buf)).await {
                Ok(Ok(v)) => v,
                _ => return None,
            };
            if from != addr || n < 17 || buf[4] != KIND_STATUS_REPLY {
                continue;
            }
            let sessions = u32::from_le_bytes(buf[5..9].try_into().ok()?);
            let players = u32::from_le_bytes(buf[9..13].try_into().ok()?);
            let uptime = u32::from_le_bytes(buf[13..17].try_into().ok()?);
            return Some((sessions, players, uptime));
        }
    }

    /// Refresh player counts, drop rooms whose process died, and stop rooms that
    /// have stood empty past the idle window. Runs on a timer from main().
    pub async fn reap_once(&self) {
        let tokens: Vec<(String, SocketAddr)> = {
            let rooms = self.rooms.lock().await;
            rooms.values().map(|r| (r.token.clone(), r.addr())).collect()
        };

        let mut statuses = HashMap::new();
        for (token, addr) in tokens {
            statuses.insert(token, self.status(addr, Duration::from_millis(300)).await);
        }

        let mut dead: Vec<(String, u16)> = Vec::new();
        {
            let mut rooms = self.rooms.lock().await;
            let now = Instant::now();
            for (token, status) in statuses {
                let Some(room) = rooms.get_mut(&token) else { continue };

                // Child exited on its own (crash, or its own --idle-exit).
                if matches!(room.child.try_wait(), Ok(Some(_))) {
                    println!("room {token} process exited; dropping");
                    dead.push((token, room.port));
                    continue;
                }
                match status {
                    Some((sessions, players, _)) => {
                        room.players = players;
                        if sessions > 0 {
                            room.last_occupied = now;
                        } else if now.duration_since(room.last_occupied).as_secs() >= self.cfg.idle_secs {
                            println!("room {token} idle for {}s; stopping", self.cfg.idle_secs);
                            dead.push((token, room.port));
                        }
                    }
                    None => {
                        // Alive as a process but not answering. Give it the same
                        // idle grace rather than killing on one missed probe.
                        if now.duration_since(room.last_occupied).as_secs() >= self.cfg.idle_secs {
                            println!("room {token} unresponsive past the idle window; stopping");
                            dead.push((token, room.port));
                        }
                    }
                }
            }
        }

        for (token, port) in dead {
            self.stop(&token).await;
            self.release_port(port).await;
        }
    }

    pub async fn stop(&self, token: &str) {
        let room = self.rooms.lock().await.remove(token);
        if let Some(mut room) = room {
            // kill_on_drop would handle this, but do it explicitly and reap the
            // child so it cannot linger as a zombie.
            let _ = room.child.start_kill();
            let _ = room.child.wait().await;
            println!("room {token} stopped");
        }
    }

    /// Stop every room. Called on shutdown so a restart never leaves orphans
    /// holding ports.
    pub async fn stop_all(&self) {
        let tokens: Vec<String> = self.rooms.lock().await.keys().cloned().collect();
        for token in tokens {
            self.stop(&token).await;
        }
    }
}

enum Either {
    Out(tokio::process::ChildStdout),
    Err(tokio::process::ChildStderr),
}

/// 128 bits of randomness, hex. It is the join secret as well as the lookup key,
/// so a reaped room's token must not be guessable from a later one.
pub fn random_token() -> String {
    use rand::RngCore;
    let mut bytes = [0u8; 16];
    rand::thread_rng().fill_bytes(&mut bytes);
    bytes.iter().map(|b| format!("{b:02x}")).collect()
}
