// commands.rs
// Tauri command handlers — these are the functions exposed to the React frontend
// via `invoke("command_name", { args })` (see src/lib/api.ts).

use std::sync::Arc;
use tauri::{Manager, State};
use tokio::sync::RwLock;

use crate::dlc_log_parser;
use crate::pipe_client::PipeClientState;
use crate::pipe_protocol::*;
use crate::state::{Achievement, AppState, ConnectionStatus, DlcEntry};

// ── Connection ──────────────────────────────────────────────────────────────
#[tauri::command]
pub async fn get_connection_status(
    state: State<'_, Arc<RwLock<AppState>>>,
) -> Result<ConnectionStatus, String> {
    Ok(state.read().await.connection_status())
}

// ── Achievements ─────────────────────────────────────────────────────────────
#[tauri::command]
pub async fn get_achievements(
    state: State<'_, Arc<RwLock<AppState>>>,
) -> Result<Vec<Achievement>, String> {
    Ok(state.read().await.achievements.clone())
}

#[tauri::command]
pub async fn unlock_achievement(
    id: String,
    pipe: State<'_, PipeClientState>,
) -> Result<(), String> {
    log::info!("[unlock_achievement] called with id={:?}", id);
    // Clone the Arc out of the shared slot and drop the guard BEFORE awaiting,
    // so we don't hold the RwLock across the send.
    let client = {
        let guard = pipe.read().await;
        match guard.as_ref() {
            Some(c) => {
                log::info!("[unlock_achievement] pipe_client_state = Some(client), cloning Arc");
                c.clone()
            }
            None => {
                log::warn!("[unlock_achievement] pipe_client_state = None — pipe not connected");
                return Err("Pipe not connected".to_string());
            }
        }
    };
    let pkt = CmdUnlockPkt::new(&id);
    log::info!("[unlock_achievement] calling send_command(CmdUnlock, {} bytes)", pkt.to_bytes().len());
    match client
        .send_command(PktType::CmdUnlock, &pkt.to_bytes())
        .await
    {
        Ok(()) => {
            log::info!("[unlock_achievement] send_command returned Ok — packet written to pipe");
            Ok(())
        }
        Err(e) => {
            log::error!("[unlock_achievement] send_command returned Err: {}", e);
            Err(e.to_string())
        }
    }
}

#[tauri::command]
pub async fn unlock_all_achievements(
    pipe: State<'_, PipeClientState>,
) -> Result<(), String> {
    let client = {
        let guard = pipe.read().await;
        guard.as_ref().ok_or("Pipe not connected")?.clone()
    };
    client
        .send_command(PktType::CmdUnlockAll, &[])
        .await
        .map_err(|e| e.to_string())
}

#[tauri::command]
pub async fn refresh_achievements(
    pipe: State<'_, PipeClientState>,
) -> Result<(), String> {
    let client = {
        let guard = pipe.read().await;
        guard.as_ref().ok_or("Pipe not connected")?.clone()
    };
    client
        .send_command(PktType::CmdRefresh, &[])
        .await
        .map_err(|e| e.to_string())
}

#[tauri::command]
pub async fn filter_achievements(
    filter: String,
    state: State<'_, Arc<RwLock<AppState>>>,
) -> Result<Vec<Achievement>, String> {
    let s = state.read().await;
    let achievements: Vec<Achievement> = match filter.as_str() {
        "all" => s.achievements.clone(),
        "locked" => s
            .achievements
            .iter()
            .filter(|a| a.state == "Locked")
            .cloned()
            .collect(),
        "unlocked" => s
            .achievements
            .iter()
            .filter(|a| a.state == "Unlocked")
            .cloned()
            .collect(),
        "hidden" => s
            .achievements
            .iter()
            .filter(|a| a.is_hidden)
            .cloned()
            .collect(),
        _ => s.achievements.clone(),
    };
    Ok(achievements)
}

