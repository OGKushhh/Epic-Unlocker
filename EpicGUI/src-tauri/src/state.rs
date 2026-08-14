// state.rs
// Shared application state, held behind an Arc<RwLock<>> and accessible from
// both the pipe reader task and Tauri command handlers.

use serde::{Deserialize, Serialize};
use std::collections::HashMap;

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Achievement {
    pub id: String,
    pub name: String,
    pub description: String,
    pub is_hidden: bool,
    pub state: String, // "Locked" | "Unlocked" | "Unlocking"
    /// UnlockedIconURL as reported by the EOS SDK. None if the SDK gave us
    /// no URL for this achievement (older DLL builds also leave this None).
    /// Used by the `fetch_achievement_icons` command to download icons
    /// independently of the in-game overlay.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub icon_url: Option<String>,
}

/// DLC entry as sent over the pipe from the DLL (catalog packet).
/// Contains only id + title from Epic's GraphQL catalog.
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct DlcEntry {
    pub id: String,
    pub title: String,
}

/// Per-DLC statistics parsed from ScreamAPI.log lines:
///   - `Item ID: <id>` increments `times_queried`
///   - `[Owned] <id>` sets `current_owned = true` and increments `times_owned`
///   - `[Not Owned] <id>` sets `current_owned = false`
///
/// These are populated by `dlc_log_parser::parse_dlc_line` and merged with
/// the DlcCatalog packet's titles when the frontend requests DLC data.
#[derive(Debug, Clone, Default, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct DlcStat {
    pub id: String,
    /// Title extracted from the log line as a fallback when the catalog
    /// packet hasn't arrived yet (or doesn't include this DLC).
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub title: Option<String>,
    pub times_queried: u32,
    pub times_owned: u32,
    pub current_owned: bool,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ConnectionStatus {
    pub connected: bool,
    pub pipe_path: String,
    pub last_error: Option<String>,
    /// Display name of the currently-connected game.
    /// Populated by the ScreamAPI pipe (when the protocol sends a game-info
    /// packet). Until then, defaults to "Epic Game" as a honest generic
    /// placeholder — NOT a fake specific title like "Racing Game".
    pub game_name: String,
}

#[derive(Debug, Default)]
pub struct AppState {
    pub connected: bool,
    pub last_error: Option<String>,
    pub achievements: Vec<Achievement>,
    pub dlc_catalog: Vec<DlcEntry>,
    pub log_path: Option<String>,
    /// Display name of the connected game. Updated when the pipe sends a
    /// game-info packet (future protocol extension). Defaults to "Epic Game".
    pub game_name: String,
    /// Per-DLC stats parsed from ScreamAPI.log. Keyed by DLC id.
    /// Populated by `dlc_log_parser` as `get_log_tail` reads new log lines.
    pub dlc_stats: HashMap<String, DlcStat>,
    /// Last-seen `GetEntitlementsCount: N` value from the log. -1 = unknown.
    pub entitlement_count: i32,
    /// Byte offset of the last-read position in ScreamAPI.log.
    /// Ports the C++ `g_logFilePos` behavior: only new bytes are read on
    /// each poll, and dlc_stats are updated incrementally (no clear).
    /// Reset to 0 when a new LogPath packet arrives (new game connecting).
    pub log_file_pos: u64,
    /// Rolling in-memory buffer of log lines for display in the Log tab.
    /// Capped at LOG_MAX_LINES (20000); oldest lines dropped.
    /// This replaces the previous "read last 2MB every poll" behavior —
    /// now we read incrementally and keep what we've seen.
    pub log_lines: Vec<String>,
}

pub(crate) const LOG_MAX_LINES: usize = 20000;
pub(crate) const LOG_TRIM_TO: usize = 18000;

impl AppState {
    pub fn connection_status(&self) -> ConnectionStatus {
        ConnectionStatus {
            connected: self.connected,
            pipe_path: crate::pipe_protocol::EPIC_PIPE_NAME.to_string(),
            last_error: self.last_error.clone(),
            game_name: if self.game_name.is_empty() {
                "Epic Game".to_string()
            } else {
                self.game_name.clone()
            },
        }
    }
}
