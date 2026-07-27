// Publish-time validation and on-disk storage of game bundles.
//
// A bundle is the same `{path: contents}` shape the editor's exporter already
// produces (GameBuilder::stageDirectoryAndJson + Scene::saveToJSON), so nothing
// new has to be invented on the C++ side:
//
//   scenes/scene.json   the world
//   scripts/*.cow       gameplay
//   models/*.obj        meshes
//
// Everything here is text, so the wire format is plain JSON with no base64.
//
// SECURITY: this is the boundary where a stranger's content enters the VPS. Two
// separate concerns:
//   1. Path handling — a bundle must not be able to write outside its own
//      directory. Handled below, conservatively (allow-list, not deny-list).
//   2. Script execution — a room process *runs* the .cow files, and the language
//      has no execution budget yet, so `while true` pins a core. That is NOT
//      solved here; it is why publishing stays invite-only until the M2
//      hardening (instruction budget + setrlimit on the child) lands.

use std::collections::BTreeMap;
use std::path::{Path, PathBuf};

use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};

/// Upload envelope. `files` maps a bundle-relative path to its text contents.
#[derive(Debug, Deserialize)]
pub struct BundleUpload {
    #[serde(default)]
    pub title: String,
    #[serde(default)]
    pub description: String,
    #[serde(default)]
    pub visibility: Option<String>,
    pub files: BTreeMap<String, String>,
}

/// What the play page fetches to seed localStorage before the wasm boots.
#[derive(Debug, Serialize)]
pub struct BundleDownload {
    pub scene: String,
    pub scripts: BTreeMap<String, String>,
    pub models: BTreeMap<String, String>,
}

#[derive(Debug)]
pub struct Limits {
    pub max_total_bytes: usize,
    pub max_files: usize,
}

impl Default for Limits {
    fn default() -> Self {
        Limits { max_total_bytes: 8 * 1024 * 1024, max_files: 200 }
    }
}

pub const ENTRY_SCENE: &str = "scenes/scene.json";

#[derive(Debug)]
pub struct ValidationError(pub String);

impl std::fmt::Display for ValidationError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}", self.0)
    }
}

fn err<T>(msg: impl Into<String>) -> Result<T, ValidationError> {
    Err(ValidationError(msg.into()))
}

/// Accept only `<dir>/<name>.<ext>` under a known directory, with a conservative
/// character set. Written as an allow-list on purpose: `..` stripping and
/// canonicalisation are easy to get subtly wrong, and nothing legitimate the
/// editor produces needs anything outside this.
fn check_path(path: &str) -> Result<(), ValidationError> {
    if path.is_empty() || path.len() > 200 {
        return err(format!("bad path length: {path:?}"));
    }
    if path.contains('\\') || path.contains("..") || path.starts_with('/') {
        return err(format!("illegal path: {path:?}"));
    }
    if path.contains("//") || path.ends_with('/') {
        return err(format!("illegal path: {path:?}"));
    }
    for c in path.chars() {
        let ok = c.is_ascii_alphanumeric() || matches!(c, '_' | '-' | '.' | '/');
        if !ok {
            return err(format!("illegal character {c:?} in path {path:?}"));
        }
    }
    let (dir, ext) = match path.split_once('/') {
        Some((d, _)) => (d, path.rsplit_once('.').map(|(_, e)| e).unwrap_or("")),
        None => return err(format!("path must be inside scenes/, scripts/ or models/: {path:?}")),
    };
    let allowed = match dir {
        "scenes" => ext == "json",
        "scripts" => ext == "cow",
        "models" => ext == "obj",
        _ => false,
    };
    if !allowed {
        return err(format!("{path:?} is not an allowed <dir>/<file>.<ext> combination"));
    }
    Ok(())
}

