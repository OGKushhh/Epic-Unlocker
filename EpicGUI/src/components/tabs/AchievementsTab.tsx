/**
 * Achievements Tab — carbon copy of the mockup's ach tab.
 *
 * Features:
 *   - Search input (filters by title/desc, case-insensitive)
 *   - 4 filter pills: All / Locked / Unlocked / Hidden
 *   - Scrollable list of 52px rows
 *   - Each row: emoji icon + title + desc + progress bar + badges + Unlock btn
 *   - Row hover → achievement tooltip (title + desc)
 *   - Stat-gated badge hover → explanation tooltip (how stat-gated works + 5–30s wait)
 */

import { useEffect, useMemo, useState } from "react";
import { convertFileSrc } from "@tauri-apps/api/core";
import type { Achievement } from "../../data/mockupData";

// Detect Tauri runtime so we can fall back to emoji in browser dev mode
// (convertFileSrc throws if called outside Tauri).
declare global {
  interface Window {
    __TAURI_INTERNALS__?: unknown;
    __TAURI__?: unknown;
  }
}
const IS_TAURI =
  typeof window !== "undefined" &&
  (window.__TAURI_INTERNALS__ !== undefined || window.__TAURI__ !== undefined);

type FilterId = "all" | "locked" | "unlocked" | "hidden";

const FILTERS: { id: FilterId; label: string }[] = [
  { id: "all", label: "All" },
  { id: "locked", label: "Locked" },
  { id: "unlocked", label: "Unlocked" },
  { id: "hidden", label: "Hidden" },
];

interface AchievementsTabProps {
  active: boolean;
  achievements: Achievement[];
  emojis: string[];
  loading?: boolean;
  connected?: boolean;
  onHoverRow: (ach: Achievement | null, e?: { clientX: number; clientY: number }) => void;
  onHoverStatGated: (e: { clientX: number; clientY: number }) => void;
  onUnlockRow: (ach: Achievement) => void;
}

export default function AchievementsTab({
  active,
  achievements,
  emojis,
  loading = false,
  connected = true,
  onHoverRow,
  onHoverStatGated,
  onUnlockRow,
}: AchievementsTabProps) {
  const [search, setSearch] = useState("");
  const [filter, setFilter] = useState<FilterId>("all");

  const filtered = useMemo(() => {
    const q = search.trim().toLowerCase();
    return achievements.filter((a) => {
      // Filter pill
      if (filter === "locked" && a.unlocked) return false;
      if (filter === "unlocked" && !a.unlocked) return false;
      if (filter === "hidden" && !a.hidden) return false;
      // Search
      if (!q) return true;
      return (
        a.title.toLowerCase().includes(q) ||
        a.desc.toLowerCase().includes(q) ||
        a.id.toLowerCase().includes(q)
      );
    });
  }, [achievements, search, filter]);

  return (
    <div className={`tab-content${active ? " active" : ""}`} id="tab-ach">
      <div className="ach-toolbar">
        <input
          className="search"
          placeholder="Search achievements…"
          value={search}
          onChange={(e) => setSearch(e.target.value)}
        />
        {FILTERS.map((f) => (
          <button
            key={f.id}
            className={`filter-btn${filter === f.id ? " active" : ""}`}
            onClick={() => setFilter(f.id)}
          >
            {f.label}
          </button>
        ))}
      </div>

      <div className="ach-list" id="achList">
        {loading && (
          <EmptyState
            icon="⏳"
            title="Connecting…"
            body="Waiting for the Epic Unlocker pipe to deliver the achievement list."
          />
        )}

        {!loading && achievements.length === 0 && !connected && (
          <EmptyState
            icon="🔌"
            title="Not connected"
            body="Launch a game with Epic Unlocker injected to establish the pipe connection. The list will populate automatically once the game's EOS SDK is hooked."
          />
        )}

        {!loading && achievements.length === 0 && connected && (
          <EmptyState
            icon="📭"
            title="No achievements"
            body="The pipe is connected but no achievement definitions have been received yet. Try Refresh."
          />
        )}

        {!loading && achievements.length > 0 && filtered.length === 0 && (
          <EmptyState
            icon="🔍"
            title="No matches"
            body="No achievements match your current search and filter combination."
          />
        )}

        {!loading &&
          filtered.map((ach, idx) => (
            <AchievementRow
              key={ach.id}
              ach={ach}
              emoji={emojis[idx % emojis.length] || "🏅"}
              iconSrc={
                ach.iconPath && IS_TAURI
                  ? convertFileSrc(ach.iconPath)
                  : null
              }
              onHover={onHoverRow}
              onHoverStatGated={onHoverStatGated}
              onUnlock={onUnlockRow}
            />
          ))}
      </div>
    </div>
  );
}

