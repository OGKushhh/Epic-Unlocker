#include "pch.h"
#include "eos_hooks.h"
#include "ScreamAPI.h"
#include "eos_intercept.h"
#include "achievement_manager.h"
#include "util.h"
#include "eos_compat.h"
#include "eos_resolve.h"
#include "Logger.h"
#include "MinHook.h"
#include "dlc_catalog.h"
#include "eos-sdk/eos_logging.h"
#include "eos-sdk/eos_metrics.h"
#include <mutex>
#include <vector>
#include <queue>
#include <thread>
#include <atomic>
#include <map>
#include <fstream>

namespace EOS_Hooks {

static bool hooksInitialized = false;
static HMODULE originalEOSDLL = nullptr;

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

// ============================================================================
// HOOK IMPLEMENTATIONS — thin wrappers delegating to Intercept::
// ============================================================================

namespace Hooks {

// Platform hooks
EOS_HPlatform EOS_CALL Platform_Create(const EOS_Platform_Options* Options) {
    return Intercept::Platform_Create(Original::Platform_Create, Options);
}

void EOS_CALL Platform_Release(EOS_HPlatform Handle) {
    Intercept::Platform_Release(Original::Platform_Release, Handle);
}

void EOS_CALL Platform_Tick(EOS_HPlatform Handle) {
    Intercept::Platform_Tick(Original::Platform_Tick, Handle);
}

EOS_HConnect EOS_CALL Platform_GetConnectInterface(EOS_HPlatform Handle) {
    return Intercept::Platform_GetConnectInterface(Original::Platform_GetConnectInterface, Handle);
}

EOS_HAuth EOS_CALL Platform_GetAuthInterface(EOS_HPlatform Handle) {
    return Intercept::Platform_GetAuthInterface(Original::Platform_GetAuthInterface, Handle);
}

EOS_HAchievements EOS_CALL Platform_GetAchievementsInterface(EOS_HPlatform Handle) {
    return Intercept::Platform_GetAchievementsInterface(Original::Platform_GetAchievementsInterface, Handle);
}

EOS_HEcom EOS_CALL Platform_GetEcomInterface(EOS_HPlatform Handle) {
    return Intercept::Platform_GetEcomInterface(Original::Platform_GetEcomInterface, Handle);
}

EOS_HStats EOS_CALL Platform_GetStatsInterface(EOS_HPlatform Handle) {
    return Intercept::Platform_GetStatsInterface(Original::Platform_GetStatsInterface, Handle);
}

EOS_HUI EOS_CALL Platform_GetUIInterface(EOS_HPlatform Handle) {
    return Intercept::Platform_GetUIInterface(Original::Platform_GetUIInterface, Handle);
}

// Achievements hooks
void EOS_CALL Achievements_QueryDefinitions(EOS_HAchievements Handle, const EOS_Achievements_QueryDefinitionsOptions* Options, void* ClientData, const EOS_Achievements_OnQueryDefinitionsCompleteCallback CompletionDelegate) {
    Intercept::Achievements_QueryDefinitions(Original::Achievements_QueryDefinitions, Handle, Options, ClientData, CompletionDelegate);
}

void EOS_CALL Achievements_QueryPlayerAchievements(EOS_HAchievements Handle, const EOS_Achievements_QueryPlayerAchievementsOptions* Options, void* ClientData, const EOS_Achievements_OnQueryPlayerAchievementsCompleteCallback CompletionDelegate) {
    Intercept::Achievements_QueryPlayerAchievements(Original::Achievements_QueryPlayerAchievements, Handle, Options, ClientData, CompletionDelegate);
}

void EOS_CALL Achievements_UnlockAchievements(EOS_HAchievements Handle, const EOS_Achievements_UnlockAchievementsOptions* Options, void* ClientData, const EOS_Achievements_OnUnlockAchievementsCompleteCallback CompletionDelegate) {
    Intercept::Achievements_UnlockAchievements(Original::Achievements_UnlockAchievements, Handle, Options, ClientData, CompletionDelegate);
}

EOS_NotificationId EOS_CALL Achievements_AddNotifyAchievementsUnlockedV2(EOS_HAchievements Handle, const EOS_Achievements_AddNotifyAchievementsUnlockedV2Options* Options, void* ClientData, const EOS_Achievements_OnAchievementsUnlockedCallbackV2 NotificationFn) {
    return Intercept::Achievements_AddNotifyAchievementsUnlockedV2(Original::Achievements_AddNotifyAchievementsUnlockedV2, Handle, Options, ClientData, NotificationFn);
}

EOS_NotificationId EOS_CALL Achievements_AddNotifyAchievementsUnlocked(EOS_HAchievements Handle, const EOS_Achievements_AddNotifyAchievementsUnlockedOptions* Options, void* ClientData, const EOS_Achievements_OnAchievementsUnlockedCallback NotificationFn) {
    return Intercept::Achievements_AddNotifyAchievementsUnlocked(Original::Achievements_AddNotifyAchievementsUnlocked, Handle, Options, ClientData, NotificationFn);
}

// Ecom hooks
void EOS_CALL Ecom_QueryOwnership(EOS_HEcom Handle, const EOS_Ecom_QueryOwnershipOptions* Options, void* ClientData, const EOS_Ecom_OnQueryOwnershipCallback CompletionDelegate) {
    Intercept::Ecom_QueryOwnership(Original::Ecom_QueryOwnership, Handle, Options, ClientData, CompletionDelegate);
}

void EOS_CALL Ecom_QueryEntitlements(EOS_HEcom Handle, const EOS_Ecom_QueryEntitlementsOptions* Options, void* ClientData, const EOS_Ecom_OnQueryEntitlementsCallback CompletionDelegate) {
    Intercept::Ecom_QueryEntitlements(Original::Ecom_QueryEntitlements, Handle, Options, ClientData, CompletionDelegate);
}

uint32_t EOS_CALL Ecom_GetEntitlementsCount(EOS_HEcom Handle, const EOS_Ecom_GetEntitlementsCountOptions* Options) {
    return Intercept::Ecom_GetEntitlementsCount(Original::Ecom_GetEntitlementsCount, Handle, Options);
}

EOS_EResult EOS_CALL Ecom_CopyEntitlementByIndex(EOS_HEcom Handle, const EOS_Ecom_CopyEntitlementByIndexOptions* Options, EOS_Ecom_Entitlement** OutEntitlement) {
    return Intercept::Ecom_CopyEntitlementByIndex(Original::Ecom_CopyEntitlementByIndex, Handle, Options, OutEntitlement);
}

void EOS_CALL Ecom_Entitlement_Release(EOS_Ecom_Entitlement* Entitlement) {
    Intercept::Ecom_Entitlement_Release(Original::Ecom_Entitlement_Release, Entitlement);
}

// Ecom OwnershipBySandboxIds hook
void EOS_CALL Ecom_QueryOwnershipBySandboxIds(EOS_HEcom Handle, const EOS_Ecom_QueryOwnershipBySandboxIdsOptions* Options, void* ClientData, const EOS_Ecom_OnQueryOwnershipBySandboxIdsCallback CompletionDelegate) {
    Intercept::Ecom_QueryOwnershipBySandboxIds(Original::Ecom_QueryOwnershipBySandboxIds, Handle, Options, ClientData, CompletionDelegate);
}

// Ecom QueryOwnershipToken hook
void EOS_CALL Ecom_QueryOwnershipToken(EOS_HEcom Handle, const EOS_Ecom_QueryOwnershipTokenOptions* Options, void* ClientData, const EOS_Ecom_OnQueryOwnershipTokenCallback CompletionDelegate) {
    Intercept::Ecom_QueryOwnershipToken(Original::Ecom_QueryOwnershipToken, Handle, Options, ClientData, CompletionDelegate);
}

// Connect hooks
void EOS_CALL Connect_Login(EOS_HConnect Handle, const EOS_Connect_LoginOptions* Options, void* ClientData, const EOS_Connect_OnLoginCallback CompletionDelegate) {
    Intercept::Connect_Login(Original::Connect_Login, Handle, Options, ClientData, CompletionDelegate);
}

EOS_ProductUserId EOS_CALL Connect_GetLoggedInUserByIndex(EOS_HConnect Handle, int32_t Index) {
    return Intercept::Connect_GetLoggedInUserByIndex(Original::Connect_GetLoggedInUserByIndex, Handle, Index);
}

void EOS_CALL Connect_AddNotifyLoginStatusChanged(EOS_HConnect Handle, const EOS_Connect_AddNotifyLoginStatusChangedOptions* Options, void* ClientData, const EOS_Connect_OnLoginStatusChangedCallback NotificationFn) {
    Intercept::Connect_AddNotifyLoginStatusChanged(Original::Connect_AddNotifyLoginStatusChanged, Handle, Options, ClientData, NotificationFn);
}

// Auth hooks
void EOS_CALL Auth_Login(EOS_HAuth Handle, const EOS_Auth_LoginOptions* Options, void* ClientData, const EOS_Auth_OnLoginCallback CompletionDelegate) {
    Intercept::Auth_Login(Original::Auth_Login, Handle, Options, ClientData, CompletionDelegate);
}

EOS_EpicAccountId EOS_CALL Auth_GetLoggedInAccountByIndex(EOS_HAuth Handle, int32_t Index) {
    return Intercept::Auth_GetLoggedInAccountByIndex(Original::Auth_GetLoggedInAccountByIndex, Handle, Index);
}

void EOS_CALL Auth_AddNotifyLoginStatusChanged(EOS_HAuth Handle, const EOS_Auth_AddNotifyLoginStatusChangedOptions* Options, void* ClientData, const EOS_Auth_OnLoginStatusChangedCallback NotificationFn) {
    Intercept::Auth_AddNotifyLoginStatusChanged(Original::Auth_AddNotifyLoginStatusChanged, Handle, Options, ClientData, NotificationFn);
}

// Metrics hooks (E1: BlockMetrics config knob)
EOS_EResult EOS_CALL Metrics_BeginPlayerSession(EOS_HMetrics Handle, const EOS_Metrics_BeginPlayerSessionOptions* Options) {
    return Intercept::Metrics_BeginPlayerSession(Original::Metrics_BeginPlayerSession, Handle, Options);
}

EOS_EResult EOS_CALL Metrics_EndPlayerSession(EOS_HMetrics Handle, const EOS_Metrics_EndPlayerSessionOptions* Options) {
    return Intercept::Metrics_EndPlayerSession(Original::Metrics_EndPlayerSession, Handle, Options);
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
    if (EOS_Compat::isFeatureAvailable("AchievementsUnlockedV2")) {
        Logger::info("[HOOK]   V2 achievement notifications: AVAILABLE (SDK 1.14+)");
    } else {
        Logger::warn("[HOOK]   V2 achievement notifications: NOT AVAILABLE (SDK < 1.14)");
        Logger::warn("[HOOK]   V2 hooks will be skipped (INSTALL_HOOK_OPTIONAL)");
    }
    INSTALL_HOOK(originalDLL, EOS_Achievements_QueryDefinitions, Hooks::Achievements_QueryDefinitions, Original::Achievements_QueryDefinitions);
    INSTALL_HOOK(originalDLL, EOS_Achievements_QueryPlayerAchievements, Hooks::Achievements_QueryPlayerAchievements, Original::Achievements_QueryPlayerAchievements);
    INSTALL_HOOK(originalDLL, EOS_Achievements_UnlockAchievements, Hooks::Achievements_UnlockAchievements, Original::Achievements_UnlockAchievements);
    INSTALL_HOOK_OPTIONAL(originalDLL, EOS_Achievements_AddNotifyAchievementsUnlockedV2, Hooks::Achievements_AddNotifyAchievementsUnlockedV2, Original::Achievements_AddNotifyAchievementsUnlockedV2);
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

    // Store achievement originals for ForceAchievementsConfiguration
    Intercept::SetAchievementsOriginals(
        Original::Achievements_QueryDefinitions,
        Original::Achievements_QueryPlayerAchievements,
        Original::Achievements_UnlockAchievements
    );

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
    Intercept::QueueUnlock(id);
}

} // namespace EOS_Hooks
