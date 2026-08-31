# nEOS Technical Analysis & Cross-Tool Comparison

> Analysis of nEOS v1.3.1-alpha source code, compared against EpicFix (IDA decompilation) and Epic Unlocker (active project). Focus: what each tool teaches us and how Epic Unlocker can benefit.

---

## 1. nEOS Overview

nEOS is a lightweight EOS SDK emulator/proxy DLL by infogram. It has two modes:

- **Emulator mode** (default): Replaces the EOS SDK entirely. Fakes all EOS responses from config. No real network calls. Good for single-player games that just check ownership.
- **Proxy mode** (since v0.5): Loads the real EOS SDK DLL (renamed to `_o.dll`), forwards most calls to it, but intercepts ownership/entitlement queries to inject DLC unlocks. This is the mode relevant to Epic Unlocker.

nEOS is open source (Apache 2.0), ~115 unique EOS function implementations across SDK versions 1.1.0/1.2.0/1.3.0, and has been battle-tested since Control v1.04.

---

## 2. What nEOS Teaches Us: GameAllocMemoryFunc

### 2.1 The Problem

The EOS SDK contract allows games to provide custom memory allocators through `EOS_InitializeOptions`:

```cpp
typedef void* (EOS_MEMORY_CALL *EOS_AllocateMemoryFunc)(size_t SizeInBytes, size_t Alignment);
typedef void  (EOS_MEMORY_CALL *EOS_ReleaseMemoryFunc)(void* Pointer);
```