// ── DLC ──────────────────────────────────────────────────────────────────────
// The catalog packet from the DLL gives us id + title. The log parser gives us
// per-DLC stats (times_queried, times_owned, current_owned) parsed from
// ScreamAPI.log [DLC] lines. The frontend wants both merged into one list.
//
// `get_dlc_catalog` returns just the catalog (legacy contract — used internally
// by the pipe reader event handler). `get_dlc_with_stats` is what the frontend
// DLC tab actually calls to populate the table.
#[tauri::command]
pub async fn get_dlc_catalog(
    state: State<'_, Arc<RwLock<AppState>>>,
) -> Result<Vec<DlcEntry>, String> {
    Ok(state.read().await.dlc_catalog.clone())
}

/// Merged DLC view: catalog (id+title) joined with log-parsed stats
/// (times_queried, times_owned, current_owned). The frontend's DLC tab
/// consumes this instead of `get_dlc_catalog` so the columns are populated.
#[derive(Debug, serde::Serialize)]
#[serde(rename_all = "camelCase")]
pub struct DlcWithStats {
    pub id: String,
    pub title: String,
    pub times_queried: u32,
    pub times_owned: u32,
    pub current_owned: bool,
    /// Convenience for the frontend: "Owned" if current_owned else "Not Owned".
    pub status: String,
}

#[tauri::command]
pub async fn get_dlc_with_stats(
    state: State<'_, Arc<RwLock<AppState>>>,
) -> Result<Vec<DlcWithStats>, String> {
    let s = state.read().await;
    // Catalog entries first (canonical id+title source), enriched with stats.
    let mut out: Vec<DlcWithStats> = s
        .dlc_catalog
        .iter()
        .map(|e| {
            let stat = s.dlc_stats.get(&e.id);
            let current_owned = stat.map(|st| st.current_owned).unwrap_or(false);
            DlcWithStats {
                id: e.id.clone(),
                title: e.title.clone(),
                times_queried: stat.map(|st| st.times_queried).unwrap_or(0),
                times_owned: stat.map(|st| st.times_owned).unwrap_or(0),
                current_owned,
                status: if current_owned { "Owned".to_string() } else { "Not Owned".to_string() },
            }
        })
        .collect();
    // Append log-only DLCs (in the log but not yet in the catalog packet).
    for (id, st) in &s.dlc_stats {
        if !s.dlc_catalog.iter().any(|e| e.id == *id) {
            out.push(DlcWithStats {
                id: id.clone(),
                title: st.title.clone().unwrap_or_default(),
                times_queried: st.times_queried,
                times_owned: st.times_owned,
                current_owned: st.current_owned,
                status: if st.current_owned { "Owned".to_string() } else { "Not Owned".to_string() },
            });
        }
    }
    Ok(out)
}

/// Returns the last-seen `GetEntitlementsCount: N` value from the log.
/// `-1` means unknown (no such line has been parsed yet).
#[tauri::command]
pub async fn get_entitlement_count(
    state: State<'_, Arc<RwLock<AppState>>>,
) -> Result<i32, String> {
    Ok(state.read().await.entitlement_count)
}

