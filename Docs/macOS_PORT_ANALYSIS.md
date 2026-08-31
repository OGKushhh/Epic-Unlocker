# macOS Port — Full Analysis & Plan

> EpicFlash (Epic Unlocker + EpicGUI + Overlay) → macOS  
> Date: 2026-08-17

---

## 1. Architecture Overview

```
┌─────────────────────────────────────────────────┐
│                  Game Process                     │
│                                                   │
│  ┌──────────────┐    ┌───────────────────────┐   │
│  │  EOS SDK     │◄───│  Epic Unlocker (.dll)     │   │
│  │  (real)      │    │  hooks via MinHook    │   │
│  └──────────────┘    │  proxy via LinkerExp  │   │
│                       │  pipe server ───────────────► EpicGUI (Tauri)
│  ┌──────────────┐    └───────────────────────┘   │
│  │  Overlay     │                                  │
│  │  (DX11/DX12) │  ImGui in-game overlay           │
│  └──────────────┘                                  │
└─────────────────────────────────────────────────┘
```

**Three components to port:**

| Component | Tech | Role |
|-----------|------|------|
| **EpicGUI** | Rust/Tauri + React | Desktop GUI, communicates via IPC |
| **Epic Unlocker** | C++ DLL | Injected into game, hooks EOS SDK, serves pipe |
| **Overlay** | C++ DX11/DX12 + ImGui | In-game overlay rendered via DirectX |

---

## 2. Cross-Platform Scorecard

### ✅ Already Portable (zero changes)

| Layer | Why |
|-------|-----|
| All React frontend (`src/`) | Runs in WebView — OS-agnostic |
| Tailwind CSS, i18n, themes | Pure web |
| Tauri event system | Cross-platform by design |
| All React hooks (`useToasts`, `useTheme`, `useMusic`, `useGameData`) | Pure React |
| Manifest upload logic (HTTP, SHA256, chunking, retry) | Pure Rust + reqwest |
| Consent & hash cache (JSON files) | Tauri `app_local_data_dir` works on macOS |
| Settings persistence | Same Tauri path APIs |
| Achievement rarity fetching | Pure HTTP to egdata/Epic APIs |
| `Cargo.toml` dependencies | All cross-platform (reqwest, sha2, serde, etc.) |
| `build.rs` | Pure Rust, reads file, sets env var |
| `package.json` | No OS-specific scripts |
| EOS SDK Mac headers | Already exist at `eos-sdk/Mac/` |

### 🔧 Needs Changes (manageable)

| Component | What | Effort |
|-----------|------|--------|
| `windows_impl.rs` → `macos_impl.rs` | Replace Win32 named pipe with Unix domain socket | Medium |
| `stub_impl.rs` | Replace with real macOS implementation | Medium |
| `pipe_protocol.rs` | Change pipe name constant `\\.\pipe\EpicGUI` → `/tmp/epicgui.sock` | Low |
| `lib.rs` | Add `#[cfg(target_os = "macos")] mod macos_impl;` | Trivial |
| `commands.rs` `ShellExecuteW` | Replace with `Command::new("open")` | Low |
| `commands.rs` log reading | Remove unnecessary `#[cfg(target_os = "windows")]` — code is portable | Low |
| `manifest.rs` `manifest_dirs()` | Add macOS Epic Launcher paths | Low |
| `tauri.conf.json` | Add `"dmg"` / `"app"` bundle targets, macOS icon | Low |
| `PipeServer.cpp` | Rewrite for Unix domain socket (server side) | High |
| `dllmain.cpp` | Replace `DllMain` with `__attribute__((constructor))` | Medium |
| `ScreamAPI.h/cpp` | Replace `HMODULE`/`LoadLibrary`/`GetProcAddress` → `dlopen`/`dlsym` | High |
| `eos_hooks.cpp` | Replace MinHook → fishhook | High |
| `Logger.cpp` | Replace Win32 file I/O → POSIX | Low |
| `Config.cpp` | Replace `std::wstring` → `std::string`, `MessageBox` → stderr | Low |
| Build system | `.vcxproj` → CMakeLists.txt, `build.bat` → `build.sh` | Medium |

### ❌ Blockers (major rework or redesign)

| Component | Why |
|-----------|-----|
| **Overlay** | 100% DirectX + Win32. Needs complete rewrite for Metal. |
| **DLL export forwarding** (`LinkerExports64.h`) | Windows-only mechanism. macOS uses `DYLD_INSERT_LIBRARIES` (simpler but different). |
| **Epic Unlocker injection model** | On Windows: DLL proxy (`Epic Unlocker.dll` masquerades as `EOSSDK-Win64-Shipping.dll`). On macOS: `DYLD_INSERT_LIBRARIES` for symbol interposition. Fundamentally different mechanism. |