Unreal Engine (and most EOS games) passes its own allocator (UE's `FMalloc`) via these callbacks. The EOS SDK then uses this allocator for **all memory it returns to the game** — entitlement structs, ownership arrays, string buffers, everything.

When a proxy DLL allocates memory that the game will later free, it **must** use the same allocator. Otherwise:

| Scenario | What happens |
|----------|-------------|
| Proxy allocates with `new`, game frees with `FMalloc` | **Heap corruption** — cross-allocator free crashes or silently corrupts the heap |
| Proxy allocates with `malloc`, game frees with `FMalloc` | Same problem — different heap, different bookkeeping |
| Proxy allocates with `GameAllocMemoryFunc`, game frees with `FMalloc` | **Correct** — same allocator, same heap |

### 2.2 How nEOS Solves It

nEOS captures the allocator at init time in `eos_init.cpp`:

```cpp
// Global pointer, set once during EOS_Initialize
EOS_AllocateMemoryFunc GameAllocMemoryFunc = nullptr;

// Inside hooked EOS_Initialize:
if (Options->AllocateMemoryFunction)
    GameAllocMemoryFunc = Options->AllocateMemoryFunction;
else
    log(LL::Warning, "Game didn't provide memory-allocation function!");
```

Then uses it via a helper whenever it needs to allocate memory that the game will own:

```cpp
inline char* TryAllocMemory(size_t Size) {
    if (GameAllocMemoryFunc)
        return static_cast<char*>(GameAllocMemoryFunc(Size, 1));
    if (ForcedDLCUseMalloc)          // INI fallback for edge cases
        return static_cast<char*>(malloc(Size));
    return nullptr;                  // Fail gracefully
}
```

This is used in `EOS_Ecom_QueryOwnership` to allocate string buffers for **forced DLC IDs** — DLCs the game didn't originally ask about but nEOS injects into the ownership results. Since the game will eventually free these strings (via `EOS_Ecom_ItemOwnership` release), they must be allocated with the game's own allocator.

### 2.3 Why This Matters for Any EOS Proxy

**Side effects of capturing GameAllocMemoryFunc: essentially zero.** You're saving a function pointer the game already gave you. The only theoretical risk is calling the allocator from the wrong thread — but since you'd only call it from within EOS callbacks (which run on the game's own thread/context), this is safe. nEOS has done this for years without issues.

**Side effects of NOT capturing it:** Real bugs. Here's where Epic Unlocker **had** problems (now fixed):

#### Bug 1 (FIXED): `MakeEntitlement()` used `new` — wrong allocator

```cpp
// OLD Epic Unlocker code (before fix):
auto* e = new EOS_Ecom_Entitlement{};   // CRT heap, not game's heap
e->EntitlementId   = id.c_str();         // DANGLING POINTER (see Bug 2)
e->CatalogItemId   = id.c_str();         // DANGLING POINTER
e->EntitlementName = title.c_str();      // DANGLING POINTER
```

**Fixed.** Current code uses `GameAlloc::Allocate(sizeof(EOS_Ecom_Entitlement), alignof(...))` with placement new, and `GameAlloc::CopyString()` for each string. Both use the game's captured allocator (or `_aligned_malloc` CRT fallback when the game provides no allocator).

#### Bug 2 (FIXED): Dangling string pointers

`id.c_str()` and `title.c_str()` pointed into `std::string` parameters that were destroyed after `MakeEntitlement` returned. The `char*` fields in the entitlement struct would dangle.

**Fixed.** Current code copies strings into game-allocator-owned buffers via `GameAlloc::CopyString()`. The `char*` fields point to independently allocated buffers that outlive the function call.

#### Bug 3 (FIXED): `Ecom_Entitlement_Release` used `delete` — wrong deallocator

```cpp
// OLD Epic Unlocker code (before fix):
delete Entitlement;       // CRT delete, wrong allocator
```

**Fixed.** Current code frees each string buffer individually with `GameAlloc::Release()`, then frees the struct itself with `GameAlloc::Release()`. All four frees use the same allocator that was used for the corresponding allocation.

### 2.4 The Fix Pattern (from nEOS) — Now Implemented in Epic Unlocker

Epic Unlocker now implements this pattern via the `GameAlloc` namespace in `eos_intercept.h`:

1. **Capture** `AllocateMemoryFunction`, `ReleaseMemoryFunction`, `ReallocateMemoryFunction` during `EOS_Initialize` via `GameAlloc::CaptureFromOptions(Options)`
2. **Allocate** all game-owned memory with `GameAlloc::Allocate(size, alignment)` — uses captured allocator, falls back to `_aligned_malloc` if game provided none
3. **Free** all game-owned memory with `GameAlloc::Release(ptr)` — uses captured deallocator, falls back to `_aligned_free`
4. **Copy strings** into allocator-owned buffers via `GameAlloc::CopyString(str)` — each string gets its own allocation

**Note on allocation strategy:** Separate allocations for struct + each string (4 total) is the correct and standard pattern. The real EOS SDK itself allocates strings individually in `CopyEntitlementByIndex`. A "monolithic" single-block allocation (struct + all strings in one `Allocate` call, with `char*` fields pointing to offsets within the block) is NOT required by the SDK contract. It would only be necessary if the deallocation side was out of Epic Unlocker's control — but Epic Unlocker hooks both `CopyEntitlement*` (allocation) and `Entitlement_Release` (deallocation), so it controls both sides. As long as each `Allocate` has a matching `Release`, separate allocations are perfectly safe and match what the real SDK does.

### 2.5 What About Epic Unlocker's Ownership Interception?

Epic Unlocker's `Ecom_QueryOwnership` handler has a **different** allocator situation. It doesn't allocate new ownership data — it modifies the `EOS_Ecom_ItemOwnership` array **in-place** inside the callback from the real SDK. The real SDK allocated that array with `GameAllocMemoryFunc`, so in-place modification of `OwnershipStatus` is safe (same allocator, just flipping a field). The `g_ownership_id_storage` strings pointed to by `Id` fields are static `std::vector<std::string>` globals, so they don't dangle.

**However**, if Epic Unlocker ever needs to **inject additional ownership entries** (like nEOS's `[ForcedDLC]` feature — adding DLCs the game didn't query), it would need `GameAllocMemoryFunc` to allocate the extra `EOS_Ecom_ItemOwnership` entries and their `Id` string buffers. This is the exact scenario nEOS's `TryAllocMemory` was built for.

**Verdict:** Capturing `GameAllocMemoryFunc` **fixed** real bugs in Epic Unlocker's entitlement path (now implemented) and enables future forced-DLC injection. The `GameAlloc` namespace in `eos_intercept.h` provides a clean abstraction with automatic CRT fallback — strictly better than nEOS's `TryAllocMemory` which requires a manual INI toggle (`ForcedDLCUseMalloc`) for the fallback path.

---

## 3. Other Notable nEOS Techniques

### 3.1 Forced DLC Injection

nEOS has a `[ForcedDLC]` INI section that injects ownership entries for DLCs the game **never asked about**. This is different from `[DLC]` which only marks queried items as owned.

```
[DLC]
AllOwned=1              ; Mark everything the game queries as owned

[ForcedDLC]
abc123=Season Pass      ; INJECT this into every QueryOwnership response
def456=Premium Edition  ; Even if the game didn't ask about it
```

This requires careful memory work (hence `TryAllocMemory`) because you're adding new entries to an array the game expects to free. Epic Unlocker doesn't have this — it only responds to what the game asks.

**Relevance:** Some games only query ownership of DLCs they already know about (from a local manifest). If a DLC isn't in the manifest, the game never queries it, and Epic Unlocker can't unlock it. Forced injection solves this.

### 3.2 Game Identity Override

nEOS can masquerade as a completely different game:

```ini
[Override]
ProductId=Fortnite
SandboxId=...
ClientId=...
ClientSecret=...
EncryptionKey=...
DeploymentId=...
```

Combined with command-line forgery (`-epicapp=Fortnite`), this convinces the real EOS SDK (in proxy mode) that it's serving a different title. This is how nEOS handles games with strict entitlement checks — it disguises itself as a game the user actually owns.

**Relevance:** Epic Unlocker doesn't need this (it uses the game's real identity), but it's interesting for the "make one game's DLC work on another" use case.

### 3.3 Synchronous Callback Fallback

```ini
[nEOS]
UseAsyncCallbacks=false
```

Some games crash if EOS callbacks fire asynchronously (from `EOS_Platform_Tick`). nEOS can fire all callbacks synchronously in the calling function instead. This is a game-compatibility hack that Epic Unlocker doesn't have.

**Relevance:** If Epic Unlocker ever hits timing issues with specific games, this is a proven fallback. But Epic Unlocker currently wraps callbacks (it calls the real SDK which handles timing), so this is less likely to be needed.

### 3.4 Options Struct Deep-Copying

nEOS deep-copies `EOS_Ecom_QueryOwnershipOptions` before passing it to async callbacks:

```cpp
// Some games free Options immediately after calling
EOS_Ecom_QueryOwnershipOptions options;
if (Options) {
    options = *Options;  // Deep copy the struct
    // Also deep-copy the CatalogItemIds array separately
    for (uint32_t i = 0; i < Options->CatalogItemIdCount; i++)
        catalogItemIds[i] = Options->CatalogItemIds[i];
    options.CatalogItemIds = nullptr;  // Don't use the copy for the array
}
```

This is defensive against games that free the options struct immediately after the function returns (before the async callback fires). Epic Unlocker does something similar with its `OriginalDataContainer` pattern but doesn't deep-copy the options themselves for ownership queries.

---

## 4. nEOS vs EpicFix — Detailed Comparison

### 4.1 Architecture

| Dimension | nEOS | EpicFix |
|-----------|------|---------|
| **Source** | Open source (Apache 2.0) | Closed source, VMProtect/Themida obfuscated |
| **Primary purpose** | DLC unlock via proxy/emulation | Full EOS to Steam translation shim |
| **Injection** | DLL proxy (`EOSSDK-Win64-Shipping.dll`) | DLL proxy via `winmm.dll` loader |
| **SDK interaction** | Replaces OR wraps the real EOS SDK | Hooks specific functions, passes rest through |
| **Auth** | Emulates auth (emulator mode) or passes through (proxy mode) | Real OAuth Device Authorization (RFC 8628) — obtains genuine Epic token |
| **Network** | No direct HTTP (emulator) or through real SDK (proxy) | Direct HTTP to Epic API + GraphQL catalog scanner |
| **Steam integration** | None | Full bidirectional layer (friends, presence, identity) |
| **Target SDK** | Multi-version (1.1.0, 1.2.0, 1.3.0 builds) | Targets 1.16.1 specifically |
| **Config** | `nEOS.ini` (6 sections) | `EpicFix.ini` (per-game) |

### 4.2 Ownership Spoofing Approach

| Aspect | nEOS | EpicFix |
|--------|------|---------|
| **Technique** | Replaces ownership results entirely (emulator) or intercepts callback (proxy) | Callback wrapping — wraps game's callback, modifies result before forwarding |
| **DLC discovery** | Manual INI list (`[DLC]` section) | Dynamic GraphQL catalog scan (`UnlockAllEntitlements`) |
| **Forced DLC injection** | Yes — `[ForcedDLC]` injects unqueried items | Yes — GraphQL discovers all items, injects all |
| **Memory safety** | Uses `GameAllocMemoryFunc` for injected strings | Unknown (obfuscated — ~70% of logic is VM-mutated) |
| **Base game ownership** | Configurable via `AllOwned` | Always included in GraphQL scan results |

### 4.3 What Each Does Differently

#### nEOS unique capabilities:

1. **Full EOS emulation without the real SDK** — can run games that don't have the EOS DLL at all (emulator mode)
2. **Game identity disguise** — `[Override]` section + command-line forgery
3. **Synchronous callback fallback** — `UseAsyncCallbacks=false`
4. **Multi-version SDK support** — conditional compilation for 1.1.0/1.2.0/1.3.0
5. **`GameAllocMemoryFunc` capture** — correct allocator usage for game-owned memory
6. **Proxy SDK log capture** — `ProxyHookLogs` intercepts real SDK's log output

#### EpicFix unique capabilities:

1. **Real OAuth Device Authorization** — obtains genuine Epic auth token via browser login
2. **Steam API translation layer** — `SteamUser023`, `SteamFriends017` for identity/presence bridging
3. **Friends mapping** — Steam friends appear as EOS friends (and vice versa)
4. **Presence bridging** — EOS presence maps to Steam Rich Presence
5. **GraphQL catalog scanner** — dynamically discovers all DLC/entitlements at runtime
6. **Lobby + P2P emulation** — local discovery instead of Epic servers
7. **Custom memory allocator** — 32-byte aligned SIMD-friendly private heap
8. **Path integrity check** — djb2 hash variant verifies correct game directory

#### What they share:

1. **DLL proxy injection** — both intercept EOS calls between game and SDK
2. **Ownership spoofing via callback hooking** — both modify ownership status before the game sees it
3. **Config-driven** — INI files control behavior
4. **Same entitlement token ceiling** — neither can forge cryptographically signed tokens
5. **Real Epic authentication** — both ultimately rely on real auth (nEOS in proxy mode, EpicFix via OAuth)

### 4.4 Trust Analysis

| Aspect | nEOS | EpicFix |
|--------|------|---------|
| **Auditability** | Fully open source — every line visible | ~30% visible (VM-mutated code is opaque) |
| **Auth safety** | Passes through game's own auth (proxy) or fakes it (emulator) | Performs its own OAuth — stores `refresh_token` |
| **Network calls** | Through real SDK (proxy) or none (emulator) | Direct HTTP to `api.epicgames.dev` + `graphql.epicgames.com` |
| **Data exfiltration risk** | **None** — no network code of its own | **Unknown** — VM code could do anything |
| **Obfuscation** | None | VMProtect/Themida — prevents static analysis |

---

## 5. How Both Can Benefit Epic Unlocker

### 5.1 From nEOS (High Priority)

| Feature | Benefit to Epic Unlocker | Difficulty |
|---------|---------------------|------------|
| **`GameAllocMemoryFunc` capture** | Fixes heap corruption bugs in `MakeEntitlement`/`Ecom_Entitlement_Release`. Enables future forced-DLC injection. | **Low** — ~20 lines of code in `eos_initialize.cpp` + `eos_intercept.cpp` |
| **Forced DLC injection** | Unlock DLCs the game doesn't know about (no manifest entry = no query = Epic Unlocker can't touch it). | **Medium** — requires allocator fix first, then injection logic in `Ecom_QueryOwnership` callback |
| **Options struct deep-copy** | Defensive against games that free `Options` immediately after calling. Prevents use-after-free in async callbacks. | **Low** — copy the struct and arrays at call time |
| **Synchronous callback fallback** | Fix for games that crash with async EOS callbacks. | **Low** — add a config flag, skip the `NEOS_AddCallback` queue |
| **SDK version conditional compilation** | Support games using older EOS SDK versions (1.1.0, 1.2.0) where struct layouts differ. | **Medium** — need version detection + `#ifdef` blocks |

