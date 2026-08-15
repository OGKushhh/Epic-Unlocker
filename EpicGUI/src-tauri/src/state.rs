// state.rs
// Shared application state, held behind an Arc<RwLock<>> and accessible from
// both the pipe reader task and Tauri command handlers.

use serde::{Deserialize, Serialize};
use std::collections::HashMap;

use crate::rarity::RarityTier;

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
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub icon_url: Option<String>,
    /// A3: Player progress 0..1 from EOS_Achievements_PlayerAchievement::Progress.
    #[serde(default)]
    pub progress: f32,
    /// A3: Human-readable stat threshold annotation, e.g. "12/50 kills".
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub stat_threshold: Option<String>,
    /// G4: Unlock timestamp as ISO 8601 string (e.g. "2024-03-15T18:30:00Z").
    /// None if the achievement is not unlocked or the DLL didn't send it.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub unlock_time: Option<String>,
    /// G4: Global unlock percentage from external API (egdata or Epic GraphQL).
    /// None until rarity data is fetched.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub rarity_percent: Option<f32>,
    /// G4: Rarity tier derived from XP (Bronze/Silver/Gold/Platinum).
    /// None until rarity data is fetched.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub rarity_tier: Option<RarityTier>,
}

/// DLC entry as sent over the pipe from the DLL (catalog packet).
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct DlcEntry {
    pub id: String,
    pub title: String,
}

/// Per-DLC statistics parsed from ScreamAPI.log lines.
#[derive(Debug, Clone, Default, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct DlcStat {
    pub id: String,
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

/// Game info received from the DLL via GameInfo packet (0x05).
#[derive(Debug, Clone, Default, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct GameInfo {
    /// EOS sandbox/namespace ID — used to call external APIs for game name + rarity.
    pub sandbox_id: String,
    /// EOS product ID.
    pub product_id: String,
    /// EOS SDK version string from EOS_GetVersion().
    pub eos_version: String,
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
    pub dlc_stats: HashMap<String, DlcStat>,
    /// Last-seen `GetEntitlementsCount: N` value from the log. -1 = unknown.
    pub entitlement_count: i32,
    /// Byte offset of the last-read position in ScreamAPI.log.
    pub log_file_pos: u64,
    /// Rolling in-memory buffer of log lines for display in the Log tab.
    pub log_lines: Vec<String>,
    /// G4/A2: Game info from DLL (sandbox ID, product ID, EOS version).
    pub game_info: GameInfo,
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
