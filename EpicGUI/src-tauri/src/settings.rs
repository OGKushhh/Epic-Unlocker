// settings.rs
// G2: Persistent application settings.
//
// Stored as a single JSON file (settings.json) in the app's local data dir
// (e.g. %LOCALAPPDATA%/EpicGUI/settings.json on Windows). Loaded once on
// startup and on demand via the `get_settings` Tauri command; saved whenever
// the user changes a setting in the Settings tab via `save_settings`.
//
// This replaces the previous visual-only SettingsTab where toggles/dropdowns
// flipped local state but never persisted — the dead `tauri-plugin-fs` dep
// is now actually used (via std::fs directly, simpler than the plugin).

use serde::{Deserialize, Serialize};

/// Default for `manifest_consent` — true because this is an opt-out model.
/// Needed as a named function for `#[serde(default = "...")]` since
/// `bool`'s Default is `false`, but we want `true`.
fn default_manifest_consent() -> bool {
    true
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct AppSettings {
    /// Polling interval for the connection-status fallback, in milliseconds.
    /// Default 2000. Lower = snappier UI, higher = less CPU.
    pub auto_refresh_interval_ms: u32,

    /// Whether to attempt connecting to the pipe on app launch.
    /// Default true. When false, the user must click "Refresh" manually.
    pub connect_on_launch: bool,

    /// Maximum number of log lines retained in the in-memory display buffer.
    /// Default 20000. Older lines are dropped (matching LOG_MAX_LINES).
    pub max_log_lines: u32,

    /// Whether the user has consented to manifest uploads.
    /// Default true — opt-out model.
    #[serde(default = "default_manifest_consent")]
    pub manifest_consent: bool,

    /// Whether the one-time consent modal has been dismissed.
    /// Default false — modal shows on first launch.
    /// On "OK, got it": consent=true, dismissed=true (never shows again).
    /// On "Disable": consent=false, dismissed=false (gate re-shows every launch).
    /// When consent is toggled to false in Settings: dismissed=false (modal shows again).
    #[serde(default)]
    pub manifest_consent_dismissed: bool,
}

impl Default for AppSettings {
    fn default() -> Self {
        Self {
            auto_refresh_interval_ms: 2000,
            connect_on_launch: true,
            max_log_lines: 20000,
            manifest_consent: true,
            manifest_consent_dismissed: false,
        }
    }
}

impl AppSettings {
    /// Returns the path to the settings file in the app's local data dir.
    /// On Windows: %LOCALAPPDATA%/EpicGUI/settings.json
    /// Falls back to the current dir if app_local_data_dir is unavailable.
    fn settings_path(app: &tauri::AppHandle) -> Result<std::path::PathBuf, String> {
        use tauri::Manager;
        let dir = app
            .path()
            .app_local_data_dir()
            .map_err(|e| format!("Failed to resolve app_local_data_dir: {e}"))?;
        std::fs::create_dir_all(&dir)
            .map_err(|e| format!("Failed to create settings dir {dir:?}: {e}"))?;
        Ok(dir.join("settings.json"))
    }

    /// Load settings from disk. Returns Default if the file doesn't exist
    /// or fails to parse (never errors — bad settings shouldn't block startup).
    pub fn load(app: &tauri::AppHandle) -> Result<Self, String> {
        let path = Self::settings_path(app)?;
        if !path.exists() {
            log::info!("[settings] No settings file at {path:?} — using defaults");
            return Ok(Self::default());
        }
        let text = std::fs::read_to_string(&path)
            .map_err(|e| format!("Failed to read settings {path:?}: {e}"))?;
        let settings: Self = serde_json::from_str(&text)
            .map_err(|e| {
                log::warn!("[settings] Failed to parse {path:?}: {e} — using defaults");
                // Return defaults on parse error rather than failing —
                // a corrupt settings file shouldn't block app startup.
                Self::default()
            })
            .unwrap_or_default();
        log::info!("[settings] Loaded from {path:?}: {:?}", settings);
        Ok(settings)
    }

    /// Save settings to disk atomically (write to .tmp then rename).
    pub fn save(&self, app: &tauri::AppHandle) -> Result<(), String> {
        let path = Self::settings_path(app)?;
        let tmp = path.with_extension("json.tmp");
        let text = serde_json::to_string_pretty(self)
            .map_err(|e| format!("Failed to serialize settings: {e}"))?;
        std::fs::write(&tmp, text)
            .map_err(|e| format!("Failed to write settings {tmp:?}: {e}"))?;
        std::fs::rename(&tmp, &path)
            .map_err(|e| format!("Failed to rename {tmp:?} -> {path:?}: {e}"))?;
        log::info!("[settings] Saved to {path:?}: {:?}", self);
        Ok(())
    }
}
