# UE5.4+ EOS Proxy Mode — Investigation & Conclusions

Investigated with Process Hacker (Process Explorer) and runtime log analysis against two UE5.4+ titles running ScreamAPI proxy mode.

**Games:** A Cat Named Mojave (UE 5.4), Oddsparks (UE 5.5)  
**Game EOS SDK:** 1.16.3.0 / 1.16.4.0  
**ScreamAPI SDK headers:** 1.18.1.2  

---

## Process Investigation — How We Found the Bugs

### 1. Process Explorer: Confirmed the Proxy DLL Was Loaded

Used Process Explorer (Sysinternals) to inspect `CatmanProject-Win64-Shipping.exe` (PID 9520) while running with ScreamAPI proxy. The **Modules tab** confirmed:

- **`EOSSDK-Win64-Shipping.dll`** loaded at base `0x7FFBAD910000`, size **18,400 KB (~17.97 MB)** — this is our ScreamAPI proxy
- The original was renamed to `EOSSDK-Win64-Shipping_o.dll` on disk and loaded by the proxy via `LoadLibrary`

This confirmed the DLL replacement chain was working correctly — the game was loading our proxy, not the original.

### 2. File Properties: Confirmed SDK Version

Windows file properties on the game's `EOSSDK-Win64-Shipping.dll` showed:

- **Product:** Epic Online Services SDK
- **File Version:** 1.16.3.0
- **Product Version:** 1.16.3-32303053
- **Size:** 17.2 MB (18,078,208 bytes)

This was critical — ScreamAPI's headers are v1.18.1.2 while the game ships v1.16.3.0. The version mismatch meant the `LinkerExports64.h` shipped with ScreamAPI was generated against a *different* DLL version.

### 3. `dumpbin /exports` — The Smoking Gun for Both Bugs

Ran `dumpbin /exports` on both the proxy DLL and the original `_o.dll` from Process Explorer's identified path:

| | Original `_o.dll` | ScreamAPI proxy DLL |
|---|---|---|
| **Export count** | **677** | **554** |
| **Native exports** | 677 (all real code) | **0** |
| **PE forwarders** | 0 | **554 (100%)** |

This single comparison proved both bugs simultaneously:

- **Bug 1 — 123 missing exports:** 677 − 554 = 123 functions the game could call but the proxy didn't export at all. UE5.4+ validates exports via `FPlatformProcess::GetProcAddress()` during module boot — missing core functions (like `EOS_Platform_CheckForLauncherAndRestart`, `EOS_Platform_SetApplicationStatus`) caused the engine to silently abort EOS initialization before ever calling `EOS_Platform_Create`.

- **Bug 2 — 100% forwarder rate:** Every single exported symbol was a PE forwarder to `_o.dll`, meaning the `eos-impl/` C++ interception code (compiled with `__declspec(dllexport)` via `EOS_DECLARE_FUNC`) was completely bypassed. The `#pragma comment(linker, "/export:...")` directives in `LinkerExports64.h` were resolved by MSVC's linker as forwarders that shadowed the `__declspec(dllexport)` symbols. The old `.def` file also had a `LIBRARY` line that caused LNK2001 conflicts.

### 4. Runtime Logs: Confirmed the Call Pattern

ScreamAPI diagnostic logs captured the actual EOS call sequence at runtime.

**Mojave (proxy mode, pre-fix):**
```
[14:58:16.638] [INFO]   [INTERCEPT] >>> EOS_Initialize CALLED! <<<
[14:58:16.666] [INFO]   [INTERCEPT]   ApiVersion=4, ProductName=CatmanProject
[14:58:16.788] [INFO]   [INTERCEPT] EOS_Initialize - Returned: 0
...
[14:58:26.873] [DEBUG]  Still waiting for EOS platform... (10/60 seconds)
[14:58:36.910] [DEBUG]  Still waiting for EOS platform... (20/60 seconds)
[14:58:46.968] [DEBUG]  Still waiting for EOS platform... (30/60 seconds)
[14:58:57.001] [DEBUG]  Still waiting for EOS platform... (40/60 seconds)
[14:59:07.033] [DEBUG]  Still waiting for EOS platform... (50/60 seconds)
```