---

## 3. Win32 API → macOS Mapping (Complete)

### Rust Side (EpicGUI)

| Win32 API | File | macOS Replacement |
|-----------|------|-------------------|
| `CreateFileW` on `\\.\pipe\` | `windows_impl.rs` | `tokio::net::UnixStream::connect("/tmp/epicgui.sock")` |
| `ReadFile` / `WriteFile` | `windows_impl.rs` | `AsyncReadExt::read` / `AsyncWriteExt::write` on UnixStream |
| `PeekNamedPipe` | `windows_impl.rs` | Not needed — async Unix socket is naturally non-blocking |
| `CloseHandle` | `windows_impl.rs` | `drop(UnixStream)` |
| `ShellExecuteW` | `commands.rs` | `std::process::Command::new("open").arg(path).spawn()` |
| `GetLastError` | `windows_impl.rs` | `std::io::Error::last_os_error()` / `errno` |

### C++ Side (Epic Unlocker + Overlay)

| Win32 API | File | macOS Replacement |
|-----------|------|-------------------|
| `CreateNamedPipeW` | `PipeServer.cpp` | `socket(AF_UNIX)` + `bind()` + `listen()` |
| `ConnectNamedPipe` | `PipeServer.cpp` | `accept()` |
| `PeekNamedPipe` | `PipeServer.cpp` | `poll()` / `select()` |
| `ReadFile` / `WriteFile` | `PipeServer.cpp`, `Logger.cpp` | `read()` / `write()` or `std::ofstream` |
| `CloseHandle` | `PipeServer.cpp`, `Logger.cpp` | `close()` |
| `DisconnectNamedPipe` | `PipeServer.cpp` | `close()` on socket |
| `CancelIoEx` | `PipeServer.cpp` | `shutdown()` on socket |
| `Sleep(ms)` | `ScreamAPI.cpp`, `PipeServer.cpp` | `std::this_thread::sleep_for(ms)` |
| `GetModuleHandle` | `ScreamAPI.cpp` | `dlopen(nullptr, RTLD_NOLOAD)` or `RTLD_DEFAULT` |
| `LoadLibrary` / `FreeLibrary` | `ScreamAPI.cpp` | `dlopen()` / `dlclose()` |
| `GetProcAddress` | `ScreamAPI.h`, `eos_compat.cpp` | `dlsym()` |
| `GetModuleFileName` | `util.cpp` | `dladdr()` |
| `DllMain` | `dllmain.cpp` | `__attribute__((constructor))` / `__attribute__((destructor))` |
| `MH_CreateHook` (MinHook) | `eos_hooks.cpp` | `fishhook` — `rebind_symbols()` |
| `WideCharToMultiByte` | `PipeServer.cpp` | Not needed (UTF-8 native on macOS) |
| `GetLocalTime` | `Logger.cpp` | `std::chrono::system_clock` or `localtime_r()` |
| `gmtime_s` | `PipeServer.cpp` | `gmtime_r()` |
| `strncpy_s` / `sprintf_s` | `PipeServer.cpp`, `Logger.cpp` | `strncpy()` / `snprintf()` |
| `MessageBox` | `Logger.cpp`, `Config.cpp` | `fprintf(stderr, ...)` or `NSAlert` |
| `CreateDXGIFactory1` / `D3D12CreateDevice` | `d3d12hook.cpp` | `MTLCreateSystemDefaultDevice` |
| `GetCursorPos` / `GetAsyncKeyState` | `Overlay.cpp` | `NSEvent.mouseLocation` / `modifierFlags` |
| `RegisterHotKey` | `HotkeyHandler.cpp` | `NSEvent.addLocalMonitorForEvents` |

---

## 4. Hardcoded Windows Paths

| Windows Path | File | macOS Equivalent |
|-------------|------|------------------|
| `%PROGRAMDATA%\Epic\EpicGamesLauncher\Data\Manifests\` | `manifest.rs` | `~/Library/Application Support/Epic Games/EpicGamesLauncher/Manifests/` |
| `%LOCALAPPDATA%\EpicGamesLauncher\Saved\` | `manifest.rs` | `~/Library/Application Support/Epic Games/EpicGamesLauncher/Saved/` |
| `Engine/Binaries/ThirdParty/EOS/Win64/` | `ScreamAPI.cpp` | `Contents/MacOS/` or `Epic/OnlineServices/Mac/` |
| `EOSSDK-Win64-Shipping.dll` | `Constants.h` | `libEOSSDK-Mac-Shipping.dylib` |
| `\\.\pipe\EpicGUI` | `pipe_protocol.rs`, `pipe_protocol.h` | `/tmp/epicgui.sock` |
| `ScreamAPI.log` | Various | Same (portable) |

---

## 5. Existing CFG Gates (Good News!)

The codebase **already has cross-platform scaffolding**:

```rust
// lib.rs
#[cfg(target_os = "windows")]
mod windows_impl;
#[cfg(not(target_os = "windows"))]
mod stub_impl;

