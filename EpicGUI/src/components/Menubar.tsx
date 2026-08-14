/**
 * Menubar — carbon copy of the mockup's tab strip.
 * 4 tabs with the exact emoji + label text. The active tab gets a 3px accent
 * bottom border + slight scale (handled by CSS .menubar .item.active).
 */

export type TabId = "ach" | "dlc" | "log" | "settings";

const TABS: { id: TabId; label: string }[] = [
  { id: "ach", label: "🏆 Achievements" },
  { id: "dlc", label: "🎮 DLC" },
  { id: "log", label: "📜 Log" },
  { id: "settings", label: "⚙ Settings" },
];

interface MenubarProps {
  active: TabId;
  onChange: (tab: TabId) => void;
}

export default function Menubar({ active, onChange }: MenubarProps) {
  return (
    <div className="menubar">
      {TABS.map((tab) => (
        <div
          key={tab.id}
          className={`item${active === tab.id ? " active" : ""}`}
          onClick={() => onChange(tab.id)}
        >
          {tab.label}
        </div>
      ))}
    </div>
  );
}
