#include "pch.h"
#include "eos_hooks.h"
#include "ScreamAPI.h"
#include "achievement_manager.h"
#include "util.h"
#include "eos_compat.h"
#include "eos_resolve.h"
#include "Logger.h"
#include "MinHook.h"
#include "dlc_catalog.h"
#include "eos-sdk/eos_logging.h"
#include "eos-sdk/eos_metrics.h"   // E1: EOS_Metrics_BeginPlayerSession / EndPlayerSession + their Options structs
#include <mutex>
#include <vector>
#include <queue>
#include <thread>
#include <atomic>
#include <map>
#include <fstream>

// ── A1: SDK log capture (separate file) ────────────────────────────────────
// The EOS SDK emits rich diagnostics (every backend call, network failure,
// token issue) via EOS_Logging_SetCallback. We route these to a separate
// ScreamAPI_SDK.log file so they don't drown out ScreamAPI's curated output
// in the main ScreamAPI.log, and so the DLC log parser doesn't see them.
// The path is set once from ScreamAPI::init (after Config::LogFilename() is
// known). Empty string = SDK logging disabled.
static std::wstring g_sdkLogPath;
static std::mutex    g_sdkLogMutex;
// B: persistent file handle. Previously we opened/closed the file
// PER LOG LINE (3 kernel syscalls per message: CreateFileW + WriteFile
// + CloseHandle). At Verbose + ALL_CATEGORIES the SDK can fire
// thousands of lines per second, which meant thousands of syscalls
// per second -- enough to starve game startup. We now open once
// (lazily, on first message) and keep the handle open.
static HANDLE g_sdkLogFile = INVALID_HANDLE_VALUE;

void SetSDKLogPath(const std::wstring& path) {
    std::lock_guard<std::mutex> lk(g_sdkLogMutex);
    // Close any previously-open handle
    if (g_sdkLogFile != INVALID_HANDLE_VALUE) {
        CloseHandle(g_sdkLogFile);
        g_sdkLogFile = INVALID_HANDLE_VALUE;
    }
    g_sdkLogPath = path;
}

// D: re-entrancy guard. std::mutex is NOT recursive -- if anything
// in the write path (CreateFileW, WriteFile, heap alloc) re-enters
// the EOS SDK and triggers another log message on the same thread,
// we would deadlock on the mutex we already hold. thread_local flag
// drops the re-entrant message instead of deadlocking.
static thread_local bool g_inSdkLogCallback = false;

