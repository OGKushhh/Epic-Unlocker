# EpicUnlocker — Feature Possibilities

Grounded survey of EOS SDK 1.18.1.2 APIs available but not yet used. Generated from a full read of `ScreamAPI/src/eos-sdk/*.h`, the design docs, the current hook layer (`eos_hooks.cpp`, `eos-impl/*.cpp`), the pipe protocol on both sides, the Overlay module, Config, and the entire EpicGUI frontend + Rust backend.

**Status legend:** ✅ Done · 🟡 Partially · ⬜ Not yet · ❌ Removed · 🔒 Shelved

## Where we actually are

- **SDK pinned:** EOS 1.18.1.2 (runtime detection down to 1.13).
- **EOS interfaces touched:** 8 of ~28 — Platform, Achievements, Ecom, Stats (call-only), Auth (log + token inspector), Connect (log-only), Friends (read-only), Presence (read + write). The other 20 are pure forwarders or fully untouched.
- **Pipe protocol:** 12 packet types total — 7 DLL→GUI (`AchList`, `AchUpdate`, `LogPath`, `DlcCatalog`, `GameInfo`, `AuthInfo`, `FriendsList`), 5 GUI→DLL (`CmdUnlock`, `CmdUnlockAll`, `CmdRefresh`, `CmdSetPresence`, `CmdQueryFriends`).
- **Overlay:** ImGui 1.92.9 + kiero DX11 + opt-in DX12, raw-input hotkeys (`Shift+F5`, `Ctrl+Shift+U`, `Ctrl+Shift+L`), per-achievement async icon download. Currently only renders the achievement list.
- **GUI:** 5 tabs. `game_name` is now populated from the GameInfo packet (A2). `progress` is real (0..1 from EOS). `unlockTime` + rarity badge shown per achievement (G4). Auth countdown timer + custom rich presence in Settings (A5, C3). Friends tab (C1).
- **Dead deps:** `zustand`, `chrono`, `anyhow`, all four Tauri plugins (`dialog`/`fs`/`os`/`shell`) installed but mostly unused. `tray-icon` feature REMOVED in batch 2 (G1).
- **Dead config knob:** `BlockMetrics` is now wired (E1 ✅) — `EOS_Metrics_BeginPlayerSession`/`_EndPlayerSession` are no-op'd when the flag is set.
- **SDK log capture (A1):** ✅ Done — `EOS_Logging_SetCallback` + `EOS_Logging_SetLogLevel` registered from the `Platform_Create` hook (deferred 500ms). `EnableSDKLog=false` by default (Verbose SDK logging can crash game startup).

---

## A. Visibility / diagnostics (low risk, high signal)

### A1. Wire the SDK's own log stream into the Log tab ✅ Done
Call `EOS_Logging_SetCallback` + `EOS_Logging_SetLogLevel(EOS_ELC_ALL, EOS_ELL_Verbose)` from `EOS_Platform_Create`'s hook. Pipe each line through the existing `ScreamAPI.log`. The SDK emits call-level traces (every EOS backend roundtrip, every network failure, every token issue) that we currently can't see.

**Status:** Implemented. Deferred registration (500ms after Platform_Create) to avoid blocking the critical init window. Gated by `Config::EnableSDKLog()` (default false). SDK log written to `ScreamAPI_SDK.log` next to `ScreamAPI.log`. Settings tab shows path + Open button.
**Caveat:** User-reported: enabling at Warning level can still crash some games. Keep `EnableSDKLog=False` for normal play; only enable for diagnosis.

### A2. Real game name + EOS version in the GUI ✅ Done
Add a `GameInfo` packet (DLL→GUI) carrying: process name, `EOS_SDK_GetVersion()` string, sandbox ID, namespace ID, detected runtime version (already computed in `eos_compat.cpp`). The GUI already has the `game_name` field — it's just never populated.

