#include "pch.h"
#include "eos_intercept.h"
#include "ScreamAPI.h"
#include "achievement_manager.h"
#include "util.h"
#include "Config.h"
#include "eos_compat.h"
#include "dlc_catalog.h"
#include "Logger.h"
#include <mutex>
#include <vector>
#include <queue>
#include <thread>
#include <atomic>
#include <map>

namespace Intercept {

// ── Fallback Platform Capture ─────────────────────────────────────────────
// When EOS_Platform_Create hook never fires (UE plugin loads SDK dynamically
// or calls through an indirect wrapper), we capture the platform handle from
// Platform_Tick or Platform_GetXxxInterface, which always receive it.
static std::atomic<bool> g_platformCapturedFromFallback{false};

static void CapturePlatformHandleFromFallback(EOS_HPlatform Handle, const char* source) {
    if (Handle == nullptr || Util::hPlatform != nullptr) return;
    if (g_platformCapturedFromFallback.exchange(true)) return; // already captured

    Logger::info("[INTERCEPT] %s: platform handle %p detected (EOS_Platform_Create hook was missed!)", source, Handle);
    Logger::info("[INTERCEPT] %s: capturing handle -> Util::hPlatform so isEOSPlatformReady() returns true", source);
    Util::hPlatform = Handle;
    // Note: SandboxId/ProductId remain empty since we don't have EOS_Platform_Options.
    // DLC catalog auto-fetch won't work, but achievements will initialize via the
    // polling thread in ScreamAPI::init().
}

// ── SDK Log Capture ────────────────────────────────────────────────────────
static std::wstring g_sdkLogPath;
static std::mutex    g_sdkLogMutex;
static HANDLE g_sdkLogFile = INVALID_HANDLE_VALUE;
static thread_local bool g_inSdkLogCallback = false;

void SetSDKLogPath(const std::wstring& path) {
    std::lock_guard<std::mutex> lk(g_sdkLogMutex);
    if (g_sdkLogFile != INVALID_HANDLE_VALUE) {
        CloseHandle(g_sdkLogFile);
        g_sdkLogFile = INVALID_HANDLE_VALUE;
    }
    g_sdkLogPath = path;
}

static void EOS_CALL SdkLogCallback(const EOS_LogMessage* Message) {
    if (g_sdkLogPath.empty() || Message == nullptr) return;
    if (g_inSdkLogCallback) return;  // re-entrancy guard
    g_inSdkLogCallback = true;
    std::lock_guard<std::mutex> lk(g_sdkLogMutex);
    // Lazy-open the file handle once, keep it open
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

void RegisterSDKLogCallback(Logging_SetCallback_t setCallback, Logging_SetLogLevel_t setLogLevel) {
    if (g_sdkLogPath.empty() || !Config::EnableSDKLog()) {
        if (!g_sdkLogPath.empty() && !Config::EnableSDKLog())
            Logger::info("[INTERCEPT] SDK log capture disabled (EnableSDKLog=false in config)");
        return;
    }
    std::thread([setCallback, setLogLevel]() {
        Sleep(500);  // let Platform_Create return + game settle
        if (!setCallback || !setLogLevel) {
            Logger::warn("[INTERCEPT] SDK log functions not resolved -- SDK log disabled");
            return;
        }
        std::string lvlStr = Config::SDKLogLevel();
        EOS_ELogLevel lvl = EOS_ELogLevel::EOS_LOG_Warning;
        if      (lvlStr == "Off")         return;  // disabled entirely
        else if (lvlStr == "Fatal")       lvl = EOS_ELogLevel::EOS_LOG_Fatal;
        else if (lvlStr == "Error")       lvl = EOS_ELogLevel::EOS_LOG_Error;
        else if (lvlStr == "Warning")     lvl = EOS_ELogLevel::EOS_LOG_Warning;
        else if (lvlStr == "Info")        lvl = EOS_ELogLevel::EOS_LOG_Info;
        else if (lvlStr == "Verbose")     lvl = EOS_ELogLevel::EOS_LOG_Verbose;
        else if (lvlStr == "VeryVerbose") lvl = EOS_ELogLevel::EOS_LOG_VeryVerbose;
        else Logger::warn("[INTERCEPT] Unknown SDKLogLevel \"%s\", falling back to Warning", lvlStr.c_str());

        EOS_EResult cbRes = setCallback(SdkLogCallback);
        if (cbRes == EOS_EResult::EOS_Success) {
            setLogLevel(EOS_ELogCategory::EOS_LC_ALL_CATEGORIES, lvl);
            Logger::info("[INTERCEPT] SDK log callback registered (level=%s) -> %ls",
                         lvlStr.c_str(), g_sdkLogPath.c_str());
        } else {
            Logger::warn("[INTERCEPT] EOS_Logging_SetCallback failed: %s",
                         EOS_EResult_ToString(cbRes));
        }
    }).detach();
}

// ── GUI Unlock Queue ───────────────────────────────────────────────────────
static std::queue<std::string> g_guiUnlockQueue;
static std::mutex g_guiQueueMutex;

void QueueUnlock(const char* id) {
    std::lock_guard<std::mutex> lk(g_guiQueueMutex);
    g_guiUnlockQueue.push(id);
    Logger::info("[INTERCEPT] QueueUnlock: queued %s", id);
}

// ── Pending Unlock Storage ─────────────────────────────────────────────────
struct PendingUnlock {
    EOS_HAchievements Handle;
    EOS_Achievements_UnlockAchievementsOptions Options;
    void* ClientData;
    EOS_Achievements_OnUnlockAchievementsCompleteCallback CompletionDelegate;
    std::vector<std::string>  id_storage;
    std::vector<const char*>  id_array;
};
static std::vector<PendingUnlock> g_pendingUnlocks;
static std::mutex g_pendingMutex;
static std::atomic<bool> g_bAchievementsConfigured{false};
static std::mutex g_configMutex;
static std::atomic<int> g_forcedQueriesPending{0};

// ── Stored original achievement function pointers ──────────────────────────
// Set via SetAchievementsOriginals() from whichever mode resolves them.
static Achievements_QueryDefinitions_t g_orig_Achievements_QueryDefinitions = nullptr;
static Achievements_QueryPlayerAchievements_t g_orig_Achievements_QueryPlayerAchievements = nullptr;
static Achievements_UnlockAchievements_t g_orig_Achievements_UnlockAchievements = nullptr;

void SetAchievementsOriginals(
    Achievements_QueryDefinitions_t queryDefs,
    Achievements_QueryPlayerAchievements_t queryPlayer,
    Achievements_UnlockAchievements_t unlockAchievements)
{
    g_orig_Achievements_QueryDefinitions = queryDefs;
    g_orig_Achievements_QueryPlayerAchievements = queryPlayer;
    g_orig_Achievements_UnlockAchievements = unlockAchievements;
}

static void RetryPendingUnlocks() {
    std::lock_guard<std::mutex> lock(g_pendingMutex);
    for (auto& p : g_pendingUnlocks) {
        Logger::info("[INTERCEPT] Retrying pending unlock for achievement: %s", p.Options.AchievementIds[0]);
        g_orig_Achievements_UnlockAchievements(p.Handle, &p.Options, p.ClientData, p.CompletionDelegate);
    }
    g_pendingUnlocks.clear();
}

static void ForceAchievementsConfiguration(EOS_HAchievements handle, EOS_ProductUserId userId) {
    if (g_bAchievementsConfigured) return;
    std::lock_guard<std::mutex> lock(g_configMutex);
    if (g_bAchievementsConfigured) return;
    if (!g_orig_Achievements_QueryDefinitions || !g_orig_Achievements_QueryPlayerAchievements) return;

    Logger::info("[INTERCEPT] Forcing achievements configuration (QueryDefinitions + QueryPlayerAchievements)");

    g_forcedQueriesPending = 2;

    EOS_Achievements_QueryDefinitionsOptions defOpts = {1, userId, nullptr, nullptr, 0};
    g_orig_Achievements_QueryDefinitions(handle, &defOpts, nullptr,
        [](const EOS_Achievements_OnQueryDefinitionsCompleteCallbackInfo* Data) {
            Logger::debug("[INTERCEPT] Forced QueryDefinitions result: %s", EOS_EResult_ToString(Data->ResultCode));
            if (--g_forcedQueriesPending == 0) {
                g_bAchievementsConfigured = true;
                RetryPendingUnlocks();
            }
        });

    EOS_Achievements_QueryPlayerAchievementsOptions playerOpts = {1, userId};
    g_orig_Achievements_QueryPlayerAchievements(handle, &playerOpts, nullptr,
        [](const EOS_Achievements_OnQueryPlayerAchievementsCompleteCallbackInfo* Data) {
            Logger::debug("[INTERCEPT] Forced QueryPlayerAchievements result: %s", EOS_EResult_ToString(Data->ResultCode));
            if (--g_forcedQueriesPending == 0) {
                g_bAchievementsConfigured = true;
                RetryPendingUnlocks();
            }
        });
}

// ── Entitlement Data ───────────────────────────────────────────────────────
static std::map<std::string, std::string> g_entitlement_map;
static std::vector<std::string> g_entitlement_ids;
static std::mutex s_cache_mutex;
static std::map<std::string, std::string> s_catalog_cache;
static bool s_catalog_fetched = false;

static void AutoFetchEntitlements() {
    EnsureCatalogFetched();
    auto catalog = GetCatalogSnapshot();
    for (auto& [id, title] : catalog) {
        if (Config::IsDlcUnlocked(id, false)) {
            Logger::debug("[INTERCEPT]   Auto-fetch adding: %s - \"%s\"", id.c_str(), title.c_str());
            g_entitlement_map[id] = title;
        }
    }
}

static void InjectExtraEntitlements() {
    for (auto& [id, title] : Config::ExtraEntitlements()) {
        if (Config::IsDlcUnlocked(id, true)) {
            Logger::debug("[INTERCEPT]   Config adding: %s - \"%s\"", id.c_str(), title.c_str());
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

// ── Ownership Data ─────────────────────────────────────────────────────────
static std::vector<std::string>            g_ownership_id_storage;
static std::vector<EOS_Ecom_ItemOwnership> g_ownerships;

// ── Catalog Helpers ────────────────────────────────────────────────────────
void EnsureCatalogFetched() {
    if (s_catalog_fetched) return;

    std::string ns = Util::g_namespace_id;
    if (ns.empty()) {
        ns = Config::NamespaceId();
        if (!ns.empty())
            Logger::debug("[INTERCEPT] EnsureCatalogFetched: using NamespaceId from config: %s", ns.c_str());
    }
    if (ns.empty()) {
        Logger::warn("[INTERCEPT] EnsureCatalogFetched: namespace_id unavailable.");
        return;
    }

    s_catalog_fetched = true;
    auto result = DlcCatalog::fetch(ns);
    if (result.has_value()) {
        std::lock_guard<std::mutex> lk(s_cache_mutex);
        s_catalog_cache = std::move(*result);
        Logger::dlc("[INTERCEPT] EnsureCatalogFetched: cached %zu entries", s_catalog_cache.size());
    } else {
        Logger::warn("[INTERCEPT] EnsureCatalogFetched: failed to retrieve catalog from Epic's API");
    }
}

std::map<std::string, std::string> GetCatalogSnapshot() {
    std::lock_guard<std::mutex> lk(s_cache_mutex);
    return s_catalog_cache;
}

// ══════════════════════════════════════════════════════════════════════════
// Intercept Implementations
// ══════════════════════════════════════════════════════════════════════════

// ── Platform ───────────────────────────────────────────────────────────────

EOS_HPlatform Platform_Create(Platform_Create_t original, const EOS_Platform_Options* Options) {
    Logger::info("[INTERCEPT] EOS_Platform_Create called");
    if (Options) {
        Logger::debug("[INTERCEPT]   ApiVersion: %d", Options->ApiVersion);
        Logger::debug("[INTERCEPT]   ProductId: %s", Options->ProductId ? Options->ProductId : "NULL");
        Logger::debug("[INTERCEPT]   Flags: %llu", Options->Flags);
        if (Options->SandboxId && Options->SandboxId[0] != '\0') {
            Util::g_namespace_id = Options->SandboxId;
            Logger::info("[INTERCEPT]   Captured namespace_id: %s", Options->SandboxId);
            Logger::info("[INTERCEPT]   DLC database: https://scream-db.web.app/games/%s", Options->SandboxId);
        }
        if (Options->ProductId && Options->ProductId[0] != '\0') {
            Util::g_product_id = Options->ProductId;
            Logger::info("[INTERCEPT]   Captured product_id: %s", Options->ProductId);
        }
        if (Config::ForceEpicOverlay()) {
            auto mOptions = const_cast<EOS_Platform_Options*>(Options);
            mOptions->Flags = 0;
            Logger::debug("[INTERCEPT]   Disabled Epic Overlay (Flags set to 0)");
        }
    }

    EOS_HPlatform result = original(Options);
    Util::hPlatform = result;
    Logger::info("[INTERCEPT] Platform created: %p", result);

    if (result == nullptr) {
        Logger::error("[INTERCEPT] EOS_Platform_Create returned NULL - initialization will fail!");
        return result;
    }

    // Set ApplicationStatus / NetworkStatus (v1.15+ mandatory)
    if (ScreamAPI::originalDLL != nullptr) {
        typedef void (*SetAppStatus_t)(EOS_HPlatform, EOS_EApplicationStatus);
        auto SetAppStatusFunc = (SetAppStatus_t)GetProcAddress((HMODULE)ScreamAPI::originalDLL, "EOS_Platform_SetApplicationStatus");
        if (SetAppStatusFunc) {
            try { SetAppStatusFunc(result, EOS_EApplicationStatus::EOS_AS_BackgroundConstrained); }
            catch(...) { Logger::warn("[INTERCEPT] Exception calling EOS_Platform_SetApplicationStatus"); }
        }
        typedef void (*SetNetStatus_t)(EOS_HPlatform, EOS_ENetworkStatus);
        auto SetNetStatusFunc = (SetNetStatus_t)GetProcAddress((HMODULE)ScreamAPI::originalDLL, "EOS_Platform_SetNetworkStatus");
        if (SetNetStatusFunc) {
            try { SetNetStatusFunc(result, EOS_ENetworkStatus::EOS_NS_Online); }
            catch(...) { Logger::warn("[INTERCEPT] Exception calling EOS_Platform_SetNetworkStatus"); }
        }
    }

    // Register SDK log callback (deferred, background thread)
    if (ScreamAPI::originalDLL) {
        auto setCb = (Logging_SetCallback_t)GetProcAddress((HMODULE)ScreamAPI::originalDLL, "EOS_Logging_SetCallback");
        auto setLvl = (Logging_SetLogLevel_t)GetProcAddress((HMODULE)ScreamAPI::originalDLL, "EOS_Logging_SetLogLevel");
        RegisterSDKLogCallback(setCb, setLvl);
    }

    // Trigger achievement manager init
    std::thread([]() {
        Sleep(500);
        Logger::info("[INTERCEPT] Triggering achievement manager initialization");
        AchievementManager::init();
    }).detach();

    return result;
}

void Platform_Release(Platform_Release_t original, EOS_HPlatform Handle) {
    Logger::info("[INTERCEPT] EOS_Platform_Release called");
    original(Handle);
    Util::hPlatform = nullptr;
    g_platformCapturedFromFallback = false; // allow re-capture if a new platform is created
}

void Platform_Tick(Platform_Tick_t original, EOS_HPlatform Handle) {
    Logger::info("[INTERCEPT] >>> EOS_Platform_Tick called (Handle=%p) <<<", Handle);
    // Fallback: if EOS_Platform_Create hook never fired but the game is calling
    // Tick with a valid handle, capture it so isEOSPlatformReady() returns true.
    CapturePlatformHandleFromFallback(Handle, "Platform_Tick");

    // Drain GUI unlock queue on the game thread (safe to call EOS here)
    {
        std::lock_guard<std::mutex> lk(g_guiQueueMutex);
        while (!g_guiUnlockQueue.empty()) {
            const std::string& id = g_guiUnlockQueue.front();
            Logger::info("[INTERCEPT] Platform_Tick: draining GUI unlock: %s", id.c_str());
            AchievementManager::findAchievement(id.c_str(), [](Overlay_Achievement& a) {
                if (a.UnlockState == UnlockState::Locked)
                    AchievementManager::unlockAchievement(&a);
            });
            g_guiUnlockQueue.pop();
        }
    }
    original(Handle);
}

EOS_HConnect Platform_GetConnectInterface(Platform_GetConnectInterface_t original, EOS_HPlatform Handle) {
    Logger::info("[INTERCEPT] >>> EOS_Platform_GetConnectInterface called (Handle=%p) <<<", Handle);
    CapturePlatformHandleFromFallback(Handle, "GetConnectInterface");
    auto result = original(Handle);
    Logger::info("[INTERCEPT] EOS_Platform_GetConnectInterface -> %p", result);
    return result;
}

EOS_HAuth Platform_GetAuthInterface(Platform_GetAuthInterface_t original, EOS_HPlatform Handle) {
    Logger::info("[INTERCEPT] >>> EOS_Platform_GetAuthInterface called (Handle=%p) <<<", Handle);
    CapturePlatformHandleFromFallback(Handle, "GetAuthInterface");
    auto result = original(Handle);
    Logger::info("[INTERCEPT] EOS_Platform_GetAuthInterface -> %p", result);
    return result;
}

EOS_HAchievements Platform_GetAchievementsInterface(Platform_GetAchievementsInterface_t original, EOS_HPlatform Handle) {
    Logger::info("[INTERCEPT] >>> EOS_Platform_GetAchievementsInterface called (Handle=%p) <<<", Handle);
    CapturePlatformHandleFromFallback(Handle, "GetAchievementsInterface");
    auto result = original(Handle);
    Logger::info("[INTERCEPT] EOS_Platform_GetAchievementsInterface -> %p", result);
    return result;
}

EOS_HEcom Platform_GetEcomInterface(Platform_GetEcomInterface_t original, EOS_HPlatform Handle) {
    Logger::info("[INTERCEPT] >>> EOS_Platform_GetEcomInterface called (Handle=%p) <<<", Handle);
    CapturePlatformHandleFromFallback(Handle, "GetEcomInterface");
    auto result = original(Handle);
    Logger::info("[INTERCEPT] EOS_Platform_GetEcomInterface -> %p", result);
    return result;
}

EOS_HStats Platform_GetStatsInterface(Platform_GetStatsInterface_t original, EOS_HPlatform Handle) {
    Logger::info("[INTERCEPT] >>> EOS_Platform_GetStatsInterface called (Handle=%p) <<<", Handle);
    CapturePlatformHandleFromFallback(Handle, "GetStatsInterface");
    auto result = original(Handle);
    if (result) {
        Logger::info("[STATS] EOS_Platform_GetStatsInterface -> %p (Stats interface available)", result);
    } else {
        Logger::warn("[STATS] EOS_Platform_GetStatsInterface -> NULL (Stats interface NOT available - stat-gated achievements may not unlock)");
    }
    return result;
}

EOS_HUI Platform_GetUIInterface(Platform_GetUIInterface_t original, EOS_HPlatform Handle) {
    Logger::info("[INTERCEPT] >>> EOS_Platform_GetUIInterface called (Handle=%p) <<<", Handle);
    CapturePlatformHandleFromFallback(Handle, "GetUIInterface");
    auto result = original(Handle);
    Logger::info("[INTERCEPT] EOS_Platform_GetUIInterface -> %p", result);
    return result;
}

// ── Achievements ───────────────────────────────────────────────────────────

void Achievements_QueryDefinitions(Achievements_QueryDefinitions_t original, EOS_HAchievements Handle, const EOS_Achievements_QueryDefinitionsOptions* Options, void* ClientData, const EOS_Achievements_OnQueryDefinitionsCompleteCallback CompletionDelegate) {
    Logger::debug("[INTERCEPT] EOS_Achievements_QueryDefinitions called");
    original(Handle, Options, ClientData, CompletionDelegate);
}

void Achievements_QueryPlayerAchievements(Achievements_QueryPlayerAchievements_t original, EOS_HAchievements Handle, const EOS_Achievements_QueryPlayerAchievementsOptions* Options, void* ClientData, const EOS_Achievements_OnQueryPlayerAchievementsCompleteCallback CompletionDelegate) {
    Logger::debug("[INTERCEPT] EOS_Achievements_QueryPlayerAchievements called");
    original(Handle, Options, ClientData, CompletionDelegate);
}

void Achievements_UnlockAchievements(Achievements_UnlockAchievements_t original, EOS_HAchievements Handle, const EOS_Achievements_UnlockAchievementsOptions* Options, void* ClientData, const EOS_Achievements_OnUnlockAchievementsCompleteCallback CompletionDelegate) {
    Logger::info("[INTERCEPT] EOS_Achievements_UnlockAchievements called");
    if (Options) {
        Logger::info("[INTERCEPT]   ApiVersion: %d", Options->ApiVersion);
        Logger::info("[INTERCEPT]   UserId: %p", Options->UserId);
        Logger::info("[INTERCEPT]   AchievementsCount: %u", Options->AchievementsCount);
        for (uint32_t i = 0; i < Options->AchievementsCount; i++) {
            Logger::info("[INTERCEPT]     Achievement ID: %s", Options->AchievementIds[i]);
        }
    } else {
        Logger::warn("[INTERCEPT]   Options is NULL!");
    }

    auto currentUserId = Util::getProductUserId();
    auto hAchievements = Util::getHAchievements();
    Logger::info("[INTERCEPT]   Current Util::getProductUserId() = %p", currentUserId);
    Logger::info("[INTERCEPT]   Current Util::getHAchievements() = %p", hAchievements);
    Logger::info("[INTERCEPT]   Handle passed to intercept = %p", Handle);

    if (Options && Options->UserId != currentUserId) {
        Logger::warn("[INTERCEPT]   UserId mismatch! Options->UserId (%p) != current Util::getProductUserId() (%p)", Options->UserId, currentUserId);
    }

    // If achievements not yet configured AND the config option is enabled, force configuration and postpone unlock
    if (!g_bAchievementsConfigured && Options && Options->UserId && Config::ForceAchievementsConfig()) {
        Logger::warn("[INTERCEPT] Achievements not configured yet - forcing configuration and postponing unlock");
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
    original(Handle, Options, ClientData, CompletionDelegate);
}

uint32_t Achievements_GetPlayerAchievementCount(Achievements_GetPlayerAchievementCount_t original, EOS_HAchievements Handle, const EOS_Achievements_GetPlayerAchievementCountOptions* Options) {
    auto result = original(Handle, Options);
    Logger::ach("[INTERCEPT] Player Achievement Count: %d", result);
    return result;
}

uint32_t Achievements_GetAchievementDefinitionCount(Achievements_GetAchievementDefinitionCount_t original, EOS_HAchievements Handle, const EOS_Achievements_GetAchievementDefinitionCountOptions* Options) {
    auto result = original(Handle, Options);
    Logger::ach("[INTERCEPT] Achievement Definition Count: %d", result);
    return result;
}

EOS_NotificationId Achievements_AddNotifyAchievementsUnlockedV2(Achievements_AddNotifyAchievementsUnlockedV2_t original, EOS_HAchievements Handle, const EOS_Achievements_AddNotifyAchievementsUnlockedV2Options* Options, void* ClientData, const EOS_Achievements_OnAchievementsUnlockedCallbackV2 NotificationFn) {
    Logger::debug("[INTERCEPT] EOS_Achievements_AddNotifyAchievementsUnlockedV2 called");
    // If V2 function not available in older SDK, return invalid notification ID
    if (!original) {
        Logger::warn("[INTERCEPT] AddNotifyAchievementsUnlockedV2 not available in this SDK version - returning EOS_INVALID_NOTIFICATIONID");
        return EOS_INVALID_NOTIFICATIONID;
    }
    return original(Handle, Options, ClientData, NotificationFn);
}

EOS_NotificationId Achievements_AddNotifyAchievementsUnlocked(Achievements_AddNotifyAchievementsUnlocked_t original, EOS_HAchievements Handle, const EOS_Achievements_AddNotifyAchievementsUnlockedOptions* Options, void* ClientData, const EOS_Achievements_OnAchievementsUnlockedCallback NotificationFn) {
    Logger::debug("[INTERCEPT] EOS_Achievements_AddNotifyAchievementsUnlocked (deprecated) called");
    // Guard against missing function in older SDKs
    if (!original) {
        Logger::warn("[INTERCEPT] AddNotifyAchievementsUnlocked not available - returning EOS_INVALID_NOTIFICATIONID");
        return EOS_INVALID_NOTIFICATIONID;
    }
    return original(Handle, Options, ClientData, NotificationFn);
}

// ── Ecom Ownership ─────────────────────────────────────────────────────────

void Ecom_QueryOwnership(Ecom_QueryOwnership_t original, EOS_HEcom Handle, const EOS_Ecom_QueryOwnershipOptions* Options, void* ClientData, const EOS_Ecom_OnQueryOwnershipCallback CompletionDelegate) {
    Logger::info("[INTERCEPT] EOS_Ecom_QueryOwnership called");

    EnsureCatalogFetched();
    auto catalog = GetCatalogSnapshot();

    g_ownership_id_storage.clear();
    g_ownerships.clear();
    if (Options) {
        Logger::dlc("[INTERCEPT] Game queried ownership of %d item(s):", Options->CatalogItemIdCount);
        g_ownership_id_storage.reserve(Options->CatalogItemIdCount);
        g_ownerships.reserve(Options->CatalogItemIdCount);
        for (uint32_t i = 0; i < Options->CatalogItemIdCount; i++) {
            const char* id = Options->CatalogItemIds[i];
            auto it = catalog.find(id);
            const char* title = (it != catalog.end()) ? it->second.c_str() : "Unknown Title";
            Logger::dlc("[INTERCEPT]   Item ID: %s (\"%s\")", id, title);
            bool unlocked = Config::IsDlcUnlocked(std::string(id), true);
            g_ownership_id_storage.emplace_back(id);
            g_ownerships.emplace_back(EOS_Ecom_ItemOwnership{
                EOS_ECOM_ITEMOWNERSHIP_API_LATEST,
                g_ownership_id_storage.back().c_str(),
                unlocked ? EOS_EOwnershipStatus::EOS_OS_Owned : EOS_EOwnershipStatus::EOS_OS_NotOwned
            });
        }
    } else {
        Logger::warn("[INTERCEPT] Game queried DLC ownership without Options parameter");
    }

    if (Config::EnableOwnershipUnlocker()) {
        auto container = new ScreamAPI::OriginalDataContainer(ClientData, CompletionDelegate);
        original(Handle, Options, container,
            [](const EOS_Ecom_QueryOwnershipCallbackInfo* Data){
                ScreamAPI::proxyCallback<EOS_Ecom_QueryOwnershipCallbackInfo>(Data, &Data->ClientData,
                    [](EOS_Ecom_QueryOwnershipCallbackInfo* mData){
                        if(mData->ResultCode != EOS_EResult::EOS_Success){
                            Logger::warn("[INTERCEPT] EOS_Ecom_QueryOwnership failed: %s",
                                EOS_EResult_ToString(mData->ResultCode));
                            if(Config::ForceSuccess()){
                                Logger::warn("[INTERCEPT] Forcing EOS_Success");
                                mData->ItemOwnershipCount = (uint32_t)g_ownerships.size();
                                mData->ItemOwnership      = g_ownerships.data();
                                mData->ResultCode         = EOS_EResult::EOS_Success;
                            }
                        }

                        Logger::dlc("[INTERCEPT] Responding with %d ownership(s):", mData->ItemOwnershipCount);
                        for(uint32_t i = 0; i < mData->ItemOwnershipCount; i++){
                            auto* item = const_cast<EOS_Ecom_ItemOwnership*>(mData->ItemOwnership + i);
                            bool original = (item->OwnershipStatus == EOS_EOwnershipStatus::EOS_OS_Owned);
                            bool unlocked = Config::IsDlcUnlocked(std::string(item->Id), original);
                            item->OwnershipStatus = unlocked
                                ? EOS_EOwnershipStatus::EOS_OS_Owned
                                : EOS_EOwnershipStatus::EOS_OS_NotOwned;
                            auto snap = GetCatalogSnapshot();
                            auto cit = snap.find(item->Id);
                            const char* title = (cit != snap.end()) ? cit->second.c_str() : "Unknown Title";
                            Logger::dlc("[INTERCEPT]   [%s] %s (\"%s\")", unlocked ? "Owned" : "Not Owned", item->Id, title);
                        }
                    }
                );
            }
        );
    } else {
        original(Handle, Options, ClientData, CompletionDelegate);
    }
}

void Ecom_QueryOwnershipBySandboxIds(Ecom_QueryOwnershipBySandboxIds_t original, EOS_HEcom Handle, const EOS_Ecom_QueryOwnershipBySandboxIdsOptions* Options, void* ClientData, const EOS_Ecom_OnQueryOwnershipBySandboxIdsCallback CompletionDelegate) {
    Logger::info("[INTERCEPT] EOS_Ecom_QueryOwnershipBySandboxIds called");

    if (!Config::EnableOwnershipUnlocker()) {
        original(Handle, Options, ClientData, CompletionDelegate);
        return;
    }

    EnsureCatalogFetched();
    auto catalog = GetCatalogSnapshot();

    if (Options && Options->SandboxIdsCount > 0) {
        Logger::dlc("[INTERCEPT] Sandbox ownership query: %u sandbox(s)", Options->SandboxIdsCount);
        for (uint32_t i = 0; i < Options->SandboxIdsCount; i++) {
            Logger::dlc("[INTERCEPT]   SandboxId: %S", Options->SandboxIds[i]);
        }
    }

    auto container = new ScreamAPI::OriginalDataContainer(ClientData, CompletionDelegate);
    original(Handle, Options, container,
        [](const EOS_Ecom_QueryOwnershipBySandboxIdsCallbackInfo* Data){
            ScreamAPI::proxyCallback<EOS_Ecom_QueryOwnershipBySandboxIdsCallbackInfo>(
                Data, &Data->ClientData,
                [](EOS_Ecom_QueryOwnershipBySandboxIdsCallbackInfo* mData){
                    if (mData->ResultCode != EOS_EResult::EOS_Success) {
                        Logger::warn("[INTERCEPT] EOS_Ecom_QueryOwnershipBySandboxIds failed: %s",
                            EOS_EResult_ToString(mData->ResultCode));
                        if (Config::ForceSuccess()) {
                            Logger::warn("[INTERCEPT] Forcing EOS_Success");
                            mData->ResultCode = EOS_EResult::EOS_Success;
                        }
                    }

                    // Log per-sandbox ownership for diagnostics + apply unlock flag
                    if (mData->ResultCode == EOS_EResult::EOS_Success && mData->SandboxIdItemOwnershipsCount > 0) {
                        Logger::dlc("[INTERCEPT] Responding with %u sandbox ownership group(s):",
                                    mData->SandboxIdItemOwnershipsCount);
                        for (uint32_t s = 0; s < mData->SandboxIdItemOwnershipsCount; s++) {
                            auto* sb = const_cast<EOS_Ecom_SandboxIdItemOwnership*>(mData->SandboxIdItemOwnerships + s);
                            Logger::dlc("[INTERCEPT]   Sandbox %S: %u owned item(s)",
                                        sb->SandboxId, sb->OwnedCatalogItemIdsCount);
                            for (uint32_t i = 0; i < sb->OwnedCatalogItemIdsCount; i++) {
                                const char* itemId = sb->OwnedCatalogItemIds[i];
                                bool unlocked = Config::IsDlcUnlocked(std::string(itemId), true);
                                Logger::dlc("[INTERCEPT]     [%s] %s",
                                            unlocked ? "Owned" : "Not Owned", itemId);
                            }
                        }
                    }
                }
            );
        }
    );
}

void Ecom_QueryOwnershipToken(Ecom_QueryOwnershipToken_t original, EOS_HEcom Handle, const EOS_Ecom_QueryOwnershipTokenOptions* Options, void* ClientData, const EOS_Ecom_OnQueryOwnershipTokenCallback CompletionDelegate) {
    Logger::debug("[INTERCEPT] EOS_Ecom_QueryOwnershipToken called");

    if (Config::EnableOwnershipUnlocker()) {
        auto container = new ScreamAPI::OriginalDataContainer(ClientData, CompletionDelegate);
        original(Handle, Options, container,
            [](const EOS_Ecom_QueryOwnershipTokenCallbackInfo* Data){
                ScreamAPI::proxyCallback<EOS_Ecom_QueryOwnershipTokenCallbackInfo>(
                    Data, &Data->ClientData,
                    [](EOS_Ecom_QueryOwnershipTokenCallbackInfo* mData){
                        Logger::dlc("[INTERCEPT] QueryOwnershipToken result: %s",
                            EOS_EResult_ToString(mData->ResultCode));
                        mData->ResultCode = EOS_EResult::EOS_Success;
                    }
                );
            }
        );
    } else {
        original(Handle, Options, ClientData, CompletionDelegate);
    }
}

// ── Ecom Entitlements ──────────────────────────────────────────────────────

void Ecom_QueryEntitlements(Ecom_QueryEntitlements_t original, EOS_HEcom Handle, const EOS_Ecom_QueryEntitlementsOptions* Options, void* ClientData, const EOS_Ecom_OnQueryEntitlementsCallback CompletionDelegate) {
    Logger::info("[INTERCEPT] EOS_Ecom_QueryEntitlements called");

    if (!Config::EnableEntitlementUnlocker()) {
        original(Handle, Options, ClientData, CompletionDelegate);
        return;
    }

    g_entitlement_map.clear();
    g_entitlement_ids.clear();

    Logger::dlc("[INTERCEPT] Game queried %d entitlement(s):", Options->EntitlementNameCount);
    for (uint32_t i = 0; i < Options->EntitlementNameCount; i++) {
        const char* id = Options->EntitlementNames[i];
        Logger::dlc("[INTERCEPT]   %s", id);
        if (Config::IsDlcUnlocked(std::string(id), true))
            g_entitlement_map[id] = "Unknown Title";
    }

    auto container = new ScreamAPI::OriginalDataContainer(ClientData, CompletionDelegate);
    original(Handle, Options, container,
        [](const EOS_Ecom_QueryEntitlementsCallbackInfo* Data){
            ScreamAPI::proxyCallback<EOS_Ecom_QueryEntitlementsCallbackInfo>(
                Data, &Data->ClientData,
                [](EOS_Ecom_QueryEntitlementsCallbackInfo* mData){
                    try {
                        AutoFetchEntitlements();
                        InjectExtraEntitlements();

                        g_entitlement_ids.clear();
                        for (auto& [id, title] : g_entitlement_map)
                            g_entitlement_ids.push_back(id);

                        Logger::dlc("[INTERCEPT] Responding with %zu entitlement(s):", g_entitlement_map.size());
                        for (auto& [id, title] : g_entitlement_map)
                            Logger::dlc("[INTERCEPT]   %s = \"%s\"", id.c_str(), title.c_str());

                        mData->ResultCode = EOS_EResult::EOS_Success;
                    } catch (const std::exception& e) {
                        Logger::error("[INTERCEPT] QueryEntitlements callback error: %s", e.what());
                    }
                }
            );
        }
    );
}

uint32_t Ecom_GetEntitlementsCount(Ecom_GetEntitlementsCount_t original, EOS_HEcom Handle, const EOS_Ecom_GetEntitlementsCountOptions* Options) {
    Logger::debug("[INTERCEPT] EOS_Ecom_GetEntitlementsCount called");
    if (!Config::EnableEntitlementUnlocker()) {
        return original(Handle, Options);
    }
    const auto count = (uint32_t)g_entitlement_map.size();
    Logger::debug("[INTERCEPT] GetEntitlementsCount: %u", count);
    return count;
}

uint32_t Ecom_GetEntitlementsByNameCount(Ecom_GetEntitlementsByNameCount_t original, EOS_HEcom Handle, const EOS_Ecom_GetEntitlementsByNameCountOptions* Options) {
    Logger::debug("[INTERCEPT] EOS_Ecom_GetEntitlementsByNameCount called");
    if (!Config::EnableEntitlementUnlocker()) {
        return original(Handle, Options);
    }
    const char* name = Options->EntitlementName;
    uint32_t count = g_entitlement_map.count(std::string(name)) ? 1u : 0u;
    Logger::dlc("[INTERCEPT] GetEntitlementsByNameCount '%s': %u", name, count);
    return count;
}

EOS_EResult Ecom_CopyEntitlementByIndex(Ecom_CopyEntitlementByIndex_t original, EOS_HEcom Handle, const EOS_Ecom_CopyEntitlementByIndexOptions* Options, EOS_Ecom_Entitlement** OutEntitlement) {
    Logger::debug("[INTERCEPT] EOS_Ecom_CopyEntitlementByIndex called");
    if (!Config::EnableEntitlementUnlocker()) {
        return original(Handle, Options, OutEntitlement);
    }
    const auto index = Options->EntitlementIndex;
    if (index >= g_entitlement_ids.size()) {
        Logger::warn("[INTERCEPT] CopyEntitlementByIndex: index %u out of bounds (%zu)", index, g_entitlement_ids.size());
        return EOS_EResult::EOS_NotFound;
    }
    const auto& id    = g_entitlement_ids[index];
    const auto& title = g_entitlement_map.at(id);
    Logger::dlc("[INTERCEPT] CopyEntitlementByIndex[%u]: %s", index, id.c_str());
    *OutEntitlement = MakeEntitlement(id, title);
    return EOS_EResult::EOS_Success;
}

EOS_EResult Ecom_CopyEntitlementByNameAndIndex(Ecom_CopyEntitlementByNameAndIndex_t original, EOS_HEcom Handle, const EOS_Ecom_CopyEntitlementByNameAndIndexOptions* Options, EOS_Ecom_Entitlement** OutEntitlement) {
    Logger::debug("[INTERCEPT] EOS_Ecom_CopyEntitlementByNameAndIndex called");
    if (!Config::EnableEntitlementUnlocker()) {
        return original(Handle, Options, OutEntitlement);
    }
    const std::string name = Options->EntitlementName;
    auto it = g_entitlement_map.find(name);
    if (it == g_entitlement_map.end() || Options->Index > 0) {
        Logger::warn("[INTERCEPT] CopyEntitlementByNameAndIndex: '%s'[%u] not found", name.c_str(), Options->Index);
        return EOS_EResult::EOS_NotFound;
    }
    Logger::dlc("[INTERCEPT] CopyEntitlementByNameAndIndex: %s", name.c_str());
    *OutEntitlement = MakeEntitlement(it->first, it->second);
    return EOS_EResult::EOS_Success;
}

EOS_EResult Ecom_CopyEntitlementById(Ecom_CopyEntitlementById_t original, EOS_HEcom Handle, const EOS_Ecom_CopyEntitlementByIdOptions* Options, EOS_Ecom_Entitlement** OutEntitlement) {
    Logger::debug("[INTERCEPT] EOS_Ecom_CopyEntitlementById called");
    if (!Config::EnableEntitlementUnlocker()) {
        return original(Handle, Options, OutEntitlement);
    }
    const std::string id = Options->EntitlementId;
    auto it = g_entitlement_map.find(id);
    if (it == g_entitlement_map.end()) {
        Logger::warn("[INTERCEPT] CopyEntitlementById: '%s' not found", id.c_str());
        return EOS_EResult::EOS_NotFound;
    }
    Logger::dlc("[INTERCEPT] CopyEntitlementById: %s", id.c_str());
    *OutEntitlement = MakeEntitlement(it->first, it->second);
    return EOS_EResult::EOS_Success;
}

void Ecom_Entitlement_Release(Ecom_Entitlement_Release_t original, EOS_Ecom_Entitlement* Entitlement) {
    if (!Config::EnableEntitlementUnlocker()) {
        original(Entitlement);
        return;
    }
    if (Entitlement) {
        Logger::debug("[INTERCEPT] EOS_Ecom_Entitlement_Release: %s", Entitlement->EntitlementId);
        delete Entitlement;
    } else {
        Logger::warn("[INTERCEPT] EOS_Ecom_Entitlement_Release: null entitlement");
    }
}

void Ecom_QueryEntitlementToken(Ecom_QueryEntitlementToken_t original, EOS_HEcom Handle, const EOS_Ecom_QueryEntitlementTokenOptions* Options, void* ClientData, const EOS_Ecom_OnQueryEntitlementTokenCallback CompletionDelegate) {
    Logger::debug("[INTERCEPT] EOS_Ecom_QueryEntitlementToken called");
    auto container = new ScreamAPI::OriginalDataContainer(ClientData, CompletionDelegate);
    original(Handle, Options, container,
        [](const EOS_Ecom_QueryEntitlementTokenCallbackInfo* Data){
            ScreamAPI::proxyCallback<EOS_Ecom_QueryEntitlementTokenCallbackInfo>(
                Data, &Data->ClientData,
                [](EOS_Ecom_QueryEntitlementTokenCallbackInfo* mData){
                    Logger::dlc("[INTERCEPT] QueryEntitlementToken result: %s - forcing success",
                        EOS_EResult_ToString(mData->ResultCode));
                    mData->ResultCode = EOS_EResult::EOS_Success;
                }
            );
        }
    );
}

void Ecom_RedeemEntitlements(Ecom_RedeemEntitlements_t original, EOS_HEcom Handle, const EOS_Ecom_RedeemEntitlementsOptions* Options, void* ClientData, const EOS_Ecom_OnRedeemEntitlementsCallback CompletionDelegate) {
    Logger::debug("[INTERCEPT] EOS_Ecom_RedeemEntitlements called");
    auto container = new ScreamAPI::OriginalDataContainer(ClientData, CompletionDelegate);
    original(Handle, Options, container,
        [](const EOS_Ecom_RedeemEntitlementsCallbackInfo* Data){
            ScreamAPI::proxyCallback<EOS_Ecom_RedeemEntitlementsCallbackInfo>(
                Data, &Data->ClientData,
                [](EOS_Ecom_RedeemEntitlementsCallbackInfo* mData){
                    Logger::dlc("[INTERCEPT] RedeemEntitlements result: %s - forcing success",
                        EOS_EResult_ToString(mData->ResultCode));
                    mData->ResultCode = EOS_EResult::EOS_Success;
                }
            );
        }
    );
}

uint32_t Ecom_GetItemReleaseCount(Ecom_GetItemReleaseCount_t /*original*/, EOS_HEcom Handle, const EOS_Ecom_GetItemReleaseCountOptions* Options) {
    Logger::debug("[INTERCEPT] EOS_Ecom_GetItemReleaseCount called");
    Logger::dlc("[INTERCEPT] GetItemReleaseCount for item: %s -> returning 0", Options->ItemId);
    return 0;
}

void Ecom_Checkout(Ecom_Checkout_t original, EOS_HEcom Handle, const EOS_Ecom_CheckoutOptions* Options, void* ClientData, const EOS_Ecom_OnCheckoutCallback CompletionDelegate) {
    Logger::debug("[INTERCEPT] EOS_Ecom_Checkout called");
    auto container = new ScreamAPI::OriginalDataContainer(ClientData, CompletionDelegate);
    original(Handle, Options, container, [](const EOS_Ecom_CheckoutCallbackInfo* Data){
        ScreamAPI::proxyCallback<EOS_Ecom_CheckoutCallbackInfo>(Data, &Data->ClientData,
            [](EOS_Ecom_CheckoutCallbackInfo* mData){
                Logger::dlc("[INTERCEPT]   ResultString: %s", EOS_EResult_ToString(mData->ResultCode));
                Logger::dlc("[INTERCEPT]   TransactionId: %s", mData->TransactionId);
        });
    });
}

// ── Connect ────────────────────────────────────────────────────────────────

void Connect_Login(Connect_Login_t original, EOS_HConnect Handle, const EOS_Connect_LoginOptions* Options, void* ClientData, const EOS_Connect_OnLoginCallback CompletionDelegate) {
    Logger::info("[INTERCEPT] EOS_Connect_Login called");
    auto container = new ScreamAPI::OriginalDataContainer(ClientData, CompletionDelegate);
    original(Handle, Options, container, [](const EOS_Connect_LoginCallbackInfo* Data){
        ScreamAPI::proxyCallback<EOS_Connect_LoginCallbackInfo>(Data, &Data->ClientData, [](EOS_Connect_LoginCallbackInfo* Data){
            if(Data->ResultCode == EOS_EResult::EOS_Success){
                Logger::info("[INTERCEPT] EOS Connect login successful, initializing achievement manager");
                AchievementManager::init();
            } else {
                Logger::error("[INTERCEPT] EOS Connect login failed: %s", EOS_EResult_ToString(Data->ResultCode));
            }
        });
    });
}

EOS_ProductUserId Connect_GetLoggedInUserByIndex(Connect_GetLoggedInUserByIndex_t original, EOS_HConnect Handle, int32_t Index) {
    auto result = original(Handle, Index);
    if (result) Logger::debug("[INTERCEPT] EOS_Connect_GetLoggedInUserByIndex[%d] -> %p", Index, result);
    return result;
}

// ── Auth ───────────────────────────────────────────────────────────────────

void Auth_Login(Auth_Login_t original, EOS_HAuth Handle, const EOS_Auth_LoginOptions* Options, void* ClientData, const EOS_Auth_OnLoginCallback CompletionDelegate) {
    Logger::info("[INTERCEPT] EOS_Auth_Login called");
    original(Handle, Options, ClientData, CompletionDelegate);
}

EOS_EpicAccountId Auth_GetLoggedInAccountByIndex(Auth_GetLoggedInAccountByIndex_t original, EOS_HAuth Handle, int32_t Index) {
    auto result = original(Handle, Index);
    if (result) Logger::debug("[INTERCEPT] EOS_Auth_GetLoggedInAccountByIndex[%d] -> %p", Index, result);
    return result;
}

// ── Common ─────────────────────────────────────────────────────────────────

EOS_Bool EpicAccountId_IsValid(EpicAccountId_IsValid_t original, EOS_EpicAccountId AccountId) {
    auto result = original(AccountId);
    Logger::debug("[INTERCEPT] EpicAccountId_IsValid: %s", result ? "True" : "False");
    return result;
}

const char* EResult_ToString(EResult_ToString_t original, EOS_EResult Result) {
    try {
        return original(Result);
    } catch(ScreamAPI::FunctionNotFoundException) {
        return Util::copy_c_string((std::to_string((int)Result).c_str()));
    }
}

// ── Notifications ──────────────────────────────────────────────────────────

EOS_NotificationId Connect_AddNotifyLoginStatusChanged(Connect_AddNotifyLoginStatusChanged_t original, EOS_HConnect Handle, const EOS_Connect_AddNotifyLoginStatusChangedOptions* Options, void* ClientData, const EOS_Connect_OnLoginStatusChangedCallback NotificationFn) {
    Logger::debug("[INTERCEPT] EOS_Connect_AddNotifyLoginStatusChanged called");
    return original(Handle, Options, ClientData, NotificationFn);
}

EOS_NotificationId Auth_AddNotifyLoginStatusChanged(Auth_AddNotifyLoginStatusChanged_t original, EOS_HAuth Handle, const EOS_Auth_AddNotifyLoginStatusChangedOptions* Options, void* ClientData, const EOS_Auth_OnLoginStatusChangedCallback NotificationFn) {
    Logger::debug("[INTERCEPT] EOS_Auth_AddNotifyLoginStatusChanged called");
    return original(Handle, Options, ClientData, NotificationFn);
}

// ── Metrics ────────────────────────────────────────────────────────────────

EOS_EResult Metrics_BeginPlayerSession(Metrics_BeginPlayerSession_t original, EOS_HMetrics Handle, const EOS_Metrics_BeginPlayerSessionOptions* Options) {
    if (Config::BlockMetrics()) {
        Logger::info("[INTERCEPT] EOS_Metrics_BeginPlayerSession blocked (BlockMetrics=true)");
        return EOS_EResult::EOS_Success;
    }
    return original(Handle, Options);
}

EOS_EResult Metrics_EndPlayerSession(Metrics_EndPlayerSession_t original, EOS_HMetrics Handle, const EOS_Metrics_EndPlayerSessionOptions* Options) {
    if (Config::BlockMetrics()) {
        Logger::debug("[INTERCEPT] EOS_Metrics_EndPlayerSession blocked (BlockMetrics=true)");
        return EOS_EResult::EOS_Success;
    }
    return original(Handle, Options);
}

} // namespace Intercept
