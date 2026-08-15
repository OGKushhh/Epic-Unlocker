/**
 * Achievements Tab — carbon copy of the mockup's ach tab.
 *
 * Features:
 *   - Search input (filters by title/desc, case-insensitive)
 *   - 4 filter pills: All / Locked / Unlocked / Hidden
 *   - Scrollable list of 52px rows
 *   - Each row: emoji icon + title + desc + progress bar + badges + Unlock btn
 *   - Row hover -> achievement tooltip (title + desc) ONLY when description is truncated/overflowed
 *   - Stat-gated badge hover -> explanation tooltip (how stat-gated works + 5-30s wait)
 *   - G4: Rarity badge (Bronze/Silver/Gold/Platinum) + unlock timestamp
 */

import { useEffect, useMemo, useRef, useState } from "react";
import { convertFileSrc } from "@tauri-apps/api/core";
import type { Achievement, RarityTier } from "../../data/mockupData";
import { t, type Locale } from "../../i18n";

// Detect Tauri runtime so we can fall back to emoji in browser dev mode
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

const FILTER_KEYS: { id: FilterId; key: string }[] = [
  { id: "all", key: "ach.filterAll" },
  { id: "locked", key: "ach.filterLocked" },
  { id: "unlocked", key: "ach.filterUnlocked" },
  { id: "hidden", key: "ach.filterHidden" },
];

// -- Rarity tier display -------------------------------------------------------

const RARITY_COLORS: Record<RarityTier, string> = {
  bronze: "#CD7F32",
  silver: "#C0C0C0",
  gold: "#FFD700",
  platinum: "#E5E4E2",
  unknown: "#808080",
};

const RARITY_KEY_MAP: Record<RarityTier, string> = {
  bronze: "ach.rarityBronze",
  silver: "ach.raritySilver",
  gold: "ach.rarityGold",
  platinum: "ach.rarityPlatinum",
  unknown: "ach.rarityBronze", // fallback
};

function formatUnlockTime(iso: string | undefined, locale: Locale): string | null {
  if (!iso) return null;
  try {
    const d = new Date(iso);
    const now = new Date();
    const diffMs = now.getTime() - d.getTime();
    const diffDays = Math.floor(diffMs / (1000 * 60 * 60 * 24));
    if (diffDays < 0) return null; // future = glitch
    if (diffDays === 0) return t("ach.timeToday", locale);
    if (diffDays === 1) return t("ach.timeYesterday", locale);
    if (diffDays < 30) return `${diffDays}${t("ach.timeDaysAgo", locale)}`;
    if (diffDays < 365) return `${Math.floor(diffDays / 30)}${t("ach.timeMonthsAgo", locale)}`;
    return `${Math.floor(diffDays / 365)}${t("ach.timeYearsAgo", locale)}`;
  } catch {
    return null;
  }
}

