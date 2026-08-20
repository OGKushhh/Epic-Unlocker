// manifest.rs
// Manifest scanning, uploading, consent management, and upload tracking.
//
// Smart scanner: finds .item files, parses their JSON to get CompleteManifestPath,
// then also includes the binary .manifest files they reference.
// Both .item (JSON) and .manifest (binary) are uploaded — server parses each type.
//
// Smart uploader:
//   - ≤100MB total → batch (current behavior)
//   - >100MB total → sequential with 2s delay between uploads
//   - Single file >100MB → chunked upload (10MB chunks via /api/manifest/upload/chunk)
//   - Server auto-completes on last chunk — no separate /complete endpoint needed
//   - Progress events emitted to frontend via Tauri events
//
// - Local dedup via SHA256 hash cache (uploaded_manifests.json)
// - API key embedded in binary at build time from MANIFEST_API_KEY.txt

use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use std::collections::HashSet;
use std::path::{Path, PathBuf};
use tauri::{Emitter, Manager};

// ── Constants ──────────────────────────────────────────────────────────────

const MANIFEST_API_BASE_URL: &str = "https://ogkushhh-abdobest.hf.space";
const BATCH_SIZE_LIMIT: u64 = 100 * 1024 * 1024; // 100MB
const CHUNK_SIZE: usize = 10 * 1024 * 1024; // 10MB chunks
const UPLOAD_DELAY_SECS: u64 = 2; // delay between sequential uploads
const MAX_RETRIES: u32 = 3;

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

