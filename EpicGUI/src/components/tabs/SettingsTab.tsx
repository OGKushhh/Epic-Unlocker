/**
 * Settings Tab — application preferences.
 *
 * Groups:
 *   - Appearance (theme select)
 *   - Behavior (auto-refresh, connect on launch)
 *   - Hotkeys (read-only display of active hotkeys)
 *   - Logging (max log lines display)
 *
 * The theme select is wired to the parent's setTheme (so it stays in sync with
 * the titlebar's cycle button). All other controls are visual carbon copies
 * (toggle switches flip locally), to be wired to real settings persistence in
 * a later iteration.
 */

import { useState } from "react";
import type { ThemeName } from "../../hooks/useTheme";

interface SettingsTabProps {
  active: boolean;
  theme: ThemeName;
  onThemeChange: (t: ThemeName) => void;
}

interface ToggleProps {
  initial?: boolean;
}

function Toggle({ initial = false }: ToggleProps) {
  const [on, setOn] = useState(initial);
  return (
    <div
      className={`toggle-switch${on ? " on" : ""}`}
      onClick={() => setOn((v) => !v)}
      role="switch"
      aria-checked={on}
    >
      <div className="knob" />
    </div>
  );
}

export default function SettingsTab({ active, theme, onThemeChange }: SettingsTabProps) {
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
              <select className="setting-select" defaultValue="500 ms">
                <option>250 ms</option>
                <option>500 ms</option>
                <option>1 s</option>
                <option>2 s</option>
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
              <Toggle initial />
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
              <span className="hotkey-display">20,000</span>
            </div>
          </div>
        </div>
      </div>
    </div>
  );
}
