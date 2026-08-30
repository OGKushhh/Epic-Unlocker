#pragma once
#include "pch.h"
#include <map>

namespace Config{

void init(const std::wstring iniPath);

// ScreamAPI
bool EnableOwnershipUnlocker();   // renamed from EnableItemUnlocker
bool EnableEntitlementUnlocker();
bool EnableLogging();
bool EnableOverlay();
bool ForceAchievementsConfig();
bool EnableKeyboardNavigation();
bool BlockMetrics();
// Logging
bool EnableLogging();
bool EnableSDKLog();            // A1: capture EOS SDK log to ScreamAPI_SDK.log (default: false)
std::string SDKLogLevel();      // A1: SDK log verbosity: Off|Fatal|Error|Warning|Info|Verbose|VeryVerbose (default: Warning)
std::string LogLevel();
std::string LogFilename();
bool LogDLCQueries();
bool LogAchievementQueries();
bool LogOverlay();
// Overlay
bool LoadIcons();
bool CacheIcons();
bool ValidateIcons();
bool ForceEpicOverlay();
bool EnableDX12Hook();
// DLC
bool UnlockAllDLC();
bool ForceSuccess();
// EAC (Easy Anti-Cheat) mode
bool EACMode();         // Master switch for EAC-specific patches:
                        //   game-handle capture, piggyback, fallbacks, etc.
                        // Defaults to false — non-EAC games are unaffected.
bool EACNoServerMode(); // When true, Ecom hooks skip the real SDK and return
                        // fake results locally (no server roundtrip). Set under [EAC].
// DLC_List (legacy explicit ID list)
std::vector<std::string> DLC_List();

// Per-item unlock decision. Combines legacy flags with the new
// [DLC_Override] per-ID status map.
// original_unlocked - the value returned by the EOS server (for ownership),
//                     or true if the game queried the ID (for entitlements).
bool IsDlcUnlocked(const std::string& id, bool original_unlocked);

// Extra entitlements to inject via the Entitlements API.
// Set via [Extra_Entitlements] in config. Key = id, Value = title.
std::map<std::string, std::string> ExtraEntitlements();

// Manual namespace_id override. Set via [ScreamAPI] NamespaceId=
// Useful when ScreamAPI loads after EOS_Platform_Create fires.
std::string NamespaceId();

// Custom EOS SDK path
std::string GetCustomEOSPath();

};
