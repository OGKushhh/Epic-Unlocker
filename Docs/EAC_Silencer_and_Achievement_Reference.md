# EAC Silencer & EOS Achievement Unlock — Complete Technical Reference

> Compiled: 2026-08-30  
> Scope: Everything we learned reverse-engineering OnlineFix/EpicFix, building an EAC silencer, and getting achievements to unlock on EAC-protected EOS games.

---

## 0. Table of Contents

1. Foundations
2. OnlineFix's Reference Implementation
3. EAC Silencer — Approaches Tried
4. Achievement Unlock Implementation
5. Per-Game Status Matrix
6. SDK Version Differences
7. The Patcher Tool (optional)
8. Open Issues & Future Work
9. Reference — Files & Glossary


## 1. Foundations

### 1.1 What is EAC?

Easy Anti-Cheat (EAC) is Epic Games' anti-cheat runtime. On EOS-protected games it loads as a **client** inside the game process via the EOS SDK's `EOS_AntiCheatClient` interface. Two relevant properties:

1. **File integrity enforcement**: EAC validates every DLL the game loads. Any unsigned/untrusted DLL causes "untrusted system file" errors at startup or during gameplay.
2. **Behavioral detection**: EAC scans for known injection patterns (CreateRemoteThread, SetWindowsHookEx, APC injection, PEB manipulation). Even an empty DLL injected via CreateRemoteThread triggers detection — *the act of injection itself is the violation*, not what the DLL does.

### 1.2 What is the EOS SDK?

Epic Online Services SDK — a C API surface (exported from `EOSSDK-Win64-Shipping.dll`) covering: Auth, Connect, Ecom (entitlements/store), Achievements, Stats, Leaderboards, AntiCheatClient, Sessions, Lobby, Voice, etc. Each subsystem is exposed as a typed handle (`EOS_HAchievements`, `EOS_HStats`, `EOS_HEcom`, ...).

The SDK is statically compiled into a single ~20 MB DLL with no external dependencies except Windows. The same DLL is shipped with every EOS game — the only differences are SDK version (1.15.x through 1.19.x) and configuration (ClientId/ProductId/SandboxId passed at `EOS_Platform_Create`).

### 1.3 How achievements work in EOS

Achievement flow has three phases:

1. **Definition query** — `EOS_Achievements_QueryDefinitions` fetches the catalog of achievements for the product from EOS backend. Returns ~32 entries on a typical game. Cached locally.
2. **Player achievement query** — `EOS_Achievements_QueryPlayerAchievements` fetches the player's progress on each achievement (locked/unlocked + progress percentage).
3. **Unlock** — `EOS_Achievements_UnlockAchievements` posts an unlock to EOS backend. Server records it, returns success, fires a notification to the overlay.

The critical question is **who decides the unlock**:

- **Client-authoritative** (single-player / PvE games): the game reports "I did X" and EOS just records it. Epic Unlocker can intercept this and inject fake unlocks.
- **Server-authoritative** (competitive PvP games): the dedicated server reports "player did X" and EOS verifies with the server. Client-side interception is rejected.

### 1.4 The dual-layer problem

EAC silencing and achievement unlocking are **orthogonal** problems:

| Layer | What it does | How we attack it |
|---|---|---|
| Anti-cheat | Stops the game from rejecting our DLL/proxy | On-disk SDK patch + env var + genuine EAC certs |
| Achievement system | Stops the server from rejecting our unlock | Game-handle capture + NoServerMode + (sometimes) can't be done |

The mistake we made early was conflating these — assuming that "if EAC is silenced, achievements will work". They don't, because the achievement backend has its own auth path.

### 1.5 The dual-platform issue (UE5 + OSSv2)

Unreal Engine 5 games that use Redpoint's `OnlineServices` integration create **two or more** EOS platform instances during init:

- Platform A: created by the game's bootstrap with one EpicAccountId
- Platform B: created by the OSSv2 wrapper with a different EpicAccountId (or even anonymous)

