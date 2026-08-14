// pipe_client.rs
// Named-pipe client that connects to \\.\pipe\EpicGUI (the ScreamAPI DLL is the server).
// On Windows: uses raw std::os::windows::io + CreateFileW via the windows-sys crate-free path.
// On non-Windows: stubbed out (returns Disconnected) so the frontend can still build/dev.
//
// Lifecycle:
//   1. ScreamAPI DLL (injected into the Epic game) creates the pipe server.
//   2. EpicGUI (this Rust binary) connects as a client.
//   3. DLL pushes AchList, DlcCatalog, LogPath; then async AchUpdate packets.
//   4. GUI sends CmdUnlock / CmdUnlockAll / CmdRefresh on demand.
//
// We run a background reader task that:
//   - Reads PktHeader + payload
//   - Updates the shared AppState
//   - Emits Tauri events to the frontend (achievement_update, log_path, etc.)
//
// IMPORTANT (sharing model):
//   The connected `PipeClient` is stored in `Arc<RwLock<Option<Arc<PipeClient>>>>`
//   (managed by Tauri as `pipe_client_state`). The reader loop holds an `Arc<PipeClient>`
//   clone for `read_packet(&self)`, and command handlers grab an `Arc<PipeClient>` clone
//   for `send_command(&self)`. Both methods take `&self`, so a single `Arc<PipeClient>`
//   can serve both the reader and any number of concurrent senders.
//   Win32 named pipes allow concurrent ReadFile + WriteFile on the same handle (the
//   kernel serializes access), and we enforce single-reader at the application level.

use std::sync::Arc;
use std::time::Duration;
use tokio::sync::RwLock;

use crate::state::AppState;
use crate::pipe_protocol::*;

// `windows_impl` and `stub_impl` are declared at the crate root in lib.rs
// (because they live at src/windows_impl.rs and src/stub_impl.rs as siblings
// of pipe_client.rs, not in a `pipe_client/` subdirectory).
// We re-export the platform-specific PipeClient from there.
#[cfg(target_os = "windows")]
pub use crate::windows_impl::PipeClient;

#[cfg(not(target_os = "windows"))]
pub use crate::stub_impl::PipeClient;

/// Shared, mutable slot holding the currently-connected pipe client (or None).
///
/// - The reader loop writes `Some(Arc<PipeClient>)` on connect and `None` on disconnect.
/// - Command handlers read the slot, clone the `Arc`, drop the guard, then call
///   `send_command` on their owned `Arc<PipeClient>`. This avoids holding the
///   RwLock guard across the `send_command().await`.
pub type PipeClientState = Arc<RwLock<Option<Arc<PipeClient>>>>;

/// Spawns the pipe client loop. It retries connection every 1s until the DLL
/// is reachable, then streams packets into the shared AppState and emits events.
///
/// On each successful connect, the new `Arc<PipeClient>` is stored into
/// `pipe_client_state` so Tauri command handlers can dispatch commands through it.
/// On disconnect, the slot is cleared to `None`.
pub fn spawn_pipe_loop(
    app: tauri::AppHandle,
    state: Arc<RwLock<AppState>>,
    pipe_client_state: PipeClientState,
) {
    // IMPORTANT: use `tauri::async_runtime::spawn`, NOT `tokio::spawn`.
    // Tauri's `setup` hook runs on the main thread BEFORE the Tokio runtime
    // is set as the current thread's context. `tokio::spawn` requires a
    // current-thread runtime context and panics with "there is no reactor
    // running" if called from setup. `tauri::async_runtime::spawn` correctly
    // enters the runtime context regardless of caller thread.
    tauri::async_runtime::spawn(async move {
        loop {
            match PipeClient::connect().await {
                Ok(client) => {
                    let client = Arc::new(client);
                    log::info!("Connected to ScreamAPI pipe");
                    {
                        let mut s = state.write().await;
                        s.connected = true;
                        s.last_error = None;
                    }
                    {
                        let mut pc = pipe_client_state.write().await;
                        *pc = Some(client.clone());
                    }
                    let _ = tauri::Emitter::emit(
                        &app,
                        "connection-changed",
                        true,
                    );

                    // Reader loop — uses peek-polling instead of blocking ReadFile.
                    // This is CRITICAL: in non-overlapped mode, a blocking ReadFile
                    // would hold the handle's I/O slot and prevent WriteFile (from
                    // send_command/CmdUnlock) from completing. By polling with
                    // try_read_packet (which peeks first and only reads when data
                    // is available), the handle stays free for writes between
                    // incoming packets.
                    loop {
                        match client.try_read_packet().await {
                            Ok(Some(pkt)) => {
                                match handle_packet(&pkt, &state, &app).await {
                                    Ok(_) => {}
                                    Err(e) => {
                                        log::warn!("packet handle error: {e}");
                                        break;
                                    }
                                }
                            }
                            Ok(None) => {
                                // No data available yet — sleep briefly and retry.
                                // 50ms gives ~20 polls/sec, which is fast enough
                                // for AchUpdate packets (sub-100ms latency) while
                                // keeping CPU usage negligible.
                                tokio::time::sleep(Duration::from_millis(50)).await;
                            }
                            Err(e) => {
                                log::warn!("pipe read error: {e}");
                                break;
                            }
                        }
                    }

                    // Disconnected
                    log::warn!("Pipe disconnected, retrying in 1s");
                    {
                        let mut s = state.write().await;
                        s.connected = false;
                        s.last_error = Some("Pipe disconnected".into());
                    }
                    {
                        let mut pc = pipe_client_state.write().await;
                        // Only clear if the slot still points to OUR client.
                        // (A reconnect may have already swapped in a new one.)
                        if let Some(current) = pc.as_ref() {
                            if Arc::ptr_eq(current, &client) {
                                *pc = None;
                            }
                        }
                    }
                    let _ = tauri::Emitter::emit(
                        &app,
                        "connection-changed",
                        false,
                    );
                }
                Err(e) => {
                    {
                        let mut s = state.write().await;
                        if s.connected || s.last_error.is_none() {
                            log::debug!("pipe connect retry: {e}");
                        }
                        s.connected = false;
                        s.last_error = Some(format!("{e}"));
                    }
                }
            }
            tokio::time::sleep(Duration::from_secs(1)).await;
        }
    });
}

