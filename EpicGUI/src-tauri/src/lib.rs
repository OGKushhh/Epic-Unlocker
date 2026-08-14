// lib.rs — Tauri library entry point.
// The main binary (main.rs) just calls into this.

mod commands;
mod dlc_log_parser;
mod pipe_client;
mod pipe_protocol;
mod state;
#[cfg(target_os = "windows")]
mod windows_impl;
#[cfg(not(target_os = "windows"))]
mod stub_impl;

use std::sync::Arc;
use tokio::sync::RwLock;

use state::AppState;

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    env_logger::Builder::from_env(env_logger::Env::default().default_filter_or("info"))
        .format_timestamp_millis()
        .init();

    let state = Arc::new(RwLock::new(AppState::default()));
    // Shared slot for the currently-connected pipe client.
    // - The reader loop (spawn_pipe_loop) writes Some(Arc<PipeClient>) on connect
    //   and None on disconnect.
    // - Command handlers read this slot, clone the Arc, drop the guard, then call
    //   send_command on their owned Arc<PipeClient>.
    let pipe_client_state: pipe_client::PipeClientState =
        Arc::new(RwLock::new(None));

    tauri::Builder::default()
        .plugin(tauri_plugin_shell::init())
        .plugin(tauri_plugin_dialog::init())
        .plugin(tauri_plugin_fs::init())
        .plugin(tauri_plugin_os::init())
        .manage(state.clone())
        .manage(pipe_client_state.clone())
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
            commands::fetch_achievement_icons,
            commands::window_minimize,
            commands::window_toggle_maximize,
            commands::window_close,
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
