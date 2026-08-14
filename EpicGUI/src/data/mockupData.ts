/**
 * Mockup data — ported verbatim from the ultimate mockup HTML.
 * The user confirmed this data is REAL (parsed from actual EOS SDK logs),
 * so we treat it as the canonical starting dataset for the carbon copy.
 */

export interface Achievement {
  id: string;
  title: string;
  desc: string;
  unlocked: boolean;
  hidden: boolean;
  progress: number; // 0..1
  /**
   * Local file path of the downloaded achievement icon (set after the user
   * clicks "Fetch Icons"). When present, the achievement row renders an
   * <img> instead of the rotating emoji palette. The path is converted to
   * an `asset://` URL via Tauri's convertFileSrc at render time.
   */
  iconPath?: string;
  /**
   * A3: Human-readable stat threshold annotation, e.g. "12/50 kills".
   * Undefined when no stat info available. Rendered next to the progress bar.
   */
  statThreshold?: string;
}

export interface DlcEntry {
  id: string;
  title: string;
  queried: number;
  owned: number;
  status: "Owned" | "Not Owned";
}

export type LogLevel =
  | "lvl-info"
  | "lvl-warn"
  | "lvl-error"
  | "lvl-ach"
  | "lvl-dlc"
  | "lvl-ovrly"
  | "lvl-hook";

export interface LogLine {
  text: string;
  cls: LogLevel;
}

