# EpicFix vs OnlineFix vs ScreamAPI — Final Analysis

> Complete findings from reverse-engineering, decompilation, binary diffing, runtime testing,
> and source code analysis across multiple EOS unlocker variants.

---

## 1. Executive Summary

Three tools, three architectures, all solving the same problem: making EOS-dependent games
work without legitimate ownership. The key finding is that **OnlineFix works where ScreamAPI
fails because OnlineFix emulates the Ecom interface locally without contacting Epic's
servers, while ScreamAPI calls through to the real SDK which requires valid server auth.**

| Tool | Core approach | EAC bypass | DLC/ownership | Achievements | Auth dependency |
|---|---|---|---|---|---|
| **ScreamAPI** | DLL proxy + callback wrapping | None | Calls real SDK → modifies result | Hooks real SDK | Yes — server validates auth |
| **ScreamAPI + NoServerMode** | DLL proxy + local Ecom stub | None | Returns "owned" locally (no server) | Hooks real SDK | Ecom: No. Achievements: Yes |
| **OnlineFix** | SDK patches + packed Ecom emulator | SDK patches (func1/func2) | Local stub (via VMProtected DLL) | Pass-through | No — never contacts Epic for Ecom |
| **FreeTP EpicFix** | DLL proxy + Steamworks bridge | None needed (no EAC) | ScreamAPI shim | ScreamAPI shim | Steam (not Epic) |

---

## 2. Component Map — All Variants

### 2.1 OnlineFix (MARVEL Tokon: Fighting Souls)

| File | Role |
|---|---|
| `EGSAuthLauncher.exe` | .NET + WebView2 app. Real Epic OAuth login → exchange code |
| `OnlineFixLauncher.json` | Launch template with `-AUTH_TYPE=exchangecode` |
| `EOSSDK-Win64-Shipping.dll` | **Patched SDK** — 18 bytes changed in 19 MB (2 functions stubbed) |
| `EOSSDK-Win64-Shipping.ofme` | Pristine backup of original SDK |
| `OnlineFix64.dll` (10.9 MB) | VMProtected payload — Ecom emulation + multiplayer relay |
| `preloader.dll` (5 KB) | Proxy loader — loads patched SDK + OnlineFix64.dll + original Steam stub |
| `EasyAntiCheat/Certificates/` | **Genuine** Epic-signed EAC files (not fake — for local bootstrapper only) |
| `OnlineFix.ini` | Config: `UnlockAllDLC=true`, `Ecom=true` |

### 2.2 FreeTP EpicFix (TMNT: Splintered Fate)

| File | Role |
|---|---|
| `winmm.dll` (22 KB) | DLL-hijack proxy — loads EpicFix64.dll, forwards real winmm calls |
| `EpicFix64.dll` (500 KB) | **Lightly packed** — 474/480 functions readable. EOS API interception + Steamworks bridge |
| `EpicFix.ini` | Config: `[Info] Id=6300`, `[ScopeFlags] Country=False, FriendsList=False` |
| `winhttp.dll` (7.3 MB) | Koaloader proxy — auto-loads ScreamAPI64.dll |
| `ScreamAPI64.dll` (1.5 MB) | Standard ScreamAPI — DLC entitlements + achievement overlay |
| `EOSSDK-Win64-Shipping.dll` | **Pristine** — ScreamAPI loads it at runtime and forwards calls |

### 2.3 ScreamAPI (standalone, our project)

| Component | Role |
|---|---|
| `EOSSDK-Win64-Shipping.dll` (proxy) | ScreamAPI proxy — intercepts EOS calls, loads real SDK |
| `EOSSDK-Win64-Shipping_o.dll` | Renamed real SDK |
| `ScreamAPI.ini` | Config: `NoServerMode`, `UnlockAllDLC`, achievement settings |

---

## 3. The Two SDK Patches (OnlineFix only)

Both patches land on functions in a C++ vtable at `.rdata:0x180F52548`:

### Patch site 1: `sub_1803B9A60` — EAC silencer

**Original function**: Reads env var `EOS_USE_ANTICHEATCLIENTNULL` and compares to `"1"`.
Returns true if the anticheat client should be NULL (disabled).

**Patch** (11 bytes at file offset `0x3B8E60`):
```asm
mov rax, 1        ; 48 C7 C0 01 00 00 00
ret               ; C3
nop; nop; nop     ; 90 90 90
```

