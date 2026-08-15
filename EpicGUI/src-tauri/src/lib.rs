// lib.rs — Tauri library entry point.
// The main binary (main.rs) just calls into this.

mod commands;
mod dlc_log_parser;
mod pipe_client;
mod pipe_protocol;
mod rarity;
mod settings;
mod state;
#[cfg(target_os = "windows")]
mod windows_impl;
#[cfg(not(target_os = "windows"))]
mod stub_impl;

use std::sync::Arc;
use tokio::sync::RwLock;

use state::AppState;
use rarity::RarityCache;

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    env_logger::Builder::from_env(env_logger::Env::default().default_filter_or("info"))
        .format_timestamp_millis()
        .init();

    let state = Arc::new(RwLock::new(AppState::default()));
    // Shared slot for the currently-connected pipe client.
    let pipe_client_state: pipe_client::PipeClientState =
        Arc::new(RwLock::new(None));
    // G4: Rarity data cache (by sandbox_id), persists for the session.
    let rarity_cache = Arc::new(RwLock::new(RarityCache::default()));

    tauri::Builder::default()
        .plugin(tauri_plugin_shell::init())
        .plugin(tauri_plugin_dialog::init())
        .plugin(tauri_plugin_fs::init())
        .plugin(tauri_plugin_os::init())
        .manage(state.clone())
        .manage(pipe_client_state.clone())
        .manage(rarity_cache.clone())
        .setup({
            let state = state.clone();
            let pipe_client_state = pipe_client_state.clone();
            move |app| {
                // Spawn the pipe reader loop. It runs for the lifetime of the app.
                pipe_client::spawn_pipe_loop(
                    app.handle().clone(),
                    state.clone(),
                    pipe_client_state.clone(),
                );

                Ok(())
            }
        })
        .invoke_handler(tauri::generate_handler![
            commands::get_connection_status,
            commands::get_achievements,
            commands::unlock_achievement,
            commands::unlock_all_achievements,
            commands::refresh_achievements,
            commands::filter_achievements,
            commands::get_dlc_catalog,
            commands::get_dlc_with_stats,
            commands::get_entitlement_count,
            commands::get_log_tail,
            commands::clear_log,
            commands::open_log_externally,
            // A1: SDK log path + open externally
            commands::get_sdk_log_path,
            commands::open_sdk_log_externally,
            // G2: settings persistence
            commands::get_settings,
            commands::save_settings,
            commands::fetch_achievement_icons,
            commands::window_minimize,
            commands::window_toggle_maximize,
            commands::window_close,
            // G4/A2: game info + achievement rarity
            commands::get_game_info,
            commands::fetch_achievement_rarity,
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