// ── 38 achievements (verbatim from mockup) ──────────────────────────────────
export const achievementData: Achievement[] = [
  { id: 'ach_01_01', title: 'First of Many', desc: 'Finish a World Tour Race', unlocked: true, hidden: false, progress: 1 },
  { id: 'ach_01_02', title: 'Tournament Star', desc: 'Finish a Tournament', unlocked: true, hidden: false, progress: 1 },
  { id: 'ach_01_03', title: 'Brand New Ride', desc: 'Unlock a Car', unlocked: true, hidden: false, progress: 1 },
  { id: 'ach_01_04', title: 'Bzzzt', desc: 'Install an upgrade', unlocked: true, hidden: false, progress: 1 },
  { id: 'ach_01_05', title: 'Automotive Stylist', desc: 'Unlock a Paint', unlocked: true, hidden: false, progress: 1 },
  { id: 'ach_01_06', title: 'Mr Wheeler', desc: 'Unlock a Rims', unlocked: true, hidden: false, progress: 1 },
  { id: 'ach_01_07', title: 'Car Workshop', desc: 'Unlock a Car Body', unlocked: true, hidden: false, progress: 1 },
  { id: 'ach_01_08', title: 'Trophy Collector', desc: 'Achieve 3x Super Trophies', unlocked: true, hidden: false, progress: 1 },
  { id: 'ach_01_09', title: 'Right Foot', desc: 'Get one Start Boost correctly', unlocked: true, hidden: false, progress: 1 },
  { id: 'ach_01_10', title: 'Beat the Clock', desc: 'Win a "Time Trials" race', unlocked: true, hidden: false, progress: 1 },
  { id: 'ach_02_01', title: 'Roll the Credits!', desc: 'Beat the World Tour', unlocked: false, hidden: false, progress: 0 },
  { id: 'ach_02_02', title: 'Amateur Champion', desc: 'Finish all Amateur Tournaments', unlocked: true, hidden: false, progress: 1 },
  { id: 'ach_02_03', title: 'Professional Champion', desc: 'Finish all Professional Tournaments', unlocked: false, hidden: false, progress: 0.25 },
  { id: 'ach_02_04', title: 'Master Champion', desc: 'Finish all Master Tournaments', unlocked: false, hidden: false, progress: 0 },
  { id: 'ach_02_05', title: 'Fully Upgraded', desc: 'Upgrade a car to max level', unlocked: false, hidden: false, progress: 0 },
  { id: 'ach_02_06', title: 'My Favorite Ride', desc: 'Unlock all paints and bodies for a car', unlocked: false, hidden: false, progress: 0 },
  { id: 'ach_02_07', title: 'Whale of a Time', desc: 'Collect all Time Coins in a "Time Trials" race', unlocked: true, hidden: false, progress: 1 },
  { id: 'ach_02_08', title: 'Car Specialist', desc: 'Unlock 2 bodies for a car', unlocked: true, hidden: false, progress: 1 },
  { id: 'ach_03_01', title: 'USA - Check!', desc: 'Complete 100% of all races in USA', unlocked: true, hidden: false, progress: 1 },
  { id: 'ach_03_02', title: 'Brazil - Check!', desc: 'Complete 100% of all races in Brazil', unlocked: true, hidden: true, progress: 1 },
  { id: 'ach_03_03', title: 'Morocco - Check!', desc: 'Complete 100% of all races in Morocco', unlocked: true, hidden: true, progress: 1 },
  { id: 'ach_03_04', title: 'Italy - Check!', desc: 'Complete 100% of all races in Italy', unlocked: false, hidden: true, progress: 0 },
  { id: 'ach_03_05', title: 'Thailand - Check!', desc: 'Complete 100% of all races in Thailand', unlocked: false, hidden: true, progress: 0 },
  { id: 'ach_03_06', title: 'Passport Complete', desc: 'Beat the World Tour with 100%', unlocked: false, hidden: true, progress: 0 },
  { id: 'ach_03_07', title: 'Golden Amateur Champion', desc: 'Finish all Amateur Tournaments with gold', unlocked: true, hidden: false, progress: 1 },
  { id: 'ach_03_08', title: 'Golden Professional Champion', desc: 'Finish all Professional Tournaments with gold', unlocked: false, hidden: false, progress: 0.25 },
  { id: 'ach_03_09', title: 'Golden Master Champion', desc: 'Finish all Master Tournaments with gold', unlocked: false, hidden: false, progress: 0 },
  { id: 'ach_03_10', title: 'Customization Collector', desc: 'Unlock 2 different bodies for 5 cars', unlocked: false, hidden: false, progress: 0 },
  { id: 'ach_03_11', title: 'Japan - Check!', desc: 'Complete 100% of all races in Japan', unlocked: false, hidden: true, progress: 0 },
  { id: 'ach_04_01', title: 'Long Time on the Road', desc: 'Run a total of 3000 miles', unlocked: true, hidden: false, progress: 1 },
  { id: 'ach_04_02', title: 'Gotta go Fast', desc: 'Use a total of 100 Nitros', unlocked: true, hidden: false, progress: 1 },
  { id: 'ach_04_03', title: 'Consolation Trophy', desc: 'Lose a race reaching less than 3rd place', unlocked: true, hidden: true, progress: 1 },
  { id: 'ach_04_04', title: 'Flawless Racer', desc: 'Reach 1st in 20 consecutive races', unlocked: true, hidden: false, progress: 1 },
  { id: 'ach_04_05', title: 'That was a Close Call!', desc: 'Win a race with less than 0.1s difference', unlocked: true, hidden: true, progress: 1 },
  { id: 'ach_04_06', title: 'Eclectic Driver', desc: 'Finish a race with 10 different cars', unlocked: true, hidden: false, progress: 1 },
  { id: 'ach_04_07', title: 'Nitro Saver', desc: 'Finish a Race without spending any Nitro', unlocked: true, hidden: true, progress: 1 },
  { id: 'ach_04_08', title: 'Trigger-Happy', desc: 'Finish a Race with no Nitros left', unlocked: true, hidden: true, progress: 1 },
  { id: 'ach_04_09', title: 'Nitro on a curve? Better not.', desc: 'Collide with an object out of the road while using nitro', unlocked: true, hidden: true, progress: 1 },
];

// ── 12 DLC entries (verbatim) ────────────────────────────────────────────────
export const dlcData: DlcEntry[] = [
  { id: 'dlc_season_pass_01', title: 'Season Pass', queried: 4, owned: 2, status: 'Owned' },
  { id: 'dlc_starter_pack', title: 'Starter Pack', queried: 2, owned: 1, status: 'Owned' },
  { id: 'dlc_super_boost', title: 'Super Boost', queried: 1, owned: 0, status: 'Not Owned' },
  { id: 'dlc_legendary_skin', title: 'Legendary Skin Collection', queried: 3, owned: 3, status: 'Owned' },
  { id: 'dlc_extra_missions', title: 'Extra Missions', queried: 1, owned: 0, status: 'Not Owned' },
  { id: 'dlc_soundtrack', title: 'Official Soundtrack', queried: 2, owned: 1, status: 'Owned' },
  { id: 'dlc_early_access', title: 'Early Access Pass', queried: 1, owned: 1, status: 'Owned' },
  { id: 'dlc_ultimate_edition', title: 'Ultimate Edition Upgrade', queried: 2, owned: 0, status: 'Not Owned' },
  { id: 'dlc_weapon_pack', title: 'Weapon Pack 01', queried: 1, owned: 0, status: 'Not Owned' },
  { id: 'dlc_vehicle_skin', title: 'Vehicle Skin Pack', queried: 1, owned: 0, status: 'Not Owned' },
  { id: 'dlc_side_stories', title: 'Side Stories', queried: 2, owned: 1, status: 'Owned' },
  { id: 'dlc_challenge_mode', title: 'Challenge Mode', queried: 1, owned: 0, status: 'Not Owned' },
];

