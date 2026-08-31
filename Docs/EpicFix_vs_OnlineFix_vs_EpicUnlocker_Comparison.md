# EpicFix vs OnlineFix vs Epic Unlocker — Ultimate Analysis

> Complete findings from reverse-engineering, decompilation, binary diffing, runtime testing,
> and source code analysis across all three EOS unlocker variants.
>
> **Status as of 2026-08-30**: Achievements confirmed working on Deceive Inc. (SDK 1.15.5) and The Riflemen (SDK 1.19.0.3) using cert files only — no SDK patches needed.

---

## Table of Contents

1. Executive Summary
2. Component Map — All Variants
3. The Two SDK Patches (OnlineFix only)
4. Architecture Comparison — Deep Dive
5. EACNoServerMode — Epic Unlocker's Ecom Bypass
6. The EAC Certificate Finding — THE Critical Missing Piece
7. OnlineFix64.dll — Static Analysis Results
8. EpicFix — Deep Decompilation
9. Three-Way Comparison: Epic Unlocker vs OnlineFix vs EpicFix
10. Security & Trust Considerations
11. Composability

---

## 1. Executive Summary

Three tools, three architectures, all solving the same problem: making EOS-dependent games
work without legitimate ownership.

| Tool | Core approach | EAC bypass | DLC/ownership | Achievements | Auth dependency |
|---|---|---|---|---|---|
| **Epic Unlocker** | DLL proxy + callback wrapping | None (relies on cert files) | Calls real SDK → modifies result | Hooks real SDK (with EACMode fix) | Yes — server validates auth |
| **Epic Unlocker + `[EAC]` config** | DLL proxy + local Ecom stub + game-handle capture | None (cert files) | Returns "owned" locally (no server) | Uses game's auth session (EACMode) | Ecom: No. Achievements: game's session |
| **OnlineFix** | SDK patches + packed Ecom emulator | SDK patches (func1/func2) | Local stub (via VMProtected DLL) | Pass-through | No — never contacts Epic for Ecom |
| **EpicFix** | DLL proxy + Steamworks bridge | None needed (no EAC games) | Epic Unlocker shim | Epic Unlocker shim | Steam (not Epic) |

**Key finding (2026-08-30)**: The cert files (`base.cer`, `base.bin`, `runtime.conf`) are the universal requirement — they satisfy EAC's loader. Whether SDK patches are additionally needed depends on whether the game enforces EAC's runtime reports. Our confirmed games (Deceive Inc., The Riflemen) don't enforce runtime reports — cert files alone work.

---

## 2. Component Map — All Variants

### 2.1 OnlineFix (Marvel Tokon: Fighting Souls)

| File | Role |
|---|---|
| `EGSAuthLauncher.exe` | .NET + WebView2 app. Real Epic OAuth login → exchange code |
| `OnlineFixLauncher.json` | Launch template with `-AUTH_TYPE=exchangecode` |
| `EOSSDK-Win64-Shipping.dll` | **Patched SDK** — 18 bytes changed in 19 MB (2 functions stubbed) |
| `EOSSDK-Win64-Shipping.ofme` | Pristine backup of original SDK |
| `OnlineFix64.dll` (10.9 MB) | VMProtected payload — Ecom emulation + multiplayer relay |
| `preloader.dll` (5 KB) | Proxy loader — loads patched SDK + OnlineFix64.dll + original Steam stub |
| `EasyAntiCheat/Certificates/` | **Genuine** Epic-signed EAC files (universal — work across games) |
| `OnlineFix.ini` | Config: `UnlockAllDLC=true`, `Ecom=true` |

### 2.2 EpicFix (TMNT: Splintered Fate / FreeTP variant)

| File | Role |
|---|---|
| `winmm.dll` (22 KB) | DLL-hijack proxy — Windows loads this before game code; forwards real `winmm` calls + loads EpicFix64.dll |
| `EpicFix64.dll` (500 KB) | **Lightly packed** — 474/480 functions readable. EOS API interception + Steamworks bridge |
| `EpicFix.ini` | Config: `[Info] Id=6300`, `[ScopeFlags] Country=False, FriendsList=False` |
| `winhttp.dll` (7.3 MB) | Koaloader proxy — auto-loads ScreamAPI64.dll |
| `ScreamAPI64.dll` (1.5 MB) | Standard Epic Unlocker — DLC entitlements + achievement overlay |
| `EOSSDK-Win64-Shipping.dll` | **Pristine** — Epic Unlocker loads it at runtime and forwards calls |

### 2.3 Epic Unlocker (standalone, our project)

| Component | Role |
|---|---|
| `EOSSDK-Win64-Shipping.dll` (proxy) | Epic Unlocker proxy — intercepts EOS calls, loads real SDK |
| `EOSSDK-Win64-Shipping_o.dll` | Renamed real SDK |
| `ScreamAPI.ini` | Config: `[EAC] EACMode`, `EACNoServerMode`, `UnlockAllDLC`, achievement settings |

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
- The `.eosbak` pristine copy enables instant revert

