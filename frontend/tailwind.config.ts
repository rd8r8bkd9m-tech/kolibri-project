import type { Config } from "tailwindcss";

export default {
  darkMode: ["class"],
  content: ["./index.html", "./src/**/*.{ts,tsx}"],
  theme: {
    container: {
      center: true,
      padding: "1rem"
    },
    extend: {
      colors: {
        background: "rgb(var(--background) / <alpha-value>)",
        foreground: "rgb(var(--foreground) / <alpha-value>)",
        card: "rgb(var(--card) / <alpha-value>)",
        sidebar: "rgb(var(--sidebar) / <alpha-value>)",
        muted: "rgb(var(--muted) / <alpha-value>)",
        border: "rgb(var(--border) / <alpha-value>)",
        overlay: "rgb(var(--overlay) / <alpha-value>)",
        cyan: {
          400: "#00eaff",
          500: "#00f2ff",
          600: "#00b3ff"
        }
      },
      boxShadow: {
        "glow-cyan": "0 0 20px rgba(0,242,255,0.15)",
        "glow-cyan-sm": "0 0 12px rgba(0,242,255,0.12)",
        "soft-inner": "inset 0 1px 2px rgba(255,255,255,0.05)"
      },
      backdropBlur: {
        md: "12px"
      },
      keyframes: {
        ringPulse: {
          "0%, 100%": { boxShadow: "0 0 0 0 rgba(0,242,255,0.25)" },
          "50%": { boxShadow: "0 0 0 6px rgba(0,242,255,0.05)" }
        }
      },
      animation: {
        "ring-pulse": "ringPulse 1.4s ease-in-out infinite"
      }
    }
  },
  plugins: [require("tailwindcss-animate")]
} satisfies Config;