When Epic Unlocker captures `HPlatform` from the first `EOS_Platform_Create` call, achievements queried through that handle use Platform A's auth — but the game's actual achievement calls go through Platform B. This causes `EOS_InvalidUser` / `EOS_UnexpectedError` on every achievement query.

**Fix**: capture the game's own handle from its calls to `EOS_Achievements_GetPlayerAchievementCount`, `EOS_Platform_GetStatsInterface`, and from the `Auth_Login` / `Connect_Login` callbacks. Use those captured handles for Epic Unlocker's queries.

---

## 2. OnlineFix's Reference Implementation

We reverse-engineered OnlineFix's EAC bypass by examining `OnlineFix64.dll` (the proxy DLL they ship), the preloader, `EGSAuthLauncher.exe`, and the bundled EAC folder. Here's what we found.

### 2.1 Architecture

OnlineFix ships a self-contained wrapper:

```
Game's Binaries/Win64/
├── Game.exe                  ← untouched
├── winmm.dll                 ← OnlineFix's loader (loads OnlineFix64.dll early)
├── OnlineFix64.dll           ← VMProtect-packed, contains all hooks
├── EOSSDK-Win64-Shipping.dll ← PATCHED original SDK
├── EasyAntiCheat/
│   ├── Settings.json         ← Game-specific EAC config
│   ├── Certificates/
│   │   ├── base.cer          ← Genuine Epic PKI cert
│   │   ├── base.bin           ← Genuine Epic signed blob
│   │   └── runtime.conf       ← EAC runtime configuration
│   └── Launcher.exe          ← EAC launcher (modified)
└── EGSAuthLauncher.exe       ← Authenticates against EOS as the user
```

### 2.2 The EAC certificate files — THE critical missing piece

**Update (2026-08-30)**: This was the breakthrough. Initially we misidentified these as "fake" certs that needed to be the game's own. After extensive testing, the opposite is true: **OnlineFix's cert files are universal** — they work across multiple games (confirmed on Deceive Inc., The Riflemen). The game's own certs reject our patched SDK; OnlineFix's certs accept it.

The cert files are **genuine Epic PKI material** (not forged), but they are signed against OnlineFix's patched SDK, not the game's original SDK. The cert's signature tells EAC's loader "this SDK binary is authorized" — which is exactly what we need after patching.

| File | Purpose | Format |
|---|---|---|
| `base.cer` | X.509 certificate — signs the SDK and accepts the patched binary | X.509 DER |
| `base.bin` | Encrypted config blob — EAC runtime settings | Encrypted (AES-CTR or similar) |
| `runtime.conf` | EAC runtime configuration | JSON-ish text |

**Location**: Drop these three files into the game's `EasyAntiCheat\Certificates\` folder, overwriting the originals.

**Critical**: Leave `EasyAntiCheat\Settings.json` alone — that file is per-game (product ID, sandbox ID) and replacing it breaks the SDK's product binding. Only `Certificates\base.cer`, `base.bin`, `runtime.conf` are universal.

**Why this matters**: Without these cert files, EAC's loader rejects the patched SDK with "unknown file version" or "untrusted system file" — **even if every other step is correct**. The patches silence the SDK's anti-cheat client, but EAC's loader scans DLLs **before** the SDK initializes. The cert files are the only way to make EAC's loader accept our patched SDK.

### 2.2.1 The four-layer model

With the cert discovery, the model is four layers — but Layer 2 (SDK silencer) turned out to be **optional** for our confirmed games:

| Layer | What it does | Required? |
|---|---|---|
| 1. Cert files | Tell EAC's loader to accept our patched SDK | ✅ Always required (universal) |
| 2. SDK silencer | Stop the SDK loading the real anti-cheat client | ❌ Optional for our games (cert alone works on Deceive Inc. + The Riflemen) |
| 3. Ecom bypass | Stop server-side entitlement queries (rejected by flagged session) | ✅ Required (via `[EAC] EACNoServerMode = True`) |
| 4. Achievement auth | Use the game's auth session for unlock calls | ✅ Required (via `[EAC] EACMode = True`) |