### 5.2 From EpicFix (Medium Priority)

| Feature | Benefit to Epic Unlocker | Difficulty |
|---------|---------------------|------------|
| **GraphQL catalog scanner** | Auto-discover all DLC IDs for a namespace — no manual INI configuration needed. Epic Unlocker already has `dlc_catalog.cpp` which does this; EpicFix's approach validates the technique. | **Already partially done** — `dlc_catalog.cpp` fetches from ScreamDB. Could add direct Epic GraphQL as a fallback source. |
| **OAuth Device Auth flow** | Obtain real Epic auth token independently of the game's auth flow. Useful for authenticated API access (rarity DB, real ownership checks). | **Medium** — standard RFC 8628 implementation, but requires UI (browser popup) |
| **Steam presence bridging** | Show EOS game status on Steam profile. Niche feature for dual-platform users. | **Low** (if Steam is available) but **Low priority** |
| **Path integrity check** | Verify Epic Unlocker is running in the correct game directory, prevent misconfiguration. | **Low** — djb2 variant, few lines of code |
| **Custom 32-byte aligned allocator** | SIMD-friendly allocations for Epic Unlocker's own internal use. | **Very Low** but **Very Low priority** — Epic Unlocker doesn't do SIMD work |

### 5.3 Priority Roadmap

