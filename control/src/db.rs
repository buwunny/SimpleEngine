// SQLite schema and the small query helpers the API layer needs.
//
// Identity note: `owner_id` is nullable everywhere and unused today — publishing
// is anonymous and authorised by a secret edit key. When GitHub OAuth lands it
// fills that column in and nothing else has to move, which is the whole reason
// it exists now.

use rusqlite::{params, Connection, OptionalExtension, Row};
use serde::Serialize;

pub const SCHEMA: &str = r#"
PRAGMA journal_mode = WAL;
PRAGMA foreign_keys = ON;

CREATE TABLE IF NOT EXISTS games (
  id              TEXT PRIMARY KEY,
  title           TEXT NOT NULL,
  description     TEXT NOT NULL DEFAULT '',
  owner_id        TEXT,
  edit_key_hash   TEXT NOT NULL,
  current_version INTEGER,
  visibility      TEXT NOT NULL DEFAULT 'public',
  max_players     INTEGER NOT NULL DEFAULT 16,
  play_count      INTEGER NOT NULL DEFAULT 0,
  created_at      INTEGER NOT NULL,
  updated_at      INTEGER NOT NULL
);

CREATE TABLE IF NOT EXISTS game_versions (
  game_id    TEXT NOT NULL REFERENCES games(id) ON DELETE CASCADE,
  version    INTEGER NOT NULL,
  bundle_sha TEXT NOT NULL,
  size_bytes INTEGER NOT NULL,
  created_at INTEGER NOT NULL,
  PRIMARY KEY (game_id, version)
);

CREATE INDEX IF NOT EXISTS games_visibility_updated
  ON games(visibility, updated_at DESC);
"#;

/// Rooms are deliberately *not* persisted. They are child processes of this
/// process: if it restarts they are gone, so a table would only ever hold rows
/// describing rooms that no longer exist. RoomManager keeps them in memory.
pub fn open(path: &str) -> rusqlite::Result<Connection> {
    let conn = Connection::open(path)?;
    conn.execute_batch(SCHEMA)?;
    Ok(conn)
}

pub fn now() -> i64 {
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.as_secs() as i64)
        .unwrap_or(0)
}

#[derive(Debug, Serialize)]
pub struct GameRow {
    pub id: String,
    pub title: String,
    pub description: String,
    pub version: Option<i64>,
    pub visibility: String,
    pub max_players: i64,
    pub play_count: i64,
    pub created_at: i64,
    pub updated_at: i64,
    /// Filled in by the API layer from live room state, not stored.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub players_now: Option<u32>,
}

impl GameRow {
    fn from_row(r: &Row) -> rusqlite::Result<Self> {
        Ok(GameRow {
            id: r.get("id")?,
            title: r.get("title")?,
            description: r.get("description")?,
            version: r.get("current_version")?,
            visibility: r.get("visibility")?,
            max_players: r.get("max_players")?,
            play_count: r.get("play_count")?,
            created_at: r.get("created_at")?,
            updated_at: r.get("updated_at")?,
            players_now: None,
        })
    }
}

const GAME_COLS: &str = "id, title, description, current_version, visibility, \
                         max_players, play_count, created_at, updated_at";

/// Public catalog listing. Only games that are `public` *and* have a published
/// version appear — a game row with `current_version IS NULL` is a publish that
/// failed partway and would 404 on join.
pub fn list_games(
    conn: &Connection,
    query: Option<&str>,
    limit: i64,
    offset: i64,
) -> rusqlite::Result<Vec<GameRow>> {
    let like = query
        .map(|q| format!("%{}%", q.replace('%', "\\%").replace('_', "\\_")))
        .unwrap_or_else(|| "%".to_string());
    let sql = format!(
        "SELECT {GAME_COLS} FROM games
          WHERE visibility = 'public' AND current_version IS NOT NULL
            AND (title LIKE ?1 ESCAPE '\\' OR description LIKE ?1 ESCAPE '\\')
          ORDER BY updated_at DESC LIMIT ?2 OFFSET ?3"
    );
    let mut stmt = conn.prepare(&sql)?;
    let rows = stmt.query_map(params![like, limit, offset], GameRow::from_row)?;
    rows.collect()
}

pub fn get_game(conn: &Connection, id: &str) -> rusqlite::Result<Option<GameRow>> {
    let sql = format!("SELECT {GAME_COLS} FROM games WHERE id = ?1");
    conn.query_row(&sql, params![id], GameRow::from_row).optional()
}

pub fn get_edit_key_hash(conn: &Connection, id: &str) -> rusqlite::Result<Option<String>> {
    conn.query_row(
        "SELECT edit_key_hash FROM games WHERE id = ?1",
        params![id],
        |r| r.get(0),
    )
    .optional()
}

/// Path component for a version's bundle, e.g. `("ab", "abcdef...")`.
pub fn bundle_sha(conn: &Connection, id: &str, version: i64) -> rusqlite::Result<Option<String>> {
    conn.query_row(
        "SELECT bundle_sha FROM game_versions WHERE game_id = ?1 AND version = ?2",
        params![id, version],
        |r| r.get(0),
    )
    .optional()
}

pub fn insert_game(
    conn: &Connection,
    id: &str,
    title: &str,
    description: &str,
    edit_key_hash: &str,
    visibility: &str,
) -> rusqlite::Result<()> {
    let t = now();
    conn.execute(
        "INSERT INTO games (id, title, description, owner_id, edit_key_hash,
                            current_version, visibility, created_at, updated_at)
         VALUES (?1, ?2, ?3, NULL, ?4, NULL, ?5, ?6, ?6)",
        params![id, title, description, edit_key_hash, visibility, t],
    )?;
    Ok(())
}

/// Record a new bundle and make it current. Returns the version number.
pub fn add_version(
    conn: &mut Connection,
    id: &str,
    sha: &str,
    size_bytes: i64,
) -> rusqlite::Result<i64> {
    let tx = conn.transaction()?;
    let next: i64 = tx.query_row(
        "SELECT COALESCE(MAX(version), 0) + 1 FROM game_versions WHERE game_id = ?1",
        params![id],
        |r| r.get(0),
    )?;
    let t = now();
    tx.execute(
        "INSERT INTO game_versions (game_id, version, bundle_sha, size_bytes, created_at)
         VALUES (?1, ?2, ?3, ?4, ?5)",
        params![id, next, sha, size_bytes, t],
    )?;
    tx.execute(
        "UPDATE games SET current_version = ?2, updated_at = ?3 WHERE id = ?1",
        params![id, next, t],
    )?;
    tx.commit()?;
    Ok(next)
}

pub fn update_meta(
    conn: &Connection,
    id: &str,
    title: Option<&str>,
    description: Option<&str>,
    visibility: Option<&str>,
) -> rusqlite::Result<()> {
    conn.execute(
        "UPDATE games SET
           title       = COALESCE(?2, title),
           description = COALESCE(?3, description),
           visibility  = COALESCE(?4, visibility),
           updated_at  = ?5
         WHERE id = ?1",
        params![id, title, description, visibility, now()],
    )?;
    Ok(())
}

pub fn delete_game(conn: &Connection, id: &str) -> rusqlite::Result<()> {
    conn.execute("DELETE FROM games WHERE id = ?1", params![id])?;
    Ok(())
}

pub fn bump_play_count(conn: &Connection, id: &str) -> rusqlite::Result<()> {
    conn.execute(
        "UPDATE games SET play_count = play_count + 1 WHERE id = ?1",
        params![id],
    )?;
    Ok(())
}