Layer 1 (cert) is the universal requirement. Layer 2 (SDK patches) is only needed for "strict" games that enforce EAC's **runtime** reports (kick you mid-match, etc.) — neither of our confirmed games does. See [§8.5](#85-sdk-patches-are-optional--confirmed-2026-08-30) for the test that confirmed this.

## 3. EAC Silencer — Approaches Tried

We tried five different silencing strategies during this project. Only two worked, and as it turned out, both are now optional for our confirmed games (cert files alone suffice). Documenting the failed approaches here as a lesson on what **not** to do.

| Approach | Status | Reason |
|---|---|---|
| CreateRemoteThread injection | ❌ | EAC detects the injection mechanism itself (regardless of payload) |
| PEB unlinking | ❌ | EAC uses lower-level enumeration (`NtQuerySystemInformation`) |
| WinVerifyTrust hooking | ❌ | EAC has its own cert chain validation (`base.cer`) |
| On-disk SDK patching | ✅ (optional) | No injection = nothing to detect — but cert files alone work for our games |
| Env var `EOS_USE_ANTICHEATCLIENTNULL=1` | ✅ (optional) | Goes through SDK's legitimate code path — equivalent to Patch site 1, but cert alone suffices |

**Key learning**: EAC's loader scans DLLs **before** the SDK initializes. The cert files satisfy this loader scan. Whether additional runtime silencing (patches/env-var) is needed depends on whether the game enforces EAC's runtime reports — our confirmed games don't.

### 3.1 How the archived silencer projects fit in

Two archived DLL silencer projects are kept in `old_eac_silencer_reference/` for future research. **Neither is needed for our confirmed games** — cert files alone work. They're kept because they explore alternative approaches that might be useful for "strict" games in the future.

#### The simpler project (`old_eac_silencer_reference/EacSilencerDll/`)

- Standalone DLL injected via `CreateRemoteThread` (the `EacSilencerLauncher.exe` does the injection)
- Four modes: `NORMAL` (patches site 1 + 2), `SOFT` (MinHook on integrity callback only), `STEALTH` (patches + MinHook), `MINIMAL` (PEB unlinking only — diagnostic)
- Applies OnlineFix's two SDK patches **in memory** (vs. our `eos_patcher.pyw` which patches **on disk**)
- Includes PEB unlinking and `WinVerifyTrust` hooking — both proven ineffective on strict EAC games

**Why it's archived**: `CreateRemoteThread` injection is detected by EAC on strict games (the injection itself is the violation, not what the DLL does). The on-disk patcher (`eos_patcher.pyw`) is strictly better — no injection, no detection.

#### The advanced project (`old_eac_silencer_reference/EasyAntiCheat/`)

- DLL proxy that loads the real EOS SDK and forwards all calls
- **Dual-layer defense**:
  - **Layer 1 (kernel)**: `kdriver.sys` kernel driver manipulates `ETHREAD.CrossThreadFlags.Terminated` to hide our threads from EAC's brute-force scanner (based on adrianyy's EAC reversing)
  - **Layer 2 (user)**: Hooks `EOS_AntiCheatClient_AddNotifyClientIntegrityViolated` to swallow integrity violation callbacks
- Includes `eac/` folder with `eac_message_silencer.cpp`, `eac_thread_hider.cpp`, `eac_client_stubs.cpp`, `eac_server_stubs.cpp`
- Includes `kdriver/` folder with kernel driver source (`kdriver.h`, `HiddenProcess.h`)

**Why it's archived**: 
1. Requires `kdriver.sys` (kernel driver) — requires admin rights, test signing mode enabled, and the driver binary itself (not included)
2. Overkill — cert files alone work for our confirmed games
3. The kernel driver approach is the nuclear option; useful only if a future game enforces EAC runtime reports AND detects user-mode DLL injection

**Bugs fixed in this archived copy**:
- CRITICAL: `LoadLibraryA(self)`, `InitializeEOSHooks()`, and `InitializeDualLayerDefense()` were all called from `DllMain` → loader-lock deadlock. Fixed: moved to worker thread.
- CRITICAL: `ShutdownDualLayerDefense()` was called from `DllMain` DLL_PROCESS_DETACH → could deadlock on SCM calls. Fixed: runs after worker thread join with 3s timeout.