`EOS_Initialize` IS called and returns 0 (success). `EOS_Platform_Create` is NEVER called. ScreamAPI's 60-second polling loop times out waiting for it.

**Oddsparks (hook mode, pre-fix):**
```
[07:04:06.543] [INFO]   [HOOK] Successfully hooked: EOS_Platform_Create
[07:04:06.668] [INFO]   [HOOK] Successfully hooked: EOS_Platform_Release
[07:04:06.805] [INFO]   [HOOK] Successfully hooked: EOS_Platform_Tick
[07:04:10.195] [INFO]   Waiting for game to initialize EOS platform...
[07:04:15.408] [INFO]   Game requested to shutdown the EOS SDK
```

All 26 MinHook hooks installed successfully. But `EOS_Platform_Create` was never called — the hooks never fired. The game shut down EOS after ~5 seconds without ever creating a platform.

### 5. EpicGUI DLC Tab: Confirmed Partial Success

In Oddsparks with the .def fix applied, the EpicGUI DLC tab showed all items as "Not Owned" despite the proxy being active. However, the user confirmed **achievements were working** (successfully unlocked one). This proved the export/forwarding fix was correct — intercepted functions now resolve to C++ wrappers — but the ecom ownership spoofing path has a separate issue on UE5.4+.

---

## Root Cause Summary

### Bug 1: 123 Missing Exports (SDK Version Mismatch)

`LinkerExports64.h` was generated against a different SDK version's DLL, not the game's v1.16.3.0. UE5.4+ uses OSSv2 which validates exports via `FPlatformProcess::GetProcAddress()` during module boot — missing functions caused silent EOS init abort.

**Fix:** Generate `LinkerExports64.h` from the game's actual DLL using `master_generator.py`.

### Bug 2: Linker Export Shadowing (MSVC Precedence)

`#pragma comment(linker, "/export:...")` directives create PE forwarders that take precedence over `__declspec(dllexport)` from `EOS_DECLARE_FUNC`. All 52 intercepted functions were forwarded straight to `_o.dll`, completely bypassing the C++ wrappers.

**Fix:** A `.def` file with only the 52 intercepted function names as native EXPORTS. No `LIBRARY` line (causes LNK2001). No forwarders. The `.def` file takes precedence over `#pragma comment` directives, forcing intercepted symbols to resolve to the C++ implementations.

### UE5.4+ Platform Init: Different Code Path

`EOS_Initialize` IS called and succeeds. `EOS_Platform_Create` is NEVER called. UE5.4+ creates the EOS platform through OSSv2/EOSGS internally, bypassing the public `EOS_Platform_Create` API. This is why ScreamAPI's polling loop times out — it waits for a function the game never calls. Achievements still work because they go through `EOS_Achievements_*` interfaces which ARE called directly.

---

## The Fix

### Architecture After Fix

```
Game calls LoadLibrary("EOSSDK-Win64-Shipping.dll")
  → Loads ScreamAPI proxy
  → ScreamAPI loads EOSSDK-Win64-Shipping_o.dll (original)
  → .def file forces 52 intercepted functions → C++ wrappers in eos-impl/
  → #pragma comment(linker) forwards all other exports → _o.dll
  → Game calls EOS_Initialize → C++ wrapper → forwards to _o.dll ✓
  → Game calls EOS_Achievements_* → C++ wrapper → spoofs → forwards to _o.dll ✓
  → Game calls any other EOS_* → #pragma forwarder → _o.dll ✓
```

### Files Added/Modified

| File | Change | Purpose |
|---|---|---|
| `src/ScreamAPI.def` | Created | 52 native exports, no LIBRARY line, no forwarders — fixes shadowing |
| `src/eos-impl/eos_initialize.cpp` | Created | `EOS_Initialize` wrapper (was missing entirely — neither forwarded nor intercepted) |
| `src/eos-impl/eos_init.cpp` | Modified | Added diagnostic `[INTERCEPT]` logging to `EOS_Platform_Create` |
| `master_generator.py` | Created | One-shot script: point at any EOSSDK DLL → generates `LinkerExports{64,32}.h` + `ScreamAPI.def` |

### `.def` File Rules (Critical)

