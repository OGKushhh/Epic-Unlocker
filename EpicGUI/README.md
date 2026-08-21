# EpicGUI v2.0 — Tauri + React + TypeScript + Tailwind + Rust

A desktop GUI for **Epic Achievements Unlocker**. Replaces the old Win32/ImGui EpicGUI with a modern web-stack-based UI that compiles down to a single portable `.exe`.

## Stack

| Layer | Tech |
|---|---|
| Frontend | React 18 + TypeScript 5.6 |
| Styling | Tailwind CSS 3.4 (custom dark palette ported from old EpicGUI.cpp) |
| Build tool | Vite 6.4.3 (Tauri's default) |
| Backend | Rust (stable) + Tauri 2.1 |
| IPC to ScreamAPI | Named pipe `\\.\pipe\EpicGUI` (binary protocol — see `src-tauri/src/pipe_protocol.rs`) |
| Output | Single `EpicGUI.exe` (~10–15 MB portable, no DLLs needed on Win10/11) |


## Build instructions

### Prerequisites (Windows host — required for the final .exe)

1. **Microsoft Visual Studio C++ Build Tools** (for the Rust MSVC toolchain)
2. **Rust** — install via <https://rustup.rs>, then:
   ```powershell
   rustup default stable-x86_64-pc-windows-msvc
   ```
3. **Node.js 22+** (LTS recommended) — <https://nodejs.org>
4. **WebView2** — preinstalled on Windows 10/11. If missing, grab the Evergreen Bootstrapper from Microsoft.

### Build steps (on Windows)

```powershell
cd EpicGUI
npm install
npm run tauri:build
```

### Where the .exe lives

After a successful build:

- **Portable executable (what you want):**
  ```
  src-tauri/target/release/EpicGUI.exe
  ```
  This single file is portable — copy to USB, email it, drop on any Windows 10/11 machine. It embeds all HTML/CSS/JS inside itself. No DLLs, no installer needed.

- **Installers (ignore these):**
  ```
  src-tauri/target/release/bundle/nsis/EpicGUI_2.0.0_x64-setup.exe
  src-tauri/target/release/bundle/msi/EpicGUI_2.0.0_x64_en-US.msi
  ```

### Development mode

```powershell
npm run tauri:dev
```

Opens the app with hot-reload. Edits to `src/` rebuild instantly; edits to `src-tauri/src/` recompile the Rust binary.

## Frontend ↔ Backend bridge

The React frontend calls Rust via `invoke()`:

```ts
import { getAchievements, unlockAchievement } from "@/lib/api";

const ach = await getAchievements();         // → calls Rust get_achievements
await unlockAchievement(ach[0].id);          // → sends CmdUnlock packet to DLL
```

The Rust backend also pushes events to the frontend:

```ts
import { listen } from "@tauri-apps/api/event";

listen("achievement-update", (e) => {
  // e.payload = { id: string, state: "Locked" | "Unlocked" | "Unlocking" }
});
```

Available events: `connection-changed`, `achievements-list`, `achievement-update`, `log-path`, `dlc-catalog`.

## Protocol compatibility

`src-tauri/src/pipe_protocol.rs` is a byte-for-byte port of `EpicGUI/src/pipe_protocol.h`. Struct sizes verified:

| Struct | Size | Match |
|---|---|---|
| `PktHeader` | 9 | ✓ |
| `AchListHeader` | 8 | ✓ |
| `AchEntry` | 14 | ✓ |
| `AchUpdatePkt` | 129 | ✓ |
| `CmdUnlockPkt` | 128 | ✓ |
| `LogPathPkt` | 260 | ✓ |
| `DlcCatalogHeader` | 8 | ✓ |
| `DlcCatalogEntry` | 8 | ✓ |

The existing Epic Unlocker DLL does not need any changes — it'll keep serving the same pipe protocol; the new Rust client just connects and parses.