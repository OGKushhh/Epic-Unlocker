/**
 * Sidebar — left sidebar, visible ONLY on the Achievements tab.
 *
 * Stats are derived from the live achievements array (passed in via props)
 * so they stay in sync when achievements are unlocked.
 *
 * Contains:
 *   - Game card (icon + game name from backend + EOS SDK meta)
 *   - 2×2 stat grid (Unlocked / Locked / Stat-gated / Progress)
 *   - Action buttons (Unlock All primary, Refresh, Fetch Icons)
 *   - Toast stack at the bottom (max 4)
 */

import { useState } from "react";
import type { Toast } from "../hooks/useToasts";
import type { Achievement } from "../data/mockupData";
import { t, type Locale } from "../i18n";

// Pool of game-themed emojis. One is picked at random on every app startup
// (well, on every Sidebar mount — which is once per app session) just for fun.
const GAME_EMOJIS = [
  "🎮", "🕹️", "👾", "🎲", "🎯", "🏆", "⚔️", "🛡️",
  "🗡️", "🏹", "🔮", "💎", "🌟", "⭐", "💰", "🎁",
  "🎪", "🎰", "🚀", "⚡", "🔥", "💥", "🌈", "🌠",
  "🥇", "🥈", "🥉", "🏁", "🚦", "🛣️", "🏎️", "💨",
];

interface SidebarProps {
  visible: boolean;
  achievements: Achievement[];
  toasts: Toast[];
  connected?: boolean;
  loading?: boolean;
  /** Display name of the connected game (from Rust backend). */
  gameName?: string;
  locale?: Locale;
  onUnlockAllClick: () => void;
  onRefreshClick: () => void;
  onFetchIconsClick: () => void;
  onFetchRarityClick: () => void;
  onClearCacheClick: () => void;
}

export default function Sidebar({
  visible,
  achievements,
  toasts,
  connected = false,
  loading = false,
  gameName,
  locale = "en",
  onUnlockAllClick,
  onRefreshClick,
  onFetchIconsClick,
  onFetchRarityClick,
  onClearCacheClick,
}: SidebarProps) {
  // Pick a random game emoji ONCE per app session (lazy initial state).
  // Re-renders reuse the same value — only a full app restart picks a new one.
  const [gameEmoji] = useState(
    () => GAME_EMOJIS[Math.floor(Math.random() * GAME_EMOJIS.length)]
  );

  if (!visible) return null;

  const total = achievements.length;
  const unlocked = achievements.filter((a) => a.unlocked).length;
  const locked = total - unlocked;
  const statGated = total;
  const progress = total > 0 ? Math.round((unlocked / total) * 1000) / 10 : 0;

  // Game card meta reflects connection state
  const gameMeta = loading
    ? t("sidebar.connecting", locale)
    : connected
    ? "EOS SDK 1.16.0"
    : t("sidebar.notConnected", locale);

  // Game name comes from the Rust backend (ConnectionStatus.gameName).
  // When disconnected, show "No Game" instead of a fake title.
  const displayName = loading
    ? t("sidebar.connecting", locale)
    : connected
    ? (gameName ?? "Epic Game")
    : t("sidebar.noGame", locale);

  return (
    <div className="sidebar" id="sidebar">
      <div className="game-card">
        <div className="game-icon">{gameEmoji}</div>
        <div className="game-name">{displayName}</div>
        <div className="game-meta">{gameMeta}</div>
      </div>

      <div className="stat-grid">
        <div className="stat">
          <div className="num">
            {unlocked}
            <span style={{ fontSize: "11px", color: "var(--text-dim)" }}>
              /{total}
            </span>
          </div>
          <div className="lbl">{t("sidebar.unlocked", locale)}</div>
        </div>
        <div className="stat">
          <div className="num">{locked}</div>
          <div className="lbl">{t("sidebar.locked", locale)}</div>
        </div>
        <div className="stat">
          <div className="num">{statGated}</div>
          <div className="lbl">{t("sidebar.statGated", locale)}</div>
        </div>
        <div className="stat">
          <div className="num">{progress}%</div>
          <div className="lbl">{t("sidebar.progress", locale)}</div>
        </div>
      </div>

      <div className="actions">
        <button
          className="action-btn primary"
          onClick={onUnlockAllClick}
          title="Ctrl + Shift + U"
        >
          {t("sidebar.unlockAll", locale)}
        </button>
        <button className="action-btn" onClick={onRefreshClick}>
          {t("sidebar.refresh", locale)}
        </button>
        <button className="action-btn" onClick={onFetchIconsClick}>
          {t("sidebar.fetchIcons", locale)}
        </button>
        <button className="action-btn" onClick={onFetchRarityClick}>
          {t("sidebar.fetchRarity", locale)}
        </button>
        <button className="action-btn" onClick={onClearCacheClick}>
          {t("sidebar.clearCache", locale)}
        </button>
      </div>

      <div className="toast-container" id="toastContainer">
        {toasts.map((t) => (
          <div className="toast-item" key={t.id}>
            <div className="tt-title">{t.title}</div>
            <div className="tt-body">{t.body}</div>
          </div>
        ))}
      </div>
    </div>
  );
}
