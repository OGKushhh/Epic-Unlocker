#pragma once
#include "pch.h"
#include "eos-sdk/eos_sdk.h"
#include "eos-sdk/eos_stats.h"
#include <atomic>

namespace Util{

template <typename T>
static bool vectorContains(std::vector<T> vector, T element){
        return std::find(vector.begin(), vector.end(), element) != vector.end();
}

std::filesystem::path getDLLparentDir(HMODULE hModule);

// Captured from EOS_Platform_Create -> Options->SandboxId.
// Used by DLC catalog auto-fetch. Empty until EOS_Platform_Create fires.
extern std::string g_namespace_id;
extern std::string g_product_id;

extern EOS_HPlatform hPlatform;


// -- UE5.4+ OSSv2 Fallback Handles --------------------------------------
// When UE5.4+ creates the EOS platform through OSSv2 internally,
// EOS_Platform_Create is never called and hPlatform stays nullptr.
// However, the game still calls EOS_Achievements_* functions with valid
// handles obtained through the internal path. We capture those handles
// here so the AchievementManager can initialize lazily.
//
// These are set once (atomic exchange) from intercept wrappers and serve
// as fallbacks when the platform-derived getters return nullptr.
extern std::atomic<EOS_HAchievements>  g_fallback_hAchievements;
extern std::atomic<EOS_ProductUserId>  g_fallback_productUserId;
extern std::atomic<EOS_EpicAccountId>  g_fallback_epicAccountId;

// Called from intercept wrappers when a valid EOS_HAchievements and
// EOS_ProductUserId are available from the game's function call parameters.
// Captures once (atomic exchange). Thread-safe, no mutex, lock-free.
void TryCaptureFallbackHandles(EOS_HAchievements hAch, EOS_ProductUserId userId);

// Returns true if fallback handles have been captured (UE5.4+ OSSv2 path active).
bool HasFallbackHandles();

EOS_HPlatform getHPlatform();
EOS_HAuth getHAuth();
EOS_HConnect getHConnect();
EOS_HAchievements getHAchievements();
EOS_HStats getHStats();
EOS_EpicAccountId getEpicAccountId();
EOS_ProductUserId getProductUserId();
char* copy_c_string(const char* c_string);
bool isEOSPlatformReady();
void logPlatformStatus();
void ResetFallbackCapture();
EOS_HAchievements getGameHAchievements();
extern std::atomic<EOS_HAchievements> g_gameHAchievements;
extern std::atomic<EOS_HStats> g_gameHStats;

}