// ── Log ──────────────────────────────────────────────────────────────────────
// `get_log_tail` is the live log tail mechanism. The frontend polls it on a
// 1-second interval (see useGameData.ts). Each call:
//   1. Reads the last N lines from ScreamAPI.log on disk.
//   2. Parses [DLC]-tagged lines and updates `state.dlc_stats` +
//      `state.entitlement_count` (faithful port of the C++ ParseDlcLine).
//   3. If any DLC stat changed, emits a `dlc-stats-updated` Tauri event so
//      the frontend can re-fetch the merged DLC list and refresh the table.
//
// This mirrors the C++ GUI's `TailLogFile` (called every 500ms by IDT_LOGTAIL)
// which both appended new log lines AND called ParseDlcLine for [DLC] lines.
// We use the frontend poll instead of a Rust-side timer so the log tab and
// the DLC parsing stay in lockstep — no separate timer to manage.
#[tauri::command]
pub async fn get_log_tail(
    max_lines: Option<usize>,
    state: State<'_, Arc<RwLock<AppState>>>,
    app: tauri::AppHandle,
) -> Result<LogTail, String> {
    let path = state.read().await.log_path.clone();
    let path = match path {
        Some(p) => p,
        None => {
            return Ok(LogTail {
                path: String::new(),
                lines: vec![],
                truncated: false,
            });
        }
    };
    let n = max_lines.unwrap_or(20000);
    #[cfg(target_os = "windows")]
    {
        // Propagate IO errors instead of silently swallowing them. The
        // frontend's log poller already does `.catch(() => {})` so an error
        // here just means "keep showing the previous log lines" — which is
        // much better UX than wiping the visible log to empty every time the
        // game briefly locks ScreamAPI.log for writing.
        let log_result = read_log_tail(&path, n).map_err(|e| {
            log::warn!("[get_log_tail] read_log_tail failed for {path}: {e}");
            e.to_string()
        })?;
        // Parse [DLC] lines and update DLC stats. We do this on every poll
        // because the C++ version did the same — it's cheap (string `find`
        // on a few thousand lines is microseconds) and idempotent (re-parsing
        // the same line just re-increments the same counters, which matches
        // the C++ behavior). The frontend's poll is throttled to 1s.
        //
        // IMPORTANT: We parse the FULL tail every time, not just new lines.
        // This is correct because:
        //   - The C++ version also re-parsed everything on each TailLogFile
        //     call after a file-position reset (e.g. log file rotated).
        //   - It's idempotent for Item ID: / [Owned] / [Not Owned] — those
        //     increment counters, but since we re-read the same lines every
        //     poll, the counters would keep growing. To avoid that, we
        //     RESET dlc_stats before each parse. This means the stats always
        //     reflect the current tail of the log, not a cumulative count.
        //   - For entitlement_count, we just take the last seen value.
        // ── Snapshot-then-reparse change detection ───────────────────────────
        // We can't trust `ParseOutcome::changed` after a reset, because every
        // `Item ID:` line will report `changed = true` (the entry goes from
        // nonexistent → freshly created). Instead we snapshot the old state,
        // re-parse the entire tail, and compare. Only emit `dlc-stats-updated`
        // when the parsed state actually differs from what we had before.
        //
        // This kills the 1/event-per-second flood the frontend was receiving
        // on every poll while a game with [DLC] log lines was running.
        let mut stats_changed = false;
        let mut ec_changed = false;
        {
            let mut s = state.write().await;
            // CRITICAL: deref the guard ONCE into a &mut AppState binding.
            // Disjoint field borrows (`&mut s.dlc_stats` + `&mut s.entitlement_count`)
            // do NOT propagate through `DerefMut` on `RwLockWriteGuard` — the
            // compiler treats each field access as a fresh `deref_mut(&mut s)`
            // call, which is two simultaneous mutable borrows of the guard
            // itself (E0499). Pulling out a single `&mut AppState` reference
            // lets the disjoint-field-borrow optimization kick in.
            let s_ref = &mut *s;

            // Snapshot BEFORE clearing (cheap — usually <50 entries).
            let old_stats = s_ref.dlc_stats.clone();
            let old_ec = s_ref.entitlement_count;

            // Reset stats before re-parsing the tail. The stats always reflect
            // the current tail of the log, not a cumulative count across the
            // whole session. This matches the practical behavior of the C++
            // version (which tracked file position but only saw lines after
            // the GUI launched; we see the whole tail on every poll, which is
            // equivalent for sessions shorter than the 2MB tail window).
            s_ref.dlc_stats.clear();
            s_ref.entitlement_count = -1;
            for line in &log_result.lines {
                // The outcome's `changed` flag is unreliable post-reset; we
                // ignore it and compare snapshots below.
                let _ = dlc_log_parser::parse_dlc_line(
                    line,
                    &mut s_ref.dlc_stats,
                    &mut s_ref.entitlement_count,
                );
            }

            // Compare against the snapshot — only emit if something actually
            // moved. This is what throttles the event flood.
            if s_ref.dlc_stats != old_stats {
                stats_changed = true;
            }
            if s_ref.entitlement_count != old_ec {
                ec_changed = true;
            }
        }
        // Emit events outside the write lock to avoid holding it across emit.
        if stats_changed || ec_changed {
            log::debug!(
                "[get_log_tail] DLC stats changed (stats={}, ec={}) — emitting dlc-stats-updated",
                stats_changed,
                ec_changed
            );
            let _ = tauri::Emitter::emit(&app, "dlc-stats-updated", ());
        }
        Ok(LogTail {
            path,
            lines: log_result.lines,
            truncated: log_result.truncated,
        })
    }
    #[cfg(not(target_os = "windows"))]
    {
        let _ = n;
        let _ = app;
        Ok(LogTail { path, lines: vec![], truncated: false })
    }
}