```
Immediate (fixes real bugs):
  [1] Capture GameAllocMemoryFunc + GameReleaseMemoryFunc in EOS_Initialize
  [2] Fix MakeEntitlement to use game allocator + copy strings
  [3] Fix Ecom_Entitlement_Release to use game deallocator
  [4] Deep-copy Options structs in async callbacks

Short-term (new capabilities):
  [5] Forced DLC injection (needs [1] first)
  [6] Synchronous callback fallback config option

Long-term (nice-to-have):
  [7] Direct Epic GraphQL catalog query as fallback DLC source
  [8] OAuth Device Auth for authenticated API features
  [9] Path integrity verification
```

### 5.4 What We Cannot Learn From Either

| Limitation | Why |
|-----------|-----|
| **Forging signed entitlement tokens** | Cryptographic barrier — requires Epic's private key. Neither tool can do this. |
| **Solving Mojave (Tier 3, UE5.4+ static linking)** | nEOS doesn't address this. EpicFix uses `winmm.dll` injection which could theoretically reach statically-linked code via vtable hooking, but its obfuscation makes it impossible to verify. The fundamental problem (proxy DLL never sees the calls) requires a different approach: MinHook on internal function pointers or IAT patching within the game binary. |
| **EOS Anti-Cheat bypass** | Both tools operate at the SDK level. Anti-cheat operates at a lower level (kernel callbacks, integrity checks). Out of scope for SDK-level proxying. |
| **Relay-only multiplayer** | Games that route all gameplay traffic through EOS relay (no direct P2P) cannot be emulated by either tool. EpicFix emulates lobbies but not relay servers. |

