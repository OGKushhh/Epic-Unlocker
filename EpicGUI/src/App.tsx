/**
 * EpicGUI — carbon copy of the ultimate mockup, wired to the Rust backend.
 *
 * Data flow:
 *   - useGameData hook calls real Tauri commands (get_achievements, etc.)
 *   - Subscribes to Tauri events for live push updates from the pipe reader
 *   - Mockup data is ONLY used when running in browser dev mode (no Tauri)
 *   - In production: empty states shown when pipe is disconnected
 *
 * Connection states:
 *   - Loading (yellow pip): initial connect attempt
 *   - Connected (green pip): pipe active, real data flowing
 *   - Disconnected (red pip): no pipe, empty states shown
 */

import { useCallback, useEffect, useState } from "react";
import Titlebar from "./components/Titlebar";
import Menubar, { type TabId } from "./components/Menubar";
import Sidebar from "./components/Sidebar";
import Statusbar from "./components/Statusbar";
import UnlockAllModal from "./components/UnlockAllModal";
import Tooltip, { type TooltipState } from "./components/Tooltip";
import AchievementsTab from "./components/tabs/AchievementsTab";
import DlcTab from "./components/tabs/DlcTab";
import LogTab from "./components/tabs/LogTab";
import SettingsTab from "./components/tabs/SettingsTab";
import { useToasts } from "./hooks/useToasts";
import { useTheme } from "./hooks/useTheme";
import { useGameData } from "./hooks/useGameData";
import { achievementEmojis, type Achievement } from "./data/mockupData";
import { clearLog, openLogExternally } from "./lib/api";

// ── Stat-gated explanation tooltip content ──────────────────────────────────
const STAT_GATED_TITLE = "Stat-Gated Achievement";
const STAT_GATED_BODY =
  "These achievements unlock when underlying game stats reach required thresholds. " +
  "The unlocker force-ingests the stat values through the EOS Stats interface. " +
  "After triggering, please wait 5–30 seconds for the game to process the new stats " +
  "and grant the achievement.";