#[tauri::command]
pub async fn clear_log(
    state: State<'_, Arc<RwLock<AppState>>>,
) -> Result<(), String> {
    let path = state.read().await.log_path.clone();
    if let Some(p) = path {
        #[cfg(target_os = "windows")]
        {
            let _ = std::fs::write(&p, b"");
        }
        let _ = p;
    }
    Ok(())
}

#[tauri::command]
pub async fn open_log_externally(
    state: State<'_, Arc<RwLock<AppState>>>,
) -> Result<(), String> {
    let path = state.read().await.log_path.clone();
    let Some(p) = path else {
        return Err("No log file path available yet — launch a game first.".to_string());
    };
    #[cfg(target_os = "windows")]
    {
        // Use ShellExecuteW with the "open" verb — this is what Explorer
        // invokes when you double-click a file. It launches the user's
        // default application for the file extension (.log → Notepad on a
        // fresh Windows install, but VSCode / Notepad++ / etc. if the user
        // has reassociated .log). Falls back to the Windows "How do you
        // want to open this file?" dialog if no association exists.
        open_with_default_app_windows(&p)?;
    }
    #[cfg(not(target_os = "windows"))]
    {
        let _ = p;
    }
    Ok(())
}

/// Launches `path` with its associated application via `ShellExecuteW`.
/// Equivalent to double-clicking the file in Explorer.
#[cfg(target_os = "windows")]
fn open_with_default_app_windows(path: &str) -> Result<(), String> {
    use std::ffi::OsStr;
    use std::os::windows::ffi::OsStrExt;
    use std::os::windows::io::RawHandle;

    #[link(name = "shell32")]
    extern "system" {
        fn ShellExecuteW(
            hwnd: RawHandle,
            lp_operation: *const u16,
            lp_file: *const u16,
            lp_parameters: *const u16,
            lp_directory: *const u16,
            n_show_cmd: i32,
        ) -> RawHandle;
    }

    const SW_SHOWNORMAL: i32 = 1;

    fn to_wide(s: &str) -> Vec<u16> {
        OsStr::new(s).encode_wide().chain(std::iter::once(0)).collect()
    }

    // ShellExecuteW returns HINSTANCE > 32 on success, <= 32 on error.
    // The idiomatic check is `result as usize > 32`.
    let verb = to_wide("open");
    let file = to_wide(path);
    let result = unsafe {
        ShellExecuteW(
            std::ptr::null_mut(),
            verb.as_ptr(),
            file.as_ptr(),
            std::ptr::null(),
            std::ptr::null(),
            SW_SHOWNORMAL,
        )
    };
    if (result as isize) > 32 {
        Ok(())
    } else {
        // Map the ShellExecuteW error code to a human-readable message.
        // Common codes: 2=FILE_NOT_FOUND, 3=PATH_NOT_FOUND, 5=ACCESS_DENIED,
        // 8=NOT_ENOUGH_MEMORY, 26=SHARE_VIOLATION, 27=INVALID_NAME_ASSOC,
        // 28=DDE_NOT_READY, 29=DDE_FAIL, 30=DDE_BUSY, 31=NO_ASSOCIATION,
        // 32=DLL_NOT_FOUND.
        let msg = match result as isize {
            2 => format!("File not found: {path}"),
            3 => format!("Path not found: {path}"),
            5 => format!("Access denied: {path}"),
            31 => format!("No application associated with .log files. Right-click the file in Explorer and choose Open With."),
            code => format!("ShellExecuteW failed (code={code}) for {path}"),
        };
        Err(msg)
    }
}

// ── Window controls (frameless window) ───────────────────────────────────────
// NOTE: Tauri v2 renamed `tauri::Window` to `tauri::WebviewWindow`. The window
// commands take a `WebviewWindow` parameter that Tauri auto-injects for the
// currently-focused window when the command is invoked from the frontend.
#[tauri::command]
pub async fn window_minimize(window: tauri::WebviewWindow) -> Result<(), String> {
    window.minimize().map_err(|e| e.to_string())
}

