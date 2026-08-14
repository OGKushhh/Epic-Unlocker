/**
 * Titlebar — frameless window titlebar with window controls.
 *
 * Layout (left → right):
 *   [🎨 theme toggle]  Title  [conn chip]  [─ minimize · □ maximize · ✕ close]
 *
 * Window controls use the Windows convention (minimize · maximize · close,
 * left-to-right) with SVG icons:
 *   - ─ (minimize)  → window_minimize
 *   - □ (maximize)  → window_toggle_maximize
 *   - ✕ (close)     → window_close (red on hover)
 *
 * In browser preview (no Tauri runtime), the invocations fail silently.
 */

import { invoke } from "@tauri-apps/api/core";

interface TitlebarProps {
  themeIcon: string;
  onCycleTheme: () => void;
  connected?: boolean;
  loading?: boolean;
  gameName?: string;
}

async function safeInvoke(cmd: string) {
  try {
    await invoke(cmd);
  } catch {
    // Running in browser preview without Tauri runtime — no-op
  }
}

export default function Titlebar({
  themeIcon,
  onCycleTheme,
  connected = false,
  loading = false,
  gameName,
}: TitlebarProps) {
  const pipColor = loading
    ? "#ffbd2e"
    : connected
    ? "var(--green)"
    : "var(--red)";
  const pipGlow = loading
    ? "0 0 8px #ffbd2e"
    : connected
    ? "0 0 8px var(--green)"
    : "0 0 8px var(--red)";
  const connText = loading
    ? "Connecting…"
    : connected
    ? `Connected · ${gameName ?? "Game"}`
    : "Disconnected";

  return (
    <div className="titlebar" data-tauri-drag-region>
      {/* Left: theme toggle */}
      <div
        className="theme-toggle"
        title="Cycle themes (Gold → Light → Dark)"
        onClick={onCycleTheme}
      >
        {themeIcon}
      </div>

      <div className="title" data-tauri-drag-region>
        Epic Unlocker — Achievement Manager
      </div>

      <div className="conn" title={connected ? "Pipe connected" : "Pipe not connected"}>
        <span
          className="pip"
          style={{ background: pipColor, boxShadow: pipGlow }}
        />
        <span>{connText}</span>
      </div>

      {/* Right: Windows-style window controls (─ minimize · □ maximize · ✕ close) */}
      <div className="win-controls">
        <button
          className="win-btn"
          title="Minimize"
          onClick={() => safeInvoke("window_minimize")}
          aria-label="Minimize"
        >
          <svg width="10" height="10" viewBox="0 0 10 10">
            <rect x="0" y="4.5" width="10" height="1" fill="currentColor" />
          </svg>
        </button>
        <button
          className="win-btn"
          title="Maximize / Restore"
          onClick={() => safeInvoke("window_toggle_maximize")}
          aria-label="Maximize / Restore"
        >
          <svg width="10" height="10" viewBox="0 0 10 10">
            <rect
              x="0.5"
              y="0.5"
              width="9"
              height="9"
              stroke="currentColor"
              strokeWidth="1"
              fill="none"
            />
          </svg>
        </button>
        <button
          className="win-btn close"
          title="Close"
          onClick={() => safeInvoke("window_close")}
          aria-label="Close"
        >
          <svg width="10" height="10" viewBox="0 0 10 10">
            <path
              d="M0.5 0.5 L9.5 9.5 M9.5 0.5 L0.5 9.5"
              stroke="currentColor"
              strokeWidth="1"
              fill="none"
            />
          </svg>
        </button>
      </div>
    </div>
  );
}