export default function App() {
  const [activeTab, setActiveTab] = useState<TabId>("ach");
  const [modalOpen, setModalOpen] = useState(false);
  const [tooltip, setTooltip] = useState<TooltipState>({
    visible: false,
    title: "",
    body: "",
    x: 0,
    y: 0,
  });

  const { toasts, show: showToast } = useToasts();
  const { theme, setTheme, cycleTheme, icon: themeIcon } = useTheme();
  const {
    achievements,
    dlc,
    entitlementCount,
    logLines,
    logPath,
    connection,
    loading,
    isDevMode,
    refresh,
    unlockOne,
    unlockAll,
    fetchIcons,
  } = useGameData();

  const connected = connection?.connected ?? false;
  const gameName = connection?.gameName;

  // ── Hover tooltip handlers ────────────────────────────────────────────────
  const handleHoverRow = useCallback(
    (ach: Achievement | null, e?: { clientX: number; clientY: number }) => {
      if (!ach || !e) {
        setTooltip((t) => ({ ...t, visible: false }));
        return;
      }
      setTooltip({
        visible: true,
        title: ach.title,
        body: ach.desc,
        x: e.clientX,
        y: e.clientY,
      });
    },
    []
  );

  const handleHoverStatGated = useCallback(
    (e: { clientX: number; clientY: number }) => {
      setTooltip({
        visible: true,
        title: STAT_GATED_TITLE,
        body: STAT_GATED_BODY,
        x: e.clientX,
        y: e.clientY,
      });
    },
    []
  );

  // ── Unlock single achievement ─────────────────────────────────────────────
  const handleUnlockRow = useCallback(
    async (ach: Achievement) => {
      if (!connected) {
        const err = connection?.lastError
          ? ` (${connection.lastError})`
          : "";
        showToast(
          "🔌 Not connected",
          `Launch a game with Epic Unlocker injected first${err}.`
        );
        return;
      }
      showToast("Unlocking…", `Queued ${ach.title}`);
      const ok = await unlockOne(ach.id);
      if (ok) {
        showToast("⏳ Queued", `${ach.title} — wait 5–30s for the game to process.`);
      } else {
        showToast("❌ Failed", `Could not queue ${ach.title}. Check the log for details.`);
      }
    },
    [unlockOne, showToast, connected, connection?.lastError]
  );

  // ── Sidebar action handlers ───────────────────────────────────────────────
  const handleUnlockAllClick = useCallback(() => {
    if (!connected) {
      showToast("🔌 Not connected", "Launch a game with Epic Unlocker first.");
      return;
    }
    setModalOpen(true);
  }, [showToast, connected]);

  const handleRefreshClick = useCallback(async () => {
    if (!connected) {
      showToast("🔌 Not connected", "Launch a game with Epic Unlocker first.");
      return;
    }
    showToast("🔄 Refreshing…", "Re-syncing from game");
    await refresh();
    showToast("✅ Refreshed", "Achievement list re-synced.");
  }, [refresh, showToast, connected]);

  const handleFetchIconsClick = useCallback(async () => {
    if (!connected) {
      showToast("🔌 Not connected", "Launch a game with Epic Unlocker first to load achievements to fetch icons for.");
      return;
    }
    showToast("🖼️ Fetching icons…", "Downloading achievement icons to local cache.");
    try {
      const results = await fetchIcons();
      const ok = results.filter((r) => r.status === "ok").length;
      const skipped = results.filter((r) => r.status === "skipped").length;
      const failed = results.filter((r) => r.status === "failed").length;
      const noUrl = results.filter((r) => r.status === "no-url").length;
      if (failed > 0) {
        showToast(
          `⚠️ Fetched with ${failed} failure${failed === 1 ? "" : "s"}`,
          `New: ${ok} · Cached: ${skipped} · No URL: ${noUrl} · Failed: ${failed}. See log for details.`
        );
      } else {
        showToast(
          "✅ Icons fetched",
          `New: ${ok} · Cached: ${skipped} · No URL: ${noUrl}.`
        );
      }
    } catch (e) {
      const msg = e instanceof Error ? e.message : String(e);
      showToast("❌ Fetch failed", msg);
    }
  }, [fetchIcons, showToast, connected]);

  const handleConfirmUnlockAll = useCallback(async () => {
    showToast("🚀 Unlock All Started", `Queued all ${achievements.length} achievements.`);
    const ok = await unlockAll();
    if (ok) {
      showToast("⏳ Processing", "Achievements will unlock over 5–30s each as the game processes stats.");
    } else {
      showToast("❌ Failed", "Could not queue unlock-all. Check the log for details.");
    }
  }, [unlockAll, achievements.length, showToast]);

  // ── Log tab toolbar handlers ──────────────────────────────────────────────
  const handleClearLog = useCallback(async () => {
    try {
      await clearLog();
      showToast("🧹 Log cleared", "ScreamAPI.log wiped on disk.");
    } catch (e) {
      const msg = e instanceof Error ? e.message : String(e);
      showToast("❌ Clear failed", msg);
      throw e;
    }
  }, [showToast]);

  const handleOpenLogFile = useCallback(async () => {
    if (!logPath) {
      showToast("🔌 Not connected", "Launch a game first so ScreamAPI reports its log path.");
      throw new Error("No log path");
    }
    try {
      await openLogExternally();
      // Don't toast on success — the editor opening IS the success signal.
      // Toasting on every click would be spammy for a frequently-used button.
    } catch (e) {
      const msg = e instanceof Error ? e.message : String(e);
      showToast("❌ Could not open log", msg);
      throw e;
    }
  }, [logPath, showToast]);

  // ── Global hotkey: Ctrl+Shift+U opens the Unlock All modal ────────────────
  useEffect(() => {
    const handler = (e: KeyboardEvent) => {
      if (e.ctrlKey && e.shiftKey && (e.key === "U" || e.key === "u")) {
        e.preventDefault();
        if (connected) setModalOpen(true);
      }
    };
    window.addEventListener("keydown", handler);
    return () => window.removeEventListener("keydown", handler);
  }, [connected]);

  return (
    <div className="window">
      <Titlebar
        themeIcon={themeIcon}
        onCycleTheme={cycleTheme}
        connected={connected}
        loading={loading}
        gameName={gameName}
      />

      {isDevMode && (
        <div
          style={{
            background: "linear-gradient(90deg, #f67014, #f0b940)",
            color: "#1a1a1a",
            fontSize: "11px",
            fontWeight: 700,
            padding: "4px 14px",
            textAlign: "center",
            letterSpacing: "0.5px",
            flexShrink: 0,
          }}
        >
          ⚠️ DEV MODE — Browser preview with mockup data. In production (Tauri build), this shows real pipe data from Epic Unlocker.
        </div>
      )}

      <Menubar active={activeTab} onChange={setActiveTab} />

      <div className="main">
        <Sidebar
          visible={activeTab === "ach"}
          achievements={achievements}
          toasts={toasts}
          connected={connected}
          loading={loading}
          gameName={gameName}
          onUnlockAllClick={handleUnlockAllClick}
          onRefreshClick={handleRefreshClick}
          onFetchIconsClick={handleFetchIconsClick}
        />

        <div className="content">
          <AchievementsTab
            active={activeTab === "ach"}
            achievements={achievements}
            emojis={achievementEmojis}
            loading={loading}
            connected={connected}
            onHoverRow={handleHoverRow}
            onHoverStatGated={handleHoverStatGated}
            onUnlockRow={handleUnlockRow}
          />
          <DlcTab
            active={activeTab === "dlc"}
            dlc={dlc}
            entitlementCount={entitlementCount}
            loading={loading}
            connected={connected}
          />
          <LogTab
            active={activeTab === "log"}
            lines={logLines}
            loading={loading}
            connected={connected}
            logPath={logPath}
            onClear={handleClearLog}
            onOpenFile={handleOpenLogFile}
          />
          <SettingsTab
            active={activeTab === "settings"}
            theme={theme}
            onThemeChange={setTheme}
          />

          <Statusbar
            connected={connected}
            loading={loading}
            lastError={connection?.lastError ?? null}
            logSize={
              logLines.length > 0
                ? `${(logLines.length * 0.045).toFixed(1)} KB`
                : "0 KB"
            }
          />
        </div>
      </div>

      <UnlockAllModal
        open={modalOpen}
        onClose={() => setModalOpen(false)}
        onConfirm={handleConfirmUnlockAll}
        totalCount={achievements.length}
      />

      <Tooltip state={tooltip} />
    </div>
  );
}
