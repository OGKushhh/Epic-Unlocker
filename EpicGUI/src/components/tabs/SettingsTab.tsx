/**
 * Settings Tab — application preferences.
 *
 * Groups:
 *   - Appearance (theme select)
 *   - Behavior (auto-refresh, connect on launch)   [G2: persisted to backend]
 *   - Hotkeys (read-only display of active hotkeys)
 *   - Logging (max log lines + ScreamAPI.log + EOS SDK log — unified)
 *
 * G2: All settings persist to a JSON file in the app's local data dir via
 * the get_settings / save_settings Tauri commands. The theme select was
 * already wired (via parent's setTheme); the other controls are now wired
 * to load on mount and save on change.
 *
 * SDK log (A1): The EOS SDK's own log stream is written to ScreamAPI_SDK.log
 * next to ScreamAPI.log. We show the path here + an "Open" button so the
 * user can launch it in their preferred editor without leaving the GUI.
 */

import { useCallback, useEffect, useState } from "react";
import type { ThemeName } from "../../hooks/useTheme";
import {
  getSettings,
  saveSettings,
  getSdkLogPath,
  openSdkLogExternally,
  openLogExternally,
  clearManifestCache,
} from "../../lib/api";
import type { AppSettings, ManifestUploadResult } from "../../types";
import { t, type Locale } from "../../i18n";
import type { UploadProgressEvent } from "../../hooks/useManifestSync";

interface SettingsTabProps {
  active: boolean;
  theme: ThemeName;
  onThemeChange: (t: ThemeName) => void;
  locale?: Locale;
  manifestConsent?: boolean;
  onManifestConsentChange?: (enabled: boolean) => void;
  onSyncManifests?: () => void;
  manifestSyncing?: boolean;
  manifestProgress?: UploadProgressEvent | null;
  uploadResults?: ManifestUploadResult[] | null;
  showToast?: (title: string, body: string) => void;
}

interface ToggleProps {
  initial?: boolean;
  onChange?: (on: boolean) => void;
}

function Toggle({ initial = false, onChange }: ToggleProps) {
  const [on, setOn] = useState(initial);
  // Sync with parent prop changes (e.g. when settings load asynchronously)
  useEffect(() => setOn(initial), [initial]);
  const toggle = () => {
    const next = !on;
    setOn(next);
    onChange?.(next);
  };
  return (
    <div
      className={on ? "toggle-switch on" : "toggle-switch"}
      onClick={toggle}
      onKeyDown={(e) => {
        if (e.key === " " || e.key === "Enter") {
          e.preventDefault();
          toggle();
        }
      }}
      role="switch"
      aria-checked={on}
      tabIndex={0}
    >
      <div className="knob" />
    </div>
  );
}

