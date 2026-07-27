// CowEngine control plane: the game catalog, publishing, and room orchestration.
//
// It owns the published bundles on disk, a SQLite catalog, and one
// CowEngineServer child process per live game world. The sidecar stays the
// public transport edge and asks this service which UDP port a room token maps
// to; a reverse proxy terminates TLS for both.
//
//   browser --https--> caddy --/api/*--> control  (this)
//                            --/net-----> sidecar --udp--> room processes
//
// Env:
//   COW_CONTROL_BIND      listen address              (0.0.0.0:8080)
//   COW_CONTROL_DB        sqlite file                 (/data/cowengine.db)
//   COW_BUNDLES_DIR       published bundle store      (/data/bundles)
//   COW_SERVER_BIN        game server binary          (/app/CowEngineServer)
//   COW_ROOM_PORT_MIN/MAX room port pool              (4500 / 4599)
//   COW_MAX_ROOMS         concurrent world cap        (8)
//   COW_ROOM_IDLE_SECS    stop a room empty this long (300)
//   COW_PUBLIC_WS / _WT   URLs handed to the browser on join
//   COW_PUBLIC_CERTHASH   self-signed WT cert hash, dev only
//   COW_ALLOWED_ORIGINS   CORS allow-list, comma separated (empty = any)
//   COW_MAX_BUNDLE_BYTES  publish size cap            (3 MiB)
//   COW_MAX_BUNDLE_FILES  publish file-count cap      (200)
//   COW_PUBLISH_OPEN      "1" to open publishing      (off until M2 hardening)

mod api;
mod bundle;
mod db;
mod rooms;

use std::path::PathBuf;
use std::sync::Arc;
use std::time::Duration;

use axum::routing::{delete, get, patch, post, put};
use axum::Router;
use tower_http::cors::{Any, CorsLayer};
use tower_http::limit::RequestBodyLimitLayer;

use api::{AppState, Shared};
use rooms::{RoomConfig, RoomManager};

#[derive(Debug, Clone)]
pub struct Config {
    pub bundles_dir: PathBuf,
    pub max_bundle_bytes: usize,
    pub max_bundle_files: usize,
    pub public_ws: Option<String>,
    pub public_wt: Option<String>,
    pub public_cert_hash: Option<String>,
    /// Publishing is closed by default. A published .cow runs on this box and
    /// the language has no execution budget yet (M2), so opening this before
    /// that lands means any stranger can pin a core with `while true`.
    pub publish_open: bool,
}

fn env_or(key: &str, default: &str) -> String {
    std::env::var(key).unwrap_or_else(|_| default.to_string())
}

fn env_opt(key: &str) -> Option<String> {
    std::env::var(key).ok().filter(|s| !s.is_empty())
}

fn env_num<T: std::str::FromStr>(key: &str, default: T) -> T {
    std::env::var(key).ok().and_then(|s| s.parse().ok()).unwrap_or(default)
}

