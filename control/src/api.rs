// HTTP surface: the game catalog, publishing, and joining.
//
// Auth is intentionally minimal and matches the "anonymous now, OAuth later"
// decision: publishing returns a high-entropy edit key, and mutating a game
// requires presenting it in `X-Cow-Edit-Key`. The key is 256 random bits, so a
// plain SHA-256 of it is the right store — argon2 exists to slow down guessing
// low-entropy human passwords, which this is not. When OAuth lands, `owner_id`
// starts being filled in and this stays as the fallback for keyless clients.

use std::collections::BTreeMap;
use std::sync::Arc;

use axum::extract::{Path, Query, State};
use axum::http::{HeaderMap, StatusCode};
use axum::response::IntoResponse;
use axum::Json;
use serde::{Deserialize, Serialize};
use serde_json::json;
use sha2::{Digest, Sha256};

use crate::bundle::{self, BundleUpload, Limits};
use crate::db;
use crate::rooms::{JoinError, RoomManager};
use crate::Config;

pub struct AppState {
    pub db: tokio::sync::Mutex<rusqlite::Connection>,
    pub rooms: Arc<RoomManager>,
    pub cfg: Config,
}

pub type Shared = Arc<AppState>;

type ApiResult<T> = Result<T, ApiError>;

pub struct ApiError(StatusCode, String);

impl ApiError {
    fn new(code: StatusCode, msg: impl Into<String>) -> Self {
        ApiError(code, msg.into())
    }
    fn bad(msg: impl Into<String>) -> Self {
        Self::new(StatusCode::BAD_REQUEST, msg)
    }
    fn not_found(msg: impl Into<String>) -> Self {
        Self::new(StatusCode::NOT_FOUND, msg)
    }
    fn internal(msg: impl Into<String>) -> Self {
        Self::new(StatusCode::INTERNAL_SERVER_ERROR, msg)
    }
}

impl IntoResponse for ApiError {
    fn into_response(self) -> axum::response::Response {
        // Log 5xx: the client is told something generic, but an operator needs
        // the detail and this process's stdout is the only place it can go.
        if self.0.is_server_error() {
            eprintln!("api error {}: {}", self.0, self.1);
        }
        (self.0, Json(json!({ "error": self.1 }))).into_response()
    }
}

fn sqlite_err(e: rusqlite::Error) -> ApiError {
    ApiError::internal(format!("database: {e}"))
}

pub async fn health(State(st): State<Shared>) -> impl IntoResponse {
    Json(json!({
        "ok": true,
        "rooms": st.rooms.room_count().await,
        "max_rooms": st.rooms.config().max_rooms,
    }))
}

// ---------------------------------------------------------------- catalog

#[derive(Debug, Deserialize)]
pub struct ListQuery {
    q: Option<String>,
    limit: Option<i64>,
    offset: Option<i64>,
}

pub async fn list_games(
    State(st): State<Shared>,
    Query(q): Query<ListQuery>,
) -> ApiResult<Json<serde_json::Value>> {
    let limit = q.limit.unwrap_or(50).clamp(1, 100);
    let offset = q.offset.unwrap_or(0).max(0);
    let mut games = {
        let conn = st.db.lock().await;
        db::list_games(&conn, q.q.as_deref(), limit, offset).map_err(sqlite_err)?
    };
    let live = st.rooms.players_by_game().await;
    for g in &mut games {
        g.players_now = Some(live.get(&g.id).copied().unwrap_or(0));
    }
    Ok(Json(json!({ "games": games })))
}

pub async fn get_game(
    State(st): State<Shared>,
    Path(id): Path<String>,
) -> ApiResult<Json<serde_json::Value>> {
    let mut game = {
        let conn = st.db.lock().await;
        db::get_game(&conn, &id).map_err(sqlite_err)?
    }
    .ok_or_else(|| ApiError::not_found("no such game"))?;
    let live = st.rooms.players_by_game().await;
    game.players_now = Some(live.get(&id).copied().unwrap_or(0));
    Ok(Json(json!({ "game": game })))
}

/// The scene/scripts/models the play page seeds into localStorage before boot.
pub async fn get_bundle(
    State(st): State<Shared>,
    Path(id): Path<String>,
) -> ApiResult<Json<bundle::BundleDownload>> {
    let sha = current_sha(&st, &id).await?;
    let b = bundle::load(&st.cfg.bundles_dir, &sha)
        .map_err(|e| ApiError::internal(format!("reading bundle {sha}: {e}")))?;
    Ok(Json(b))
}