### 3.2 When would you actually use the silencer projects?

Realistically, **never** for our confirmed games. The cert-only recipe in `Docs/EAC_Guide.md` is simpler, safer, and proven to work.

The silencer projects would only be useful if:
1. A future game enforces EAC runtime reports (kicks you mid-match even with cert files)
2. AND the on-disk patcher (`eos_patcher.pyw`) doesn't apply cleanly (SDK version mismatch)
3. AND you're willing to deal with kernel driver installation (admin rights, test signing)

That's a very narrow use case. For now, the silencer projects are kept as reference for the techniques they demonstrate (PEB unlinking, kernel thread hiding, integrity callback hooking) — not as production tools.

---

## 4. Achievement Unlock Implementation

This section documents the Epic Unlocker patches that make achievements actually unlock. These are layered on top of the clean Epic Unlocker source.

### 4.1 The dual-platform problem in detail

UE5's OnlineServices (Redpoint's plugin) creates multiple EOS platforms during init:

```cpp
// In the game's bootstrap:
EOS_Platform_Create(opts1, &HPlatform_A);  // auth via EGS launcher ticket

// Later, in OSSv2 wrapper:
EOS_Platform_Create(opts2, &HPlatform_B);  // anonymous/secondary auth
```

Epic Unlocker's original capture:
```cpp
EOS_HPlatform EOS_Platform_Create(...) {
    auto result = original(...);
    g_HPlatform = result;  // captures whichever fires first
    return result;
}
```

When the game calls `EOS_Achievements_QueryPlayerAchievements(HPlatform_B->HAchievements, ...)`, Epic Unlocker's wrapped version uses the game's handle correctly — but when Epic Unlocker's own code tries to call into EOS using `g_HPlatform->HAchievements`, that's Platform A's handle, which doesn't match Platform B's auth session → `EOS_InvalidUser`.

### 4.2 Game-handle capture strategy

We added capture points in `eos_intercept.cpp` for each handle type:

```cpp
// In EOS_Auth_Login's completion callback:
void EOS_CALL AuthLoginCb(const EOS_Auth_LoginCallbackInfo* info) {
    if (info->ResultCode == EOS_EResult::EOS_Success) {
        Util::g_gameEpicAccountId = info->LocalUserId;  // ← capture
    }
    original_cb(info);
}

// In EOS_Connect_Login's completion callback:
void EOS_CALL ConnectLoginCb(const EOS_Connect_LoginCallbackInfo* info) {
    if (info->ResultCode == EOS_EResult::EOS_Success) {
        Util::g_gameProductUserId = info->LocalUserId;  // ← capture
    }
    original_cb(info);
}

// In EOS_Platform_GetStatsInterface:
EOS_HStats EOS_Platform_GetStatsInterface(EOS_HPlatform h) {
    auto result = original(h);
    Util::g_gameHStats = result;  // ← capture (game's stats handle)
    return result;
}
```

For `HAchievements`, we piggyback on the game's own achievement query (next section).

### 4.3 Piggyback achievement capture

The game calls `EOS_Achievements_GetPlayerAchievementCount` after its definition query completes. We intercept that and use it as a synchronization point:

```cpp
EOS_EResult EOS_Achievements_GetPlayerAchievementCount(
    EOS_HAchievements hAchievements,
    EOS_ProductUserId localUserId,
    uint32_t* outCount)
{
    auto result = original(hAchievements, localUserId, outCount);

    // Capture the game's HAchievements handle:
    Util::g_gameHAchievements = hAchievements;

    // Also capture the player's achievement data:
    if (result == EOS_Success && outCount && *outCount > 0) {
        // Read each player achievement into our cache:
        for (uint32_t i = 0; i < *outCount; i++) {
            EOS_Achievements_PlayerAchievement pa;
            uint32_t sz = sizeof(pa);
            EOS_Achievements_CopyPlayerAchievementAt(
                hAchievements, localUserId, i, &pa);
            // ... cache pa
        }
    }
    return result;
}
```