**Effect**: Always returns true → SDK loads its internal no-op anticheat client.
The EAC client integration path (registration, heartbeat, ProtectMessage) is never taken.

### Patch site 2: `sub_1803BCC50` — Validation bypass

**Original function**: 684-instruction settings/negotiation aggregator. Collects 6 string
properties via vtable calls, submits to server, retries on `EOS_VersionMismatch`.
Returns `0x2EE0` (12000) as one of its legitimate early-exit paths.

**Patch** (7 bytes at file offset `0x3BC051`):
```asm
mov eax, 0x2EE0   ; C7 C0 E0 2E 00 00
ret               ; C3
```

**Effect**: Always returns 12000 (the function's own OOM fallback code) → the anti-cheat
client handshake/version exchange always resolves to the "nothing to do" path.

### Why these patches are elegant

- Only 18 bytes change — every hash-based signature except these stays authentic Epic code
- The patch lives below the exported C API: all 1000+ `EOS_*` exports still behave normally
- The `.ofme` pristine copy enables instant revert

---

## 4. Architecture Comparison — Deep Dive

### 4.1 DLC/Ownership bypass — the critical difference

| | ScreamAPI (default) | ScreamAPI + NoServerMode | OnlineFix |
|---|---|---|---|
| Hook layer | Public EOS API (`EOS_Ecom_QueryOwnership`) | Same | Internal vtable / VMProtected |
| Flow | Game → ScreamAPI → real SDK → **Epic server** → modify result | Game → ScreamAPI → **return "owned" immediately** | Game → OnlineFix stub → **return "owned" immediately** |
| `original()` called? | Yes | **No** | **No** |
| Server roundtrip? | Yes | **No** | **No** |
| Auth dependency | Yes — server validates token | **No** — server never contacted | **No** |
| Failure with broken auth | `EOS_InvalidAuth` | Works (no server) | Works (no server) |

**This is the root cause of all our `EOS_InvalidAuth` failures**: ScreamAPI's default mode
calls `original()` which contacts Epic's server. When auth is broken (EAC, cert mismatch,
flagged account), the server rejects the request before ScreamAPI can modify the result.

NoServerMode (our new patch) fixes this by skipping `original()` and returning fake results
directly — matching OnlineFix's architecture.

### 4.2 EAC bypass approaches

| Approach | How it works | Works on Deceive Inc.? |
|---|---|---|
| SDK patches (OnlineFix) | Stub 2 internal functions → anticheat never initializes | ✅ Yes (if DLL injection works) |
| Cert swap (our test) | Replace EAC config files with relaxed-scan version | ✅ Startup scan passes, ❌ periodic rescan kicks |
| DLL injection + PEB unlinking | Hide injected DLL from module list | ❌ EAC detects injection itself |
| DLL injection + WinVerifyTrust hook | Make signature checks pass | ❌ EAC detects injection before hook runs |
| DLL proxy (winmm.dll) | Load via normal import resolution — no injection | ✅ No injection signature (OnlineFix uses this) |

### 4.3 Auth chain

| | OnlineFix | FreeTP EpicFix | ScreamAPI |
|---|---|---|---|
| Auth method | Real Epic OAuth (WebView2) | Whatever the game gets | Game's own auth |
| Token validity | Real token from real login | Real token | Real token |
| Ecom auth check | **Bypassed** (local stub, no server) | **Bypassed** (ScreamAPI shim) | **Validated** (server roundtrip) |
| Achievement auth check | Pass-through (real SDK handles) | ScreamAPI hooks | **Validated** (server roundtrip) |

---

## 5. NoServerMode — Our New ScreamAPI Patch

### What it does

When `NoServerMode=True` in `ScreamAPI.ini`:

| EOS function | Default behavior | NoServerMode behavior |
|---|---|---|
| `EOS_Ecom_QueryOwnership` | Call `original()` → server → modify result | Return "owned" immediately (no `original()` call) |
| `EOS_Ecom_QueryEntitlements` | Call `original()` → server → inject extra | Return fake entitlements immediately |
| `EOS_Ecom_QueryOwnershipToken` | Call `original()` → server → return token | Return `EOS_Success` with empty token |
| `EOS_Ecom_GetEntitlementsCount` | Already returns from local data | No change needed |
| `EOS_Ecom_CopyEntitlementByIndex` | Already returns from local data | No change needed |
| `EOS_Achievements_*` | Calls real SDK → server | **No change** — still requires valid auth |