interface AchievementsTabProps {
  active: boolean;
  achievements: Achievement[];
  emojis: string[];
  loading?: boolean;
  connected?: boolean;
  locale?: Locale;
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
  locale = "en",
  onHoverRow,
  onHoverStatGated,
  onUnlockRow,
}: AchievementsTabProps) {
  const [search, setSearch] = useState("");
  const [filter, setFilter] = useState<FilterId>("all");

  const filtered = useMemo(() => {
    const q = search.trim().toLowerCase();
    return achievements.filter((a) => {
      if (filter === "locked" && a.unlocked) return false;
      if (filter === "unlocked" && !a.unlocked) return false;
      if (filter === "hidden" && !a.hidden) return false;
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
          placeholder={t("ach.searchPlaceholder", locale)}
          value={search}
          onChange={(e) => setSearch(e.target.value)}
        />
        {FILTER_KEYS.map((f) => (
          <button
            key={f.id}
            className={`filter-btn${filter === f.id ? " active" : ""}`}
            onClick={() => setFilter(f.id)}
          >
            {t(f.key, locale)}
          </button>
        ))}
      </div>

      <div className="ach-list" id="achList">
        {loading && (
          <EmptyState
            icon="⏳"
            title={t("ach.emptyConnecting", locale)}
            body={t("ach.emptyConnectingBody", locale)}
          />
        )}

        {!loading && achievements.length === 0 && !connected && (
          <EmptyState
            icon="🔌"
            title={t("ach.emptyNotConnected", locale)}
            body={t("ach.emptyNotConnectedBody", locale)}
          />
        )}

        {!loading && achievements.length === 0 && connected && (
          <EmptyState
            icon="📭"
            title={t("ach.emptyNoAch", locale)}
            body={t("ach.emptyNoAchBody", locale)}
          />
        )}

        {!loading && achievements.length > 0 && filtered.length === 0 && (
          <EmptyState
            icon="🔍"
            title={t("ach.emptyNoMatches", locale)}
            body={t("ach.emptyNoMatchesBody", locale)}
          />
        )}

        {!loading &&
          filtered.map((ach, idx) => (
            <AchievementRow
              key={ach.id}
              ach={ach}
              emoji={emojis[idx % emojis.length] || "🏅"}
              locale={locale}
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
  iconSrc?: string | null;
  locale: Locale;
  onHover: (ach: Achievement | null, e?: { clientX: number; clientY: number }) => void;
  onHoverStatGated: (e: { clientX: number; clientY: number }) => void;
  onUnlock: (ach: Achievement) => void;
}

function AchievementRow({ ach, emoji, iconSrc, locale, onHover, onHoverStatGated, onUnlock }: AchievementRowProps) {
  const [imgFailed, setImgFailed] = useState(false);
  const [flickerKey, setFlickerKey] = useState(0);
  const descRef = useRef<HTMLDivElement>(null);
  const [descTruncated, setDescTruncated] = useState(false);
  useEffect(() => {
    setImgFailed(false);
  }, [iconSrc]);

  // Check if the description text overflows (is truncated by CSS ellipsis)
  useEffect(() => {
    const el = descRef.current;
    if (el) {
      setDescTruncated(el.scrollWidth > el.clientWidth);
    }
  }, [ach.desc]);

  const showImg = iconSrc && !imgFailed;
  const unlockAgo = formatUnlockTime(ach.unlockTime, locale);

  // Only trigger the row tooltip when the description is truncated (too long for the row)
  const handleRowMouseEnter = (e: React.MouseEvent) => {
    if (descTruncated) {
      onHover(ach, e);
    }
  };

  const rarityLabel = (tier: RarityTier): string => {
    if (tier === "unknown") return "?";
    return t(RARITY_KEY_MAP[tier], locale);
  };

  return (
    <div
      className={`ach-row ${ach.unlocked ? "unlocked" : "locked"}${flickerKey > 0 ? " copy-flicker" : ""}`}
      onMouseEnter={handleRowMouseEnter}
      onMouseLeave={() => onHover(null)}
      onContextMenu={(e) => {
        e.preventDefault();
        navigator.clipboard.writeText(ach.id).catch(() => {});
        // Bump key to re-trigger the CSS animation
        setFlickerKey((k) => k + 1);
      }}
      title={`Right-click to copy ID: ${ach.id}`}
      // onAnimationEnd cleans up the class so it can re-trigger next time
      onAnimationEnd={() => setFlickerKey(0)}
    >
      <div
        className="ach-icon"
        title=""
        onMouseEnter={(e) => {
          // Suppress the row tooltip while hovering the icon
          e.stopPropagation();
          if (descTruncated) onHover(null);
        }}
      >
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
              filter: ach.unlocked ? "none" : "grayscale(0.4) opacity(0.85)",
            }}
            onError={() => setImgFailed(true)}
          />
        ) : (
          emoji
        )}
      </div>
      {/* Preview popover — sibling of .ach-icon so parent opacity doesn't dim it */}
      <div className="icon-preview">
        {showImg ? (
          <img
            src={iconSrc!}
            alt=""
          />
        ) : (
          <span className="preview-emoji">{emoji}</span>
        )}
      </div>

      <div className="ach-body">
        <div className="ach-title">{ach.title}</div>
        <div className="ach-desc" ref={descRef}>{ach.desc}</div>
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

        {ach.hidden && <span className="badge hidden">{t("ach.badgeHidden", locale)}</span>}

        {/* G4: Rarity badge */}
        {ach.rarityTier && ach.rarityTier !== "unknown" && (
          <span
            className="badge rarity"
            style={{
              color: RARITY_COLORS[ach.rarityTier],
              borderColor: RARITY_COLORS[ach.rarityTier],
            }}
            title={ach.rarityPercent != null
              ? `${rarityLabel(ach.rarityTier)} — ${ach.rarityPercent.toFixed(1)}% of players`
              : rarityLabel(ach.rarityTier)}
          >
            {rarityLabel(ach.rarityTier)}
            {ach.rarityPercent != null && (
              <span className="rarity-pct">{ach.rarityPercent.toFixed(1)}%</span>
            )}
          </span>
        )}

        {ach.unlocked ? (
          <span className="badge unlocked">{t("ach.badgeUnlocked", locale)}</span>
        ) : (
          <>
            <span
              className="badge stat"
              title={t("tooltip.statGatedTitle", locale)}
              onMouseEnter={(e) => {
                e.stopPropagation();
                onHoverStatGated(e);
              }}
              onMouseLeave={() => {
                // Stat-gated tooltip is standalone — always close on leave
                onHover(null);
              }}
            >
              {t("ach.badgeStatGated", locale)}
            </span>
            <span className="badge locked">{t("ach.badgeLocked", locale)}</span>
          </>
        )}

        {/* G4: Unlock timestamp */}
        {ach.unlocked && unlockAgo && (
          <span className="badge unlock-time" title={ach.unlockTime}>
            {unlockAgo}
          </span>
        )}

        <button
          className="unlock-btn"
          disabled={ach.unlocked}
          onClick={(e) => {
            e.stopPropagation();
            onUnlock(ach);
          }}
        >
          {ach.unlocked ? t("ach.btnDone", locale) : t("ach.btnUnlock", locale)}
        </button>
      </div>
    </div>
  );
}

// -- Empty state ---------------------------------------------------------------

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
