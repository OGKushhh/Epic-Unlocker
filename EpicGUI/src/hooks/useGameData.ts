/**
 * useGameData — the wiring layer between React and the Rust backend.
 *
 * ─── CRITICAL DESIGN RULE ───────────────────────────────────────────────────
 * Mockup data is NEVER used as a fallback in the real app.
 * Mockup data is ONLY used when running in a browser without the Tauri
 * runtime (i.e. `npm run dev` for UI development).
 *
 * In production (Tauri runtime present):
 *   - We always call the real Tauri commands.
 *   - If the pipe is not connected (no game running), commands return empty
 *     arrays → the UI shows empty states with "Launch a game to connect" messaging.
 *   - We subscribe to Tauri events for live push updates from the pipe reader:
 *       • connection-changed → update connection state
 *       • achievements-list  → reload achievements
 *       • achievement-update → update a single achievement's state
 *       • dlc-catalog        → reload DLC
 *       • log-path           → update log path
 *
 * In browser dev mode (no Tauri runtime):
 *   - We load mockup data so the UI can be developed/previewed.
 *   - A "DEV MODE" banner is shown to make it obvious the data is not real.
 *
 * The adapter converts Rust wire types (id/name/description/isHidden/state) to
 * UI types (id/title/desc/hidden/unlocked/progress).
 */

import { useCallback, useEffect, useRef, useState } from "react";
import {
  getConnectionStatus,
  getAchievements,
  getDlcCatalog,
  getDlcWithStats,
  getEntitlementCount,
  getLogTail,
  unlockAchievement,
  unlockAllAchievements,
  refreshAchievements,
  fetchAchievementIcons,
  fetchAchievementRarity,
  clearIconCache,
  type IconFetchResult,
} from "../lib/api";
import type {
  RustAchievement,
  RustDlcEntry,
  RustDlcWithStats,
  ConnectionStatus,
  LogTail,
  GameInfo as RustGameInfo,
} from "../types";
import {
  achievementData as mockupAchievements,
  dlcData as mockupDlc,
  logLines as mockupLogLines,
  type Achievement,
  type DlcEntry,
  type LogLine,
  type LogLevel,
} from "../data/mockupData";

// ── Tauri runtime detection ─────────────────────────────────────────────────
// When running inside Tauri, the global `__TAURI_INTERNALS__` is set on window.
// In a plain browser (npm run dev without tauri), it's undefined.
declare global {
  interface Window {
    __TAURI_INTERNALS__?: unknown;
    __TAURI__?: unknown;
  }
}

function isTauriRuntime(): boolean {
  return (
    typeof window !== "undefined" &&
    (window.__TAURI_INTERNALS__ !== undefined ||
      window.__TAURI__ !== undefined)
  );
}

// ── Adapters: Rust wire types → UI types ────────────────────────────────────

function adaptAchievement(r: RustAchievement): Achievement {
  const unlocked = r.state === "Unlocked";
  const unlocking = r.state === "Unlocking";
  return {
    id: r.id,
    title: r.name,
    desc: r.description,
    unlocked,
    unlocking,
    hidden: r.isHidden,
    // A3: Wire protocol now carries real progress (0..1) from
    // EOS_Achievements_PlayerAchievement::Progress. Fall back to 0/1
    // for older DLL builds that don't send the progress field.
    progress: unlocked ? 1 : (r.progress ?? 0),
    // A3: Pass through the stat threshold label (e.g. "12/50 kills")
    // for the GUI to render next to the progress bar. Undefined when
    // the DLL didn't send one (older builds or non-stat-gated achievements).
    statThreshold: r.statThreshold ?? undefined,
    // G4: Pass through unlock timestamp (ISO 8601) and rarity data.
    unlockTime: r.unlockTime ?? undefined,
    rarityPercent: r.rarityPercent ?? undefined,
    rarityTier: r.rarityTier ?? undefined,
    rarityXp: r.rarityXp ?? undefined,
  };
}

function adaptLogLine(raw: string): LogLine {
  // Classify based on tag markers in the log line (matches ScreamAPI log format)
  let cls: LogLevel = "lvl-info";
  const upper = raw.toUpperCase();
  if (upper.includes("[ERROR]")) cls = "lvl-error";
  else if (upper.includes("[WARN]")) cls = "lvl-warn";
  else if (upper.includes("[ACH]")) cls = "lvl-ach";
  else if (upper.includes("[DLC]")) cls = "lvl-dlc";
  else if (upper.includes("[OVRLY]")) cls = "lvl-ovrly";
  else if (upper.includes("[HOOK]")) cls = "lvl-hook";
  return { text: raw, cls };
}

