import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";
import path from "path";
// Tauri expects a fixed port + external host when running `tauri dev`
const host = process.env.TAURI_DEV_HOST;
// https://vitejs.dev/config/
export default defineConfig(async () => ({
    plugins: [react()],
    resolve: {
        alias: {
            "@": path.resolve(__dirname, "./src"),
        },
    },
    // Vite options tailored for Tauri development
    clearScreen: false,
    server: {
        // Tauri expects a fixed port, fail if that port is not available
        port: 1420,
        strictPort: true,
        host: host || false,
        hmr: host
            ? {
                protocol: "ws",
                host,
                port: 1421,
            }
            : undefined,
        watch: {
            // Tell vite to ignore watching `src-tauri`
            ignored: ["**/src-tauri/**"],
        },
    },
}));
