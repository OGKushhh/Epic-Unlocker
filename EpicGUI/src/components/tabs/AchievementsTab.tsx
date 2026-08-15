/**
 * Achievements Tab — carbon copy of the mockup's ach tab.
 *
 * Features:
 *   - Search input (filters by title/desc, case-insensitive)
 *   - 4 filter pills: All / Locked / Unlocked / Hidden
 *   - Sort dropdown: Alphabetical / Rarity / Progress / XP
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
type SortMode = "default" | "alpha" | "rarity" | "progressHigh" | "progressLow" | "xpHigh" | "xpLow";

const FILTER_KEYS: { id: FilterId; key: string }[] = [
  { id: "all", key: "ach.filterAll" },
  { id: "locked", key: "ach.filterLocked" },
  { id: "unlocked", key: "ach.filterUnlocked" },
  { id: "hidden", key: "ach.filterHidden" },
];

const SORT_OPTIONS: { id: SortMode; key: string }[] = [
  { id: "alpha", key: "ach.sortAlpha" },
  { id: "rarity", key: "ach.sortRarity" },
  { id: "progressHigh", key: "ach.sortProgressHigh" },
  { id: "progressLow", key: "ach.sortProgressLow" },
  { id: "xpHigh", key: "ach.sortXpHigh" },
  { id: "xpLow", key: "ach.sortXpLow" },
];

// -- Rarity tier ordering for sort (higher tier = higher number) ----------
const RARITY_ORDER: Record<RarityTier, number> = {
  platinum: 4,
  gold: 3,
  silver: 2,
  bronze: 1,
  unknown: 0,
};

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


// -- Sort comparator -----------------------------------------------------------

function sortAchievements(list: Achievement[], mode: SortMode): Achievement[] {
  const sorted = [...list];
  switch (mode) {
    case "alpha":
      sorted.sort((a, b) => a.title.localeCompare(b.title));
      break;
    case "rarity":
      sorted.sort((a, b) => {
        const aR = RARITY_ORDER[a.rarityTier ?? "unknown"];
        const bR = RARITY_ORDER[b.rarityTier ?? "unknown"];
        if (aR !== bR) return bR - aR; // higher rarity first
        // tiebreak: rarer % first (lower % = rarer)
        return (a.rarityPercent ?? 100) - (b.rarityPercent ?? 100);
      });
      break;
    case "progressHigh":
      sorted.sort((a, b) => b.progress - a.progress);
      break;
    case "progressLow":
      sorted.sort((a, b) => a.progress - b.progress);
      break;
    case "xpHigh":
      sorted.sort((a, b) => {
        // XP is inversely related to rarityPercent (lower % = higher XP)
        const aXp = a.rarityPercent != null ? 100 - a.rarityPercent : -1;
        const bXp = b.rarityPercent != null ? 100 - b.rarityPercent : -1;
        return bXp - aXp;
      });
      break;
    case "xpLow":
      sorted.sort((a, b) => {
        const aXp = a.rarityPercent != null ? 100 - a.rarityPercent : -1;
        const bXp = b.rarityPercent != null ? 100 - b.rarityPercent : -1;
        return aXp - bXp;
      });
      break;
    default:
      break; // "default" = original order
  }
  return sorted;
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
  const [sortMode, setSortMode] = useState<SortMode>("default");
  const [sortOpen, setSortOpen] = useState(false);
  const sortRef = useRef<HTMLDivElement>(null);

  // Close sort dropdown on outside click
  useEffect(() => {
    if (!sortOpen) return;
    const handler = (e: MouseEvent) => {
      if (sortRef.current && !sortRef.current.contains(e.target as Node)) {
        setSortOpen(false);
      }
    };
    document.addEventListener("mousedown", handler);
    return () => document.removeEventListener("mousedown", handler);
  }, [sortOpen]);

  const filtered = useMemo(() => {
    const q = search.trim().toLowerCase();
    const result = achievements.filter((a) => {
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
    return sortAchievements(result, sortMode);
  }, [achievements, search, filter, sortMode]);

  return (
    <div className={`tab-content${active ? " active" : ""}`} id="tab-ach">
      <div className="ach-toolbar">
        <input
          className="search"
          placeholder={t("ach.searchPlaceholder", locale)}
          value={search}
          onChange={(e) => setSearch(e.target.value)}
          style={{ flex: "1 1 0px", minWidth: "100px" }}
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

        {/* Sort dropdown */}
        <div
          ref={sortRef}
          style={{ position: "relative", marginLeft: "auto" }}
        >
          <button
            className="filter-btn"
            onClick={() => setSortOpen((v) => !v)}
            title="Sort achievements"
            style={{ display: "flex", alignItems: "center", gap: "4px" }}
          >
            <span style={{ fontSize: "13px" }}>⇅</span>
            {sortMode === "default"
              ? t("ach.sortDefault", locale)
              : t(SORT_OPTIONS.find((o) => o.id === sortMode)?.key ?? "ach.sortDefault", locale)}
          </button>
          {sortOpen && (
            <div
              style={{
                position: "absolute",
                top: "100%",
                right: 0,
                zIndex: 100,
                marginTop: "4px",
                background: "var(--card, #1e1e1e)",
                border: "1px solid var(--border, #333)",
                borderRadius: "6px",
                boxShadow: "0 4px 12px rgba(0,0,0,0.4)",
                minWidth: "180px",
                overflow: "hidden",
              }}
            >
              {/* Default option */}
              <div
                onClick={() => { setSortMode("default"); setSortOpen(false); }}
                style={{
                  padding: "8px 12px",
                  cursor: "pointer",
                  fontSize: "13px",
                  color: sortMode === "default" ? "var(--accent, #f67014)" : "var(--text)",
                  background: sortMode === "default" ? "var(--accent-dim, rgba(246,112,20,0.1))" : "transparent",
                  fontWeight: sortMode === "default" ? 600 : 400,
                }}
                onMouseEnter={(e) => { if (sortMode !== "default") e.currentTarget.style.background = "var(--hover, rgba(255,255,255,0.05))"; }}
                onMouseLeave={(e) => { if (sortMode !== "default") e.currentTarget.style.background = "transparent"; }}
              >
                {t("ach.sortDefault", locale)}
              </div>
              {SORT_OPTIONS.map((opt) => (
                <div
                  key={opt.id}
                  onClick={() => { setSortMode(opt.id); setSortOpen(false); }}
                  style={{
                    padding: "8px 12px",
                    cursor: "pointer",
                    fontSize: "13px",
                    color: sortMode === opt.id ? "var(--accent, #f67014)" : "var(--text)",
                    background: sortMode === opt.id ? "var(--accent-dim, rgba(246,112,20,0.1))" : "transparent",
                    fontWeight: sortMode === opt.id ? 600 : 400,
                  }}
                  onMouseEnter={(e) => { if (sortMode !== opt.id) e.currentTarget.style.background = "var(--hover, rgba(255,255,255,0.05))"; }}
                  onMouseLeave={(e) => { if (sortMode !== opt.id) e.currentTarget.style.background = "transparent"; }}
                >
                  {t(opt.key, locale)}
                </div>
              ))}
            </div>
          )}
        </div>
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

  // Build the rarity sub-line under description
  // Format: "Unlocked Mar 19, 2023 | 50 XP Silver | 4% of players unlock"
  // or:     "50 XP Silver | 4% of players unlock"  (if not unlocked / no timestamp)
  const hasRarity = ach.rarityTier && ach.rarityTier !== "unknown";
  // Format the unlock date as "Mar 19, 2023" (locale-aware)
  const formatUnlockDate = (iso: string | undefined): string | null => {
    if (!iso) return null;
    try {
      const d = new Date(iso);
      if (isNaN(d.getTime())) return null;
      return d.toLocaleDateString(locale === "ar" ? "ar-SA" : "en-US", {
        year: "numeric",
        month: "short",
        day: "numeric",
      });
    } catch {
      return null;
    }
  };

  const unlockDateStr = formatUnlockDate(ach.unlockTime);

  // Build sub-line segments
  const subSegments: { text: string; color?: string }[] = [];

  // Unlocked timestamp
  if (ach.unlocked && unlockDateStr) {
    subSegments.push({ text: `${t("ach.unlockedOn", locale)} ${unlockDateStr}` });
  }

  // XP + Tier
  if (hasRarity) {
    const tier = ach.rarityTier!;
    const tierName = rarityLabel(tier);
    const xpVal = ach.rarityXp != null ? ach.rarityXp : null;
    const xpTierStr = xpVal != null
      ? `${xpVal} ${t("ach.xpLabel", locale)} ${tierName}`
      : tierName;
    subSegments.push({ text: xpTierStr, color: RARITY_COLORS[tier] });
  }

  // % of players
  if (ach.rarityPercent != null) {
    subSegments.push({ text: `${ach.rarityPercent.toFixed(1)}% ${t("ach.ofPlayersUnlock", locale)}` });
  }

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
            width={32}
            height={32}
            style={{
              width: 32,
              height: 32,
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
        {/* Rarity sub-line: "Unlocked Mar 19, 2023 | 50 XP Silver | 4% of players unlock" */}
        {subSegments.length > 0 && (
          <div
            className="rarity-subline"
            style={{
              fontSize: "11px",
              color: "var(--text-dim)",
              marginTop: "2px",
              lineHeight: "1.4",
              display: "flex",
              flexWrap: "wrap",
              alignItems: "center",
              gap: "0px",
            }}
          >
            {subSegments.map((seg, i) => (
              <span key={i} style={{ display: "inline-flex", alignItems: "center" }}>
                {i > 0 && <span style={{ margin: "0 6px", opacity: 0.5 }}>|</span>}
                <span style={seg.color ? { color: seg.color, fontWeight: 600 } : undefined}>
                  {seg.text}
                </span>
              </span>
            ))}
          </div>
        )}
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

        {ach.unlocked ? (
          <span className="badge unlocked">{t("ach.badgeUnlocked", locale)}</span>
        ) : (
          <>
            {ach.statThreshold && (
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
            )}
            <span className="badge locked">{t("ach.badgeLocked", locale)}</span>
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