export default function SettingsTab({
  active,
  theme,
  onThemeChange,
  locale = "en",
  manifestConsent = true,
  onManifestConsentChange,
  onSyncManifests,
  manifestSyncing = false,
  manifestProgress,
  uploadResults,
  showToast,
}: SettingsTabProps) {
  // G2: persisted settings — load on mount, save on change.
  const [settings, setSettings] = useState<AppSettings | null>(null);
  // A1: SDK log path (empty string = not connected yet).
  const [sdkLogPath, setSdkLogPath] = useState<string>("");

  useEffect(() => {
    if (!active) return;
    getSettings()
      .then((s) => setSettings(s))
      .catch((e) => console.error("get_settings failed:", e));
    getSdkLogPath()
      .then((p) => setSdkLogPath(p))
      .catch(() => setSdkLogPath(""));
  }, [active]);

  // Re-fetch SDK log path periodically (every 5s while tab is active) —
  // it's empty until the game connects and the LogPath packet arrives.
  useEffect(() => {
    if (!active) return;
    const h = window.setInterval(() => {
      getSdkLogPath().then((p) => setSdkLogPath(p)).catch(() => {});
    }, 5000);
    return () => window.clearInterval(h);
  }, [active]);

  const update = (patch: Partial<AppSettings>) => {
    if (!settings) return;
    const next = { ...settings, ...patch };
    setSettings(next);
    saveSettings(next).catch((e) =>
      console.error("save_settings failed:", e)
    );
  };

  const handleClearManifestCache = useCallback(async () => {
    const confirmed = window.confirm(
      t("settings.clearManifestCacheConfirm", locale)
    );
    if (!confirmed) return;

    try {
      const deleted = await clearManifestCache();
      if (deleted > 0) {
        showToast?.(
          `🧹 ${t("toast.manifestCacheCleared", locale)}`,
          ""
        );
      } else {
        showToast?.(
          `ℹ️ ${t("toast.manifestCacheAlreadyEmpty", locale)}`,
          ""
        );
      }
      // Optionally refresh the sync state
      onSyncManifests?.();
    } catch (e) {
      const msg = e instanceof Error ? e.message : String(e);
      showToast?.(
        `❌ ${t("toast.manifestCacheClearFailed", locale)}`,
        msg
      );
    }
  }, [locale, showToast, onSyncManifests]);

  return (
    <div className={active ? "tab-content active" : "tab-content"} id="tab-settings">
      <div className="settings-wrap">
        {/* Appearance */}
        <div className="settings-group">
          <div className="group-title">🎨 {t("settings.appearance", locale)}</div>
          <div className="setting-row">
            <div>
              <div className="label">{t("settings.themeLabel", locale)}</div>
              <div className="desc">{t("settings.themeDesc", locale)}</div>
            </div>
            <div className="control">
              <select
                className="setting-select"
                id="themeSelect"
                value={theme}
                onChange={(e) => onThemeChange(e.target.value as ThemeName)}
              >
                <option value="gold">Gold</option>
                <option value="light">Light</option>
                <option value="dark">Dark</option>
              </select>
            </div>
          </div>
        </div>

        {/* Behavior */}
        <div className="settings-group">
          <div className="group-title">⚙️ {t("settings.behavior", locale)}</div>
          <div className="setting-row">
            <div>
              <div className="label">{t("settings.autoRefresh", locale)}</div>
              <div className="desc">{t("settings.autoRefreshDesc", locale)}</div>
            </div>
            <div className="control">
              <select
                className="setting-select"
                value={settings?.autoRefreshIntervalMs ?? 2000}
                onChange={(e) =>
                  update({ autoRefreshIntervalMs: parseInt(e.target.value, 10) })
                }
                disabled={!settings}
              >
                <option value={1000}>1 s</option>
                <option value={2000}>2 s</option>
                <option value={5000}>5 s</option>
              </select>
            </div>
          </div>
          <div className="setting-row">
            <div>
              <div className="label">{t("settings.connectOnLaunch", locale)}</div>
              <div className="desc">{t("settings.connectOnLaunchDesc", locale)}</div>
            </div>
            <div className="control">
              <Toggle
                initial={settings?.connectOnLaunch ?? true}
                onChange={(on) => update({ connectOnLaunch: on })}
              />
            </div>
          </div>
        </div>

        {/* Hotkeys */}
        <div className="settings-group">
          <div className="group-title">⌨️ {t("settings.hotkeys", locale)}</div>
          <div className="setting-row">
            <div>
              <div className="label">{t("settings.hotkeyUnlockAll", locale)}</div>
            </div>
            <div className="control">
              <span className="hotkey-display">Ctrl + Shift + U</span>
            </div>
          </div>
          <div className="setting-row">
            <div>
              <div className="label">{t("settings.hotkeyUnlockList", locale)}</div>
            </div>
            <div className="control">
              <span className="hotkey-display">Ctrl + Shift + L</span>
            </div>
          </div>
          <div className="setting-row">
            <div>
              <div className="label">{t("settings.hotkeyLogOverlay", locale)}</div>
            </div>
            <div className="control">
              <span className="hotkey-display">Shift + F5</span>
            </div>
          </div>
        </div>

        {/* Manifest Sharing (opt-out consent) */}
        <div className="settings-group">
          <div className="group-title">📋 {t("settings.manifestSharing", locale)}</div>
          <div className="setting-row">
            <div>
              <div className="label">{t("settings.shareManifests", locale)}</div>
              <div className="desc">{t("settings.shareManifestsDesc", locale)}</div>
            </div>
            <div className="control">
              <Toggle
                initial={manifestConsent}
                onChange={(on) => onManifestConsentChange?.(on)}
              />
            </div>
          </div>
          {manifestConsent && (
            <div className="setting-row">
              <div>
                <div className="label">{t("settings.syncManifests", locale)}</div>
                <div className="desc">{t("settings.syncManifestsDesc", locale)}</div>
              </div>
              <div className="control">
                <button
                  className="setting-select"
                  disabled={manifestSyncing}
                  onClick={() => onSyncManifests?.()}
                >
                  {manifestSyncing ? "…" : t("settings.syncManifests", locale)}
                </button>
              </div>
            </div>
          )}
          {/* Upload progress bar */}
          {manifestConsent && manifestSyncing && manifestProgress && (
            <div className="setting-row" style={{ flexDirection: "column", alignItems: "stretch", gap: "6px" }}>
              <div style={{ fontSize: "13px", opacity: 0.9 }}>
                {manifestProgress.status === "chunking"
                  ? `📦 ${manifestProgress.currentFile}`
                  : `📤 ${t("toast.manifestUploading", locale)} ${manifestProgress.completedFiles}/${manifestProgress.totalFiles}`}
              </div>
              <div style={{
                width: "100%",
                height: "8px",
                background: "rgba(255,255,255,0.1)",
                borderRadius: "4px",
                overflow: "hidden",
              }}>
                <div style={{
                  width: `${manifestProgress.overallPercent}%`,
                  height: "100%",
                  background: "linear-gradient(90deg, #f0b940, #f67014)",
                  borderRadius: "4px",
                  transition: "width 0.3s ease",
                }} />
              </div>
              <div style={{ fontSize: "11px", opacity: 0.7, display: "flex", justifyContent: "space-between" }}>
                <span>{manifestProgress.currentFile}</span>
                <span>{manifestProgress.overallPercent}%</span>
              </div>
            </div>
          )}
          {/* Upload errors list — show when there are failed results */}
          {manifestConsent && uploadResults && uploadResults.some((r) => r.status === "error") && (
            <div className="setting-row" style={{ flexDirection: "column", alignItems: "stretch", gap: "8px" }}>
              <div className="label" style={{ opacity: 0.9 }}>
                ❌ Upload Errors ({uploadResults.filter((r) => r.status === "error").length})
              </div>
              <div style={{
                maxHeight: "160px",
                overflowY: "auto",
                display: "flex",
                flexDirection: "column",
                gap: "4px",
              }}>
                {uploadResults
                  .filter((r) => r.status === "error")
                  .map((r, i) => (
                    <div
                      key={i}
                      style={{
                        fontSize: "12px",
                        padding: "6px 10px",
                        borderRadius: "4px",
                        background: "rgba(239, 68, 68, 0.12)",
                        border: "1px solid rgba(239, 68, 68, 0.25)",
                      }}
                    >
                      <div style={{ fontWeight: 600, marginBottom: "2px", wordBreak: "break-all" }}>
                        {r.fileName}
                      </div>
                      <div style={{ opacity: 0.8, wordBreak: "break-word" }}>
                        {r.detail}
                      </div>
                    </div>
                  ))}
              </div>
            </div>
          )}
          {manifestConsent && (
            <div className="setting-row">
              <div>
                <div className="label">{t("settings.clearManifestCache", locale)}</div>
                <div className="desc">{t("settings.clearManifestCacheDesc", locale)}</div>
              </div>
              <div className="control">
                <button
                  className="setting-select"
                  onClick={handleClearManifestCache}
                >
                  🗑️ {t("settings.clearManifestCache", locale)}
                </button>
              </div>
            </div>
          )}
        </div>

        {/* Logging (unified: ScreamAPI.log + SDK log) */}
        <div className="settings-group">
          <div className="group-title">📁 {t("settings.logging", locale)}</div>
          <div className="setting-row">
            <div>
              <div className="label">{t("settings.maxLogLines", locale)}</div>
              <div className="desc">{t("settings.maxLogLinesDesc", locale)}</div>
            </div>
            <div className="control">
              <select
                className="setting-select"
                value={settings?.maxLogLines ?? 20000}
                onChange={(e) =>
                  update({ maxLogLines: parseInt(e.target.value, 10) })
                }
                disabled={!settings}
              >
                <option value={10000}>10,000</option>
                <option value={20000}>20,000</option>
                <option value={50000}>50,000</option>
                <option value={100000}>100,000</option>
              </select>
            </div>
          </div>
          <div className="setting-row">
            <div>
              <div className="label">{t("settings.screamApiLog", locale)}</div>
              <div className="desc">{t("settings.screamApiLogDesc", locale)}</div>
            </div>
            <div className="control">
              <button
                className="setting-select"
                onClick={() => openLogExternally().catch((e) =>
                  console.error("Failed to open log:", e)
                )}
              >
                {t("settings.open", locale)}
              </button>
            </div>
          </div>
          <div className="setting-row">
            <div>
              <div className="label">{t("settings.sdkLog", locale)}</div>
              <div className="desc">
                {sdkLogPath
                  ? t("settings.sdkLogDesc", locale)
                  : t("settings.sdkLogNotAvailable", locale)}
              </div>
              {sdkLogPath && (
                <div
                  className="desc"
                  style={{
                    fontFamily: "monospace",
                    fontSize: "11px",
                    marginTop: "4px",
                    opacity: 0.7,
                    wordBreak: "break-all",
                  }}
                >
                  {sdkLogPath}
                </div>
              )}
            </div>
            <div className="control">
              <button
                className="setting-select"
                disabled={!sdkLogPath}
                onClick={() =>
                  openSdkLogExternally().catch((e) =>
                    console.error("Failed to open SDK log:", e)
                  )
                }
              >
                {t("settings.open", locale)}
              </button>
            </div>
          </div>
        </div>
      </div>
    </div>
  );
}
