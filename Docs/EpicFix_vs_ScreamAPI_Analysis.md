# EpicFix vs ScreamAPI — Technical Analysis

> Findings from reverse-engineering, decompilation (IDA Pro Hex-Rays), and behavioral comparison, 2026-08-16

---

## 1. What EpicFix Actually Is — Corrected from Decompilation

EpicFix is a **full EOS ↔ Steam translation shim** — not just an ownership spoofer. It is a binary-level bridge that intercepts EOS API calls and translates them to Steam API calls, enabling EOS-dependent games to work with Steam's infrastructure (and vice versa). It is commonly used for **cross-play mods** and **launcher-agnostic play**, not just "cracking."

### File Structure

| File | Role |
|------|------|
| `winmm.dll` | **DLL loader proxy** — Windows loads `winmm.dll` before the game's own code. This fake copy forwards all real `winmm` calls to the system DLL via `GetProcAddress`, but also loads EpicFix.dll in `DllMain`. Drop-in injection, no launcher needed. |
| `EpicFix.dll` | **EOS ↔ Steam translation shim** — hooks specific EOS functions, translates them to Steam API calls, performs real Epic OAuth, and spoofs ownership. Heavily obfuscated (VMProtect/Themida code virtualization). |
| `EpicFix.ini` | **Config** — game-specific settings: `Platform`, `ProductId`, `ClientId`, `ClientSecret`, `SandboxId`, `DeploymentId`, `ScopeFlags`, `UnlockAllEntitlements`. |

### Decompilation Notes

- The decompiled output shows `_bswap`, `ROLB`, `RRCB`, `_bit_scan_reverse` patterns — this is **VMProtect or Themida code virtualization**. The visible functions are the VM dispatcher.
- The later functions (`sub_1800B09C8`, `sub_1800B0FD8`, `sub_1800B4998`) decompiled cleanly because the unpacked/decrypted business logic resides there.
- Uses **Microsoft C++ STL** heavily (`std::string`, `std::vector`, `std::map`/`std::set` — evidenced by exception strings like `"vector too long"`, `"map/set too long"`).
- Uses **WinHTTP/WinINet** APIs for networking (implied by HTTP header strings and `User-Agent`).
- Custom JSON parsing via regex-like patterns (`"id":"(.*?)"`).

---

## 2. EpicFix Architecture — Decompiled Subsystems

The decompilation reveals EpicFix is organized into **multiple subsystems**, each with its own `std::set`/`std::map` stored in globals. Initialization registers cleanup routines for each:

### 2.1 OAuth Device Authorization (RFC 8628)

EpicFix performs a **real Epic OAuth Device Authorization flow** — it actually authenticates with Epic's servers:

| Step | Action | Endpoint |
|------|--------|----------|
| 1 | Build POST to request device code | `POST /epic/oauth/v2/deviceAuthorization` with `client_id` + `scope` |
| 2 | Parse JSON response | Extract `device_code` and `verification_uri_complete` |
| 3 | Launch browser for user login | `cmd.exe /c start "link" "%s"` — opens verification URL |
| 4 | Polling loop (60 attempts, 5s interval) | `POST /epic/oauth/v1/token` with `grant_type=device_code` |
| 5a | Handle `authorization_pending` | Keep polling |
| 5b | Handle `invalid_grant` | Fail |
| 5c | Handle `scope_consent_required` | Fail |
| 6 | On success | Capture `refresh_token`, store for later use |

**Key implication:** EpicFix obtains a **real Epic auth token**. The user actually logs in to Epic. This is why achievements work, why ScreamAPI can hook on top, and why some games can join real players — the account is genuinely authenticated.

### 2.2 GraphQL Catalog Scanner (Unlock All Entitlements)

When `UnlockAllEntitlements` is enabled in the INI, EpicFix queries Epic's GraphQL API to discover **all available DLC/offer IDs**:

```graphql
{ Catalog { catalogOffers(namespace: "<namespace>", params: {count: 1000}) { elements { items { id } } } } }
```