async fn current_sha(st: &Shared, id: &str) -> ApiResult<String> {
    let conn = st.db.lock().await;
    let game = db::get_game(&conn, id)
        .map_err(sqlite_err)?
        .ok_or_else(|| ApiError::not_found("no such game"))?;
    let version = game
        .version
        .ok_or_else(|| ApiError::not_found("game has no published version"))?;
    db::bundle_sha(&conn, id, version)
        .map_err(sqlite_err)?
        .ok_or_else(|| ApiError::internal("version row has no bundle"))
}

// ---------------------------------------------------------------- publish

#[derive(Debug, Serialize)]
pub struct PublishResponse {
    pub id: String,
    pub version: i64,
    /// Returned exactly once, on create. The only way to edit the game later, so
    /// the editor has to store it — there is no recovery path until OAuth.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub edit_key: Option<String>,
}

fn hash_key(key: &str) -> String {
    Sha256::digest(key.as_bytes()).iter().map(|b| format!("{b:02x}")).collect()
}

fn new_edit_key() -> String {
    use rand::RngCore;
    let mut bytes = [0u8; 32];
    rand::thread_rng().fill_bytes(&mut bytes);
    bytes.iter().map(|b| format!("{b:02x}")).collect()
}

/// Slug from the title plus a random suffix, so two "My Game"s can coexist and
/// an id never leaks a guessable relationship to another one.
fn make_id(title: &str) -> String {
    let mut slug: String = title
        .chars()
        .map(|c| if c.is_ascii_alphanumeric() { c.to_ascii_lowercase() } else { '-' })
        .collect();
    while slug.contains("--") {
        slug = slug.replace("--", "-");
    }
    let slug = slug.trim_matches('-').chars().take(32).collect::<String>();
    let suffix: String = crate::rooms::random_token().chars().take(6).collect();
    if slug.is_empty() {
        format!("game-{suffix}")
    } else {
        format!("{slug}-{suffix}")
    }
}

fn clean_title(t: &str) -> String {
    let t: String = t.chars().filter(|c| !c.is_control()).take(80).collect();
    let t = t.trim().to_string();
    if t.is_empty() { "Untitled".to_string() } else { t }
}

fn clean_visibility(v: Option<&str>) -> String {
    match v {
        Some("unlisted") => "unlisted".to_string(),
        _ => "public".to_string(),
    }
}

fn store_bundle(st: &Shared, up: &BundleUpload) -> ApiResult<(String, usize)> {
    let limits = Limits {
        max_total_bytes: st.cfg.max_bundle_bytes,
        max_files: st.cfg.max_bundle_files,
    };
    let (_canonical, sha, size) =
        bundle::validate(up, &limits).map_err(|e| ApiError::bad(e.to_string()))?;
    bundle::store(&st.cfg.bundles_dir, &sha, &up.files)
        .map_err(|e| ApiError::internal(format!("storing bundle: {e}")))?;
    Ok((sha, size))
}

pub async fn publish(
    State(st): State<Shared>,
    Json(up): Json<BundleUpload>,
) -> ApiResult<Json<PublishResponse>> {
    if !st.cfg.publish_open {
        return Err(ApiError::new(
            StatusCode::FORBIDDEN,
            "publishing is currently invite-only",
        ));
    }
    let (sha, size) = store_bundle(&st, &up)?;

    let title = clean_title(&up.title);
    let id = make_id(&title);
    let edit_key = new_edit_key();
    let visibility = clean_visibility(up.visibility.as_deref());

    let mut conn = st.db.lock().await;
    db::insert_game(&conn, &id, &title, &up.description, &hash_key(&edit_key), &visibility)
        .map_err(sqlite_err)?;
    let version = db::add_version(&mut conn, &id, &sha, size as i64).map_err(sqlite_err)?;

    println!("published {id} v{version} ({size} bytes, sha {})", &sha[..12]);
    Ok(Json(PublishResponse { id, version, edit_key: Some(edit_key) }))
}

async fn require_edit_key(st: &Shared, id: &str, headers: &HeaderMap) -> ApiResult<()> {
    let presented = headers
        .get("x-cow-edit-key")
        .and_then(|v| v.to_str().ok())
        .ok_or_else(|| ApiError::new(StatusCode::UNAUTHORIZED, "missing X-Cow-Edit-Key"))?;
    let stored = {
        let conn = st.db.lock().await;
        db::get_edit_key_hash(&conn, id).map_err(sqlite_err)?
    }
    .ok_or_else(|| ApiError::not_found("no such game"))?;
    if hash_key(presented) != stored {
        return Err(ApiError::new(StatusCode::FORBIDDEN, "wrong edit key"));
    }
    Ok(())
}

