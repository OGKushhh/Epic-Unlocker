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
  onUnlockAllClick: () => void;
  onRefreshClick: () => void;
  onFetchIconsClick: () => void;
  onFetchRarityClick: () => void;
}

export default function Sidebar({
  visible,
  achievements,
  toasts,
  connected = false,
  loading = false,
  gameName,
  onUnlockAllClick,
  onRefreshClick,
  onFetchIconsClick,
  onFetchRarityClick,
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
    ? "Connecting…"
    : connected
    ? "EOS SDK 1.16.0"
    : "Not connected";

  // Game name comes from the Rust backend (ConnectionStatus.gameName).
  // When disconnected, show "No Game" instead of a fake title.
  const displayName = loading
    ? "Connecting…"
    : connected
    ? (gameName ?? "Epic Game")
    : "No Game";

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
          <div className="lbl">Unlocked</div>
        </div>
        <div className="stat">
          <div className="num">{locked}</div>
          <div className="lbl">Locked</div>
        </div>
        <div className="stat">
          <div className="num">{statGated}</div>
          <div className="lbl">Stat-gated</div>
        </div>
        <div className="stat">
          <div className="num">{progress}%</div>
          <div className="lbl">Progress</div>
        </div>
      </div>

      <div className="actions">
        <button
          className="action-btn primary"
          onClick={onUnlockAllClick}
          title="Ctrl + Shift + U"
        >
          ⚡ Unlock All (Ctrl+Shift+U)
        </button>
        <button className="action-btn" onClick={onRefreshClick}>
          🔄 Refresh
        </button>
        <button className="action-btn" onClick={onFetchIconsClick}>
          🖼️ Fetch Icons
        </button>
        <button className="action-btn" onClick={onFetchRarityClick}>
          🏆 Fetch Rarity
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
