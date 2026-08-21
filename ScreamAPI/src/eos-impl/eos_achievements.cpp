#include "pch.h"
#include "eos-sdk/eos_achievements.h"
#include "ScreamAPI.h"
#include "eos_intercept.h"
#include "achievement_manager.h"
#include "util.h"


EOS_DECLARE_FUNC(uint32_t) EOS_Achievements_GetPlayerAchievementCount(EOS_HAchievements Handle, const EOS_Achievements_GetPlayerAchievementCountOptions* Options){
    static auto original = ScreamAPI::proxyFunction(&EOS_Achievements_GetPlayerAchievementCount, "EOS_Achievements_GetPlayerAchievementCount");
    return Intercept::Achievements_GetPlayerAchievementCount(original, Handle, Options);
}

// This is where the achievement magic happens ;)
EOS_DECLARE_FUNC(void) EOS_Achievements_UnlockAchievements(EOS_HAchievements Handle, const EOS_Achievements_UnlockAchievementsOptions* Options, void* ClientData, const EOS_Achievements_OnUnlockAchievementsCompleteCallback CompletionDelegate){
    // UE5.4+ OSSv2: capture handle if not yet captured (no userId available here)
    if (Handle) {
        Util::TryCaptureFallbackHandles(Handle, nullptr);
        AchievementManager::TryInitFromFallback(ScreamAPI::thisDLL);
    }
    static auto original = ScreamAPI::proxyFunction(&EOS_Achievements_UnlockAchievements, "EOS_Achievements_UnlockAchievements");
    return Intercept::Achievements_UnlockAchievements(original, Handle, Options, ClientData, CompletionDelegate);
}

EOS_DECLARE_FUNC(uint32_t) EOS_Achievements_GetAchievementDefinitionCount(EOS_HAchievements Handle, const EOS_Achievements_GetAchievementDefinitionCountOptions* Options){
    static auto original = ScreamAPI::proxyFunction(&EOS_Achievements_GetAchievementDefinitionCount, "EOS_Achievements_GetAchievementDefinitionCount");
    return Intercept::Achievements_GetAchievementDefinitionCount(original, Handle, Options);
}

/* Standard Proxy Implementations */

// Achievement Definitions