static void EOS_CALL SdkLogCallback(const EOS_LogMessage* Message) {
    if (g_sdkLogPath.empty() || Message == nullptr) return;
    if (g_inSdkLogCallback) return;  // re-entrancy guard
    g_inSdkLogCallback = true;
    std::lock_guard<std::mutex> lk(g_sdkLogMutex);
    // B: lazy-open the file handle once, keep it open
    if (g_sdkLogFile == INVALID_HANDLE_VALUE) {
        g_sdkLogFile = CreateFileW(g_sdkLogPath.c_str(), FILE_APPEND_DATA,
                                    FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
        if (g_sdkLogFile == INVALID_HANDLE_VALUE) {
            g_inSdkLogCallback = false;
            return;
        }
    }
    // Format: [HH:MM:SS.mmm] [LEVEL] [Category] Message
    SYSTEMTIME t; GetLocalTime(&t);
    char header[64];
    sprintf_s(header, 64, "[%02d:%02d:%02d.%03d] ",
              t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    const char* levelStr = "???";
    switch (Message->Level) {
        case EOS_ELogLevel::EOS_LOG_Fatal:       levelStr = "FATAL"; break;
        case EOS_ELogLevel::EOS_LOG_Error:       levelStr = "ERROR"; break;
        case EOS_ELogLevel::EOS_LOG_Warning:     levelStr = "WARN";  break;
        case EOS_ELogLevel::EOS_LOG_Info:        levelStr = "INFO";  break;
        case EOS_ELogLevel::EOS_LOG_Verbose:     levelStr = "VERB";  break;
        case EOS_ELogLevel::EOS_LOG_VeryVerbose: levelStr = "VVERB"; break;
        default: break;
    }
    std::string line = std::string(header) + "[" + levelStr + "] ";
    if (Message->Category) line += "[" + std::string(Message->Category) + "] ";
    if (Message->Message)  line += Message->Message;
    line += "\r\n";
    DWORD written = 0;
    WriteFile(g_sdkLogFile, line.data(), (DWORD)line.size(), &written, nullptr);
    g_inSdkLogCallback = false;
}

// Defined in eos_ecom_entitlements.cpp — single source of truth for the catalog.
extern void EnsureCatalogFetched();
extern std::map<std::string, std::string> GetCatalogSnapshot();

namespace EOS_Hooks {

static bool hooksInitialized = false;
static HMODULE originalEOSDLL = nullptr;
static std::atomic<bool> g_bAchievementsConfigured{false};
static std::mutex g_configMutex;

// Original function pointers (filled by MinHook)
namespace Original {
    // Platform
    decltype(&EOS_Platform_Create) Platform_Create = nullptr;
    decltype(&EOS_Platform_Release) Platform_Release = nullptr;
    decltype(&EOS_Platform_Tick) Platform_Tick = nullptr;
    decltype(&EOS_Platform_GetConnectInterface) Platform_GetConnectInterface = nullptr;
    decltype(&EOS_Platform_GetAuthInterface) Platform_GetAuthInterface = nullptr;
    decltype(&EOS_Platform_GetAchievementsInterface) Platform_GetAchievementsInterface = nullptr;
    decltype(&EOS_Platform_GetEcomInterface) Platform_GetEcomInterface = nullptr;
    // Stats interface (for stat-gated achievement unlocking)
    decltype(&EOS_Platform_GetStatsInterface) Platform_GetStatsInterface = nullptr;
    // Optional
    decltype(&EOS_Platform_GetUIInterface) Platform_GetUIInterface = nullptr;

    // Achievements
    decltype(&EOS_Achievements_QueryDefinitions) Achievements_QueryDefinitions = nullptr;
    decltype(&EOS_Achievements_QueryPlayerAchievements) Achievements_QueryPlayerAchievements = nullptr;
    decltype(&EOS_Achievements_UnlockAchievements) Achievements_UnlockAchievements = nullptr;
    decltype(&EOS_Achievements_AddNotifyAchievementsUnlockedV2) Achievements_AddNotifyAchievementsUnlockedV2 = nullptr;
    // Deprecated version
    decltype(&EOS_Achievements_AddNotifyAchievementsUnlocked) Achievements_AddNotifyAchievementsUnlocked = nullptr;

    // Ecom
    decltype(&EOS_Ecom_QueryOwnership) Ecom_QueryOwnership = nullptr;
    decltype(&EOS_Ecom_QueryOwnershipBySandboxIds) Ecom_QueryOwnershipBySandboxIds = nullptr;
    decltype(&EOS_Ecom_QueryOwnershipToken) Ecom_QueryOwnershipToken = nullptr;
    decltype(&EOS_Ecom_QueryEntitlements) Ecom_QueryEntitlements = nullptr;
    decltype(&EOS_Ecom_GetEntitlementsCount) Ecom_GetEntitlementsCount = nullptr;
    decltype(&EOS_Ecom_CopyEntitlementByIndex) Ecom_CopyEntitlementByIndex = nullptr;
    decltype(&EOS_Ecom_Entitlement_Release) Ecom_Entitlement_Release = nullptr;

    // Connect
    decltype(&EOS_Connect_Login) Connect_Login = nullptr;
    decltype(&EOS_Connect_GetLoggedInUserByIndex) Connect_GetLoggedInUserByIndex = nullptr;
    // Optional
    decltype(&EOS_Connect_AddNotifyLoginStatusChanged) Connect_AddNotifyLoginStatusChanged = nullptr;

    // Auth
    decltype(&EOS_Auth_Login) Auth_Login = nullptr;
    decltype(&EOS_Auth_GetLoggedInAccountByIndex) Auth_GetLoggedInAccountByIndex = nullptr;
    // Optional
    decltype(&EOS_Auth_AddNotifyLoginStatusChanged) Auth_AddNotifyLoginStatusChanged = nullptr;

    // Metrics (E1: BlockMetrics config knob)
    decltype(&EOS_Metrics_BeginPlayerSession) Metrics_BeginPlayerSession = nullptr;
    decltype(&EOS_Metrics_EndPlayerSession) Metrics_EndPlayerSession = nullptr;

    // Logging (A1: not hooked, just resolved via GetProcAddress so we can
    // call EOS_Logging_SetCallback from inside the proxy DLL without
    // tripping the linker forwarder)
    decltype(&EOS_Logging_SetCallback) Logging_SetCallback = nullptr;
    decltype(&EOS_Logging_SetLogLevel) Logging_SetLogLevel = nullptr;
}

// Resolve an EOS SDK export by name. Returns the function pointer, or
// nullptr if not found. Delegates to EOS_Resolve::resolve (shared with
// ScreamAPI::proxyFunction and EOS_Compat::detectSDKVersion) so there is
// exactly one decoration walker in the codebase.
static void* ResolveExport(HMODULE module, const char* baseName) {
    return EOS_Resolve::resolve(module, baseName);
}

#define INSTALL_HOOK(module, funcName, hookFunc, originalPtr) \
    do { \
        void* targetFunc = ResolveExport(module, #funcName); \
        if (targetFunc) { \
            MH_STATUS status = MH_CreateHook(targetFunc, (void*)&hookFunc, (void**)&originalPtr); \
            if (status == MH_OK) { \
                status = MH_EnableHook(targetFunc); \
                if (status == MH_OK) { \
                    Logger::info("[HOOK] Successfully hooked: %s", #funcName); \
                } else { \
                    Logger::error("[HOOK] Failed to enable hook for %s: %d", #funcName, status); \
                    return false; \
                } \
            } else { \
                Logger::error("[HOOK] Failed to create hook for %s: %d", #funcName, status); \
                return false; \
            } \
        } else { \
            Logger::warn("[HOOK] Function not found (may be optional): %s", #funcName); \
        } \
    } while(0)

#define INSTALL_HOOK_OPTIONAL(module, funcName, hookFunc, originalPtr) \
    do { \
        void* targetFunc = ResolveExport(module, #funcName); \
        if (targetFunc) { \
            MH_STATUS status = MH_CreateHook(targetFunc, (void*)&hookFunc, (void**)&originalPtr); \
            if (status == MH_OK) { \
                status = MH_EnableHook(targetFunc); \
                if (status == MH_OK) { \
                    Logger::info("[HOOK] Successfully hooked (optional): %s", #funcName); \
                } else { \
                    Logger::warn("[HOOK] Failed to enable optional hook for %s: %d (continuing)", #funcName, status); \
                } \
            } else { \
                Logger::warn("[HOOK] Failed to create optional hook for %s: %d (continuing)", #funcName, status); \
            } \
        } else { \
            Logger::warn("[HOOK] Optional function not found: %s (continuing)", #funcName); \
        } \
    } while(0)

// ------------------------------------------------------------------
// Pending unlock storage and retry mechanism
// ------------------------------------------------------------------
struct PendingUnlock {
    EOS_HAchievements Handle;
    EOS_Achievements_UnlockAchievementsOptions Options;
    void* ClientData;
    EOS_Achievements_OnUnlockAchievementsCompleteCallback CompletionDelegate;
    // Deep-copied storage so Options.AchievementIds stays valid after
    // the caller returns. id_storage owns the string memory; id_array
    // is the const char** array pointing into id_storage.
    std::vector<std::string>  id_storage;
    std::vector<const char*>  id_array;
};
static std::vector<PendingUnlock> g_pendingUnlocks;
static std::mutex g_pendingMutex;
static std::atomic<int> g_forcedQueriesPending{0};

// GUI unlock queue — drained on the game thread inside Platform_Tick
static std::queue<std::string> g_guiUnlockQueue;
static std::mutex              g_guiQueueMutex;

static void RetryPendingUnlocks() {
    std::lock_guard<std::mutex> lock(g_pendingMutex);
    for (auto& p : g_pendingUnlocks) {
        Logger::info("[HOOK] Retrying pending unlock for achievement: %s", p.Options.AchievementIds[0]);
        Original::Achievements_UnlockAchievements(p.Handle, &p.Options, p.ClientData, p.CompletionDelegate);
    }
    g_pendingUnlocks.clear();
}

static void ForceAchievementsConfiguration(EOS_HAchievements handle, EOS_ProductUserId userId) {
    if (g_bAchievementsConfigured) return;
    std::lock_guard<std::mutex> lock(g_configMutex);
    if (g_bAchievementsConfigured) return;

    Logger::info("[HOOK] Forcing achievements configuration (QueryDefinitions + QueryPlayerAchievements)");

    g_forcedQueriesPending = 2;

    // ApiVersion=1 is the safe backward-compatible choice: it tells the SDK
    // to use the v1 struct layout (which exists in all SDK versions). Bumping
    // the ApiVersion would make the SDK look for fields that may not exist in
    // older SDKs, causing mismatches. The ForceAchievementsConfig option exists
    // specifically to force v1 layout for v2+ SDKs.
    EOS_Achievements_QueryDefinitionsOptions defOpts = {1, userId, nullptr, nullptr, 0};
    Original::Achievements_QueryDefinitions(handle, &defOpts, nullptr,
        [](const EOS_Achievements_OnQueryDefinitionsCompleteCallbackInfo* Data) {
            Logger::debug("[HOOK] Forced QueryDefinitions result: %s", EOS_EResult_ToString(Data->ResultCode));
            if (--g_forcedQueriesPending == 0) {
                g_bAchievementsConfigured = true;
                RetryPendingUnlocks();
            }
        });

    EOS_Achievements_QueryPlayerAchievementsOptions playerOpts = {1, userId};
    Original::Achievements_QueryPlayerAchievements(handle, &playerOpts, nullptr,
        [](const EOS_Achievements_OnQueryPlayerAchievementsCompleteCallbackInfo* Data) {
            Logger::debug("[HOOK] Forced QueryPlayerAchievements result: %s", EOS_EResult_ToString(Data->ResultCode));
            if (--g_forcedQueriesPending == 0) {
                g_bAchievementsConfigured = true;
                RetryPendingUnlocks();
            }
        });
}

// ============================================================================
// HOOK IMPLEMENTATIONS
// ============================================================================

namespace Hooks {

// Platform hooks
EOS_HPlatform EOS_CALL Platform_Create(const EOS_Platform_Options* Options) {
    Logger::info("[HOOK] EOS_Platform_Create called");
    if (Options) {
        Logger::debug("[HOOK]   ApiVersion: %d", Options->ApiVersion);
        Logger::debug("[HOOK]   ProductId: %s", Options->ProductId ? Options->ProductId : "NULL");
        Logger::debug("[HOOK]   Flags: %llu", Options->Flags);
        if (Options->SandboxId && Options->SandboxId[0] != '\0') {
            Util::g_namespace_id = Options->SandboxId;
            Logger::info("[HOOK]   Captured namespace_id: %s", Options->SandboxId);
        }
        if (Options->ProductId && Options->ProductId[0] != '\0') {
            Util::g_product_id = Options->ProductId;
            Logger::info("[HOOK]   Captured product_id: %s", Options->ProductId);
        }
        if (Config::ForceEpicOverlay()) {
            auto mOptions = const_cast<EOS_Platform_Options*>(Options);
            mOptions->Flags = 0;
            Logger::debug("[HOOK]   Disabled Epic Overlay (Flags set to 0)");
        }
    }
    EOS_HPlatform result = Original::Platform_Create(Options);
    Util::hPlatform = result;
    Logger::info("[HOOK] Platform created: %p", result);

    // ── A1: Register SDK log callback (DEFERRED) ──────────────────────────
    // Previously this ran INSIDE the Platform_Create hook, which meant the
    // SDK started firing log messages while Platform_Create was still on
    // the stack. Subsequent internal SDK work funneled through our slow
    // file-I/O callback, stretching out the most timing-sensitive call.
    // Games with strict startup timeouts would abort.
    //
    // Fix: spawn a background thread that sleeps 500ms (so Platform_Create
    // has returned and the game is past the critical init window), THEN
    // registers the callback. Also gated by Config::EnableSDKLog() so the
    // feature is OPT-IN (default off -- Verbose SDK logging can still lag
    // startup even with the deferred registration).
    if (!g_sdkLogPath.empty() && Config::EnableSDKLog()) {
        std::thread([]() {
            Sleep(500);  // let Platform_Create return + game settle
            if (!Original::Logging_SetCallback || !Original::Logging_SetLogLevel) {
                Logger::warn("[HOOK] SDK log functions not resolved in original DLL -- SDK log disabled");
                return;
            }
            // Map config string -> EOS_ELogLevel. Default to Warning
            // (NOT Verbose) -- Verbose produces thousands of lines per
            // second and can lag game startup even with deferred registration.
            std::string lvlStr = Config::SDKLogLevel();
            EOS_ELogLevel lvl = EOS_ELogLevel::EOS_LOG_Warning;
            if      (lvlStr == "Off")         return;  // disabled entirely
            else if (lvlStr == "Fatal")       lvl = EOS_ELogLevel::EOS_LOG_Fatal;
            else if (lvlStr == "Error")       lvl = EOS_ELogLevel::EOS_LOG_Error;
            else if (lvlStr == "Warning")     lvl = EOS_ELogLevel::EOS_LOG_Warning;
            else if (lvlStr == "Info")        lvl = EOS_ELogLevel::EOS_LOG_Info;
            else if (lvlStr == "Verbose")     lvl = EOS_ELogLevel::EOS_LOG_Verbose;
            else if (lvlStr == "VeryVerbose") lvl = EOS_ELogLevel::EOS_LOG_VeryVerbose;
            // Unknown value -> fall back to Warning (logged once)
            else Logger::warn("[HOOK] Unknown SDKLogLevel \"%s\", falling back to Warning", lvlStr.c_str());

            EOS_EResult cbRes = Original::Logging_SetCallback(SdkLogCallback);
            if (cbRes == EOS_EResult::EOS_Success) {
                Original::Logging_SetLogLevel(EOS_ELogCategory::EOS_LC_ALL_CATEGORIES, lvl);
                Logger::info("[HOOK] SDK log callback registered (level=%s) -> %ls",
                             lvlStr.c_str(), g_sdkLogPath.c_str());
            } else {
                Logger::warn("[HOOK] EOS_Logging_SetCallback failed: %s",
                             EOS_EResult_ToString(cbRes));
            }
        }).detach();
    } else if (!g_sdkLogPath.empty() && !Config::EnableSDKLog()) {
        Logger::info("[HOOK] SDK log capture disabled (EnableSDKLog=false in config)");
    }

    // Trigger achievement manager init unconditionally (achievements work
    // with the overlay disabled). init() is idempotent so it is safe if the
    // polling thread in ScreamAPI::init races us.
    if (result) {
        std::thread([]() {
            Sleep(500);
            Logger::info("[HOOK] Triggering achievement manager initialization");
            AchievementManager::init();
        }).detach();
    }
    return result;
}

void EOS_CALL Platform_Release(EOS_HPlatform Handle) {
    Logger::info("[HOOK] EOS_Platform_Release called");
    Original::Platform_Release(Handle);
    Util::hPlatform = nullptr;
}

void EOS_CALL Platform_Tick(EOS_HPlatform Handle) {
    // Drain GUI unlock queue on the game thread (safe to call EOS here)
    {
        std::lock_guard<std::mutex> lk(g_guiQueueMutex);
        while (!g_guiUnlockQueue.empty()) {
            const std::string& id = g_guiUnlockQueue.front();
            Logger::info("[HOOK] Platform_Tick: draining GUI unlock: %s", id.c_str());
            AchievementManager::findAchievement(id.c_str(), [](Overlay_Achievement& a) {
                if (a.UnlockState == UnlockState::Locked)
                    AchievementManager::unlockAchievement(&a);
            });
            g_guiUnlockQueue.pop();
        }
    }
    Original::Platform_Tick(Handle);
}

EOS_HConnect EOS_CALL Platform_GetConnectInterface(EOS_HPlatform Handle) {
    auto result = Original::Platform_GetConnectInterface(Handle);
    if (result) Logger::debug("[HOOK] EOS_Platform_GetConnectInterface -> %p", result);
    return result;
}

EOS_HAuth EOS_CALL Platform_GetAuthInterface(EOS_HPlatform Handle) {
    auto result = Original::Platform_GetAuthInterface(Handle);
    if (result) Logger::debug("[HOOK] EOS_Platform_GetAuthInterface -> %p", result);
    return result;
}

EOS_HAchievements EOS_CALL Platform_GetAchievementsInterface(EOS_HPlatform Handle) {
    auto result = Original::Platform_GetAchievementsInterface(Handle);
    if (result) Logger::debug("[HOOK] EOS_Platform_GetAchievementsInterface -> %p", result);
    return result;
}

EOS_HEcom EOS_CALL Platform_GetEcomInterface(EOS_HPlatform Handle) {
    auto result = Original::Platform_GetEcomInterface(Handle);
    if (result) Logger::debug("[HOOK] EOS_Platform_GetEcomInterface -> %p", result);
    return result;
}

EOS_HStats EOS_CALL Platform_GetStatsInterface(EOS_HPlatform Handle) {
    auto result = Original::Platform_GetStatsInterface(Handle);
    // Runtime Stats interface availability check.
    // - Non-null: game has Stats enabled in its EOS config; stat-gated
    //   achievements can be unlocked via EOS_Stats_IngestStat.
    // - Null: game disabled Stats (or SDK is too old to expose the
    //   interface). Stat-gated achievements will not unlock through us.
    // See also the static SDK check logged once at DLL load in ScreamAPI.cpp.
    if (result) {
        Logger::info("[STATS] EOS_Platform_GetStatsInterface -> %p (Stats interface available)", result);
    } else {
        Logger::warn("[STATS] EOS_Platform_GetStatsInterface -> NULL (Stats interface NOT available - stat-gated achievements may not unlock)");
    }
    return result;
}

EOS_HUI EOS_CALL Platform_GetUIInterface(EOS_HPlatform Handle) {
    auto result = Original::Platform_GetUIInterface(Handle);
    if (result) Logger::debug("[HOOK] EOS_Platform_GetUIInterface -> %p", result);
    return result;
}

// Achievements hooks
void EOS_CALL Achievements_QueryDefinitions(EOS_HAchievements Handle, const EOS_Achievements_QueryDefinitionsOptions* Options, void* ClientData, const EOS_Achievements_OnQueryDefinitionsCompleteCallback CompletionDelegate) {
    Logger::debug("[HOOK] EOS_Achievements_QueryDefinitions called");
    Original::Achievements_QueryDefinitions(Handle, Options, ClientData, CompletionDelegate);
}

void EOS_CALL Achievements_QueryPlayerAchievements(EOS_HAchievements Handle, const EOS_Achievements_QueryPlayerAchievementsOptions* Options, void* ClientData, const EOS_Achievements_OnQueryPlayerAchievementsCompleteCallback CompletionDelegate) {
    Logger::debug("[HOOK] EOS_Achievements_QueryPlayerAchievements called");
    Original::Achievements_QueryPlayerAchievements(Handle, Options, ClientData, CompletionDelegate);
}

void EOS_CALL Achievements_UnlockAchievements(EOS_HAchievements Handle, const EOS_Achievements_UnlockAchievementsOptions* Options, void* ClientData, const EOS_Achievements_OnUnlockAchievementsCompleteCallback CompletionDelegate) {
    Logger::info("[HOOK] EOS_Achievements_UnlockAchievements called");
    if (Options) {
        Logger::info("[HOOK]   ApiVersion: %d", Options->ApiVersion);
        Logger::info("[HOOK]   UserId: %p", Options->UserId);
        Logger::info("[HOOK]   AchievementsCount: %u", Options->AchievementsCount);
        for (uint32_t i = 0; i < Options->AchievementsCount; i++) {
            Logger::info("[HOOK]     Achievement ID: %s", Options->AchievementIds[i]);
        }
    } else {
        Logger::warn("[HOOK]   Options is NULL!");
    }

    auto currentUserId = Util::getProductUserId();
    auto hAchievements = Util::getHAchievements();
    Logger::info("[HOOK]   Current Util::getProductUserId() = %p", currentUserId);
    Logger::info("[HOOK]   Current Util::getHAchievements() = %p", hAchievements);
    Logger::info("[HOOK]   Handle passed to hook = %p", Handle);

    if (Options && Options->UserId != currentUserId) {
        Logger::warn("[HOOK]   UserId mismatch! Hook Options->UserId (%p) != current Util::getProductUserId() (%p)", Options->UserId, currentUserId);
    }

    // If achievements not yet configured AND the config option is enabled, force configuration and postpone unlock
    if (!g_bAchievementsConfigured && Options && Options->UserId && Config::ForceAchievementsConfig()) {
        Logger::warn("[HOOK] Achievements not configured yet – forcing configuration and postponing unlock");
        ForceAchievementsConfiguration(Handle, Options->UserId);
        std::lock_guard<std::mutex> lock(g_pendingMutex);
        PendingUnlock p;
        p.Handle             = Handle;
        p.Options            = *Options;          // shallow copy is fine for ApiVersion/UserId/Count
        p.ClientData         = ClientData;
        p.CompletionDelegate = CompletionDelegate;
        // Deep-copy AchievementIds: the caller's array is freed once we
        // return, so we must own both the string storage and the pointer
        // array that Options.AchievementIds will point at when
        // RetryPendingUnlocks() replays the call later.
        p.id_storage.reserve(Options->AchievementsCount);
        p.id_array.reserve(Options->AchievementsCount);
        for (uint32_t i = 0; i < Options->AchievementsCount; i++) {
            p.id_storage.emplace_back(Options->AchievementIds[i]);
        }
        for (auto& s : p.id_storage) {
            p.id_array.push_back(s.c_str());
        }
        p.Options.AchievementIds    = p.id_array.data();
        p.Options.AchievementsCount = (uint32_t)p.id_array.size();
        g_pendingUnlocks.push_back(std::move(p));
        return;
    }

    // Otherwise proceed normally
    Original::Achievements_UnlockAchievements(Handle, Options, ClientData, CompletionDelegate);
}

EOS_NotificationId EOS_CALL Achievements_AddNotifyAchievementsUnlockedV2(EOS_HAchievements Handle, const EOS_Achievements_AddNotifyAchievementsUnlockedV2Options* Options, void* ClientData, const EOS_Achievements_OnAchievementsUnlockedCallbackV2 NotificationFn) {
    Logger::debug("[HOOK] EOS_Achievements_AddNotifyAchievementsUnlockedV2 called");
    // Fix #4: If V2 function not available in older SDK, return invalid notification ID
    if (!Original::Achievements_AddNotifyAchievementsUnlockedV2) {
        Logger::warn("[HOOK] AddNotifyAchievementsUnlockedV2 not available in this SDK version - returning EOS_INVALID_NOTIFICATIONID");
        return EOS_INVALID_NOTIFICATIONID;
    }
    return Original::Achievements_AddNotifyAchievementsUnlockedV2(Handle, Options, ClientData, NotificationFn);
}

EOS_NotificationId EOS_CALL Achievements_AddNotifyAchievementsUnlocked(EOS_HAchievements Handle, const EOS_Achievements_AddNotifyAchievementsUnlockedOptions* Options, void* ClientData, const EOS_Achievements_OnAchievementsUnlockedCallback NotificationFn) {
    Logger::debug("[HOOK] EOS_Achievements_AddNotifyAchievementsUnlocked (deprecated) called");
    // Fix #4: Guard against missing function in older SDKs
    if (!Original::Achievements_AddNotifyAchievementsUnlocked) {
        Logger::warn("[HOOK] AddNotifyAchievementsUnlocked not available - returning EOS_INVALID_NOTIFICATIONID");
        return EOS_INVALID_NOTIFICATIONID;
    }
    return Original::Achievements_AddNotifyAchievementsUnlocked(Handle, Options, ClientData, NotificationFn);
}

// ============================================================================
// Ecom hooks — actual DLC logic (proxy mode equivalent, using Original:: trampolines)
// ============================================================================

// Pre-built ownership list for the ForceSuccess fallback path.
// g_ownership_id_storage owns the string memory; g_ownerships[i].Id points
// into it. Both are cleared together on every QueryOwnership call - no leak.
static std::vector<std::string>            g_ownership_id_storage;
static std::vector<EOS_Ecom_ItemOwnership> g_ownerships;

// Entitlement state — rebuilt on every QueryEntitlements call.
static std::map<std::string, std::string> g_entitlement_map;
static std::vector<std::string>           g_entitlement_ids;

static void AutoFetchEntitlements() {
    EnsureCatalogFetched();
    auto catalog = GetCatalogSnapshot();
    for (auto& [id, title] : catalog) {
        if (Config::IsDlcUnlocked(id, false)) {
            Logger::debug("[HOOK]   Auto-fetch adding: %s", id.c_str());
            g_entitlement_map[id] = title;
        }
    }
}


static void InjectExtraEntitlements() {
    for (auto& [id, title] : Config::ExtraEntitlements()) {
        if (Config::IsDlcUnlocked(id, true)) {
            Logger::debug("[HOOK]   Config adding: %s", id.c_str());
            g_entitlement_map[id] = title;
        }
    }
}

static EOS_Ecom_Entitlement* MakeEntitlement(const std::string& id, const std::string& title) {
    auto* e = new EOS_Ecom_Entitlement{};
    e->ApiVersion      = EOS_ECOM_ENTITLEMENT_API_LATEST;
    e->EntitlementId   = id.c_str();
    e->CatalogItemId   = id.c_str();
    e->EntitlementName = title.c_str();
    e->bRedeemed       = false;
    e->EndTimestamp    = -1;
    e->ServerIndex     = -1;
    return e;
}

void EOS_CALL Ecom_QueryOwnership(EOS_HEcom Handle, const EOS_Ecom_QueryOwnershipOptions* Options, void* ClientData, const EOS_Ecom_OnQueryOwnershipCallback CompletionDelegate) {
    Logger::info("[HOOK] EOS_Ecom_QueryOwnership called");

    EnsureCatalogFetched();
    auto catalog = GetCatalogSnapshot();

    g_ownership_id_storage.clear();
    g_ownerships.clear();
    if (Options) {
        Logger::dlc("[HOOK] Game queried ownership of %d item(s):", Options->CatalogItemIdCount);
        g_ownership_id_storage.reserve(Options->CatalogItemIdCount);
        g_ownerships.reserve(Options->CatalogItemIdCount);
        for (uint32_t i = 0; i < Options->CatalogItemIdCount; i++) {
            const char* id = Options->CatalogItemIds[i];
            auto it = catalog.find(id);
            const char* title = (it != catalog.end()) ? it->second.c_str() : "Unknown Title";
            Logger::dlc("[HOOK]   Item ID: %s (\"%s\")", id, title);
            bool unlocked = Config::IsDlcUnlocked(std::string(id), true);
            // Store the id in g_ownership_id_storage so the pointer stays
            // alive until the next QueryOwnership call clears it.
            g_ownership_id_storage.emplace_back(id);
            g_ownerships.emplace_back(EOS_Ecom_ItemOwnership{
                EOS_ECOM_ITEMOWNERSHIP_API_LATEST,
                g_ownership_id_storage.back().c_str(),
                unlocked ? EOS_EOwnershipStatus::EOS_OS_Owned : EOS_EOwnershipStatus::EOS_OS_NotOwned
            });
        }
    } else {
        Logger::warn("[HOOK] Game queried DLC ownership without Options parameter");
    }

    if (Config::EnableOwnershipUnlocker()) {
        struct Container {
            void* ClientData;
            EOS_Ecom_OnQueryOwnershipCallback CompletionDelegate;
        };
        auto* container = new Container{ClientData, CompletionDelegate};
        Original::Ecom_QueryOwnership(Handle, Options, container,
            [](const EOS_Ecom_QueryOwnershipCallbackInfo* Data) {
                auto* c = static_cast<Container*>(Data->ClientData);
                auto* mData = const_cast<EOS_Ecom_QueryOwnershipCallbackInfo*>(Data);

                if (mData->ResultCode != EOS_EResult::EOS_Success) {
                    Logger::warn("[HOOK] EOS_Ecom_QueryOwnership failed: %s",
                        EOS_EResult_ToString(mData->ResultCode));
                    if (Config::ForceSuccess()) {
                        Logger::warn("[HOOK] Forcing EOS_Success");
                        mData->ItemOwnershipCount = (uint32_t)g_ownerships.size();
                        mData->ItemOwnership      = g_ownerships.data();
                        mData->ResultCode         = EOS_EResult::EOS_Success;
                    }
                }

                Logger::dlc("[HOOK] Responding with %d ownership(s):", mData->ItemOwnershipCount);
                for (uint32_t i = 0; i < mData->ItemOwnershipCount; i++) {
                    auto* item = const_cast<EOS_Ecom_ItemOwnership*>(mData->ItemOwnership + i);
                    bool original = (item->OwnershipStatus == EOS_EOwnershipStatus::EOS_OS_Owned);
                    bool unlocked = Config::IsDlcUnlocked(std::string(item->Id), original);
                    item->OwnershipStatus = unlocked
                        ? EOS_EOwnershipStatus::EOS_OS_Owned
                        : EOS_EOwnershipStatus::EOS_OS_NotOwned;
                    auto snap = GetCatalogSnapshot();
                    auto cit = snap.find(item->Id);
                    const char* title = (cit != snap.end()) ? cit->second.c_str() : "Unknown Title";
                    Logger::dlc("[HOOK]   [%s] %s (\"%s\")", unlocked ? "Owned" : "Not Owned", item->Id, title);
                }

                mData->ClientData = c->ClientData;
                c->CompletionDelegate(Data);
                delete c;
            }
        );
    } else {
        Original::Ecom_QueryOwnership(Handle, Options, ClientData, CompletionDelegate);
    }
}

void EOS_CALL Ecom_QueryEntitlements(EOS_HEcom Handle, const EOS_Ecom_QueryEntitlementsOptions* Options, void* ClientData, const EOS_Ecom_OnQueryEntitlementsCallback CompletionDelegate) {
    Logger::info("[HOOK] EOS_Ecom_QueryEntitlements called");

    if (!Config::EnableEntitlementUnlocker()) {
        Original::Ecom_QueryEntitlements(Handle, Options, ClientData, CompletionDelegate);
        return;
    }

    g_entitlement_map.clear();
    g_entitlement_ids.clear();

    Logger::dlc("[HOOK] Game queried %d entitlement(s):", Options->EntitlementNameCount);
    for (uint32_t i = 0; i < Options->EntitlementNameCount; i++) {
        const char* id = Options->EntitlementNames[i];
        Logger::dlc("[HOOK]   %s", id);
        if (Config::IsDlcUnlocked(std::string(id), true))
            g_entitlement_map[id] = "Unknown Title";
    }

    struct Container {
        void* ClientData;
        EOS_Ecom_OnQueryEntitlementsCallback CompletionDelegate;
    };
    auto* container = new Container{ClientData, CompletionDelegate};
    Original::Ecom_QueryEntitlements(Handle, Options, container,
        [](const EOS_Ecom_QueryEntitlementsCallbackInfo* Data) {
            auto* c = static_cast<Container*>(Data->ClientData);
            auto* mData = const_cast<EOS_Ecom_QueryEntitlementsCallbackInfo*>(Data);
            try {
                AutoFetchEntitlements();
                InjectExtraEntitlements();

                g_entitlement_ids.clear();
                for (auto& [id, title] : g_entitlement_map)
                    g_entitlement_ids.push_back(id);

                Logger::dlc("[HOOK] Responding with %zu entitlement(s):", g_entitlement_map.size());
                for (auto& [id, title] : g_entitlement_map)
                    Logger::dlc("[HOOK]   %s = \"%s\"", id.c_str(), title.c_str());

                mData->ResultCode = EOS_EResult::EOS_Success;
            } catch (const std::exception& e) {
                Logger::error("[HOOK] QueryEntitlements callback error: %s", e.what());
            }
            mData->ClientData = c->ClientData;
            c->CompletionDelegate(Data);
            delete c;
        }
    );
}

uint32_t EOS_CALL Ecom_GetEntitlementsCount(EOS_HEcom Handle, const EOS_Ecom_GetEntitlementsCountOptions* Options) {
    Logger::debug("[HOOK] EOS_Ecom_GetEntitlementsCount called");
    if (!Config::EnableEntitlementUnlocker()) {
        return Original::Ecom_GetEntitlementsCount(Handle, Options);
    }
    const auto count = (uint32_t)g_entitlement_map.size();
    Logger::debug("[HOOK] GetEntitlementsCount: %u", count);
    return count;
}

EOS_EResult EOS_CALL Ecom_CopyEntitlementByIndex(EOS_HEcom Handle, const EOS_Ecom_CopyEntitlementByIndexOptions* Options, EOS_Ecom_Entitlement** OutEntitlement) {
    Logger::debug("[HOOK] EOS_Ecom_CopyEntitlementByIndex called");
    if (!Config::EnableEntitlementUnlocker()) {
        return Original::Ecom_CopyEntitlementByIndex(Handle, Options, OutEntitlement);
    }
    const auto index = Options->EntitlementIndex;
    if (index >= g_entitlement_ids.size()) {
        Logger::warn("[HOOK] CopyEntitlementByIndex: index %u out of bounds (%zu)", index, g_entitlement_ids.size());
        return EOS_EResult::EOS_NotFound;
    }
    const auto& id    = g_entitlement_ids[index];
    const auto& title = g_entitlement_map.at(id);
    Logger::dlc("[HOOK] CopyEntitlementByIndex[%u]: %s", index, id.c_str());
    *OutEntitlement = MakeEntitlement(id, title);
    return EOS_EResult::EOS_Success;
}

void EOS_CALL Ecom_Entitlement_Release(EOS_Ecom_Entitlement* Entitlement) {
    if (Entitlement) {
        Logger::debug("[HOOK] EOS_Ecom_Entitlement_Release: %s", Entitlement->EntitlementId);
        delete Entitlement;
    } else {
        Logger::warn("[HOOK] EOS_Ecom_Entitlement_Release: null entitlement");
    }
}

// B1: Hook EOS_Ecom_QueryOwnershipBySandboxIds to apply the same unlock
// logic as EOS_Ecom_QueryOwnership. Some multi-title launchers query ownership
// per-sandbox instead of per-item; without this hook, DLC unlocks fail silently
// for those titles. The callback walks each SandboxIdItemOwnership and logs
// the per-item ownership state according to Config::IsDlcUnlocked.
//
// NOTE: We cannot expand the SDK-owned OwnedCatalogItemIds array in place
// (it would require allocating a new array and replacing the pointer, which
// risks a use-after-free when the SDK frees its copy). For now this hook
// logs the unlock-eligible items and applies ForceSuccess if the original
// query failed. A future revision that needs to ADD items the SDK didn't
// return will need to allocate a per-callback owned vector and swap it in.
void EOS_CALL Ecom_QueryOwnershipBySandboxIds(EOS_HEcom Handle, const EOS_Ecom_QueryOwnershipBySandboxIdsOptions* Options, void* ClientData, const EOS_Ecom_OnQueryOwnershipBySandboxIdsCallback CompletionDelegate) {
    Logger::info("[HOOK] EOS_Ecom_QueryOwnershipBySandboxIds called");

    if (!Config::EnableOwnershipUnlocker()) {
        Original::Ecom_QueryOwnershipBySandboxIds(Handle, Options, ClientData, CompletionDelegate);
        return;
    }

    EnsureCatalogFetched();
    auto catalog = GetCatalogSnapshot();

    if (Options && Options->SandboxIdsCount > 0) {
        Logger::dlc("[HOOK] Sandbox ownership query: %u sandbox(s)", Options->SandboxIdsCount);
        for (uint32_t i = 0; i < Options->SandboxIdsCount; i++) {
            Logger::dlc("[HOOK]   SandboxId: %S", Options->SandboxIds[i]);
        }
    }

    struct Container {
        void* ClientData;
        EOS_Ecom_OnQueryOwnershipBySandboxIdsCallback CompletionDelegate;
    };
    auto* container = new Container{ClientData, CompletionDelegate};
    Original::Ecom_QueryOwnershipBySandboxIds(Handle, Options, container,
        [](const EOS_Ecom_QueryOwnershipBySandboxIdsCallbackInfo* Data) {
            auto* c = static_cast<Container*>(Data->ClientData);
            auto* mData = const_cast<EOS_Ecom_QueryOwnershipBySandboxIdsCallbackInfo*>(Data);

            if (mData->ResultCode != EOS_EResult::EOS_Success) {
                Logger::warn("[HOOK] EOS_Ecom_QueryOwnershipBySandboxIds failed: %s",
                    EOS_EResult_ToString(mData->ResultCode));
                if (Config::ForceSuccess()) {
                    Logger::warn("[HOOK] Forcing EOS_Success");
                    mData->ResultCode = EOS_EResult::EOS_Success;
                }
            }

            // Log per-sandbox ownership for diagnostics + apply unlock flag
            // to existing entries. Items the SDK didn't return as owned but
            // that Config says should be unlocked are logged but NOT added
            // to the array (see NOTE above).
            if (mData->ResultCode == EOS_EResult::EOS_Success && mData->SandboxIdItemOwnershipsCount > 0) {
                Logger::dlc("[HOOK] Responding with %u sandbox ownership group(s):",
                            mData->SandboxIdItemOwnershipsCount);
                for (uint32_t s = 0; s < mData->SandboxIdItemOwnershipsCount; s++) {
                    auto* sb = const_cast<EOS_Ecom_SandboxIdItemOwnership*>(mData->SandboxIdItemOwnerships + s);
                    Logger::dlc("[HOOK]   Sandbox %S: %u owned item(s)",
                                sb->SandboxId, sb->OwnedCatalogItemIdsCount);
                    for (uint32_t i = 0; i < sb->OwnedCatalogItemIdsCount; i++) {
                        const char* itemId = sb->OwnedCatalogItemIds[i];
                        bool unlocked = Config::IsDlcUnlocked(std::string(itemId), true);
                        Logger::dlc("[HOOK]     [%s] %s",
                                    unlocked ? "Owned" : "Not Owned", itemId);
                    }
                }
            }

            mData->ClientData = c->ClientData;
            c->CompletionDelegate(Data);
            delete c;
        }
    );
}

void EOS_CALL Ecom_QueryOwnershipToken(EOS_HEcom Handle, const EOS_Ecom_QueryOwnershipTokenOptions* Options, void* ClientData, const EOS_Ecom_OnQueryOwnershipTokenCallback CompletionDelegate) {
    Logger::debug("[HOOK] EOS_Ecom_QueryOwnershipToken called");
    Original::Ecom_QueryOwnershipToken(Handle, Options, ClientData, CompletionDelegate);
}

// Connect hooks
void EOS_CALL Connect_Login(EOS_HConnect Handle, const EOS_Connect_LoginOptions* Options, void* ClientData, const EOS_Connect_OnLoginCallback CompletionDelegate) {
    Logger::info("[HOOK] EOS_Connect_Login called");
    Original::Connect_Login(Handle, Options, ClientData, CompletionDelegate);
}

EOS_ProductUserId EOS_CALL Connect_GetLoggedInUserByIndex(EOS_HConnect Handle, int32_t Index) {
    auto result = Original::Connect_GetLoggedInUserByIndex(Handle, Index);
    if (result) Logger::debug("[HOOK] EOS_Connect_GetLoggedInUserByIndex[%d] -> %p", Index, result);
    return result;
}

void EOS_CALL Connect_AddNotifyLoginStatusChanged(EOS_HConnect Handle, const EOS_Connect_AddNotifyLoginStatusChangedOptions* Options, void* ClientData, const EOS_Connect_OnLoginStatusChangedCallback NotificationFn) {
    Logger::debug("[HOOK] EOS_Connect_AddNotifyLoginStatusChanged called");
    Original::Connect_AddNotifyLoginStatusChanged(Handle, Options, ClientData, NotificationFn);
}

// Auth hooks
void EOS_CALL Auth_Login(EOS_HAuth Handle, const EOS_Auth_LoginOptions* Options, void* ClientData, const EOS_Auth_OnLoginCallback CompletionDelegate) {
    Logger::info("[HOOK] EOS_Auth_Login called");
    Original::Auth_Login(Handle, Options, ClientData, CompletionDelegate);
}

EOS_EpicAccountId EOS_CALL Auth_GetLoggedInAccountByIndex(EOS_HAuth Handle, int32_t Index) {
    auto result = Original::Auth_GetLoggedInAccountByIndex(Handle, Index);
    if (result) Logger::debug("[HOOK] EOS_Auth_GetLoggedInAccountByIndex[%d] -> %p", Index, result);
    return result;
}

void EOS_CALL Auth_AddNotifyLoginStatusChanged(EOS_HAuth Handle, const EOS_Auth_AddNotifyLoginStatusChangedOptions* Options, void* ClientData, const EOS_Auth_OnLoginStatusChangedCallback NotificationFn) {
    Logger::debug("[HOOK] EOS_Auth_AddNotifyLoginStatusChanged called");
    Original::Auth_AddNotifyLoginStatusChanged(Handle, Options, ClientData, NotificationFn);
}

// ── Metrics hooks (E1: BlockMetrics config knob) ──────────────────────────
// When Config::BlockMetrics() is true, no-op both BeginPlayerSession and
// EndPlayerSession — game telemetry stays local. The config knob existed in
// the INI for years but was never read; this finally wires it up.
EOS_EResult EOS_CALL Metrics_BeginPlayerSession(EOS_HMetrics Handle, const EOS_Metrics_BeginPlayerSessionOptions* Options) {
    if (Config::BlockMetrics()) {
        Logger::info("[HOOK] EOS_Metrics_BeginPlayerSession blocked (BlockMetrics=true)");
        return EOS_EResult::EOS_Success;
    }
    return Original::Metrics_BeginPlayerSession(Handle, Options);
}

EOS_EResult EOS_CALL Metrics_EndPlayerSession(EOS_HMetrics Handle, const EOS_Metrics_EndPlayerSessionOptions* Options) {
    if (Config::BlockMetrics()) {
        Logger::debug("[HOOK] EOS_Metrics_EndPlayerSession blocked (BlockMetrics=true)");
        return EOS_EResult::EOS_Success;
    }
    return Original::Metrics_EndPlayerSession(Handle, Options);
}

} // namespace Hooks

// ============================================================================
// HOOK INITIALIZATION
// ============================================================================

bool InitializeHooks(HMODULE originalDLL) {
    Logger::debug("[HOOK] Entering InitializeHooks");
    if (hooksInitialized) {
        Logger::warn("[HOOK] Hooks already initialized");
        return true;
    }
    if (!originalDLL) {
        Logger::error("[HOOK] Invalid DLL handle");
        return false;
    }
    originalEOSDLL = originalDLL;

    Logger::info("[HOOK] ========================================");
    Logger::info("[HOOK] Initializing MinHook-based EOS hooking");
    Logger::info("[HOOK] ========================================");

    MH_STATUS status = MH_Initialize();
    if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) {
        Logger::error("[HOOK] MH_Initialize failed: %d", status);
        return false;
    }
    Logger::info("[HOOK] MinHook initialized successfully");

    Logger::info("[HOOK] Installing Platform hooks...");
    INSTALL_HOOK(originalDLL, EOS_Platform_Create, Hooks::Platform_Create, Original::Platform_Create);
    INSTALL_HOOK(originalDLL, EOS_Platform_Release, Hooks::Platform_Release, Original::Platform_Release);
    INSTALL_HOOK(originalDLL, EOS_Platform_Tick, Hooks::Platform_Tick, Original::Platform_Tick);
    INSTALL_HOOK(originalDLL, EOS_Platform_GetConnectInterface, Hooks::Platform_GetConnectInterface, Original::Platform_GetConnectInterface);
    INSTALL_HOOK(originalDLL, EOS_Platform_GetAuthInterface, Hooks::Platform_GetAuthInterface, Original::Platform_GetAuthInterface);
    INSTALL_HOOK(originalDLL, EOS_Platform_GetAchievementsInterface, Hooks::Platform_GetAchievementsInterface, Original::Platform_GetAchievementsInterface);
    INSTALL_HOOK(originalDLL, EOS_Platform_GetEcomInterface, Hooks::Platform_GetEcomInterface, Original::Platform_GetEcomInterface);
    INSTALL_HOOK(originalDLL, EOS_Platform_GetStatsInterface, Hooks::Platform_GetStatsInterface, Original::Platform_GetStatsInterface);
    INSTALL_HOOK(originalDLL, EOS_Platform_GetUIInterface, Hooks::Platform_GetUIInterface, Original::Platform_GetUIInterface);

    Logger::info("[HOOK] Installing Achievement hooks...");
    // Fix #4: Version-gate hook installation. V2 achievement notification hooks
    // (AddNotifyAchievementsUnlockedV2, CopyAchievementDefinitionV2*) were
    // introduced in SDK 1.14.0. On older SDKs, these functions don't exist in
    // the DLL, so we use INSTALL_HOOK_OPTIONAL to avoid fatal errors.
    if (EOS_Compat::isFeatureAvailable("AchievementsUnlockedV2")) {
        Logger::info("[HOOK]   V2 achievement notifications: AVAILABLE (SDK 1.14+)");
    } else {
        Logger::warn("[HOOK]   V2 achievement notifications: NOT AVAILABLE (SDK < 1.14)");
        Logger::warn("[HOOK]   V2 hooks will be skipped (INSTALL_HOOK_OPTIONAL)");
    }
    INSTALL_HOOK(originalDLL, EOS_Achievements_QueryDefinitions, Hooks::Achievements_QueryDefinitions, Original::Achievements_QueryDefinitions);
    INSTALL_HOOK(originalDLL, EOS_Achievements_QueryPlayerAchievements, Hooks::Achievements_QueryPlayerAchievements, Original::Achievements_QueryPlayerAchievements);
    INSTALL_HOOK(originalDLL, EOS_Achievements_UnlockAchievements, Hooks::Achievements_UnlockAchievements, Original::Achievements_UnlockAchievements);
    // Fix #4: AddNotifyAchievementsUnlockedV2 was introduced in SDK 1.14.0.
    // It does not exist in SDK 1.13.0. Use OPTIONAL to avoid crash on older SDKs.
    INSTALL_HOOK_OPTIONAL(originalDLL, EOS_Achievements_AddNotifyAchievementsUnlockedV2, Hooks::Achievements_AddNotifyAchievementsUnlockedV2, Original::Achievements_AddNotifyAchievementsUnlockedV2);
    // Deprecated V1 notification hook - make optional for older SDKs that may not export it
    INSTALL_HOOK_OPTIONAL(originalDLL, EOS_Achievements_AddNotifyAchievementsUnlocked, Hooks::Achievements_AddNotifyAchievementsUnlocked, Original::Achievements_AddNotifyAchievementsUnlocked);

    Logger::info("[HOOK] Installing Ecom hooks...");
    INSTALL_HOOK(originalDLL, EOS_Ecom_QueryOwnership, Hooks::Ecom_QueryOwnership, Original::Ecom_QueryOwnership);
    INSTALL_HOOK_OPTIONAL(originalDLL, EOS_Ecom_QueryOwnershipBySandboxIds, Hooks::Ecom_QueryOwnershipBySandboxIds, Original::Ecom_QueryOwnershipBySandboxIds);
    INSTALL_HOOK_OPTIONAL(originalDLL, EOS_Ecom_QueryOwnershipToken, Hooks::Ecom_QueryOwnershipToken, Original::Ecom_QueryOwnershipToken);
    INSTALL_HOOK(originalDLL, EOS_Ecom_QueryEntitlements, Hooks::Ecom_QueryEntitlements, Original::Ecom_QueryEntitlements);
    INSTALL_HOOK(originalDLL, EOS_Ecom_GetEntitlementsCount, Hooks::Ecom_GetEntitlementsCount, Original::Ecom_GetEntitlementsCount);
    INSTALL_HOOK(originalDLL, EOS_Ecom_CopyEntitlementByIndex, Hooks::Ecom_CopyEntitlementByIndex, Original::Ecom_CopyEntitlementByIndex);
    INSTALL_HOOK(originalDLL, EOS_Ecom_Entitlement_Release, Hooks::Ecom_Entitlement_Release, Original::Ecom_Entitlement_Release);

    Logger::info("[HOOK] Installing Connect hooks...");
    INSTALL_HOOK(originalDLL, EOS_Connect_Login, Hooks::Connect_Login, Original::Connect_Login);
    INSTALL_HOOK(originalDLL, EOS_Connect_GetLoggedInUserByIndex, Hooks::Connect_GetLoggedInUserByIndex, Original::Connect_GetLoggedInUserByIndex);
    INSTALL_HOOK_OPTIONAL(originalDLL, EOS_Connect_AddNotifyLoginStatusChanged, Hooks::Connect_AddNotifyLoginStatusChanged, Original::Connect_AddNotifyLoginStatusChanged);

    Logger::info("[HOOK] Installing Auth hooks...");
    INSTALL_HOOK(originalDLL, EOS_Auth_Login, Hooks::Auth_Login, Original::Auth_Login);
    INSTALL_HOOK(originalDLL, EOS_Auth_GetLoggedInAccountByIndex, Hooks::Auth_GetLoggedInAccountByIndex, Original::Auth_GetLoggedInAccountByIndex);
    INSTALL_HOOK_OPTIONAL(originalDLL, EOS_Auth_AddNotifyLoginStatusChanged, Hooks::Auth_AddNotifyLoginStatusChanged, Original::Auth_AddNotifyLoginStatusChanged);

    Logger::info("[HOOK] Installing Metrics hooks (BlockMetrics=%s)...",
                 Config::BlockMetrics() ? "true" : "false");
    INSTALL_HOOK_OPTIONAL(originalDLL, EOS_Metrics_BeginPlayerSession, Hooks::Metrics_BeginPlayerSession, Original::Metrics_BeginPlayerSession);
    INSTALL_HOOK_OPTIONAL(originalDLL, EOS_Metrics_EndPlayerSession, Hooks::Metrics_EndPlayerSession, Original::Metrics_EndPlayerSession);

    // A1: Resolve logging function pointers (not hooked -- we call them
    // directly from the Platform_Create hook to register SdkLogCallback).
    // Using GetProcAddress avoids the LNK2001 from the LinkerExports
    // forwarder pragma (which only re-exports, does not make importable).
    // ResolveExport handles both 64-bit (bare name) and 32-bit __stdcall
    // (_Name@N decoration) transparently, so the #ifdef _WIN64 duplication
    // that used to live here is gone.
    {
        if (void* pSetCB = ResolveExport(originalDLL, "EOS_Logging_SetCallback")) {
            Original::Logging_SetCallback = reinterpret_cast<decltype(Original::Logging_SetCallback)>(pSetCB);
        } else {
            Logger::warn("[HOOK] Could not resolve EOS_Logging_SetCallback (SDK log capture disabled)");
        }

        if (void* pSetLvl = ResolveExport(originalDLL, "EOS_Logging_SetLogLevel")) {
            Original::Logging_SetLogLevel = reinterpret_cast<decltype(Original::Logging_SetLogLevel)>(pSetLvl);
        } else {
            Logger::warn("[HOOK] Could not resolve EOS_Logging_SetLogLevel (SDK log capture disabled)");
        }
    }

    hooksInitialized = true;

    Logger::info("[HOOK] ========================================");
    Logger::info("[HOOK] All hooks installed successfully!");
    Logger::info("[HOOK] ========================================");

    Logger::flush();
    return true;
}

void ShutdownHooks() {
    if (!hooksInitialized) return;
    Logger::info("[HOOK] Shutting down hooks...");
    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();
    hooksInitialized = false;
    Logger::info("[HOOK] Hooks shutdown complete");
}

bool AreHooksActive() {
    return hooksInitialized;
}

void QueueUnlock(const char* id) {
    std::lock_guard<std::mutex> lk(g_guiQueueMutex);
    g_guiUnlockQueue.push(id);
    Logger::info("[HOOK] QueueUnlock: queued %s", id);
}

} // namespace EOS_Hooks