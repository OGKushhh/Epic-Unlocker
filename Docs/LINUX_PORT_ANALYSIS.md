# Linux Port — Full Analysis & Plan

> EpicFlash (Epic Unlocker + EpicGUI + Overlay) → Linux
> Date: 2026-10-17
> Companion doc: [`macOS_PORT_ANALYSIS.md`](./macOS_PORT_ANALYSIS.md) — read alongside for comparison

---

## 1. Executive Summary

**Yes, Linux is meaningfully easier than macOS.** Three structural reasons:

1. **No SIP / hardened runtime.** `LD_PRELOAD` works on any Linux binary without code signing gymnastics. macOS requires dylib signing or SIP disabled for `DYLD_INSERT_LIBRARIES`.
2. **The Overlay blocker evaporates.** macOS requires a Metal rewrite (months). Linux has the **Vulkan layer mechanism** — purpose-built for this kind of hook, with reference implementations (MangoHud, Steam overlay, RenderDoc).
3. **EAC silencer is moot** until Epic ships EAC-Linux. Currently zero EAC-protected games run on Linux, so no silencer logic is needed for v1.

**Estimated effort:** ~3–4 weeks for a working Linux port (vs ~2 months for macOS).
**Recommended trigger:** wait for the v2 launcher to stabilize AND Fortnite to ship on Linux (Epic announced this is in progress alongside the launcher Preview). Before then, the test surface is too small.

This doc supersedes parts of `macOS_PORT_ANALYSIS.md` for Linux-specific decisions. Where the two docs disagree on a shared concern (e.g. Unix socket path, `xdg-open` vs `open`), this doc is authoritative for Linux.

---

## 2. Architecture Overview

```
┌──────────────────────────────────────────────────────────┐
│                    Game Process                           │
│                                                           │
│  ┌──────────────┐    ┌────────────────────────────────┐  │
│  │  EOS SDK     │◄───│  Epic Unlocker (.so)            │  │
│  │  libEOSSDK-  │    │  proxy via dlopen/dlsym         │  │
│  │  Linux-      │    │  hooks via LD_PRELOAD           │  │
│  │  Shipping.so │    │  interposition OR funchook      │  │
│  │  (real,      │    │  Unix socket server ─────────────────► EpicGUI (Tauri)
│  │  renamed)    │    └────────────────────────────────┘  │
│  └──────────────┘                                        │
│  ┌──────────────┐                                        │
│  │  Overlay     │  ImGui in-game overlay                 │
│  │  (Vulkan     │  via Vulkan layer                      │
│  │   layer)     │  (VK_LAYER_EpicOverlay)                │
│  └──────────────┘                                        │
└──────────────────────────────────────────────────────────┘
```

**Three components to port:**

| Component | Tech | Role |
|-----------|------|------|
| **EpicGUI** | Rust/Tauri + React | Desktop GUI, communicates via Unix socket |
| **Epic Unlocker** | C++ `.so` | Injected via `LD_PRELOAD` or proxy-replacement, hooks EOS SDK, serves socket |
| **Overlay** | C++ Vulkan + ImGui | In-game overlay rendered via Vulkan layer |

---

## 3. Linux vs macOS — Head-to-Head

| Concern | macOS | Linux | Winner |
|---|---|---|---|
| **Code signing** | dylib must be signed or SIP disabled | No signing required | **Linux** |
| **SIP / hardened runtime** | Modern apps can opt out of dylib injection | No equivalent | **Linux** |
| **Hook library** | fishhook (lazy-symbol only, flaky with newer dyld) | Multiple mature options (see §9) | **Linux** |
| **Overlay rendering** | Metal rewrite (months) | Vulkan layer (weeks, purpose-built for this) | **Linux** |
| **Overlay input handling** | NSEvent, complex | X11 XGrabKey + Wayland portal | **Tie** (both non-trivial) |
| **EAC silencer** | Required (EAC runs on macOS) | Not yet needed (no EAC games run on Linux) | **Linux** |
| **Test surface** | Small (few EOS games on macOS) | Small but growing (Rocket League native now, Fortnite coming) | **Tie** |
| **Distribution packaging** | `.dmg` (single target) | `.deb` + `.AppImage` + (optional `.rpm`) | **macOS** (simpler) |
| **Wayland vs X11 fragmentation** | N/A | Must support both | **macOS** |
| **Total estimated effort** | ~2 months | ~3–4 weeks | **Linux** |

---

## 4. Cross-Platform Scorecard

### ✅ Already Portable (zero changes)

| Layer | Why |
|-------|-----|
| All React frontend (`src/`) | Runs in WebView — OS-agnostic |
| Tailwind CSS, i18n, themes | Pure web |
| Tauri event system | Cross-platform by design |
| All React hooks (`useToasts`, `useTheme`, `useMusic`, `useGameData`) | Pure React |
| Manifest upload logic (HTTP, SHA256, chunking, retry) | Pure Rust + reqwest |
| Consent & hash cache (JSON files) | Tauri `app_local_data_dir` works on Linux |
| Settings persistence | Same Tauri path APIs |
| Achievement rarity fetching | Pure HTTP to egdata/Epic APIs |
| `Cargo.toml` dependencies | All cross-platform (reqwest, sha2, serde, etc.) |
| `build.rs` | Pure Rust, reads file, sets env var |
| `package.json` | No OS-specific scripts |
| EOS SDK Linux headers | Already exist at `eos-sdk/Linux/` |

### 🔧 Needs Changes (manageable)

