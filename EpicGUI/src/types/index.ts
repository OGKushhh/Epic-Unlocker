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
}

export type AchievementFilter = "all" | "locked" | "unlocked" | "hidden";
