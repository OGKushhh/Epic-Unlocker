/** @type {import('tailwindcss').Config} */
export default {
  content: [
    "./index.html",
    "./src/**/*.{js,ts,jsx,tsx}",
  ],
  theme: {
    extend: {
      colors: {
        // Palette ported from the existing EpicGUI.cpp
        // Base: dark + achievement-gold accent (no purple)
        bg: "#1a1a1a",
        surface: "#2a2a2a",
        "surface-2": "#323232",
        "surface-3": "#3a3a3a",
        border: "#3a3a3a",
        "border-2": "#505050",
        text: "#e0e0e0",
        "text-dim": "#828282",
        "text-sub": "#aaaaaa",
        accent: "#c9a227",        // achievement gold
        "accent-2": "#e6be46",    // brighter gold (hover)
        "accent-dim": "#785f19",
        success: "#2ec470",
        warning: "#f8bc20",
        danger: "#ec4040",
        info: "#4bafc3",
        orange: "#f67014",
        header: "#141414",
      },
      fontFamily: {
        sans: ['"Segoe UI"', "system-ui", "-apple-system", "sans-serif"],
        mono: ['"Cascadia Code"', '"JetBrains Mono"', "Consolas", "monospace"],
      },
      fontSize: {
        // Tighter scale for desktop UI density
        "2xs": ["0.6875rem", { lineHeight: "1rem" }],
      },
      animation: {
        "fade-in": "fade-in 0.15s ease-out",
        "slide-up": "slide-up 0.2s ease-out",
        "pulse-gold": "pulse-gold 1.5s ease-in-out infinite",
      },
      keyframes: {
        "fade-in": {
          "0%": { opacity: "0" },
          "100%": { opacity: "1" },
        },
        "slide-up": {
          "0%": { opacity: "0", transform: "translateY(4px)" },
          "100%": { opacity: "1", transform: "translateY(0)" },
        },
        "pulse-gold": {
          "0%, 100%": { opacity: "1" },
          "50%": { opacity: "0.5" },
        },
      },
    },
  },
  plugins: [],
};