pub async fn republish(
    State(st): State<Shared>,
    Path(id): Path<String>,
    headers: HeaderMap,
    Json(up): Json<BundleUpload>,
) -> ApiResult<Json<PublishResponse>> {
    require_edit_key(&st, &id, &headers).await?;
    let (sha, size) = store_bundle(&st, &up)?;

    let mut conn = st.db.lock().await;
    let version = db::add_version(&mut conn, &id, &sha, size as i64).map_err(sqlite_err)?;
    if !up.title.is_empty() {
        db::update_meta(&conn, &id, Some(&clean_title(&up.title)), Some(&up.description), None)
            .map_err(sqlite_err)?;
    }
    println!("republished {id} v{version}");
    // Existing rooms keep serving the old version until they empty out; a new
    // join picks the new one. Kicking players mid-session to apply an edit would
    // be worse than letting the current world finish.
    Ok(Json(PublishResponse { id, version, edit_key: None }))
}

#[derive(Debug, Deserialize)]
pub struct PatchMeta {
    title: Option<String>,
    description: Option<String>,
    visibility: Option<String>,
}

pub async fn patch_game(
    State(st): State<Shared>,
    Path(id): Path<String>,
    headers: HeaderMap,
    Json(p): Json<PatchMeta>,
) -> ApiResult<StatusCode> {
    require_edit_key(&st, &id, &headers).await?;
    let title = p.title.map(|t| clean_title(&t));
    let visibility = p.visibility.map(|v| clean_visibility(Some(&v)));
    let conn = st.db.lock().await;
    db::update_meta(&conn, &id, title.as_deref(), p.description.as_deref(), visibility.as_deref())
        .map_err(sqlite_err)?;
    Ok(StatusCode::NO_CONTENT)
}

pub async fn delete_game(
    State(st): State<Shared>,
    Path(id): Path<String>,
    headers: HeaderMap,
) -> ApiResult<StatusCode> {
    require_edit_key(&st, &id, &headers).await?;
    {
        let conn = st.db.lock().await;
        db::delete_game(&conn, &id).map_err(sqlite_err)?;
    }
    // Bundles are content-addressed and may be shared with another game's
    // version, so unpublishing deliberately leaves them on disk. Reclaiming
    // orphans is a separate sweep, not something to get wrong under a delete.
    println!("unpublished {id}");
    Ok(StatusCode::NO_CONTENT)
}

// ---------------------------------------------------------------- join

#[derive(Debug, Serialize)]
pub struct JoinResponse {
    pub room: String,
    pub version: i64,
    pub ws: Option<String>,
    pub wt: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub cert_hash: Option<String>,
}

pub async fn join_game(
    State(st): State<Shared>,
    Path(id): Path<String>,
) -> ApiResult<Json<JoinResponse>> {
    let (version, sha, max_players) = {
        let conn = st.db.lock().await;
        let game = db::get_game(&conn, &id)
            .map_err(sqlite_err)?
            .ok_or_else(|| ApiError::not_found("no such game"))?;
        let version = game
            .version
            .ok_or_else(|| ApiError::not_found("game has no published version"))?;
        let sha = db::bundle_sha(&conn, &id, version)
            .map_err(sqlite_err)?
            .ok_or_else(|| ApiError::internal("version row has no bundle"))?;
        (version, sha, game.max_players as u32)
    };

    let dir = bundle::dir_for(&st.cfg.bundles_dir, &sha);
    if !dir.join(bundle::ENTRY_SCENE).exists() {
        return Err(ApiError::internal(format!("bundle {sha} is missing on disk")));
    }

    let token = st
        .rooms
        .join(&id, version, dir, max_players)
        .await
        .map_err(|e| match e {
            JoinError::AtCapacity => ApiError::new(StatusCode::SERVICE_UNAVAILABLE, e.to_string()),
            JoinError::StartFailed(_) => ApiError::internal(e.to_string()),
        })?;

    {
        let conn = st.db.lock().await;
        let _ = db::bump_play_count(&conn, &id);
    }

    Ok(Json(JoinResponse {
        room: token,
        version,
        ws: st.cfg.public_ws.clone(),
        wt: st.cfg.public_wt.clone(),
        cert_hash: st.cfg.public_cert_hash.clone(),
    }))
}

// ---------------------------------------------------------------- internal

/// Room token -> UDP port, for the sidecar. Bound to the internal network and
/// never routed by the reverse proxy: it turns a room token into a way to reach
/// a game server, so exposing it publicly would bypass the whole join flow.
pub async fn internal_room(
    State(st): State<Shared>,
    Path(token): Path<String>,
) -> ApiResult<Json<serde_json::Value>> {
    match st.rooms.port_of(&token).await {
        Some(port) => Ok(Json(json!({ "port": port }))),
        None => Err(ApiError::not_found("no such room")),
    }
}

/// Used by the tests to build an upload without going through the editor.
#[allow(dead_code)]
pub fn upload_from_files(title: &str, files: BTreeMap<String, String>) -> BundleUpload {
    BundleUpload {
        title: title.to_string(),
        description: String::new(),
        visibility: None,
        files,
    }
}
