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

// ====================================================================
// Phase B #1: EOS_HOOK_TABLE -- single source of truth for intercepted
// functions. Add a row here, the declaration AND the install call are
// both generated automatically. No more three-place edits.
//
// Columns:
//   EosName     - the EOS_ symbol name (passed to GetProcAddress)
//   OurName     - the local name (Original::OurName, Hooks::OurName)
//   Variant     - MANDATORY or OPTIONAL
//   Category    - logical group (for logging during install)
// ====================================================================
#define EOS_HOOK_TABLE(X) \
    /* Category: Platform */ \
    X(EOS_Platform_Create,                        Platform_Create,                        MANDATORY, Platform) \
    X(EOS_Platform_Release,                       Platform_Release,                       MANDATORY, Platform) \
    X(EOS_Platform_Tick,                          Platform_Tick,                          MANDATORY, Platform) \
    X(EOS_Platform_GetConnectInterface,           Platform_GetConnectInterface,           MANDATORY, Platform) \
    X(EOS_Platform_GetAuthInterface,              Platform_GetAuthInterface,              MANDATORY, Platform) \
    X(EOS_Platform_GetAchievementsInterface,      Platform_GetAchievementsInterface,      MANDATORY, Platform) \
    X(EOS_Platform_GetEcomInterface,              Platform_GetEcomInterface,              MANDATORY, Platform) \
    X(EOS_Platform_GetStatsInterface,             Platform_GetStatsInterface,             MANDATORY, Platform) \
    X(EOS_Platform_GetUIInterface,                Platform_GetUIInterface,                OPTIONAL,  Platform) \
    /* Category: Achievements */ \
    X(EOS_Achievements_QueryDefinitions,          Achievements_QueryDefinitions,           MANDATORY, Achievements) \
    X(EOS_Achievements_QueryPlayerAchievements,   Achievements_QueryPlayerAchievements,   MANDATORY, Achievements) \
    X(EOS_Achievements_UnlockAchievements,        Achievements_UnlockAchievements,        MANDATORY, Achievements) \
    X(EOS_Achievements_AddNotifyAchievementsUnlockedV2, Achievements_AddNotifyAchievementsUnlockedV2, OPTIONAL, Achievements) \
    X(EOS_Achievements_AddNotifyAchievementsUnlocked,  Achievements_AddNotifyAchievementsUnlocked,  OPTIONAL, Achievements) \
    /* Category: Ecom */ \
    X(EOS_Ecom_QueryOwnership,                    Ecom_QueryOwnership,                    MANDATORY, Ecom) \
    X(EOS_Ecom_QueryOwnershipBySandboxIds,        Ecom_QueryOwnershipBySandboxIds,       OPTIONAL,  Ecom) \
    X(EOS_Ecom_QueryOwnershipToken,               Ecom_QueryOwnershipToken,               OPTIONAL,  Ecom) \
    X(EOS_Ecom_QueryEntitlements,                 Ecom_QueryEntitlements,                 MANDATORY, Ecom) \
    X(EOS_Ecom_GetEntitlementsCount,              Ecom_GetEntitlementsCount,              MANDATORY, Ecom) \
    X(EOS_Ecom_CopyEntitlementByIndex,            Ecom_CopyEntitlementByIndex,             MANDATORY, Ecom) \
    X(EOS_Ecom_Entitlement_Release,               Ecom_Entitlement_Release,               MANDATORY, Ecom) \
    /* Category: Connect */ \
    X(EOS_Connect_Login,                          Connect_Login,                          MANDATORY, Connect) \
    X(EOS_Connect_GetLoggedInUserByIndex,         Connect_GetLoggedInUserByIndex,         MANDATORY, Connect) \
    X(EOS_Connect_AddNotifyLoginStatusChanged,    Connect_AddNotifyLoginStatusChanged,    OPTIONAL,  Connect) \
    /* Category: Auth */ \
    X(EOS_Auth_Login,                             Auth_Login,                             MANDATORY, Auth) \
    X(EOS_Auth_GetLoggedInAccountByIndex,         Auth_GetLoggedInAccountByIndex,         MANDATORY, Auth) \
    X(EOS_Auth_AddNotifyLoginStatusChanged,       Auth_AddNotifyLoginStatusChanged,       OPTIONAL,  Auth) \
    /* Category: Metrics */ \
    X(EOS_Metrics_BeginPlayerSession,              Metrics_BeginPlayerSession,            OPTIONAL,  Metrics) \
    X(EOS_Metrics_EndPlayerSession,                Metrics_EndPlayerSession,              OPTIONAL,  Metrics) \
    /* Logging: not hooked, just resolved via GetProcAddress so we can call
       them from inside the proxy DLL without tripping the linker forwarder. */ \
    X(EOS_Logging_SetCallback,                    Logging_SetCallback,                    RESOLVE_ONLY, Logging) \
    X(EOS_Logging_SetLogLevel,                    Logging_SetLogLevel,                    RESOLVE_ONLY, Logging)