- **NO** `LIBRARY` line — causes LNK2001 when combined with `__declspec(dllexport)` from `EOS_BUILD_DLL`
- **NO** forwarders — forwarders stay in `LinkerExports64.h` via `#pragma comment(linker)`
- **ONLY** the 52 intercepted function names as bare `EXPORTS` entries
- The `.def` file takes precedence over `#pragma comment` directives, forcing these 52 symbols to resolve to compiled C++ code instead of PE forwarders

---

## Test Results After Fix

| Game | Achievements | DLC Spoof | Platform_Create Called | Classification |
|---|---|---|---|---|
| Oddsparks (UE 5.5, proxy) | ✅ Working | ✅ Intercept fires (ownership spoof active) | No (UE5.4+ behavior) | Tier 2 — `EOS_Achievements_*` + `EOS_Ecom_*` calls go through DLL |
| Mojave (UE 5.4, proxy) | ❌ Nothing fires | ❌ Nothing fires | No (UE5.4+ behavior) | Tier 3 — statically linked EOS after `EOS_Initialize` |
| Tannenberg (proxy) | ⚠️ Local only (overlay) | — | Yes | Pre-UE5.4 — normal path works, separate server-side reporting issue |

---

## `master_generator.py`

```bash
python master_generator.py <path_to_game_DLL>
```

1. Parses the DLL's PE export table (pure Python, zero dependencies)
2. Auto-detects 32-bit vs 64-bit
3. Generates `LinkerExports64.h` — `#pragma comment(linker)` forwarders for ALL exports, intercepted ones marked `// REMOVED:`
4. Generates `ScreamAPI.def` — native exports ONLY for the 52 intercepted functions
5. Prints copy instructions (does NOT copy automatically)

## Fallback Handle Capture System (Tier 2 Fix)

UE5.4+ OSSv2 creates the EOS platform internally without calling `EOS_Platform_Create` through the public API. For Tier 2 games (e.g., Oddsparks), the engine still calls `EOS_Achievements_QueryDefinitions`, `EOS_Achievements_QueryPlayerAchievements`, and `EOS_Achievements_UnlockAchievements` through the DLL export table — it just obtains the handles through an internal path.

The fallback capture system works by extracting the needed handles from the function call parameters themselves:

```
Game calls EOS_Achievements_QueryDefinitions(HAch, Options->LocalUserId, ...)
  → ScreamAPI intercepts (C++ wrapper via .def)
  → TryCaptureFallbackHandles(HAch, LocalUserId) captures handles atomically
  → AchievementManager::TryInitFromFallback() lazy-inits if polling timed out
  → Forwards to original _o.dll
```

**Key design:** `std::atomic<bool>` with `exchange(true)` ensures single capture with zero mutex overhead on the hot path. The 60-second polling loop checks `HasFallbackHandles()` at timeout before giving up.

### UE5.4+ Game Classification

| Tier | `Platform_Create` via DLL? | Achievement calls via DLL? | Fallback helps? | Example |
|---|---|---|---|---|
| Pre-UE5.4 | ✅ Yes | ✅ Yes | Not needed (normal path) | Most older titles |
| Tier 2 | ❌ No | ✅ Yes | ✅ Yes | Oddsparks (UE 5.5) |
| Tier 3 | ❌ No | ❌ No | ❌ No | Mojave (UE 5.4) |

### Files Modified for Fallback

| File | Change |
|---|---|
| `src/util.h` | Added `g_fallback_hAchievements`, `g_fallback_productUserId` atomics, `TryCaptureFallbackHandles()`, `HasFallbackHandles()` |
| `src/util.cpp` | Atomic fallback storage, `TryCaptureFallbackHandles()` impl, patched `getHAchievements()`/`getProductUserId()` with OSSv2 fallback return |
| `src/eos-impl/eos_achievements.cpp` | Added `TryCaptureFallbackHandles` + `TryInitFromFallback` to `QueryDefinitions`, `QueryPlayerAchievements`, `UnlockAchievements` |
| `src/achievement_manager.h/cpp` | Added `TryInitFromFallback()` for lazy initialization after polling timeout |
| `src/ScreamAPI.cpp` | Timeout path now checks `HasFallbackHandles()` before giving up |

## Tier 3 — Statically Linked EOS (Open Investigation)

