#include "pch.h"
#include "eos_compat.h"
#include "ScreamAPI.h"
#include "eos_resolve.h"

namespace EOS_Compat {

SDKVersion gameSDKVersion;

bool detectSDKVersion(HMODULE eosDLL) {
        if (!eosDLL) {
                Logger::error("[COMPAT] Cannot detect SDK version - DLL handle is NULL");
                return false;
        }

        // Method 1: Try to call EOS_GetVersion() if available
        // EOS_GetVersion takes 0 params -> @0 on 32-bit
        typedef const char* (EOS_CALL* EOS_GetVersion_Func)();
        auto getVersion = (EOS_GetVersion_Func)EOS_Resolve::resolve(eosDLL, "EOS_GetVersion");

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

        // Method 2: Probe for version-specific functions.
        // EOS_Resolve::resolve handles both 64-bit (bare name) and 32-bit
        // __stdcall (_Name@N decoration) transparently.
        Logger::warn("[COMPAT] EOS_GetVersion not available, probing for version-specific functions...");

        // =============================================================
        // Probe chain (highest version first). Each probe is verified
        // against the official EOS SDK release notes:
        //   https://dev.epicgames.com/docs/epic-online-services/release-notes
        // =============================================================

        // v1.19.0+ -- two new runtime markers (per v1.19.0.3 release notes:
        //   "New: Added a function EOS_AntiCheatClient_GetModuleBuildId"
        //   "New: Use EOS_PresenceModification_SetTemplateData ... Localized Presence V2")
        // SetTemplateId was declared in v1.18 headers but only shipped in v1.19
        // runtime, so it does NOT discriminate v1.18 from v1.19 -- use SetTemplateData
        // as the unambiguous v1.19 marker instead.
        if (EOS_Resolve::resolve(eosDLL, "EOS_PresenceModification_SetTemplateData")) {
                Logger::info("[COMPAT] Found EOS_PresenceModification_SetTemplateData - SDK is v1.19.0+");
                gameSDKVersion.major = 1;
                gameSDKVersion.minor = 19;
                gameSDKVersion.patch = 0;
                gameSDKVersion.detected = true;
                return true;
        }
        // Alternate v1.19 marker (Anti-Cheat client module build ID)
        if (EOS_Resolve::resolve(eosDLL, "EOS_AntiCheatClient_GetModuleBuildId")) {
                Logger::info("[COMPAT] Found EOS_AntiCheatClient_GetModuleBuildId - SDK is v1.19.0+");
                gameSDKVersion.major = 1;
                gameSDKVersion.minor = 19;
                gameSDKVersion.patch = 0;
                gameSDKVersion.detected = true;
                return true;
        }

        // v1.18.0+ -- Localized Presence (per v1.18.0.4 release notes:
        //   "Presence: New: Added support for localized presence.")
        // Note: EOS_PresenceModification_SetTemplateId was declared in v1.18 headers
        // and only shipped in the runtime DLL starting v1.19.0.0. This probe
        // fires for v1.18+ games that have the v1.18.0.4 (or later v1.18.x) runtime
        // DLL. The v1.19 probe above catches v1.19+ first, so this correctly
        // discriminates v1.18 from v1.19.
        if (EOS_Resolve::resolve(eosDLL, "EOS_PresenceModification_SetTemplateId")) {
                Logger::info("[COMPAT] Found EOS_PresenceModification_SetTemplateId - SDK is v1.18.0+");
                gameSDKVersion.major = 1;
                gameSDKVersion.minor = 18;
                gameSDKVersion.patch = 0;
                gameSDKVersion.detected = true;
                return true;
        }

        // v1.17.0+ -- Lobby RTC integration (per v1.17.0 release notes:
        //   "New methods on the Lobby API ... EOS_Lobby_LeaveRTCRoom and
        //   EOS_Lobby_JoinRTCRoom")
        // Note: previously used EOS_Connect_CopyIdToken as the v1.17 marker, but
        // that function was declared in v1.14 headers (per v1.14 release notes:
        //   "New: Added new APIs EOS_Connect_CopyIdToken ..."). The function
        // only shipped in the runtime DLL starting around v1.16.3.0 64-bit,
        // so it could not discriminate v1.17 from v1.16.3.
        if (EOS_Resolve::resolve(eosDLL, "EOS_Lobby_JoinRTCRoom")) {
                Logger::info("[COMPAT] Found EOS_Lobby_JoinRTCRoom - SDK is v1.17.0+");
                gameSDKVersion.major = 1;
                gameSDKVersion.minor = 17;
                gameSDKVersion.patch = 0;
                gameSDKVersion.detected = true;
                return true;
        }

        // v1.16.0+ -- two markers per v1.16 release notes:
        //   - "Ecom: New: Added EOS_Ecom_QueryOwnershipBySandboxIds"
        //   - "Connect: New: Added EOS_Connect_Logout" (per v1.16.2 release notes)
        // Try Connect_Logout first (added v1.16.0), then Ecom probe as fallback.
        if (EOS_Resolve::resolve(eosDLL, "EOS_Connect_Logout")) {
                Logger::info("[COMPAT] Found EOS_Connect_Logout - SDK is v1.16.0+");
                gameSDKVersion.major = 1;
                gameSDKVersion.minor = 16;
                gameSDKVersion.patch = 0;
                gameSDKVersion.detected = true;
                return true;
        }
        if (EOS_Resolve::resolve(eosDLL, "EOS_Ecom_QueryOwnershipBySandboxIds")) {
                Logger::info("[COMPAT] Found EOS_Ecom_QueryOwnershipBySandboxIds - SDK is v1.16.0+");
                gameSDKVersion.major = 1;
                gameSDKVersion.minor = 16;
                gameSDKVersion.patch = 0;
                gameSDKVersion.detected = true;
                return true;
        }

        // v1.15.0+ -- Desktop crossplay status (per v1.15 release notes)
        if (EOS_Resolve::resolve(eosDLL, "EOS_Platform_GetDesktopCrossplayStatus")) {
                Logger::info("[COMPAT] Found EOS_Platform_GetDesktopCrossplayStatus - SDK is v1.15.0+");
                gameSDKVersion.major = 1;
                gameSDKVersion.minor = 15;
                gameSDKVersion.patch = 0;
                gameSDKVersion.detected = true;
                return true;
        }

        // Fallback: assume v1.14.0 if no newer marker found.
        // (v1.14 added many Auth/Connect token APIs per the v1.14 release notes;
        //  the oldest probe above catches v1.15+, so the absence of all markers
        //  means the SDK predates v1.15 -- most likely v1.14.)
        Logger::warn("[COMPAT] No version-specific runtime functions found, assuming v1.14.0");
        Logger::warn("[COMPAT] (If the game uses SDK 1.15+, the 32-bit decorated name lookup may have failed.)");
        gameSDKVersion.major = 1;
        gameSDKVersion.minor = 14;
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

// Single source of truth for EOS feature -> SDK version mapping.
// Adding a new feature flag is now a one-line table edit instead of three
// scattered changes (isFeatureAvailable branch + logCompatibilityInfo line
// + caller spelling the string correctly).
struct FeatureEntry {
        const char* key;          // programmatic name (callers use this)
        const char* logLabel;     // human-readable name (for log output)
        int major, minor, patch;  // SDK version that introduced it
};

static const FeatureEntry kFeatures[] = {
        {"ConnectLogout",                "Connect Logout",          1, 16, 0},
        {"DesktopCrossplay",             "Desktop Crossplay",       1, 15, 0},
        {"ExternalAuthProviders",        "External Auth Providers", 1, 14, 0},
        {"HiddenAchievements",           "Hidden Achievements",     1, 15, 0},
        {"QueryOwnershipBySandboxIds",   "OwnershipBySandboxIds",   1, 16, 0},  // v1.16 release notes: "Ecom: New: Added EOS_Ecom_QueryOwnershipBySandboxIds"
        {"RTCOptions",                   "RTC Options",             1, 14, 0},
        {"TickBudget",                   "Tick Budget",             1, 15, 0},
        {"IntegratedPlatform",           "Integrated Platform",     1, 13, 0},  // v1.13 release notes (not v1.17 as previously claimed)
        {"TaskNetworkTimeout",           "Task Network Timeout",    1, 17, 0},
        {"LocalizedPresence",            "Localized Presence",      1, 18, 0},  // v1.18.0.4 release notes: "Presence: New: Added support for localized presence"
        {"LobbyRTC",                     "Lobby RTC Integration",   1, 17, 0},  // v1.17.0 release notes: "EOS_Lobby_JoinRTCRoom and EOS_Lobby_LeaveRTCRoom"
        {"OnScreenKeyboard",             "On-Screen Keyboard",      1, 19, 0},  // v1.19.0.3 release notes: "New: Added new EOS_UI APIs to configure on-screen keyboards"
        {"LocalizedPresenceV2",          "Localized Presence V2",   1, 19, 0},  // v1.19.0.3+ -- EOS_PresenceModification_SetTemplateData
        {"AntiCheatBuildId",             "Anticheat Build ID",      1, 19, 0},  // v1.19.0.3 release notes: "New: Added EOS_AntiCheatClient_GetModuleBuildId"
        // Fix #4: EOS_Achievements_AddNotifyAchievementsUnlockedV2 does not
        // exist in SDK 1.13.0. This feature flag gates hook installation.
        {"AchievementsUnlockedV2",       "AchievementsUnlockedV2",  1, 14, 0},
        {"CopyAchievementDefinitionV2",  "CopyDefinitionV2",        1, 14, 0},
};

bool isFeatureAvailable(const char* featureName) {
        if (!gameSDKVersion.detected) return false;
        for (const auto& f : kFeatures) {
                if (strcmp(featureName, f.key) == 0)
                        return isVersionOrNewer(f.major, f.minor, f.patch);
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
        for (const auto& f : kFeatures) {
                Logger::info("[COMPAT]   %-24s %s",
                        f.logLabel, isFeatureAvailable(f.key) ? "YES" : "NO");
        }

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
