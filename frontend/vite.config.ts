import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";
import { VitePWA } from "vite-plugin-pwa";
import { resolve } from "node:path";

export default defineConfig({
  plugins: [
    react(),
    VitePWA({
      registerType: "autoUpdate",
      selfDestroying: true,
      includeAssets: ["g-logo.svg"],
      manifest: {
        name: "Колибри AI",
        short_name: "Колибри",
        description: "Чат-интерфейс Колибри AI",
        theme_color: "#000000",
        background_color: "#000000",
        display: "standalone",
        start_url: "/",
        icons: [{ src: "/g-logo.svg", sizes: "32x32", type: "image/svg+xml" }]
      },
      workbox: {
        clientsClaim: true,
        skipWaiting: true,
        cleanupOutdatedCaches: true,
      },
      devOptions: {
        enabled: false
      }
    })
  ],
  resolve: {
    alias: {
      "@": resolve(__dirname, "./src")
    }
  },
  build: {
    rollupOptions: {
      output: {
        manualChunks: {
          "vendor-react": ["react", "react-dom", "@tanstack/react-query", "zustand"],
          "vendor-ui": [
            "@radix-ui/react-avatar",
            "@radix-ui/react-dialog",
            "@radix-ui/react-dropdown-menu",
            "@radix-ui/react-scroll-area",
            "@radix-ui/react-slot",
            "@radix-ui/react-switch",
            "@radix-ui/react-tabs",
            "lucide-react",
            "class-variance-authority",
            "clsx",
            "tailwind-merge",
          ],
          "vendor-markdown": ["dompurify", "markdown-it", "react-syntax-highlighter"],
          "vendor-motion": ["framer-motion"],
        },
      },
    },
  },
  server: {
    host: true,
    port: 3000,
    proxy: {
      "/api": {
        target: "http://127.0.0.1:8001",
        changeOrigin: true
      }
    }
  }
});