interface AchievementRowProps {
  ach: Achievement;
  emoji: string;
  /** Tauri webview URL for the locally-cached icon, or null to fall back to emoji. */
  iconSrc?: string | null;
  onHover: (ach: Achievement | null, e?: { clientX: number; clientY: number }) => void;
  onHoverStatGated: (e: { clientX: number; clientY: number }) => void;
  onUnlock: (ach: Achievement) => void;
}

function AchievementRow({ ach, emoji, iconSrc, onHover, onHoverStatGated, onUnlock }: AchievementRowProps) {
  // Track whether the cached <img> failed to load (corrupt download, file
  // deleted after cache, etc.). When that happens, we fall back to the emoji
  // so the row never shows an empty icon box.
  const [imgFailed, setImgFailed] = useState(false);
  // If the iconSrc changes (e.g. user re-fetched icons), reset the failure flag
  // so we try the new image instead of sticking on emoji forever.
  useEffect(() => {
    setImgFailed(false);
  }, [iconSrc]);

  const showImg = iconSrc && !imgFailed;

  return (
    <div
      className={`ach-row ${ach.unlocked ? "unlocked" : "locked"}`}
      onMouseEnter={(e) => onHover(ach, e)}
      onMouseLeave={() => onHover(null)}
    >
      <div className="ach-icon">
        {showImg ? (
          <img
            src={iconSrc!}
            alt=""
            width={28}
            height={28}
            style={{
              width: 28,
              height: 28,
              objectFit: "contain",
              borderRadius: 4,
              imageRendering: "auto",
              // Desaturate locked achievements slightly so the unlocked ones pop.
              filter: ach.unlocked ? "none" : "grayscale(0.4) opacity(0.85)",
            }}
            // If the image fails to load (e.g. corrupt download, file deleted
            // from cache after fetch), fall back to the emoji instead of
            // leaving an empty box.
            onError={() => setImgFailed(true)}
          />
        ) : (
          emoji
        )}
      </div>

      <div className="ach-body">
        <div className="ach-title">{ach.title}</div>
        <div className="ach-desc">{ach.desc}</div>
      </div>

      <div className="ach-meta">
        {!ach.unlocked && ach.progress < 1 && (
          <>
            <div className="stat-progress">
              <div
                className="stat-bar"
                style={{ width: `${ach.progress * 100}%` }}
              />
            </div>
            <span className="stat-text">
              {ach.statThreshold
                ? `${ach.statThreshold} · ${Math.round(ach.progress * 100)}%`
                : `${Math.round(ach.progress * 100)}%`}
            </span>
          </>
        )}

        {ach.hidden && <span className="badge hidden">Hidden</span>}

        {ach.unlocked ? (
          <span className="badge unlocked">Unlocked</span>
        ) : (
          <>
            <span
              className="badge stat"
              title="What does Stat-gated mean? Hover for details."
              onMouseEnter={(e) => {
                e.stopPropagation();
                onHoverStatGated(e);
              }}
              onMouseLeave={(e) => onHover(ach, e)}
            >
              Stat-gated
            </span>
            <span className="badge locked">Locked</span>
          </>
        )}

        <button
          className="unlock-btn"
          disabled={ach.unlocked}
          onClick={(e) => {
            e.stopPropagation();
            onUnlock(ach);
          }}
        >
          {ach.unlocked ? "Done" : "Unlock"}
        </button>
      </div>
    </div>
  );
}

// ── Empty state (used for loading / disconnected / no-data / no-matches) ─────

interface EmptyStateProps {
  icon: string;
  title: string;
  body: string;
}

function EmptyState({ icon, title, body }: EmptyStateProps) {
  return (
    <div
      style={{
        padding: "48px 32px",
        textAlign: "center",
        color: "var(--text-dim)",
      }}
    >
      <div style={{ fontSize: "48px", marginBottom: "12px" }}>{icon}</div>
      <div
        style={{
          fontSize: "16px",
          fontWeight: 600,
          color: "var(--text)",
          marginBottom: "8px",
        }}
      >
        {title}
      </div>
      <div
        style={{
          fontSize: "13px",
          maxWidth: "420px",
          margin: "0 auto",
          lineHeight: "1.6",
        }}
      >
        {body}
      </div>
    </div>
  );
}
