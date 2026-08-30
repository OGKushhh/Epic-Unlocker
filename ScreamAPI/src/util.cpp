#include "pch.h"
#include "util.h"
#include "ScreamAPI.h"
#include "eos-sdk/eos_auth.h"
#include "eos-sdk/eos_stats.h"
#include "Logger.h"
#include "Config.h"

namespace Util{

EOS_HPlatform hPlatform = nullptr;

std::string g_namespace_id;
std::string g_product_id;

// -- UE5.4+ OSSv2 Fallback Handles --------------------------------------
std::atomic<EOS_HAchievements>  g_fallback_hAchievements{nullptr};
std::atomic<EOS_ProductUserId>  g_fallback_productUserId{nullptr};
std::atomic<EOS_EpicAccountId>  g_fallback_epicAccountId{nullptr};
static std::atomic<bool> g_fallback_captured{false};

// Externs: defined in eos_intercept.cpp, used here
extern std::atomic<EOS_HPlatform> g_loginPlatform;
std::atomic<EOS_HAchievements> g_gameHAchievements{nullptr};
std::atomic<EOS_HStats> g_gameHStats{nullptr};

void TryCaptureFallbackHandles(EOS_HAchievements hAch, EOS_ProductUserId userId) {
    if (!hAch) return;
    if (g_fallback_captured.exchange(true)) return; // already captured, lock-free

    g_fallback_hAchievements.store(hAch, std::memory_order_relaxed);
    if (userId) {
        g_fallback_productUserId.store(userId, std::memory_order_relaxed);
    }
    Logger::info("[UTIL] Captured fallback handles from game call (UE5.4+ OSSv2 path)");
    Logger::debug("[UTIL]   HAchievements: %p, ProductUserId: %p", (void*)hAch, (void*)userId);
}

bool HasFallbackHandles() {
    return g_fallback_captured.load(std::memory_order_relaxed)
        && g_fallback_hAchievements.load(std::memory_order_relaxed) != nullptr;
}

std::filesystem::path getDLLparentDir(HMODULE hModule){
        WCHAR modulePathBuffer[MAX_PATH];
        GetModuleFileName(hModule, modulePathBuffer, MAX_PATH);

        std::filesystem::path modulePath = modulePathBuffer;
        return modulePath.parent_path();
}

EOS_HPlatform getHPlatform(){
        return hPlatform;
}

EOS_HAuth getHAuth(){
        // Don't cache - platform might be created after first call
        auto result = EOS_Platform_GetAuthInterface(getHPlatform());
        if(result == nullptr) {
                Logger::debug("[UTIL] getHAuth: Returned NULL");
        }
        return result;
}

EOS_HConnect getHConnect(){
        // Don't cache - platform might be created after first call
        auto result = EOS_Platform_GetConnectInterface(getHPlatform());
        if(result == nullptr) {
                Logger::debug("[UTIL] getHConnect: Returned NULL");
        }
        return result;
}

EOS_HAchievements getGameHAchievements(){
        return g_gameHAchievements.load(std::memory_order_relaxed);
}

EOS_HAchievements getHAchievements(){
        // EAC mode: prefer the handle captured from the game's own call (correct auth session).
        // In non-EAC mode, ScreamAPI's captured HPlatform is the right one and the
        // game-handle capture is a no-op (g_gameHAchievements stays null).
        if (Config::EACMode()) {
            auto gameHandle = g_gameHAchievements.load(std::memory_order_relaxed);
            if(gameHandle) { Logger::debug("[UTIL] getHAchievements: using GAME handle %p (EAC mode)", (void*)gameHandle); return gameHandle; }
            Logger::debug("[UTIL] getHAchievements: game handle is NULL (EAC mode), falling through to platform");
        }

        // Normal path: derive from platform handle
        auto platform = getHPlatform();
        if (platform) {
                auto result = EOS_Platform_GetAchievementsInterface(platform);
                if (result) return result;
        }

        // UE5.4+ OSSv2 fallback: use handle captured from game's EOS_Achievements_* call
        auto fallback = g_fallback_hAchievements.load(std::memory_order_relaxed);
        if (fallback) {
                Logger::debug("[UTIL] getHAchievements: using fallback handle %p (OSSv2 path)", (void*)fallback);
                return fallback;
        }

        Logger::debug("[UTIL] getHAchievements: Returned NULL");
        return nullptr;
}

EOS_HStats getHStats(){
        // EAC mode: prefer the handle captured from the game's own call (correct auth session).
        if (Config::EACMode()) {
            auto gameStats = g_gameHStats.load(std::memory_order_relaxed);
            if(gameStats) { Logger::debug("[UTIL] getHStats: using GAME handle %p (EAC mode)", (void*)gameStats); return gameStats; }
        }

        // Don't cache - platform might be created after first call
        auto platform = getHPlatform();
        if(platform == nullptr) {
                Logger::debug("[UTIL] getHStats: Platform is NULL");
                return nullptr;
        }
        auto result = EOS_Platform_GetStatsInterface(platform);
        if(result == nullptr) {
                Logger::debug("[UTIL] getHStats: Returned NULL (Platform=%p)", platform);
        } else {
                Logger::debug("[UTIL] getHStats: Success (Handle=%p)", result);
        }
        return result;
}

bool isEOSPlatformReady(){
        // Normal path: platform handle + achievements interface
        auto platform = getHPlatform();
        if(platform != nullptr) {
                auto hAchievements = EOS_Platform_GetAchievementsInterface(platform);
                return (hAchievements != nullptr);
        }

        // EAC mode / UE5.4+ OSSv2 fallback: check if we captured handles from game calls.
        // The fallback ProductUserId is only meaningful when EACMode is on (game-handle capture).
        if (Config::EACMode() || HasFallbackHandles()) {
            return HasFallbackHandles();
        }
        return false;
}

void ResetFallbackCapture(){
    g_fallback_captured.store(false, std::memory_order_relaxed);
}

void logPlatformStatus(){
        auto platform = getHPlatform();
        
        Logger::debug("[UTIL] ========== EOS Platform Status ==========");
        Logger::debug("[UTIL] Platform Handle:     %p", platform);
        
        if(platform == nullptr) {
                // Check if we're on the OSSv2 fallback path
                if (HasFallbackHandles()) {
                        Logger::info("[UTIL] Platform is NULL but fallback handles are available (UE5.4+ OSSv2)");
                        Logger::debug("[UTIL] Fallback HAchievements: %p",
                                (void*)g_fallback_hAchievements.load(std::memory_order_relaxed));
                        Logger::debug("[UTIL] Fallback ProductUserId: %p",
                                (void*)g_fallback_productUserId.load(std::memory_order_relaxed));
                } else {
                        Logger::error("[UTIL] Platform is NULL - cannot check interfaces");
                }
                Logger::debug("[UTIL] ==========================================");
                return;
        }
        
        auto hConnect = EOS_Platform_GetConnectInterface(platform);
        auto hAuth = EOS_Platform_GetAuthInterface(platform);
        auto hAchievements = EOS_Platform_GetAchievementsInterface(platform);
        
        Logger::debug("[UTIL] Connect Interface:   %p %s", hConnect, hConnect ? "OK" : "NULL");
        Logger::debug("[UTIL] Auth Interface:      %p %s", hAuth, hAuth ? "OK" : "NULL");
        Logger::debug("[UTIL] Achievements Int:    %p %s", hAchievements, hAchievements ? "OK" : "NULL");
        
        auto productUserId = getProductUserId();
        auto epicAccountId = getEpicAccountId();
        
        Logger::debug("[UTIL] Product User ID:     %p %s", productUserId, productUserId ? "OK" : "NULL");
        Logger::debug("[UTIL] Epic Account ID:     %p %s", epicAccountId, epicAccountId ? "OK" : "NULL");
        Logger::debug("[UTIL] ==========================================");
}

EOS_EpicAccountId getEpicAccountId(){
        // Normal path: derive from platform
        auto hAuth = getHAuth();
        if(hAuth != nullptr) {
                auto result = EOS_Auth_GetLoggedInAccountByIndex(hAuth, 0);
                if(result != nullptr) {
                        Logger::debug("[UTIL] getEpicAccountId: Found account (Handle=%p)", result);
                        return result;
                }
        }

        // UE5.4+ OSSv2 fallback
        auto fallback = g_fallback_epicAccountId.load(std::memory_order_relaxed);
        if (fallback) {
                Logger::debug("[UTIL] getEpicAccountId: using fallback %p (OSSv2 path)", (void*)fallback);
                return fallback;
        }

        Logger::debug("[UTIL] getEpicAccountId: HAuth is NULL");
        return nullptr;
}

EOS_ProductUserId getProductUserId(){
        // Normal path: derive from platform
        auto hConnect = getHConnect();
        if(hConnect != nullptr) {
                auto result = EOS_Connect_GetLoggedInUserByIndex(hConnect, 0);
                if(result != nullptr) {
                        Logger::debug("[UTIL] getProductUserId: Found user (Handle=%p)", result);
                        return result;
                }
        }

        // UE5.4+ OSSv2 fallback
        auto fallback = g_fallback_productUserId.load(std::memory_order_relaxed);
        if (fallback) {
                Logger::debug("[UTIL] getProductUserId: using fallback %p (OSSv2 path)", (void*)fallback);
                return fallback;
        }

        Logger::debug("[UTIL] getProductUserId: HConnect is NULL");
        return nullptr;
}

/**
 * A small utility function that copies the c string into a newly allocated memory
 * @return Pointer to the new string
 */
char* copy_c_string(const char* c_string){
        // Get string size
        auto string_size = strlen(c_string) + 1;// +1 for null terminator

        // Allocate enough memory for the new string
        char* new_string = new char[string_size];

        // Copy the string contents
        strcpy_s(new_string, string_size, c_string);

        return new_string;
}

}