/// Validate an upload and return (canonical_json, sha256_hex, total_bytes).
///
/// The canonical form is a BTreeMap serialisation, so the same content always
/// hashes the same regardless of upload order — that is what makes republishing
/// an unchanged project a no-op rather than a new stored copy.
pub fn validate(
    up: &BundleUpload,
    limits: &Limits,
) -> Result<(String, String, usize), ValidationError> {
    if up.files.len() > limits.max_files {
        return err(format!("too many files: {} (max {})", up.files.len(), limits.max_files));
    }
    let total: usize = up.files.iter().map(|(k, v)| k.len() + v.len()).sum();
    if total > limits.max_total_bytes {
        return err(format!(
            "bundle too large: {total} bytes (max {})",
            limits.max_total_bytes
        ));
    }
    for path in up.files.keys() {
        check_path(path)?;
    }

    let scene_src = match up.files.get(ENTRY_SCENE) {
        Some(s) => s,
        None => return err(format!("bundle must contain {ENTRY_SCENE}")),
    };
    let scene: serde_json::Value = serde_json::from_str(scene_src)
        .map_err(|e| ValidationError(format!("{ENTRY_SCENE} is not valid JSON: {e}")))?;
    let objects = match scene.get("objects").and_then(|o| o.as_array()) {
        Some(o) => o,
        None => return err(format!("{ENTRY_SCENE} has no \"objects\" array")),
    };

    // Every asset the scene names must be inside the bundle. A room process runs
    // with the bundle directory as its cwd, so a dangling reference would either
    // fail to load or — worse — silently resolve to something else on the host
    // through the engine's path-ladder fallback.
    for (i, obj) in objects.iter().enumerate() {
        let name = obj.get("name").and_then(|v| v.as_str()).unwrap_or("<unnamed>");
        if let Some(mesh) = obj.get("mesh").and_then(|v| v.as_str()) {
            if !up.files.contains_key(mesh) {
                return err(format!(
                    "object {i} ({name}) references mesh {mesh:?}, which is not in the bundle"
                ));
            }
        }
        // Both the current array form and the legacy single-string form.
        if let Some(scripts) = obj.get("scripts").and_then(|v| v.as_array()) {
            for s in scripts {
                if let Some(p) = s.as_str() {
                    if !up.files.contains_key(p) {
                        return err(format!(
                            "object {i} ({name}) references script {p:?}, which is not in the bundle"
                        ));
                    }
                }
            }
        }
        if let Some(p) = obj.get("script").and_then(|v| v.as_str()) {
            if !up.files.contains_key(p) {
                return err(format!(
                    "object {i} ({name}) references script {p:?}, which is not in the bundle"
                ));
            }
        }
    }

    let canonical = serde_json::to_string(&up.files)
        .map_err(|e| ValidationError(format!("could not canonicalise bundle: {e}")))?;
    let sha = hex(&Sha256::digest(canonical.as_bytes()));
    Ok((canonical, sha, total))
}

fn hex(bytes: &[u8]) -> String {
    bytes.iter().map(|b| format!("{b:02x}")).collect()
}

/// `<root>/<sha[0..2]>/<sha>` — fanned out so one directory never holds every
/// bundle ever published.
pub fn dir_for(root: &Path, sha: &str) -> PathBuf {
    root.join(&sha[..2]).join(sha)
}

/// Materialise a validated bundle on disk. Content-addressed, so an identical
/// republish finds the directory already there and does nothing.
pub fn store(root: &Path, sha: &str, files: &BTreeMap<String, String>) -> std::io::Result<PathBuf> {
    let dir = dir_for(root, sha);
    if dir.join(ENTRY_SCENE).exists() {
        return Ok(dir);
    }
    // Write to a temp dir and rename, so a crash mid-write can never leave a
    // half-populated bundle that looks complete to the check above.
    let staging = root.join(format!(".staging-{sha}"));
    let _ = std::fs::remove_dir_all(&staging);
    for (path, contents) in files {
        let target = staging.join(path);
        if let Some(parent) = target.parent() {
            std::fs::create_dir_all(parent)?;
        }
        std::fs::write(&target, contents)?;
    }
    if let Some(parent) = dir.parent() {
        std::fs::create_dir_all(parent)?;
    }
    match std::fs::rename(&staging, &dir) {
        Ok(()) => Ok(dir),
        Err(e) => {
            // Lost a race with a concurrent identical publish: the destination
            // already exists and is complete, which is exactly what we wanted.
            let _ = std::fs::remove_dir_all(&staging);
            if dir.join(ENTRY_SCENE).exists() {
                Ok(dir)
            } else {
                Err(e)
            }
        }
    }
}

/// Read a stored bundle back into the shape the play page seeds into localStorage.
pub fn load(root: &Path, sha: &str) -> std::io::Result<BundleDownload> {
    let dir = dir_for(root, sha);
    let scene = std::fs::read_to_string(dir.join(ENTRY_SCENE))?;
    let mut scripts = BTreeMap::new();
    let mut models = BTreeMap::new();
    for (sub, out) in [("scripts", &mut scripts), ("models", &mut models)] {
        let subdir = dir.join(sub);
        if !subdir.is_dir() {
            continue;
        }
        for entry in std::fs::read_dir(&subdir)? {
            let entry = entry?;
            if !entry.file_type()?.is_file() {
                continue;
            }
            let name = entry.file_name().to_string_lossy().to_string();
            out.insert(format!("{sub}/{name}"), std::fs::read_to_string(entry.path())?);
        }
    }
    Ok(BundleDownload { scene, scripts, models })
}

#[cfg(test)]
mod tests {
    use super::*;