#[tauri::command]
pub async fn window_toggle_maximize(window: tauri::WebviewWindow) -> Result<(), String> {
    if window.is_maximized().unwrap_or(false) {
        window.unmaximize().map_err(|e| e.to_string())
    } else {
        window.maximize().map_err(|e| e.to_string())
    }
}

#[tauri::command]
pub async fn window_close(window: tauri::WebviewWindow) -> Result<(), String> {
    window.close().map_err(|e| e.to_string())
}

// ── Fetch Achievement Icons ──────────────────────────────────────────────────
// Downloads each achievement's UnlockedIconURL to a per-app cache directory,
// independent of the in-game overlay's icon loader. The overlay's loader has
// historically been unstable (curl + DX11 texture creation races), so EpicGUI
// owns its own download path here and serves the cached files to the webview
// via Tauri's `asset://` protocol (convertFileSrc on the frontend).
//
// Cache layout:
//   <app_local_data_dir>/EpicGUI/icons/<achievement_id>.<ext>
//
// The command is idempotent — icons already on disk are skipped unless the
// `force` flag is set. Returns one IconFetchResult per achievement so the
// frontend knows exactly which icons succeeded, failed, or were skipped.

#[derive(Debug, serde::Serialize)]
#[serde(rename_all = "camelCase")]
pub struct IconFetchResult {
    pub id: String,
    /// Local file path on disk if the icon was downloaded (or was already cached).
    /// None if the achievement has no URL, or the download failed.
    pub path: Option<String>,
    /// "ok" | "skipped" | "no-url" | "failed"
    pub status: String,
    /// Human-readable detail (error message on failure, original URL on success, etc.)
    pub detail: Option<String>,
}

#[tauri::command]
pub async fn fetch_achievement_icons(
    state: State<'_, Arc<RwLock<AppState>>>,
    app: tauri::AppHandle,
    force: Option<bool>,
) -> Result<Vec<IconFetchResult>, String> {
    // Snapshot the achievement list under the read lock, then drop it before
    // doing any HTTP I/O (which can take seconds per icon).
    let snapshot: Vec<(String, Option<String>)> = {
        let s = state.read().await;
        s.achievements
            .iter()
            .map(|a| (a.id.clone(), a.icon_url.clone()))
            .collect()
    };

    if snapshot.is_empty() {
        return Err("No achievements loaded — launch a game with Epic Unlocker injected first.".into());
    }

    // Cache dir = <app_local_data_dir>/EpicGUI/icons
    let cache_dir = app
        .path()
        .app_local_data_dir()
        .map_err(|e| format!("Failed to resolve app data dir: {e}"))?
        .join("EpicGUI")
        .join("icons");
    std::fs::create_dir_all(&cache_dir)
        .map_err(|e| format!("Failed to create cache dir {}: {e}", cache_dir.display()))?;

    let force = force.unwrap_or(false);
    let client = reqwest::Client::builder()
        .user_agent("EpicGUI/2.0 (icon-fetcher)")
        .build()
        .map_err(|e| format!("Failed to build HTTP client: {e}"))?;

    // Download all icons concurrently. We cap the concurrency at 8 to avoid
    // hammering Epic's CDN (and to keep memory bounded — each response is
    // buffered fully into memory before being written to disk).
    const MAX_CONCURRENT: usize = 8;
    let sem = Arc::new(tokio::sync::Semaphore::new(MAX_CONCURRENT));

    let futures = snapshot.into_iter().map(|(id, url)| {
        let sem = sem.clone();
        let client = client.clone();
        let cache_dir = cache_dir.clone();
        async move {
            let _permit = sem.acquire().await.unwrap();

            // No URL → can't fetch. This is normal for some achievements.
            let Some(url) = url else {
                return IconFetchResult {
                    id,
                    path: None,
                    status: "no-url".into(),
                    detail: None,
                };
            };

            // Derive a file extension from the URL path. Default to .png if
            // we can't tell — the EOS CDN almost always serves PNG.
            let ext = url
                .rsplit('/')
                .next()
                .and_then(|last| last.rsplit('.').next())
                .filter(|e| e.len() <= 4 && e.chars().all(|c| c.is_ascii_alphanumeric()))
                .unwrap_or("png")
                .to_lowercase();
            let file_path = cache_dir.join(format!("{}.{}", sanitize_id(&id), ext));

            // If the file already exists and we're not forcing a re-download,
            // treat it as a cache hit. The overlay re-validates via HEAD
            // requests; we skip that to keep the fetch cheap and stable.
            if !force && file_path.exists() {
                return IconFetchResult {
                    id,
                    path: Some(file_path.to_string_lossy().into_owned()),
                    status: "skipped".into(),
                    detail: Some(url),
                };
            }

            // Download
            match client.get(&url).send().await {
                Ok(resp) if resp.status().is_success() => {
                    let bytes = match resp.bytes().await {
                        Ok(b) => b,
                        Err(e) => {
                            return IconFetchResult {
                                id,
                                path: None,
                                status: "failed".into(),
                                detail: Some(format!("read body: {e}")),
                            };
                        }
                    };
                    if let Err(e) = std::fs::write(&file_path, &bytes) {
                        return IconFetchResult {
                            id,
                            path: None,
                            status: "failed".into(),
                            detail: Some(format!("write file: {e}")),
                        };
                    }
                    IconFetchResult {
                        id,
                        path: Some(file_path.to_string_lossy().into_owned()),
                        status: "ok".into(),
                        detail: Some(url),
                    }
                }
                Ok(resp) => IconFetchResult {
                    id,
                    path: None,
                    status: "failed".into(),
                    detail: Some(format!("HTTP {} for {}", resp.status(), url)),
                },
                Err(e) => IconFetchResult {
                    id,
                    path: None,
                    status: "failed".into(),
                    detail: Some(format!("request: {e}")),
                },
            }
        }
    });

    let results: Vec<IconFetchResult> = futures_util::future::join_all(futures).await;
    Ok(results)
}