It parses the response with a regex (`"id":"(.*?)"`) to extract every offer ID, then uses these IDs to fake ownership of **all DLC** to the EOS SDK. This is the "unlock all" feature — it scans the entire catalog rather than hardcoding DLC IDs.

### 2.3 Steam API Translation Layer

EpicFix **dynamically loads** `steamclient64.dll` and resolves Steam API functions:

| Steam Function | Purpose |
|---|---|
| `SteamAPI_ISteamUser_GetSteamID` | Get Steam user ID → map to EOS product user ID |
| `SteamAPI_ISteamFriends_GetPersonaName` | Get Steam display name → supply to EOS `EOS_Connect` |
| `SteamAPI_ISteamFriends_SetRichPresence` | Set Steam Rich Presence from EOS presence data |
| `SteamAPI_ISteamFriends_GetFriendRichPresence` | Read Steam friend presence → supply to EOS |
| `SteamAPI_ISteamFriends_RequestFriendRichPresence` | Request presence updates from Steam |

These are stored in global function pointers and called whenever the EOS SDK asks for user/profile data. This is a **bidirectional translation layer** — EOS presence maps to Steam Rich Presence and vice versa.

### 2.4 Friends Mapping (Steam ↔ EOS)

When `FriendsList=True` in the INI, EpicFix maps Steam friends to EOS friends. The game's `EOS_Friends_*` calls are translated to `SteamFriends_*` calls, so the game sees Steam friends as EOS friends.

### 2.5 Presence Bridging

EOS presence calls are translated to **Steam Rich Presence**. When the game sets EOS presence (e.g., "In Game — Level 3"), EpicFix calls `SetRichPresence` on Steam, making your Steam profile show your Epic game status.

### 2.6 INI Configuration Parser

EpicFix includes a **hand-written INI parser** (not Windows API `GetPrivateProfileString`) that:

- Skips whitespace and comments (`#` and `;`)
- Parses `[Section]` headers
- Parses `Key = Value` pairs (with quoted strings and `<<<` multi-line value support)
- Stores results in a `std::map`-like tree (red-black tree nodes)

Config keys extracted:
- `Platform`, `ProductId`, `ClientId`, `ClientSecret`, `SandboxId`, `DeploymentId`
- `ScopeFlags` (bitmask: `BasicProfile`, `Email`, `FriendsList`, `Presence`, `Country`, `FriendsManagement`)
- `Misc` → `UnlockAllEntitlements`

### 2.7 Path Hashing / Integrity Check

Functions `sub_1800B55F8` and `sub_1800B5968` compute a **custom rolling checksum** of the `.exe`'s full path (magic constant `0x700602`), then compare against an expected value stored in a `.hash` file next to the `.exe`.

Purpose:
- Verify the DLL is running from the correct game directory
- Possibly prevent tampering or ensure the correct game version

### 2.8 Version Targeting & User-Agent Spoofing

EpicFix sends specific `User-Agent` strings:

```
User-Agent: EOS-SDK/1.16.1 (Windows/10.0.19041.2788.64bit) App/1.0
```

For GraphQL requests, it uses a **Chrome user-agent** to avoid rejection by Epic's WAF/rate limiting.

This indicates it targets **EOS SDK version 1.16.1** specifically and pretends to be a legitimate game client.

### 2.9 Custom Memory Allocators

The DLL contains custom `malloc`/`free` wrappers that:
- Align allocations to **32-byte boundaries** (for SIMD)
- Use a kernel32 heap handle for allocation
- Override STL's `std::allocator` (evident from `"bad allocation"` strings)

### 2.10 Thread Safety

Uses `_InterlockedCompareExchange` and `_InterlockedExchange` as a **spinlock** on `dword_1800C3208` to guard critical sections (e.g., modifying the global hash map of loaded modules). Also uses `__security_cookie` pattern for stack canaries (buffer overflow protection).

---

## 3. Corrected Architecture: EpicFix is a Bidirectional Translation Middleware

