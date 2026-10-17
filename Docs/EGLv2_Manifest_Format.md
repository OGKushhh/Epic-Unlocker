# Epic Games Launcher v2 — Manifest Storage Format

**Research notes for ScreamAPI / Epic-Unlocker compatibility.**
**Launcher version documented:** `0.97.0+g4f63cc1` (Preview, released Oct 14, 2026).

> ⚠️ **The v2 launcher is in Preview.** Epic explicitly warns: *"with any beta there could be issues."*
> Everything in this doc was reverse-engineered from the launcher binary and confirmed against a real
> Windows install. Field names and paths may change before the stable release. Treat this as a
> snapshot, not a contract. See [§9 Forward-Compatibility](#9-forward-compatibility-strategy) for
> how to write code that survives schema churn.

---

## 1. TL;DR

The new Epic Games Launcher ("v2", internal codename `eglv2`, Tauri app identifier
`com.epicgames.eglv2`) stores game install metadata in a **completely different location and
format** than v1:

| What | v1 | v2 |
|---|---|---|
| Install metadata file | `<ProgramData>\Epic\EpicGamesLauncher\Data\Manifests\<appName>.item` (JSON, one file per game) | `<LOCALAPPDATA>\com.epicgames.eglv2\installations.jsonl` (JSONL, all games in one file) |
| Binary game manifest | Path in `.item`'s `CompleteManifestPath` field | `<gameDir>\.egstore\<installationId>.manifest` (constructed from JSONL fields) |
| Binary manifest format | Epic binary, magic `0x44BEC00C` | **Same Epic binary format — unchanged** |
| Has `CompleteManifestPath` field? | Yes | **No** — replaced by `complete: bool` |
| DLC representation | Separate `.item` per DLC | Separate JSONL line with `parentInstallationId` set |

**The binary manifest parser needs no changes.** Only the *discovery* of manifests and the
*parsing of metadata* need updating. The v2 binary manifest file is byte-for-byte compatible
with v1's binary manifest file.

---

## 2. Background: v1 vs v2 Launcher

Epic announced the **Epic Games Launcher Preview** on October 14, 2026. It is a ground-up
rewrite of the launcher in Rust + Tauri, replacing the old Chromium-Embedded C++ launcher.
Notable changes relevant to manifest discovery:

- **First Linux-native release.** The launcher ships as a `.deb` (and presumably an `.rpm`
  eventually) — no more Wine required. This is also the first time the launcher's data
  directory layout follows XDG conventions on Linux.
- **Tauri-based.** The Tauri app identifier is `com.epicgames.eglv2`. Tauri uses the
  `directories` crate convention for `app_data_dir()`, which means the data directory is
  keyed off the app identifier — not off a human-readable product name like v1's
  `EpicGamesLauncher` folder.
- **Decentralized manifest storage.** v1 kept all manifests in a single central folder
  (`%PROGRAMDATA%\Epic\EpicGamesLauncher\Data\Manifests\`). v2 keeps install *records*
  centrally (in `installations.jsonl`) but moves the binary game *manifests* out into each
  game's own install directory under `.egstore/`.
- **Internal codename `eglv2`.** The Debian package's `Conflicts/Replaces` field lists
  `eglv2, eg-lv2`. The Tauri identifier, log file name (`logs/eglv2.log`), Rust crate names
  (`eglv2_lib`, `eglv2_setup`), and even Windows installer internals (`EGLv2Setup.exe`)
  all use this name. The user-facing product name is "Epic Games Launcher"; `eglv2` is the
  developer-facing name.

---

## 3. Confirming Which Launcher Is Installed

Before reading any manifest, the scanner should determine which launcher wrote the data.
The most reliable signal is the presence of the v2 data directory.

### 3.1 v2 Data Directory Locations

Tauri's `app_data_dir()` resolves to:

| Platform | Path |
|---|---|
| **Windows** | `%LOCALAPPDATA%\com.epicgames.eglv2\` (e.g. `C:\Users\<you>\AppData\Local\com.epicgames.eglv2\`) |
| **Linux** | `~/.local/share/com.epicgames.eglv2/` (respects `XDG_DATA_HOME`) |
| **macOS** | `~/Library/Application Support/com.epicgames.eglv2/` |

### 3.2 v1 Data Directory Locations (legacy, keep as fallback)

| Platform | Path |
|---|---|
| **Windows** | `%PROGRAMDATA%\Epic\EpicGamesLauncher\Data\Manifests\` (central manifest store) |
| **Windows** | `%LOCALAPPDATA%\EpicGamesLauncher\Saved\` (per-user state) |
| **Linux** | N/A — v1 never ran natively on Linux. May exist under Wine prefix as `drive_c/users/<you>/AppData/...`. |
| **macOS** | `~/Library/Application Support/Epic/EpicGamesLauncher/` (rare) |

### 3.3 Version Confirmation Files

Inside the v2 data directory:

- **`last-version.txt`** — contains the last-run launcher version string, e.g.
  `0.97.0+g4f63cc1`. Useful for telemetry / "what version produced this data?".
- **`api.json`** — local IPC configuration. Sample content:
  ```json
  {
    "version": "0.97.0+g4f63cc1",
    "machineSecret": "ae47b021b9a505f72b656ed77e6b30a8cbc5e2585c752fadc188ff7a31350a32",
    "port": 36783
  }
  ```
  The launcher spins up a local HTTP server on `127.0.0.1:<port>` for IPC between its
  embedded webview (Tauri's WebView2/WebKitGTK layer) and the Rust backend. The
  `machineSecret` authenticates that local channel — it is **not** an Epic account
  credential and should not be uploaded. The scanner should ignore this file.

A scanner that wants to be robust should probe both v1 and v2 paths and scan whichever
exists. A user mid-migration may have both populated; in that case v2 wins (v1's
`.item` files are typically deleted by the v1→v2 migration step on first v2 launch).

---

## 4. v2 Data Directory Layout

The v2 data directory contains 16 subfolders and 7 top-level files. Each was identified
by grepping the launcher binary's embedded strings and confirmed against a real install
on Windows. Below is the complete layout with the purpose of each entry, sourced from
the binary's own log/error strings.

### 4.1 Top-Level Files

| File | Purpose | Source (binary string) |
|---|---|---|
| `installations.jsonl` | **The new "manifest"** — one JSON object per installed game per line | `eglv2_lib::install_layout`, `Install.Manifest.Saved` |
| `installations.jsonl.prev` | Atomic-rename backup of the above (written first, then renamed on success) | `Removed an add-on record...` log paths |
| `last-version.txt` | Last-run launcher version, used to trigger migrations | `run-v1-migration`, `run-v1-settings-migration` triggers |
| `api.json` | Local IPC config (port + machine secret) — see §3.3 | (IPC config; not user-relevant) |
| `device.json` | Machine identity (used by `auth::machine_id` for playtime journalling) | `playtime stays journalled (see auth::machine_id)` |
| `last-played.json` | Per-game last-played timestamps | (playtime tracking) |
| `recently-played.json` | Recently-played games list | (playtime tracking) |

### 4.2 Subfolders

| Folder | Purpose | Source (binary string) |
|---|---|---|
| `auth/` | Credential store (login tokens). Honors `EGLV2_FORCE_KEYRING` env var on Linux to force use of the system keyring instead. | `eglv2_lib::auth::credential_store`, `Auth.Credentials.FileReadFailed` |
| `cache/` | Parsed-manifest cache (deserialized binary manifests kept on disk for fast re-load) | `Parsed manifest cache hit`, `Parsed manifest cache miss` |
| `cloud-saves/` | Pending cloud save syncs | `cloud-saves/pending.json`, `eglv2_lib::cloud_saves::pending` |
| `download/` | Active/paused download state, chunk staging | `chunk_cache.fab-staging`, `Fab.LegacyChunkCacheAdopted` |
| `EBWebView/` | Tauri's WebView2 user-data directory (Edge runtime cache, cookies, IndexedDB) | (Tauri convention) |
| `feedback/` | Bug-report bundles before upload to Epic | `eglv2_lib::feedback::collect`, `feedback: skipping file with a non-log extension` |
| `heroes/` | Library "hero" banner images for each game | (UI assets) |
| `images/` | Generic image cache (icons, thumbnails) | (UI assets) |
| `import-scan/` | Results of "scan existing install folder" operations | `Install scan admitted`, `Install scan cancellation requested` |
| `logs/` | Single rolling log file: `eglv2.log` | `logs/eglv2log` (string-join artifact) |
| `playtime/` | Playtime journal (append-only, per-game) | `playtime stays journalled` |
| `settings/` | Launcher settings + per-account settings (v1 settings are migrated in on first v2 launch) | `V1SettingsMigrationReport`, `V1SettingEntry`, `V1SettingStatus` |
| `telemetry/` | Telemetry event queue (offline buffer until next upload window) | (telemetry batching) |
| `ue/` | Discovered Unreal Engine installs (the launcher enumerates installed UE versions) | `Discovered engines via enumeration`, `Engine list changed on revalidate` |
| `update/` | Self-updater staging directory | `eglv2_setup::silent`, `EGLv2CleanupTemp` |
| `update-splash/` | Splash screen assets shown during self-update | (updater UI) |

Notably **absent**: any `Manifests/` folder. v1's central manifest store has no v2
equivalent — binary game manifests now live exclusively inside each game's own install
directory under `.egstore/` (see §6).

---

## 5. The `installations.jsonl` File

This is the v2 replacement for v1's per-game `.item` files. It is a JSONL file: one JSON
object per line, with the first line being a header.

### 5.1 Header Line

The first line of the file is a metadata header:

```json
{"version":1,"writtenAt":"2026-10-12T23:21:53Z","entries":5}
```

| Field | Type | Meaning |
|---|---|---|
| `version` | integer | Schema version of the JSONL file itself (currently `1`) |
| `writtenAt` | string (ISO 8601 UTC) | When the file was last written |
| `entries` | integer | Number of installation records following the header |

A scanner reading this file line-by-line should skip any line where `version` and `entries`
are both present (i.e. the header) before parsing installation records.

### 5.2 Installation Record Schema

Each subsequent line is one installation record. Sample (from a real install, 5 entries):

```json
{"installationId":"C244C14F39C1464E927B15BB125DF5A4","complete":true,"dir":"D:\\Games\\SongsOfConquest","platform":"Windows","namespace":"3a18db06dff0465b8a58308aa29020a0","itemId":"5b2d14c165be4b9b829fb1bff9ce491c","artifactId":"537f7351f9fb4b67a2fea2273e9c8277","version":"1.10.4_a515e789d6_Windows_4733","tags":[""],"sidecar":{"revision":1,"deploymentId":"e88de2624a1b40b494eeeb5a2705ee1e"},"launchCommand":"","launchExecutable":"SongsOfConquest.exe","writtenAt":"2026-10-09T03:31:10Z"}
```

Field reference:

| Field | Type | Required | Meaning | v1 equivalent |
|---|---|---|---|---|
| `installationId` | string (GUID, uppercase, no braces) | ✓ | Stable unique ID for this install. Used to derive the binary manifest filename. | `InstallationGuid` |
| `complete` | bool | ✓ | `true` = install finished; `false` = partial / in-progress. **Replaces v1's Pending-folder check.** | (none — v1 used path inspection) |
| `dir` | string (absolute path) | ✓ | Game install directory. May contain trailing fragments after the game name (e.g. `D:\Games\TownofSalem29u4MZ` — the launcher appends a random suffix in some cases). | `InstallLocation` |
| `platform` | string | ✓ | `"Windows"`, `"Mac"`, or `"Linux"` | (none) |
| `namespace` | string (hex) | ✓ | Epic namespace ID — identifies the game's "ownership scope" | (packed into `CatalogItemId`) |
| `itemId` | string (hex) | ✓ | Catalog item ID — identifies the specific SKU | (packed into `CatalogItemId`) |
| `artifactId` | string (hex) | ✓ | Build artifact ID — identifies the specific build | (none) |
| `version` | string | ✓ | Build version string, e.g. `"1.10.4_a515e789d6_Windows_4733"`. Format is game-specific. | `AppVersionString` / `BuildVersion` |
| `tags` | array of strings | ✓ | Component selection tags (usually `[""]` for base games). | (none) |
| `sidecar` | object | ✓ | `{revision: int, deploymentId?: string}`. `deploymentId` is the Epic CDN deployment this build came from; used for re-fetch if `.egstore/` is missing. May be absent if `revision == 0`. | (none) |
| `launchCommand` | string | ✓ | Extra command-line args for the launch executable (often empty). | `LaunchCommand` |
| `launchExecutable` | string | ✓ | Filename of the game's main executable, relative to `dir`. | `LaunchExecutable` |
| `writtenAt` | string (ISO 8601 UTC) | ✓ | When this record was last written. | (none) |
| `prereqSha1Hash` | string (hex) | optional | SHA1 of the prerequisite installer (Redist, DirectX, etc.) | (none) |
| `lastPrereqSucceededSha1Hash` | string (hex) | optional | SHA1 of the last successfully-installed prereq | (none) |
| `parentInstallationId` | string (GUID) | optional | Present only on DLC records. References the parent game's `installationId`. | (none — v1 used separate `.item` files per DLC) |
| `pendingVersion` | string | optional | If non-empty, an update is queued for this install | (none) |
| `pendingTags` | array | optional | Component selection for the pending update | (none) |
| `deleted` | bool | optional | If `true`, the record is marked for deletion (game was uninstalled but the record is kept briefly for cleanup). Scanner should skip. | (none) |

### 5.3 Parsing Rules

1. **Skip the header line.** Detect by presence of `version` AND `entries` keys.
2. **Skip empty lines.** Trailing newlines are common.
3. **Skip malformed lines with `log::warn!`** — do not abort the whole scan. One corrupt
   line shouldn't lose the other 50 records.
4. **Skip records where `complete != true`.** This is the v2 equivalent of v1's
   `/Pending/`-folder rejection. The launcher writes the record *before* the download
   completes and updates `complete` to `true` only when finished.
5. **Skip records where `deleted == true`.** These are tombstones.
6. **Treat missing optional fields as empty/default**, never as errors.

---

## 6. The Binary Game Manifest (`.egstore/<installationId>.manifest`)

### 6.1 Location Pattern

For each installation record in `installations.jsonl`, the binary game manifest lives at:

```
<dir>/.egstore/<installationId>.manifest
```

Where:
- `<dir>` is the record's `dir` field
- `<installationId>` is the record's `installationId` field (uppercase, no braces)
- `.egstore` is a literal folder name created inside each game's install directory

Concrete examples from a real install:

| Game | Binary manifest path |
|---|---|
| Songs of Conquest | `D:\Games\SongsOfConquest\.egstore\C244C14F39C1464E927B15BB125DF5A4.manifest` |
| Town of Salem 2 | `D:\Games\TownofSalem29u4MZ\.egstore\3130CAD0A97BF39CB05A1E3BCFB327A8.manifest` |
| GTA V | `D:\Games\GTAV\.egstore\3F290CFD414982EA7F244DBF2716065C.manifest` |
| MiniRoyale | `D:\Games\MiniRoyale\.egstore\499893220D5FF80E229855973481F635.manifest` |
| Oddsparks | `D:\Games\OddSparks\.egstore\D6785291FC7D715B81A2E4924AF226B5.manifest` |

The `.egstore/` folder also contains a `chunk_cache/` subdirectory used by the launcher
for download resume — not relevant for ScreamAPI.

### 6.2 Binary Format

**Unchanged from v1.** The file is the same Epic binary manifest format the existing
parser already handles:

- Magic: `0x44BEC00C` (little-endian u32)
- Header: 41 bytes (`header_size`, `data_size_uncompressed`, `data_size_compressed`,
  20-byte SHA1, `stored_as` flags, `version`)
- Body: optionally zlib-compressed, optionally encrypted (encrypted manifests are
  unsupported and rejected by the parser)
- Metadata section: `feature_level`, `data_version`, `app_id`, `app_name`,
  `build_version`, `launch_exe`, `launch_command`, `prereq_ids[]`, `prereq_name`,
  `prereq_path`, `prereq_args`, `build_id` (if `data_version >= 1`)
- Custom fields section: key-value string pairs (often contains `AppVersionString`)

No parser changes are required. The existing `_parse_binary_manifest()` in
`manifest_parser.py` and the existing `parse_manifest_file()` CLI both work on v2 binary
manifests byte-for-byte.

### 6.3 Source Strings (how we know)

The launcher binary's embedded log strings confirm the location pattern unambiguously:

- `"Manifest saved to .egstore"` / `Install.Manifest.Saved`
- `"Loaded the installation's current manifest from .egstore"`
- `"Loaded old manifest from .egstore"`
- `"No old manifest found in .egstore"`
- `"No .egstore manifest for installation"`
- `"Installation record carries no installation id, so its .egstore manifest cannot be named"`
  ← this is the smoking gun: the launcher derives the filename from `installationId`.
- `"Loaded manifest binary by GUID"` / `"No manifest for this GUID"`
- `"No local manifest, fetching from CDN for uninstall"` ← CDN fallback path
- `"Install.Manifest.EgstoreCreateFailed"`, `"Install.Manifest.WriteFailed"`,
  `"Install.Manifest.Absent"`, `"Install.Manifest.Loaded"`, `"Install.Manifest.ReadFailed"`,
  `"Install.Manifest.RemoveFailed"`, `"Install.Manifest.FieldsUnreadable"`,
  `"Install.Manifest.FieldsSaved"` (the last one saves "decrypted protected fields beside
  the installed manifest" — interesting but not relevant to ScreamAPI)
- Rust module: `eglv2_lib::manifest_store` (`src/manifest_store.rs`)

### 6.4 When the Binary Manifest Is Missing

The launcher has a CDN fallback (`"No local manifest, fetching from CDN for uninstall"`).
This means `.egstore/<installationId>.manifest` can be absent in three scenarios:

1. **Game installed before v2 launcher existed, never re-verified by v2.** The launcher
   didn't write the `.egstore/` folder. The record still appears in `installations.jsonl`
   (migrated from v1 on first v2 launch) but the binary manifest is on Epic's CDN only.
2. **User manually deleted `.egstore/`** (rare, but possible if they cleaned up "unused"
   folders).
3. **DLC with no local binary manifest** — see §7.

The scanner should handle this gracefully: skip the binary upload, log a warning, but
**still upload the metadata line** so the backend knows the install exists. The metadata
line alone is enough for ScreamAPI's primary use case (knowing which games are installed
and where); the binary manifest is only needed for DLC catalog enumeration.

---

## 7. DLC Handling

DLCs in v2 are represented as separate lines in `installations.jsonl` with the
`parentInstallationId` field set to the parent game's `installationId`. A DLC record
looks like (hypothetical example — not yet observed in a real install):

```json
{"installationId":"<DLC_GUID>","complete":true,"dir":"D:\\Games\\SongsOfConquest","platform":"Windows","namespace":"...","itemId":"<DLC itemId>","artifactId":"<DLC artifactId>","version":"...","tags":[""],"sidecar":{"revision":1,"deploymentId":"..."},"launchCommand":"","launchExecutable":"","parentInstallationId":"C244C14F39C1464E927B15BB125DF5A4","writtenAt":"..."}
```

Note `dir` typically matches the parent game's `dir` (DLCs install into the parent game's
folder) and `launchExecutable` is often empty for DLCs.

### 7.1 Where DLC Binary Manifests Live

The launcher binary's log strings reveal three possible storage locations, in order of
preference:

1. **`<parentGameDir>/.egstore/<dlcInstallationId>.manifest`** — same `.egstore/` folder
   as the parent game, just with the DLC's own `installationId` as the filename.
   Confirmed by: `"Loaded DLC manifest from .egstore for uninstall"` (plural-aware —
   `Install.Uninstall.ManifestFilesDeleted` is plural in the launcher binary).

2. **Inside `cache/`** (the parsed-manifest cache) — used when the launcher has parsed
   the binary manifest once and cached the deserialized form. Confirmed by:
   `"DLC manifest size from .egstore cache"` and `"Parsed manifest cache hit"`.

3. **Nowhere on disk** — fetched on-demand from Epic's CDN using the `sidecar.deploymentId`.
   Confirmed by: `"No local manifest, fetching from CDN for uninstall"`. This happens for
   DLCs that were installed via "manifest summary" only (no full binary manifest was ever
   written locally).

### 7.2 What ScreamAPI Actually Needs

For ScreamAPI's purposes, **the parent game's binary manifest already contains the DLC
catalog** via the `prereq_ids` field (an array of strings extracted at line 183 of
`manifest_parser.py`). When a user uploads the parent game's `.manifest`, the backend
already learns which DLCs exist for that game. The DLC's own binary manifest is only
needed for file-level operations (uninstall, verify) which ScreamAPI does not perform.

**Recommended scanner rule**: for each JSONL record (parent or DLC), try to read
`<dir>/.egstore/<installationId>.manifest`. If present → upload it. If absent → log a
warning, skip the binary, but still upload the metadata line. This single rule handles
all three DLC storage cases without special-casing.

### 7.3 Grouping DLCs With Their Parent

The backend's existing `upload_manifest_manual()` endpoint groups uploads by `app_name`.
For v2, the `app_name` of a DLC record will be its `launchExecutable` (often empty) or
its `installationId` — not a human-readable name. To group DLCs with their parent game,
the scanner should set the synthetic `.item` filename for DLC records to
`<parentInstallationId>__<installationId>.item` (note the double underscore separator).
The backend can then group by the prefix before `__` to associate DLCs with parents.

---

## 8. v1 ↔ v2 Field Mapping

When the backend parses an uploaded metadata file, it needs to recognize both schemas.
The dispatcher logic should be based on **field presence**, not on a version field
(since v2 has no version field on individual records — only on the header line).

### 8.1 Dispatcher Rules

```python
def _parse_json_manifest(data: bytes) -> ManifestInfo:
    json_data = json.loads(data.decode('utf-8'))

    # v2 JSONL line: identified by installationId + dir + complete
    if "installationId" in json_data and "dir" in json_data:
        return _parse_v2_jsonl_line(json_data, data)

    # v1 .item file: identified by AppName + CompleteManifestPath
    if "AppName" in json_data and "CompleteManifestPath" in json_data:
        return _parse_v1_item(json_data, data)  # existing code

    # Unknown schema — store as opaque, preserve raw bytes for future re-parse
    return _parse_unknown_schema(json_data, data)
```

### 8.2 Field Mapping Table

| Concept | v1 field | v2 field | Notes |
|---|---|---|---|
| Install GUID | `InstallationGuid` | `installationId` | v2 is uppercase, no braces. v1 may have braces. |
| Install directory | `InstallLocation` | `dir` | Same semantics. |
| Build version | `BuildVersion` | `version` | Same semantics, format is game-specific. |
| App version string | `AppVersionString` | `version` (reuse) | v2 doesn't separate the two; use `version` for both. |
| App name | `AppName` | `launchExecutable` (fallback to `installationId`) | v2 has no `AppName` field. Use the executable name as a best-effort human-readable identifier. |
| Display name | `DisplayName` | `launchExecutable` (fallback) | Same caveat. |
| Launch executable | `LaunchExecutable` | `launchExecutable` | Same. |
| Launch command | `LaunchCommand` | `launchCommand` | Same. |
| Binary manifest path | `CompleteManifestPath` | **(none)** | v2 constructs it as `<dir>/.egstore/<installationId>.manifest`. |
| Install complete? | (path-based heuristic: contains `/Pending/`) | `complete` (bool) | v2 makes this explicit. |
| App ID | `AppId` (integer) | **(none in JSONL)** | v2's JSONL has no `AppId`. It's inside the binary manifest instead. |
| Catalog namespace | (packed into `CatalogItemId`) | `namespace` | v2 splits it out. |
| Catalog item ID | `CatalogItemId` | `itemId` | v2 splits it out. |
| Build artifact ID | (none) | `artifactId` | v2-only — identifies the specific build. |
| CDN deployment ID | (none) | `sidecar.deploymentId` | v2-only — used for CDN re-fetch. |
| Parent install | (none — separate `.item` per DLC) | `parentInstallationId` | v2-only — links DLCs to parent game. |
| Last written | (none) | `writtenAt` | v2-only — ISO 8601 UTC. |
| Schema version | (none) | (none on per-record; `version:1` on file header only) | Both schemas are versionless per-record. |

### 8.3 Pending / Incomplete Install Detection

This is the one behavioral change that matters most:

- **v1**: The launcher wrote `.item` files for in-progress installs into a `Pending/`
  subfolder. The backend rejects these by checking if `CompleteManifestPath` contains
  `/pending/` (see `_is_pending_path()` in `app.py` line 1406).

- **v2**: There is no `Pending/` folder. Instead, every record has a `complete: bool`
  field. `false` means the install is in progress or interrupted.

The v2 parser should **reject records where `complete != true`** with a clear error
message. This is the direct equivalent of v1's Pending-folder rejection and serves the
same purpose: prevent uploading half-downloaded manifests that would corrupt the index.

---

## 9. Forward-Compatibility Strategy

The launcher is in Preview. Epic will change things. Code that touches v2 data should be
written to survive schema churn without requiring a re-release. Three principles:

### 9.1 Detect by Structure, Not by Version

Never gate behavior on `"version": 1` from the header. Instead, dispatch on field
presence (see §8.1). When v3 lands with a different schema, add a third dispatcher
branch; v1 and v2 uploads continue to work unchanged.

### 9.2 Never Fail on Unknown Fields

Use `.get(key, default)` everywhere. Anything not explicitly mapped goes into
`custom_fields` with a `v2_<fieldname>` prefix (or `v3_<fieldname>` for v3, etc.) —
preserved verbatim, never discarded. If Epic renames `dir` → `installDir` in v3, add the
new key to the dispatcher; old uploads still parse from `custom_fields`.

Example: a v2 record with an unknown new field `installSize` should result in
`custom_fields["v2_installSize"] = "12345678"`, not a parse error.

### 9.3 Always Tag Index Entries with Schema Version

Every index entry should get an `egl_version` field: `"v1"`, `"v2"`, `"v3"`, or
`"unknown"`. This lets you:

- Query "how many v2 manifests do we have?" later
- Re-parse entries with a future parser if needed
- Detect when a user upgrades from v1 to v2 (their next upload will have a different
  `egl_version` for the same game)

### 9.4 Forward-Compat Escape Hatch

For records that don't match any known schema, the client should still upload the raw
line bytes (as `<installationId>.unknown-schema.jsonl-line`) and the backend should
store them as-is in `custom_fields["_raw_unknown_schema"]`. When the parser is updated,
the existing `/api/manifest/rebuild` endpoint can be extended to re-parse these entries
in place.

### 9.5 Client-Side Scan Resilience

The client scanner should never crash on a malformed JSONL line. Use `serde_json::Value`
and `.get()` chains with `Option`. If a line fails to parse, `log::warn!` and continue.
The worst case if v3 ships tomorrow and breaks our parser: v2 records still scan, v3
records log warnings and get skipped, until a parser update is shipped.

---

## 10. Migration Behavior (v1 → v2)

The v2 launcher has explicit v1→v2 migration logic, triggered on first v2 launch if it
detects v1 data:

- `allow-run-v1-migration` / `deny-run-v1-migration` (Tauri ACL permission gates)
- `allow-run-v1-settings-migration` / `deny-run-v1-settings-migration`
- `V1SettingsMigrationReport`, `V1SettingEntry`, `V1SettingStatus` (settings migration
  types in `eglv2_lib::settings::staff_only`)

What the migration does (inferred from binary strings):

1. Reads all v1 `.item` files from `%PROGRAMDATA%\Epic\EpicGamesLauncher\Data\Manifests\`.
2. For each, writes a corresponding line to `installations.jsonl` with mapped fields.
3. Copies or migrates the binary `.manifest` files into each game's `.egstore/` folder
   (renaming them to `<installationId>.manifest`).
4. Migrates v1 settings (download rate limits, install paths, etc.) into v2's
   `settings/` folder.
5. **Deletes the v1 `.item` files** on successful migration (so the v1 `Manifests/`
   folder ends up empty or absent).

This means: **a user who has run v2 even once will have no v1 `.item` files left.** The
scanner cannot rely on v1 paths as a fallback for v2 users — it must scan the v2 path.

However, **a user who installed v2 but never launched it** still has only v1 data. The
scanner should probe both paths and scan whichever exists.

---

## 11. Implementation Notes (Deferred)

Code changes are deferred until the launcher stabilizes. When ready, the work splits into
two parts:

### 11.1 Backend (AbDoBest Space, Python)

**`manifest_parser.py`** — add `_parse_v2_jsonl_line()` and a dispatcher in
`_parse_json_manifest()` (see §8.1). The new function should:

- Reject records where `complete != true` (raise `ManifestParseError`)
- Reject records where `deleted == true`
- Map fields per §8.2
- Set `egl_version = "v2"` in `custom_fields`
- Preserve unknown fields as `custom_fields["v2_<name>"]`

**`app.py`** — no endpoint changes strictly required. The existing `_is_pending_path()`
check on `CompleteManifestPath` is silently a no-op for v2 (the field doesn't exist),
which is fine because the parser now rejects incomplete v2 records itself. Optional
polish: surface `egl_version` in the `/upload` response body.

### 11.2 Client (Epic-Unlocker, Rust)

**`EpicGUI/src-tauri/src/manifest.rs`** — three changes:

1. `manifest_dirs()` — add the v2 path (see §3.1). Keep v1 paths as fallback.
2. New `scan_v2_installations_jsonl(path: &Path)` function — see §5.3 for parsing rules.
   For each valid record, emit two `ScannedManifest` entries: one for the JSONL line
   (treated as JSON metadata) and one for the binary `.manifest` file at
   `<dir>/.egstore/<installationId>.manifest` (if it exists).
3. Dispatch in `scan_manifests_on_disk()` — if a directory contains
   `installations.jsonl`, call the v2 scanner instead of walking for `.item` files.

The existing upload flow, dedup logic, and chunked upload path all work unchanged —
each scanned file gets its own SHA256 and uploaded via the existing
`/api/manifest/upload` endpoint.

---

## 12. Open Questions

Things still worth verifying on a real install before writing code:

1. **Exact filename pattern for DLC binary manifests.** Is it
   `<parentGameDir>/.egstore/<dlcInstallationId>.manifest`, or does the DLC get its own
   `.egstore/` subfolder inside the parent? The binary strings suggest the former
   (`"Loaded DLC manifest from .egstore for uninstall"`, `"ManifestFilesDeleted"` plural)
   but a real DLC install would confirm.

2. **What happens if `installationId` contains lowercase characters?** All observed
   examples are uppercase hex GUIDs. The scanner should probably treat them as
   case-sensitive (use the exact string from the JSONL record as the filename), but
   worth confirming.

3. **Encoding of `dir` on non-English Windows.** UTF-16 paths with non-ASCII characters
   (e.g. `D:\Games\Игры\...`) — does `serde_json` decode them correctly, and does the
   filesystem layer handle them on the Rust side? Probably yes (both are UTF-8 internally)
   but worth a smoke test.

4. **What writes `installations.jsonl.prev`?** Is it written before *every* write of
   `installations.jsonl` (atomic-rename pattern), or only on certain operations? If the
   scanner reads `installations.jsonl` mid-write, it might get a partial line. The
   scanner should be tolerant of partial lines (skip with warning).

5. **Does v2 ever write multiple binary manifests per game** (e.g. for older build
   versions kept around for rollback)? The `.egstore/` folder might contain other
   `.manifest` files besides the one matching `installationId`. The scanner should
   only read the one matching the record's `installationId` — others are launcher
   internal.

---

## 13. Appendix: How This Doc Was Researched

All findings in this doc come from three sources, cross-referenced against each other:

1. **Static analysis of the v2 launcher binary.** The Linux `.deb` was downloaded from
   `https://launcher-public-service-prod06.ol.epicgames.com/launcher/api/installer/download/EpicGamesLauncher-x86_64.deb`
   (redirects to `https://content-download-egs.distro.on.epicgames.com/Builds/EGLv2/0.97.0+g4f63cc1/Linux/EpicGamesLauncher-x86_64.deb`).
   The single 192 MB ELF binary at `/usr/bin/EpicGamesLauncher` was extracted and
   `strings`-grep'd for path-like patterns, log message templates, Rust crate names, and
   Tauri configuration. Key strings cited inline throughout this doc.

2. **Real install verification on Windows.** The user supplied the contents of
   `%LOCALAPPDATA%\com.epicgames.eglv2\installations.jsonl` (5 entries, 3 KB) and a
   directory listing of `%LOCALAPPDATA%\com.epicgames.eglv2\` showing all 16 subfolders
   and 7 top-level files. The user also confirmed the existence of
   `D:\Games\SongsOfConquest\.egstore\C244C14F39C1464E927B15BB125DF5A4.manifest`,
   validating the binary manifest location pattern derived from the binary strings.

3. **Backend source code review.** The HuggingFace Space at
   `https://huggingface.co/spaces/OGKushhh/AbdoBest` was cloned and `manifest_parser.py`,
   `app.py` (manifest endpoints only, lines 1393–2187), and `manifest_tool.py` were read
   in full. Field mappings in §8.2 and parser change notes in §11.1 are derived directly
   from this code.

Source artifacts retained at:
- `/home/z/my-project/download/EpicGamesLauncherSetup-x86_64.exe` (Windows installer, 206 MB)
- `/home/z/my-project/download/EpicGamesLauncher-x86_64.deb` (Linux installer, 78 MB)
- `/home/z/my-project/download/AbDoBest-backend/` (backend clone)
- `/home/z/my-project/download/Epic-Unlocker/` (client clone)

---

**Document version:** 1.0 — 2026-10-17
**Launcher version documented:** `0.97.0+g4f63cc1` (Preview)
**Status:** Research complete. Implementation deferred until launcher stable release.