// pipe_client.rs
#[cfg(target_os = "windows")]
pub use crate::windows_impl::PipeClient;
#[cfg(not(target_os = "windows"))]
pub use crate::stub_impl::PipeClient;

// commands.rs — multiple locations
#[cfg(target_os = "windows")]
{ /* real implementation */ }
#[cfg(not(target_os = "windows"))]
{ /* stub */ }

// manifest.rs
#[cfg(target_os = "windows")]
{ /* Windows manifest dirs */ }
#[cfg(not(target_os = "windows"))]
{ Vec::new() }
```

This means the architecture was **designed with portability in mind** — the stubs just need real implementations.

---

## 6. Injection Mechanism: Windows vs macOS

This is the most fundamental architectural difference.

### Windows (current)
```
Game.exe loads EOSSDK-Win64-Shipping.dll
        ↓ (but finds Epic Unlocker.dll renamed to that name)
Epic Unlocker.dll loads via DllMain
        ↓
Links to real EOSSDK-Win64-Shipping_o.dll via LinkerExports forwarding
        ↓
Hooks EOS_Achievements_* functions via MinHook
        ↓
Starts PipeServer on \\.\pipe\EpicGUI
        ↓
EpicGUI.exe connects to named pipe
```

### macOS (proposed)
```
Game.app launches
        ↓
DYLD_INSERT_LIBRARIES=libEpic Unlocker.dylib injected into process
        ↓
__attribute__((constructor)) runs on dylib load
        ↓
dlopen("libEOSSDK-Mac-Shipping.dylib") to get real SDK
        ↓
fishhook rebind_symbols() for EOS_Achievements_* functions
        ↓
Starts Unix socket server on /tmp/epicgui.sock
        ↓
