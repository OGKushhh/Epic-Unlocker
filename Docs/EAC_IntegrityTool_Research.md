# EAC Integrity Tool — Custom Certificate Generation Research

> **Status**: TODO — Research document, not yet implemented.
> 
> **Goal**: Generate our own EAC certificate files (`base.cer`, `base.bin`, `runtime.conf`) 
> from the game's own private key, instead of relying on OnlineFix's universal certs.

---

## Background

Currently, Epic Unlocker's EAC support relies on **OnlineFix's universal cert files** 
(`base.cer`, `base.bin`, `runtime.conf`) bundled in the `eos_patcher.pyw` tool. These 
cert files are "universal" — the same three files work across all confirmed games 
(Deceive Inc., The Riflemen).

The certs are genuine Epic-signed PKI material that tell EAC's loader "this SDK 
binary is authorized." Without them, EAC rejects the proxy DLL with "untrusted 
system file" and the game won't launch.

## The Opportunity

Epic ships a tool called **`anticheat_integritytool.exe`** as part of the 
`EOS_AntiCheatTools` package (v2.3.0). This tool:

1. Takes a **private key** (downloaded from the Epic Developer Portal)
2. Takes a **signed certificate** (corresponding to the private key)
3. Takes an **integrity tool config** (`anticheat_integritytool.cfg`)
4. Generates:
   - `base.bin` — the integrity catalog (hashes of game files)
   - `runtime.conf` — runtime configuration passed to the anti-cheat client
   - Optionally: `base.cer` — the public certificate

### Tool usage (from strings analysis)

```
integritytool -incert mod1.cer -inkey mod1_private.key -outname mod1 mod1.cfg
integritytool -inkey 95e760e6a6a447c28dc50db3e78d060f_private.key -incert 95e760e6a6a447c28dc50db3e78d060f_cert.cer
integritytool list C:\Games\MyGame\EasyAntiCheat\Certificates\base.bin
integritytool list C:\Games\MyGame\EasyAntiCheat\Certificates\runtime.conf
```

### Config file structure (`anticheat_integritytool.cfg`)

```
config_info:
{
    version = 2;
};
search_options:
{
    exclude_size_threshold = 0;    // MB, 0 = include all files
    // Paths to ignore (glob patterns)
    // Files that differ per-user or don't affect gameplay
};
```

## Why This Matters

### Current approach (OnlineFix universal certs)

| Aspect | Status |
|---|---|
| Source | OnlineFix's bundled `base.cer`, `base.bin`, `runtime.conf` |
| Universality | Same files work across all confirmed games |
| Trust model | Certs are signed against OnlineFix's patched SDK binary |
| Risk | If Epic revokes these certs, all users break simultaneously |
| Maintenance | Zero — just ship the same files |
| Legality | Gray area — using someone else's signed PKI material |

### Potential approach (custom cert generation)

| Aspect | Status |
|---|---|
| Source | Game's own private key (extracted from EAC deployment) |
| Universality | Per-game — each game needs its own key |
| Trust model | Certs signed against the game's own EAC deployment |
| Risk | If Epic revokes per-game keys, only that game's users break |
| Maintenance | Higher — need to extract a key per game |
| Legality | Same gray area, but using the game's own key rather than a third party's |

## Key Questions (TODO — Research Needed)

### 1. Where is the private key stored?

The private key is downloaded from the Epic Developer Portal by the game's 
developer. It's typically stored:
- In the game's build system (not shipped with the game)
- In the `EasyAntiCheat/Certificates/` folder (sometimes)

**Research needed**: Check if any game ships the private key in its own 
`EasyAntiCheat/` folder. If so, we could extract it and generate our own certs.

### 2. Can we extract the private key from existing cert files?

The `base.cer` file is a public certificate. The `base.bin` file is an encrypted 
catalog. Neither contains the private key directly.