// ── Hook state ──────────────────────────────────────────────────────────────

export interface GameDataState {
  achievements: Achievement[];
  dlc: DlcEntry[];
  /** Last-seen `GetEntitlementsCount: N` from the log. -1 = unknown. */
  entitlementCount: number;
  logLines: LogLine[];
  logPath: string;
  /** Real byte size of the log file on disk (from get_log_tail return).
   *  Undefined when no log is connected yet. Drives the statusbar display. */
  logFileSize?: number;
  connection: ConnectionStatus | null;
  loading: boolean;
  /** true ONLY when running in browser dev mode with mockup data */
  isDevMode: boolean;
  refresh: () => Promise<void>;
  unlockOne: (id: string) => Promise<boolean>;
  unlockAll: () => Promise<boolean>;
  /**
   * Downloads each achievement's UnlockedIconURL into the EpicGUI cache dir.
   * Returns the per-achievement result list AND merges iconPath into the
   * local achievements state so rows immediately render the new icons.
   */
  fetchIcons: (force?: boolean) => Promise<IconFetchResult[]>;
  /**
   * Fetch achievement rarity from egdata/Epic GraphQL and merge into achievements.
   * Returns the count of achievements that got rarity data.
   */
  fetchRarity: () => Promise<number>;
  /**
   * Clear the on-disk icon cache. Returns the number of files deleted.
   * Next fetchIcons() will re-download everything from scratch.
   */
  clearIconCache: () => Promise<number>;
  /** Game info from DLL (sandbox ID, product ID, EOS version). */
  gameInfo: RustGameInfo | null;
}