| Component | What | Effort |
|-----------|------|--------|
| `windows_impl.rs` → `linux_impl.rs` | Replace Win32 named pipe with Unix domain socket | Medium |
| `stub_impl.rs` | Replace with real Linux implementation | Medium |
| `pipe_protocol.rs` | Change pipe name constant `\\.\pipe\EpicGUI` → `/tmp/epicgui.sock` | Low |
| `lib.rs` | Add `#[cfg(target_os = "linux")] mod linux_impl;` | Trivial |
| `commands.rs` `ShellExecuteW` | Replace with `Command::new("xdg-open")` | Low |
| `commands.rs` log reading | Remove unnecessary `#[cfg(target_os = "windows")]` — code is portable | Low |
| `manifest.rs` `manifest_dirs()` | Add Linux Epic Launcher paths (v1 + v2 — see EGLv2 doc) | Low |
| `tauri.conf.json` | Add `"deb"` / `"appimage"` bundle targets, Linux icon | Low |
| `PipeServer.cpp` | Rewrite for Unix domain socket (server side) | High |
| `dllmain.cpp` | Replace `DllMain` with `__attribute__((constructor))` | Medium |
| `ScreamAPI.h/cpp` | Replace `HMODULE`/`LoadLibrary`/`GetProcAddress` → `dlopen`/`dlsym` | High |
| `eos_hooks.cpp` | Replace MinHook → LD_PRELOAD interposition (preferred) or funchook | Medium |
| `Logger.cpp` | Replace Win32 file I/O → POSIX | Low |
| `Config.cpp` | Replace `std::wstring` → `std::string`, `MessageBox` → stderr | Low |
| Build system | `.vcxproj` → CMakeLists.txt, `build.bat` → `build.sh` | Medium |

### ❌ Blockers (major rework or redesign)

| Component | Why | Mitigation |
|-----------|------|-----------|
| **Overlay** | 100% DirectX + Win32. Needs complete rewrite. | **Vulkan layer** — see §8. Not a true blocker on Linux, just substantial work. |
| **DLL export forwarding** (`LinkerExports64.h`) | Windows-only mechanism. | **LD_PRELOAD or .so proxy** — simpler. See §7. Not a blocker, just different. |

Note: Unlike macOS, the Overlay is **not a true blocker** on Linux. The Vulkan layer mechanism was designed for exactly this use case (RenderDoc, MangoHud, Steam overlay all use it). It's substantial work (~2 weeks) but not a "months of Metal rewrite" blocker.

---

## 5. Win32 API → Linux Mapping (Complete)

### Rust Side (EpicGUI)

| Win32 API | File | Linux Replacement |
|-----------|------|-------------------|
| `CreateFileW` on `\\.\pipe\` | `windows_impl.rs` | `tokio::net::UnixStream::connect("/tmp/epicgui.sock")` |
| `ReadFile` / `WriteFile` | `windows_impl.rs` | `AsyncReadExt::read` / `AsyncWriteExt::write` on UnixStream |
| `PeekNamedPipe` | `windows_impl.rs` | Not needed — async Unix socket is naturally non-blocking |
| `CloseHandle` | `windows_impl.rs` | `drop(UnixStream)` |
| `ShellExecuteW` | `commands.rs` | `std::process::Command::new("xdg-open").arg(path).spawn()` |
| `GetLastError` | `windows_impl.rs` | `std::io::Error::last_os_error()` / `errno` |

### C++ Side (Epic Unlocker + Overlay)

| Win32 API | File | Linux Replacement |
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
| `MH_CreateHook` (MinHook) | `eos_hooks.cpp` | **LD_PRELOAD interposition (preferred)** or funchook (see §9) |
| `WideCharToMultiByte` | `PipeServer.cpp` | Not needed (UTF-8 native on Linux) |
| `GetLocalTime` | `Logger.cpp` | `std::chrono::system_clock` or `localtime_r()` |
| `gmtime_s` | `PipeServer.cpp` | `gmtime_r()` |
| `strncpy_s` / `sprintf_s` | `PipeServer.cpp`, `Logger.cpp` | `strncpy()` / `snprintf()` |
| `MessageBox` | `Logger.cpp`, `Config.cpp` | `fprintf(stderr, ...)` or `zenity --error` |
| `CreateDXGIFactory1` / `D3D12CreateDevice` | `d3d12hook.cpp` | Vulkan: `vkCreateInstance` + layer (see §8) |
| `GetCursorPos` / `GetAsyncKeyState` | `Overlay.cpp` | X11: `XQueryPointer` / `XGrabKey`. Wayland: portal API. |
| `RegisterHotKey` | `HotkeyHandler.cpp` | X11: `XGrabKey`. Wayland: `org.gnome.Shell.GlobalShortcuts` portal. |

---

## 6. Hardcoded Windows Paths

| Windows Path | File | Linux Equivalent |
|---|---|---|
| `%PROGRAMDATA%\Epic\EpicGamesLauncher\Data\Manifests\` | `manifest.rs` | `/var/lib/Epic/EpicGamesLauncher/Data/Manifests/` (rare on Linux) |
| `%LOCALAPPDATA%\EpicGamesLauncher\Saved\` | `manifest.rs` | `~/.local/share/EpicGamesLauncher/Saved/` (v1, rare) |
| **`%LOCALAPPDATA%\com.epicgames.eglv2\`** | `manifest.rs` | **`~/.local/share/com.epicgames.eglv2/`** (v2 — see EGLv2 doc) |
| `Engine/Binaries/ThirdParty/EOS/Win64/` | `ScreamAPI.cpp` | `Engine/Binaries/ThirdParty/EOS/Linux/` |
| `EOSSDK-Win64-Shipping.dll` | `Constants.h` | `libEOSSDK-Linux-Shipping.so` |
| `\\.\pipe\EpicGUI` | `pipe_protocol.rs`, `pipe_protocol.h` | `/tmp/epicgui.sock` (or `$XDG_RUNTIME_DIR/epicgui.sock`) |
| `ScreamAPI.log` | Various | Same (portable) |

**Important**: the v2 launcher installs its own `libEOSSDK-Linux-Shipping.so` at `/usr/lib/epic-games-launcher/libEOSSDK-Linux-Shipping.so` (confirmed by extracting the .deb — see EGLv2 doc §13). Games that depend on the launcher's EOS SDK will look here; this is a known good location for the proxy-replacement approach.

For the Unix socket, prefer `$XDG_RUNTIME_DIR` (typically `/run/user/<uid>/`) over `/tmp/` — it's per-user, world-writable by default, and avoids the `/tmp` sticky-bit complications. Fall back to `/tmp/` if `XDG_RUNTIME_DIR` is unset (some bare SSH sessions don't set it).

---

## 7. Injection Mechanism: Windows vs Linux

This is the most fundamental architectural difference. Linux has **two viable approaches**; the recommended one is the .so proxy (direct port of the Windows DLL proxy pattern).

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

### Linux — Approach A: `.so` Proxy (recommended, mirrors Windows)

```
Game launches
        ↓