/// Dispatches a single inbound packet from the DLL.
async fn handle_packet(
    pkt: &InboundPkt,
    state: &Arc<RwLock<AppState>>,
    app: &tauri::AppHandle,
) -> Result<(), ProtocolError> {
    match pkt.pkt_type {
        PktType::AchList => {
            let hdr = AchListHeader::read_from(&pkt.payload)?;
            let entries_size = hdr.count as usize * AchEntry::SIZE;
            if pkt.payload.len() < AchListHeader::SIZE + entries_size {
                return Err(ProtocolError::PayloadTooShort {
                    got: pkt.payload.len(),
                    want: AchListHeader::SIZE + entries_size,
                });
            }
            let mut achievements = Vec::with_capacity(hdr.count as usize);
            let blob_start = AchListHeader::SIZE + entries_size;
            let blob = &pkt.payload[blob_start..];
            for i in 0..hdr.count as usize {
                let off = AchListHeader::SIZE + i * AchEntry::SIZE;
                let entry = AchEntry::read_from(&pkt.payload[off..off + AchEntry::SIZE])?;
                let id = read_string(blob, entry.id_off).unwrap_or_default();
                let name = read_string(blob, entry.name_off).unwrap_or_default();
                let description = read_string(blob, entry.desc_off).unwrap_or_default();
                // icon_url_off == 0 is the explicit "no URL" sentinel (the blob
                // starts with a NUL byte precisely so offset 0 can never be a
                // real string). Any other offset reads the URL string.
                let icon_url = if entry.icon_url_off == 0 {
                    None
                } else {
                    read_string(blob, entry.icon_url_off).filter(|s| !s.is_empty())
                };
                let wire_state = WireUnlockState::from_u8(entry.state)
                    .unwrap_or(WireUnlockState::Locked);
                achievements.push(crate::state::Achievement {
                    id,
                    name,
                    description,
                    is_hidden: entry.is_hidden != 0,
                    state: wire_state.as_str().to_string(),
                    icon_url,
                });
            }
            // Drop the write lock BEFORE logging/emitting to minimize contention.
            {
                let mut s = state.write().await;
                s.achievements = achievements.clone();
            }
            log::info!("AchList: {} entries", achievements.len());
            let _ = tauri::Emitter::emit(app, "achievements-list", achievements);
        }
        PktType::AchUpdate => {
            let upd = AchUpdatePkt::read_from(&pkt.payload)?;
            let id = upd.id_str();
            let new_state = WireUnlockState::from_u8(upd.state)
                .unwrap_or(WireUnlockState::Locked)
                .as_str()
                .to_string();
            {
                let mut s = state.write().await;
                if let Some(a) = s.achievements.iter_mut().find(|a| a.id == id) {
                    a.state = new_state.clone();
                }
            }
            log::debug!("AchUpdate: {id} -> {new_state}");
            let _ = tauri::Emitter::emit(
                app,
                "achievement-update",
                serde_json::json!({ "id": id, "state": new_state }),
            );
        }
        PktType::LogPath => {
            let lp = LogPathPkt::read_from(&pkt.payload)?;
            let path = lp.path_str();
            {
                let mut s = state.write().await;
                s.log_path = Some(path.clone());
            }
            log::info!("LogPath: {path}");
            let _ = tauri::Emitter::emit(app, "log-path", path);
        }
        PktType::DlcCatalog => {
            let hdr = DlcCatalogHeader::read_from(&pkt.payload)?;
            let entries_size = hdr.count as usize * DlcCatalogEntry::SIZE;
            if pkt.payload.len() < DlcCatalogHeader::SIZE + entries_size {
                return Err(ProtocolError::PayloadTooShort {
                    got: pkt.payload.len(),
                    want: DlcCatalogHeader::SIZE + entries_size,
                });
            }
            let mut dlc = Vec::with_capacity(hdr.count as usize);
            let blob_start = DlcCatalogHeader::SIZE + entries_size;
            let blob = &pkt.payload[blob_start..];
            for i in 0..hdr.count as usize {
                let off = DlcCatalogHeader::SIZE + i * DlcCatalogEntry::SIZE;
                let entry = DlcCatalogEntry::read_from(&pkt.payload[off..off + DlcCatalogEntry::SIZE])?;
                let id = read_string(blob, entry.id_off).unwrap_or_default();
                let title = read_string(blob, entry.title_off).unwrap_or_default();
                dlc.push(crate::state::DlcEntry { id, title });
            }
            {
                let mut s = state.write().await;
                s.dlc_catalog = dlc.clone();
            }
            log::info!("DlcCatalog: {} entries", dlc.len());
            let _ = tauri::Emitter::emit(app, "dlc-catalog", dlc);
        }
        // These are GUI→DLL commands; we should never receive them inbound.
        PktType::CmdUnlock | PktType::CmdUnlockAll | PktType::CmdRefresh => {
            log::warn!("Received outbound-only packet type {:?}, ignoring", pkt.pkt_type);
        }
    }
    Ok(())
}

/// Wrapper for an inbound packet (already parsed header + payload bytes).
pub struct InboundPkt {
    pub pkt_type: PktType,
    pub payload: Vec<u8>,
}