EOS_DECLARE_FUNC(void) EOS_Achievements_QueryDefinitions(EOS_HAchievements Handle, const EOS_Achievements_QueryDefinitionsOptions* Options, void* ClientData, const EOS_Achievements_OnQueryDefinitionsCompleteCallback CompletionDelegate){
    // UE5.4+ OSSv2: capture handle + userId from game's call parameters
    Util::TryCaptureFallbackHandles(Handle, Options ? Options->LocalUserId : nullptr);
    // If the polling thread timed out, trigger achievement manager init now
    AchievementManager::TryInitFromFallback(ScreamAPI::thisDLL);
    static auto original = ScreamAPI::proxyFunction(&EOS_Achievements_QueryDefinitions, "EOS_Achievements_QueryDefinitions");
    Intercept::Achievements_QueryDefinitions(original, Handle, Options, ClientData, CompletionDelegate);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Achievements_CopyAchievementDefinitionV2ByIndex(EOS_HAchievements Handle, const EOS_Achievements_CopyAchievementDefinitionV2ByIndexOptions* Options, EOS_Achievements_DefinitionV2** OutDefinition){
    EOS_IMPLEMENT_FUNC(EOS_Achievements_CopyAchievementDefinitionV2ByIndex, Handle, Options, OutDefinition);
}

EOS_DECLARE_FUNC(void) EOS_Achievements_DefinitionV2_Release(EOS_Achievements_DefinitionV2* AchievementDefinition){
    EOS_IMPLEMENT_FUNC(EOS_Achievements_DefinitionV2_Release, AchievementDefinition)
}

// Player Achievements

EOS_DECLARE_FUNC(void) EOS_Achievements_QueryPlayerAchievements(EOS_HAchievements Handle, const EOS_Achievements_QueryPlayerAchievementsOptions* Options, void* ClientData, const EOS_Achievements_OnQueryPlayerAchievementsCompleteCallback CompletionDelegate){
    // UE5.4+ OSSv2: capture handle + userId from game's call parameters
    Util::TryCaptureFallbackHandles(Handle, Options ? Options->LocalUserId : nullptr);
    // If the polling thread timed out, trigger achievement manager init now
    AchievementManager::TryInitFromFallback(ScreamAPI::thisDLL);
    static auto original = ScreamAPI::proxyFunction(&EOS_Achievements_QueryPlayerAchievements, "EOS_Achievements_QueryPlayerAchievements");
    Intercept::Achievements_QueryPlayerAchievements(original, Handle, Options, ClientData, CompletionDelegate);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Achievements_CopyPlayerAchievementByIndex(EOS_HAchievements Handle, const EOS_Achievements_CopyPlayerAchievementByIndexOptions* Options, EOS_Achievements_PlayerAchievement** OutAchievement){
    EOS_IMPLEMENT_FUNC(EOS_Achievements_CopyPlayerAchievementByIndex, Handle, Options, OutAchievement);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Achievements_CopyPlayerAchievementByAchievementId(EOS_HAchievements Handle, const EOS_Achievements_CopyPlayerAchievementByAchievementIdOptions* Options, EOS_Achievements_PlayerAchievement** OutAchievement){
    EOS_IMPLEMENT_FUNC(EOS_Achievements_CopyPlayerAchievementByAchievementId, Handle, Options, OutAchievement);
}

EOS_DECLARE_FUNC(void) EOS_Achievements_PlayerAchievement_Release(EOS_Achievements_PlayerAchievement* Achievement){
    EOS_IMPLEMENT_FUNC(EOS_Achievements_PlayerAchievement_Release, Achievement);
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_Achievements_AddNotifyAchievementsUnlockedV2(EOS_HAchievements Handle, const EOS_Achievements_AddNotifyAchievementsUnlockedV2Options* Options, void* ClientData, const EOS_Achievements_OnAchievementsUnlockedCallbackV2 NotificationFn){
    static auto original = ScreamAPI::proxyFunction(&EOS_Achievements_AddNotifyAchievementsUnlockedV2, "EOS_Achievements_AddNotifyAchievementsUnlockedV2");
    return Intercept::Achievements_AddNotifyAchievementsUnlockedV2(original, Handle, Options, ClientData, NotificationFn);
}

// Deprecated Functions

EOS_DECLARE_FUNC(EOS_EResult) EOS_Achievements_CopyAchievementDefinitionByIndex(EOS_HAchievements Handle, const EOS_Achievements_CopyAchievementDefinitionByIndexOptions* Options, EOS_Achievements_Definition** OutDefinition){
    EOS_IMPLEMENT_FUNC(EOS_Achievements_CopyAchievementDefinitionByIndex, Handle, Options, OutDefinition);
}

EOS_DECLARE_FUNC(void) EOS_Achievements_Definition_Release(EOS_Achievements_Definition* AchievementDefinition){
    EOS_IMPLEMENT_FUNC(EOS_Achievements_Definition_Release, AchievementDefinition);
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_Achievements_AddNotifyAchievementsUnlocked(EOS_HAchievements Handle, const EOS_Achievements_AddNotifyAchievementsUnlockedOptions* Options, void* ClientData, const EOS_Achievements_OnAchievementsUnlockedCallback NotificationFn){
    static auto original = ScreamAPI::proxyFunction(&EOS_Achievements_AddNotifyAchievementsUnlocked, "EOS_Achievements_AddNotifyAchievementsUnlocked");
    return Intercept::Achievements_AddNotifyAchievementsUnlocked(original, Handle, Options, ClientData, NotificationFn);
}
