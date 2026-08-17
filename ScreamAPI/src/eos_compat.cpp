#include "pch.h"
#include "eos_compat.h"
#include "ScreamAPI.h"

namespace EOS_Compat {

SDKVersion gameSDKVersion;

// Fix #2: Helper to resolve a symbol from the EOS DLL, trying both undecorated
// and __stdcall decorated names. On 32-bit DLLs compiled with __stdcall,
// exports are decorated as _FunctionName@paramBytes. GetProcAddress with the
// undecorated name fails, causing detectSDKVersion to fall back to v1.13.0
// even when the game uses a newer SDK.
//
// paramBytes: total pushed parameter bytes (4 * number_of_params on 32-bit).
//             Ignored on 64-bit (names are never decorated).
static void* ResolveEOSExport(HMODULE eosDLL, const char* name, int paramBytes) {
    // Try undecorated name first (works on 64-bit and undecorated 32-bit DLLs)
    void* p = GetProcAddress(eosDLL, name);
    if (p) return p;

#ifndef _WIN64
    // 32-bit: try __stdcall decorated name _Name@paramBytes
    if (paramBytes >= 0) {
        char decorated[256];
        sprintf_s(decorated, "_%s@%d", name, paramBytes);
        p = GetProcAddress(eosDLL, decorated);
        if (p) {
            Logger::debug("[COMPAT] Resolved %s via decorated name %s", name, decorated);
            return p;
        }
    }
#endif
    return nullptr;
}

bool detectSDKVersion(HMODULE eosDLL) {
        if (!eosDLL) {
                Logger::error("[COMPAT] Cannot detect SDK version - DLL handle is NULL");
                return false;
        }

        // Method 1: Try to call EOS_GetVersion() if available
        // EOS_GetVersion takes 0 params -> @0 on 32-bit
        typedef const char* (EOS_CALL* EOS_GetVersion_Func)();
        auto getVersion = (EOS_GetVersion_Func)ResolveEOSExport(eosDLL, "EOS_GetVersion", 0);

        if (getVersion) {
                const char* versionStr = getVersion();
                Logger::info("[COMPAT] Game EOS SDK version (from DLL): %s", versionStr);

                // Parse version string "1.17.1.3" or "1.17.1.3-CL123456"
                int parsed = sscanf_s(versionStr, "%d.%d.%d.%d",
                        &gameSDKVersion.major,
                        &gameSDKVersion.minor,
                        &gameSDKVersion.patch,
                        &gameSDKVersion.hotfix);

                if (parsed >= 3) {
                        gameSDKVersion.detected = true;
                        Logger::info("[COMPAT] Parsed EOS SDK version: %d.%d.%d.%d",
                                gameSDKVersion.major,
                                gameSDKVersion.minor,
                                gameSDKVersion.patch,
                                gameSDKVersion.hotfix);
                        return true;
                }
        }

        // Method 2: Probe for version-specific functions
        // Fix #2: Use ResolveEOSExport with correct param byte counts instead of
        // raw GetProcAddress. On 32-bit __stdcall DLLs, exports are decorated
        // (e.g. _EOS_Connect_Logout@4) -> raw GetProcAddress fails -> fallback
        // to v1.13.0 even when the game uses v1.15.0+.
        Logger::warn("[COMPAT] EOS_GetVersion not available, probing for version-specific functions...");

        // v1.18.0+ has EOS_PresenceModification_SetTemplateId (Localized Presence)
        //   2 params: (EOS_HPresenceModification, const char*) -> @8 on 32-bit
        if (ResolveEOSExport(eosDLL, "EOS_PresenceModification_SetTemplateId", 8)) {
                Logger::info("[COMPAT] Found EOS_PresenceModification_SetTemplateId - SDK is v1.18.0+");
                gameSDKVersion.major = 1;
                gameSDKVersion.minor = 18;
                gameSDKVersion.patch = 0;
                gameSDKVersion.detected = true;
                return true;
        }

        // v1.17.0+ has EOS_Connect_CopyIdToken
        //   3 params: (Handle, Options*, OutIdToken**) -> @12 on 32-bit
        if (ResolveEOSExport(eosDLL, "EOS_Connect_CopyIdToken", 12)) {
                Logger::info("[COMPAT] Found EOS_Connect_CopyIdToken - SDK is v1.17.0+");
                gameSDKVersion.major = 1;
                gameSDKVersion.minor = 17;
                gameSDKVersion.patch = 0;
                gameSDKVersion.detected = true;
                return true;
        }

        // v1.16.0+ has EOS_Connect_Logout
        //   4 params: (Handle, Options*, ClientData, Callback) -> @16 on 32-bit
        if (ResolveEOSExport(eosDLL, "EOS_Connect_Logout", 16)) {
                Logger::info("[COMPAT] Found EOS_Connect_Logout - SDK is v1.16.0+");
                gameSDKVersion.major = 1;
                gameSDKVersion.minor = 16;
                gameSDKVersion.patch = 0;
                gameSDKVersion.detected = true;
                return true;
        }

        // v1.15.0+ has EOS_Platform_GetDesktopCrossplayStatus
        //   3 params: (Handle, Options*, OutStatusInfo*) -> @12 on 32-bit
        if (ResolveEOSExport(eosDLL, "EOS_Platform_GetDesktopCrossplayStatus", 12)) {
                Logger::info("[COMPAT] Found EOS_Platform_GetDesktopCrossplayStatus - SDK is v1.15.0+");
                gameSDKVersion.major = 1;
                gameSDKVersion.minor = 15;
                gameSDKVersion.patch = 0;
                gameSDKVersion.detected = true;
                return true;
        }

        // v1.14.0+ has EOS_Ecom_QueryOwnershipBySandboxIds
        //   4 params: (Handle, Options*, ClientData, Callback) -> @16 on 32-bit
        if (ResolveEOSExport(eosDLL, "EOS_Ecom_QueryOwnershipBySandboxIds", 16)) {
                Logger::info("[COMPAT] Found EOS_Ecom_QueryOwnershipBySandboxIds - SDK is v1.14.0+");
                gameSDKVersion.major = 1;
                gameSDKVersion.minor = 14;
                gameSDKVersion.patch = 0;
                gameSDKVersion.detected = true;
                return true;
        }

        // Assume v1.13.0 if no newer features found
        Logger::warn("[COMPAT] No version-specific functions found, assuming v1.13.0");
        Logger::warn("[COMPAT] (If the game uses SDK 1.14+, the 32-bit decorated name lookup may have failed.)");
        gameSDKVersion.major = 1;
        gameSDKVersion.minor = 13;
        gameSDKVersion.patch = 0;
        gameSDKVersion.detected = true;
        return true;
}

const char* getVersionString() {
        static char versionStr[64];
        if (gameSDKVersion.detected) {
                sprintf_s(versionStr, "v%d.%d.%d.%d",
                        gameSDKVersion.major,
                        gameSDKVersion.minor,
                        gameSDKVersion.patch,
                        gameSDKVersion.hotfix);
        }
        else {
                strcpy_s(versionStr, "unknown");
        }
        return versionStr;
}

bool isVersionOrNewer(int major, int minor, int patch) {
        if (!gameSDKVersion.detected) return false;

        if (gameSDKVersion.major > major) return true;
        if (gameSDKVersion.major < major) return false;

        if (gameSDKVersion.minor > minor) return true;
        if (gameSDKVersion.minor < minor) return false;

        if (gameSDKVersion.patch >= patch) return true;
        return false;
}

int getApiVersion(const char* apiName) {
        if (!gameSDKVersion.detected) {
                return -1;  // Use compile-time constant
        }

        // Platform Options evolved significantly
        if (strcmp(apiName, "PlatformOptions") == 0) {
                if (isVersionOrNewer(1, 17, 0)) return 14;  // v1.17.0+
                if (isVersionOrNewer(1, 16, 0)) return 13;  // v1.16.0
                if (isVersionOrNewer(1, 15, 0)) return 13;  // v1.15.0 added TickBudget
                if (isVersionOrNewer(1, 14, 0)) return 12;  // v1.14.0 added RTCOptions
                return 11;  // v1.13.0
        }

        return -1;  // Unknown API
}

bool isFeatureAvailable(const char* featureName) {
        if (!gameSDKVersion.detected) return false;

        // Connect Logout (v1.16.0+)
        if (strcmp(featureName, "ConnectLogout") == 0) {
                return isVersionOrNewer(1, 16, 0);
        }

        // Desktop Crossplay (v1.15.0+)
        if (strcmp(featureName, "DesktopCrossplay") == 0) {
                return isVersionOrNewer(1, 15, 0);
        }

        // External Auth Providers - Apple, Google, Oculus, itch.io (v1.14.0+)
        if (strcmp(featureName, "ExternalAuthProviders") == 0) {
                return isVersionOrNewer(1, 14, 0);
        }

        // Hidden Achievements (v1.15.0+)
        if (strcmp(featureName, "HiddenAchievements") == 0) {
                return isVersionOrNewer(1, 15, 0);
        }

        // QueryOwnershipBySandboxIds - newer DLC ownership check path (v1.14.0+)
        if (strcmp(featureName, "QueryOwnershipBySandboxIds") == 0) {
                return isVersionOrNewer(1, 14, 0);
        }

        // RTC Options in Platform (v1.14.0+)
        if (strcmp(featureName, "RTCOptions") == 0) {
                return isVersionOrNewer(1, 14, 0);
        }

        // Tick Budget (v1.15.0+)
        if (strcmp(featureName, "TickBudget") == 0) {
                return isVersionOrNewer(1, 15, 0);
        }

        // Integrated Platform (v1.17.0+)
        if (strcmp(featureName, "IntegratedPlatform") == 0) {
                return isVersionOrNewer(1, 17, 0);
        }

        // Task Network Timeout (v1.17.0+)
        if (strcmp(featureName, "TaskNetworkTimeout") == 0) {
                return isVersionOrNewer(1, 17, 0);
        }

        // Localized Presence (v1.18.0+)
        if (strcmp(featureName, "LocalizedPresence") == 0) {
                return isVersionOrNewer(1, 18, 0);
        }

        // V2 Achievement Notifications (v1.14.0+)
        // Fix #4: EOS_Achievements_AddNotifyAchievementsUnlockedV2 does not
        // exist in SDK 1.13.0. This feature flag gates hook installation.
        if (strcmp(featureName, "AchievementsUnlockedV2") == 0) {
                return isVersionOrNewer(1, 14, 0);
        }

        // CopyAchievementDefinitionV2 (v1.14.0+)
        if (strcmp(featureName, "CopyAchievementDefinitionV2") == 0) {
                return isVersionOrNewer(1, 14, 0);
        }

        return false;
}

void logCompatibilityInfo() {
        Logger::info("[COMPAT] ========================================");
        Logger::info("[COMPAT] EOS SDK Compatibility Information");
        Logger::info("[COMPAT] ========================================");
        Logger::info("[COMPAT] ScreamAPI SDK Version: v%d.%d.%d.%d (headers)",
                EOS_MAJOR_VERSION, EOS_MINOR_VERSION, EOS_PATCH_VERSION, EOS_HOTFIX_VERSION);
        Logger::info("[COMPAT] Game SDK Version:      %s", getVersionString());

        if (!gameSDKVersion.detected) {
                Logger::error("[COMPAT] Game SDK version not detected!");
                Logger::info("[COMPAT] ========================================");
                return;
        }

        // --- 32-bit + SDK 1.19+ deprecation warning (official EOS 1.19.0.3) ---
        // This warning only appears in 32-bit builds. 64-bit is fully supported.
#ifdef _M_IX86
        if (isVersionOrNewer(1, 19, 0)) {
                Logger::warn("[COMPAT] This game uses an EOS SDK version (≥1.19.0) that no longer supports 32-bit Windows.");
                Logger::warn("[COMPAT] Official deprecation: https://dev.epicgames.com/docs/epic-online-services/whats-new#2026");
                Logger::warn("[COMPAT] Achievements overlay may not work. Use EOS SDK ≤1.18.1.2 for 32-bit compatibility.");
        }
#endif

        Logger::info("[COMPAT] ");
        Logger::info("[COMPAT] Feature Availability:");
        Logger::info("[COMPAT]   Connect Logout:          %s", isFeatureAvailable("ConnectLogout") ? "YES" : "NO");
        Logger::info("[COMPAT]   Desktop Crossplay:       %s", isFeatureAvailable("DesktopCrossplay") ? "YES" : "NO");
        Logger::info("[COMPAT]   External Auth Providers: %s", isFeatureAvailable("ExternalAuthProviders") ? "YES" : "NO");
        Logger::info("[COMPAT]   Hidden Achievements:     %s", isFeatureAvailable("HiddenAchievements") ? "YES" : "NO");
        Logger::info("[COMPAT]   RTC Options:             %s", isFeatureAvailable("RTCOptions") ? "YES" : "NO");
        Logger::info("[COMPAT]   Tick Budget:             %s", isFeatureAvailable("TickBudget") ? "YES" : "NO");
        Logger::info("[COMPAT]   Integrated Platform:     %s", isFeatureAvailable("IntegratedPlatform") ? "YES" : "NO");
        Logger::info("[COMPAT]   Task Network Timeout:    %s", isFeatureAvailable("TaskNetworkTimeout") ? "YES" : "NO");
        Logger::info("[COMPAT]   Localized Presence:      %s", isFeatureAvailable("LocalizedPresence") ? "YES" : "NO");
        Logger::info("[COMPAT]   OwnershipBySandboxIds:   %s", isFeatureAvailable("QueryOwnershipBySandboxIds") ? "YES" : "NO");
        Logger::info("[COMPAT]   AchievementsUnlockedV2:  %s", isFeatureAvailable("AchievementsUnlockedV2") ? "YES" : "NO");
        Logger::info("[COMPAT]   CopyDefinitionV2:        %s", isFeatureAvailable("CopyAchievementDefinitionV2") ? "YES" : "NO");

        Logger::info("[COMPAT] ");
        Logger::info("[COMPAT] API Versions:");
        Logger::info("[COMPAT]   PlatformOptions:         %d", getApiVersion("PlatformOptions"));

        // Compatibility status
        if (isVersionOrNewer(EOS_MAJOR_VERSION, EOS_MINOR_VERSION, EOS_PATCH_VERSION)) {
                Logger::info("[COMPAT] ");
                Logger::info("[COMPAT] Status: COMPATIBLE (Game >= ScreamAPI)");
        }
        else {
                Logger::warn("[COMPAT] ");
                Logger::warn("[COMPAT] Status: PARTIAL (Game < ScreamAPI)");
                Logger::warn("[COMPAT] Game uses older SDK - some ScreamAPI features unavailable");
        }

        Logger::info("[COMPAT] ========================================");
}

} // namespace EOS_Compat
