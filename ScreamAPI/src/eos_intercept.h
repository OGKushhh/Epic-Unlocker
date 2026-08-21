#pragma once
#include "pch.h"
#include "eos-sdk/eos_sdk.h"
#include "eos-sdk/eos_achievements.h"
#include "eos-sdk/eos_auth.h"
#include "eos-sdk/eos_connect.h"
#include "eos-sdk/eos_ecom.h"
#include "eos-sdk/eos_stats.h"
#include "eos-sdk/eos_logging.h"
#include "eos-sdk/eos_metrics.h"
#include "eos-sdk/eos_init.h"
#include <map>
#include <string>

namespace GameAlloc {
    // Game's custom memory allocator, captured from EOS_InitializeOptions.
    // nullptr means the game didn't provide one (use CRT malloc/free as fallback).
    inline EOS_AllocateMemoryFunc  Alloc  = nullptr;
    inline EOS_ReleaseMemoryFunc   Free   = nullptr;
    inline EOS_ReallocateMemoryFunc Realloc = nullptr;

    inline bool HasAllocator() { return Alloc != nullptr; }

    // Allocate using the game's allocator (or CRT malloc as fallback).
    // Alignment is always a power of 2 per EOS SDK contract.
    inline void* Allocate(size_t size, size_t alignment = 1) {
        if (Alloc) return Alloc(size, alignment);
        // CRT fallback — _aligned_malloc for proper alignment support
        return _aligned_malloc(size, alignment);
    }

    // Free using the game's deallocator (or CRT free as fallback).
    inline void Release(void* ptr) {
        if (!ptr) return;
        if (Free) { Free(ptr); return; }
        _aligned_free(ptr);
    }

    // Copy a std::string into a game-allocator-owned buffer.
    // Returns nullptr on allocation failure.
    inline char* CopyString(const std::string& s) {
        size_t len = s.size() + 1; // include null terminator
        char* buf = static_cast<char*>(Allocate(len, 1));
        if (!buf) return nullptr;
        memcpy(buf, s.c_str(), len);
        return buf;
    }

    // Initialize from EOS_InitializeOptions. Call once during EOS_Initialize.
    inline void CaptureFromOptions(const EOS_InitializeOptions* Options) {
        if (!Options) return;
        if (Options->AllocateMemoryFunction) {
            Alloc   = Options->AllocateMemoryFunction;
            Free    = Options->ReleaseMemoryFunction;
            Realloc = Options->ReallocateMemoryFunction;
        } else {
            Alloc   = nullptr;
            Free    = nullptr;
            Realloc = nullptr;
        }
    }
}