---

## 6. Summary Table

| Capability | nEOS | EpicFix | Epic Unlocker (current) | Should Epic Unlocker adopt? |
|-----------|------|---------|-------------------|----------------------|
| Game allocator capture | Yes | Unknown (obfuscated) | **Yes** (`GameAlloc` namespace) | **Done** — fixed bugs |
| Correct entitlement alloc/free | Partial (uses `delete`) | Unknown | **Yes** (`GameAlloc::Allocate`/`Release`) | **Done** — fixed bugs |
| Forced DLC injection | Yes | Yes (via GraphQL) | No | **Yes** — new capability |
| Dynamic DLC discovery | No (manual INI) | Yes (GraphQL) | Partial (ScreamDB) | Already in progress |
| OAuth auth flow | No | Yes | No | Maybe (low priority) |
| Steam translation | No | Yes | No | No (out of scope) |
| Game identity disguise | Yes | No | No | No (not needed) |
| Sync callback fallback | Yes | No | No | Yes (compatibility) |
| Options deep-copy | Yes | Unknown | Partial | Yes (defensive) |
| Multi-version SDK | Yes | No (targets 1.16.1) | Partial (eos_compat) | Maybe (edge cases) |
| Path integrity check | No | Yes | No | Maybe (low priority) |
| Open source | Yes | No | Yes | N/A |

