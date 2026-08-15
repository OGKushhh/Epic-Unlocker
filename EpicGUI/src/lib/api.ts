/**
 * Tauri command bindings — strongly-typed wrappers around `invoke()`.
 * Each function maps 1:1 to a `#[tauri::command]` in src-tauri/src/commands.rs.
 */

import { invoke } from "@tauri-apps/api/core";
import type {
  RustAchievement,
  RustDlcEntry,
  RustDlcWithStats,
  AchievementFilter,
  ConnectionStatus,
  LogTail,
  AppSettings,
  GameInfo,
} from "@/types";

// ── Connection ──────────────────────────────────────────────────────────────
export const getConnectionStatus = (): Promise<ConnectionStatus> =>
  invoke("get_connection_status");

// ── Achievements ─────────────────────────────────────────────────────────────
export const getAchievements = (): Promise<RustAchievement[]> =>
  invoke("get_achievements");

export const unlockAchievement = (id: string): Promise<void> =>
  invoke("unlock_achievement", { id });

export const unlockAllAchievements = (): Promise<void> =>
  invoke("unlock_all_achievements");

export const refreshAchievements = (): Promise<void> =>
  invoke("refresh_achievements");

export const filterAchievements = (
  filter: AchievementFilter
): Promise<RustAchievement[]> => invoke("filter_achievements", { filter });

// ── DLC ──────────────────────────────────────────────────────────────────────
// `getDlcCatalog` returns just the catalog (id + title) from the pipe packet.
// `getDlcWithStats` is the merged view the DLC tab actually uses — it joins
// the catalog with per-DLC stats parsed from ScreamAPI.log [DLC] lines.
export const getDlcCatalog = (): Promise<RustDlcEntry[]> =>
  invoke("get_dlc_catalog");

export const getDlcWithStats = (): Promise<RustDlcWithStats[]> =>
  invoke("get_dlc_with_stats");

// Returns the last-seen `GetEntitlementsCount: N` value from the log.
// -1 means unknown (no such line parsed yet).
export const getEntitlementCount = (): Promise<number> =>
  invoke("get_entitlement_count");

// ── Fetch Achievement Icons ──────────────────────────────────────────────────
// Downloads each achievement's UnlockedIconURL into the EpicGUI cache directory
// (independent of the in-game overlay's loader). Returns one result per
// achievement so the frontend knows which icons succeeded / failed / were
// skipped (already cached) / had no URL.
export interface IconFetchResult {
  id: string;
  /** Local file path on disk if the icon is available; null otherwise. */
  path: string | null;
  /** "ok" | "skipped" | "no-url" | "failed" */
  status: string;
  /** Human-readable detail (error message on failure, source URL on success). */
  detail: string | null;
}

export const fetchAchievementIcons = (
  force?: boolean
): Promise<IconFetchResult[]> =>
  invoke("fetch_achievement_icons", { force: force ?? false });

// ── Log ──────────────────────────────────────────────────────────────────────
export const getLogTail = (maxLines?: number): Promise<LogTail> =>
  invoke("get_log_tail", { maxLines: maxLines ?? 20000 });

export const clearLog = (): Promise<void> => invoke("clear_log");

export const openLogExternally = (): Promise<void> =>
  invoke("open_log_externally");

// ── SDK log (A1) ─────────────────────────────────────────────────────────────
// The EOS SDK's own log stream is routed to ScreamAPI_SDK.log next to
// ScreamAPI.log. These commands let the Settings tab show the path + open it.
export const getSdkLogPath = (): Promise<string> =>
  invoke("get_sdk_log_path");

export const openSdkLogExternally = (): Promise<void> =>
  invoke("open_sdk_log_externally");

// ── Settings (G2) ────────────────────────────────────────────────────────────
// Persistent JSON config in the app's local data dir.
export const getSettings = (): Promise<AppSettings> =>
  invoke("get_settings");

export const saveSettings = (settings: AppSettings): Promise<void> =>
  invoke("save_settings", { settings });

// ── Window controls (frameless window) ───────────────────────────────────────
export const windowMinimize = (): Promise<void> => invoke("window_minimize");
export const windowToggleMaximize = (): Promise<void> =>
  invoke("window_toggle_maximize");
export const windowClose = (): Promise<void> => invoke("window_close");

// ── G4/A2: Game Info + Achievement Rarity ────────────────────────────────────
// GameInfo from DLL (sandbox ID, product ID, EOS version).
export const getGameInfo = (): Promise<GameInfo> =>
  invoke("get_game_info");

// Fetch achievement rarity from external APIs (egdata primary, Epic GraphQL fallback).
// Returns the count of achievements that got rarity data merged in.
export const fetchAchievementRarity = (): Promise<number> =>
  invoke("fetch_achievement_rarity");

// Clear the on-disk icon cache. Returns the number of files deleted.
// Next Fetch Icons call will re-download everything from scratch.
export const clearIconCache = (): Promise<number> =>
  invoke("clear_icon_cache");