### Runtime log confirmation

From the Deceive Inc. test with NoServerMode enabled:
```
[INTERCEPT] NoServerMode: returning 2 ownership(s) without server roundtrip  ← WORKS
[ACH] queryDefinitionsComplete called with ResultCode: 2147483647            ← Still fails
[ACH] Failed to query achievement definitions. Result: EOS_UnexpectedError
```

**NoServerMode successfully bypasses Ecom auth** — ownership queries return locally
without server contact. But **achievement definitions query still fails** because it
goes through the real SDK → Epic server → auth rejected.

### What NoServerMode does NOT fix

Achievements require `EOS_Achievements_QueryDefinitions` which contacts Epic's server
to download the achievement definitions list. When auth is broken, the server returns
`EOS_UnexpectedError` (0x7FFFFFFF).

This is the same architecture limitation OnlineFix has — OnlineFix doesn't unlock
achievements either (it passes through to the real SDK for achievements).

### Config

```ini
[DLC]
UnlockAllDLC    = True
ForceSuccess    = False
NoServerMode    = True    ; Ecom hooks skip real SDK, return local results
```

---

## 6. The EAC Certificate Finding

### Initial misidentification

I initially identified the EAC `base.cer` as a "fake self-signed cert" created by OnlineFix.
This was **wrong**. The certificate is **genuine** — signed by Epic's real
"Anti-Cheat Integrity Intermediate CA1", with the Subject CN matching the game's
EAC product ID.

### What the EAC files actually do

| File | What it is | Purpose |
|---|---|---|
| `base.cer` | Genuine Epic-signed X.509 certificate | Local EAC bootstrapper validates the anti-cheat module chain |
| `base.bin` | Encrypted EAC module container (header `EAC\0` v5) | Decrypted by EAC's own loader at runtime |
| `runtime.conf` | Encrypted runtime configuration | Consumed by EAC loader |

### Why the cert "swap" appeared to work on Deceive Inc.

When we copied Marvel Tokon's EAC cert files into Deceive Inc.'s folder, the game
loaded past the startup screen because:
1. EAC's local bootstrapper accepted the cert (genuine Epic PKI, just for a different title)
2. EAC's startup scan used relaxed rules from the different `base.bin`

But it failed later because:
1. The periodic EAC rescan detected the cross-title cert mismatch → 4-second kick in matches
2. EOS auth was broken because the cert's Subject CN (Marvel Tokon's product ID) didn't
   match Deceive Inc.'s product ID → server rejected auth tokens → `EOS_InvalidAuth`

**The cert swap was never OnlineFix's mechanism.** OnlineFix bypasses EAC via the SDK
patches (func1/func2), not via cert replacement.

---

## 7. OnlineFix64.dll — Static Analysis Results

### Packing

- 10.9 MB DLL, heavily packed (VMProtect or custom packer)
- 777 of 797 functions decompile to `halt_baddata()` — they live in sections with zero
  raw size on disk, existing only after runtime unpacking
- Only 20 functions contain real code, all in the `.ofme2` section

### What the 20 visible functions do

| Category | Functions | Purpose |
|---|---|---|
| Anti-VM detection | `FUN_180ad6176` | Scans for `QEMU`, `Oracle`, `VirtualBox`, `VMware`, `Parallels`, `77777` strings |
| Anti-debug trap | `FUN_180ad6786` | Infinite loop (`do {} while(true)`) — traps hardware breakpoints |
| I/O port access | `FUN_180ad67dc` | `out(0x22, in_AL)` — SMM/hardware detection |
| Obfuscated dispatchers | `FUN_180667005`, `FUN_180681cbe`, `FUN_180695882` | VMProtect-style opaque arithmetic → indirect jump |
| Decompressor | `FUN_180ad4cdb` | ~600-line LZMA-style range coder for unpacking packed sections |
| String comparison | `FUN_180ad665f` | XOR-obfuscated string comparison (`0x56463b72`, `0xa9b9c48e`) |
| Constant returns | `FUN_18066dbe5` (returns `0x203b20034`), `FUN_18068871f` (returns `0xee`) | Purpose unclear — likely packer stubs |

**None of the 20 visible functions hook `EOS_Ecom_*` or any EOS API.** The actual Ecom
emulation code lives in the packed sections that only exist after runtime unpacking.

### Security implications