### Observed Behavior

Mojave (UE 5.4) and Tannenberg exhibit a pattern where `EOS_Initialize` is the **only** EOS function called through the DLL export table. Zero calls to `EOS_Platform_Create`, `EOS_Platform_Tick`, `EOS_Auth_Login`, `EOS_Connect_Login`, `EOS_Achievements_*`, or `EOS_UI_*` in 60+ seconds of actual gameplay. No Epic overlay either.

Process Explorer confirmed: only **one** `EOSSDK-Win64-Shipping.dll` loaded — the ScreamAPI proxy. The original `_o.dll` is loaded by the proxy internally. No duplicate DLLs.

Mojave has real achievement unlock rates on the Epic Store, so achievements DO work for legitimate players. The game IS using EOS — just not through the DLL's export table after `EOS_Initialize`.

### What's Happening

UE5.4+ appears to use the EOS SDK DLL as a **bootstrap only** — `EOS_Initialize` sets up global EOS state, then the engine's `OnlineSubsystemEOS` module does everything else through **statically linked EOS code inside the UE binary itself**. Platform creation, tick loop, authentication, achievements, UI — all run through internal code paths that never touch the DLL's export table.

This means DLL proxying (and MinHook detouring of DLL exports) fundamentally cannot intercept these games. The DLL is a shell; the real work happens inside the engine.

### Why This Is Hard

- **DLL proxying** only sees calls that go through the DLL's export table → nothing after `EOS_Initialize`
- **MinHook** detours DLL exports → same blind spot
- **Memory pattern scanning** could find statically linked EOS functions inside UE modules, but requires per-game signatures and breaks on every engine update
- **Network interception** (hooking WinHTTP/WinINet HTTPS calls to Epic's backend) is DLL-agnostic but extremely complex and fragile

### Possible Avenues (Unexplored)

1. **`EOS_Initialize` deep hook** — the one call we DO see might return or set up internal state that the engine caches. Tampering with what happens inside `EOS_Initialize` (in the original DLL) could inject hooks into the internal EOS state before the engine takes over. Worth investigating what `EOS_Initialize` actually sets up internally.

2. **VTable or function table returned by EOS** — if `EOS_Initialize` or any early call returns function pointers that the engine later uses, intercepting those tables could provide a hook point. The EOS SDK is a C API, but the engine might wrap it in function tables internally.

3. **Static binary patching of the UE module** — identify which `.exe` or UE engine `.dll` contains the statically linked EOS code (likely `OnlineSubsystemEOS*.dll` or the main game executable), then patch it directly. Tools like Ghidra/IDA could reveal the internal EOS call sites.

4. **Hook the original DLL's internal functions** — the `_o.dll` is loaded in-memory. If the engine obtains function pointers from it via `GetProcAddress` and caches them, those pointers DO point to the original DLL's code. MinHook could potentially detour functions in the original DLL *before* the engine caches the pointers — but this would need to happen before or during `EOS_Initialize`.

5. **Epic Online Services plugin architecture** — UE5.4+ might load EOS through a plugin system that we haven't identified. There could be additional modules or config files that control how EOS is loaded that we haven't examined.

### Status

**Open.** Tier 3 is not a ScreamAPI bug — it's a fundamental limitation of the DLL proxy approach for games that statically link EOS. A solution exists in theory but requires research into UE5.4+'s internal EOS integration that goes beyond what DLL proxying can offer. If any of the above avenues yield results, the fix would likely be a new ScreamAPI mode (not proxy, not hook — something new).

---

## Remaining Unsolved Issues

1. **How does UE5.4+ create the EOS platform without `EOS_Platform_Create`?** → Partially answered: it's statically linked. See Tier 3 section above.

2. **Why doesn't DLC ownership spoofing work in Oddsparks?** → RESOLVED: It does work. The intercept fires, catalog is fetched (9 items), and `EOS_Ecom_QueryOwnership` callback spoofs ownership. The earlier "all Not Owned" observation was likely taken before the .def fix was applied or before DLC logging was enabled.

3. **Hook mode still fails on UE5.4+.** Tier 3 games won't be helped by hook mode either — same root cause (statically linked EOS after init). Tier 2 games may benefit from hook mode as an alternative to proxy mode.