---

## 7. nEOS Code Validity vs EOS SDK v1.18

> nEOS targets EOS SDK ~1.3.0 (circa 2019-2020). Epic Unlocker targets v1.18 (2024). Is nEOS's 6-year-old code still correct?

### 7.1 Allocator Typedefs — Identical

Both old (nEOS) and new (Epic Unlocker v1.18) headers define the exact same function signatures:

```c
// nEOS (EOS SDK ~1.3.0) AND Epic Unlocker (EOS SDK v1.18) — identical
typedef void* (EOS_MEMORY_CALL *EOS_AllocateMemoryFunc)(size_t SizeInBytes, size_t Alignment);
typedef void* (EOS_MEMORY_CALL *EOS_ReallocateMemoryFunc)(void* Pointer, size_t SizeInBytes, size_t Alignment);
typedef void  (EOS_MEMORY_CALL *EOS_ReleaseMemoryFunc)(void* Pointer);
```

`EOS_MEMORY_CALL` is `__stdcall` on x86, nothing on x64 — identical in both headers. The `(size, alignment)` parameter order is unchanged. **nEOS's `GameAllocMemoryFunc(Size, 1)` call pattern is correct for v1.18.**

### 7.2 `EOS_InitializeOptions` — v3 → v4 (backwards-compatible)

| Field | nEOS (v3, `EOS_INITIALIZE_API_LATEST=3`) | Epic Unlocker v1.18 (v4, `EOS_INITIALIZE_API_LATEST=4`) |
|-------|---------------------------------------------|-----------------------------------------------------|
| ApiVersion | int32_t | int32_t (same position) |
| AllocateMemoryFunction | ptr | ptr (same position) |
| ReallocateMemoryFunc | ptr | ptr (same position) |
| ReleaseMemoryFunction | ptr | ptr (same position) |
| ProductName | const char* | const char* (same position) |
| ProductVersion | const char* | const char* (same position) |
| Reserved | void* | void* (same position) |
| SystemInitializeOptions | void* | void* (same position) |
| **OverrideThreadAffinity** | **MISSING** | `EOS_Initialize_ThreadAffinity*` **NEW (appended)** |

The v4 struct has one new field **appended at the end**. All existing field offsets are identical. Reading `Options->AllocateMemoryFunction` from a v4 struct using the v4 header works perfectly. nEOS's capture pattern is safe — it only reads `AllocateMemoryFunction`, which hasn't moved.

### 7.3 `EOS_Ecom_Entitlement` — Field changes