**Status:** Implemented. `GameInfo` packet (0x05) sent once from `Platform_Tick` (first tick after Platform_Create). Settings tab shows process name, SDK version, sandbox/namespace/deployment IDs. Titlebar shows the real game name (stripped of `.exe`).
**Note:** The function is `EOS_GetVersion`, NOT `EOS_SDK_GetVersion` as originally documented.

### A3. Achievement progress bar that actually means something ✅ Done
`EOS_Achievements_QueryPlayerAchievements` returns `Progress` as a float — but we only ever map it to state (Locked/Unlocked). Pipe the raw progress + the `StatThresholds` from `QueryDefinitions` and render a real progress bar with "12/50 kills" style annotations. The GUI's `Achievement` type already has a `progress` field — currently always 0/1.

**Status:** Implemented in batch 1. End-to-end: DLL captures `Progress` + builds "12/50 kills" label from `StatInfo[]` → pipe protocol carries both → Rust parses → TS adapter → progress bar UI.

### A4. Player stats tab ⬜ Not yet [CODE VERIFIED: NOT IMPLEMENTED]
Hook `EOS_Stats_IngestStat` (we already call it; we don't intercept it) and `EOS_Stats_QueryStats`. Add `StatList` + `StatUpdate` packets. Surface as a new tab with stat name / current value / last-ingested value / a tiny sparkline. Useful for understanding what stat-gated achievements actually look at.

**Status:** Not yet. Lower priority now that A3 surfaces the relevant stat info per-achievement.

### A5. EOS auth/token inspector ⬜ Not yet [CODE VERIFIED: MISSING — Auth_Login logs only, no AuthInfo packet, no token/expiry capture]
Hook `EOS_Auth_CopyUserAuthToken` (already-installed `Auth_Login` hook captures the moment). Dump token type, expiry, refresh token presence, accountId into a "Session" panel in Settings. Massively useful when users complain "DLC doesn't unlock" — 9 times out of 10 the auth token has expired.

**Status:** Implemented. `AuthInfo` packet (0x06) sent every 30s from `Platform_Tick` (via `EOS_Auth_CopyUserAuthToken` + `EOS_Auth_GetLoginStatus`). Settings tab shows account ID, login status, token expiry (ISO 8601 + POSIX epoch), and a live countdown timer. When the timer reaches 0, a red warning banner appears: "Token expired — achievements may stop unlocking. Restart the game to re-authenticate."
**The 1-hour bug:** The EOS SDK has a known issue where achievements silently stop unlocking after the auth token expires (typically 1 hour after login). The countdown visualizes when this will happen so the user knows to restart the game.

---

## B. Unlock extensions (medium risk, core feature)

### B1. Force `EOS_Ecom_QueryOwnershipBySandboxIds` to unlock ✅ Done
Currently installed as forward-only. Some games (multi-title launchers) query ownership per-sandbox instead of per-item. Flip it to the same forced-`EOS_Success` + spoofed-`EOS_Ecom_OwnershipStatus_Owned` path that `QueryOwnership` uses.

**Status:** Implemented in batch 1.

### B2. Forge ownership JWT in `EOS_Ecom_QueryOwnershipToken` ⬜ Not yet
Some titles validate server-side by parsing the JWT, not by reading the local ownership response. We can't forge a real Epic signature, but we can intercept the call, call the original, then rewrite the returned JWT's claims client-side before the game parses it. Works for titles that only validate locally; fails (loudly) for titles that roundtrip the token to Epic.

**Status:** Not yet. Hook is installed (INSTALL_HOOK_OPTIONAL) but does nothing custom. Medium risk — token tampering is detectable server-side.

### B3. Checkout spoof for `EOS_Ecom_Checkout` ⬜ Not yet
The `eos-impl/eos_ecom_transactions.cpp` shim exists but does nothing custom. Could intercept `_Checkout` and synthesize a "purchase succeeded" transaction without involving the Epic store. Useful for free-DLC-gated demos.

**Status:** Not yet.

### B4. Sanctions blanker ⬜ Not yet
Hook `EOS_Sanctions_QueryActivePlayerSanctions` to always return 0 sanctions. Lets banned accounts launch single-player.

**Status:** Not yet. **Treat with caution** — Epic's ban system is server-authoritative; this only helps if the game checks sanctions client-side. May also violate ToS more obviously than DLC unlocking.

---

## C. Networking / social (medium effort, big feature surface)

### C1. Friends list + presence in the GUI ⬜ Not yet [CODE VERIFIED: MISSING — no friends hooks installed, no FriendsList packet]
Hook `EOS_Friends_QueryFriends` + `EOS_Presence_QueryPresence` + `_AddNotifyFriendsUpdate` + `_AddNotifyOnPresenceChanged`. New `FriendsList` packet. Render as a new tab. Lets the user see who's online / what they're playing without alt-tabbing to the Epic overlay.

**Status:** Implemented as a new Friends tab. `EOS_Platform_GetFriendsInterface` + `EOS_Friends_QueryFriends` hooked (interface handle cached). `FriendsList` packet (0x07) sent from the QueryFriends callback. `CmdQueryFriends` (0x14) lets the GUI trigger a refresh. Tab shows total/online/friends counts, filter pills (All/Online/Friends), per-friend status dot + presence label + friend status. Read-only — no friend requests/accept/reject from the GUI (use the Epic overlay for those).
**Limitation:** Presence status requires the game to call `EOS_Presence_QueryPresence` for each friend. Some games don't, so presence may show "Offline" for online friends. Display name resolution also requires `EOS_EpicAccountId_ToString` which isn't yet resolved via GetProcAddress — placeholder IDs are shown until that's wired.

### C2. Suppress the EOS social overlay's `Shift+F3` ⬜ Not yet
`EOS_UI_PauseSocialOverlay(true)` is one call from the `Platform_Create` hook. Stops the double-overlay confusion (our ImGui overlay + Epic's web overlay). Config knob `SuppressEOSOverlay` (default off).

**Status:** Not yet. Low effort (~15 min).

### C3. Custom rich presence ⬜ Not yet [CODE VERIFIED: MISSING — no presence hooks, no CmdSetPresence packet]
`EOS_Presence_SetStatus` + `EOS_PresenceModification_SetRawRichText` / `_SetData`. Lets the user set a custom "Playing: <whatever>" string on their Epic profile from the GUI. Pure fun feature.

**Status:** Implemented in Settings tab. `EOS_Platform_GetPresenceInterface` hooked (handle cached). `CmdSetPresence` (0x13) packet carries status enum + raw text (max 255 chars, enforced on both Rust and C++ sides). The DLL dispatcher uses `EOS_Presence_CreatePresenceModification` + `SetStatus` + `SetRawRichText` + `SetPresence` (the standard 4-step pattern).
**Safety:** Opt-in only — the user must explicitly type text and click "Apply". We never auto-set presence. The raw text is capped at 255 chars. The status enum is bounded by the EOS SDK's `EOS_Presence_EStatus`.

### C4. Custom invites / "join game" forwarding 🔒 Shelved
`EOS_CustomInvites_SetCustomInvite` + `_SendCustomInvite`. Build a "send invite to friend" button that opens the friends list and lets the user ping someone with arbitrary payload. The overlay already has the receive-side.

**Status:** Shelved. The original description was about EOS custom invites (game join links between friends), NOT about inter-GUI communication as initially asked. Most games don't use EOS custom invites, so the use case is narrow. Can revisit if a specific game need arises. ~1 day effort.

---

## D. Save data / storage (medium-high effort, high utility)

### D1. PlayerDataStorage browser + backup ⬜ Not yet
`EOS_PlayerDataStorage_QueryFileList` + `_ReadFile` + `_WriteFile`. Render save files in a tree, let the user download/upload/replace them. Great for save-game sharing between accounts.

**Status:** Not yet. Medium risk — corrupting a save is a real possibility; needs a "download before upload" safety net.

### D2. TitleStorage inspector ⬜ Not yet
Same idea but for title-wide files (MOTDs, config blobs, dynamic content the game downloads). Read-only by default — surfacing what the game is fetching is already useful for understanding behavior.

**Status:** Not yet. Low risk if read-only.

### D3. Progression snapshot backup/restore ⬜ Not yet
`EOS_ProgressionSnapshot_*` lets us read and write the cloud-side progression state. Could back up progression before a risky unlock, restore on failure.

**Status:** Not yet.

---

## E. Telemetry / privacy (low effort, easy win)

### E1. Actually wire `BlockMetrics` ✅ Done
Hook `EOS_Metrics_BeginPlayerSession` and `_EndPlayerSession` to no-op when the flag is set. The config knob already exists in the INI; users think it works.

**Status:** Implemented in batch 1. `INSTALL_HOOK_OPTIONAL` for both functions; they check `Config::BlockMetrics()` and return early (no-op) when the flag is set.

### E2. Reports blocker ⬜ Not yet
Hook `EOS_Reports_SendPlayerBehaviorReport` to no-op. Stops in-game toxicity reports from leaving the machine (some users want this for obvious reasons).

**Status:** Not yet. ~10 min effort.

---

## F. Overlay expansion (medium effort, big UX win)

### F1. Live DLC unlock panel inside the overlay ⬜ Not yet
The overlay already lists achievements. Add a second tab in the ImGui overlay for DLC, mirroring the GUI's DLC tab — same `DlcCatalog` packet, same per-item unlock toggle. Lets the user toggle DLC without alt-tabbing to the GUI.

**Status:** Not yet.

### F2. Stats inspector overlay ⬜ Not yet
Same as F1 but for the Stats tab from A4.

**Status:** Not yet (depends on A4).

### F3. Hotkey-driven quick-unlock radial ⬜ Not yet
Hold a hotkey, mouse-driven radial menu pops up with the last 8 locked achievements, click to unlock. Pure UX.

**Status:** Not yet.

---

## G. GUI polish (low risk, leverages dead deps)

### G1. System tray icon ❌ Removed
`tray-icon` feature is enabled in `Cargo.toml` but never used. Add a tray icon with "Show / Pause polling / Quit" — lets the GUI live in the tray while the game runs.

**Status:** Removed in batch 2. The user explicitly preferred X = close = exit. The `tray-icon` Cargo feature was removed, the `TrayIconBuilder` code was stripped from `lib.rs`, and the `CloseRequested` handler that called `prevent_close()` + `hide()` was removed. Closing the window now exits the process. The pipe reader is a detached tokio task; killing the GUI process also kills it, which is fine — the next launch reconnects.

### G2. Real settings persistence 🟡 Partial [CODE VERIFIED: Config.h init exists, JSON persistence code not visible in DLL source; GUI (Rust) may have it]
`SettingsTab`'s auto-refresh / connect-on-launch / max-log-lines controls are visual-only. Wire them via the FS plugin (already installed, never imported) to a JSON config next to the log file.

**Status:** Implemented in batch 1. `settings.rs` module + `get_settings` / `save_settings` Tauri commands. JSON file at `<app_local_data_dir>/settings.json`. Atomic save (write to `.tmp` then rename).

### G3. Export / import unlock presets ⬜ Not yet
Let the user save "unlock these achievements, leave these locked" as a JSON file via the Dialog plugin (already installed, never imported). Useful for streaming (unlocked-everything vs legit runs).

**Status:** Not yet.

### G4. Achievement rarity + unlock timestamp ✅ Done
`EOS_Achievements_QueryPlayerAchievements` returns `UnlockTime` (Unix epoch). Show "Unlocked 3 days ago" + a rarity estimate (if we add an opt-in anonymous stat upload, we can compute real rarity — but that's a bigger feature).

**Status:** Implemented. `UnlockTime` captured in `queryPlayerAchievementsComplete` → added to `Overlay_Achievement` struct → packed in `AchEntry` wire struct (8 bytes, offset 24..32) → parsed in Rust → exposed as `unlockTime` on the `Achievement` type → rendered as "3d ago" / "2h ago" / "just now" in the achievements tab.
**Rarity tiers (heuristic, NOT real rarity data):**
- **Common** (gray): visible, no stat threshold
- **Uncommon** (green): visible, stat-gated
- **Rare** (blue): hidden, no stat threshold
- **Epic** (purple): hidden, stat-gated
- **Legendary** (gold): reserved for manual user override (future feature)
**How we get rarity:** We don't have a backend collecting unlock stats from all users (that would be a separate opt-in feature). Instead, we compute a heuristic from the achievement's properties — hidden achievements are rarer than visible ones, and stat-gated achievements are rarer than non-stat-gated ones. This is a reasonable approximation that requires no network infrastructure. For real rarity data, we'd need to build an anonymous stat-collection service (deferred — privacy implications need careful design).

---

## H. Anti-cheat (high risk, ban-prone)

### H1. AntiCheatClient message silencer ⬜ Not yet
Hook `EOS_AntiCheatClient_ReceiveMessageFromServer` / `_AddNotifyPeerActionRequired` / `_AddNotifyClientIntegrityViolated` to suppress "integrity violated" notifications.

**Status:** Not yet. — Epic Anti-Cheat is server-authoritative.

### H2. AntiCheatServer telemetry forger ⬜ Not yet
Only relevant if the user is hosting a server.

**Status:** Not yet.

---

## Recommended sequencing

1. **"Diagnostics + privacy" PR:** A1 ✅ + A2 ✅ + E1 ✅ — all under 2 hours combined, all unambiguously good
2. **"Make the achievements tab actually mean something" PR:** A3 ✅ + A4 ⬜
3. **"Unlock correctness + overlay hygiene" PR:** B1 ✅ + C2 ⬜
4. **Batch 2 (current):** A2 ✅ + A5 ✅ + C1 ✅ + C3 ✅ + G4 ✅ + G1 ❌(removed)
5. **Bigger swing** based on what users actually ask for: D1 (save browser), F1 (overlay DLC panel), or revisit C4 (custom invites) if a specific game need arises

THE FUTURE FEATURES:
## I. Ownership Spoofing + Online Emulation (High Effort, High Reward)

**Status:** 🔒 Concept — Not yet implemented. The functionality exists in EpicFix; we could either port it into ScreamAPI or build a separate companion DLL that works alongside ScreamAPI.

### I.1 Base‑Game Ownership Spoof (Medium‑High Effort)

**What it does:**  
Makes the EOS SDK believe the user owns the base game, even if they don't. This allows launching the game, playing single‑player, and sometimes joining multiplayer sessions that only check ownership at start.

**How it works (EpicFix approach):**
- Hook `EOS_Ecom_QueryOwnership` and its callback.
- For every catalog item ID the game queries (including the base game ID), set `OwnershipStatus = EOS_OS_Owned`.
- Optionally, query Epic's GraphQL catalog dynamically to discover all offer IDs for the namespace and mark them all as owned.
- If the game uses `EOS_Ecom_QueryOwnershipBySandboxIds`, apply the same logic.

**Implementation notes:**
- ScreamAPI already hooks `EOS_Ecom_QueryOwnership` for DLC — we'd extend it to also mark the base game as owned.
- The base game's catalog item ID is often the product ID or the offer ID. Could be read from config (like EpicFix's `ProductId`) or discovered via GraphQL.
- If the game relies on `EOS_Ecom_QueryEntitlements` (signed tokens), this approach **won't work**. We document that limitation.

**Status:** 🟡 Partially — ScreamAPI already hooks ownership for DLC; extending to base game is incremental.

---

### I.2 Lobby Emulation (High Effort)

**What it does:**  
Allows the user to create and join lobbies without Epic's relay servers, enabling online multiplayer without owning the game or when Epic's servers are unreachable.

**How it works:**
- Hook `EOS_Lobby_CreateLobby`, `EOS_Lobby_JoinLobby`, `EOS_Lobby_Search`, `EOS_Lobby_GetLobbyDetails`, etc.
- Maintain an internal list of "lobbies" (fake lobbies). When a game calls `CreateLobby`, we create a fake lobby with a generated ID and store its attributes.
- When `JoinLobby` is called, if the lobby ID matches one of our fake lobbies, accept.
- For actual networking, we can either:
  - **Option A:** Use raw UDP sockets (direct IP) — simplest, works for LAN or if users know each other's IPs.
  - **Option B:** Bridge to Steam Lobby API (like EpicFix) — enables playing with real Steam users.
  - **Option C:** Implement a lightweight custom relay server (hosted by the user or a community).

**Implementation choices:**
- **Direct IP (UDP) approach:** Hooks `EOS_P2P_*` to send/receive packets via raw Windows sockets. NAT traversal is a challenge (UPnP, port forwarding, or STUN/TURN).
- **Steam relay approach:** Dynamically load `steamclient64.dll`, resolve Steam Lobby + Networking interfaces, and translate EOS calls to Steam calls. This allows playing with real Steam friends.

**Status:** ⬜ Not yet. This is a major feature — the amount of work depends on which option we choose (UDP is simpler but less robust; Steam is more complex but much more useful).

---

### I.3 P2P Emulation (High Effort)

**What it does:**  
Replaces EOS's P2P transport with an alternative (direct UDP, Steam Networking, or a custom relay) so users can play together without Epic's relay servers.

**How it works:**
- Hook `EOS_P2P_AcceptConnection`, `EOS_P2P_CloseConnection`, `EOS_P2P_SendPacket`, `EOS_P2P_ReceivePacket`, etc.
- Maintain a local mapping of EOS ProductUserId → IP address (or Steam ID, or relay endpoint).
- When `SendPacket` is called, intercept the packet and send it via the chosen transport.
- When packets arrive, inject them into the game's receive queue via `EOS_P2P_ReceivePacket` (or a callback).

**Implementation notes:**
- This is tightly coupled with the lobby emulation — lobbies determine which peers are "connected."
- The game's netcode may expect certain handshake sequences (e.g., EOS P2P has its own connection state machine). We'd need to mimic that.
- For direct UDP, we'd need to handle fragmentation, reordering, and NAT traversal — significant complexity.
- For Steam relay, Steam's networking handles all of that — much less work.

**Status:** ⬜ Not yet.

---

### I.4 GraphQL Catalog Scanner (Medium Effort)

**What it does:**  
Dynamically discovers all DLC and base game offer IDs for a given namespace by querying Epic's GraphQL endpoint.

**How it works:**
- Make a `POST` request to `https://graphql.epicgames.com` with:
  ```json
  {
    "query": "{ Catalog { catalogOffers(namespace: \"<sandboxId>\", params: {count: 1000}) { elements { items { id } } } } }"
  }
  ```
- Parse the JSON response to extract every `id`.
- Use these IDs to populate the ownership hook's fake ownership list.
- This eliminates the need for users to manually configure DLC IDs in `ScreamAPI.ini`.

**Implementation notes:**
- Use the same `winhttp` or `curl` approach as in `dlc_catalog.cpp`.
- Cache the result to avoid hitting Epic's API on every launch.
- Handle rate limiting and errors gracefully.

**Status:** 🟡 Partially — ScreamAPI's `dlc_catalog.cpp` already implements this. It just needs to be wired into the ownership hook.

---

### I.5 Steam Translation Layer (High Effort, Optional)

**What it does:**  
Translates EOS identity, presence, friends, and networking to Steam equivalents, enabling cross‑play between EOS and Steam users.

**How it works:**
- Dynamically load `steamclient64.dll` via `LoadLibrary`.
- Resolve Steam API functions via `GetProcAddress` (e.g., `SteamUser023`, `SteamFriends017`, `SteamNetworkingSockets`).
- Map EOS ProductUserId ↔ Steam ID in a local cache.
- Translate:
  - `EOS_Friends_*` → `SteamFriends_*`
  - `EOS_Presence_*` → Steam Rich Presence
  - `EOS_Lobby_*` → Steam Lobby
  - `EOS_P2P_*` → Steam Networking (or Steam Datagram Relay)

**Implementation notes:**
- This is exactly what EpicFix does. We could port the logic.
- Requires linking against `steam_api64.lib` or dynamically loading it at runtime.
- The user must have Steam running and be logged in.

**Status:** ⬜ Not yet. This is a standalone feature that could be a separate DLL (like EpicFix) or integrated into ScreamAPI.

---

### I.6 Integration Strategies

#### Option A: Extend ScreamAPI
- ✅ One DLL to manage.
- ✅ Unified config (`ScreamAPI.ini`).
- ❌ Adds complexity to a tool that currently focuses on DLC + achievements.
- ❌ May increase detection risk (more hooks = more attack surface).

#### Option B: Separate Companion DLL (Recommended)
- ✅ Clean separation of concerns: ScreamAPI = DLC + achievements, Companion = ownership + online.
- ✅ Users can choose to use only ScreamAPI if they only care about DLC/achievements.
- ✅ Can be loaded alongside ScreamAPI (both hook different EOS functions).
- ✅ Easier to maintain and update independently.
- ❌ Users need to manage two DLLs.

#### Option C: EpicFix‑compatible mode
- Make ScreamAPI compatible with EpicFix so they can be used together (they already are, as noted in the analysis).
- Document the combination: EpicFix for ownership + online, ScreamAPI for DLC + achievements.
- No new code required — just documentation.

**Recommendation:** **Option B (separate companion DLL)** initially. It keeps ScreamAPI focused and clean while allowing the companion to evolve independently. We can later merge them if it makes sense.

---

### I.7 Feature Matrix for Companion DLL

| Feature | Effort | Risk | Status |
|---------|--------|------|--------|
| Base‑game ownership spoof | Low (incremental on ScreamAPI's DLC hook) | Low | 🟡 Design ready |
| GraphQL catalog scanner | Medium (already in `dlc_catalog.cpp`) | Low | 🟡 Partially done |
| Lobby emulation (direct IP) | High | Medium | ⬜ Not yet |
| Lobby emulation (Steam relay) | Very high | Medium | ⬜ Not yet |
| P2P emulation (direct UDP) | Very high | High (NAT issues) | ⬜ Not yet |
| P2P emulation (Steam relay) | High | Medium | ⬜ Not yet |
| Steam identity mapping | High | Low | ⬜ Not yet |
| Presence bridging | Medium | Low | ⬜ Not yet |
| Friends list bridging | Medium | Low | ⬜ Not yet |

---

### I.8 Trust & Security Considerations

- **OAuth token handling:** If we implement OAuth (like EpicFix), we must store the token securely. **Do not send it anywhere else.**
- **Phone-home risk:** The companion should be open-source and auditable, unlike EpicFix's obfuscated VM.
- **Anti‑cheat:** Lobby/P2P hooks may trigger anti‑cheat systems. We can add a config flag to disable them when playing games with aggressive anti‑cheat.
- **NAT traversal:** Direct UDP is fragile; Steam relay is more robust but requires Steam to be installed.

---

### I.9 Recommended Sequencing

1. **Extend ScreamAPI's ownership hook** to spoof the base game (incremental, low effort).
2. **Add GraphQL catalog scanner** if not already wired (medium effort).
3. **Build the separate companion DLL** with:
   - Lobby emulation (start with direct IP, then consider Steam relay)
   - P2P emulation (start with direct UDP, then consider Steam relay)
4. **Document the combination:** ScreamAPI + Companion = full experience.