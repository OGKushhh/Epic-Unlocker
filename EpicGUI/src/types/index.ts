/**
 * Shared types — mirror the Rust state.rs structs exactly.
 * These are the data shapes returned by Tauri commands.
 *
 * Note: The Rust `Achievement` doesn't carry a `progress` field (the wire
 * protocol only sends locked/unlocked state). Progress is a UI-layer concept
 * derived from stats. The data hook adapter defaults progress to 0 (locked)
 * or 1 (unlocked) when coming from the pipe; the mockup fallback carries the
 * real parsed progress values.
 */

export type UnlockState = "Locked" | "Unlocked" | "Unlocking";

/** Raw Rust achievement shape (camelCase, matches state.rs serde) */
export interface RustAchievement {
  id: string;
  name: string;
  description: string;
  isHidden: boolean;
  state: UnlockState;
  /** UnlockedIconURL from the EOS SDK. null/undefined when the SDK gave no URL. */
  iconUrl?: string | null;
  /** A3: Player progress 0..1 from EOS_Achievements_PlayerAchievement::Progress.
   *  0 = no progress, 1 = fully complete. Older DLL builds that don't send
   *  progress will leave this undefined (treated as 0 by the adapter). */
  progress?: number;
  /** A3: Human-readable stat threshold annotation, e.g. "12/50 kills".
   *  null/undefined when no stat info available (non-stat-gated achievements
   *  or older DLL builds). Rendered next to the progress bar. */
  statThreshold?: string | null;
}

/** Raw Rust DLC shape — catalog packet (id + title only) */
export interface RustDlcEntry {
  id: string;
  title: string;
}

/** Merged DLC view: catalog (id+title) joined with log-parsed stats. */
export interface RustDlcWithStats {
  id: string;
  title: string;
  timesQueried: number;
  timesOwned: number;
  currentOwned: boolean;
  /** "Owned" | "Not Owned" — convenience for the frontend */
  status: string;
}

export interface ConnectionStatus {
  connected: boolean;
  pipePath: string;
  lastError: string | null;
  /** Display name of the currently-connected game. */
  gameName: string;
}

export interface LogTail {
  path: string;
  lines: string[];
  truncated: boolean;
  /** Real byte size of the log file on disk (not an estimate).
   *  Drives the "Log size: X KB" display in the statusbar. */
  fileSize?: number;
}

export type AchievementFilter = "all" | "locked" | "unlocked" | "hidden";


// G2: Persistent application settings (mirrors Rust settings.rs AppSettings).
export interface AppSettings {
  /** Polling interval for the connection-status fallback, in milliseconds. */
  autoRefreshIntervalMs: number;
  /** Whether to attempt connecting to the pipe on app launch. */
  connectOnLaunch: boolean;
  /** Maximum number of log lines retained in the in-memory display buffer. */
  maxLogLines: number;
}