/// Progress update emitted to the frontend via Tauri event.
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct UploadProgress {
    pub total_files: u32,
    pub completed_files: u32,
    pub current_file: String,
    pub current_file_percent: u32,  // 0-100 for current file (chunk progress)
    pub overall_percent: u32,       // 0-100 overall
    pub status: String,             // "scanning" | "uploading" | "chunking" | "done" | "error"
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
                continue;
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

// ── Progress helper ─────────────────────────────────────────────────────────

/// Emit a progress event to the frontend.
fn emit_progress(app: &tauri::AppHandle, progress: &UploadProgress) {
    let _ = app.emit("manifest-upload-progress", progress);
    log::info!(
        "[manifest] Progress: {}/{} files, {}% overall — {} ({})",
        progress.completed_files,
        progress.total_files,
        progress.overall_percent,
        progress.current_file,
        progress.status
    );
}

// ── Single file upload (with retry) ────────────────────────────────────────

/// Upload a single file to the API with exponential backoff retry.
async fn upload_single_file(
    client: &reqwest::Client,
    api_key: &str,
    file_path: &Path,
) -> ManifestUploadResult {
    let file_name = file_path
        .file_name()
        .unwrap_or_default()
        .to_string_lossy()
        .to_string();

    let contents = match std::fs::read(file_path) {
        Ok(c) => c,
        Err(e) => {
            return ManifestUploadResult {
                file_name,
                status: "error".to_string(),
                detail: Some(format!("Read error: {}", e)),
                server_response: None,
            };
        }
    };

    let url = format!("{}/api/manifest/upload", MANIFEST_API_BASE_URL);

    for attempt in 0..=MAX_RETRIES {
        if attempt > 0 {
            let delay = std::time::Duration::from_millis(500 * 2u64.pow(attempt - 1));
            log::info!("[manifest] Retry {} for {} (waiting {:?})", attempt, file_name, delay);
            tokio::time::sleep(delay).await;
        }

        let part = reqwest::multipart::Part::bytes(contents.clone())
            .file_name(file_name.clone())
            .mime_str("application/octet-stream")
            .unwrap_or_else(|_| {
                reqwest::multipart::Part::bytes(contents.clone())
                    .file_name(file_name.clone())
            });
        let source_path_str = file_path.to_string_lossy().to_string();
        let form = reqwest::multipart::Form::new()
            .part("file", part)
            .text("source_path", source_path_str);

        match client
            .post(&url)
            .header("X-API-Key", api_key)
            .multipart(form)
            .send()
            .await
        {
            Ok(resp) => {
                let status = resp.status();
                let body = resp.text().await.unwrap_or_default();

                if status.is_success() {
                    let json: serde_json::Value = serde_json::from_str(&body).unwrap_or_default();
                    return ManifestUploadResult {
                        file_name: file_name.clone(),
                        status: "ok".to_string(),
                        detail: Some("Uploaded successfully".to_string()),
                        server_response: Some(json),
                    };
                } else if status.is_client_error() {
                    // 4xx — not retryable; parse structured error from server
                    let json: serde_json::Value = serde_json::from_str(&body).unwrap_or_default();
                    let error_msg = json
                        .get("error")
                        .and_then(|v| v.as_str())
                        .unwrap_or(&body)
                        .to_string();
                    return ManifestUploadResult {
                        file_name: file_name.clone(),
                        status: "error".to_string(),
                        detail: Some(error_msg),
                        server_response: Some(json),
                    };
                } else {
                    // 5xx — retryable
                    log::warn!("[manifest] Upload {} got HTTP {} (attempt {}/{})", file_name, status, attempt + 1, MAX_RETRIES + 1);
                    if attempt == MAX_RETRIES {
                        return ManifestUploadResult {
                            file_name: file_name.clone(),
                            status: "error".to_string(),
                            detail: Some(format!("HTTP {} after {} attempts", status, MAX_RETRIES + 1)),
                            server_response: None,
                        };
                    }
                    continue;
                }
            }
            Err(e) => {
                log::warn!("[manifest] Upload {} network error (attempt {}/{}): {}", file_name, attempt + 1, MAX_RETRIES + 1, e);
                if attempt == MAX_RETRIES {
                    return ManifestUploadResult {
                        file_name: file_name.clone(),
                        status: "error".to_string(),
                        detail: Some(format!("Network error after {} attempts: {}", MAX_RETRIES + 1, e)),
                        server_response: None,
                    };
                }
                continue;
            }
        }
    }

    ManifestUploadResult {
        file_name,
        status: "error".to_string(),
        detail: Some("Exhausted all retries".to_string()),
        server_response: None,
    }
}

// ── Chunked upload ──────────────────────────────────────────────────────────

/// Upload a large file (>100MB) in chunks.
/// Each chunk is sent to POST /api/manifest/upload/chunk.
/// The server automatically completes and reassembles the file when the
/// last chunk arrives (chunkIndex == totalChunks - 1), so no separate
/// /complete endpoint call is needed.
async fn upload_file_in_chunks(
    app: &tauri::AppHandle,
    client: &reqwest::Client,
    api_key: &str,
    file_path: &Path,
    file_idx: u32,
    total_files: u32,
) -> Result<Vec<ManifestUploadResult>, String> {
    let file_name = file_path
        .file_name()
        .unwrap_or_default()
        .to_string_lossy()
        .to_string();

    let contents = match std::fs::read(file_path) {
        Ok(c) => c,
        Err(e) => {
            return Ok(vec![ManifestUploadResult {
                file_name,
                status: "error".to_string(),
                detail: Some(format!("Read error: {}", e)),
                server_response: None,
            }]);
        }
    };

    let file_hash = sha256_hex(&contents);
    let total_chunks = (contents.len() + CHUNK_SIZE - 1) / CHUNK_SIZE;
    let chunk_url = format!("{}/api/manifest/upload/chunk", MANIFEST_API_BASE_URL);

    log::info!(
        "[manifest] Chunking {} ({} bytes → {} chunks of {}MB)",
        file_name,
        contents.len(),
        total_chunks,
        CHUNK_SIZE / (1024 * 1024)
    );

    // Track the last chunk's server response (contains completion info)
    let mut last_response: Option<serde_json::Value> = None;

    for (chunk_idx, chunk_start) in (0..contents.len()).step_by(CHUNK_SIZE).enumerate() {
        let chunk_end = std::cmp::min(chunk_start + CHUNK_SIZE, contents.len());
        let chunk_data = &contents[chunk_start..chunk_end];
        let _chunk_hash = sha256_hex(chunk_data);
        let is_last = chunk_idx == total_chunks - 1;

        // Progress for this chunk
        let chunk_percent = ((chunk_idx as f32 / total_chunks as f32) * 100.0) as u32;
        let overall_base = ((file_idx as f32 / total_files as f32) * 100.0) as u32;
        let overall_next = (((file_idx + 1) as f32 / total_files as f32) * 100.0) as u32;
        let overall_percent = overall_base + ((overall_next - overall_base) * chunk_percent / 100);

        emit_progress(app, &UploadProgress {
            total_files,
            completed_files: file_idx,
            current_file: format!("{} (chunk {}/{})", file_name, chunk_idx + 1, total_chunks),
            current_file_percent: chunk_percent,
            overall_percent,
            status: if is_last { "uploading".to_string() } else { "chunking".to_string() },
        });

        // ── Upload chunk with retry (exponential backoff) ────────────
        let mut attempt: u32 = 0;
        loop {
            // Build multipart form for chunk (must be inside loop — Form is consumed by send)
            let part = reqwest::multipart::Part::bytes(chunk_data.to_vec())
                .file_name(format!("{}_chunk_{}", file_name, chunk_idx))
                .mime_str("application/octet-stream")
                .unwrap_or_else(|_| {
                    reqwest::multipart::Part::bytes(chunk_data.to_vec())
                        .file_name(format!("{}_chunk_{}", file_name, chunk_idx))
                });
            let source_path_str = file_path.to_string_lossy().to_string();
            let form = reqwest::multipart::Form::new()
                .part("file", part)
                .text("file_hash", file_hash.clone())
                .text("original_filename", file_name.clone())
                .text("chunk_index", chunk_idx.to_string())
                .text("total_chunks", total_chunks.to_string())
                .text("source_path", source_path_str);

            match client
                .post(&chunk_url)
                .header("X-API-Key", api_key)
                .multipart(form)
                .send()
                .await
            {
                Ok(resp) => {
                    let status = resp.status();
                    let body = resp.text().await.unwrap_or_default();
                    if status.is_success() {
                        let json: serde_json::Value = serde_json::from_str(&body).unwrap_or_default();
                        if is_last {
                            log::info!(
                                "[manifest] Last chunk uploaded for {} — server auto-completed",
                                file_name
                            );
                            last_response = Some(json);
                        } else {
                            log::info!(
                                "[manifest] Chunk {}/{} uploaded for {}",
                                chunk_idx + 1,
                                total_chunks,
                                file_name
                            );
                        }
                        break; // chunk succeeded
                    } else if status.is_client_error() {
                        // 4xx — not retryable; parse structured error from server
                        log::warn!("[manifest] Chunk {} for {} got 4xx HTTP {} — not retryable", chunk_idx, file_name, status);
                        let json: serde_json::Value = serde_json::from_str(&body).unwrap_or_default();
                        let error_msg = json
                            .get("error")
                            .and_then(|v| v.as_str())
                            .unwrap_or(&body)
                            .to_string();
                        return Ok(vec![ManifestUploadResult {
                            file_name: file_name.clone(),
                            status: "error".to_string(),
                            detail: Some(error_msg),
                            server_response: Some(json),
                        }]);
                    } else {
                        // 5xx — retryable
                        attempt += 1;
                        if attempt >= MAX_RETRIES {
                            log::warn!("[manifest] Chunk {} for {} failed after {} attempts (HTTP {})", chunk_idx, file_name, attempt, status);
                            return Ok(vec![ManifestUploadResult {
                                file_name: file_name.clone(),
                                status: "error".to_string(),
                                detail: Some(format!("Chunk {} failed after {} attempts: HTTP {}", chunk_idx, attempt, status)),
                                server_response: None,
                            }]);
                        }
                        let delay = std::time::Duration::from_millis(500 * 2u64.pow(attempt - 1));
                        log::warn!(
                            "[manifest] Chunk {} for {} got HTTP {} — retry {}/{} in {:?}",
                            chunk_idx, file_name, status, attempt, MAX_RETRIES, delay
                        );
                        tokio::time::sleep(delay).await;
                    }
                }
                Err(e) => {
                    attempt += 1;
                    if attempt >= MAX_RETRIES {
                        log::warn!("[manifest] Chunk {} for {} network error after {} attempts: {}", chunk_idx, file_name, attempt, e);
                        return Ok(vec![ManifestUploadResult {
                            file_name: file_name.clone(),
                            status: "error".to_string(),
                            detail: Some(format!("Chunk {} network error after {} attempts: {}", chunk_idx, attempt, e)),
                            server_response: None,
                        }]);
                    }
                    let delay = std::time::Duration::from_millis(500 * 2u64.pow(attempt - 1));
                    log::warn!(
                        "[manifest] Chunk {} for {} network error — retry {}/{} in {:?}: {}",
                        chunk_idx, file_name, attempt, MAX_RETRIES, delay, e
                    );
                    tokio::time::sleep(delay).await;
                }
            }
        }
    }

    // ── All chunks uploaded — server auto-completed on last chunk ────────
    Ok(vec![ManifestUploadResult {
        file_name: file_name.clone(),
        status: "ok".to_string(),
        detail: Some(format!("Uploaded in {} chunks (auto-completed)", total_chunks)),
        server_response: last_response,
    }])
}

// ── Smart upload orchestration ──────────────────────────────────────────────

/// Smart upload: decides batch vs sequential vs chunked based on file sizes.
/// Emits `manifest-upload-progress` Tauri events for UI progress display.
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

    // ── 4. COLLECT FILE SIZES & FILTER DEDUPED ──────────────────────────
    let mut pending: Vec<(String, u64)> = Vec::new(); // (path, size)
    let mut skipped_results: Vec<ManifestUploadResult> = Vec::new();

    for file_path_str in files {
        let path = Path::new(file_path_str);
        if !path.exists() {
            skipped_results.push(ManifestUploadResult {
                file_name: path.file_name().unwrap_or_default().to_string_lossy().to_string(),
                status: "error".to_string(),
                detail: Some("File not found".to_string()),
                server_response: None,
            });
            continue;
        }

        let contents = match std::fs::read(path) {
            Ok(c) => c,
            Err(e) => {
                skipped_results.push(ManifestUploadResult {
                    file_name: path.file_name().unwrap_or_default().to_string_lossy().to_string(),
                    status: "error".to_string(),
                    detail: Some(format!("Read error: {}", e)),
                    server_response: None,
                });
                continue;
            }
        };

        let hash = sha256_hex(&contents);
        if already_uploaded.contains(&hash) {
            skipped_results.push(ManifestUploadResult {
                file_name: path.file_name().unwrap_or_default().to_string_lossy().to_string(),
                status: "skipped".to_string(),
                detail: Some("Already uploaded (local cache)".to_string()),
                server_response: None,
            });
            continue;
        }

        let size = contents.len() as u64;
        pending.push((file_path_str.clone(), size));
    }

    if pending.is_empty() {
        println!("📋 No new files to upload (all deduped or errored)");
        return Ok(skipped_results);
    }

    // ── 5. DECIDE STRATEGY ──────────────────────────────────────────────
    let total_size: u64 = pending.iter().map(|(_, s)| s).sum();
    let total_files = pending.len() as u32;
    println!("📊 Pending: {} files, {} bytes total", pending.len(), total_size);

    let use_batch = total_size <= BATCH_SIZE_LIMIT && pending.len() <= 10;

    let mut results = skipped_results;
    let mut newly_uploaded_hashes = Vec::new();

    if use_batch {
        // ── BATCH MODE (current behavior) ────────────────────────────────
        println!("📦 BATCH mode ({} files, {} bytes)", pending.len(), total_size);
        emit_progress(app, &UploadProgress {
            total_files,
            completed_files: 0,
            current_file: "Uploading batch…".to_string(),
            current_file_percent: 0,
            overall_percent: 0,
            status: "uploading".to_string(),
        });

        for (idx, (file_path_str, _)) in pending.iter().enumerate() {
            let path = Path::new(file_path_str);
            let file_name = path.file_name().unwrap_or_default().to_string_lossy().to_string();

            emit_progress(app, &UploadProgress {
                total_files,
                completed_files: idx as u32,
                current_file: file_name.clone(),
                current_file_percent: 0,
                overall_percent: ((idx as f32 / total_files as f32) * 100.0) as u32,
                status: "uploading".to_string(),
            });

            let result = upload_single_file(&client, api_key, path).await;
            if result.status == "ok" {
                // Re-hash to record
                if let Ok(c) = std::fs::read(path) {
                    newly_uploaded_hashes.push(sha256_hex(&c));
                }
            }

            emit_progress(app, &UploadProgress {
                total_files,
                completed_files: (idx + 1) as u32,
                current_file: file_name,
                current_file_percent: 100,
                overall_percent: (((idx + 1) as f32 / total_files as f32) * 100.0) as u32,
                status: "uploading".to_string(),
            });

            results.push(result);
        }
    } else {
        // ── SEQUENTIAL MODE (with delay + chunking for large files) ──────
        println!("📤 SEQUENTIAL mode ({} files, {} bytes)", pending.len(), total_size);

        for (idx, (file_path_str, file_size)) in pending.iter().enumerate() {
            let path = Path::new(file_path_str);
            let file_name = path.file_name().unwrap_or_default().to_string_lossy().to_string();

            emit_progress(app, &UploadProgress {
                total_files,
                completed_files: idx as u32,
                current_file: file_name.clone(),
                current_file_percent: 0,
                overall_percent: ((idx as f32 / total_files as f32) * 100.0) as u32,
                status: "uploading".to_string(),
            });

            let file_results = if *file_size > BATCH_SIZE_LIMIT {
                // Chunk this large file
                println!("🔪 Chunking {} ({} bytes > {} limit)", file_name, file_size, BATCH_SIZE_LIMIT);
                upload_file_in_chunks(app, &client, api_key, path, idx as u32, total_files).await?
            } else {
                // Normal single upload
                let result = upload_single_file(&client, api_key, path).await;
                vec![result]
            };

            // Record hashes for successful uploads
            for r in &file_results {
                if r.status == "ok" {
                    if let Ok(c) = std::fs::read(path) {
                        newly_uploaded_hashes.push(sha256_hex(&c));
                    }
                }
            }

            results.extend(file_results);

            // Delay between uploads (not after last)
            if idx < pending.len() - 1 {
                println!("⏳ Waiting {}s before next upload…", UPLOAD_DELAY_SECS);
                tokio::time::sleep(std::time::Duration::from_secs(UPLOAD_DELAY_SECS)).await;
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

    // ── 7. FINAL PROGRESS ──────────────────────────────────────────────────
    emit_progress(app, &UploadProgress {
        total_files,
        completed_files: total_files,
        current_file: "Done".to_string(),
        current_file_percent: 100,
        overall_percent: 100,
        status: "done".to_string(),
    });

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
pub fn uploaded_hashes_path(app: &tauri::AppHandle) -> Result<PathBuf, String> {
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