    fn minimal_files() -> BTreeMap<String, String> {
        let mut f = BTreeMap::new();
        f.insert(
            ENTRY_SCENE.to_string(),
            r#"{"objects":[{"type":"Cube","name":"c","mass":1.0}]}"#.to_string(),
        );
        f
    }

    fn upload(files: BTreeMap<String, String>) -> BundleUpload {
        BundleUpload {
            title: "t".into(),
            description: String::new(),
            visibility: None,
            files,
        }
    }

    #[test]
    fn accepts_a_minimal_bundle() {
        let up = upload(minimal_files());
        let (_, sha, n) = validate(&up, &Limits::default()).expect("should validate");
        assert_eq!(sha.len(), 64);
        assert!(n > 0);
    }

    #[test]
    fn hash_is_order_independent() {
        let a = validate(&upload(minimal_files()), &Limits::default()).unwrap().1;
        let mut files = minimal_files();
        files.insert("scripts/z.cow".into(), "x".into());
        files.insert("scripts/a.cow".into(), "y".into());
        let b = validate(&upload(files.clone()), &Limits::default()).unwrap().1;
        let c = validate(&upload(files), &Limits::default()).unwrap().1;
        assert_ne!(a, b, "different content must hash differently");
        assert_eq!(b, c, "same content must hash identically");
    }

    #[test]
    fn rejects_path_traversal() {
        for bad in [
            "../etc/passwd",
            "scripts/../../etc/passwd",
            "/etc/passwd",
            "scripts/..%2fx.cow",
            "scripts\\x.cow",
            "scenes/scene.json/",
            "scripts//x.cow",
        ] {
            let mut files = minimal_files();
            files.insert(bad.to_string(), "x".into());
            let r = validate(&upload(files), &Limits::default());
            assert!(r.is_err(), "should have rejected {bad:?}");
        }
    }

    #[test]
    fn rejects_unknown_dirs_and_extensions() {
        for bad in ["etc/passwd.json", "scripts/x.sh", "models/x.so", "x.cow", "scenes/x.cow"] {
            let mut files = minimal_files();
            files.insert(bad.to_string(), "x".into());
            assert!(
                validate(&upload(files), &Limits::default()).is_err(),
                "should have rejected {bad:?}"
            );
        }
    }

    #[test]
    fn requires_the_entry_scene() {
        let mut files = BTreeMap::new();
        files.insert("scripts/a.cow".to_string(), "x".to_string());
        assert!(validate(&upload(files), &Limits::default()).is_err());
    }

    #[test]
    fn rejects_dangling_asset_references() {
        let mut files = BTreeMap::new();
        files.insert(
            ENTRY_SCENE.to_string(),
            r#"{"objects":[{"name":"c","mesh":"models/gone.obj"}]}"#.to_string(),
        );
        let e = validate(&upload(files), &Limits::default()).unwrap_err();
        assert!(e.0.contains("models/gone.obj"), "{}", e.0);

        let mut files = BTreeMap::new();
        files.insert(
            ENTRY_SCENE.to_string(),
            r#"{"objects":[{"name":"c","scripts":["scripts/gone.cow"]}]}"#.to_string(),
        );
        let e = validate(&upload(files), &Limits::default()).unwrap_err();
        assert!(e.0.contains("scripts/gone.cow"), "{}", e.0);
    }

    #[test]
    fn accepts_resolved_asset_references() {
        let mut files = BTreeMap::new();
        files.insert(
            ENTRY_SCENE.to_string(),
            r#"{"objects":[{"name":"c","mesh":"models/cow.obj","scripts":["scripts/a.cow"]}]}"#
                .to_string(),
        );
        files.insert("models/cow.obj".into(), "v 0 0 0\n".into());
        files.insert("scripts/a.cow".into(), "fn update() {}\n".into());
        assert!(validate(&upload(files), &Limits::default()).is_ok());
    }

    #[test]
    fn rejects_malformed_scene_json() {
        let mut files = BTreeMap::new();
        files.insert(ENTRY_SCENE.to_string(), "{not json".to_string());
        assert!(validate(&upload(files), &Limits::default()).is_err());

        let mut files = BTreeMap::new();
        files.insert(ENTRY_SCENE.to_string(), r#"{"nope":[]}"#.to_string());
        assert!(validate(&upload(files), &Limits::default()).is_err());
    }

    #[test]
    fn enforces_limits() {
        let small = Limits { max_total_bytes: 64, max_files: 200 };
        let mut files = minimal_files();
        files.insert("scripts/big.cow".into(), "x".repeat(1000));
        assert!(validate(&upload(files), &small).is_err());

        let few = Limits { max_total_bytes: 8 << 20, max_files: 1 };
        let mut files = minimal_files();
        files.insert("scripts/a.cow".into(), "x".into());
        assert!(validate(&upload(files), &few).is_err());
    }
}
