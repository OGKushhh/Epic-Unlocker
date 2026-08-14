/**
 * Theme system — carbon copy of the mockup's cycleTheme() logic.
 *  - 3 themes: gold → light → dark → gold...
 *  - Theme is applied as `data-theme` attribute on <html>
 *  - Also persisted to localStorage (small UX bonus; mockup didn't have this
 *    but a real desktop app should remember the user's choice)
 */

import { useCallback, useEffect, useState } from "react";

export type ThemeName = "gold" | "light" | "dark";

const THEMES: ThemeName[] = ["gold", "light", "dark"];
const THEME_ICONS: Record<ThemeName, string> = {
  gold: "🌙",
  light: "☀️",
  dark: "🌙",
};
const STORAGE_KEY = "epicgui.theme";

function loadInitial(): ThemeName {
  try {
    const saved = localStorage.getItem(STORAGE_KEY);
    if (saved && THEMES.includes(saved as ThemeName)) return saved as ThemeName;
  } catch {
    // ignore — SSR or storage disabled
  }
  return "gold"; // mockup default
}

export function useTheme() {
  const [theme, setThemeState] = useState<ThemeName>(loadInitial);

  useEffect(() => {
    document.documentElement.setAttribute("data-theme", theme);
    try {
      localStorage.setItem(STORAGE_KEY, theme);
    } catch {
      // ignore
    }
  }, [theme]);

  const setTheme = useCallback((name: ThemeName) => {
    setThemeState(THEMES.includes(name) ? name : "gold");
  }, []);

  const cycleTheme = useCallback(() => {
    setThemeState((prev) => {
      const idx = THEMES.indexOf(prev);
      return THEMES[(idx + 1) % THEMES.length];
    });
  }, []);

  return {
    theme,
    setTheme,
    cycleTheme,
    icon: THEME_ICONS[theme],
  };
}
