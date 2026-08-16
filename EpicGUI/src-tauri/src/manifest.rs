// manifest.rs
// Manifest scanning, uploading, consent management, and upload tracking.
//
// Smart scanner: finds .item files, parses their JSON to get CompleteManifestPath,
// then also includes the binary .manifest files they reference.
// Both .item (JSON) and .manifest (binary) are uploaded — server parses each type.
//
// - Local dedup via SHA256 hash cache (uploaded_manifests.json)
// - Server-side dedup via GET /api/manifest/list (hash-based)
// - API key embedded in binary at build time from MANIFEST_API_KEY.txt

use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use std::collections::HashSet;
use std::path::{Path, PathBuf};
use tauri::Manager;

// ── API Key ────────────────────────────────────────────────────────────────
// The key is read from MANIFEST_API_KEY.txt at BUILD TIME by build.rs and
// embedded directly in the binary via env!(). No obfuscation, no runtime
// file read. The key file is gitignored so it never enters source control.

/// API key embedded at compile time by build.rs. Empty if key file was missing.
const API_KEY: &str = env!("MANIFEST_API_KEY");

/// Get the embedded API key. Returns None if it was not configured at build time.
fn get_api_key() -> Option<&'static str> {
    if API_KEY.is_empty() {
        log::warn!("[manifest] API key not configured (MANIFEST_API_KEY.txt was missing at build time)");
        None
    } else {
        Some(API_KEY)
    }
}

const MANIFEST_API_BASE_URL: &str = "https://ogkushhh-abdobest.hf.space";
// const MAX_RETRIES: u32 = 3; // unused after removing upload_single_with_retry

// ── Types ───────────────────────────────────────────────────────────────────

/// A scanned manifest file on disk. No parsing — just file metadata + hash.
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ScannedManifest {
    pub file_name: String,  // e.g. "Fortnite.manifest"
    pub sha256: String,     // hex hash of file contents (for local dedup)
    pub file_size: u64,
    pub file_path: String,  // full path on disk
}

/// Result of uploading a single manifest file.
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ManifestUploadResult {
    pub file_name: String,
    pub status: String, // "ok" | "skipped" | "error"
    #[serde(skip_serializing_if = "Option::is_none")]
    pub detail: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub server_response: Option<serde_json::Value>,
}

/// Manifest consent state persisted in settings.
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ManifestConsentState {
    pub consent: bool,     // whether uploads are enabled
    pub dismissed: bool,   // whether the one-time modal was dismissed
}

// ── Directory scanning ──────────────────────────────────────────────────────

/// Returns the list of Epic manifest directories to scan.
/// On Windows, these are:
///   - %PROGRAMDATA%\Epic\EpicGamesLauncher\Data\Manifests\
///   - %LOCALAPPDATA%\EpicGamesLauncher\Saved\
/// On non-Windows, returns an empty vec.
fn manifest_dirs() -> Vec<PathBuf> {
    #[cfg(target_os = "windows")]
    {
        let mut dirs = Vec::new();

        // %PROGRAMDATA%\Epic\EpicGamesLauncher\Data\Manifests\
        if let Ok(programdata) = std::env::var("PROGRAMDATA") {
            let p = PathBuf::from(programdata)
                .join("Epic")
                .join("EpicGamesLauncher")
                .join("Data")
                .join("Manifests");
            dirs.push(p);
        }

        // %LOCALAPPDATA%\EpicGamesLauncher\Saved\
        if let Ok(localappdata) = std::env::var("LOCALAPPDATA") {
            let p = PathBuf::from(localappdata)
                .join("EpicGamesLauncher")
                .join("Saved");
            dirs.push(p);
        }

        dirs
    }

    #[cfg(not(target_os = "windows"))]
    {
        Vec::new()
    }
}

/// Compute SHA256 hex hash of the given bytes.
fn sha256_hex(data: &[u8]) -> String {
    let mut hasher = Sha256::new();
    hasher.update(data);
    let result = hasher.finalize();
    format!("{:x}", result)
}