**Evolution of understanding:**
1. *Initial assumption:* EpicFix replaces the entire EOS SDK — no real Epic servers contacted. *(Incorrect)*
2. *First correction (from live testing):* EpicFix is a partial middleware — hooks only specific EOS functions, passes rest through. *(Partially correct)*
3. *Full correction (from decompilation):* EpicFix is a **bidirectional EOS ↔ Steam translation shim** that performs real Epic OAuth, translates Steam API calls to EOS and vice versa, queries Epic's GraphQL catalog to discover entitlements, and spoofs ownership via callback hooking.

### EOS Feature Handling Matrix (Updated)

| EOS Feature | EpicFix | ScreamAPI |
|---|---|---|
| **Auth** (`EOS_Connect_*`, `EOS_Auth_*`) | **Real OAuth Device Auth flow** — obtains genuine Epic token via browser login | Untouched — real Epic auth |
| **Ownership** (`EOS_Ecom_QueryOwnership`) | **Hooked** — callback result flipped to "owned" (base game + all DLC via GraphQL scan) | **Hooked** — adds DLC entitlements to real ownership results |
| **Entitlements** (`EOS_Ecom_QueryEntitlements`) | Cannot forge signed tokens | Cannot forge signed tokens |
| **Lobbies** (`EOS_Lobby_*`) | **Hooked/emulated** — local discovery instead of Epic servers | Untouched — real lobbies |
| **P2P** (`EOS_P2P_*`) | **Hooked/emulated** — local sockets instead of Epic relay | Untouched — real P2P |
| **Achievements** (`EOS_Achievements_*`) | Pass-through — real SDK handles it | **Hooked** — intercepts queries + unlock notifications |
| **Stats** (`EOS_Stats_*`) | Pass-through or skipped | **Hooked** — force-ingest via `EOS_Stats_IngestStat` |
| **Friends** (`EOS_Friends_*`) | **Translated** — Steam friends ↔ EOS friends (when enabled) | Untouched |
| **Presence** (`EOS_Presence_*`) | **Translated** — EOS presence ↔ Steam Rich Presence | Untouched |

---

## 4. How EpicFix Spoofs Ownership with the Real SDK Running

EpicFix does **not** replace the EOS SDK — it only intercepts the ownership query callback. Two known techniques:

### Technique A: Post-call hook

```
Game calls EOS_Ecom_QueryOwnership(...)
  → EpicFix's hook fires first
  → EpicFix calls the real EOS_Ecom_QueryOwnership (passes through to Epic servers)
  → Real servers respond: "you don't own this"
  → EpicFix's hook on the callback modifies the result
  → Game receives: "you own everything"
```

### Technique B: Callback wrapping

```
Game calls EOS_Ecom_QueryOwnership with a callback
  → EpicFix intercepts the call
  → Registers its OWN callback with the real SDK
  → When real SDK calls back with "not owned"
  → EpicFix's callback rewrites ownership status to EOS_Owner = 1
  → Then calls the game's original callback with the modified result
```

Technique B is more common — you don't touch the query itself, you just wrap the callback. Real auth, real server call, real network traffic all happen normally. The answer is edited on the way back.

### The GraphQL-Powered "Unlock All"

Unlike ScreamAPI (which uses a static INI list of DLC IDs), EpicFix **dynamically discovers** all entitlements by querying Epic's GraphQL catalog endpoint at runtime. The `UnlockAllEntitlements` flag triggers this scan, which fetches up to 1000 offers for the configured namespace and fakes ownership of every item found.

---

## 5. How EpicFix Players Can Sometimes Join Real (Legitimate) Players

Not all multiplayer traffic goes through EOS. Several scenarios allow cross-play between fix users and legitimate players:

### 5.1 Direct IP / LAN — Game Bypasses EOS for Actual Gameplay

Most EOS games only use Epic's services for **matchmaking** (finding a lobby) and **relay** (NAT punch-through). Once the game session starts, the netcode often switches to **direct UDP sockets** between players.

```
EOS role:      "Find lobby" → "Exchange host IP" → "Connect"
                                      ↑
                              This handoff is where
                              the real & fix worlds merge
```