// ── Real log lines (verbatim — user confirmed these came from real EOS run) ──
export const logLines: LogLine[] = [
  { text: '[03:57:24.061] [INFO]      Epic Unlocker v1.18.1.2', cls: 'lvl-info' },
  { text: '[03:57:24.130] [INFO]      Successfully obtained original EOS SDK: EOSSDK-Win64-Shipping.dll', cls: 'lvl-info' },
  { text: '[03:57:24.170] [INFO]      [COMPAT] Game EOS SDK version (from DLL): 1.16.0-25297126', cls: 'lvl-info' },
  { text: '[03:57:24.712] [WARN]      [COMPAT] Status: PARTIAL (Game < ScreamAPI)', cls: 'lvl-warn' },
  { text: '[03:57:24.753] [WARN]      [COMPAT] Game uses older SDK - some ScreamAPI features unavailable', cls: 'lvl-warn' },
  { text: '[03:57:27.020] [INFO]      MinHook hooking system initialized - all EOS functions hooked', cls: 'lvl-info' },
  { text: '[03:57:29.312] [WARN]      [ACH] Both ProductUserId and EpicAccountId are NULL - user not logged in yet', cls: 'lvl-warn' },
  { text: '[03:57:47.174] [ACH]       Achievement Definition Count: 38', cls: 'lvl-ach' },
  { text: '[03:57:47.220] [ACH]       [Achievement Definition] achievement_01_02: Tournament Star', cls: 'lvl-ach' },
  { text: '[03:57:47.491] [ACH]       [Achievement Definition] achievement_02_01: Roll the Credits!', cls: 'lvl-ach' },
  { text: '[03:57:47.704] [ACH]       [Achievement Definition] achievement_03_02: Brazil - Check! (HIDDEN)', cls: 'lvl-ach' },
  { text: '[03:57:48.645] [ACH]       [Achievement Definition] achievement_04_06: Eclectic Driver', cls: 'lvl-ach' },
  { text: '[03:57:50.806] [INFO]      [PIPE] Server started on \\\\.\\pipe\\EpicGUI', cls: 'lvl-info' },
  { text: '[03:57:50.861] [ACH]       Player Achievement Count: 34', cls: 'lvl-ach' },
  { text: '[03:57:51.529] [ACH]       [Player Achievement] achievement_02_03: Professional Champion (Progress: 25%)', cls: 'lvl-ach' },
  { text: '[03:57:52.585] [ACH]       Unlocked: 26', cls: 'lvl-ach' },
  { text: '[03:57:52.679] [ACH]       Locked: 12', cls: 'lvl-ach' },
  { text: '[03:57:52.722] [ACH]       Progress: 68.4%', cls: 'lvl-ach' },
  { text: '[03:57:56.024] [INFO]      [HOTKEY] Shift+F5 (raw) — overlay shown', cls: 'lvl-info' },
  { text: '[03:58:30.120] [INFO]      [HOOK] EOS_Platform_Release called', cls: 'lvl-hook' },
  { text: '[03:58:31.652] [INFO]      [HOOK] Shutting down hooks...', cls: 'lvl-hook' },
  { text: '[03:58:31.785] [OVRLY]     AchievementManagerUI shut down', cls: 'lvl-ovrly' },
  { text: '[03:58:31.833] [INFO]      Overlay shutdown complete', cls: 'lvl-info' },
];

// ── Achievement emoji palette (verbatim) ────────────────────────────────────
export const achievementEmojis = [
  '🏆','🚗','🏁','🎯','⚡','🔥','💪','⭐','🏎️','🛞',
  '📋','🎮','🔧','🛡️','💀','🌊','🔫','📖','🎯','🏃',
  '🛡️','💎','⚡','🚀','🎨','🔄','🛞','🔩','⏱️','🏅',
  '🥇','🥈','🥉','🏁','🚦','🛣️','🏎️','💨',
];

// ── Derived sidebar stat values (match mockup numbers) ──────────────────────
export const sidebarStats = {
  unlocked: 26,
  unlockedTotal: 38,
  locked: 12,
  statGated: 38,
  progress: 68.4,
};