| Field | nEOS (~v1.3.0) | Epic Unlocker v1.18 | Status |
|-------|----------------|-----------------|--------|
| ApiVersion | int32_t | int32_t | Identical |
| **Id** (was `EntitlementId`) | `EOS_Ecom_EntitlementId` | Renamed → `EntitlementName` (`EOS_Ecom_EntitlementName`) | **Semantic change** |
| **InstanceId** | `EOS_Ecom_EntitlementInstanceId` | **REMOVED** | **Removed** |
| CatalogItemId | `EOS_Ecom_CatalogItemId` | `EOS_Ecom_CatalogItemId` | Identical |
| ServerIndex | int32_t | int32_t | Identical |
| bRedeemed | EOS_Bool | EOS_Bool | Identical |
| EndTimestamp | int64_t | int64_t | Identical |

Both versions use `EOS_ECOM_ENTITLEMENT_API_LATEST = 2`, but the struct behind that version number is different. The first `const char*` field changed semantic meaning from entitlement instance ID to entitlement name, and the second `const char*` field (`InstanceId`) was removed entirely.

**Impact on Epic Unlocker:** Epic Unlocker's current `MakeEntitlement` uses the v1.18 field names (`EntitlementName`, `EntitlementId`, `CatalogItemId`) — correct for v1.18. nEOS never actually populates entitlement structs (it only does ownership), so this change doesn't affect nEOS's code.

### 7.4 `EOS_Ecom_ItemOwnership` — Identical

Both versions define the same struct with `EOS_ECOM_ITEMOWNERSHIP_API_LATEST = 1`:

```c
int32_t ApiVersion;
EOS_Ecom_CatalogItemId Id;           // const char*
EOS_EOwnershipStatus OwnershipStatus; // int32_t enum
```

No changes in 6 years. nEOS's ownership interception code is fully compatible with v1.18.

### 7.5 `EOS_Ecom_QueryOwnership` Callback Data — Compatible

nEOS's `QueryOwnership` callback sets fields on `EOS_Ecom_QueryOwnershipCallbackInfo` and `EOS_Ecom_ItemOwnership`. Both structs are ABI-compatible between nEOS's SDK headers and v1.18. The ownership interception pattern (modify `OwnershipStatus` in-place or inject new entries) works identically.

### 7.6 nEOS Implementation Bugs Found During Validation

While the *patterns* are valid, nEOS's actual implementation has bugs that Epic Unlocker has already surpassed:

| Bug | nEOS | Epic Unlocker |
|-----|------|-----------|
| `Entitlement_Release` uses `delete` instead of game deallocator | Yes — `delete Entitlement` | Fixed — uses `GameAlloc::Release` |
| Null-allocator fallback requires manual INI toggle | Yes — `ForcedDLCUseMalloc` must be set by user | Better — automatic `_aligned_malloc`/`_aligned_free` fallback |
| Only saves `AllocateMemoryFunc`, not `ReleaseMemoryFunc` | Yes — no way to free with game's deallocator | Correct — saves all three (Alloc/Free/Realloc) |
| Alignment parameter hardcoded to 1 | Yes — `GameAllocMemoryFunc(Size, 1)` | Better — passes actual `alignof(T)` |

### 7.7 Verdict

| Aspect | Valid for v1.18? | Notes |
|--------|-------------------|-------|
| Allocator typedef signatures | Yes | Identical in both headers |
| `EOS_InitializeOptions` field offsets | Yes | New v4 field appended at end |
| `EOS_Ecom_ItemOwnership` layout | Yes | Unchanged in 6 years |
| `EOS_Ecom_QueryOwnership` callback pattern | Yes | ABI-compatible |
| `EOS_Ecom_Entitlement` layout | No (changed) | Field renamed, field removed — but nEOS doesn't use this struct anyway |
| nEOS's `TryAllocMemory` concept | Yes | But Epic Unlocker's `GameAlloc` implementation is strictly better (auto fallback, alignment, saves all 3 function pointers) |
| nEOS's `Entitlement_Release` implementation | No (buggy) | Uses `delete` — Epic Unlocker already fixed this |