export function useGameData(): GameDataState {
  const inTauri = isTauriRuntime();

  const [achievements, setAchievements] = useState<Achievement[]>(
    inTauri ? [] : mockupAchievements
  );
  const [dlc, setDlc] = useState<DlcEntry[]>(inTauri ? [] : mockupDlc);
  const [entitlementCount, setEntitlementCount] = useState<number>(inTauri ? -1 : 8);
  const [logLines, setLogLines] = useState<LogLine[]>(
    inTauri ? [] : mockupLogLines
  );
  const [logPath, setLogPath] = useState<string>("");
  // Log size fix: real byte size of ScreamAPI.log (from get_log_tail return).
  const [logFileSize, setLogFileSize] = useState<number | undefined>(undefined);
  const [connection, setConnection] = useState<ConnectionStatus | null>(null);
  const [loading, setLoading] = useState(inTauri);
  const [gameInfo, setGameInfo] = useState<RustGameInfo | null>(null);
  const gameInfoRef = useRef<RustGameInfo | null>(null);
  // Dedup flag for auto-fetch rarity (prevents double fetch from achievements-list + game-info events)
  const rarityFetchInProgress = useRef(false);
  // Keep ref in sync so event listeners can read the latest value
  useEffect(() => { gameInfoRef.current = gameInfo; }, [gameInfo]);

  // ── Browser dev mode: load mockup once, no commands, no events ───────────
  useEffect(() => {
    if (inTauri) return;
    // Already initialized from mockup via useState defaults
    setConnection({
      connected: false,
      pipePath: "\\\\.\\pipe\\EpicGUI",
      lastError: "Browser dev mode — using mockup data",
      gameName: "Epic Game (dev)",
    });
    setLoading(false);
  }, [inTauri]);

  // ── Tauri mode: load all data from commands ──────────────────────────────
  const loadAll = useCallback(async () => {
    if (!inTauri) return;
    setLoading(true);

    try {
      const conn = await getConnectionStatus();
      setConnection(conn);
    } catch (e) {
      console.error("get_connection_status failed:", e);
    }

    try {
      const raw: RustAchievement[] = await getAchievements();
      // Preserve any previously-fetched iconPath across the reload (same
      // merge logic as the `achievements-list` event handler below).
      setAchievements((prev) => {
        const prevById = new Map(prev.map((a) => [a.id, a]));
        return raw.map((r) => {
          const adapted = adaptAchievement(r);
          const old = prevById.get(adapted.id);
          if (old?.iconPath) {
            return { ...adapted, iconPath: old.iconPath };
          }
          return adapted;
        });
      });
    } catch (e) {
      console.error("get_achievements failed:", e);
      setAchievements([]);
    }

    try {
      // Use the merged view (catalog + stats from log) so the DLC tab's
      // Times Queried / Times Owned / Status columns are populated.
      // Falls back to plain catalog if the merged command fails (e.g. older
      // Rust build without get_dlc_with_stats).
      try {
        const merged: RustDlcWithStats[] = await getDlcWithStats();
        setDlc(
          merged.map((r) => ({
            id: r.id,
            title: r.title,
            queried: r.timesQueried,
            owned: r.timesOwned,
            status: (r.currentOwned ? "Owned" : "Not Owned") as
              | "Owned"
              | "Not Owned",
          }))
        );
      } catch (e) {
        console.warn("get_dlc_with_stats failed, falling back to catalog:", e);
        const raw: RustDlcEntry[] = await getDlcCatalog();
        setDlc(
          raw.map((r) => ({
            id: r.id,
            title: r.title,
            queried: 0,
            owned: 0,
            status: "Not Owned" as const,
          }))
        );
      }
    } catch (e) {
      console.error("DLC load failed:", e);
      setDlc([]);
    }

    try {
      const ec = await getEntitlementCount();
      setEntitlementCount(ec);
    } catch (e) {
      console.error("get_entitlement_count failed:", e);
    }

    try {
      const tail: LogTail = await getLogTail(20000);
      setLogLines(tail.lines.map(adaptLogLine));
      setLogPath(tail.path);
      if (typeof tail.fileSize === "number") setLogFileSize(tail.fileSize);
    } catch (e) {
      console.error("get_log_tail failed:", e);
      setLogLines([]);
    }

    setLoading(false);
  }, [inTauri]);

  // Initial load + subscribe to Tauri events for live push updates
  useEffect(() => {
    if (!inTauri) return;

    loadAll();

    // Subscribe to live events emitted by the pipe reader (pipe_client.rs)
    let unlistenFns: Array<() => void> = [];

    (async () => {
      try {
        const { listen } = await import("@tauri-apps/api/event");

        const unsubs = await Promise.all([
          listen<boolean>("connection-changed", (event) => {
            if (event.payload) {
              // On connect, fetch fresh state from backend instead of using stale local state
              getConnectionStatus().then((cs) => setConnection(cs)).catch(() => {});
            } else {
              setConnection((prev) => ({
                connected: false,
                pipePath: prev?.pipePath ?? "\\\\.\\pipe\\EpicGUI",
                lastError: prev?.lastError ?? "Pipe disconnected",
                gameName: prev?.gameName ?? "Epic Game",
              }));
              // Clear stale data so empty states show
              setAchievements([]);
              setDlc([]);
              setLogLines([]);
            }
          }),
          listen<RustAchievement[]>("achievements-list", (event) => {
            // Preserve iconPath across list refreshes. The DLL pushes a fresh
            // AchList packet on every Refresh (and on initial connect), which
            // would otherwise wipe out icons the user already fetched via
            // "Fetch Icons". Merge by id: keep the old iconPath if the new
            // payload doesn't carry one (it never does — iconUrl is the URL,
            // not the local cached path).
            setAchievements((prev) => {
              const prevById = new Map(prev.map((a) => [a.id, a]));
              return event.payload.map((r) => {
                const adapted = adaptAchievement(r);
                const old = prevById.get(adapted.id);
                if (old?.iconPath) {
                  return { ...adapted, iconPath: old.iconPath };
                }
                return adapted;
              });
            });
            // Auto-fetch rarity when achievements arrive, if we have a sandbox ID
            // and achievements don't already have rarity data
            const hasRarity = event.payload.some((r) => r.rarityTier != null);
            if (!hasRarity && gameInfoRef.current?.sandboxId && !rarityFetchInProgress.current) {
              rarityFetchInProgress.current = true;
              fetchAchievementRarity()
                .then((count) => {
                  if (count > 0) {
                    console.log(`[G4] Auto-fetched rarity for ${count} achievements on achievements-list`);
                    getAchievements().then((raw) => {
                      setAchievements((prev) => {
                        const prevById = new Map(prev.map((a) => [a.id, a]));
                        return raw.map((r) => {
                          const adapted = adaptAchievement(r);
                          const old = prevById.get(adapted.id);
                          if (old?.iconPath) {
                            return { ...adapted, iconPath: old.iconPath };
                          }
                          return adapted;
                        });
                      });
                    });
                  }
                })
                .catch((e) => console.warn("[G4] Auto-fetch rarity on achievements-list failed:", e))
                .finally(() => { rarityFetchInProgress.current = false; });
            }
          }),
          listen<{ id: string; state: string; unlockTime?: string | null }>("achievement-update", (event) => {
            const { id, state, unlockTime } = event.payload;
            setAchievements((prev) =>
              prev.map((a) =>
                a.id === id
                  ? {
                      ...a,
                      unlocked: state === "Unlocked",
                      unlocking: state === "Unlocking",
                      progress: state === "Unlocked" ? 1 : a.progress,
                      // G4: Update unlockTime from the AchUpdate packet.
                      // The DLL now sends the real EOS-provided timestamp.
                      unlockTime: unlockTime ?? a.unlockTime,
                    }
                  : a
              )
            );
          }),
          listen<RustDlcEntry[]>("dlc-catalog", () => {
            // Catalog packet arrived — re-fetch the merged view so the new
            // titles show up with their stats.
            getDlcWithStats()
              .then((merged) =>
                setDlc(
                  merged.map((r) => ({
                    id: r.id,
                    title: r.title,
                    queried: r.timesQueried,
                    owned: r.timesOwned,
                    status: (r.currentOwned ? "Owned" : "Not Owned") as
                      | "Owned"
                      | "Not Owned",
                  }))
                )
              )
              .catch((e) =>
                console.error("dlc-catalog → getDlcWithStats failed:", e)
              );
          }),
          // Emitted by get_log_tail when [DLC] log lines change the stats.
          // Re-fetch the merged DLC list and the entitlement count.
          listen<void>("dlc-stats-updated", () => {
            getDlcWithStats()
              .then((merged) =>
                setDlc(
                  merged.map((r) => ({
                    id: r.id,
                    title: r.title,
                    queried: r.timesQueried,
                    owned: r.timesOwned,
                    status: (r.currentOwned ? "Owned" : "Not Owned") as
                      | "Owned"
                      | "Not Owned",
                  }))
                )
              )
              .catch((e) =>
                console.error("dlc-stats-updated → getDlcWithStats failed:", e)
              );
            getEntitlementCount()
              .then((ec) => setEntitlementCount(ec))
              .catch(() => {});
          }),
          listen<string>("log-path", (event) => {
            setLogPath(event.payload);
            // Reload log lines when the path arrives/changes
            getLogTail(20000)
              .then((tail) => setLogLines(tail.lines.map(adaptLogLine)))
              .catch(() => {});
          }),
          // G4: Auto-fetch rarity when GameInfo arrives from the DLL
          listen<RustGameInfo>("game-info", (event) => {
            setGameInfo(event.payload);
            // Auto-fetch rarity in the background when we get a sandbox ID
            // Dedup with achievements-list handler to avoid double fetch
            if (event.payload.sandboxId && !rarityFetchInProgress.current) {
              rarityFetchInProgress.current = true;
              fetchAchievementRarity()
                .then((count) => {
                  console.log(`[G4] Auto-fetched rarity for ${count} achievements`);
                  // Re-fetch achievements to get the merged rarity data
                  getAchievements().then((raw) => {
                    setAchievements((prev) => {
                      const prevById = new Map(prev.map((a) => [a.id, a]));
                      return raw.map((r) => {
                        const adapted = adaptAchievement(r);
                        const old = prevById.get(adapted.id);
                        if (old?.iconPath) {
                          return { ...adapted, iconPath: old.iconPath };
                        }
                        return adapted;
                      });
                    });
                  });
                })
                .catch((e) => console.warn("[G4] Auto-fetch rarity failed:", e))
                .finally(() => { rarityFetchInProgress.current = false; });
            }
          }),
        ]);
        unlistenFns = unsubs.map((u) => u);
      } catch (e) {
        console.error("Failed to subscribe to Tauri events:", e);
      }
    })();

    // Fallback poll: the `connection-changed` event only fires on connect/
    // disconnect transitions. If the pipe was never found at app startup, the
    // event never fires and the UI stays "loading…" forever. Poll every 2s
    // as a safety net so the Statusbar reflects the actual state (showing the
    // last_error from the Rust backend, e.g. "CreateFileW failed").
    const pollHandle = window.setInterval(() => {
      getConnectionStatus()
        .then((cs) => setConnection(cs))
        .catch(() => {});
    }, 2000);

    // Log tail poll: the DLL writes new log lines to ScreamAPI.log on disk
    // continuously (every command, every hook call, every EOS callback). The
    // pipe does NOT push log lines — only the log *path* is pushed once. So
    // to show live log output, we must re-read the file periodically.
    //
    // This poll ALSO drives DLC stats parsing: each get_log_tail call parses
    // [DLC] lines and emits `dlc-stats-updated` events when stats change. The
    // DLC tab updates automatically via the event listener above. 1s interval
    // gives near-live feedback without thrashing the disk.
    const logPollHandle = window.setInterval(() => {
      getLogTail(20000)
        .then((tail) => {
          setLogLines(tail.lines.map(adaptLogLine));
          // Log size fix: capture the real byte size from the tail return
          // (was previously estimated as logLines.length * 0.045 in the statusbar).
          if (typeof tail.fileSize === "number") setLogFileSize(tail.fileSize);
        })
        .catch(() => {});
    }, 1000);

    return () => {
      unlistenFns.forEach((fn) => fn());
      window.clearInterval(pollHandle);
      window.clearInterval(logPollHandle);
    };
  }, [inTauri, loadAll]);

  // ── Actions ──────────────────────────────────────────────────────────────

  const refresh = useCallback(async () => {
    if (!inTauri) return;
    try {
      await refreshAchievements();
    } catch (e) {
      console.error("refresh_achievements failed:", e);
    }
    await loadAll();
  }, [inTauri, loadAll]);

  const unlockOne = useCallback(
    async (id: string): Promise<boolean> => {
      if (!inTauri) return false;
      try {
        await unlockAchievement(id);
        // NOTE: we do NOT optimistically mark as unlocked here. The real
        // confirmation comes asynchronously via the `achievement-update`
        // event from the pipe. The toast tells the user it's queued.
        return true;
      } catch (e) {
        console.error("unlock_achievement failed:", e);
        return false;
      }
    },
    [inTauri]
  );

  const unlockAll = useCallback(async (): Promise<boolean> => {
    if (!inTauri) return false;
    try {
      await unlockAllAchievements();
      // Same as above — wait for achievement-update events.
      return true;
    } catch (e) {
      console.error("unlock_all_achievements failed:", e);
      return false;
    }
  }, [inTauri]);

  const fetchIcons = useCallback(
    async (force?: boolean): Promise<IconFetchResult[]> => {
      if (!inTauri) {
        // Browser dev mode: nothing to fetch (mockup achievements have no URLs).
        return [];
      }
      const results = await fetchAchievementIcons(force);
      // Merge iconPath into local achievements state so rows re-render with
      // the freshly downloaded icon. We only set iconPath for results that
      // actually have a local file (status "ok" or "skipped").
      const pathById = new Map<string, string>();
      for (const r of results) {
        if (r.path) pathById.set(r.id, r.path);
      }
      if (pathById.size > 0) {
        setAchievements((prev) =>
          prev.map((a) =>
            pathById.has(a.id) ? { ...a, iconPath: pathById.get(a.id) } : a
          )
        );
      }
      return results;
    },
    [inTauri]
  );

  const fetchRarity = useCallback(
    async (): Promise<number> => {
      if (!inTauri) return 0;
      try {
        const count = await fetchAchievementRarity();
        // Re-fetch achievements to pick up the merged rarity data
        const raw: RustAchievement[] = await getAchievements();
        setAchievements((prev) => {
          const prevById = new Map(prev.map((a) => [a.id, a]));
          return raw.map((r) => {
            const adapted = adaptAchievement(r);
            const old = prevById.get(adapted.id);
            if (old?.iconPath) {
              return { ...adapted, iconPath: old.iconPath };
            }
            return adapted;
          });
        });
        return count;
      } catch (e) {
        console.error("fetch_achievement_rarity failed:", e);
        return 0;
      }
    },
    [inTauri]
  );

  const clearIconCacheFn = useCallback(
    async (): Promise<number> => {
      if (!inTauri) return 0;
      try {
        const deleted = await clearIconCache();
        // Re-fetch achievements to pick up the cleared icon paths
        const raw: RustAchievement[] = await getAchievements();
        setAchievements(() => {
          return raw.map((r) => adaptAchievement(r));
        });
        return deleted;
      } catch (e) {
        console.error("clear_icon_cache failed:", e);
        return 0;
      }
    },
    [inTauri]
  );

  return {
    achievements,
    dlc,
    entitlementCount,
    logLines,
    logPath,
    logFileSize,
    connection,
    loading,
    isDevMode: !inTauri,
    refresh,
    unlockOne,
    unlockAll,
    fetchIcons,
    fetchRarity,
    clearIconCache: clearIconCacheFn,
    gameInfo,
  };
}