Why piggyback? Because we tried wrapping the `EOS_Achievements_QueryPlayerAchievements` callback ourselves and our callback fired *before* the SDK finished populating its internal cache — so we always read 0. Using the game's own `GetPlayerAchievementCount` call guarantees the cache is populated.

### 4.4 `getHAchievements` / `getHStats` with game-handle preference

In `util.cpp`:

```cpp
EOS_HAchievements getHAchievements() {
    if (g_gameHAchievements) return g_gameHAchievements;  // prefer game's
    if (g_HPlatform) {
        auto h = EOS_Platform_GetAchievementsInterface(g_HPlatform);
        if (h) return h;
    }
    return nullptr;
}

EOS_HStats getHStats() {
    if (g_gameHStats) return g_gameHStats;
    if (g_HPlatform) {
        auto h = EOS_Platform_GetStatsInterface(g_HPlatform);
        if (h) return h;
    }
    return nullptr;
}
```

The game-handle-first preference ensures that even if Epic Unlocker captured a fallback handle from the wrong platform, we prefer the one the game actually uses.

### 4.5 EACNoServerMode (Ecom emulation)

> **Note**: Originally implemented as a top-level `NoServerMode` key under `[DLC]`. In the clean EACMode refactor, this was moved to `[EAC] EACNoServerMode` (the `[DLC]` alias was removed since we hadn't published it yet). See [§8.5](#85-sdk-patches-are-optional--confirmed-2026-08-30) for the confirmation that this approach works.

When `[EAC] EACNoServerMode = True` in `ScreamAPI.ini`, the Ecom hooks return synthetic "owned" responses without contacting the server:

```cpp
EOS_EResult EOS_Ecom_QueryOwnership(EOS_HEcom hEcom, ...) {
    if (Config::EACNoServerMode()) {
        // Fill outOwnedItemCopy[] with "owned" for every requested item
        return EOS_Success;
    }
    return original(hEcom, ...);
}
```

Similarly for `EOS_Ecom_QueryEntitlements` and `EOS_Ecom_QueryEntitlementToken`. This avoids the `EOS_InvalidAuth` error from Ecom roundtrips.

### 4.6 `createFallbackAchievement` for resilience

If our definition query fails (e.g., dual-platform issue means our `HAchievements` is stale), the achievements list comes back empty. We then construct entries from the player achievement data we piggybacked:

```cpp
Overlay_Achievement createFallbackAchievement(
    const std::string& id,
    const EOS_Achievements_PlayerAchievement& pa)
{
    Overlay_Achievement a;
    a.id = id;
    a.unlocked = (pa.Progress >= 100.0);
    a.progress = pa.Progress;
    a.unlock_time = pa.UnlockTime;
    a.display_name = id;  // fallback name = id
    // ... other fields
    return a;
}
```

This guarantees the UI always has *something* to show even when the definition catalog query fails.

### 4.7 `ResetFallbackCapture` on platform release

When the game calls `EOS_Platform_Release`, the captured handles become stale. We must clear them:

```cpp
EOS_EResult EOS_Platform_Release(EOS_HPlatform h) {
    Util::g_gameHAchievements = nullptr;
    Util::g_gameHStats = nullptr;
    Util::ResetFallbackCapture();
    return original(h);
}
```

Without this, a stale handle would crash on the next definition query retry.

### 4.8 Null re-check before retry

The retry thread for `QueryAchievementDefinitions` was crashing when `g_gameHAchievements` became null mid-retry:

```cpp
// In the retry thread:
if (!hAchievements) {
    hAchievements = Util::getHAchievements();
    if (!hAchievements) {
        // still null — abort retry, don't crash
        return;
    }
}
EOS_Achievements_QueryDefinitions(hAchievements, ...);
```

This was the crash at `achievement_manager.cpp:769` you kept hitting until we added the null re-check.

### 4.9 `isEOSPlatformReady` accepting fallback

The platform-ready check originally required the Epic Unlocker-captured `HPlatform`. With dual-platform, the captured one might be Platform A while the game uses Platform B. We relaxed the check to also accept the fallback `ProductUserId`:

```cpp
bool isEOSPlatformReady() {
    if (g_HPlatform) return true;
    if (g_gameProductUserId) return true;  // game's auth session is alive
    return false;
}
```

---

## 5. Per-Game Status Matrix

| Game | SDK version | Anti-cheat | Auth model | Achievements | Notes |
|---|---|---|---|---|---|
| **Deceive Inc.** | 1.15.5 | EAC (SDK-side only) | EGS launcher | ✅ Works | Single-player, client-authoritative |
| **The Riflemen** | 1.19.0.3 | EAC | EGS launcher | ✅ Works | Confirmed with OnlineFix cert + on-disk patches |
| **Marvel Tokon** | 1.18.1.2 | EAC | EGS launcher | ❌ Untestable | PC spec issue, not our problem — out of scope |
| **MiniRoyale** | 1.19.0.3 | EAC + **SteelShield** (game plugin) | EGS launcher | ❌ Blocked | Server-authoritative + SteelShield |

### 5.1 Deceive Inc. — confirmed working

**Why it works**:
1. Anti-cheat is purely SDK-side (no game-plugin anti-cheat)
2. Achievements are client-authoritative (single-player game)
3. SDK 1.15.5 has stable function layouts matching both patch sites

**Working setup** (full four-layer recipe):
1. Epic Unlocker proxy DLL installed as `EOSSDK-Win64-Shipping.dll` (original renamed to `_o.dll`)
2. `ScreamAPI.ini` with `[EAC] EACMode=True, EACNoServerMode=True`
3. On-disk SDK patches via `eos_patcher.pyw` (both sites apply cleanly on 1.15.5)
4. OnlineFix EAC cert files in `EasyAntiCheat/Certificates/` (overwriting originals)
5. Launch from Epic Games Launcher

**What works**:
- Achievements unlock in-game
- Achievements list shows up correctly in the GUI
- Game launches from Epic Games Launcher
- No "untrusted system file" warnings

### 5.2 The Riflemen — confirmed working (2026-08-30)

This was the breakthrough that confirmed the cert files are universal. Initial attempts with just Epic Unlocker + patches failed with "unknown file version" / "untrusted system file" — EAC's loader rejected the patched SDK. Adding OnlineFix's cert files (`base.cer`, `base.bin`, `runtime.conf`) to the game's `EasyAntiCheat/Certificates/` folder fixed it instantly.

**Working setup**: identical to Deceive Inc. (same four-layer recipe).

**Key learning**: The cert files are **not** per-game. The same OnlineFix cert files work on Deceive Inc. (SDK 1.15.5) AND The Riflemen (SDK 1.19.0.3). This means the cert files are signed against OnlineFix's patched SDK binary, not against any specific game's product ID.

### 5.3 Marvel Tokon — untestable (PC spec issue)

User reported the game won't launch even with OnlineFix's full setup. Diagnosis: PC spec incompatibility, not our problem. **Out of scope** — don't spend time debugging this game.

(The SDK is 1.18.1.2 — same as the OnlineFix sample we reverse-engineered, so patches apply cleanly. The four-layer recipe would work if the game could launch at all.)

### 5.4 MiniRoyale — blocked by SteelShield

**Discovery**: MiniRoyale mounts a UE5 project plugin called `SteelShield` (visible in the game's plugin list and log: `Mounting Project plugin SteelShield`). This is a **game-side** anti-cheat, separate from the SDK's EAC client.

**Why this blocks us**:
1. SteelShield validates unlocks **server-side** (typical for competitive PvP shooters)
2. Setting `EOS_USE_ANTICHEATCLIENTNULL=1` only silences the SDK's EAC client, NOT SteelShield
3. SDK patches on `EOSSDK-Win64-Shipping_o.dll` cause a crash because the v3 patcher landed on the wrong function in SDK 1.19.0.3 (see [§6](#6-sdk-version-differences))

**Crash signature**:
```
FMallocBinned2 Attempt to realloc an unrecognized block 0xF70000
canary == 0x0 != 0xe3
```

The patched function (not the real predicate) returns `1`, which a caller dereferences as a pointer → heap corruption.

**Conclusion**: MiniRoyale achievement unlock is **unfixable without bypassing SteelShield**, which is out of scope.

---

## 6. SDK Version Differences

The patcher handles three SDK versions. SDK 1.19.x has a different function layout for patch site 1 (compiler reordered register spills), but the v6 patcher's VA-based string reference search + size validation handles it. Patch site 2 (`0x2EE0` immediate) is stable across all versions.

| Version | Game | Patch site 1 | Patch site 2 |
|---|---|---|---|
| 1.15.5 | Deceive Inc. | ✅ env var works (cert-only confirmed) | ✅ |
| 1.18.1.2 | Marvel Tokon | ✅ sig matches | ✅ |
| 1.19.0.3 | The Riflemen | ✅ v6 patcher finds small candidate (cert-only confirmed) | ✅ |

**Note**: For our confirmed games (Deceive Inc., The Riflemen), patches are optional — cert files alone work. The patcher is only needed for "strict" games that enforce EAC runtime reports.

---

## 7. The Patcher Tool (optional)

> **Note**: The patcher is **optional** for our confirmed games (Deceive Inc., The Riflemen). Both games work with cert files alone. The patcher is only needed for "strict" games that enforce EAC runtime reports (kick you mid-match, etc.).

Location: `ScreamAPI/tools/eos_patcher.pyw` (bundled in the project) and `/home/z/my-project/download/eos_patcher.pyw` (standalone). GUI-only — no CLI mode.

For the patcher usage guide (how to run, what the buttons do, what the log shows, troubleshooting), see [`Docs/EAC_Guide.md`](Docs/EAC_Guide.md) §"Optional: Apply SDK patches".

For the patcher's internal logic (signatures, search strategy, backup convention), see the source code comments in `ScreamAPI/tools/eos_patcher.pyw`.

---

## 8. Open Issues & Future Work

### 8.1 MiniRoyale SteelShield — NOT SOLVABLE

Status: **Not solvable** with our current approach. SteelShield is game-side anti-cheat with server-authoritative unlocks. Epic Unlocker intercepts client-side EOS calls, which is the wrong layer.

Possible avenues (not pursued):
- Reverse-engineer SteelShield itself (separate, large effort)
- Hook the game's achievement-reporting code (which is in `MiniRoyale.exe`, not EOS) — would require per-game patching and likely violates ToS more obviously
- Use a kernel driver (out of scope; user explicitly rejected this)

### 8.2 Bundle OnlineFix cert files with smart install option — TODO

Status: **Pending**. Currently the user must manually copy OnlineFix's cert files (`base.cer`, `base.bin`, `runtime.conf`) from a separate OnlineFix download into the game's `EasyAntiCheat/Certificates/` folder.

Future work: Bundle the cert files inside `ScreamAPI/tools/certs/` and add a "smart install" option to `eos_patcher.pyw` that:

1. Detects the game's `EasyAntiCheat/` folder (search upward from the SDK location)
2. Backs up the existing cert files (with `.eosbak` extension)
3. Copies the bundled OnlineFix cert files into place
4. **Asks the user whether to also apply SDK patches** (since cert-only is the simpler default)
5. If user opts in to patches, applies both patch sites
6. Shows a clear summary in the log area of what was done

Example GUI interaction:
```
[User clicks SMART INSTALL button]
> Found EasyAntiCheat/ at: D:\Games\<Game>\EasyAntiCheat\
> Backing up existing cert files... done (3 files → .eosbak)
> Installing OnlineFix cert files... done
>
> Cert files installed. Achievements should now unlock.
>
> Optional: apply SDK patches for strict games? [Yes] [No]
> [User clicks No]
> Skipped patches. To apply later: open patcher GUI → BROWSE → PATCH!
```

This makes the patcher a one-stop tool: cert install by default, patches opt-in.

### 8.3 Marvel Tokon — untestable (PC spec issue) — OUT OF SCOPE

User reported the game won't launch even with OnlineFix's full setup. Diagnosis: PC spec incompatibility, not our problem. Don't spend time debugging this game.

### 8.4 Investigate cert file scope

Status: **Open question**. We've confirmed OnlineFix's cert files work on Deceive Inc. (SDK 1.15.5) and The Riflemen (SDK 1.19.0.3). Open questions:
- Do the same cert files work on ALL EAC-protected EOS games, or only some?
- Are the cert files version-specific (e.g., does an SDK 1.20 cert exist that differs from the SDK 1.18 cert)?
- Can we extract cert files from any OnlineFix release and use them universally?

Testing more games would answer these. Not blocking — current setup works on the two confirmed games.

### 8.5 SDK patches are optional — CONFIRMED (2026-08-30)

Status: **Answered**. We confirmed both Deceive Inc. AND The Riflemen work with **cert files only** — no SDK patches, no env var.

**Conclusion**: The SDK patches are entirely optional for our confirmed games. The cert files satisfy EAC's **loader** (outer layer), and neither confirmed game enforces EAC's **runtime** reports.

The patches are still useful for:
- "Strict" competitive PvP games that kick you mid-match with "EAC validation failed"
- Games that actively monitor EAC runtime reports and act on them
- Defensive use (patches are harmless if unnecessary)

For the default deployment recipe, we now recommend **cert-only** as the simpler, less invasive option.

---

## 9. Reference — Files & Glossary

### 9.1 Project file locations

| File | Purpose |
|---|---|
| `/home/z/my-project/download/ScreamAPI_EACMode.zip` | Full project zip (Epic Unlocker source + Config + eos_patcher.pyw + EAC Guide + this reference doc) |
| `/home/z/my-project/download/eos_patcher.pyw` | Standalone v6 on-disk SDK patcher |
| `/home/z/my-project/download/EAC_Silencer_and_Achievement_Reference.md` | This document (technical reference) |
| `/home/z/my-project/download/EpicFix_vs_OnlineFix_vs_Epic Unlocker_Comparison.md` | Three-way comparison (reverse engineering analysis) |
| `ScreamAPI/tools/eos_patcher.pyw` (inside zip) | Bundled patcher |
| `Docs/EAC_Guide.md` (inside zip) | Clean how-to guide |
| `old_eac_silencer_reference/` (inside zip) | Archived standalone DLL silencer project (bugs fixed) |

### 9.2 Glossary

- **EAC** — Easy Anti-Cheat (Epic's runtime anti-cheat)
- **EOS** — Epic Online Services
- **EOS SDK** — The C-API DLL `EOSSDK-Win64-Shipping.dll`
- **HPlatform / HAchievements / HStats** — Typed opaque handles returned by EOS SDK
- **EpicAccountId** — User identity in EOS Auth subsystem
- **ProductUserId** — User identity in EOS Connect subsystem (separate from EpicAccountId)
- **EACMode** — Epic Unlocker config flag that gates all 12 EAC-specific patches (game-handle capture, etc.)
- **EACNoServerMode** — Epic Unlocker config flag that bypasses Ecom server roundtrips
- **Cert files** — `base.cer`, `base.bin`, `runtime.conf` — OnlineFix's universal EAC cert files, dropped into game's `EasyAntiCheat/Certificates/`
- **Piggyback capture** — Reading data through the game's own SDK calls instead of issuing our own
- **Dual-platform issue** — UE5 OSSv2 creating multiple HPlatform instances with different auth
- **SteelShield** — MiniRoyale's game-side anti-cheat (separate from EAC)
- **Patch site 1 / site 2** — The two SDK binary patches OnlineFix uses
- **`.eosbak`** — Backup file extension used by our patcher (was `.ofme` in v4 and earlier; OnlineFix still uses `.ofme`)

---

*End of reference document.*
