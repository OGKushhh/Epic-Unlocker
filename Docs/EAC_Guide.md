# EAC Guide — How to Make Achievements Unlock on EAC-Protected EOS Games

> **Confirmed working**: Deceive Inc. (SDK 1.15.5), The Riflemen (SDK 1.19.0.3).
> Both work with **cert files only** — no SDK patches needed.

---

## Quick start (4 steps)

### Step 1 — Install Epic Unlocker as proxy

In the game's `Binaries\Win64\` folder (or wherever `EOSSDK-Win64-Shipping.dll` lives):

```cmd
ren EOSSDK-Win64-Shipping.dll EOSSDK-Win64-Shipping_o.dll
copy ScreamAPI\EOSSDK-Win64-Shipping.dll .
```

The Epic Unlocker proxy forwards all EOS calls to the renamed `_o.dll`, while intercepting the calls it cares about (achievements, Ecom).

### Step 2 — Configure ScreamAPI.ini

Drop `ScreamAPI.ini` next to the proxy DLL. Set the `[EAC]` section:

```ini
[EAC]
EACMode         = True
EACNoServerMode = True
```

- `EACMode = True` — enables game-handle capture (the dual-platform auth fix)
- `EACNoServerMode = True` — makes Ecom hooks return local "owned" without contacting the server

Both default to `False`. The `[EAC]` section is optional — non-EAC games are unaffected even if the section is present.

### Step 3 — Install cert files (SMART INSTALL)

The patcher GUI bundles OnlineFix's universal EAC cert files and installs them automatically.

1. Double-click `ScreamAPI\tools\eos_patcher.pyw` (or run `pythonw ScreamAPI\tools\eos_patcher.pyw`)
2. Click **SMART INSTALL** (yellow button)
3. Select the game's `EOSSDK-Win64-Shipping_o.dll` (the patcher uses it to locate the game's `EasyAntiCheat/` folder)
4. The patcher auto-detects `EasyAntiCheat/Certificates/`, backs up existing cert files (`.eosbak`), and writes the bundled OnlineFix certs (`base.cer`, `base.bin`, `runtime.conf`)
5. A dialog asks: **"Apply SDK patches too?"** — click **No** (cert-only, recommended for confirmed games) or **Yes** (for strict games that kick you mid-match)

### Step 4 — Launch

Launch the game from the **Epic Games Launcher** (not from the .exe directly). The launcher provides the EOS auth ticket the SDK expects.

**Verify it worked**:
- Game reaches the main menu without "untrusted system file" or "EAC validation failed"
- Press `Ctrl+Shift+U` to unlock all achievements, or `Shift+F5` to open the overlay

---

## What the patcher does

The patcher (`eos_patcher.pyw`) is a small GUI tool that does two jobs:

**1. Installs cert files (SMART INSTALL button)**

EAC's loader checks the EOS SDK against certificate files in `EasyAntiCheat/Certificates/`. The game's original certs reject our Epic Unlocker proxy, so the game shows "untrusted system file" and refuses to launch.

The patcher bundles OnlineFix's universal cert files (base.cer, base.bin, runtime.conf) inside itself. When you click SMART INSTALL, it:
- Finds the game's `EasyAntiCheat/` folder by walking up from the SDK location
- Backs up the existing cert files (renamed to `.eosbak`)
- Writes the bundled OnlineFix certs in their place

These cert files are universal — the same three files work on every confirmed game. They're genuine Epic PKI material, signed to accept the patched SDK.

**2. Patches the SDK (PATCH! button — optional)**

Some "strict" games enforce EAC's runtime reports and kick you mid-match with "EAC validation failed". For those games only, the patcher modifies the SDK DLL itself:

- **Site 1**: Makes the SDK load a stub anti-cheat client instead of the real one — no runtime integrity scans
- **Site 2**: Makes the SDK skip validation roundtrips to the server — no mid-match kicks

Both patches are applied to the SDK file on disk (not in memory), and a `.eosbak` backup is created first so you can always undo with the RESTORE button.

For our confirmed games (Deceive Inc., The Riflemen), **you don't need this** — cert files alone are enough. Click "No" when SMART INSTALL asks about patches. Only use PATCH! if the game kicks you mid-match.

---

## Manual cert install (if you can't use SMART INSTALL)

If you don't want to use the patcher GUI, or it fails to find the `EasyAntiCheat/` folder, install the cert files manually:

1. Find the game's `EasyAntiCheat\Certificates\` folder (usually next to the game's `.exe`)
2. Back up the existing `base.cer`, `base.bin`, `runtime.conf` (e.g., rename to `.bak`)
3. Copy OnlineFix's `base.cer`, `base.bin`, `runtime.conf` into `Certificates\`, overwriting the originals
4. **Do NOT touch `EasyAntiCheat\Settings.json`** — it's per-game (product ID, sandbox ID). Replacing it breaks the SDK's product binding.

The cert files are **universal** — the same OnlineFix certs work on all confirmed games.

---

## Undo everything

1. **Restore SDK** (if you applied patches): patcher GUI → **RESTORE**
2. **Remove proxy**: `del EOSSDK-Win64-Shipping.dll` then `ren EOSSDK-Win64-Shipping_o.dll EOSSDK-Win64-Shipping.dll`
3. **Restore cert files**: Epic Games Launcher → right-click game → **Verify** (re-downloads original `EasyAntiCheat/` folder)
4. **Remove config**: `del ScreamAPI.ini ScreamAPI.log`

---

## Confirmed working games

| Game | SDK | Setup | Notes |
|---|---|---|---|
| **Deceive Inc.** | 1.15.5 | Cert-only (no patches) | Single-player, client-authoritative |
| **The Riflemen** | 1.19.0.3 | Cert-only (no patches) | Confirmed 2026-08-30 |

For broken cases (e.g., MiniRoyale's SteelShield) and testing history, see [`EAC_Silencer_and_Achievement_Reference.md`](https://github.com/OGKushhh/Epic-Unlocker/tree/main/Docs/EAC_Silencer_and_Achievement_Reference.md).

---

## Troubleshooting

### "Untrusted system file" or "Unknown file version" at startup

EAC's loader is rejecting the SDK. **Fix**: Run **SMART INSTALL** again (or manually copy the cert files into `EasyAntiCheat/Certificates/`). If `Settings.json` was accidentally replaced, use Epic Games Launcher → **Verify** to restore it.

### Achievements don't unlock (no error)

Likely missing `[EAC]` config. Verify `ScreamAPI.ini` has `EACMode = True` and `EACNoServerMode = True` under `[EAC]`. Check `ScreamAPI.log` for `[INTERCEPT] EOS Auth login successful` — if missing, Epic Unlocker isn't loading or `[EAC]` isn't set.

### Game crashes or infinite loading

Most likely cause: you applied SDK patches to a game that doesn't need them (cert-only works). **Fix**: patcher GUI → **RESTORE**, then launch with cert-only setup. If still hanging, verify the cert files are correctly installed via SMART INSTALL.
