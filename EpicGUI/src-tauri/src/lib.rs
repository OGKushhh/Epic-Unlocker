// lib.rs — Tauri library entry point.
// The main binary (main.rs) just calls into this.

mod commands;
mod dlc_log_parser;
mod pipe_client;
mod pipe_protocol;
mod settings;
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

                // G1: System tray icon with Show / Quit menu.
                // The `tray-icon` feature is already enabled in Cargo.toml
                // but was never used. This adds a minimal tray icon so the
                // GUI can live in the tray while a game is running — the
                // user can close the window without killing the app.
                use tauri::tray::{TrayIconBuilder, MouseButton, TrayIconEvent};
                use tauri::menu::{Menu, MenuItem};
                use tauri::Manager;
                let show_item = MenuItem::with_id(app, "show", "Show", true, None::<&str>)?;
                let quit_item = MenuItem::with_id(app, "quit", "Quit", true, None::<&str>)?;
                let menu = Menu::with_items(app, &[&show_item, &quit_item])?;
                // Use the app's default window icon for the tray. Fall back
                // to no icon if none is set (rare — tauri.conf.json usually
                // provides one) rather than panicking.
                let tray_icon = app.default_window_icon().cloned();
                let mut tray_builder = TrayIconBuilder::new()
                    .tooltip("Epic Unlocker")
                    .menu(&menu)
                    .show_menu_on_left_click(false);
                if let Some(icon) = tray_icon {
                    tray_builder = tray_builder.icon(icon);
                }
                let _tray = tray_builder
                    .on_menu_event(|app, event| {
                        match event.id.as_ref() {
                            "show" => {
                                if let Some(window) = app.get_webview_window("main") {
                                    let _ = window.show();
                                    let _ = window.set_focus();
                                }
                            }
                            "quit" => {
                                app.exit(0);
                            }
                            _ => {}
                        }
                    })
                    .on_tray_icon_event(|tray, event| {
                        // Double-click on tray icon shows the window.
                        // Note: in Tauri 2.11+ the DoubleClick variant only
                        // carries `button` (the button_state field was dropped);
                        // we match with a wildcard so this stays forward-compat.
                        if let TrayIconEvent::DoubleClick {
                            button: MouseButton::Left,
                            ..
                        } = event
                        {
                            let app = tray.app_handle();
                            if let Some(window) = app.get_webview_window("main") {
                                let _ = window.show();
                                let _ = window.set_focus();
                            }
                        }
                    })
                    .build(app)?;
                // When the user closes the main window, hide to tray instead
                // of exiting (so the pipe reader keeps running). The Quit
                // tray menu item is the only way to actually exit.
                let window = app.get_webview_window("main").ok_or("no main window")?;
                let window_ref = window.clone();
                window.on_window_event(move |event| {
                    if let tauri::WindowEvent::CloseRequested { api, .. } = event {
                        // Hide instead of close. The user can re-show via the
                        // tray icon's "Show" menu or by double-clicking it.
                        let _ = window_ref.hide();
                        api.prevent_close();
                    }
                });

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
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
