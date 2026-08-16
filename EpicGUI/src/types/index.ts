/**
 * Shared types — mirror the Rust state.rs structs exactly.
 * These are the data shapes returned by Tauri commands.
 */

export type UnlockState = "Locked" | "Unlocked" | "Unlocking";

/** Rarity tier derived from XP value (same as PlayStation/egdata). */
export type RarityTier = "bronze" | "silver" | "gold" | "platinum" | "unknown";

/** Raw Rust achievement shape (camelCase, matches state.rs serde) */
export interface RustAchievement {
  id: string;
  name: string;
  description: string;
  isHidden: boolean;
  state: UnlockState;
  /** UnlockedIconURL from the EOS SDK. null/undefined when the SDK gave no URL. */
  iconUrl?: string | null;
  /** A3: Player progress 0..1 from EOS_Achievements_PlayerAchievement::Progress. */
  progress?: number;
  /** A3: Human-readable stat threshold annotation, e.g. "12/50 kills". */
  statThreshold?: string | null;
  /** G4: Unlock timestamp as ISO 8601 string (e.g. "2024-03-15T18:30:00Z"). */
  unlockTime?: string | null;
  /** G4: Global unlock percentage from external API (egdata or Epic GraphQL). */
  rarityPercent?: number | null;
  /** G4: Rarity tier derived from XP (Bronze/Silver/Gold/Platinum). */
  rarityTier?: RarityTier | null;
  /** G4: XP value from Epic (determines rarity tier). */
  rarityXp?: number | null;
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
  /** Real byte size of the log file on disk (not an estimate). */
  fileSize?: number;
}

export type AchievementFilter = "all" | "locked" | "unlocked" | "hidden";

/** G4/A2: Game info from DLL (sandbox ID, product ID, EOS version). */
export interface GameInfo {
  sandboxId: string;
  productId: string;
  eosVersion: string;
}

// G2: Persistent application settings (mirrors Rust settings.rs AppSettings).
export interface AppSettings {
  /** Polling interval for the connection-status fallback, in milliseconds. */
  autoRefreshIntervalMs: number;
  /** Whether to attempt connecting to the pipe on app launch. */
  connectOnLaunch: boolean;
  /** Maximum number of log lines retained in the in-memory display buffer. */
  maxLogLines: number;
  /** Whether manifest sharing is enabled (opt-out: default true). */
  manifestConsent: boolean;
  /** Whether the one-time consent modal has been dismissed. */
  manifestConsentDismissed: boolean;
}

/** Scanned Epic manifest file from disk. No parsing — just file metadata + hash. */
export interface ScannedManifest {
  fileName: string;
  sha256: string;
  fileSize: number;
  filePath: string;
}

/** Result of uploading a single manifest. */
export interface ManifestUploadResult {
  fileName: string;
  status: "ok" | "skipped" | "error";
  detail: string | null;
  serverResponse?: Record<string, unknown>;
}

/** Manifest consent state. */
export interface ManifestConsentState {
  consent: boolean;
  dismissed: boolean;
}

/** Upload progress event (mirrors Rust UploadProgress). */
export interface UploadProgress {
  totalFiles: number;
  completedFiles: number;
  currentFile: string;
  currentFilePercent: number;
  overallPercent: number;
  status: string;
}