/// Sanitize an achievement ID for safe use as a filename. EOS achievement IDs
/// are typically alphanumeric + underscores, but we strip anything else to be
/// safe against path traversal.
fn sanitize_id(id: &str) -> String {
    let mut out = String::with_capacity(id.len());
    for c in id.chars() {
        if c.is_ascii_alphanumeric() || c == '_' || c == '-' {
            out.push(c);
        } else {
            out.push('_');
        }
    }
    if out.is_empty() {
        out.push_str("unnamed");
    }
    out
}

// ── Log tail response ────────────────────────────────────────────────────────
#[derive(Debug, Default, serde::Serialize)]
#[serde(rename_all = "camelCase")]
pub struct LogTail {
    pub path: String,
    pub lines: Vec<String>,
    pub truncated: bool,
}

#[cfg(target_os = "windows")]
fn read_log_tail(path: &str, max_lines: usize) -> std::io::Result<LogTail> {
    use std::io::{BufRead, BufReader, Seek, SeekFrom};
    let mut file = std::fs::File::open(path)?;
    let file_size = file.metadata()?.len() as usize;
    // Read the last ~2 MB (covers ~20000 lines of typical log at ~100 bytes/line)
    let chunk = 2 * 1024 * 1024;
    if file_size > chunk {
        file.seek(SeekFrom::End(-(chunk as i64)))?;
    } else {
        file.seek(SeekFrom::Start(0))?;
    }
    // IMPORTANT: keep the BufReader alive for BOTH the partial-line skip AND
    // the subsequent lines() pass. If we let a temporary BufReader go out of
    // scope after skipping the first line, its internal 8KB buffer would be
    // dropped along with it, losing data.
    let mut reader = BufReader::new(file);
    if file_size > chunk {
        // Skip the partial first line (likely cut in half by the seek).
        let mut discard = String::new();
        reader.read_line(&mut discard)?;
        let _ = discard;
    }
    let mut lines: Vec<String> = reader.lines().filter_map(|l| l.ok()).collect();
    let total = lines.len();
    let truncated = total > max_lines;
    if truncated {
        let drain_count = total - max_lines;
        lines.drain(..drain_count);
    }
    Ok(LogTail {
        path: path.to_string(),
        lines,
        truncated,
    })
}