Once the game has the host's IP, it connects directly. UDP packets don't go through EOS — they use normal OS networking. At that point, **there's no EOS in the loop**, and the game's own netcode usually doesn't re-check ownership.

### 5.2 Real Auth Token from OAuth Device Flow

Because EpicFix obtains a **genuine Epic auth token** via the OAuth Device Authorization flow, the game has a valid session. Servers that check "does this token resolve to a valid Epic account?" will accept the connection. The server may never independently verify ownership — it trusts the client-side SDK's response, which EpicFix has already spoofed.

### 5.3 Crossplay Titles (Steam + EOS)

Many Epic titles are also on Steam with crossplay. EpicFix's **Steam ↔ EOS translation layer** is specifically designed for this scenario — it maps Steam identity to EOS identity. The shared dedicated server accepts both EOS-authenticated and Steam-authenticated players.

### 5.4 Peer-Hosted with No Client Validation

In peer-hosted games (Deep Rock Galactic, Satisfactory, etc.), the host doesn't verify ownership of joining clients. The host is a player, not Epic's infrastructure — they have no way to check if the joiner really owns the game.

### 5.5 When It Fails

| Scenario | Can join real players? | Reason |
|---|---|---|
| Peer-hosted, no client validation | **Yes** | Host is just a player, doesn't re-check ownership |
| Dedicated server, validates ownership at join only | **Yes** | EpicFix already spoofed it at auth time |
| Dedicated server, validates ownership periodically | **Maybe** | Depends if EpicFix's hook is still active when the server re-queries |
| Server validates EOS entitlement **token** (not just ownership bool) | **No** | Tokens are cryptographically signed by Epic — cannot be forged |
| EOS relay-only (no direct IP) | **No** | All traffic goes through Epic's relay, which knows you don't own the game |
| Game uses EOS Anti-Cheat | **No** | Anti-cheat validates the real entitlement chain |

---

## 6. The Entitlement Token Wall — The Shared Ceiling

Both tools hit the same hard limit: **cryptographic entitlement tokens**.

Some games don't just call `EOS_Ecom_QueryOwnership` — they call `EOS_Ecom_QueryEntitlements`, which returns **signed entitlement tokens**. These are JWT-like blobs signed by Epic's private key. The game can verify the signature locally using Epic's public key.

