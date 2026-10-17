// pipe_protocol.h
// Shared between ScreamAPI (DLL on Windows, .so on Linux) and EpicGUI.
// Copy this file into both projects.
//
// Transport:
//   Windows: named pipe \\.\pipe\EpicGUI
//   Linux:   Unix domain socket $XDG_RUNTIME_DIR/epicgui.sock
//            (fallback /tmp/epicgui.sock if XDG_RUNTIME_DIR is unset)
//
// See Docs/LINUX_PORT_ANALYSIS.md §6 for path selection rationale.

#pragma once
#include <cstdint>

// ── Transport path ──────────────────────────────────────────────────────────
#if defined(_WIN32)
    // Win32 named pipe — case-insensitive, kernel-persisted.
    #define EPIC_PIPE_NAME       L"\\\\.\\pipe\\EpicGUI"
    #define EPIC_PIPE_NAME_A     "\\\\.\\pipe\\EpicGUI"
#elif defined(__linux__)
    // Unix domain socket path. The Rust side (linux_impl.rs) honors
    // $XDG_RUNTIME_DIR first and falls back to this literal path.
    // Keep this in sync with pipe_protocol.rs (Linux cfg branch).
    #define EPIC_PIPE_NAME_A     "/tmp/epicgui.sock"
    // No wide-char variant on Linux — UTF-8 is native.
    #define EPIC_PIPE_NAME       EPIC_PIPE_NAME_A
#else
    // macOS, BSD, etc. — not yet supported. Define a placeholder so the
    // header compiles even on unsupported platforms (the ScreamAPI side
    // will refuse to initialize the socket server).
    #define EPIC_PIPE_NAME_A     "/tmp/epicgui.sock"
    #define EPIC_PIPE_NAME       EPIC_PIPE_NAME_A
#endif

// ── Packet types ──────────────────────────────────────────────────────────────
enum class PktType : uint8_t {
    // DLL → GUI
    AchList     = 0x01,  // full achievement list dump
    AchUpdate   = 0x02,  // single achievement state changed
    LogPath     = 0x03,  // absolute path to ScreamAPI.log (UTF-8)
    DlcCatalog  = 0x04,  // full id→title map from Epic's GraphQL catalog
    GameInfo    = 0x05,  // sandbox ID + product ID + EOS version (for external API calls)

    // GUI → DLL
    CmdUnlock    = 0x10,  // unlock one achievement by id
    CmdUnlockAll = 0x11,  // unlock all locked achievements
    CmdRefresh   = 0x12,  // re-query definitions + player achievements
};

enum class WireUnlockState : uint8_t {
    Locked    = 0,
    Unlocked  = 1,
    Unlocking = 2,
};

#pragma pack(push, 1)

// Every packet starts with this header
struct PktHeader {
    uint32_t magic;        // always EPIC_MAGIC — detects packing/endian mismatches
    PktType  type;
    uint32_t payloadSize;  // bytes following this header
};
static constexpr uint32_t EPIC_MAGIC       = 0xABD04E21u;
static constexpr uint32_t EPIC_MAX_PAYLOAD = 8u * 1024u * 1024u; // 8 MB sanity cap

// ── AchList payload ───────────────────────────────────────────────────────────
// Header: PktType::AchList
// Payload: AchListHeader, then `count` AchEntry records, then string blob.
// String blob: null-terminated UTF-8 strings packed together.
// Each AchEntry holds byte offsets into the blob.

struct AchListHeader {
    uint32_t count;
    uint32_t blobSize;
};

struct AchEntry {
    uint32_t idOff;
    uint32_t nameOff;
    uint32_t descOff;
    uint32_t iconUrlOff;          // offset of UnlockedIconURL in the blob (0 = no URL)
    uint8_t  isHidden;
    WireUnlockState state;
    // A3: player progress as fixed-point 0..1000 (0 = 0%, 1000 = 100%).
    // We avoid float on the wire for cross-platform stability. The GUI
    // divides by 1000.0 to recover the 0..1 float.
    uint16_t progress;
    // A3: offset of the StatThresholdLabel string in the blob (0 = no label).
    // e.g. "12/50 kills" — the GUI renders this next to the progress bar.
    uint32_t statThresholdOff;
    // G4: offset of the UnlockTimestamp string in the blob (0 = no timestamp).
    // Stored as ISO 8601 string (e.g. "2024-03-15T18:30:00Z") for readability.
    // -1 from the SDK (not unlocked) is never stored; locked achievements
    // have unlockTimeOff == 0.
    uint32_t unlockTimeOff;
};

// ── AchUpdate payload ─────────────────────────────────────────────────────────
// Header: PktType::AchUpdate
// Payload: AchUpdatePkt

struct AchUpdatePkt {
    char            id[128];
    WireUnlockState state;
    // G4: POSIX epoch seconds from EOS_OnAchievementsUnlockedV2Info::UnlockTime.
    // -1 = not unlocked / no timestamp (older DLLs that don't send this field).
    int64_t         unlockTime;
};

// ── CmdUnlock payload ─────────────────────────────────────────────────────────
// Header: PktType::CmdUnlock
// Payload: CmdUnlockPkt

struct CmdUnlockPkt {
    char id[128];
};

// ── CmdUnlockAll / CmdRefresh — no payload (payloadSize = 0) ──────────────────

// ── LogPath payload ───────────────────────────────────────────────────────────
// Header: PktType::LogPath
// Payload: LogPathPkt — null-terminated UTF-8 absolute path to ScreamAPI.log

struct LogPathPkt {
    char path[MAX_PATH];
};

// ── DlcCatalog payload ────────────────────────────────────────────────────────
// Header: PktType::DlcCatalog
// Payload: DlcCatalogHeader, then `count` DlcCatalogEntry records, then string blob.
// Same blob pattern as AchList: null-terminated UTF-8 strings packed together.
// Sent once on connect after AchList. Re-sent if catalog is populated later.

struct DlcCatalogHeader {
    uint32_t count;
    uint32_t blobSize;
};

struct DlcCatalogEntry {
    uint32_t idOff;
    uint32_t titleOff;
};

// ── GameInfo payload ──────────────────────────────────────────────────────────
// Header: PktType::GameInfo
// Payload: GameInfoPkt
// Sent once after EOS_Platform_Create fires (which gives us SandboxId/ProductId).
// The GUI uses the sandbox ID to call external APIs (egdata, Epic GraphQL)
// for game name (A2) and achievement rarity (G4).

struct GameInfoPkt {
    char sandboxId[64];     // EOS_Platform_Options::SandboxId (null-term)
    char productId[64];     // EOS_Platform_Options::ProductId (null-term)
    char eosVersion[32];    // EOS_GetVersion() result (null-term)
};

#pragma pack(pop)
