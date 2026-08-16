/**
 * Menubar — carbon copy of the mockup's tab strip.
 * 4 tabs with the exact emoji + label text. The active tab gets a 3px accent
 * bottom border + slight scale (handled by CSS .menubar .item.active).
 */

import { t, type Locale } from "../i18n";

export type TabId = "ach" | "dlc" | "log" | "settings";

interface MenubarProps {
  active: TabId;
  onChange: (tab: TabId) => void;
  locale?: Locale;
}

const TAB_IDS: { id: TabId; emoji: string; key: string }[] = [
  { id: "ach", emoji: "🏆", key: "menu.achievements" },
  { id: "dlc", emoji: "🎮", key: "menu.dlc" },
  { id: "log", emoji: "📜", key: "menu.log" },
  { id: "settings", emoji: "⚙", key: "menu.settings" },
];

export default function Menubar({ active, onChange, locale = "en" }: MenubarProps) {
  return (
    <div className="menubar" role="tablist">
      {TAB_IDS.map((tab) => (
        <div
          key={tab.id}
          className={active === tab.id ? "item active" : "item"}
          onClick={() => onChange(tab.id)}
          onKeyDown={(e) => {
            if (e.key === " " || e.key === "Enter") { e.preventDefault(); onChange(tab.id); }
            const idx = TAB_IDS.findIndex(t => t.id === tab.id);
            if (e.key === "ArrowRight" || e.key === "ArrowLeft") {
              e.preventDefault();
              const dir = e.key === "ArrowRight" ? 1 : -1;
              const next = TAB_IDS[(idx + dir + TAB_IDS.length) % TAB_IDS.length];
              onChange(next.id);
            }
          }}
          role="tab"
          aria-selected={active === tab.id}
          tabIndex={active === tab.id ? 0 : -1}
        >
          {tab.emoji} {t(tab.key, locale)}
        </div>
      ))}
    </div>
  );
}