#[tokio::main]
async fn main() {
    // Under Docker stdout is a pipe and therefore block-buffered; Rust's stdout
    // is line-buffered to a pipe, which is fine, but be explicit about flushing
    // on the paths that matter by using println! only (never write! + no flush).
    let bind = env_or("COW_CONTROL_BIND", "0.0.0.0:8080");
    let db_path = env_or("COW_CONTROL_DB", "/data/cowengine.db");
    let bundles_dir = PathBuf::from(env_or("COW_BUNDLES_DIR", "/data/bundles"));

    if let Some(parent) = PathBuf::from(&db_path).parent() {
        let _ = std::fs::create_dir_all(parent);
    }
    std::fs::create_dir_all(&bundles_dir).expect("could not create COW_BUNDLES_DIR");

    let conn = db::open(&db_path).expect("could not open the catalog database");

    let room_cfg = RoomConfig {
        server_bin: PathBuf::from(env_or("COW_SERVER_BIN", "/app/CowEngineServer")),
        port_min: env_num("COW_ROOM_PORT_MIN", 4500u16),
        port_max: env_num("COW_ROOM_PORT_MAX", 4599u16),
        max_rooms: env_num("COW_MAX_ROOMS", 8usize),
        idle_secs: env_num("COW_ROOM_IDLE_SECS", rooms::DEFAULT_IDLE_SECS),
        ..RoomConfig::default()
    };
    if room_cfg.port_min > room_cfg.port_max {
        panic!("COW_ROOM_PORT_MIN must not exceed COW_ROOM_PORT_MAX");
    }
    if !room_cfg.server_bin.exists() {
        // Not fatal: the catalog still serves. But a join will fail, and finding
        // that out at join time is much worse than a loud line at boot.
        eprintln!(
            "WARNING: COW_SERVER_BIN {:?} does not exist — joins will fail",
            room_cfg.server_bin
        );
    }

    let cfg = Config {
        bundles_dir: bundles_dir.clone(),
        max_bundle_bytes: env_num("COW_MAX_BUNDLE_BYTES", 3 * 1024 * 1024usize),
        max_bundle_files: env_num("COW_MAX_BUNDLE_FILES", 200usize),
        public_ws: env_opt("COW_PUBLIC_WS"),
        public_wt: env_opt("COW_PUBLIC_WT"),
        public_cert_hash: env_opt("COW_PUBLIC_CERTHASH"),
        publish_open: env_or("COW_PUBLISH_OPEN", "0") == "1",
    };

    let room_mgr = RoomManager::new(room_cfg.clone())
        .await
        .expect("could not open the room probe socket");

    println!("cowengine-control: listening on {bind}");
    println!("  db      {db_path}");
    println!("  bundles {}", bundles_dir.display());
    println!(
        "  rooms   max={} ports={}..={} idle={}s bin={:?}",
        room_cfg.max_rooms, room_cfg.port_min, room_cfg.port_max, room_cfg.idle_secs,
        room_cfg.server_bin
    );
    println!(
        "  publish {} (max {} bytes / {} files)",
        if cfg.publish_open { "OPEN" } else { "closed (set COW_PUBLISH_OPEN=1)" },
        cfg.max_bundle_bytes, cfg.max_bundle_files
    );
    println!("  join -> ws={:?} wt={:?}", cfg.public_ws, cfg.public_wt);

    let state: Shared = Arc::new(AppState {
        db: tokio::sync::Mutex::new(conn),
        rooms: room_mgr.clone(),
        cfg: cfg.clone(),
    });

    // Reap on a timer: refresh player counts, drop rooms whose process died, and
    // stop rooms that have stood empty past the idle window.
    {
        let mgr = room_mgr.clone();
        tokio::spawn(async move {
            let mut ticker = tokio::time::interval(Duration::from_secs(5));
            loop {
                ticker.tick().await;
                mgr.reap_once().await;
            }
        });
    }

    let cors = match env_opt("COW_ALLOWED_ORIGINS") {
        Some(list) => {
            let origins: Vec<_> = list
                .split(',')
                .map(str::trim)
                .filter(|s| !s.is_empty())
                .filter_map(|s| s.parse().ok())
                .collect();
            CorsLayer::new().allow_origin(origins).allow_methods(Any).allow_headers(Any)
        }
        // The catalog is public and the page lives on a different origin
        // (GitHub Pages), so cross-origin reads are the normal case. Writes are
        // still gated by the edit key, which CORS does not protect anyway.
        None => CorsLayer::new().allow_origin(Any).allow_methods(Any).allow_headers(Any),
    };

    let app = Router::new()
        .route("/api/health", get(api::health))
        .route("/api/games", get(api::list_games).post(api::publish))
        .route("/api/games/:id", get(api::get_game))
        .route("/api/games/:id", put(api::republish))
        .route("/api/games/:id", patch(api::patch_game))
        .route("/api/games/:id", delete(api::delete_game))
        .route("/api/games/:id/bundle", get(api::get_bundle))
        .route("/api/games/:id/join", post(api::join_game))
        .route("/internal/rooms/:token", get(api::internal_room))
        // Hard cap before the JSON body is even buffered, so an oversized upload
        // costs us nothing. The validator's own limit is the finer-grained one.
        .layer(RequestBodyLimitLayer::new(cfg.max_bundle_bytes * 2 + 65536))
        .layer(cors)
        .with_state(state);

    let listener = tokio::net::TcpListener::bind(&bind)
        .await
        .unwrap_or_else(|e| panic!("could not bind {bind}: {e}"));

    let shutdown_mgr = room_mgr.clone();
    axum::serve(listener, app)
        .with_graceful_shutdown(async move {
            shutdown_signal().await;
            // Rooms are children of this process; leaving them running across a
            // restart would strand players on ports the new process thinks are
            // free. kill_on_drop covers a hard crash, this covers a clean stop.
            println!("shutting down: stopping all rooms");
            shutdown_mgr.stop_all().await;
        })
        .await
        .expect("server error");
}

async fn shutdown_signal() {
    let ctrl_c = async {
        tokio::signal::ctrl_c().await.expect("install ctrl-c handler");
    };
    #[cfg(unix)]
    let term = async {
        tokio::signal::unix::signal(tokio::signal::unix::SignalKind::terminate())
            .expect("install SIGTERM handler")
            .recv()
            .await;
    };
    #[cfg(not(unix))]
    let term = std::future::pending::<()>();

    tokio::select! {
        _ = ctrl_c => {}
        _ = term => {}
    }
}