Loader resolves libEOSSDK-Linux-Shipping.so
        ↓ (but finds ScreamAPI's .so renamed to that name)
__attribute__((constructor)) runs at .so load time
        ↓
dlopen("libEOSSDK-Linux-Shipping_o.so", RTLD_NOW)  // get real SDK
        ↓
Hook EOS_Achievements_* via LD_PRELOAD interposition OR funchook (see §9)
        ↓
Start Unix socket server on $XDG_RUNTIME_DIR/epicgui.sock
        ↓
EpicGUI connects to Unix socket
```

**Why this mirrors Windows:** Same proxy pattern, just `.so` instead of `.dll`. Renaming `libEOSSDK-Linux-Shipping.so` → `libEOSSDK-Linux-Shipping_o.so` and dropping ScreamAPI's `.so` in its place works identically to the Windows flow. No `LinkerExports` machinery needed — `dlsym` handles symbol resolution directly.

### Linux — Approach B: `LD_PRELOAD` (alternative, simpler)

```
LD_PRELOAD=./libScreamAPI.so ./GameExecutable
        ↓
Loader preloads libScreamAPI.so before any other library
        ↓
ScreamAPI's exported EOS_* symbols take precedence in global scope
        ↓
Game's EOS_* calls route to ScreamAPI's implementations automatically
        ↓
ScreamAPI's implementations forward to real libEOSSDK via dlopen/dlsym
        ↓
__attribute__((constructor)) starts Unix socket server
        ↓
EpicGUI connects
```

**Why this is simpler:** Zero file renaming. The user just sets `LD_PRELOAD` and launches the game. The dynamic linker handles interposition for free — no hooking library needed for cross-library calls.

**The catch:** `LD_PRELOAD` is set per-process via env var. Either:
- The user runs `LD_PRELOAD=/path/to/libScreamAPI.so ./game` manually (annoying)
- EpicGUI launches the game for them with the env var set (cleaner, but EpicGUI isn't currently a game launcher)
- A wrapper script is installed alongside the game

### Recommendation: Approach A for v1

The `.so` proxy is the recommended path because:
1. **Same architecture as Windows** — code changes are localized to OS API mappings (`dlopen`/`dlsym` vs `LoadLibrary`/`GetProcAddress`), not architectural rework.
2. **No user-facing env var** — just drop files in the game directory, like Windows.
3. **No `LD_PRELOAD` caveats** — `LD_PRELOAD` is ignored for setuid binaries, can be filtered by `/etc/ld.so.preload` policies, etc.
4. **Same flow as macOS** — if macOS port is ever attempted later, the dylib proxy pattern is the direct equivalent.

Reserve `LD_PRELOAD` as a fallback for games where the proxy-rename approach doesn't work (e.g. games that hardcode a non-standard EOS SDK path that the user can't write to).

---

## 8. Overlay: Vulkan Layer Approach

This is the single biggest reason Linux is easier than macOS. The Vulkan loader supports a layer mechanism that was **purpose-built for this use case** — intercepting Vulkan calls before they reach the driver.

### How Vulkan Layers Work

1. A layer is a `.so` file that exports `vkGetInstanceProcAddr` and `vkGetDeviceProcAddr`.
2. A JSON manifest file describes the layer (name, description, functions, environment variables).
3. The Vulkan loader discovers layers via:
   - `VK_INSTANCE_LAYERS` / `VK_DEVICE_LAYERS` env vars
   - `VK_LAYER_PATH` env var (custom layer search path)
   - System paths: `/usr/share/vulkan/implicit_layer.d/` (implicit, always-on)
   - User paths: `~/.local/share/vulkan/implicit_layer.d/`
4. When the loader initializes Vulkan, it walks the active layer chain and calls each layer's `vkGetInstanceProcAddr` to interpose.
5. The layer intercepts calls by returning its own function pointer instead of the next layer's.

Reference implementations to study:
- **MangoHud** — overlay with FPS, frame time, CPU/GPU stats. Uses Vulkan layer + OpenGL hook.
- **Steam overlay** — Valve's in-house overlay, similar architecture.
- **RenderDoc** — graphics debugging, hooks every Vulkan call.

### Concrete Overlay Port Plan

Replace these Windows-specific files:

| File | Windows Tech | Linux Replacement |
|---|---|---|
| `d3d12hook.cpp` | DirectX 12 + kiero | Vulkan layer (`vulkan_layer.cpp`) |
| `imgui_impl_dx11.cpp` | ImGui DX11 backend | `imgui_impl_vulkan.cpp` (ships with ImGui) |
| `imgui_impl_dx12.cpp` | ImGui DX12 backend | `imgui_impl_vulkan.cpp` |
| `kiero.cpp` | DX11/DX12 hooking | Removed — Vulkan layer replaces this entirely |
| `HotkeyHandler.cpp` (Win32 part) | `RegisterHotKey` | X11: `XGrabKey`. Wayland: portal. |

The Vulkan layer intercepts `vkQueuePresentKHR` (the Vulkan equivalent of `IDXGISwapChain::Present`). On each present:
1. Render ImGui draw data to a texture
2. Composite over the game's swapchain image before present
3. Call the original `vkQueuePresentKHR`

### Input Handling: X11 vs Wayland

| Compositor | Global Hotkeys | Cursor Clipping | Window Class |
|---|---|---|---|
| **X11 / XWayland** | `XGrabKey` (mature, well-documented) | `XGrabPointer` / `XWarpPointer` | `XGetInputFocus` |
| **Wayland (GNOME)** | `org.gnome.Shell.GlobalShortcuts` portal | `zwp_relative_pointer_v1` + `zwp_pointer_constraints_v1` | `org.freedesktop.portal.ScreenKeyboard` |
| **Wayland (KDE Plasma 6+)** | Same portal | Same protocols | Same |
| **Wayland (wlroots-based)** | `ext-idle-notification-v1` (limited) | `zwp_relative_pointer_v1` | Variable |

**Recommendation for v1:** support X11 only. Most games run via XWayland anyway (the Vulkan layer sees XWayland's window). Native Wayland games can be supported in v2 via the portal API. X11-only is the pragmatic 80% solution.

### Vulkan Layer vs Metal Rewrite: Effort Comparison

| | macOS Metal | Linux Vulkan |
|---|---|---|
| New backend code | ~3000 lines (Metal + ImGui backend) | ~1500 lines (Vulkan layer + ImGui backend) |
| Reference implementations | Few (mostly proprietary) | Many open-source (MangoHud, RenderDoc, etc.) |
| Hook mechanism | Metal doesn't have one — must hook via DYLD interposition of `MTLCreateSystemDefaultDevice` | Built into Vulkan spec — `vkGetInstanceProcAddr` chain |
| Driver compatibility | Must test against AMD, Intel, Nvidia on Mac | Must test against AMD (RADV/AMDVLK), Intel (ANV), Nvidia (proprietary) |
| Estimated effort | 2-3 months | 2 weeks |

---

## 9. Hooking Strategy — funchook Archived, What Now?

The user found that **kubo/funchook was archived on Sep 28, 2025**. This is a real concern but **not a blocker**, because Linux has a tiered set of hooking mechanisms — and for ScreamAPI's specific needs, the simplest tier is sufficient.

### 9.1 The Three Hooking Tiers on Linux

| Tier | Mechanism | When It Works | Code Cost | Library Needed |
|---|---|---|---|---|
| **1 (simplest)** | `.so` proxy + `LD_PRELOAD` symbol interposition | Cross-library calls via PLT (game→EOS SDK) | Zero — just `__attribute__((visibility("default")))` on hook functions | None |
| **2** | PLT/GOT rewriting at runtime | Cross-library calls; can be installed/uninstalled at runtime | ~100 lines | `kubo/plthook` or hand-rolled |
| **3 (most powerful)** | Inline hooking (patch function prologue with jump) | Any function including internal calls | ~50 lines per hook | funchook / subhook / Dobby |

**For ScreamAPI's use case** (hooking `EOS_Achievements_*`, `EOS_Ecom_*`, etc.), **Tier 1 is sufficient**. All EOS SDK calls from the game go through the PLT — they're cross-library by definition. Internal EOS SDK calls (SDK calling its own functions) are not ScreamAPI's concern.

### 9.2 Tier 1 in Detail: How Interposition Works

When the dynamic linker resolves a function call like `EOS_Achievements_QueryPlayerAchievements(...)`, it walks the link order:

1. The executable itself
2. Libraries listed in `LD_PRELOAD` (in order)
3. Libraries listed in `DT_NEEDED` (the executable's direct dependencies)
4. System libraries

The first library to export a symbol wins. So if `libScreamAPI.so` is in `LD_PRELOAD` and exports `EOS_Achievements_QueryPlayerAchievements`, all calls to that function (from the game or from any other library) route to ScreamAPI's version.

ScreamAPI's version then:
1. Inspects the arguments
2. Modifies them if needed (e.g. fake ownership of DLC items)
3. Forwards to the real SDK via `dlsym(real_sdk_handle, "EOS_Achievements_QueryPlayerAchievements")`
4. Inspects/modifies the result
5. Returns to the caller

**Zero hooking library needed.** Just C++ functions with the right names, declared `extern "C"` and `__attribute__((visibility("default")))`.

This is the same model ScreamAPI's Windows DLL proxy uses — it's just that on Windows, the proxy mechanism is `LinkerExports` forwarding, while on Linux it's `LD_PRELOAD` interposition. Same logical design, different mechanism.

### 9.3 Inline Hooking Library Comparison (for Tier 3, if ever needed)

If ScreamAPI ever needs to hook a function that **doesn't** go through the PLT (e.g. an internal EOS SDK call), Tier 1 doesn't work and we need an inline hooking library. Options:

| Library | License | Status | Linux x86_64 | Linux arm64 | macOS | Windows | Notes |
|---|---|---|---|---|---|---|---|
| **kubo/funchook** | GPLv2+ with linking exception | **Archived Sep 2025** | ✓ | ✓ | ✓ (limited) | ✓ | Most mature. Linking exception allows use in non-GPL projects. No new updates but no known critical bugs. |
| **Zeex/subhook** | BSD-2-Clause | Verify before use | ✓ | ✗ | ✓ | ✓ | Single-header, very minimal. No arm64. |
| **jmpews/Dobby** | Apache-2.0 | Active (verify) | ✓ | ✓ | ✓ | ✓ | Most features, supports ARM64 well, used in RE/cheat communities. Heavier dependency. |
| **kubo/plthook** | GPLv2+ | Same author as funchook, likely archived too | ✓ | ✓ | ✓ | ✓ | PLT/GOT only — Tier 2, not Tier 3. |
| **Hand-rolled** | N/A | You own it | ✓ | ✓ | N/A | N/A | ~200 lines for x86_64. Only worth it if you need one or two hooks. |

**About the AlanFokCo/injector repo** the user found: it's a fork of `kubo/injector`, which is **a ptrace-based .so injector** — NOT a hooking library. It loads a `.so` into a running process and calls functions in it. Useful as an alternative to `LD_PRELOAD` for the injection half (if you want to inject into an already-running game without restarting it), but it does not replace funchook. Worth keeping in mind for an "inject into running process" feature later, but irrelevant for v1.

### 9.4 License Compatibility Note

ScreamAPI is currently MIT-licensed. funchook is GPLv2+ **with a linking exception** that explicitly permits use in non-GPL projects. So even if you choose funchook, the MIT license of ScreamAPI is preserved — you just need to:
1. Include funchook's license notice in the distribution
2. Not modify funchook itself (or release modifications under GPLv2+)
3. Document the funchook dependency in `LICENSES/` or equivalent

subhook (BSD-2) and Dobby (Apache-2.0) are permissively licensed — no concerns.

### 9.5 Recommendation for v1

**Use Tier 1 (LD_PRELOAD interposition + .so proxy) for v1. No inline hooking library.** Reasons:

1. **ScreamAPI's hooks are all on public EOS API entry points** — these always go through the PLT.
2. **Zero new dependencies** — no library to vendor, no license to worry about, no archived repo to fork.
3. **Same logical architecture as Windows** — minimal cognitive overhead when maintaining both platforms.
4. **If a future hook needs Tier 3** (e.g. to intercept an internal EOS SDK call), funchook's archived status is still not a blocker:
   - funchook v1.1.3 (Jun 2023) is stable and works. "Archived" means no new updates, not "broken".
   - The linking exception license is permanent — you can keep using it indefinitely.
   - If a critical bug emerges, fork it — the codebase is ~5000 lines, maintainable.
   - Or switch to Dobby (Apache-2.0, active) — the API is different but conceptually similar.

**Concrete decision:** start v1 with Tier 1 only. If Tier 3 is needed later, evaluate funchook (archived but functional) vs Dobby (active) at that time. Don't pre-commit.

---

## 10. CI Worker — GitHub Actions Design

The user wants a CI worker to build the Linux port. This is straightforward — GitHub Actions has `ubuntu-22.04` runners that match the v2 launcher's supported distros.

### 10.1 Workflow Overview

```yaml
# .github/workflows/build-linux.yml
name: Build Linux

on:
  push:
    branches: [main, linux-port]
    tags: ['v*']
  pull_request:
    branches: [main]
  workflow_dispatch:

jobs:
  build-epicgui:
    runs-on: ubuntu-22.04
    steps:
      - uses: actions/checkout@v4
      - uses: actions/setup-node@v4
        with: { node-version: '20' }
      - uses: dtolnay/rust-toolchain@stable
      - uses: swatinem/rust-cache@v2
      - name: Install Tauri system deps
        run: |
          sudo apt-get update
          sudo apt-get install -y \
            libwebkit2gtk-4.1-dev libgtk-3-dev \
            libayatana-appindicator3-dev librsvg2-dev \
            libssl-dev
      - name: Build EpicGUI
        working-directory: EpicGUI
        run: |
          npm ci
          npm run tauri build
      - uses: actions/upload-artifact@v4
        with:
          name: EpicGUI-linux
          path: |
            EpicGUI/src-tauri/target/release/bundle/deb/*.deb
            EpicGUI/src-tauri/target/release/bundle/appimage/*.AppImage

  build-screamapi:
    runs-on: ubuntu-22.04
    steps:
      - uses: actions/checkout@v4
      - name: Install build deps
        run: |
          sudo apt-get update
          sudo apt-get install -y build-essential cmake ninja-build
      - name: Configure
        run: cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
      - name: Build
        run: cmake --build build --parallel
      - uses: actions/upload-artifact@v4
        with:
          name: ScreamAPI-linux
          path: build/libScreamAPI.so

  build-overlay:
    runs-on: ubuntu-22.04
    steps:
      - uses: actions/checkout@v4
      - name: Install Vulkan deps
        run: |
          sudo apt-get update
          sudo apt-get install -y \
            build-essential cmake ninja-build \
            libvulkan-dev vulkan-validationlayers-dev \
            libx11-dev libxi-dev libxxf86vm-dev libxrandr-dev
      - name: Configure
        run: cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
      - name: Build
        run: cmake --build build --parallel
      - uses: actions/upload-artifact@v4
        with:
          name: Overlay-linux
          path: |
            build/libEpicOverlay.so
            build/EpicOverlay.json

  release:
    needs: [build-epicgui, build-screamapi, build-overlay]
    if: startsWith(github.ref, 'refs/tags/v')
    runs-on: ubuntu-22.04
    steps:
      - uses: actions/download-artifact@v4
      - name: Stage release bundle
        run: |
          mkdir -p EpicUnlocker-Linux
          cp EpicGUI-linux/*.deb EpicGUI-linux/*.AppImage EpicUnlocker-Linux/
          cp ScreamAPI-linux/libScreamAPI.so EpicUnlocker-Linux/
          cp -r Overlay-linux/* EpicUnlocker-Linux/
          tar czf EpicUnlocker-Linux.tar.gz EpicUnlocker-Linux
      - uses: softprops/action-gh-release@v2
        with:
          files: EpicUnlocker-Linux.tar.gz
          draft: true
```

### 10.2 CI Considerations

| Concern | Solution |
|---|---|
| **Distro compatibility** | Build on `ubuntu-22.04` (oldest LTS the v2 launcher supports). The .so and .deb will work on newer distros (Fedora 43, Debian 12) via glibc backward compat. |
| **AppImage portability** | AppImage bundles its own libraries, works on any distro with FUSE. Use `linuxdeploy` plugin in Tauri. |
| **Vulkan layer packaging** | The JSON manifest + `.so` must be installed to `~/.local/share/vulkan/implicit_layer.d/` (user) or `/usr/share/vulkan/implicit_layer.d/` (system). The Tauri installer should handle this. |
| **Caching** | Use `swatinem/rust-cache@v2` for Cargo, `actions/cache@v4` for CMake build dirs. Cuts rebuild time from ~10 min to ~2 min on cached runs. |
| **Cross-compilation** | Not needed for v1 — x86_64 only. Steam Deck is x86_64. ARM64 (Raspberry Pi 5, Asahi Linux) can be added later via QEMU cross-compile job. |
| **Testing** | Smoke test: load the .so in a dummy process, verify it exports the expected symbols. Full integration test requires a real EOS game (manual). |
| **Security** | Pin Action versions to SHA (`@v4` → `@<sha>`). Use `softprops/action-gh-release`'s draft mode — never auto-publish a release. |

### 10.3 Matrix Build (future, when EAC-Linux ships)

Once Fortnite or another EAC-protected game ships on Linux, expand the matrix to test against multiple distros:

```yaml
strategy:
  matrix:
    distro: [ubuntu-22.04, ubuntu-24.04]
    # Fedora and Debian would need container-based runners
```

For v1, single-distro is fine.

---

## 11. Phased Implementation Plan

Concrete week-by-week plan assuming the v2 launcher has stabilized (post-Preview).

### Phase 1: EpicGUI Linux Port (1 week)

**Goal:** EpicGUI compiles and runs on Linux in "disconnected" mode (no socket connection yet).

| Step | Task | Effort |
|------|------|--------|
| 1.1 | Create `linux_impl.rs` with `tokio::net::UnixStream` socket client | 2 days |
| 1.2 | Update `lib.rs` cfg gates: add `#[cfg(target_os = "linux")] mod linux_impl;` | 1 hour |
| 1.3 | Update `pipe_client.rs` cfg: Linux uses `linux_impl::PipeClient` | 1 hour |
| 1.4 | Add `EPIC_PIPE_NAME` cfg gate in `pipe_protocol.rs` → `$XDG_RUNTIME_DIR/epicgui.sock` (fallback `/tmp/`) | 2 hours |
| 1.5 | Implement `open_with_default_app_linux()` via `Command::new("xdg-open")` | 2 hours |
| 1.6 | Ungate `read_log_incremental` — remove `#[cfg(target_os = "windows")]` (it's portable `std::fs`) | 1 hour |
| 1.7 | Ungate `clear_log` similarly | 30 min |
| 1.8 | Add Linux v1 paths to `manifest_dirs()` (rare, but for completeness) | 1 hour |
| 1.9 | **Add Linux v2 paths to `manifest_dirs()`** — see EGLv2 doc §3.1 | 1 hour |
| 1.10 | Implement `scan_v2_installations_jsonl()` — see EGLv2 doc §5.3 | 1 day |
| 1.11 | Update `tauri.conf.json`: add `"deb"`, `"appimage"` targets, Linux icon | 1 hour |
| 1.12 | Test `npm run tauri dev` on Linux | 1 day |
| **Total** | | **~5–7 days** |

### Phase 2: CI Worker (2–3 days, can run in parallel with Phase 1)

**Goal:** Push to `linux-port` branch produces downloadable artifacts.

| Step | Task | Effort |
|------|------|--------|
| 2.1 | Create `.github/workflows/build-linux.yml` (EpicGUI build only initially) | 4 hours |
| 2.2 | Add ScreamAPI build job once Phase 3 starts | 2 hours |
| 2.3 | Add Overlay build job once Phase 4 starts | 2 hours |
| 2.4 | Add release workflow (tag-triggered) | 2 hours |
| 2.5 | Test end-to-end on a PR | 4 hours |
| **Total** | | **~2 days** |

### Phase 3: Epic Unlocker Linux Port — .so Proxy (1 week)

**Goal:** Epic Unlocker as `.so`, dropped into a game directory as `libEOSSDK-Linux-Shipping.so`, forwards to the real SDK and serves the Unix socket.

| Step | Task | Effort |
|------|------|--------|
| 3.1 | Create `CMakeLists.txt` build system for Epic Unlocker on Linux | 1 day |
| 3.2 | Rewrite `PipeServer.cpp` for Unix domain socket (server side) | 2 days |
| 3.3 | Rewrite `dllmain.cpp` → `__attribute__((constructor))` / `__attribute__((destructor))` | 4 hours |
| 3.4 | Replace `HMODULE`/`LoadLibrary`/`FreeLibrary` → `void*`/`dlopen`/`dlclose` in `ScreamAPI.h/cpp` | 1 day |
| 3.5 | Replace `GetProcAddress` → `dlsym` in `ScreamAPI.h` and `eos_compat.cpp` | 4 hours |
| 3.6 | Replace `GetModuleFileName` → `dladdr` in `util.cpp` | 2 hours |
| 3.7 | Implement hooks via Tier 1 (LD_PRELOAD interposition) — no funchook needed | 2 days |
| 3.8 | Replace `LinkerExports` → no-op (LD_PRELOAD interposition handles it) | 2 hours |
| 3.9 | Test with a real EOS Linux game (Rocket League is the easy first target) | 2 days |
| **Total** | | **~7–10 days** |

### Phase 4: Overlay Vulkan Layer (2 weeks)

**Goal:** In-game overlay on Linux via Vulkan layer.

| Step | Task | Effort |
|------|------|--------|
| 4.1 | Study MangoHud source for Vulkan layer skeleton | 1 day |
| 4.2 | Create `vulkan_layer.cpp` implementing `vkGetInstanceProcAddr` chain | 2 days |
| 4.3 | Write `EpicOverlay.json` layer manifest | 2 hours |
| 4.4 | Replace `imgui_impl_dx11`/`dx12` with `imgui_impl_vulkan` | 2 days |
| 4.5 | Implement ImGui rendering in `vkQueuePresentKHR` hook | 3 days |
| 4.6 | Implement X11 input handling (`XGrabKey`, `XQueryPointer`) | 2 days |
| 4.7 | Test with Rocket League (Vulkan native) | 1 day |
| 4.8 | Test with a Proton game (Vulkan via DXVK) — sanity check | 1 day |
| **Total** | | **~12 days** |

### Phase 5: Polish & Release (1 week)

| Step | Task | Effort |
|------|------|--------|
| 5.1 | Write `Docs/LINUX_INSTALL.md` for end users | 4 hours |
| 5.2 | Add `LD_PRELOAD` fallback path for games where proxy-rename doesn't work | 1 day |
| 5.3 | Test on Steam Deck (read-only filesystem workaround doc) | 1 day |
| 5.4 | Test on Fedora 43 (verify glibc compat) | 4 hours |
| 5.5 | Cut v1.0.0-linux tag → CI auto-publishes draft release | 1 hour |
| **Total** | | **~3 days** |

### Total Timeline

| Phase | Duration | Can Overlap With |
|---|---|---|
| 1. EpicGUI | 5–7 days | Phase 2 |
| 2. CI Worker | 2 days | Phase 1 |
| 3. ScreamAPI | 7–10 days | (after Phase 1) |
| 4. Overlay | 12 days | (after Phase 3) |
| 5. Polish | 3 days | (after Phase 4) |
| **Total** | **~4 weeks** | |

---

## 12. Trigger Event — When to Actually Start

The v2 launcher is in Preview. The Linux native game catalog is currently tiny (Rocket League is the main EOS-using native Linux game). Starting now means building against a moving target.

**Recommended trigger: wait for both of these:**

1. **v2 launcher stable release** (out of Preview). Epic's announcement says the Preview is "just the beginning" — stable release date TBD but typically 3–6 months after Preview. This ensures `installations.jsonl` schema and `.egstore/` layout are frozen.
2. **Fortnite ships on Linux**. Epic explicitly said this is coming alongside EAC-Linux work. Fortnite is the canonical EOS test case — once it runs natively on Linux, you have a real flagship title to test ScreamAPI against.

**What to do before the trigger (zero-regret work):**

These tasks pay off on Windows AND Linux, so do them now regardless of the Linux port decision:

| Task | Why it helps both platforms |
|---|---|
| Implement EGLv2 manifest support (see EGLv2 doc) | Works identically on Windows and Linux — just different paths |
| Make Rust-side `#[cfg(not(target_os = "windows"))]` stubs compile on Linux | `cargo check --target x86_64-unknown-linux-gnu` from any shell. Catches leaks. 1-day task. |
| Audit `Cargo.toml` for any Windows-only deps | `windows`, `windows-sys`, `winapi` crates should be behind `target_os = "windows"` cfg. |
| Audit `commands.rs` for any unconditional Win32 calls | Same — they should all be cfg-gated already, but verify. |

These four tasks get EpicGUI compiling on Linux in "disconnected" mode (no socket) — which is the first ~30% of Phase 1, done ahead of schedule.

---

## 13. Open Questions

Things still worth verifying before starting Phase 3 (ScreamAPI port):

1. **Does the EOS SDK Linux `.so` use lazy binding?** If yes, LD_PRELOAD interposition works for all calls. If it uses `-Wl,-z,now` (full RELRO + immediate binding), interposition still works but only at load time. Either way Tier 1 should be fine, but worth confirming with `readelf -d libEOSSDK-Linux-Shipping.so | grep BIND`.

2. **Does any EOS SDK function get called via `dlsym` from the game?** If the game does `dlsym(rtlsdk, "EOS_...")` instead of calling it directly, LD_PRELOAD interposition is bypassed (the game gets the real function pointer). For ScreamAPI's hooks to work in that case, you'd need Tier 2 (PLT/GOT rewrite of the game's PLT, not the SDK's) or Tier 3 (inline hook on the SDK function itself). Likely not an issue, but verify with `strace`/`ltrace` on a real game.

3. **Vulkan layer ordering with MangoHud.** Users running both will have layer ordering issues. Need to test that ScreamAPI's overlay and MangoHud's overlay can coexist (they should — different `vkQueuePresentKHR` chain links).

4. **Steam Deck's read-only filesystem.** SteamOS's root filesystem is read-only by default. Users need to `sudo steamos-readonly disable` before installing ScreamAPI system-wide. Doc this in `LINUX_INSTALL.md`.

5. **Proton/Wine interplay.** Proton games use the Windows ScreamAPI, not the Linux one. Need clear docs explaining which to use when. (See §14.)

6. **funchook fork landscape.** If you do end up needing Tier 3, check the funchook fork network graph for an active maintained fork before reusing the archived original. The `kubo/funchook` README explicitly directs users to do this.

---

## 14. Proton/Wine Interplay

A unique Linux consideration: most "Linux games" today are actually Windows games running through Proton/Wine. For these:

- The game's `EOSSDK-Win64-Shipping.dll` is loaded by Wine
- The existing **Windows ScreamAPI** already works in this case — it's just a Windows DLL
- The Linux port is **not needed** for Proton games

**Rule of thumb for users:**

| Game type | Which ScreamAPI to use |
|---|---|
| Native Linux game (rare — Rocket League, Fortnite-when-it-ships) | Linux port (this doc) |
| Windows game running via Proton/Wine | Existing Windows ScreamAPI |

The Tauri GUI should detect which case applies (check if the game's install directory contains `.exe` vs Linux ELF binaries) and guide the user to the right installer. This is a docs/UI concern, not a code architecture concern.

---

## 15. Appendix: How to Evaluate funchook Forks (if Tier 3 is needed later)

If a future hook needs inline hooking (Tier 3), and the original `kubo/funchook` is still archived, here's how to evaluate forks:

1. **Check the network graph** at `https://github.com/kubo/funchook/network` for active forks.
2. **For each candidate fork, check:**
   - Latest commit date (should be within the last 6 months)
   - Open vs closed issue ratio (should be < 0.2 — most issues get closed)
   - Whether it's just a one-off experiment or has continuous activity
   - Whether it's published to a package registry (crates.io for Rust, vcpkg/conan for C++)
   - Whether the fork author has maintained other reputable projects
3. **Test before committing:**
   - Build the fork on `ubuntu-22.04`
   - Run the fork's test suite (should pass)
   - Write a 50-line test program that hooks `printf` and verify it works
   - Stress-test by hooking 100 functions simultaneously
4. **License check:** ensure the fork hasn't changed the license from the original (GPLv2+ with linking exception). A license change would require re-evaluating compatibility with ScreamAPI's MIT license.

If no good fork exists, **Dobby (Apache-2.0)** is the recommended alternative — different API but conceptually equivalent, and actively maintained.

---

## 16. Appendix: Source Material

This doc was informed by:

- **Static analysis of the v2 Linux launcher `.deb`** — see `EGLv2_Manifest_Format.md` §13 for details. The .deb extracted cleanly to a 192 MB Tauri/Rust ELF binary plus `libEOSSDK-Linux-Shipping.so`. Confirmed the EOS SDK Linux build exists and is shipped with the launcher.
- **The existing `macOS_PORT_ANALYSIS.md`** — much of the Win32→POSIX API mapping table is shared between macOS and Linux. Where the two docs differ, this one is authoritative for Linux.
- **Vulkan Layer spec** — KhronosGroup/Vulkan-Loader documentation, plus source code of MangoHud and RenderDoc for reference implementations.
- **funchook status** — `kubo/funchook` README and archive notice, retrieved Oct 2026.
- **`AlanFokCo/injector`** — confirmed as a fork of `kubo/injector` (a ptrace-based .so injector), NOT a fork of `kubo/funchook`. These are different projects by the same author solving different problems.

Source artifacts retained at:
- `/home/z/my-project/download/EpicGamesLauncher-x86_64.deb` (Linux launcher, 78 MB)
- `/home/z/my-project/download/EpicGamesLauncherSetup-x86_64.exe` (Windows launcher, 206 MB)
- `/home/z/my-project/download/Epic-Unlocker/` (client repo clone)

---

**Document version:** 1.0 — 2026-10-17
**Status:** Analysis complete. Implementation deferred until v2 launcher stable + Fortnite Linux release.
**Companion docs:**
- [`macOS_PORT_ANALYSIS.md`](./macOS_PORT_ANALYSIS.md) — for comparison and shared concerns
- [`EGLv2_Manifest_Format.md`](./EGLv2_Manifest_Format.md) — for v2 manifest path details on Linux