// Original function pointers (filled by MinHook). Generated from EOS_HOOK_TABLE.
namespace Original {
    // X-macro expansion: one decltype decl per row.
    #define X_DECL(EosName, OurName, Variant, Category) \
        decltype(&EosName) OurName = nullptr;
    EOS_HOOK_TABLE(X_DECL)
    #undef X_DECL
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

// ====================================================================
// Phase B #8: PLATFORM_INTERFACE_HOOK macro
// All 6 EOS_Platform_Get*Interface hooks have the same signature shape:
//   RetType EOS_CALL Platform_GetXxxInterface(EOS_HPlatform Handle)
//   { return Intercept::Platform_GetXxxInterface(Original::..., Handle); }
// One macro, six invocations. No more copy-paste drift.
// ====================================================================
#define PLATFORM_INTERFACE_HOOK(OurName, RetType) \
    RetType EOS_CALL OurName(EOS_HPlatform Handle) { \
        return Intercept::OurName(Original::OurName, Handle); \
    }

PLATFORM_INTERFACE_HOOK(Platform_GetConnectInterface,      EOS_HConnect)
PLATFORM_INTERFACE_HOOK(Platform_GetAuthInterface,         EOS_HAuth)
PLATFORM_INTERFACE_HOOK(Platform_GetAchievementsInterface, EOS_HAchievements)
PLATFORM_INTERFACE_HOOK(Platform_GetEcomInterface,         EOS_HEcom)
PLATFORM_INTERFACE_HOOK(Platform_GetStatsInterface,        EOS_HStats)
PLATFORM_INTERFACE_HOOK(Platform_GetUIInterface,           EOS_HUI)

#undef PLATFORM_INTERFACE_HOOK

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

    // ====================================================================
    // Phase B #1: Install hooks from EOS_HOOK_TABLE. The variant column
    // selects between INSTALL_HOOK (MANDATORY), INSTALL_HOOK_OPTIONAL
    // (OPTIONAL), and RESOLVE_ONLY (just GetProcAddress, no hook).
    // Each row's INSTALL_* macro already logs its own "Successfully hooked"
    // or "Function not found" line, so we don't add a duplicate log here.
    // ====================================================================
    if (EOS_Compat::isFeatureAvailable("AchievementsUnlockedV2")) {
        Logger::info("[HOOK]   V2 achievement notifications: AVAILABLE (SDK 1.14+)");
    } else {
        Logger::warn("[HOOK]   V2 achievement notifications: NOT AVAILABLE (SDK < 1.14)");
        Logger::warn("[HOOK]   V2 hooks will be skipped (INSTALL_HOOK_OPTIONAL)");
    }
    Logger::info("[HOOK] BlockMetrics=%s", Config::BlockMetrics() ? "true" : "false");

    // Token-paste aliases so INSTALL_##Variant resolves correctly.
    // INSTALL_HOOK and INSTALL_HOOK_OPTIONAL are defined above; we add
    // INSTALL_MANDATORY / INSTALL_OPTIONAL as aliases to match the X-macro
    // variant column names. RESOLVE_ONLY is handled inline below.
    #define INSTALL_MANDATORY   INSTALL_HOOK
    #define INSTALL_OPTIONAL    INSTALL_HOOK_OPTIONAL

    #define X_INSTALL(EosName, OurName, Variant, Category) \
        INSTALL_##Variant(originalDLL, EosName, Hooks::OurName, Original::OurName);
    // RESOLVE_ONLY entries skip the install and just GetProcAddress into Original::.
    #define INSTALL_RESOLVE_ONLY(module, EosName, HookFunc, originalPtr) \
        do { \
            if (void* p = ResolveExport(module, #EosName)) { \
                originalPtr = reinterpret_cast<decltype(originalPtr)>(p); \
            } else { \
                Logger::warn("[HOOK] Could not resolve %s (SDK log capture disabled)", #EosName); \
            } \
        } while(0)
    EOS_HOOK_TABLE(X_INSTALL)
    #undef X_INSTALL
    #undef INSTALL_RESOLVE_ONLY
    #undef INSTALL_MANDATORY
    #undef INSTALL_OPTIONAL

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

#undef EOS_HOOK_TABLE

} // namespace EOS_Hooks
