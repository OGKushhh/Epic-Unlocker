// commands.rs
// Tauri command handlers — these are the functions exposed to the React frontend
// via `invoke("command_name", { args })` (see src/lib/api.ts).

use std::sync::Arc;
use tauri::{Manager, State};
use tokio::sync::RwLock;

use crate::dlc_log_parser;
use crate::pipe_client::PipeClientState;
use crate::pipe_protocol::*;
use crate::state::{Achievement, AppState, ConnectionStatus, DlcEntry, GameInfo, LOG_MAX_LINES, LOG_TRIM_TO};
use crate::rarity::{self, AchievementRarity, RarityCache};

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
    _max_lines: Option<usize>,
    state: State<'_, Arc<RwLock<AppState>>>,
    app: tauri::AppHandle,
) -> Result<LogTail, String> {
    // ── Incremental log tail (ports C++ TailLogFile behavior) ──────────────
    // Previous behavior (BUGGY): read last 2MB every poll, clear dlc_stats,
    // re-parse whole tail. This caused current_owned to flip depending on
    // which lines fell in the 2MB window — unstable across restarts because
    // the tail window shifts forward as the log grows.
    //
    // New behavior (FIXED): track log_file_pos in AppState. Each poll reads
    // only NEW bytes since the last poll, parses only those new lines into
    // dlc_stats (NO clear), and appends to a rolling log_lines buffer capped
    // at LOG_MAX_LINES. On LogPath packet (new game), log_file_pos is reset
    // to 0 and dlc_stats is cleared — exactly mirroring the C++ behavior
    // where g_logFilePos=0 and g_dlcItems.clear() only happen on a new game.
    //
    // Edge case: if the file size is now SMALLER than log_file_pos (log was
    // truncated/rotated by the user or by ScreamAPI), reset to 0 and re-read
    // the whole file, and clear dlc_stats so we don't double-count.
    let path = state.read().await.log_path.clone();
    let path = match path {
        Some(p) => p,
        None => {
            return Ok(LogTail {
                path: String::new(),
                lines: vec![],
                truncated: false,
                file_size: 0,
            });
        }
    };
    #[cfg(target_os = "windows")]
    {
        let last_pos = state.read().await.log_file_pos;
        let inc = read_log_incremental(&path, last_pos).map_err(|e| {
            log::warn!("[get_log_tail] read_log_incremental failed for {path}: {e}");
            e.to_string()
        })?;

        if inc.new_lines.is_empty() {
            // Nothing new since last poll — return the current in-memory buffer.
            let lines_snapshot = state.read().await.log_lines.clone();
            return Ok(LogTail {
                path,
                lines: lines_snapshot,
                truncated: false,
                file_size: inc.file_size,
            });
        }

        let lines_snapshot: Vec<String>;
        let truncated: bool;
        let mut stats_changed = false;
        let mut ec_changed = false;
        {
            let mut s = state.write().await;
            let s_ref = &mut *s;

            // If the file was rotated/truncated, reset stats so we don't
            // carry stale state forward.
            if inc.reset {
                s_ref.dlc_stats.clear();
                s_ref.entitlement_count = -1;
                s_ref.log_lines.clear();
            }

            // Snapshot for change detection (only emit events when state
            // actually moved). Cheap — usually <50 entries.
            let old_stats = s_ref.dlc_stats.clone();
            let old_ec = s_ref.entitlement_count;

            // Parse only the NEW lines. Incremental update — no clear.
            // Disjoint field borrows (`&mut s_ref.dlc_stats` + `&mut s_ref.entitlement_count`)
            // work here because we already deref'd the guard into a single
            // `&mut AppState` binding (s_ref).
            for line in &inc.new_lines {
                let _ = dlc_log_parser::parse_dlc_line(
                    line,
                    &mut s_ref.dlc_stats,
                    &mut s_ref.entitlement_count,
                );
            }

            // Append new lines to the rolling display buffer.
            s_ref.log_lines.extend(inc.new_lines.iter().cloned());
            if s_ref.log_lines.len() > LOG_MAX_LINES {
                let drain_count = s_ref.log_lines.len() - LOG_TRIM_TO;
                s_ref.log_lines.drain(..drain_count);
            }

            // Advance file position.
            s_ref.log_file_pos = inc.new_pos;

            truncated = s_ref.log_lines.len() >= LOG_MAX_LINES;

            if s_ref.dlc_stats != old_stats {
                stats_changed = true;
            }
            if s_ref.entitlement_count != old_ec {
                ec_changed = true;
            }

            // Clone the buffer for the return value while we still hold the
            // lock, so the snapshot is consistent with the state we just wrote.
            // Cloning ~20k small Strings is ~1ms — negligible vs the 1s poll.
            lines_snapshot = s_ref.log_lines.clone();
        }
        // Emit + return outside the write lock so we don't hold it across emit.
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
            lines: lines_snapshot,
            truncated,
            file_size: inc.file_size,
        })
    }
    #[cfg(not(target_os = "windows"))]
    {
        let _ = app;
        Ok(LogTail {
            path,
            lines: vec![],
            truncated: false,
            file_size: 0,
        })
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
    // Also clear the in-memory log_lines buffer + reset file position so the
    // next get_log_tail poll starts fresh from offset 0 of the now-empty file.
    // dlc_stats is intentionally NOT cleared here — those stats reflect the
    // session history, not the log file contents, and the user expects "Clear"
    // to only wipe the visible log view. (If they want a full reset, launching
    // a new game will reset everything via the LogPath handler.)
    {
        let mut s = state.write().await;
        s.log_lines.clear();
        s.log_file_pos = 0;
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

// ── SDK log path ────────────────────────────────────────────────────────────
// A1: The EOS SDK's own log stream is routed to ScreamAPI_SDK.log next to
// ScreamAPI.log. We expose the path so the Settings tab can show it + offer
// an "Open" button. The path is derived from log_path by replacing the
// filename with "ScreamAPI_SDK.log" — same logic the DLL uses.

#[tauri::command]
pub async fn get_sdk_log_path(
    state: State<'_, Arc<RwLock<AppState>>>,
) -> Result<String, String> {
    let path = state.read().await.log_path.clone();
    let Some(p) = path else {
        // No log path yet — game hasn't connected.
        return Ok(String::new());
    };
    // Replace filename with ScreamAPI_SDK.log (matches DLL's sdkLogPath logic).
    let sdk_path = std::path::Path::new(&p)
        .with_file_name("ScreamAPI_SDK.log")
        .to_string_lossy()
        .to_string();
    Ok(sdk_path)
}

#[tauri::command]
pub async fn open_sdk_log_externally(
    state: State<'_, Arc<RwLock<AppState>>>,
) -> Result<(), String> {
    let path = state.read().await.log_path.clone();
    let Some(p) = path else {
        return Err("No log file path available yet — launch a game first.".to_string());
    };
    let sdk_path = std::path::Path::new(&p)
        .with_file_name("ScreamAPI_SDK.log")
        .to_string_lossy()
        .to_string();
    #[cfg(target_os = "windows")]
    {
        // If the SDK log file doesn't exist yet, surface a friendly error
        // rather than letting ShellExecuteW show a confusing OS dialog.
        if !std::path::Path::new(&sdk_path).exists() {
            return Err(format!(
                "SDK log file does not exist yet: {sdk_path}\n\
                Launch a game with Epic Unlocker — the SDK log is created on first connection."
            ));
        }
        open_with_default_app_windows(&sdk_path)?;
    }
    #[cfg(not(target_os = "windows"))]
    {
        let _ = sdk_path;
    }
    Ok(())
}

// ── Settings persistence (G2) ───────────────────────────────────────────────
// Load/save a JSON config file in the app's local data dir. Surfaces the
// settings that the SettingsTab displays (auto-refresh interval, connect on
// launch, max log lines). The actual settings struct + load/save logic lives
// in settings.rs; these commands are thin wrappers.

#[tauri::command]
pub async fn get_settings(
    app: tauri::AppHandle,
) -> Result<crate::settings::AppSettings, String> {
    crate::settings::AppSettings::load(&app)
}

#[tauri::command]
pub async fn save_settings(
    settings: crate::settings::AppSettings,
    app: tauri::AppHandle,
) -> Result<(), String> {
    settings.save(&app)
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
    /// Real byte size of the log file on disk (not an estimate). Drives the
    /// "Log size: X KB" display in the statusbar — previously the GUI
    /// estimated size as `logLines.length * 0.045`, which was wildly off for
    /// long lines or short lines. This is the actual file_size from
    /// std::fs::metadata, captured during the incremental read.
    pub file_size: u64,
}

/// Result of an incremental log read.
struct IncrementalRead {
    /// Only the lines that were NEW since `last_pos` (i.e. not yet seen).
    /// Empty if the file hasn't grown since the last poll.
    new_lines: Vec<String>,
    /// New byte offset to store as `log_file_pos` for the next poll.
    new_pos: u64,
    /// True if we detected the file was truncated/rotated and reset to 0.
    /// Caller should clear dlc_stats + log_lines in this case.
    reset: bool,
    /// Total byte size of the log file on disk (from metadata().len()).
    /// Returned to the frontend so the statusbar can show real KB/MB
    /// instead of estimating from line count.
    file_size: u64,
}

/// Reads only the new bytes from the log file since `last_pos`.
///
/// Behavior:
///   - If file_size <= last_pos: nothing new, return empty new_lines.
///   - If file_size < last_pos (file shrank — rotated/truncated): reset=true,
///     read the whole file from offset 0.
///   - Otherwise: seek to last_pos, read everything from there to EOF.
///
/// The caller is responsible for:
///   - Parsing new_lines into dlc_stats (incremental, no clear).
///   - Appending new_lines to the rolling log_lines display buffer.
///   - Storing new_pos back into AppState.log_file_pos.
///   - On reset=true, clearing dlc_stats + log_lines first.
///
/// We do NOT use a BufReader here because we want byte-precise positioning
/// and we read the entire tail in one ReadFile call (matching the C++ impl
/// which allocates one buffer of `fs.QuadPart - g_logFilePos` bytes).
#[cfg(target_os = "windows")]
fn read_log_incremental(path: &str, last_pos: u64) -> std::io::Result<IncrementalRead> {
    use std::io::Read;
    let mut file = std::fs::File::open(path)?;
    let file_size = file.metadata()?.len();

    // Case 1: nothing new since last poll.
    if file_size <= last_pos && last_pos > 0 {
        return Ok(IncrementalRead {
            new_lines: Vec::new(),
            new_pos: last_pos,
            reset: false,
            file_size,
        });
    }

    // Case 2: file shrank — log was rotated/truncated. Reset to 0.
    let (start, reset) = if file_size < last_pos {
        log::warn!(
            "[read_log_incremental] log file shrank (was {last_pos}, now {file_size}) — resetting to 0"
        );
        (0u64, true)
    } else {
        (last_pos, false)
    };

    use std::io::Seek;
    file.seek(std::io::SeekFrom::Start(start))?;
    let to_read = (file_size - start) as usize;
    // Read raw bytes, then convert with from_utf8_lossy — the file may be
    // mid-write by ScreamAPI and contain a partial UTF-8 sequence at EOF.
    // (The next poll will pick up the rest of that sequence once it's
    // complete.) from_utf8_lossy replaces any invalid bytes with U+FFFD
    // rather than failing the whole read.
    let mut bytes = Vec::with_capacity(to_read);
    file.read_to_end(&mut bytes)?;
    let raw = String::from_utf8_lossy(&bytes).into_owned();

    let new_pos = start + bytes.len() as u64;

    // Split into lines, stripping trailing \r (Windows CRLF) and skipping
    // empty lines (matches C++ behavior in TailLogFile).
    let mut new_lines: Vec<String> = Vec::new();
    for line in raw.split('\n') {
        let line = line.strip_suffix('\r').unwrap_or(line);
        if !line.is_empty() {
            new_lines.push(line.to_string());
        }
    }

    Ok(IncrementalRead {
        new_lines,
        new_pos,
        reset,
        file_size,
    })
}

// ── G4/A2: Game Info + Achievement Rarity ────────────────────────────────────

/// Returns the GameInfo received from the DLL (sandbox ID, product ID, EOS version).
/// Empty strings until the GameInfo packet arrives.
#[tauri::command]
pub async fn get_game_info(
    state: State<'_, Arc<RwLock<AppState>>>,
) -> Result<GameInfo, String> {
    Ok(state.read().await.game_info.clone())
}

/// Fetches achievement rarity data from external APIs (egdata primary,
/// Epic GraphQL fallback) and merges it into the in-memory achievements.
/// Returns the number of achievements that got rarity data.
#[tauri::command]
pub async fn fetch_achievement_rarity(
    state: State<'_, Arc<RwLock<AppState>>>,
    rarity_cache: State<'_, Arc<RwLock<RarityCache>>>,
) -> Result<u32, String> {
    let sandbox_id = {
        let s = state.read().await;
        s.game_info.sandbox_id.clone()
    };

    if sandbox_id.is_empty() {
        return Err("No sandbox ID available - connect to a game first".to_string());
    }

    // Build a reqwest client (reuse across requests)
    let client = reqwest::Client::new();

    // Fetch rarity data (egdata primary, Epic GraphQL fallback)
    let rarity_data = {
        let mut cache = rarity_cache.write().await;
        rarity::fetch_rarity(&sandbox_id, &client, &mut cache).await?
    };

    // Build a lookup map by achievement ID for fast merging
    let rarity_by_id: std::collections::HashMap<String, &AchievementRarity> = rarity_data
        .iter()
        .map(|r| (r.id.clone(), r))
        .collect();

    // Merge rarity data into achievements
    let mut merged_count = 0u32;
    {
        let mut s = state.write().await;
        for ach in &mut s.achievements {
            if let Some(rd) = rarity_by_id.get(&ach.id) {
                ach.rarity_percent = Some(rd.completed_percent);
                ach.rarity_tier = Some(rd.tier);
                merged_count += 1;
            }
        }
    }

    log::info!("[G4] Merged rarity data for {} achievements", merged_count);

    Ok(merged_count)
}

/// Clears the on-disk icon cache directory (<app_local_data_dir>/EpicGUI/icons/)
/// so the next Fetch Icons call will re-download everything.
/// Returns the number of files deleted.
#[tauri::command]
pub async fn clear_icon_cache(
    app: tauri::AppHandle,
    state: State<'_, Arc<RwLock<AppState>>>,
) -> Result<u32, String> {
    let cache_dir = app
        .path()
        .app_local_data_dir()
        .map_err(|e| format!("Failed to resolve app data dir: {e}"))?
        .join("EpicGUI")
        .join("icons");

    let mut deleted = 0u32;
    if cache_dir.exists() {
        let entries = std::fs::read_dir(&cache_dir)
            .map_err(|e| format!("Failed to read icon cache dir: {e}"))?;
        for entry in entries {
            if let Ok(entry) = entry {
                if entry.path().is_file() {
                    if std::fs::remove_file(entry.path()).is_ok() {
                        deleted += 1;
                    }
                }
            }
        }
    }

    // Clear icon_url from in-memory achievements so the UI re-fetches icons
    let mut s = state.write().await;
    for ach in &mut s.achievements {
        ach.icon_url = None;
    }

    log::info!("[IconCache] Cleared {} cached icon files", deleted);
    Ok(deleted)
}