The VMProtected `OnlineFix64.dll` is unauditable. It could:
- Exfiltrate the OAuth refresh token to third-party servers
- Collect telemetry/analytics
- Install persistent hooks beyond what's visible
- The anti-VM and anti-debug code suggests the author actively prevents analysis

**Our NoServerMode patch provides the same Ecom bypass with clean, auditable code.**

---

## 8. FreeTP EpicFix — Alternative Architecture

### Key difference: Steamworks bridge instead of Epic relay

FreeTP's `EpicFix64.dll` is **lightly packed** (474/480 functions readable) and uses a
completely different multiplayer strategy:

```
OnlineFix:  Epic auth → EOS SDK patches → Ecom emulation → online-fix relay servers
FreeTP:     Game's auth → EOS API interception → Steamworks bridge → Steam networking
```

### EOS API interception table (from plaintext decompilation)

FreeTP intercepts the entire auth/connect/ecom surface:
- `EOS_Platform_GetConnectInterface`, `EOS_Platform_GetAuthInterface`
- `EOS_Initialize`, `EOS_Platform_Create`, `EOS_Platform_Tick`
- `EOS_Auth_Login`, `EOS_Connect_Login`
- `EOS_Ecom_QueryOwnership`, `EOS_Ecom_QueryEntitlements`
- `EOS_Ecom_CopyEntitlementByIndex`, `EOS_Ecom_GetEntitlementsCount`
- `EOS_AntiCheatClient_PollStatus` (neutralized at call level)
- Plus auth/connect/userinfo/metrics functions

### Steamworks bridge

Resolves via `GetProcAddress` on Steam module:
- `SteamAPI_ISteamUser_GetSteamID` — player identity
- `SteamAPI_ISteamFriends_GetPersonaName` — display name
- `SteamAPI_ISteamFriends_SetRichPresence` — presence
- `SteamAPI_ISteamFriends_GetFriendCount` — friend list

Player identity and social presence come from Steam, replacing the Epic social graph
that a pirated copy cannot access.

### Why no SDK patching was needed

TMNT: Splintered Fate ships without EAC enforcement (no `EasyAntiCheat/` folder).
The SDK stays pristine because ScreamAPI needs to forward unhooked calls to it.

---

## 9. Runtime Test Results on Deceive Inc.

### Test matrix

| Configuration | Game loads? | Ecom works? | Achievements work? | EAC kick? |
|---|---|---|---|---|
| No modifications | ✅ Yes | ✅ Yes (real) | ✅ Yes (real) | ❌ No (game works normally) |
| Cert swap only | ✅ Yes | ❌ `EOS_InvalidAuth` | ❌ `EOS_InvalidAuth` | ❌ 4-sec kick in match |
| Cert swap + ScreamAPI | ✅ Yes | ❌ `EOS_InvalidAuth` | ❌ `EOS_UnexpectedError` | ❌ 4-sec kick in match |
| Cert swap + ScreamAPI + NoServerMode | ✅ Yes | ✅ **Works** (local stub) | ❌ `EOS_UnexpectedError` | ❌ 4-sec kick in match |
| DLL injection (EacSilencer) | ❌ Infinite loading | N/A | N/A | N/A |
| DLL injection (EacSilencer --minimal) | ❌ Infinite loading | N/A | N/A | N/A |
| DLL injection (EacSilencer --soft) | ❌ Infinite loading | N/A | N/A | N/A |

### Key findings

1. **DLL injection fails on Deceive Inc.** — EAC detects the injection itself, not what
   the DLL does. Even `--minimal` mode (DLL loaded but does nothing) causes infinite loading.

2. **Cert swap works for startup but fails for periodic rescan** — game loads, but EAC
   kicks after 4 seconds of actual gameplay (when "Deploy Agent" is pressed).

3. **NoServerMode fixes Ecom** — ownership queries return locally without server contact.
   The log confirms: `NoServerMode: returning 2 ownership(s) without server roundtrip`.

4. **Achievements still fail** — `EOS_Achievements_QueryDefinitions` contacts Epic's
   server, which rejects the auth token → `EOS_UnexpectedError`. NoServerMode doesn't
   cover achievements (they use a different code path).

5. **Two EOS platforms are created by design** — the game creates one for the engine
   and one for the OnlineSubsystem. This is normal UE5 behavior, not caused by the cert
   swap or any patch.

---

## 10. The Auth Chain Problem

### Why achievements can't work without valid auth