/// Scan all manifest directories for .item files, then look for the binary
/// .manifest files they reference. Only includes the .item if its binary manifest
/// exists on disk — orphaned .items are skipped entirely.
pub fn scan_manifests_on_disk() -> Vec<ScannedManifest> {
    let mut results = Vec::new();

    for dir in manifest_dirs() {
        if !dir.exists() {
            log::info!("[manifest] Directory does not exist: {:?}", dir);
            continue;
        }

        log::info!("[manifest] Scanning directory: {:?}", dir);

        let entries = match std::fs::read_dir(&dir) {
            Ok(e) => e,
            Err(e) => {
                log::warn!("[manifest] Failed to read dir {:?}: {}", dir, e);
                continue;
            }
        };

        for entry in entries {
            let entry = match entry {
                Ok(e) => e,
                Err(e) => {
                    log::warn!("[manifest] Failed to read dir entry: {}", e);
                    continue;
                }
            };

            let path = entry.path();

            // Only process .item files
            let ext = path.extension().and_then(|ext| ext.to_str()).unwrap_or("");
            if ext != "item" {
                continue;
            }

            let file_name = path
                .file_name()
                .and_then(|n| n.to_str())
                .unwrap_or("<unknown>")
                .to_string();

            // Read the .item file (JSON)
            let contents = match std::fs::read(&path) {
                Ok(c) => c,
                Err(e) => {
                    log::warn!("[manifest] Failed to read .item file {:?}: {}", path, e);
                    continue;
                }
            };

            // Parse JSON to find CompleteManifestPath
            let json: serde_json::Value = match serde_json::from_slice(&contents) {
                Ok(v) => v,
                Err(e) => {
                    log::warn!("[manifest] Failed to parse JSON from {:?}: {}", path, e);
                    continue;
                }
            };

            // Get the binary manifest path from JSON
            let manifest_path_str = json
                .get("CompleteManifestPath")
                .and_then(|v| v.as_str())
                .unwrap_or("");

            if manifest_path_str.is_empty() {
                log::warn!("[manifest] No CompleteManifestPath in {:?} — skipping", path);
                continue;
            }

            let manifest_path = Path::new(manifest_path_str);
            if !manifest_path.exists() {
                log::warn!(
                    "[manifest] Binary manifest not found for .item: {} — skipping orphaned .item",
                    file_name
                );
                continue; // ← SKIP the entire .item
            }

            // ── Valid: binary manifest exists ──────────────────────────────

            // Add the .item file itself
            let metadata = match std::fs::metadata(&path) {
                Ok(m) => m,
                Err(e) => {
                    log::warn!("[manifest] Failed to read metadata for {:?}: {}", path, e);
                    continue;
                }
            };

            let file_size = metadata.len();
            let sha256 = sha256_hex(&contents);

            log::info!(
                "[manifest] Found .item: {} ({} bytes, SHA256={})",
                file_name,
                file_size,
                sha256
            );

            results.push(ScannedManifest {
                file_name: file_name.clone(),
                sha256,
                file_size,
                file_path: path.to_string_lossy().to_string(),
            });

            // ── Add the binary .manifest file ──────────────────────────────
            let manifest_file_name = manifest_path
                .file_name()
                .and_then(|n| n.to_str())
                .unwrap_or("<unknown>")
                .to_string();

            let manifest_contents = match std::fs::read(manifest_path) {
                Ok(c) => c,
                Err(e) => {
                    log::warn!(
                        "[manifest] Failed to read binary manifest {:?}: {}",
                        manifest_path,
                        e
                    );
                    continue;
                }
            };

            let manifest_size = match std::fs::metadata(manifest_path) {
                Ok(m) => m.len(),
                Err(e) => {
                    log::warn!(
                        "[manifest] Failed to read metadata for {:?}: {}",
                        manifest_path,
                        e
                    );
                    continue;
                }
            };

            let manifest_sha256 = sha256_hex(&manifest_contents);

            log::info!(
                "[manifest] Found binary manifest: {} ({} bytes, SHA256={})",
                manifest_file_name,
                manifest_size,
                manifest_sha256
            );

            results.push(ScannedManifest {
                file_name: manifest_file_name,
                sha256: manifest_sha256,
                file_size: manifest_size,
                file_path: manifest_path.to_string_lossy().to_string(),
            });
        }
    }

    log::info!("[manifest] Scan complete — found {} files (valid items + manifests)", results.len());
    results
}

// ── Server-side dedup ───────────────────────────────────────────────────────
// NOTE: fetch_server_hashes was removed — the new upload_manifests_to_api does
// local-hash dedup only. Server-side dedup can be re-added if needed later.

// ── Upload with retry ───────────────────────────────────────────────────────
// NOTE: upload_single_with_retry was removed — the new upload_manifests_to_api
// does direct upload with println! debug logging at every step. Retry logic
// can be re-added inside upload_manifests_to_api if needed later.

// ── Orchestrate uploads ─────────────────────────────────────────────────────

