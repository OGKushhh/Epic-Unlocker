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
import ErrorBoundary from "./components/ErrorBoundary";
import AchievementsTab from "./components/tabs/AchievementsTab";
import DlcTab from "./components/tabs/DlcTab";
import LogTab from "./components/tabs/LogTab";
import SettingsTab from "./components/tabs/SettingsTab";
import { useToasts } from "./hooks/useToasts";
import { useTheme } from "./hooks/useTheme";
import { useMusic } from "./hooks/useMusic";
import { useGameData } from "./hooks/useGameData";
import { useManifestSync } from "./hooks/useManifestSync";
import { achievementEmojis, type Achievement } from "./data/mockupData";
import { clearLog, openLogExternally } from "./lib/api";
import { t, isRTL, type Locale } from "./i18n";
import ConsentGate from "./components/ConsentGate";

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
  const { playing: musicPlaying, volume, muted, togglePlay, setVolume, toggleMute } = useMusic();
  const [locale, setLocale] = useState<Locale>("en");

  // Manifest sharing consent + sync
  const {
    consentState,
    syncing: manifestSyncing,
    syncManifests,
    acceptConsent,
    declineConsent,
    toggleConsent,
  } = useManifestSync({ locale, showToast });

  // Show consent modal when not dismissed (on decline: re-shows every launch)
  const showConsentGate = !consentState.dismissed;

  // Localized stat-gated tooltip content
  const STAT_GATED_TITLE = t("tooltip.statGatedTitle", locale);
  const STAT_GATED_BODY = t("tooltip.statGatedBody", locale);

  const {
    achievements,
    dlc,
    entitlementCount,
    logLines,
    logPath,
    logFileSize,
    connection,
    loading,
    isDevMode,
    refresh,
    unlockOne,
    unlockAll,
    fetchIcons,
    fetchRarity,
    clearIconCache,
    gameInfo,
  } = useGameData();

  const connected = connection?.connected ?? false;
  const gameName = connection?.gameName;

  // ── Locale toggle ────────────────────────────────────────────────
  const handleToggleLocale = useCallback(() => {
    setLocale((l) => l === "en" ? "ar" : "en");
  }, []);

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
    [locale]  // re-create when locale changes so tooltip text stays translated
  );

  // ── Unlock single achievement ─────────────────────────────────────────────
  const handleUnlockRow = useCallback(
    async (ach: Achievement) => {
      if (!connected) {
        const err = connection?.lastError
          ? ` (${connection.lastError})`
          : "";
        showToast(
          `🔌 ${t("toast.notConnected", locale)}`,
          `${t("toast.notConnectedBody", locale)}${err}`
        );
        return;
      }
      showToast(t("toast.unlocking", locale), `${t("toast.unlockingBody", locale)} ${ach.title}`);
      const ok = await unlockOne(ach.id);
      if (ok) {
        showToast(`⏳ ${t("toast.queued", locale)}`, `${ach.title} ${t("toast.queuedBody", locale)}`);
      } else {
        showToast(`❌ ${t("toast.failed", locale)}`, `${ach.title} — ${t("toast.failedBody", locale)}`);
      }
    },
    [unlockOne, showToast, connected, connection?.lastError, locale]
  );

  // ── Sidebar action handlers ───────────────────────────────────────────────
  const handleUnlockAllClick = useCallback(() => {
    if (!connected) {
      showToast(`🔌 ${t("toast.notConnected", locale)}`, t("toast.notConnectedBody", locale));
      return;
    }
    setModalOpen(true);
  }, [showToast, connected, locale]);

  const handleRefreshClick = useCallback(async () => {
    if (!connected) {
      showToast(`🔌 ${t("toast.notConnected", locale)}`, t("toast.notConnectedBody", locale));
      return;
    }
    showToast(`🔄 ${t("toast.refreshing", locale)}`, t("toast.refreshingBody", locale));
    await refresh();
    showToast(`✅ ${t("toast.refreshed", locale)}`, t("toast.refreshedBody", locale));
  }, [refresh, showToast, connected, locale]);

  const handleFetchIconsClick = useCallback(async () => {
    if (!connected) {
      showToast(`🔌 ${t("toast.notConnected", locale)}`, t("toast.notConnectedBody", locale));
      return;
    }
    showToast(`*️⃣ ${t("toast.fetchIcons", locale)}`, t("toast.fetchIconsBody", locale));
    try {
      const results = await fetchIcons();
      const ok = results.filter((r) => r.status === "ok").length;
      const skipped = results.filter((r) => r.status === "skipped").length;
      const failed = results.filter((r) => r.status === "failed").length;
      const noUrl = results.filter((r) => r.status === "no-url").length;
      if (failed > 0) {
        showToast(
          `⚠️ ${t("toast.iconsFetchedFail", locale)} (${failed})`,
          t("toast.iconsDetailFailed", locale).replace("{ok}", String(ok)).replace("{skipped}", String(skipped)).replace("{noUrl}", String(noUrl)).replace("{failed}", String(failed))
        );
      } else {
        showToast(
          `✅ ${t("toast.iconsFetched", locale)}`,
          t("toast.iconsDetail", locale).replace("{ok}", String(ok)).replace("{skipped}", String(skipped)).replace("{noUrl}", String(noUrl))
        );
      }
    } catch (e) {
      const msg = e instanceof Error ? e.message : String(e);
      showToast(`❌ ${t("toast.fetchFailed", locale)}`, msg);
    }
  }, [fetchIcons, showToast, connected, locale]);

  const handleFetchRarityClick = useCallback(async () => {
    if (!connected) {
      showToast(`🔌 ${t("toast.notConnected", locale)}`, t("toast.notConnectedBody", locale));
      return;
    }
    showToast(`🏆 ${t("toast.fetchRarity", locale)}`, t("toast.fetchRarityBody", locale));
    try {
      const count = await fetchRarity();
      if (count > 0) {
        showToast(`✅ ${t("toast.rarityFetched", locale)}`, `${count} ${t("toast.rarityFetchedBody", locale)}`);
      } else {
        showToast(`⚠️ ${t("toast.noRarity", locale)}`, t("toast.noRarityBody", locale));
      }
    } catch (e) {
      const msg = e instanceof Error ? e.message : String(e);
      showToast(`❌ ${t("toast.rarityFetchFailed", locale)}`, msg);
    }
  }, [fetchRarity, showToast, connected, locale]);

  const handleClearCacheClick = useCallback(async () => {
    showToast(`🧹 ${t("toast.clearingCache", locale)}`, t("toast.clearingCacheBody", locale));
    try {
      const deleted = await clearIconCache();
      if (deleted > 0) {
        showToast(`✅ ${t("toast.cacheCleared", locale)}`, `${t("toast.cacheClearedCount", locale).replace("{count}", String(deleted))} ${t("toast.cacheClearedBody", locale)}`);
      } else {
        showToast(`✅ ${t("toast.cacheCleared", locale)}`, t("toast.noCachedIcons", locale));
      }
    } catch (e) {
      const msg = e instanceof Error ? e.message : String(e);
      showToast(`❌ ${t("toast.clearFailed", locale)}`, msg);
    }
  }, [clearIconCache, showToast, locale]);

  const handleConfirmUnlockAll = useCallback(async () => {
    showToast(`🚀 ${t("toast.unlockAllStarted", locale)}`, `${t("toast.unlockAllBody", locale)} (${achievements.length})`);
    const ok = await unlockAll();
    if (ok) {
      showToast(`⏳ ${t("toast.processing", locale)}`, t("toast.processingBody", locale));
    } else {
      showToast(`❌ ${t("toast.failed", locale)}`, t("toast.unlockAllFailed", locale));
    }
  }, [unlockAll, achievements.length, showToast, locale]);

  // ── Log tab toolbar handlers ──────────────────────────────────────────────
  const handleClearLog = useCallback(async () => {
    try {
      await clearLog();
      showToast(`🧹 ${t("toast.logCleared", locale)}`, t("toast.logClearedBody", locale));
    } catch (e) {
      const msg = e instanceof Error ? e.message : String(e);
      showToast(`❌ ${t("toast.clearFailed", locale)}`, msg);
      throw e;
    }
  }, [showToast, locale]);

  const handleOpenLogFile = useCallback(async () => {
    if (!logPath) {
      showToast(`🔌 ${t("toast.notConnected", locale)}`, t("toast.notConnectedLogBody", locale));
      throw new Error("No log path");
    }
    try {
      await openLogExternally();
      // Don't toast on success — the editor opening IS the success signal.
      // Toasting on every click would be spammy for a frequently-used button.
    } catch (e) {
      const msg = e instanceof Error ? e.message : String(e);
      showToast(`❌ ${t("toast.couldNotOpenLog", locale)}`, msg);
      throw e;
    }
  }, [logPath, showToast, locale]);

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
    <div className="window" dir={isRTL(locale) ? "rtl" : "ltr"}>
      <Titlebar
        themeIcon={themeIcon}
        onCycleTheme={cycleTheme}
        locale={locale}
        onToggleLocale={handleToggleLocale}
        musicPlaying={musicPlaying}
        onToggleMusic={togglePlay}
        volume={volume}
        onVolumeChange={setVolume}
        muted={muted}
        onToggleMute={toggleMute}
        connected={connected}
        loading={loading}
        gameName={gameName}
        titleText={t("titlebar.title", locale)}
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

      <Menubar active={activeTab} onChange={setActiveTab} locale={locale} />

      <div className="main">
        <ErrorBoundary>
          <Sidebar
            visible={activeTab === "ach"}
            achievements={achievements}
            toasts={toasts}
            connected={connected}
            loading={loading}
            gameName={gameName}
            eosVersion={gameInfo?.eosVersion}
            locale={locale}
            onUnlockAllClick={handleUnlockAllClick}
            onRefreshClick={handleRefreshClick}
            onFetchIconsClick={handleFetchIconsClick}
            onFetchRarityClick={handleFetchRarityClick}
            onClearCacheClick={handleClearCacheClick}
          />
        </ErrorBoundary>

        <ErrorBoundary>
          <div className="content">
            <AchievementsTab
              active={activeTab === "ach"}
              achievements={achievements}
              emojis={achievementEmojis}
              loading={loading}
              connected={connected}
              locale={locale}
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
              locale={locale}
            />
            <LogTab
              active={activeTab === "log"}
              lines={logLines}
              loading={loading}
              connected={connected}
              logPath={logPath}
              locale={locale}
              onClear={handleClearLog}
              onOpenFile={handleOpenLogFile}
            />
            <SettingsTab
              active={activeTab === "settings"}
              theme={theme}
              onThemeChange={setTheme}
              locale={locale}
              manifestConsent={consentState.consent}
              onManifestConsentChange={toggleConsent}
              onSyncManifests={syncManifests}
              manifestSyncing={manifestSyncing}
            />

            <Statusbar
              connected={connected}
              loading={loading}
              lastError={connection?.lastError ?? null}
              logSizeBytes={logFileSize}
              locale={locale}
            />
          </div>
        </ErrorBoundary>
      </div>

      <UnlockAllModal
        open={modalOpen}
        onClose={() => setModalOpen(false)}
        onConfirm={handleConfirmUnlockAll}
        totalCount={achievements.length}
        locale={locale}
      />

      <ConsentGate
        open={showConsentGate}
        onAccept={acceptConsent}
        onDecline={declineConsent}
        locale={locale}
      />

      <Tooltip state={tooltip} />
    </div>
  );
}