**Research needed**: Check if `anticheat_integritytool.exe` can:
- Extract the private key from an existing `base.bin` + `base.cer` pair
- Re-sign a new catalog with an existing key
- Generate a new key pair from scratch (unlikely — that would require Epic's root CA)

### 3. Can we modify the integrity catalog to include ScreamAPI?

The `base.bin` file contains hashes of the game's files. EAC's loader checks 
these hashes at startup. If ScreamAPI's DLL hash isn't in the catalog, EAC 
rejects it.

**Research needed**: Can we:
- Add ScreamAPI's DLL hash to the existing `base.bin` catalog?
- Regenerate `base.bin` with our DLL included?
- Use the integrity tool to generate a new catalog that includes both the 
  game's files AND our DLL?

### 4. Does the tool support "mod certificates"?

From the strings analysis:
```
"If the certificate is not a mod certificate, a signed runtime configuration 
file will also be created."
```

This suggests the tool supports **mod certificates** — a separate certificate 
type for mods. If ScreamAPI could be registered as a "mod" with its own 
certificate, it might be accepted by EAC without modifying the base catalog.

**Research needed**: 
- What is a "mod certificate"?
- How does it differ from a base certificate?
- Can we generate one for ScreamAPI?
- Does EAC's loader accept mod certificates from DLL proxy setups?

### 5. Can we use the tool to verify our cert files?

The tool has a `verify` mode:
```
integritytool -productid 95e760e6a6a447c28dc50db3e78d060f verify C:\Games\MyGame
```

This could be used to verify that our cert files are valid before shipping them.

**Research needed**: Run the verify mode against a game with OnlineFix's certs 
to understand what "valid" means.

## Potential Implementation Plan (TODO)

If the research shows that custom cert generation is feasible:

### Phase 1: Key Extraction
- Find a game that ships its private key in `EasyAntiCheat/Certificates/`
- Extract the key
- Run `anticheat_integritytool.exe` with the key + config to generate new certs
- Verify the generated certs work with EAC

### Phase 2: ScreamAPI Integration
- Generate a `base.bin` catalog that includes ScreamAPI's DLL hash
- Generate a `runtime.conf` that accepts ScreamAPI
- Bundle the generated certs in the `eos_patcher.pyw` tool

### Phase 3: Per-Game Automation
- Auto-detect the game's EAC deployment ID from `Settings.json`
- Auto-extract the private key (if available)
- Auto-generate certs per game
- Ship in the Tauri GUI as a one-click operation

## Alternative: Use `start_protected_game.exe` Directly

The EAC bootstrapper (`start_protected_game.exe`) is also shipped in the 
AntiCheatTools package. It:
- Launches the game with EAC protection
- Reads `Settings.json` for product ID, sandbox ID
- Handles the EAC service initialization

**Research needed**: Could we replace the game's own bootstrapper with this 
tool, configured to accept ScreamAPI? This might bypass the cert file 
requirement entirely.

## References

- `EOS_AntiCheatTools-win32-x64-2.3.0.zip` — the tools package
- `anticheat_integritytool.exe` — cert generation tool
- `anticheat_integritytool.cfg` — config file
- `start_protected_game.exe` — EAC bootstrapper
- `EasyAntiCheat_EOS_Setup.exe` — EAC service installer
- `Settings.json` — per-game EAC config (product ID, sandbox ID)
- `EAC_Guide.md` — current EAC setup guide (cert-file approach)

## Status

- [ ] Extract private key from a game's EAC folder
- [ ] Run `anticheat_integritytool.exe` with extracted key
- [ ] Generate new `base.bin` + `runtime.conf` including ScreamAPI hash
- [ ] Test generated certs on a confirmed game (Deceive Inc. or The Riflemen)
- [ ] Test `verify` mode
- [ ] Research mod certificate workflow
- [ ] Research `start_protected_game.exe` replacement approach
- [ ] Write implementation doc if feasible
