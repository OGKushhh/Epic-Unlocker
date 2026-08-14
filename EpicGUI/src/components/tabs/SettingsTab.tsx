/**
 * Settings Tab — application preferences.
 *
 * Groups:
 *   - Appearance (theme select)
 *   - Behavior (auto-refresh, connect on launch)   [G2: persisted to backend]
 *   - Hotkeys (read-only display of active hotkeys)
 *   - Logging (max log lines display)              [G2: persisted to backend]
 *   - SDK Log (A1: path + Open button for ScreamAPI_SDK.log)
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

interface SettingsTabProps {
  active: boolean;
  theme: ThemeName;
  onThemeChange: (t: ThemeName) => void;
}

interface ToggleProps {
  initial?: boolean;
  onChange?: (on: boolean) => void;
}

function Toggle({ initial = false, onChange }: ToggleProps) {
  const [on, setOn] = useState(initial);
  // Sync with parent prop changes (e.g. when settings load asynchronously)
  useEffect(() => setOn(initial), [initial]);
  return (
    <div
      className={`toggle-switch${on ? " on" : ""}`}
      onClick={() => {
        const next = !on;
        setOn(next);
        onChange?.(next);
      }}
      role="switch"
      aria-checked={on}
    >
      <div className="knob" />
    </div>
  );
}

export default function SettingsTab({ active, theme, onThemeChange }: SettingsTabProps) {
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
    <div className={`tab-content${active ? " active" : ""}`} id="tab-settings">
      <div className="settings-wrap">
        {/* Appearance */}
        <div className="settings-group">
          <div className="group-title">🎨 Appearance</div>
          <div className="setting-row">
            <div>
              <div className="label">Theme</div>
              <div className="desc">Choose your preferred color scheme</div>
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
          <div className="group-title">⚙️ Behavior</div>
          <div className="setting-row">
            <div>
              <div className="label">Auto-refresh interval</div>
              <div className="desc">
                How often to poll the game for updates
              </div>
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
              <div className="label">Connect on launch</div>
              <div className="desc">
                Automatically attempt to connect when the app starts
              </div>
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
          <div className="group-title">⌨️ Hotkeys</div>
          <div className="setting-row">
            <div>
              <div className="label">Unlock All</div>
            </div>
            <div className="control">
              <span className="hotkey-display">Ctrl + Shift + U</span>
            </div>
          </div>
          <div className="setting-row">
            <div>
              <div className="label">Unlock from List</div>
            </div>
            <div className="control">
              <span className="hotkey-display">Ctrl + Shift + L</span>
            </div>
          </div>
          <div className="setting-row">
            <div>
              <div className="label">Toggle Log Overlay</div>
            </div>
            <div className="control">
              <span className="hotkey-display">Shift + F5</span>
            </div>
          </div>
        </div>

        {/* Logging */}
        <div className="settings-group">
          <div className="group-title">📁 Logging</div>
          <div className="setting-row">
            <div>
              <div className="label">Maximum log lines</div>
              <div className="desc">
                Number of lines retained in the log view (older lines are dropped)
              </div>
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
              <div className="label">ScreamAPI log</div>
              <div className="desc">
                Open ScreamAPI.log (the curated unlock-debugging log) externally
              </div>
            </div>
            <div className="control">
              <button
                className="setting-select"
                onClick={() => openLogExternally().catch((e) =>
                  alert(`Failed to open log: ${e}`)
                )}
              >
                Open ScreamAPI.log
              </button>
            </div>
          </div>
        </div>

        {/* SDK Log (A1) */}
        <div className="settings-group">
          <div className="group-title">🔧 SDK Log (verbose)</div>
          <div className="setting-row">
            <div>
              <div className="label">EOS SDK log file</div>
              <div className="desc">
                {sdkLogPath
                  ? "Verbose EOS SDK backend trace. Separate from ScreamAPI.log to avoid noise."
                  : "Not available until a game connects (launch a game with Epic Unlocker)."}
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
                    alert(`Failed to open SDK log: ${e}`)
                  )
                }
              >
                Open SDK log
              </button>
            </div>
          </div>
        </div>
      </div>
    </div>
  );
}
