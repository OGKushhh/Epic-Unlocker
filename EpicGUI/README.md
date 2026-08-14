# EpicGUI v2.0 — Tauri + React + TypeScript + Tailwind + Rust

A desktop GUI for **Epic Achievements Unlocker**. Replaces the old Win32/ImGui EpicGUI with a modern web-stack-based UI that compiles down to a single portable `.exe`.

## Stack

| Layer | Tech |
|---|---|
| Frontend | React 18 + TypeScript 5.6 |
| Styling | Tailwind CSS 3.4 (custom dark palette ported from old EpicGUI.cpp) |
| Build tool | Vite 5 (Tauri's default) |
| Backend | Rust (stable) + Tauri 2.1 |
| IPC to ScreamAPI | Named pipe `\\.\pipe\EpicGUI` (binary protocol — see `src-tauri/src/pipe_protocol.rs`) |
| Output | Single `EpicGUI.exe` (~10–15 MB portable, no DLLs needed on Win10/11) |

## Project layout

```
EpicGUI/
├── src/                        # React frontend
│   ├── App.tsx                 # Root component (placeholder until mockup)
│   ├── main.tsx                # React entry
│   ├── lib/api.ts              # Typed wrappers around `invoke()`
│   ├── types/index.ts          # Shared TS types (mirror Rust structs)
│   └── styles/globals.css      # Tailwind + custom scrollbar/btn classes
├── src-tauri/                  # Rust + Tauri backend
│   ├── Cargo.toml
│   ├── tauri.conf.json         # Window config (frameless, 1100x720)
│   ├── capabilities/default.json
│   └── src/
│       ├── main.rs             # Binary entry
│       ├── lib.rs              # Tauri builder + plugin registration
│       ├── commands.rs         # #[tauri::command] handlers (frontend API)
│       ├── pipe_protocol.rs    # Wire format ported from pipe_protocol.h
│       ├── pipe_client.rs      # Connection loop + packet dispatcher
│       ├── state.rs            # Shared AppState (Arc<RwLock<...>>)
│       ├── windows_impl.rs     # Win32 named-pipe client (CreateFileW)
│       └── stub_impl.rs        # Non-Windows stub (for dev on Linux/macOS)
├── package.json
├── vite.config.ts
├── tailwind.config.js
├── tsconfig.json
└── index.html
```

## Build instructions

### Prerequisites (Windows host — required for the final .exe)

1. **Microsoft Visual Studio C++ Build Tools** (for the Rust MSVC toolchain)
2. **Rust** — install via <https://rustup.rs>, then:
   ```powershell
   rustup default stable-x86_64-pc-windows-msvc
   ```
3. **Node.js 18+** (LTS recommended) — <https://nodejs.org>
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

The existing ScreamAPI DLL does not need any changes — it'll keep serving the same pipe protocol; the new Rust client just connects and parses.

## Status

- ✅ Tauri + React + TS + Tailwind scaffolding
- ✅ Rust backend (pipe protocol, command handlers, frameless window)
- ✅ Frontend shell with Tauri API hooks
- ✅ TypeScript compiles, Vite builds clean
- ✅ Rust pipe_protocol + state modules compile and pass size/runtime checks
- ⏳ **Waiting for UI mockup** — `src/App.tsx` is a placeholder