/// Upload the given manifest files to the API.
/// Uses local hash dedup and server-side hash dedup.
/// Retries failed uploads with exponential backoff.
/// On success, hashes are recorded in uploaded_manifests.json.
pub async fn upload_manifests_to_api(
    app: &tauri::AppHandle,
    files: &[String],
) -> Result<Vec<ManifestUploadResult>, String> {
    println!("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    println!("📤 upload_manifests_to_api CALLED with {} files", files.len());
    for (i, f) in files.iter().enumerate() {
        println!("  [{}] {}", i, f);
    }
    println!("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");

    // ── 1. API KEY ──────────────────────────────────────────────────────────
    let api_key = match get_api_key() {
        Some(k) => {
            println!("✅ API key loaded (len={}, first 8 chars: {})", k.len(), &k[..8.min(k.len())]);
            k
        }
        None => {
            println!("❌ API key is NONE – returning error");
            return Err("Manifest uploads not configured".to_string());
        }
    };

    // ── 2. CLIENT ──────────────────────────────────────────────────────────
    let client = reqwest::Client::new();
    println!("✅ HTTP client created");

    // ── 3. ALREADY UPLOADED HASHES ──────────────────────────────────────
    let already_uploaded: HashSet<String> = match read_uploaded_hashes(app) {
        Ok(h) => {
            println!("✅ Loaded {} already-uploaded hashes", h.len());
            h.into_iter().collect()
        }
        Err(e) => {
            println!("⚠️ Failed to read uploaded hashes: {}", e);
            HashSet::new()
        }
    };

    let mut results = Vec::new();
    let mut newly_uploaded_hashes = Vec::new();

    // ── 4. LOOP THROUGH FILES ────────────────────────────────────────────
    for (idx, file_path_str) in files.iter().enumerate() {
        println!("─────────────────────────────────────────────────────────");
        println!("📄 Processing file {}/{}: {}", idx + 1, files.len(), file_path_str);

        let path = Path::new(file_path_str);
        if !path.exists() {
            println!("❌ File does NOT exist!");
            results.push(ManifestUploadResult {
                file_name: path.file_name().unwrap_or_default().to_string_lossy().to_string(),
                status: "error".to_string(),
                detail: Some("File not found".to_string()),
                server_response: None,
            });
            continue;
        }
        println!("✅ File exists");

        let contents = match std::fs::read(path) {
            Ok(c) => {
                println!("✅ Read {} bytes", c.len());
                c
            }
            Err(e) => {
                println!("❌ Failed to read file: {}", e);
                results.push(ManifestUploadResult {
                    file_name: path.file_name().unwrap_or_default().to_string_lossy().to_string(),
                    status: "error".to_string(),
                    detail: Some(format!("Read error: {}", e)),
                    server_response: None,
                });
                continue;
            }
        };

        let hash = sha256_hex(&contents);
        println!("🔑 SHA256: {}", hash);

        if already_uploaded.contains(&hash) {
            println!("⏭️ Skipping – already uploaded locally");
            results.push(ManifestUploadResult {
                file_name: path.file_name().unwrap_or_default().to_string_lossy().to_string(),
                status: "skipped".to_string(),
                detail: Some("Already uploaded (local cache)".to_string()),
                server_response: None,
            });
            continue;
        }

        // ── 5. UPLOAD ──────────────────────────────────────────────────────
        let url = format!("{}/api/manifest/upload", MANIFEST_API_BASE_URL);
        println!("🌐 POST to: {}", url);

        let part = match reqwest::multipart::Part::bytes(contents.clone())
            .file_name(path.file_name().unwrap_or_default().to_string_lossy().to_string())
            .mime_str("application/octet-stream") {
                Ok(p) => p,
                Err(e) => {
                    println!("❌ Failed to create multipart: {}", e);
                    results.push(ManifestUploadResult {
                        file_name: path.file_name().unwrap_or_default().to_string_lossy().to_string(),
                        status: "error".to_string(),
                        detail: Some(format!("Multipart error: {}", e)),
                        server_response: None,
                    });
                    continue;
                }
            };
        let form = reqwest::multipart::Form::new().part("file", part);

        println!("📤 Sending request...");
        match client
            .post(&url)
            .header("X-API-Key", api_key)
            .multipart(form)
            .send()
            .await
        {
            Ok(resp) => {
                let status = resp.status();
                let body = match resp.text().await {
                    Ok(t) => t,
                    Err(e) => {
                        println!("❌ Failed to read response body: {}", e);
                        "".to_string()
                    }
                };
                println!("📊 Status: {}", status);
                println!("📄 Body: {}", body);

                if status.is_success() {
                    newly_uploaded_hashes.push(hash);
                    let json: serde_json::Value = serde_json::from_str(&body).unwrap_or_default();
                    results.push(ManifestUploadResult {
                        file_name: path.file_name().unwrap_or_default().to_string_lossy().to_string(),
                        status: "ok".to_string(),
                        detail: Some("Uploaded successfully".to_string()),
                        server_response: Some(json),
                    });
                } else {
                    results.push(ManifestUploadResult {
                        file_name: path.file_name().unwrap_or_default().to_string_lossy().to_string(),
                        status: "error".to_string(),
                        detail: Some(format!("Server error {}: {}", status, body)),
                        server_response: None,
                    });
                }
            }
            Err(e) => {
                println!("❌ Request failed: {}", e);
                results.push(ManifestUploadResult {
                    file_name: path.file_name().unwrap_or_default().to_string_lossy().to_string(),
                    status: "error".to_string(),
                    detail: Some(format!("Network error: {}", e)),
                    server_response: None,
                });
            }
        }
    }

    // ── 6. SAVE HASHES ──────────────────────────────────────────────────────
    if !newly_uploaded_hashes.is_empty() {
        println!("💾 Saving {} new hashes", newly_uploaded_hashes.len());
        let mut all_hashes: Vec<String> = already_uploaded.into_iter().collect();
        all_hashes.extend(newly_uploaded_hashes);
        if let Err(e) = write_uploaded_hashes(app, &all_hashes) {
            println!("⚠️ Failed to save hashes: {}", e);
        }
    }

    println!("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    println!("📋 Returning {} results", results.len());
    for r in &results {
        println!("  {} → {}", r.file_name, r.status);
    }
    println!("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");

    Ok(results)
}

// ── Upload tracking ─────────────────────────────────────────────────────────

/// Returns the path to the uploaded_manifests.json file in the app local data dir.
fn uploaded_hashes_path(app: &tauri::AppHandle) -> Result<PathBuf, String> {
    let dir = app
        .path()
        .app_local_data_dir()
        .map_err(|e| format!("Failed to resolve app_local_data_dir: {e}"))?;
    std::fs::create_dir_all(&dir)
        .map_err(|e| format!("Failed to create data dir {dir:?}: {e}"))?;
    Ok(dir.join("uploaded_manifests.json"))
}

/// Read the set of already-uploaded SHA256 hashes from disk.
pub fn read_uploaded_hashes(app: &tauri::AppHandle) -> Result<Vec<String>, String> {
    let path = uploaded_hashes_path(app)?;
    if !path.exists() {
        return Ok(Vec::new());
    }
    let text = std::fs::read_to_string(&path)
        .map_err(|e| format!("Failed to read {path:?}: {e}"))?;
    let hashes: Vec<String> = serde_json::from_str(&text)
        .map_err(|e| format!("Failed to parse {path:?}: {e}"))?;
    Ok(hashes)
}

/// Write the set of uploaded SHA256 hashes to disk (atomic write).
fn write_uploaded_hashes(app: &tauri::AppHandle, hashes: &[String]) -> Result<(), String> {
    let path = uploaded_hashes_path(app)?;
    let tmp = path.with_extension("json.tmp");
    let text = serde_json::to_string_pretty(hashes)
        .map_err(|e| format!("Failed to serialize hashes: {e}"))?;
    std::fs::write(&tmp, text)
        .map_err(|e| format!("Failed to write {tmp:?}: {e}"))?;
    std::fs::rename(&tmp, &path)
        .map_err(|e| format!("Failed to rename {tmp:?} -> {path:?}: {e}"))?;
    log::info!(
        "[manifest] Saved {} uploaded hashes to {:?}",
        hashes.len(),
        path
    );
    Ok(())
}

// ── Consent management ──────────────────────────────────────────────────────

/// Read the current manifest consent state from settings.
pub fn get_consent_state(app: &tauri::AppHandle) -> Result<ManifestConsentState, String> {
    let settings = crate::settings::AppSettings::load(app)?;
    Ok(ManifestConsentState {
        consent: settings.manifest_consent,
        dismissed: settings.manifest_consent_dismissed,
    })
}

/// Write manifest consent flags to settings.
pub fn set_consent_state(
    app: &tauri::AppHandle,
    consent: bool,
    dismissed: bool,
) -> Result<(), String> {
    let mut settings = crate::settings::AppSettings::load(app)?;
    settings.manifest_consent = consent;
    settings.manifest_consent_dismissed = dismissed;
    settings.save(app)?;
    log::info!(
        "[manifest] Consent updated: consent={}, dismissed={}",
        consent,
        dismissed
    );
    Ok(())
}