EpicGUI.app connects to Unix socket
```

**Key differences:**
- No DLL proxy/export forwarding needed — `DYLD_INSERT_LIBRARIES` handles symbol interposition
- No `DllMain` — use `__attribute__((constructor))`
- No MinHook — use **fishhook** (Facebook's open-source rebinding library for macOS)
- The dylib must be **code-signed** (or SIP disabled) for `DYLD_INSERT_LIBRARIES` to work

**⚠️ SIP (System Integrity Protection) consideration:**
On modern macOS, `DYLD_INSERT_LIBRARIES` is **blocked for system-protected binaries**. For third-party game apps, it usually works. If a game is hardened runtime signed, you may need `DYLD_INSERT_LIBRARIES` + `csreq` or an entitlements exception.

---

## 7. Overlay: The Blocker

The overlay is **100% Windows-only** — DirectX 11/12 rendering, Win32 window management, raw input, cursor clipping. A macOS port requires a complete rewrite.

### Options

| Option | Effort | Result |
|--------|--------|--------|
| **A. Port to Metal** | Very High (months) | Full in-game overlay on macOS via Metal + ImGui_ImplOSX |
| **B. Port to OpenGL** | High | Works but less performant, ImGui has OpenGL3 backend |
| **C. Skip overlay** | Zero | EpicGUI already provides ALL the same functionality as a standalone app. Overlay is a convenience, not a requirement. |
| **D. Web overlay** | Medium | Use a transparent webview (like Steam's overlay) — possible with Tauri's WebView |

**Recommendation: Option C (skip overlay for v1)**. The EpicGUI provides achievements list, DLC catalog, unlock functionality, log viewer, and settings — everything the overlay does. The overlay is a convenience for in-game access, not core functionality.

---

## 8. Phased Implementation Plan

### Phase 1: EpicGUI macOS Port
**Goal:** EpicGUI compiles and runs on macOS in "disconnected" mode (no pipe connection)

| Step | Task | Effort |
|------|------|--------|
| 1.1 | Create `macos_impl.rs` with `tokio::net::UnixStream` pipe client | 2 days |
| 1.2 | Update `lib.rs` cfg gates: add `#[cfg(target_os = "macos")] mod macos_impl;` | 1 hour |
| 1.3 | Update `pipe_client.rs` cfg: macOS uses `macos_impl::PipeClient` | 1 hour |
| 1.4 | Add `EPIC_PIPE_NAME` cfg gate in `pipe_protocol.rs` → `"/tmp/epicgui.sock"` | 1 hour |
| 1.5 | Implement `open_with_default_app_macos()` via `Command::new("open")` | 2 hours |
| 1.6 | Ungate `read_log_incremental` — remove `#[cfg(target_os = "windows")]` (it's portable `std::fs`) | 1 hour |
| 1.7 | Ungate `clear_log` similarly | 30 min |
| 1.8 | Add macOS paths to `manifest_dirs()` | 2 hours |
| 1.9 | Update `tauri.conf.json`: add `"dmg"`, `"app"` targets, macOS icon (`icon.icns` or `.png`) | 1 hour |
| 1.10 | Test `npm run tauri dev` on macOS | 1 day |
| **Total** | | **~4–5 days** |

### Phase 2: Epic Unlocker macOS Port (without Overlay)
**Goal:** Epic Unlocker as `.dylib`, injectable via `DYLD_INSERT_LIBRARIES`, communicates with EpicGUI over Unix socket

| Step | Task | Effort |
|------|------|--------|
| 2.1 | Create `CMakeLists.txt` build system for Epic Unlocker on macOS | 1 day |
| 2.2 | Rewrite `PipeServer.cpp` for Unix domain socket (server side) | 2 days |
| 2.3 | Rewrite `dllmain.cpp` → `__attribute__((constructor))` / `__attribute__((destructor))` | 4 hours |
| 2.4 | Replace `HMODULE`/`LoadLibrary`/`FreeLibrary` → `void*`/`dlopen`/`dlclose` in `ScreamAPI.h/cpp` | 1 day |
| 2.5 | Replace `GetProcAddress` → `dlsym` in `ScreamAPI.h` and `eos_compat.cpp` | 4 hours |
| 2.6 | Replace `GetModuleFileName` → `dladdr` in `util.cpp` | 2 hours |
| 2.7 | Replace MinHook → fishhook in `eos_hooks.cpp` | 2 days |
| 2.8 | Replace `LinkerExports` → DYLD_INSERT_LIBRARIES (no export forwarding needed) | 4 hours |
| 2.9 | Port `Logger.cpp`: Win32 file I/O → `std::ofstream`, `GetLocalTime` → `localtime_r`, `sprintf_s` → `snprintf` | 4 hours |
| 2.10 | Port `Config.cpp`: `std::wstring` → `std::string`, `MessageBox` → `fprintf(stderr)` | 4 hours |
| 2.11 | Update `Constants.h`: `EOSSDK-Win64-Shipping.dll` → `libEOSSDK-Mac-Shipping.dylib` | 1 hour |
| 2.12 | Update `pipe_protocol.h`: pipe name → socket path | 1 hour |
| 2.13 | Replace Windows subfolder paths (`Engine/Binaries/ThirdParty/EOS/Win64/`) → macOS equivalents | 2 hours |
| 2.14 | Build & test dylib injection on a macOS game | 2 days |
| **Total** | | **~8–10 days** |

### Phase 3: Build System & Distribution
**Goal:** One-command build, signed macOS app

| Step | Task | Effort |
|------|------|--------|
| 3.1 | Create `build.sh` (equivalent of `build.bat`) | 4 hours |
| 3.2 | Create `Makefile` or CMake target for Epic Unlocker dylib | 4 hours |
| 3.3 | Apple Developer certificate + code signing | 1 day |
| 3.4 | Notarization workflow (`notarytool submit`) | 1 day |
| 3.5 | DMG packaging via Tauri bundler | 4 hours |
| **Total** | | **~3 days** |

### Phase 4: Overlay (Optional — Future Work)
**Goal:** In-game overlay on macOS

| Step | Task | Effort |
|------|------|--------|
| 4.1 | Choose backend: Metal or OpenGL | Decision |
| 4.2 | Rewrite rendering: DX11/DX12 → chosen backend | Weeks |
| 4.3 | Replace kiero vtable hook → method swizzling / fishhook | Days |
| 4.4 | Replace Win32 input → NSEvent / Cocoa | Days |
| 4.5 | ImGui backend: `imgui_impl_win32` → `imgui_impl_osx` | Days |
| 4.6 | Replace DX texture loading → Metal/OpenGL textures | Days |
| **Total** | | **Weeks+** (recommend skipping for v1) |

---

## 9. Time & Effort Summary

| Phase | Scope | Time | Risk |
|-------|-------|------|------|
| **Phase 1** | EpicGUI macOS port | 4–5 days | Low — well-structured cfg gates already exist |
| **Phase 2** | Epic Unlocker dylib port | 8–10 days | Medium — fishhook migration + DYLD_INSERT_LIBRARIES testing |
| **Phase 3** | Build & distribution | 3 days | Low — standard macOS tooling |
| **Phase 4** | Overlay (optional) | Weeks+ | High — Metal rewrite is essentially a new project |
| **Total (v1, no overlay)** | | **~15–18 days** | |

---

## 10. Risk Assessment

| Risk | Severity | Mitigation |
|------|----------|------------|
| **SIP blocks DYLD_INSERT_LIBRARIES** | High | Test on target games; hardened runtime may need `DYLD_INSERT_LIBRARIES` + entitlements workaround |
| **fishhook doesn't cover all hook scenarios** | Medium | fishhook handles dynamic symbol rebinding; for vtable hooks, use Objective-C method swizzling |
| **EOS SDK Mac differs from Windows** | Medium | Mac headers exist in codebase; real SDK from Epic may have API differences |
| **macOS Epic Games Launcher paths differ** | Low | Easy to find — just inspect a Mac EGL installation |
| **No overlay = reduced functionality** | Low | EpicGUI provides all the same features; overlay is convenience-only |
| **Code signing / notarization complexity** | Medium | Standard macOS process; well-documented |

---

## 11. Quick-Start: What to Do First

```bash
# 1. Install Rust macOS target (if cross-compiling from another machine)
rustup target add aarch64-apple-darwin
rustup target add x86_64-apple-darwin

# 2. Try building EpicGUI on macOS
cd EpicGUI
npm install
npm run tauri dev    # Will compile with stub_impl — UI works, pipe disconnected

# 3. Create the Unix socket implementation
touch src-tauri/src/macos_impl.rs
# ... implement PipeClient using tokio::net::UnixStream

# 4. Test end-to-end
# Start Epic Unlocker-injected game on macOS
# Start EpicGUI
# Watch for Unix socket connection
```

---

## 12. File Change Checklist

### EpicGUI (Rust) — Must Change

- [ ] `src-tauri/src/macos_impl.rs` — **NEW** — Unix domain socket client
- [ ] `src-tauri/src/lib.rs` — Add `#[cfg(target_os = "macos")] mod macos_impl;`
- [ ] `src-tauri/src/pipe_client.rs` — Add macOS branch for `PipeClient`
- [ ] `src-tauri/src/pipe_protocol.rs` — Add macOS socket path constant
- [ ] `src-tauri/src/commands.rs` — Implement `open_with_default_app` for macOS, ungate log reading
- [ ] `src-tauri/src/manifest.rs` — Add macOS `manifest_dirs()` implementation
- [ ] `src-tauri/tauri.conf.json` — Add macOS bundle targets and icons

### Epic Unlocker (C++) — Must Change

- [ ] `CMakeLists.txt` — **NEW** — macOS build system
- [ ] `src/PipeServer.cpp` — Rewrite for Unix domain socket
- [ ] `src/PipeServer.h` — Replace `std::wstring` with `std::string`
- [ ] `src/dllmain.cpp` — Replace `DllMain` with `__attribute__((constructor))`
- [ ] `src/ScreamAPI.h` — Replace `HMODULE`/`GetProcAddress` → `void*`/`dlsym`
- [ ] `src/ScreamAPI.cpp` — Replace `LoadLibrary`/`GetModuleHandle`/`GetModuleFileName` → `dlopen`/`dladdr`
- [ ] `src/eos_hooks.cpp` — Replace MinHook → fishhook
- [ ] `src/pipe_protocol.h` — Add macOS socket path, keep wire format identical
- [ ] `src/Constants.h` — Change DLL name to macOS dylib name
- [ ] `src/Logger.cpp` — Replace Win32 file I/O → POSIX
- [ ] `src/Logger.h` — Replace `std::wstring` → `std::string`
- [ ] `src/eos_compat.cpp` — Replace `GetProcAddress` → `dlsym`
- [ ] `src/util.cpp` — Replace `GetModuleFileName` → `dladdr`

### Build System — Must Change

- [ ] `build.sh` — **NEW** — macOS equivalent of `build.bat`
- [ ] `ScreamAPI/CMakeLists.txt` — **NEW** — CMake build for dylib

### Overlay — SKIP for v1

- [ ] Not ported — EpicGUI provides equivalent functionality
