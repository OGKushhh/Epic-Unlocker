/**
 * Titlebar — frameless window titlebar with window controls.
 *
 * Layout (left → right):
 *   [🌐 lang] [🎨 theme] [🎵 music] [🔊━━●━━ volume]  Title  [conn chip]  [─ · □ · ✕]
 *
 * Language toggle: EN ⇄ AR (Arabic enables RTL)
 * Music: on/off toggle + inline volume slider + mute
 * Theme: cycles Gold → Light → Dark
 * Window controls: minimize, maximize, close (Windows convention)
 */

import { invoke } from "@tauri-apps/api/core";
import { t, type Locale } from "../i18n";

interface TitlebarProps {
  themeIcon: string;
  onCycleTheme: () => void;
  locale: Locale;
  onToggleLocale: () => void;
  musicPlaying: boolean;
  onToggleMusic: () => void;
  volume: number;
  onVolumeChange: (v: number) => void;
  muted: boolean;
  onToggleMute: () => void;
  connected?: boolean;
  loading?: boolean;
  gameName?: string;
  titleText: string;
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
  locale,
  onToggleLocale,
  musicPlaying,
  onToggleMusic,
  volume,
  onVolumeChange,
  muted,
  onToggleMute,
  connected = false,
  loading = false,
  gameName,
  titleText,
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
    ? t("status.connecting", locale)
    : connected
    ? `${t("status.connected", locale)} · ${gameName ?? "Game"}`
    : t("status.disconnected", locale);

  return (
    <div className="titlebar" data-tauri-drag-region>
      {/* Theme toggle */}
      <div
        className="theme-toggle"
        title={t("titlebar.cycleThemes", locale)}
        onClick={onCycleTheme}
        role="button"
        tabIndex={0}
        onKeyDown={(e) => { if (e.key === " " || e.key === "Enter") { e.preventDefault(); onCycleTheme(); } }}
      >
        {themeIcon}
      </div>

      {/* Language toggle */}
      <div
        className="titlebar-btn"
        title={t("lang.toggle", locale)}
        onClick={onToggleLocale}
        role="button"
        tabIndex={0}
        onKeyDown={(e) => { if (e.key === " " || e.key === "Enter") { e.preventDefault(); onToggleLocale(); } }}
        style={{ fontSize: "14px", cursor: "pointer" }}
      >
        {locale === "en" ? "🌐" : "🌐"}
        <span style={{ fontSize: "10px", marginLeft: "2px", opacity: 0.8 }}>
          {locale === "en" ? "EN" : "AR"}
        </span>
      </div>

      {/* Music toggle */}
      <div
        className="titlebar-btn"
        title={musicPlaying ? t("music.toggleOff", locale) : t("music.toggleOn", locale)}
        onClick={onToggleMusic}
        role="button"
        tabIndex={0}
        onKeyDown={(e) => { if (e.key === " " || e.key === "Enter") { e.preventDefault(); onToggleMusic(); } }}
        style={{
          fontSize: "14px",
          cursor: "pointer",
          opacity: musicPlaying ? 1 : 0.5,
        }}
      >
        {musicPlaying ? "🎵" : "🎵"}
      </div>

      {/* Volume controls (visible when music is playing) */}
      {musicPlaying && (
        <div
          className="volume-controls"
          style={{
            display: "flex",
            alignItems: "center",
            gap: "4px",
            marginLeft: "2px",
          }}
        >
          <div
            className="titlebar-btn"
            title={muted ? t("music.unmute", locale) : t("music.mute", locale)}
            onClick={onToggleMute}
            role="button"
            tabIndex={0}
            onKeyDown={(e) => { if (e.key === " " || e.key === "Enter") { e.preventDefault(); onToggleMute(); } }}
            style={{ fontSize: "12px", cursor: "pointer" }}
          >
            {muted ? "🔇" : volume < 0.5 ? "🔉" : "🔊"}
          </div>
          <input
            type="range"
            min={0}
            max={100}
            value={muted ? 0 : Math.round(volume * 100)}
            onChange={(e) => onVolumeChange(Number(e.target.value) / 100)}
            style={{
              width: "80px",
              height: "4px",
              cursor: "pointer",
              accentColor: "var(--accent, #ffbd2e)",
            }}
            title={`${t("titlebar.volume", locale)}: ${Math.round(volume * 100)}%`}
          />
        </div>
      )}

      <div className="title" data-tauri-drag-region>
        {titleText}
      </div>

      <div className="conn" title={connected ? t("titlebar.pipeConnected", locale) : t("titlebar.pipeNotConnected", locale)}>
        <span
          className="pip"
          style={{ background: pipColor, boxShadow: pipGlow }}
        />
        <span>{connText}</span>
      </div>

      {/* Right: Windows-style window controls */}
      <div className="win-controls">
        <button
          className="win-btn"
          title={t("titlebar.minimize", locale)}
          onClick={() => safeInvoke("window_minimize")}
          aria-label={t("titlebar.minimize", locale)}
        >
          <svg width="10" height="10" viewBox="0 0 10 10">
            <rect x="0" y="4.5" width="10" height="1" fill="currentColor" />
          </svg>
        </button>
        <button
          className="win-btn"
          title={t("titlebar.maximize", locale)}
          onClick={() => safeInvoke("window_toggle_maximize")}
          aria-label={t("titlebar.maximize", locale)}
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
          title={t("titlebar.close", locale)}
          onClick={() => safeInvoke("window_close")}
          aria-label={t("titlebar.close", locale)}
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
