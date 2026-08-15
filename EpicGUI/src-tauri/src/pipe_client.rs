// pipe_client.rs
// Named-pipe client that connects to \\.\pipe\EpicGUI (the ScreamAPI DLL is the server).
// On Windows: uses raw std::os::windows::io + CreateFileW via the windows-sys crate-free path.
// On non-Windows: stubbed out (returns Disconnected) so the frontend can still build/dev.

use std::sync::Arc;
use std::time::Duration;
use tokio::sync::RwLock;

use crate::state::AppState;
use crate::pipe_protocol::*;

/// Convert days since Unix epoch (1970-01-01) to (year, month, day).
/// Simplified civil calendar algorithm — no external deps needed.
fn days_to_ymd(mut days: u64) -> (u64, u64, u64) {
    // Shift from 1970-03-01 baseline (simplifies leap-year math)
    days += 719468; // days from 0000-03-01 to 1970-01-01
    let era = days / 146097; // 400-year era
    let day_of_era = days % 146097;
    let year_of_era = (day_of_era - day_of_era / 1460 + day_of_era / 36524
        - day_of_era / 146096) / 365;
    let day_of_year = day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
    let month = (5 * day_of_year + 2) / 153;
    let day = day_of_year - (153 * month + 2) / 5;
    let year = era * 400 + year_of_era;
    let year = if month >= 10 { year + 1 } else { year };
    let month = if month >= 10 { month - 9 } else { month + 3 };
    (year, month, day + 1)
}

#[cfg(target_os = "windows")]
pub use crate::windows_impl::PipeClient;

#[cfg(not(target_os = "windows"))]
pub use crate::stub_impl::PipeClient;

/// Shared, mutable slot holding the currently-connected pipe client (or None).
pub type PipeClientState = Arc<RwLock<Option<Arc<PipeClient>>>>;

/// Spawns the pipe client loop.
pub fn spawn_pipe_loop(
    app: tauri::AppHandle,
    state: Arc<RwLock<AppState>>,
    pipe_client_state: PipeClientState,
) {
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
                let icon_url = if entry.icon_url_off == 0 {
                    None
                } else {
                    read_string(blob, entry.icon_url_off).filter(|s| !s.is_empty())
                };
                let progress = entry.progress as f32 / 1000.0;
                let stat_threshold = if entry.stat_threshold_off == 0 {
                    None
                } else {
                    read_string(blob, entry.stat_threshold_off).filter(|s| !s.is_empty())
                };
                // G4: Parse unlock timestamp from blob (ISO 8601 string)
                let unlock_time = if entry.unlock_time_off == 0 {
                    None
                } else {
                    read_string(blob, entry.unlock_time_off).filter(|s| !s.is_empty())
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
                    progress,
                    stat_threshold,
                    unlock_time,
                    rarity_percent: None,    // filled later by fetch_achievement_rarity
                    rarity_tier: None,       // filled later by fetch_achievement_rarity
                });
            }
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
            // G4: Convert POSIX epoch to ISO 8601 string for the frontend.
            // -1 or 0 means no timestamp (older DLL or not unlocked).
            let unlock_time = if upd.unlock_time > 0 {
                let secs = upd.unlock_time as u64;
                // Format as UTC ISO 8601 without depending on chrono
                let days = secs / 86400;
                let time_of_day = secs % 86400;
                let hours = time_of_day / 3600;
                let minutes = (time_of_day % 3600) / 60;
                let seconds = time_of_day % 60;
                // Compute year/month/day from days since epoch (simplified UTC calendar)
                let (year, month, day) = days_to_ymd(days);
                Some(format!(
                    "{:04}-{:02}-{:02}T{:02}:{:02}:{:02}Z",
                    year, month, day, hours, minutes, seconds
                ))
            } else {
                None
            };
            {
                let mut s = state.write().await;
                if let Some(a) = s.achievements.iter_mut().find(|a| a.id == id) {
                    a.state = new_state.clone();
                    a.unlock_time = unlock_time.clone();
                }
            }
            log::debug!("AchUpdate: {id} -> {new_state} (unlock_time={:?})", unlock_time);
            let _ = tauri::Emitter::emit(
                app,
                "achievement-update",
                serde_json::json!({ "id": id, "state": new_state, "unlockTime": unlock_time }),
            );
        }
        PktType::LogPath => {
            let lp = LogPathPkt::read_from(&pkt.payload)?;
            let path = lp.path_str();
            {
                let mut s = state.write().await;
                s.log_path = Some(path.clone());
                s.log_file_pos = 0;
                s.dlc_stats.clear();
                s.entitlement_count = -1;
                s.log_lines.clear();
            }
            log::info!("LogPath: {path} (incremental log state reset)");
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
        PktType::GameInfo => {
            let gi = GameInfoPkt::read_from(&pkt.payload)?;
            let sandbox_id = gi.sandbox_id_str();
            let product_id = gi.product_id_str();
            let eos_version = gi.eos_version_str();
            {
                let mut s = state.write().await;
                s.game_info.sandbox_id = sandbox_id.clone();
                s.game_info.product_id = product_id.clone();
                s.game_info.eos_version = eos_version.clone();
            }
            log::info!("GameInfo: sandboxId={}, productId={}, eosVersion={}",
                       sandbox_id, product_id, eos_version);
            let _ = tauri::Emitter::emit(app, "game-info", serde_json::json!({
                "sandboxId": sandbox_id,
                "productId": product_id,
                "eosVersion": eos_version
            }));
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
