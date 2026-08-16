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

import { useEffect, useState } from "react";
import type { ThemeName } from "../../hooks/useTheme";
import {
  getSettings,
  saveSettings,
  getSdkLogPath,
  openSdkLogExternally,
  openLogExternally,
} from "../../lib/api";
import type { AppSettings } from "../../types";
import { t, type Locale } from "../../i18n";

interface SettingsTabProps {
  active: boolean;
  theme: ThemeName;
  onThemeChange: (t: ThemeName) => void;
  locale?: Locale;
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

export default function SettingsTab({ active, theme, onThemeChange, locale = "en" }: SettingsTabProps) {
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