```
Achievement flow:
  Game → EOS_Achievements_QueryDefinitions(HAchievements, ProductUserId, callback)
       → Real EOS SDK sends request to Epic's server
       → Server validates:
           1. Auth token (from EOS_Auth_Login)
           2. Product user ID (from EOS_Connect_Login)
           3. Sandbox ID (from EOS_Platform_Create)
       → If any validation fails → EOS_UnexpectedError

Ecom flow (with NoServerMode):
  Game → ScreamAPI hook → return "owned" immediately
       → Server NEVER contacted
       → No auth validation needed
```

### Why NoServerMode can't be applied to achievements

Achievement definitions are **server-side data** — the list of achievements, their
descriptions, icons, and stat thresholds are all stored on Epic's servers. To return
them locally, we'd need to:
1. Hardcode the achievement definitions for each game (different per game)
2. Or fetch them once from a valid session and cache them

This is significantly more complex than Ecom emulation (which just returns "owned").

### The fundamental tension

| Goal | Requires server contact? | Breaks when auth is broken? |
|---|---|---|
| DLC ownership bypass | No (can stub locally) | No |
| Entitlement emulation | No (can stub locally) | No |
| Achievement definitions query | **Yes** (server-side data) | **Yes** |
| Achievement unlock | **Yes** (server records it) | **Yes** |
| Stat ingestion | **Yes** (server evaluates) | **Yes** |

NoServerMode solves the first two. Achievements require a different approach entirely.

---

## 11. Implementation Status

### What we built

| Component | Status | Purpose |
|---|---|---|
| **EacSilencer** (`/download/EacSilencer.zip`) | ✅ Built and tested | SDK patches + WinVerifyTrust hook + PEB unlinking |
| **ScreamAPI NoServerMode** (`/download/ScreamAPI_NoServerMode.zip`) | ✅ Built and tested | Ecom local stub (no server roundtrip) |
| **Comparison MD** (this document) | ✅ Updated | Complete analysis |

### What works

| Feature | Non-EAC games | EAC games (with cert swap) | EAC games (with DLL injection) |
|---|---|---|---|
| Game launches | ✅ | ✅ | ❌ (infinite loading) |
| DLC/ownership bypass | ✅ | ✅ (with NoServerMode) | N/A |
| Achievement unlock | ✅ | ❌ (`EOS_UnexpectedError`) | N/A |
| Stay in match | ✅ | ❌ (4-sec kick) | N/A |

### What doesn't work

1. **Achievements on EAC games** — server rejects auth, can't query definitions
2. **DLL injection on strict EAC games** — EAC detects the injection itself
3. **Staying in matches with cert swap** — EAC periodic rescan catches the cross-title cert

### What would be needed to fix achievements on EAC games

Option A: **Cache achievement definitions** — fetch them once from a valid session,
then return them locally in NoServerMode. Requires:
- A one-time tool to query and save achievement definitions
- A new ScreamAPI mode that returns cached definitions without server contact

Option B: **Fix the auth chain** — make EOS_Auth_Login work despite EAC.
This is what OnlineFix64.dll likely does (via runtime hooks in its packed code),
but we can't replicate it without unpacking the VMProtected payload.

Option C: **Use the Steamworks bridge approach** (like FreeTP) — bypass Epic auth
entirely and use Steam for identity. This is a major architecture change.

---

## 12. Recommendation

### For non-EAC EOS games
Use **ScreamAPI as-is** (no NoServerMode needed). Auth works, achievements work, DLC bypass works.

### For EAC games where DLL injection works
Use **EacSilencer (--normal mode) + ScreamAPI**. The SDK patches silence EAC, ScreamAPI
handles DLC/achievements. No cert swap needed.

### For EAC games where DLL injection fails (like Deceive Inc.)
Use **cert swap + ScreamAPI with NoServerMode=True**. This gives:
- ✅ Game launches (cert swap relaxes EAC startup scan)
- ✅ DLC/ownership bypass (NoServerMode returns "owned" locally)
- ❌ Achievements don't work (server rejects auth)
- ❌ 4-second kick in matches (periodic EAC rescan)

### For a complete OnlineFix-equivalent bypass
Would require either:
- Unpacking `OnlineFix64.dll` to see how it fixes the auth chain (runtime analysis with x64dbg)
- Building a Steamworks bridge like FreeTP (major architecture change)
- Caching achievement definitions for offline return (Option A above)

---

*End of analysis. This document supersedes all previous versions.*