namespace Intercept {

// ── Function pointer types ──────────────────────────────────────────────────

// Platform
typedef EOS_HPlatform (EOS_CALL *Platform_Create_t)(const EOS_Platform_Options*);
typedef void (EOS_CALL *Platform_Release_t)(EOS_HPlatform);
typedef void (EOS_CALL *Platform_Tick_t)(EOS_HPlatform);
typedef EOS_HConnect (EOS_CALL *Platform_GetConnectInterface_t)(EOS_HPlatform);
typedef EOS_HAuth (EOS_CALL *Platform_GetAuthInterface_t)(EOS_HPlatform);
typedef EOS_HAchievements (EOS_CALL *Platform_GetAchievementsInterface_t)(EOS_HPlatform);
typedef EOS_HEcom (EOS_CALL *Platform_GetEcomInterface_t)(EOS_HPlatform);
typedef EOS_HStats (EOS_CALL *Platform_GetStatsInterface_t)(EOS_HPlatform);
typedef EOS_HUI (EOS_CALL *Platform_GetUIInterface_t)(EOS_HPlatform);

// Achievements
typedef void (EOS_CALL *Achievements_QueryDefinitions_t)(EOS_HAchievements, const EOS_Achievements_QueryDefinitionsOptions*, void*, const EOS_Achievements_OnQueryDefinitionsCompleteCallback);
typedef void (EOS_CALL *Achievements_QueryPlayerAchievements_t)(EOS_HAchievements, const EOS_Achievements_QueryPlayerAchievementsOptions*, void*, const EOS_Achievements_OnQueryPlayerAchievementsCompleteCallback);
typedef void (EOS_CALL *Achievements_UnlockAchievements_t)(EOS_HAchievements, const EOS_Achievements_UnlockAchievementsOptions*, void*, const EOS_Achievements_OnUnlockAchievementsCompleteCallback);
typedef uint32_t (EOS_CALL *Achievements_GetPlayerAchievementCount_t)(EOS_HAchievements, const EOS_Achievements_GetPlayerAchievementCountOptions*);
typedef uint32_t (EOS_CALL *Achievements_GetAchievementDefinitionCount_t)(EOS_HAchievements, const EOS_Achievements_GetAchievementDefinitionCountOptions*);
typedef EOS_NotificationId (EOS_CALL *Achievements_AddNotifyAchievementsUnlockedV2_t)(EOS_HAchievements, const EOS_Achievements_AddNotifyAchievementsUnlockedV2Options*, void*, const EOS_Achievements_OnAchievementsUnlockedCallbackV2);
typedef EOS_NotificationId (EOS_CALL *Achievements_AddNotifyAchievementsUnlocked_t)(EOS_HAchievements, const EOS_Achievements_AddNotifyAchievementsUnlockedOptions*, void*, const EOS_Achievements_OnAchievementsUnlockedCallback);

// Ecom Ownership
typedef void (EOS_CALL *Ecom_QueryOwnership_t)(EOS_HEcom, const EOS_Ecom_QueryOwnershipOptions*, void*, const EOS_Ecom_OnQueryOwnershipCallback);
typedef void (EOS_CALL *Ecom_QueryOwnershipBySandboxIds_t)(EOS_HEcom, const EOS_Ecom_QueryOwnershipBySandboxIdsOptions*, void*, const EOS_Ecom_OnQueryOwnershipBySandboxIdsCallback);
typedef void (EOS_CALL *Ecom_QueryOwnershipToken_t)(EOS_HEcom, const EOS_Ecom_QueryOwnershipTokenOptions*, void*, const EOS_Ecom_OnQueryOwnershipTokenCallback);

// Ecom Entitlements
typedef void (EOS_CALL *Ecom_QueryEntitlements_t)(EOS_HEcom, const EOS_Ecom_QueryEntitlementsOptions*, void*, const EOS_Ecom_OnQueryEntitlementsCallback);
typedef uint32_t (EOS_CALL *Ecom_GetEntitlementsCount_t)(EOS_HEcom, const EOS_Ecom_GetEntitlementsCountOptions*);
typedef uint32_t (EOS_CALL *Ecom_GetEntitlementsByNameCount_t)(EOS_HEcom, const EOS_Ecom_GetEntitlementsByNameCountOptions*);
typedef EOS_EResult (EOS_CALL *Ecom_CopyEntitlementByIndex_t)(EOS_HEcom, const EOS_Ecom_CopyEntitlementByIndexOptions*, EOS_Ecom_Entitlement**);
typedef EOS_EResult (EOS_CALL *Ecom_CopyEntitlementByNameAndIndex_t)(EOS_HEcom, const EOS_Ecom_CopyEntitlementByNameAndIndexOptions*, EOS_Ecom_Entitlement**);
typedef EOS_EResult (EOS_CALL *Ecom_CopyEntitlementById_t)(EOS_HEcom, const EOS_Ecom_CopyEntitlementByIdOptions*, EOS_Ecom_Entitlement**);
typedef void (EOS_CALL *Ecom_Entitlement_Release_t)(EOS_Ecom_Entitlement*);
typedef void (EOS_CALL *Ecom_QueryEntitlementToken_t)(EOS_HEcom, const EOS_Ecom_QueryEntitlementTokenOptions*, void*, const EOS_Ecom_OnQueryEntitlementTokenCallback);
typedef void (EOS_CALL *Ecom_RedeemEntitlements_t)(EOS_HEcom, const EOS_Ecom_RedeemEntitlementsOptions*, void*, const EOS_Ecom_OnRedeemEntitlementsCallback);
typedef uint32_t (EOS_CALL *Ecom_GetItemReleaseCount_t)(EOS_HEcom, const EOS_Ecom_GetItemReleaseCountOptions*);
typedef void (EOS_CALL *Ecom_Checkout_t)(EOS_HEcom, const EOS_Ecom_CheckoutOptions*, void*, const EOS_Ecom_OnCheckoutCallback);

// Connect
typedef void (EOS_CALL *Connect_Login_t)(EOS_HConnect, const EOS_Connect_LoginOptions*, void*, const EOS_Connect_OnLoginCallback);
typedef EOS_ProductUserId (EOS_CALL *Connect_GetLoggedInUserByIndex_t)(EOS_HConnect, int32_t);

// Auth
typedef void (EOS_CALL *Auth_Login_t)(EOS_HAuth, const EOS_Auth_LoginOptions*, void*, const EOS_Auth_OnLoginCallback);
typedef EOS_EpicAccountId (EOS_CALL *Auth_GetLoggedInAccountByIndex_t)(EOS_HAuth, int32_t);

// Common
typedef EOS_Bool (EOS_CALL *EpicAccountId_IsValid_t)(EOS_EpicAccountId);
typedef const char* (EOS_CALL *EResult_ToString_t)(EOS_EResult);

// Logging
typedef EOS_EResult (EOS_CALL *Logging_SetCallback_t)(EOS_LogMessageFunc);
typedef void (EOS_CALL *Logging_SetLogLevel_t)(EOS_ELogCategory, EOS_ELogLevel);

// Metrics
typedef EOS_EResult (EOS_CALL *Metrics_BeginPlayerSession_t)(EOS_HMetrics, const EOS_Metrics_BeginPlayerSessionOptions*);
typedef EOS_EResult (EOS_CALL *Metrics_EndPlayerSession_t)(EOS_HMetrics, const EOS_Metrics_EndPlayerSessionOptions*);

// Connect/Auth notifications
typedef EOS_NotificationId (EOS_CALL *Connect_AddNotifyLoginStatusChanged_t)(EOS_HConnect, const EOS_Connect_AddNotifyLoginStatusChangedOptions*, void*, const EOS_Connect_OnLoginStatusChangedCallback);
typedef EOS_NotificationId (EOS_CALL *Auth_AddNotifyLoginStatusChanged_t)(EOS_HAuth, const EOS_Auth_AddNotifyLoginStatusChangedOptions*, void*, const EOS_Auth_OnLoginStatusChangedCallback);

// ── Intercept function declarations ─────────────────────────────────────────

// Platform
EOS_HPlatform Platform_Create(Platform_Create_t original, const EOS_Platform_Options* Options);
void Platform_Release(Platform_Release_t original, EOS_HPlatform Handle);
void Platform_Tick(Platform_Tick_t original, EOS_HPlatform Handle);
EOS_HConnect Platform_GetConnectInterface(Platform_GetConnectInterface_t original, EOS_HPlatform Handle);
EOS_HAuth Platform_GetAuthInterface(Platform_GetAuthInterface_t original, EOS_HPlatform Handle);
EOS_HAchievements Platform_GetAchievementsInterface(Platform_GetAchievementsInterface_t original, EOS_HPlatform Handle);
EOS_HEcom Platform_GetEcomInterface(Platform_GetEcomInterface_t original, EOS_HPlatform Handle);
EOS_HStats Platform_GetStatsInterface(Platform_GetStatsInterface_t original, EOS_HPlatform Handle);
EOS_HUI Platform_GetUIInterface(Platform_GetUIInterface_t original, EOS_HPlatform Handle);

// Achievements
void Achievements_QueryDefinitions(Achievements_QueryDefinitions_t original, EOS_HAchievements Handle, const EOS_Achievements_QueryDefinitionsOptions* Options, void* ClientData, const EOS_Achievements_OnQueryDefinitionsCompleteCallback CompletionDelegate);
void Achievements_QueryPlayerAchievements(Achievements_QueryPlayerAchievements_t original, EOS_HAchievements Handle, const EOS_Achievements_QueryPlayerAchievementsOptions* Options, void* ClientData, const EOS_Achievements_OnQueryPlayerAchievementsCompleteCallback CompletionDelegate);
void Achievements_UnlockAchievements(Achievements_UnlockAchievements_t original, EOS_HAchievements Handle, const EOS_Achievements_UnlockAchievementsOptions* Options, void* ClientData, const EOS_Achievements_OnUnlockAchievementsCompleteCallback CompletionDelegate);
uint32_t Achievements_GetPlayerAchievementCount(Achievements_GetPlayerAchievementCount_t original, EOS_HAchievements Handle, const EOS_Achievements_GetPlayerAchievementCountOptions* Options);
uint32_t Achievements_GetAchievementDefinitionCount(Achievements_GetAchievementDefinitionCount_t original, EOS_HAchievements Handle, const EOS_Achievements_GetAchievementDefinitionCountOptions* Options);
EOS_NotificationId Achievements_AddNotifyAchievementsUnlockedV2(Achievements_AddNotifyAchievementsUnlockedV2_t original, EOS_HAchievements Handle, const EOS_Achievements_AddNotifyAchievementsUnlockedV2Options* Options, void* ClientData, const EOS_Achievements_OnAchievementsUnlockedCallbackV2 NotificationFn);
EOS_NotificationId Achievements_AddNotifyAchievementsUnlocked(Achievements_AddNotifyAchievementsUnlocked_t original, EOS_HAchievements Handle, const EOS_Achievements_AddNotifyAchievementsUnlockedOptions* Options, void* ClientData, const EOS_Achievements_OnAchievementsUnlockedCallback NotificationFn);

// Ecom Ownership
void Ecom_QueryOwnership(Ecom_QueryOwnership_t original, EOS_HEcom Handle, const EOS_Ecom_QueryOwnershipOptions* Options, void* ClientData, const EOS_Ecom_OnQueryOwnershipCallback CompletionDelegate);
void Ecom_QueryOwnershipBySandboxIds(Ecom_QueryOwnershipBySandboxIds_t original, EOS_HEcom Handle, const EOS_Ecom_QueryOwnershipBySandboxIdsOptions* Options, void* ClientData, const EOS_Ecom_OnQueryOwnershipBySandboxIdsCallback CompletionDelegate);
void Ecom_QueryOwnershipToken(Ecom_QueryOwnershipToken_t original, EOS_HEcom Handle, const EOS_Ecom_QueryOwnershipTokenOptions* Options, void* ClientData, const EOS_Ecom_OnQueryOwnershipTokenCallback CompletionDelegate);

// Ecom Entitlements
void Ecom_QueryEntitlements(Ecom_QueryEntitlements_t original, EOS_HEcom Handle, const EOS_Ecom_QueryEntitlementsOptions* Options, void* ClientData, const EOS_Ecom_OnQueryEntitlementsCallback CompletionDelegate);
uint32_t Ecom_GetEntitlementsCount(Ecom_GetEntitlementsCount_t original, EOS_HEcom Handle, const EOS_Ecom_GetEntitlementsCountOptions* Options);
uint32_t Ecom_GetEntitlementsByNameCount(Ecom_GetEntitlementsByNameCount_t original, EOS_HEcom Handle, const EOS_Ecom_GetEntitlementsByNameCountOptions* Options);
EOS_EResult Ecom_CopyEntitlementByIndex(Ecom_CopyEntitlementByIndex_t original, EOS_HEcom Handle, const EOS_Ecom_CopyEntitlementByIndexOptions* Options, EOS_Ecom_Entitlement** OutEntitlement);
EOS_EResult Ecom_CopyEntitlementByNameAndIndex(Ecom_CopyEntitlementByNameAndIndex_t original, EOS_HEcom Handle, const EOS_Ecom_CopyEntitlementByNameAndIndexOptions* Options, EOS_Ecom_Entitlement** OutEntitlement);
EOS_EResult Ecom_CopyEntitlementById(Ecom_CopyEntitlementById_t original, EOS_HEcom Handle, const EOS_Ecom_CopyEntitlementByIdOptions* Options, EOS_Ecom_Entitlement** OutEntitlement);
void Ecom_Entitlement_Release(Ecom_Entitlement_Release_t original, EOS_Ecom_Entitlement* Entitlement);
void Ecom_QueryEntitlementToken(Ecom_QueryEntitlementToken_t original, EOS_HEcom Handle, const EOS_Ecom_QueryEntitlementTokenOptions* Options, void* ClientData, const EOS_Ecom_OnQueryEntitlementTokenCallback CompletionDelegate);
void Ecom_RedeemEntitlements(Ecom_RedeemEntitlements_t original, EOS_HEcom Handle, const EOS_Ecom_RedeemEntitlementsOptions* Options, void* ClientData, const EOS_Ecom_OnRedeemEntitlementsCallback CompletionDelegate);
uint32_t Ecom_GetItemReleaseCount(Ecom_GetItemReleaseCount_t original, EOS_HEcom Handle, const EOS_Ecom_GetItemReleaseCountOptions* Options);
void Ecom_Checkout(Ecom_Checkout_t original, EOS_HEcom Handle, const EOS_Ecom_CheckoutOptions* Options, void* ClientData, const EOS_Ecom_OnCheckoutCallback CompletionDelegate);

// Connect
void Connect_Login(Connect_Login_t original, EOS_HConnect Handle, const EOS_Connect_LoginOptions* Options, void* ClientData, const EOS_Connect_OnLoginCallback CompletionDelegate);
EOS_ProductUserId Connect_GetLoggedInUserByIndex(Connect_GetLoggedInUserByIndex_t original, EOS_HConnect Handle, int32_t Index);

// Auth
void Auth_Login(Auth_Login_t original, EOS_HAuth Handle, const EOS_Auth_LoginOptions* Options, void* ClientData, const EOS_Auth_OnLoginCallback CompletionDelegate);
EOS_EpicAccountId Auth_GetLoggedInAccountByIndex(Auth_GetLoggedInAccountByIndex_t original, EOS_HAuth Handle, int32_t Index);

// Common
EOS_Bool EpicAccountId_IsValid(EpicAccountId_IsValid_t original, EOS_EpicAccountId AccountId);
const char* EResult_ToString(EResult_ToString_t original, EOS_EResult Result);

// Notifications
EOS_NotificationId Connect_AddNotifyLoginStatusChanged(Connect_AddNotifyLoginStatusChanged_t original, EOS_HConnect Handle, const EOS_Connect_AddNotifyLoginStatusChangedOptions* Options, void* ClientData, const EOS_Connect_OnLoginStatusChangedCallback NotificationFn);
EOS_NotificationId Auth_AddNotifyLoginStatusChanged(Auth_AddNotifyLoginStatusChanged_t original, EOS_HAuth Handle, const EOS_Auth_AddNotifyLoginStatusChangedOptions* Options, void* ClientData, const EOS_Auth_OnLoginStatusChangedCallback NotificationFn);

// Metrics
EOS_EResult Metrics_BeginPlayerSession(Metrics_BeginPlayerSession_t original, EOS_HMetrics Handle, const EOS_Metrics_BeginPlayerSessionOptions* Options);
EOS_EResult Metrics_EndPlayerSession(Metrics_EndPlayerSession_t original, EOS_HMetrics Handle, const EOS_Metrics_EndPlayerSessionOptions* Options);

// SDK Log support (called from Intercept::Platform_Create)
void SetSDKLogPath(const std::wstring& path);
void RegisterSDKLogCallback(Logging_SetCallback_t setCallback, Logging_SetLogLevel_t setLogLevel);

// GUI unlock queue (called from Intercept::Platform_Tick)
void QueueUnlock(const char* id);

// Catalog helpers (called from both ownership and entitlement paths)
void EnsureCatalogFetched();
std::map<std::string, std::string> GetCatalogSnapshot();

// Store original achievement function pointers for ForceAchievementsConfiguration
// Called from the UnlockAchievements wrapper before the first unlock attempt
void SetAchievementsOriginals(
    Achievements_QueryDefinitions_t queryDefs,
    Achievements_QueryPlayerAchievements_t queryPlayer,
    Achievements_UnlockAchievements_t unlockAchievements);

} // namespace Intercept