### When are the SDK patches actually needed? (2026-08-30 finding)

After extensive testing, we discovered that **the patches are optional** for our confirmed games (Deceive Inc., The Riflemen). Both games work with **cert files only** — no patches, no env var.

The patches address scenarios that **neither confirmed game actually hits**:

| Patch | What it does | When it's needed |
|---|---|---|
| Site 1 (`ShouldUseNullAntiCheatClient` → TRUE) | Silences the SDK's anti-cheat client (prevents runtime integrity scans) | Only needed for games that **enforce** EAC runtime reports (act on detection) |
| Site 2 (`SubmitServerValidationRequest` → 0x2EE0) | Skips anti-cheat validation roundtrips | Only needed for games that **kick you mid-match** when validation fails |

**Our confirmed games**:
- Deceive Inc. — works with cert only (game doesn't enforce runtime reports)
- The Riflemen — works with cert only (same)

**Hypothesis**: The cert files satisfy EAC's **loader** (outer layer). Whether the SDK patches are additionally needed depends on whether the game enforces EAC's **runtime** reports (not just the loader scan).

**When to apply patches anyway**:
- For "strict" competitive PvP games that kick you mid-match
- For games where the cert alone produces "EAC validation failed" during gameplay
- As a defensive measure if you're unsure (patches are harmless if unnecessary)

For most single-player / co-op EOS games with EAC, cert-only should suffice.

---

## 4. Architecture Comparison — Deep Dive

### 4.1 DLC/Ownership bypass — the critical difference

| | Epic Unlocker (default) | Epic Unlocker + `[EAC] EACNoServerMode` | OnlineFix |
|---|---|---|---|
| Hook layer | Public EOS API (`EOS_Ecom_QueryOwnership`) | Same | Internal vtable / VMProtected |
| Flow | Game → Epic Unlocker → real SDK → **Epic server** → modify result | Game → Epic Unlocker → **return "owned" immediately** | Game → OnlineFix stub → **return "owned" immediately** |
| `original()` called? | Yes | **No** | **No** |
| Server roundtrip? | Yes | **No** | **No** |
| Auth dependency | Yes — server validates token | **No** — server never contacted | **No** |
| Failure with broken auth | `EOS_InvalidAuth` | Works (no server) | Works (no server) |

**This is the root cause of all our `EOS_InvalidAuth` failures**: Epic Unlocker's default mode
calls `original()` which contacts Epic's server. When auth is broken (EAC, cert mismatch,
flagged account), the server rejects the request before Epic Unlocker can modify the result.

`EACNoServerMode` (our patch) fixes this by skipping `original()` and returning fake results
directly — matching OnlineFix's architecture.

### 4.2 EAC bypass approaches

| Approach | How it works | Works? |
|---|---|---|
| Cert files (universal) | Replace EAC config files with OnlineFix's universal certs | ✅ Works on all confirmed games |
| SDK patches (OnlineFix) | Stub 2 internal functions → anticheat never initializes | ✅ Optional (cert alone suffices for our games) |
| DLL injection + PEB unlinking | Hide injected DLL from module list | ❌ EAC detects injection itself |
| DLL injection + WinVerifyTrust hook | Make signature checks pass | ❌ EAC detects injection before hook runs |
| DLL proxy (winmm.dll) | Load via normal import resolution — no injection | ✅ No injection signature (EpicFix uses this) |

### 4.3 Auth chain

| | OnlineFix | EpicFix | Epic Unlocker |
|---|---|---|---|
| Auth method | Real Epic OAuth (WebView2) | Real Epic OAuth Device Auth (RFC 8628) | Game's own auth |
| Token validity | Real token from real login | Real token from real login | Real token |
| Ecom auth check | **Bypassed** (local stub, no server) | **Bypassed** (callback hooking) | **Bypassed** (with EACNoServerMode) |
| Achievement auth check | Pass-through (real SDK handles) | Pass-through (real SDK handles) | **Fixed** (with EACMode game-handle capture) |

---

## 5. EACNoServerMode — Epic Unlocker's Ecom Bypass

### What it does

When `[EAC] EACNoServerMode = True` in `ScreamAPI.ini`:

| EOS function | Default behavior | EACNoServerMode behavior |
|---|---|---|
| `EOS_Ecom_QueryOwnership` | Call `original()` → server → modify result | Return "owned" immediately (no `original()` call) |
| `EOS_Ecom_QueryEntitlements` | Call `original()` → server → inject extra | Return fake entitlements immediately |
| `EOS_Ecom_QueryOwnershipToken` | Call `original()` → server → return token | Return `EOS_Success` with empty token |
| `EOS_Ecom_GetEntitlementsCount` | Already returns from local data | No change needed |
| `EOS_Ecom_CopyEntitlementByIndex` | Already returns from local data | No change needed |
| `EOS_Achievements_*` | Calls real SDK → server | **No change** — uses game's auth session (via EACMode) |