| What the tool can do | What it cannot do |
|---|---|
| Flip `EOS_Owner = 1` in the callback | Forge a valid entitlement token (requires Epic's private key) |
| Modify the ownership boolean in memory | Create a token that passes signature verification |
| Hook the callback to change the response | Defeat local cryptographic validation of the token itself |

This is the definitive barrier for both ScreamAPI and EpicFix. No amount of callback hooking gets past cryptographic verification of signed tokens.

---

## 7. ScreamAPI vs EpicFix — Full Comparison

| Dimension | ScreamAPI | EpicFix |
|---|---|---|
| **Purpose** | Unlock DLC/achievements on legitimate accounts | Bridge EOS ↔ Steam for cross-play, ownership spoofing, and launcher-agnostic play |
| **Injection method** | DLL proxy of `eossdk-win64-shipping.dll` | DLL proxy via `winmm.dll` → loads fix DLL |
| **EOS interaction** | Hooks inside the real EOS SDK (MinHook on real functions) | Hooks specific EOS functions, translates to Steam API, passes rest through to real SDK |
| **Auth** | Real Epic auth — real token, real account | **Real Epic OAuth Device Auth** — obtains genuine token via browser login flow |
| **Ownership scope** | DLC entitlements (static list from INI) | Base game + all DLC (dynamic via GraphQL catalog scan) |
| **DLC discovery** | Manual INI configuration | Automatic GraphQL query to Epic's catalog |
| **Steam integration** | None | **Full bidirectional layer** — friends, presence, identity mapping |
| **Multiplayer** | Does not touch multiplayer | Emulates lobbies + P2P locally |
| **Achievements** | Hooks achievement queries + unlock notifications, talks to real EOS service | Pass-through — real SDK handles it |
| **Stats** | Can force-ingest stats via real `EOS_Stats_IngestStat` | Pass-through or skipped |
| **Friends** | Untouched | **Translated** — Steam friends ↔ EOS friends |
| **Presence** | Untouched | **Translated** — EOS presence ↔ Steam Rich Presence |
| **Game specificity** | **Generic** — same DLL works for all EOS games | **Game-specific** — `ProductId`, `SandboxId`, `DeploymentId` per game |
| **Obfuscation** | Open source | VMProtect/Themida (code virtualization) |
| **GUI** | Named pipe → EpicGUI (Tauri + React) for real-time status | No GUI — fire and forget (browser pop-up for auth) |
| **Risk profile** | Flagging on tracking sites (Exophase, TrueAchievements) because unlocks hit real servers | Real auth token obtained, but ownership spoofed — lower detection risk for achievements, but still unauthorized |
| **Targeted SDK version** | Agnostic — hooks whatever SDK the game ships | Targets EOS SDK 1.16.1 specifically |
| **Entitlement token limit** | Cannot forge signed tokens | Cannot forge signed tokens |

---

## 8. What Both Tools Share

1. **DLL proxy injection** — Same concept, different entry point. ScreamAPI proxies `eossdk-win64-shipping.dll` directly; EpicFix proxies `winmm.dll` as a loader.

2. **EOS export interception** — Both sit between the game and EOS. Same API surface: `EOS_Ecom_*`, `EOS_Achievements_*`, `EOS_P2P_*`, `EOS_Lobby_*`.

3. **Ownership spoofing via callback hooking** — Both intercept the ownership query callback and modify `EOS_Owner` before the game sees it. ScreamAPI adds DLC; EpicFix adds base game + all DLC (via GraphQL).

4. **Config-driven** — `ScreamAPI.ini` vs `EpicFix.ini`. Both configure what to spoof/skip.

5. **Same EOS SDK domain knowledge** — Both require understanding the exact EOS SDK API surface, struct layouts, and callback semantics.

6. **Same hard limit** — Neither can forge cryptographically signed entitlement tokens.

7. **Real Epic authentication** — Both ultimately rely on real Epic auth. ScreamAPI uses the game's existing auth flow; EpicFix performs its own OAuth Device Authorization flow.

8. **HTTP communication with Epic servers** — ScreamAPI lets the real SDK handle HTTP; EpicFix makes direct HTTP/GraphQL calls to `api.epicgames.dev` and `graphql.epicgames.com`.

---

## 9. Composability

ScreamAPI and EpicFix are **complementary** — they hook different subsets of the EOS API:

- **EpicFix** handles: "Can I play this game online?" (ownership of base game + lobby/P2P emulation + Steam ↔ EOS identity translation)
- **ScreamAPI** handles: "What DLC/achievements do I have?" (DLC entitlements + achievement hooks + stat force-ingest)

Because they hook non-overlapping EOS functions (aside from ownership, where they can coexist since both say "owned"), **they can be stacked**: EpicFix for online access, ScreamAPI for achievement management. The real EOS SDK handles everything else.

**Important caveat for stacking:** Since EpicFix performs its own OAuth flow and obtains its own auth token, ScreamAPI running on top will use whichever auth context is established. If the game uses EpicFix's token for achievement queries, ScreamAPI's achievement hooks will operate on that token's context. This should work correctly as long as the token has the required scopes.

---

## 10. Injection Method Comparison

| | ScreamAPI | EpicFix |
|---|---|---|
| **Proxy DLL** | `eossdk-win64-shipping.dll` | `winmm.dll` |
| **Interception granularity** | Only EOS calls | Every `winmm` call forwarded + EOS hooks loaded |
| **Cleanliness** | Targeted — only intercepts EOS | Broader — hooks multimedia DLL to load, then hooks EOS internally |
| **Detection surface** | Smaller — game only loads one proxy | Larger — `winmm.dll` proxy is a well-known crack signature |
| **Universal?** | Only works for EOS games | Works for any game that links `winmm.dll` (nearly all Windows games) |

ScreamAPI's approach is cleaner because it only intercepts EOS calls. The `winmm.dll` trick is older and more detectable but works universally since every Windows game links `winmm.dll`.

---

## 11. Implications for ScreamAPI / EpicGUI Development

Based on these findings, several features in ScreamAPI could be informed by EpicFix's approach:

### 11.1 Dynamic DLC Discovery (from EpicFix's GraphQL Scanner)

ScreamAPI currently requires manual DLC ID configuration in `ScreamAPI.ini`. EpicFix's approach — querying `graphql.epicgames.com` to discover all offer IDs for a namespace — could be adopted to auto-populate the DLC list. This would eliminate the need for users to manually find and enter DLC IDs.

### 11.2 OAuth Device Auth Flow

EpicFix's RFC 8628 implementation shows it's possible to obtain a real Epic auth token outside the game's own auth flow. This could be useful for ScreamAPI features that need authenticated API access (e.g., the planned egdata rarity feature, or checking real ownership status before spoofing).

### 11.3 Steam Presence Bridging

For users who play on both platforms, translating EOS presence to Steam Rich Presence (as EpicFix does) could be a useful EpicGUI feature — showing EOS game status on the user's Steam profile.

### 11.4 Path/Integrity Verification

EpicFix's path hashing mechanism could inform a similar integrity check in ScreamAPI to ensure the DLL is running in the correct game context, preventing misconfiguration.

### 11.5 What ScreamAPI Has That EpicFix Doesn't

- **Real-time GUI** — EpicGUI provides live achievement status, unlock notifications, and a stat-gated achievement view. EpicFix has no UI.
- **Stat-gated achievement force-unlock** — ScreamAPI can force-ingest stats via `EOS_Stats_IngestStat` to unlock achievements with stat requirements. EpicFix doesn't touch stats.
- **Achievement pipe protocol** — The named pipe between ScreamAPI and EpicGUI enables real-time bi-directional communication. EpicFix is entirely one-way (inject and forget).
- **Open source** — ScreamAPI's behavior is auditable. EpicFix's VMProtect makes it impossible to fully verify what the DLL does (e.g., does it phone home? does it collect tokens?).

---

## 12. EpicFix Internals — Deep Decompilation Details

> From IDA Pro Hex-Rays decompilation of EpicFix.dll. Low-level implementation details that inform the architectural understanding above.

### 12.1 Runtime Code Mutation (VM Unpack)

The obfuscation is not just a packer — it's a **runtime code mutation engine**:

- `sub_180054399` is the **opcode interpreter loop** that processes 16-bit opcodes via `switch` cases `1`, `2`, `3`, and `10` with `rcl`/`rcr` bit rotations.
- **Purpose**: Writes the actual business logic into memory *after* the DLL loads, bypassing static analysis. The clean functions visible in the decompilation (`sub_1800B09C8` etc.) only exist in memory after the VM interpreter has decrypted/mutated them.
- **Anti-debug artifact**: The `icebp` instruction (`0xF1`) in `sub_18005B5A4` is a **dead code path** the VM never executes. Deliberate trap for disassemblers — `icebp` is an undefined opcode that triggers a debug exception if stepped through.

### 12.2 Startup Sequence (11 Steps)

The exact boot process from `DllMain` to message loop:

| Step | Action |
|------|--------|
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

### 12.3 OAuth Device Authorization — Exact Endpoints

| Operation | Endpoint | Method |
|-----------|----------|--------|
| Request device code | `https://api.epicgames.dev/epic/oauth/v2/deviceAuthorization` | POST |
| Poll for token | `https://api.epicgames.dev/epic/oauth/v1/token` | POST |

**Polling loop**: 60 attempts, 5-second interval between each.

**Error handling** (exact substrings checked in response):
- `oauth.authorization_pending` → keep polling
- `oauth.invalid_grant` → fail, stop polling
- `oauth.scope_consent_required` → fail, stop polling

**Browser launch**: `cmd.exe /c start "link" "%s"` with the `verification_uri_complete` URL.

**Authorization header**: Built using a **custom Base64 encoder** (`sub_1800ABAD8`) with standard alphabet (`A-Za-z0-9+/`) to create `Authorization: Basic <base64(client_id:client_secret)>`.

### 12.4 GraphQL Catalog Query — Exact Body

```json
{
  "query": "{ Catalog { catalogOffers(namespace: \"<namespace>\", params: {count: 1000}) { elements { items { id } } } } }"
}
```

Sent to `https://graphql.epicgames.com` with a **Chrome User-Agent** to avoid WAF rejection.

Response parsed with regex-like pattern: `"id":"(.*?)"` to extract offer IDs.

### 12.5 Steam API — Versioned Interfaces

EpicFix resolves **specific Steam interface versions** (not just the latest):

- `SteamUser023` — user identity, Steam ID
- `SteamFriends017` — friends list, rich presence, persona name

This version pinning ensures compatibility across Steam SDK updates — if Valve bumps the interface version, EpicFix still resolves the specific one it was built against.

### 12.6 INI Parser — State Machine Details

Hand-written parser (not Windows `GetPrivateProfileString`):

**Whitespace bitmask**: `0x100002600` — tests for space, tab, newline, carriage return, and form feed in a single operation.

**Multi-line values**: Supports `<<< ... >>>` syntax (triple-angle delimiters).

**String quoting**: Strips surrounding double quotes from values (`"value"` → `value`).

**ScopeFlags config mapping** (exact offsets):

| INI Key | Global Offset | Bit Position |
|---------|--------------|--------------|
| `BasicProfile` | `dword_1800C3C08` | bit 0 |
| `Email` | `dword_1800C3C08` | bit 1 |
| `FriendsList` | `dword_1800C3C08` | bit 2 |
| `Presence` | `dword_1800C3C08` | bit 3 |
| `Country` | `dword_1800C3C0C` | bit 0 |
| `FriendsManagement` | `dword_1800C3C0C` | bit 1 |

**Misc key**: `UnlockAllEntitlements` stored as a boolean flag.

### 12.7 Path Hash Algorithm (Bernstein Variant)

Used for integrity verification — ensures the DLL is running from the expected game directory.

- **Seed**: `0x700602`
- **Algorithm** (for each character `c` in the exe's full path): `hash = c + 33 * hash`
- **Comparison**: Result compared against the content of `<GameExecutable>.hash` file in the same directory

This is a variant of **djb2** (Daniel J. Bernstein hash), modified with a non-standard seed.

### 12.8 Heap Allocator Implementation

- **Private heap**: Created via `HeapCreate` → stored in `qword_1800C3210`
- **Alignment**: All allocations rounded up to **32 bytes** (for AVX/SSE SIMD alignment)
- **Large allocation header**: For allocations > 0x1000 bytes, stores the original `HeapAlloc` pointer at `(return_address - 8)` so `HeapFree` can recover the base address
- **STL integration**: Overrides `std::allocator` — evidenced by `"bad allocation"` exception strings

### 12.9 Thread-Local Storage (TLS)

- **Access pattern**: `NtCurrentTeb()->ThreadLocalStoragePointer[dword_1800C37F8]`
- **Purpose**: Per-thread state for `std::locale` / error context
- **Thread-safe singleton**: `sub_1800AABB4`/`sub_1800AAC20` implement a thread-safe accessor for the `ISteamUser` interface using the TLS slot
- **Spinlock**: `_InterlockedCompareExchange` on `dword_1800C3208` guards the global module hash map

### 12.10 Core Data Structure Layouts (Memory Offsets)

**Red-Black Tree Node** (used for INI map, entitlement cache):

| Offset | Field |
|--------|-------|
| 0 | `left` child pointer |
| 8 | `right` child pointer |
| 16 | `parent` pointer |
| 24 | `color` (red/black) |
| 32 | `key` (`std::string`) |
| 56 | `value` (`std::string`) |

**`std::string` SSO Layout** (MSVC):

| Offset | Field |
|--------|-------|
| 0 | Buffer pointer (or inline data if length ≤ 15) |
| 8 | Size |
| 16 | Capacity |
| 24 | Inline data buffer (SSO storage) |

**HTTP Request Struct**:

| Offset | Field |
|--------|-------|
| 0 | Socket handle |
| 8 | Bytes sent/received |
| 16 | HTTP response code |
| 24 | Headers map (`std::map`) |
| 56 | Body (`std::string`) |

### 12.11 atexit Cleanup Registry (20+ Functions)

EpicFix registers cleanup routines for all subsystems. Each frees its associated global state:

- Dynamic array for Steam callbacks
- Red-Black tree for GraphQL catalog results
- Vector for OAuth polling state
- `std::string` objects for: Steam client path, `client_secret`, `deployment_id`, `sandbox_id`, `product_id`, `client_id`, `platform`, `namespace`, EOS host, User-Agent, verification URI, GraphQL host, GraphQL User-Agent, Steam user ID, Steam DLL name, Epic Account ID, Steam persona name

This is **production-quality cleanup** — no memory leaks on unload. The DLL can be safely injected and uninjected without leaking heap memory.

### 12.12 Edge Cases and Fallbacks

| Scenario | Behavior |
|----------|----------|
| HTTP timeout | Retries up to **3 times** with **60-second** timeout per attempt |
| Missing INI file | Falls back to placeholder defaults (e.g., `ProductId = "0"`) |
| Steam not running | Graceful fail — logs `"Failed to initialize Steam"`, does **not** crash the host process |
| Expired OAuth token | **Automatically re-triggers** the full OAuth Device Authorization flow |
| `NoAuth = 1` in INI | Skips OAuth entirely — no auth token obtained, some features may not work |

### 12.13 Stack Protection

- **Security cookie**: `__security_cookie` pattern (`qword_1800C28D8`) — XORed with stack frame address to detect buffer overflows
- **GS handler**: `_GSHandlerCheckCommon` registered as global SEH handler for structured exception handling
- **Exception logging**: Catches `std::exception` and unknown exceptions, prints `"unknown exception"` or `"bad cast"` before terminating — rudimentary crash reporting

---

## 13. Security & Trust Considerations

Given the decompilation findings, there are trust-relevant observations about EpicFix:

### 13.1 What We Can Verify

- The OAuth flow is **standard RFC 8628** — it sends the user to Epic's real domain for authentication. No credential harvesting is visible in the decompiled clean functions.
- The GraphQL query goes to Epic's real endpoint (`graphql.epicgames.com`).
- Steam API calls use standard `steamclient64.dll` exports — no custom Steam auth intercept.
- Cleanup is thorough (20+ `atexit` handlers) — suggests production-quality development, not a quick hack.

### 13.2 What We Cannot Verify (Due to Obfuscation)

- **The VM-mutated code** — ~70% of the DLL's logic is generated at runtime by the opcode interpreter. We can see the dispatcher, but not what it dispatches. There could be:
  - Token exfiltration (sending the OAuth `refresh_token` to a third-party server)
  - Telemetry/analytics collection
  - Additional hooks not visible in the clean decompiled functions
- **The `winmm.dll` proxy** — it forwards calls to the real `winmm`, but the forwarding table could skip or modify specific exports.
- **Anti-tamper** — the path hash check and VM mutation make it difficult to patch or audit the DLL at runtime.

### 13.3 Comparison with ScreamAPI's Trust Model

| Aspect | ScreamAPI | EpicFix |
|--------|-----------|---------|
| Source visibility | **Fully open source** — every hook and callback is auditable | **Closed + obfuscated** — only clean functions visible, VM-mutated logic opaque |
| Auth handling | Uses game's existing auth, never touches tokens directly | Performs own OAuth flow — obtains and stores `refresh_token` |
| Network calls | Only through the real EOS SDK | Direct HTTP to Epic + GraphQL (auditable endpoints, but body/headers could be modified by VM code) |
| Data collection risk | **None** — no telemetry, no phone-home | **Unknown** — VM code could exfiltrate data before clean functions execute |
| Auditability | Build from source to verify binary matches | Cannot reproduce — no source, no way to verify binary matches any particular intent |
