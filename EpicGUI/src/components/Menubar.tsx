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
    <div className="menubar">
      {TAB_IDS.map((tab) => (
        <div
          key={tab.id}
          className={`item${active === tab.id ? " active" : ""}`}
          onClick={() => onChange(tab.id)}
        >
          {tab.emoji} {t(tab.key, locale)}
        </div>
      ))}
    </div>
  );
}