### Config

```ini
[EAC]
EACMode         = True       ; game-handle capture + piggyback + fallbacks
EACNoServerMode = True       ; Ecom hooks return local "owned" (no server roundtrip)
```

Both default to `False`. The `[EAC]` section is optional — non-EAC games are unaffected.

---

## 6. The EAC Certificate Finding — THE Critical Missing Piece

> **Update (2026-08-30)**: This was the breakthrough discovery. After extensive testing, we confirmed OnlineFix's cert files are **universal** — they work across multiple games (Deceive Inc., The Riflemen) without modification.

### What the EAC files actually are

| File | What it is | Purpose |
|---|---|---|
| `base.cer` | Genuine Epic-signed X.509 certificate | EAC's loader validates the SDK binary against this cert |
| `base.bin` | Encrypted EAC module container | Decrypted by EAC's loader at runtime |
| `runtime.conf` | Encrypted runtime configuration | Consumed by EAC loader |

These are **not forged** — they are genuine Epic PKI material. They are signed to accept OnlineFix's patched SDK, which is exactly what we need after we patch the SDK ourselves (or even without patching — see below).

### Why the cert swap is universally required

Without these cert files, EAC's **loader** (which runs before SDK init) rejects the patched SDK with "unknown file version" or "untrusted system file" — **even if every other step is correct**. The cert files tell EAC's loader "this SDK binary is authorized".

- The cert files are universal across games (confirmed on Deceive Inc. SDK 1.15.5 + The Riflemen SDK 1.19.0.3)
- They are NOT per-game — the same OnlineFix cert files work on every game we tested
- They are signed against OnlineFix's patched SDK binary, not against any specific game's product ID

### Critical: Leave `Settings.json` alone

Each game ships its own `EasyAntiCheat\Settings.json` with the game's specific EAC configuration (product ID, sandbox ID, etc.). Do **not** overwrite it with OnlineFix's version — that breaks the SDK's product binding. Only `Certificates\base.cer`, `base.bin`, `runtime.conf` are universal.

### Historical analysis (now superseded)

Our earlier analysis assumed the cert files were per-game. This was wrong — we were testing with mismatched cert+SDK combinations (Marvel Tokon's cert on Deceive Inc.'s SDK without patches), which produced misleading failures (4-second kicks, `EOS_InvalidAuth`). With the correct universal cert files properly installed, none of those failures occur.

The correct interpretation: the cert swap **is** OnlineFix's mechanism. OnlineFix's cert files are signed against OnlineFix's patched SDK, and they work universally because the cert validates the SDK binary, not the game's product ID.

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

The anti-VM and anti-debug code suggests the author actively prevents analysis.

**Our `EACNoServerMode` patch provides the same Ecom bypass with clean, auditable code.**

---

## 8. EpicFix — Deep Decompilation

> From IDA Pro Hex-Rays decompilation of `EpicFix64.dll`. EpicFix is **lightly packed** (474/480 functions readable), so this analysis is far more complete than OnlineFix64.dll's.

### 8.1 What EpicFix Actually Is

EpicFix is a **full EOS ↔ Steam translation shim** — not just an ownership spoofer. It is a binary-level bridge that intercepts EOS API calls and translates them to Steam API calls, enabling EOS-dependent games to work with Steam's infrastructure (and vice versa). It is commonly used for **cross-play mods** and **launcher-agnostic play**, not just "cracking."

### 8.2 Architecture: Bidirectional EOS↔Steam Translation

**Evolution of understanding:**
1. *Initial assumption:* EpicFix replaces the entire EOS SDK — no real Epic servers contacted. *(Incorrect)*
2. *First correction (from live testing):* EpicFix is a partial middleware — hooks only specific EOS functions, passes rest through. *(Partially correct)*
3. *Full correction (from decompilation):* EpicFix is a **bidirectional EOS ↔ Steam translation shim** that performs real Epic OAuth, translates Steam API calls to EOS and vice versa, queries Epic's GraphQL catalog to discover entitlements, and spoofs ownership via callback hooking.

### EOS Feature Handling Matrix

| EOS Feature | EpicFix | Epic Unlocker |
|---|---|---|
| **Auth** (`EOS_Connect_*`, `EOS_Auth_*`) | **Real OAuth Device Auth flow** — obtains genuine Epic token via browser login | Untouched — real Epic auth |
| **Ownership** (`EOS_Ecom_QueryOwnership`) | **Hooked** — callback result flipped to "owned" (base game + all DLC via GraphQL scan) | **Hooked** — adds DLC entitlements to real ownership results |
| **Entitlements** (`EOS_Ecom_QueryEntitlements`) | Cannot forge signed tokens | Cannot forge signed tokens |
| **Lobbies** (`EOS_Lobby_*`) | **Hooked/emulated** — local discovery instead of Epic servers | Untouched — real lobbies |
| **P2P** (`EOS_P2P_*`) | **Hooked/emulated** — local sockets instead of Epic relay | Untouched — real P2P |
| **Achievements** (`EOS_Achievements_*`) | Pass-through — real SDK handles it | **Hooked** — intercepts queries + unlock notifications (EACMode uses game's auth) |
| **Stats** (`EOS_Stats_*`) | Pass-through or skipped | **Hooked** — force-ingest via `EOS_Stats_IngestStat` |
| **Friends** (`EOS_Friends_*`) | **Translated** — Steam friends ↔ EOS friends (when enabled) | Untouched |
| **Presence** (`EOS_Presence_*`) | **Translated** — EOS presence ↔ Steam Rich Presence | Untouched |

### 8.3 How EpicFix Spoofs Ownership

EpicFix does **not** replace the EOS SDK — it only intercepts the ownership query callback. Two known techniques:

**Technique A: Post-call hook**
```
Game calls EOS_Ecom_QueryOwnership(...)
  → EpicFix's hook fires first
  → EpicFix calls the real EOS_Ecom_QueryOwnership (passes through to Epic servers)
  → Real servers respond: "you don't own this"
  → EpicFix's hook on the callback modifies the result
  → Game receives: "you own everything"
```

**Technique B: Callback wrapping** (more common)
```
Game calls EOS_Ecom_QueryOwnership with a callback
  → EpicFix intercepts the call
  → Registers its OWN callback with the real SDK
  → When real SDK calls back with "not owned"
  → EpicFix's callback rewrites ownership status to EOS_Owner = 1
  → Then calls the game's original callback with the modified result
```

Real auth, real server call, real network traffic all happen normally. The answer is edited on the way back.

### The GraphQL-Powered "Unlock All"

Unlike Epic Unlocker (which uses a static INI list of DLC IDs), EpicFix **dynamically discovers** all entitlements by querying Epic's GraphQL catalog endpoint at runtime. The `UnlockAllEntitlements` flag triggers this scan, which fetches up to 1000 offers for the configured namespace and fakes ownership of every item found.

### 8.4 How EpicFix Players Can Sometimes Join Real Players

Not all multiplayer traffic goes through EOS. Several scenarios allow cross-play between fix users and legitimate players:

**Direct IP / LAN — Game bypasses EOS for gameplay**: Most EOS games only use Epic's services for matchmaking and relay (NAT punch-through). Once the game session starts, the netcode often switches to direct UDP sockets between players. At that point, there's no EOS in the loop.

**Real auth token**: Because EpicFix obtains a genuine Epic auth token via OAuth Device Authorization, the game has a valid session. Servers that check "does this token resolve to a valid Epic account?" will accept the connection.

**Crossplay titles (Steam + EOS)**: EpicFix's Steam ↔ EOS translation layer maps Steam identity to EOS identity. The shared dedicated server accepts both EOS-authenticated and Steam-authenticated players.

**Peer-hosted with no client validation**: In peer-hosted games (Deep Rock Galactic, Satisfactory, etc.), the host doesn't verify ownership of joining clients.

| Scenario | Can join real players? | Reason |
|---|---|---|
| Peer-hosted, no client validation | **Yes** | Host is just a player, doesn't re-check ownership |
| Dedicated server, validates ownership at join only | **Yes** | EpicFix already spoofed it at auth time |
| Dedicated server, validates ownership periodically | **Maybe** | Depends if EpicFix's hook is still active when the server re-queries |
| Server validates EOS entitlement **token** (not just ownership bool) | **No** | Tokens are cryptographically signed by Epic — cannot be forged |
| EOS relay-only (no direct IP) | **No** | All traffic goes through Epic's relay, which knows you don't own the game |
| Game uses EOS Anti-Cheat | **No** | Anti-cheat validates the real entitlement chain |

### 8.5 The Entitlement Token Wall — The Shared Ceiling

Both Epic Unlocker and EpicFix hit the same hard limit: **cryptographic entitlement tokens**.

Some games don't just call `EOS_Ecom_QueryOwnership` — they call `EOS_Ecom_QueryEntitlements`, which returns **signed entitlement tokens**. These are JWT-like blobs signed by Epic's private key. The game can verify the signature locally using Epic's public key.

| What the tool can do | What it cannot do |
|---|---|
| Flip `EOS_Owner = 1` in the callback | Forge a valid entitlement token (requires Epic's private key) |
| Modify the ownership boolean in memory | Create a token that passes signature verification |
| Hook the callback to change the response | Defeat local cryptographic validation of the token itself |

This is the definitive barrier for both Epic Unlocker and EpicFix. No amount of callback hooking gets past cryptographic verification of signed tokens.

### 8.6 Deep Decompilation Details

#### OAuth Device Authorization (RFC 8628)

EpicFix performs a **real Epic OAuth Device Authorization flow** — it actually authenticates with Epic's servers:

| Step | Action | Endpoint |
|---|---|---|
| 1 | Build POST to request device code | `POST /epic/oauth/v2/deviceAuthorization` with `client_id` + `scope` |
| 2 | Parse JSON response | Extract `device_code` and `verification_uri_complete` |
| 3 | Launch browser for user login | `cmd.exe /c start "link" "%s"` — opens verification URL |
| 4 | Polling loop (60 attempts, 5s interval) | `POST /epic/oauth/v1/token` with `grant_type=device_code` |
| 5a | Handle `authorization_pending` | Keep polling |
| 5b | Handle `invalid_grant` | Fail |
| 5c | Handle `scope_consent_required` | Fail |
| 6 | On success | Capture `refresh_token`, store for later use |

**Key implication:** EpicFix obtains a **real Epic auth token**. The user actually logs in to Epic.

#### Exact OAuth endpoints

| Operation | Endpoint | Method |
|---|---|---|
| Request device code | `https://api.epicgames.dev/epic/oauth/v2/deviceAuthorization` | POST |
| Poll for token | `https://api.epicgames.dev/epic/oauth/v1/token` | POST |

**Polling loop**: 60 attempts, 5-second interval between each.

**Error handling** (exact substrings checked in response):
- `oauth.authorization_pending` → keep polling
- `oauth.invalid_grant` → fail, stop polling
- `oauth.scope_consent_required` → fail, stop polling

**Browser launch**: `cmd.exe /c start "link" "%s"` with the `verification_uri_complete` URL.

**Authorization header**: Built using a custom Base64 encoder with standard alphabet (`A-Za-z0-9+/`) to create `Authorization: Basic <base64(client_id:client_secret)>`.

#### GraphQL Catalog Scanner

When `UnlockAllEntitlements` is enabled in the INI, EpicFix queries Epic's GraphQL API to discover all available DLC/offer IDs:

```graphql
{ Catalog { catalogOffers(namespace: "<namespace>", params: {count: 1000}) { elements { items { id } } } } }
```

Sent to `https://graphql.epicgames.com` with a **Chrome User-Agent** to avoid WAF rejection. Response parsed with regex-like pattern: `"id":"(.*?)"` to extract offer IDs.

#### Steam API Translation Layer

EpicFix **dynamically loads** `steamclient64.dll` and resolves Steam API functions:

| Steam Function | Purpose |
|---|---|
| `SteamAPI_ISteamUser_GetSteamID` | Get Steam user ID → map to EOS product user ID |
| `SteamAPI_ISteamFriends_GetPersonaName` | Get Steam display name → supply to EOS `EOS_Connect` |
| `SteamAPI_ISteamFriends_SetRichPresence` | Set Steam Rich Presence from EOS presence data |
| `SteamAPI_ISteamFriends_GetFriendRichPresence` | Read Steam friend presence → supply to EOS |
| `SteamAPI_ISteamFriends_RequestFriendRichPresence` | Request presence updates from Steam |

**Versioned interfaces** (pinned for compatibility):
- `SteamUser023` — user identity, Steam ID
- `SteamFriends017` — friends list, rich presence, persona name

This version pinning ensures compatibility across Steam SDK updates — if Valve bumps the interface version, EpicFix still resolves the specific one it was built against.

#### INI Parser

Hand-written parser (not Windows `GetPrivateProfileString`):

- **Whitespace bitmask**: `0x100002600` — tests for space, tab, newline, carriage return, and form feed in a single operation
- **Multi-line values**: Supports `<<< ... >>>` syntax (triple-angle delimiters)
- **String quoting**: Strips surrounding double quotes from values (`"value"` → `value`)

**ScopeFlags config mapping** (exact offsets):

| INI Key | Global Offset | Bit Position |
|---|---|---|
| `BasicProfile` | `dword_1800C3C08` | bit 0 |
| `Email` | `dword_1800C3C08` | bit 1 |
| `FriendsList` | `dword_1800C3C08` | bit 2 |
| `Presence` | `dword_1800C3C08` | bit 3 |
| `Country` | `dword_1800C3C0C` | bit 0 |
| `FriendsManagement` | `dword_1800C3C0C` | bit 1 |

#### Path Hash Algorithm (Bernstein Variant)

Used for integrity verification — ensures the DLL is running from the expected game directory.

- **Seed**: `0x700602`
- **Algorithm** (for each character `c` in the exe's full path): `hash = c + 33 * hash`
- **Comparison**: Result compared against the content of `<GameExecutable>.hash` file in the same directory

This is a variant of **djb2** (Daniel J. Bernstein hash), modified with a non-standard seed.

#### Version Targeting & User-Agent Spoofing

EpicFix sends specific `User-Agent` strings:

```
User-Agent: EOS-SDK/1.16.1 (Windows/10.0.19041.2788.64bit) App/1.0
```

For GraphQL requests, it uses a **Chrome user-agent** to avoid rejection by Epic's WAF/rate limiting. This indicates it targets **EOS SDK version 1.16.1** specifically.

#### Heap Allocator Implementation

- **Private heap**: Created via `HeapCreate` → stored in `qword_1800C3210`
- **Alignment**: All allocations rounded up to **32 bytes** (for AVX/SSE SIMD alignment)
- **Large allocation header**: For allocations > 0x1000 bytes, stores the original `HeapAlloc` pointer at `(return_address - 8)` so `HeapFree` can recover the base address
- **STL integration**: Overrides `std::allocator` — evidenced by `"bad allocation"` exception strings

#### Thread-Local Storage (TLS)

- **Access pattern**: `NtCurrentTeb()->ThreadLocalStoragePointer[dword_1800C37F8]`
- **Purpose**: Per-thread state for `std::locale` / error context
- **Thread-safe singleton**: Implements a thread-safe accessor for the `ISteamUser` interface using the TLS slot
- **Spinlock**: `_InterlockedCompareExchange` on `dword_1800C3208` guards the global module hash map

#### Core Data Structure Layouts

**Red-Black Tree Node** (used for INI map, entitlement cache):

| Offset | Field |
|---|---|
| 0 | `left` child pointer |
| 8 | `right` child pointer |
| 16 | `parent` pointer |
| 24 | `color` (red/black) |
| 32 | `key` (`std::string`) |
| 56 | `value` (`std::string`) |

**`std::string` SSO Layout** (MSVC):

| Offset | Field |
|---|---|
| 0 | Buffer pointer (or inline data if length ≤ 15) |
| 8 | Size |
| 16 | Capacity |
| 24 | Inline data buffer (SSO storage) |

#### Startup Sequence (11 Steps)

| Step | Action |
|---|---|
| 1 | VM unpack — opcode interpreter decrypts/mutates real code into executable memory |
| 2 | Initialize INI `std::map` (red-black tree) |
| 3 | Read `EpicFix.ini` from disk |
| 4 | Parse sections → populate global config variables |
| 5 | If `NoAuth == 0`, run OAuth Device Authorization flow (RFC 8628) |
| 6 | Load `steamclient64.dll` via `LoadLibrary` |
| 7 | Resolve all Steam API functions via `GetProcAddress` |
| 8 | Log into Steam (`SteamUser023` interface) |
| 9 | If `UnlockAllEntitlements == 1`, run GraphQL catalog query |
| 10 | Inject hooks into EOS SDK vtables |
| 11 | Enter combined Steam/EOS message loop |

**Anti-debug artifact**: The `icebp` instruction (`0xF1`) in `sub_18005B5A4` is a dead code path the VM never executes. Deliberate trap for disassemblers — `icebp` is an undefined opcode that triggers a debug exception if stepped through.

**Runtime code mutation**: The obfuscation is not just a packer — it's a runtime code mutation engine. The opcode interpreter loop processes 16-bit opcodes via `switch` cases with `rcl`/`rcr` bit rotations. The clean functions visible in the decompilation only exist in memory after the VM interpreter has decrypted/mutated them.

#### atexit Cleanup Registry (20+ Functions)

EpicFix registers cleanup routines for all subsystems. Each frees its associated global state — dynamic arrays for Steam callbacks, red-black trees for GraphQL catalog results, vectors for OAuth polling state, `std::string` objects for all config values. This is **production-quality cleanup** — no memory leaks on unload. The DLL can be safely injected and uninjected without leaking heap memory.

#### Edge Cases and Fallbacks

| Scenario | Behavior |
|---|---|
| HTTP timeout | Retries up to **3 times** with **60-second** timeout per attempt |
| Missing INI file | Falls back to placeholder defaults (e.g., `ProductId = "0"`) |
| Steam not running | Graceful fail — logs `"Failed to initialize Steam"`, does **not** crash the host process |
| Expired OAuth token | **Automatically re-triggers** the full OAuth Device Authorization flow |
| `NoAuth = 1` in INI | Skips OAuth entirely — no auth token obtained, some features may not work |

#### Stack Protection

- **Security cookie**: `__security_cookie` pattern — XORed with stack frame address to detect buffer overflows
- **GS handler**: `_GSHandlerCheckCommon` registered as global SEH handler for structured exception handling
- **Exception logging**: Catches `std::exception` and unknown exceptions, prints `"unknown exception"` or `"bad cast"` before terminating — rudimentary crash reporting

---

## 9. Three-Way Comparison: Epic Unlocker vs OnlineFix vs EpicFix

### 9.1 Full Dimension Comparison

| Dimension | Epic Unlocker | OnlineFix | EpicFix |
|---|---|---|---|
| **Purpose** | Unlock DLC/achievements on legitimate accounts | Bypass EAC + Ecom emulation | Bridge EOS ↔ Steam for cross-play |
| **Injection method** | DLL proxy of `eossdk-win64-shipping.dll` | `winmm.dll` proxy → preloader → patched SDK + OnlineFix64.dll | DLL proxy via `winmm.dll` → loads EpicFix64.dll |
| **EOS interaction** | Hooks inside real EOS SDK (MinHook) | Patches SDK binary + VMProtected Ecom emulator | Hooks specific EOS functions, translates to Steam |
| **Auth** | Real Epic auth — real token, real account | Real Epic OAuth (WebView2 via EGSAuthLauncher) | Real Epic OAuth Device Auth (RFC 8628, browser login) |
| **EAC handling** | None (relies on cert files) | SDK patches (func1/func2) + universal cert files | None needed (targets non-EAC games) |
| **Ownership scope** | DLC entitlements (static INI list) | All DLC (local stub) | Base game + all DLC (dynamic via GraphQL scan) |
| **DLC discovery** | Manual INI configuration | N/A (unlocks all) | Automatic GraphQL query to Epic's catalog |
| **Steam integration** | None | None | **Full bidirectional layer** — friends, presence, identity mapping |
| **Multiplayer** | Does not touch multiplayer | Pass-through (real SDK handles) | Emulates lobbies + P2P locally |
| **Achievements** | Hooks queries + unlocks, uses game's auth (EACMode) | Pass-through — real SDK handles | Pass-through — real SDK handles |
| **Stats** | Force-ingest via `EOS_Stats_IngestStat` | Pass-through | Pass-through or skipped |
| **Friends** | Untouched | Untouched | **Translated** — Steam friends ↔ EOS friends |
| **Presence** | Untouched | Untouched | **Translated** — EOS presence ↔ Steam Rich Presence |
| **Game specificity** | **Generic** — same DLL works for all EOS games | **Generic** — patches + cert files work across games | **Game-specific** — `ProductId`, `SandboxId`, `DeploymentId` per game |
| **Obfuscation** | Open source | VMProtect (777/797 functions packed) | VMProtect/Themida (lightly packed — 474/480 readable) |
| **GUI** | Named pipe → EpicGUI (Tauri + React) | No GUI — fire and forget | No GUI — fire and forget (browser pop-up for auth) |
| **Risk profile** | Flagging on tracking sites (unlocks hit real servers) | Anonymous platform — no tracking | Real auth token obtained, ownership spoofed |
| **Targeted SDK version** | Agnostic — hooks whatever SDK the game ships | Targets SDK 1.18.1.2 specifically | Targets EOS SDK 1.16.1 specifically |
| **Entitlement token limit** | Cannot forge signed tokens | Cannot forge signed tokens | Cannot forge signed tokens |

### 9.2 What All Three Tools Share

1. **DLL proxy injection** — Same concept, different entry points. Epic Unlocker proxies `eossdk-win64-shipping.dll` directly; OnlineFix and EpicFix proxy `winmm.dll` as a loader.

2. **EOS export interception** — All three sit between the game and EOS. Same API surface: `EOS_Ecom_*`, `EOS_Achievements_*`, `EOS_P2P_*`, `EOS_Lobby_*`.

3. **Ownership spoofing via callback hooking** — All three intercept the ownership query callback and modify `EOS_Owner` before the game sees it. Epic Unlocker adds DLC; OnlineFix returns "owned" locally; EpicFix adds base game + all DLC (via GraphQL).

4. **Config-driven** — `ScreamAPI.ini` / `OnlineFix.ini` / `EpicFix.ini`. All configure what to spoof/skip.

5. **Same EOS SDK domain knowledge** — All require understanding the exact EOS SDK API surface, struct layouts, and callback semantics.

6. **Same hard limit** — None can forge cryptographically signed entitlement tokens.

7. **Real Epic authentication** — All ultimately rely on real Epic auth. Epic Unlocker uses the game's existing auth flow; OnlineFix uses EGSAuthLauncher (WebView2); EpicFix performs its own OAuth Device Authorization flow.

8. **HTTP communication with Epic servers** — Epic Unlocker lets the real SDK handle HTTP; OnlineFix and EpicFix make direct HTTP/GraphQL calls to `api.epicgames.dev` and `graphql.epicgames.com`.

### 9.3 Injection Method Comparison

| | Epic Unlocker | OnlineFix | EpicFix |
|---|---|---|---|
| **Proxy DLL** | `eossdk-win64-shipping.dll` | `winmm.dll` → preloader | `winmm.dll` |
| **Interception granularity** | Only EOS calls | EOS calls + SDK binary patches | Every `winmm` call forwarded + EOS hooks loaded |
| **Cleanliness** | Targeted — only intercepts EOS | Patches SDK + VMProtected Ecom stub | Broader — hooks multimedia DLL to load, then hooks EOS internally |
| **Detection surface** | Smaller — game only loads one proxy | Medium — `winmm.dll` proxy + SDK patches | Larger — `winmm.dll` proxy is a well-known crack signature |
| **Universal?** | Only works for EOS games | Works for EOS games with EAC | Works for any game that links `winmm.dll` (nearly all Windows games) |

Epic Unlocker's approach is cleaner because it only intercepts EOS calls. The `winmm.dll` trick (used by both OnlineFix and EpicFix) is older and more detectable but works universally since every Windows game links `winmm.dll`.

---

## 10. Security & Trust Considerations

### 10.1 Epic Unlocker (our project)

| Aspect | Status |
|---|---|
| Source visibility | **Fully open source** — every hook and callback is auditable |
| Auth handling | Uses game's existing auth, never touches tokens directly |
| Network calls | Only through the real EOS SDK |
| Data collection risk | **None** — no telemetry, no phone-home |
| Auditability | Build from source to verify binary matches |

### 10.2 OnlineFix

| Aspect | Status |
|---|---|
| Source visibility | **Closed + heavily VMProtected** — 777/797 functions packed, only 20 visible |
| Auth handling | Real Epic OAuth via EGSAuthLauncher (WebView2) |
| Network calls | VMProtected — could exfiltrate tokens, cannot verify |
| Data collection risk | **Unknown** — VM code could exfiltrate OAuth refresh token to third-party servers |
| Auditability | Cannot reproduce — no source, VMProtected payload is opaque |
| Anti-analysis | Anti-VM (QEMU/VirtualBox/VMware detection), anti-debug (infinite loop trap) |

### 10.3 EpicFix

#### What We Can Verify

- The OAuth flow is **standard RFC 8628** — it sends the user to Epic's real domain for authentication. No credential harvesting is visible in the decompiled clean functions.
- The GraphQL query goes to Epic's real endpoint (`graphql.epicgames.com`).
- Steam API calls use standard `steamclient64.dll` exports — no custom Steam auth intercept.
- Cleanup is thorough (20+ `atexit` handlers) — suggests production-quality development, not a quick hack.

#### What We Cannot Verify (Due to Obfuscation)

- **The VM-mutated code** — ~70% of the DLL's logic is generated at runtime by the opcode interpreter. We can see the dispatcher, but not what it dispatches. There could be:
  - Token exfiltration (sending the OAuth `refresh_token` to a third-party server)
  - Telemetry/analytics collection
  - Additional hooks not visible in the clean decompiled functions
- **The `winmm.dll` proxy** — it forwards calls to the real `winmm`, but the forwarding table could skip or modify specific exports.
- **Anti-tamper** — the path hash check and VM mutation make it difficult to patch or audit the DLL at runtime.

### 10.4 Trust Model Comparison

| Aspect | Epic Unlocker | OnlineFix | EpicFix |
|---|---|---|---|
| Source visibility | **Fully open source** | **Closed + VMProtected** | **Closed + lightly packed** (474/480 readable) |
| Auth handling | Uses game's existing auth, never touches tokens | Real OAuth via EGSAuthLauncher | Performs own OAuth flow — obtains and stores `refresh_token` |
| Network calls | Only through real EOS SDK | VMProtected — unverifiable | Direct HTTP to Epic + GraphQL (auditable endpoints, but body/headers could be modified by VM code) |
| Data collection risk | **None** | **Unknown** | **Unknown** |
| Auditability | Build from source to verify | Cannot reproduce | Cannot reproduce |

**Our `EACNoServerMode` + `EACMode` patches provide the same Ecom bypass + achievement auth fix with clean, auditable code** — matching OnlineFix's functionality without the trust risk.

---

## 11. Composability

Epic Unlocker and EpicFix are **complementary** — they hook different subsets of the EOS API:

- **EpicFix** handles: "Can I play this game online?" (ownership of base game + lobby/P2P emulation + Steam ↔ EOS identity translation)
- **Epic Unlocker** handles: "What DLC/achievements do I have?" (DLC entitlements + achievement hooks + stat force-ingest)

Because they hook non-overlapping EOS functions (aside from ownership, where they can coexist since both say "owned"), **they can be stacked**: EpicFix for online access, Epic Unlocker for achievement management. The real EOS SDK handles everything else.

**Important caveat for stacking**: Since EpicFix performs its own OAuth flow and obtains its own auth token, Epic Unlocker running on top will use whichever auth context is established. If the game uses EpicFix's token for achievement queries, Epic Unlocker's achievement hooks will operate on that token's context. This should work correctly as long as the token has the required scopes.

OnlineFix, by contrast, is **not composable** — it patches the SDK binary directly and replaces the Ecom path entirely. Stacking Epic Unlocker on top of OnlineFix would be redundant (both bypass Ecom) and could conflict.

---

*End of analysis. This document is the consolidated reference for all three EOS unlocker variants.*
