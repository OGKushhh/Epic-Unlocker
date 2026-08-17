#include "pch.h"
#include "ScreamAPI.h"
#include "constants.h"
#include "util.h"
#include <Overlay.h>
#include "achievement_manager.h"
#include "eos_compat.h"
#include "eos_hooks.h"
#include "PipeServer.h"
#include "eos-sdk/eos_init.h"
#include "eos-sdk/eos_types.h"
#include <vector>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

using namespace Util;

// ------------------------------------------------------------------
// Recursively search for the EOS SDK DLL starting from a root folder
// ------------------------------------------------------------------
static HMODULE FindEOSSDKRecursive(const fs::path& root, const std::wstring& dllName, int maxDepth = 10) {
    if (!fs::exists(root) || !fs::is_directory(root))
        return nullptr;

    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); ++it) {
        if (it.depth() > maxDepth)
            continue;

        const auto& entry = *it;
        if (!entry.is_regular_file())
            continue;

        if (entry.path().filename() == dllName) {
            HMODULE h = LoadLibrary(entry.path().c_str());
            if (h) {
                Logger::info("Found original EOS SDK in: %ls", entry.path().c_str());
                return h;
            }
        }
    }
    return nullptr;
}

// Forward declaration: SetSDKLogPath is defined in eos_hooks.cpp at
// global scope. Declared here (outside namespace ScreamAPI) so callers
// inside the namespace resolve to the global symbol, not a phantom
// ScreamAPI::SetSDKLogPath (which is what caused LNK2001).
void SetSDKLogPath(const std::wstring& path);

namespace ScreamAPI
{
    HMODULE thisDLL = nullptr;
    HMODULE originalDLL = nullptr;
    bool isScreamAPIinitialized = false;

    void init(HMODULE hModule) {
        if (isScreamAPIinitialized)
            return;

        isScreamAPIinitialized = true;
        thisDLL = hModule;

        const auto iniPath = getDLLparentDir(hModule) / SCREAM_API_CONFIG;
        Config::init(iniPath.generic_wstring());

        const auto logPath = getDLLparentDir(hModule) / Config::LogFilename();
        Logger::init(Config::EnableLogging(),
                     Config::LogDLCQueries(),
                     Config::LogAchievementQueries(),
                     Config::LogOverlay(),
                     Config::LogLevel(),
                     logPath.generic_wstring());
        PipeServer::SetLogPath(logPath.generic_wstring());

        // A1: SDK log path = same dir as ScreamAPI.log, with _SDK suffix.
        // The EOS SDK's own log stream is routed here via EOS_Logging_SetCallback
        // (registered in the EOS_Platform_Create hook). Kept separate from
        // ScreamAPI.log so the DLC log parser doesn't see SDK noise, and so
        // users can open it in their preferred editor without drowning out
        // ScreamAPI's curated output.
        auto sdkLogPath = logPath;
        sdkLogPath.replace_filename(L"ScreamAPI_SDK.log");
        // SetSDKLogPath is forward-declared at file scope (above the
        // namespace ScreamAPI block) so the linker resolves it to the
        // global symbol defined in eos_hooks.cpp, not a phantom
        // ScreamAPI::SetSDKLogPath (which is what caused LNK2001).
        SetSDKLogPath(sdkLogPath.generic_wstring());
        Logger::info("[SDKLOG] EOS SDK log will be written to: %ls", sdkLogPath.c_str());

        Logger::info("Epic Unlocker v" SCREAM_API_VERSION);

        // ── Static SDK capability check (runs once at DLL load) ───────
        // Logs the EOS SDK header version this DLL was compiled against,
        // and whether stat-gated achievements are supported at the API level.
        // This is necessary-but-not-sufficient: a runtime null return from
        // EOS_Platform_GetStatsInterface means the game disabled Stats in its
        // EOS config (see the runtime hook in eos_hooks.cpp).
        Logger::info("EOS SDK (headers): v%d.%d.%d.%d",
                     EOS_MAJOR_VERSION, EOS_MINOR_VERSION,
                     EOS_PATCH_VERSION, EOS_HOTFIX_VERSION);
#if defined(EOS_ACHIEVEMENTS_STATTHRESHOLDS_API_LATEST)
        Logger::info("Stat-gated achievements: SUPPORTED (SDK headers expose "
                     "EOS_Achievements_StatThresholds, API v%d)",
                     EOS_ACHIEVEMENTS_STATTHRESHOLDS_API_LATEST);
#else
        Logger::warn("Stat-gated achievements: NOT SUPPORTED (SDK headers "
                     "predate EOS_Achievements_StatThresholds - upgrade SDK)");
#endif

        Logger::debug("DLL init function called");
        Logger::debug("EnableOverlay: %s", Config::EnableOverlay() ? "true" : "false");

        std::string origDllA(SCREAM_API_ORIG_DLL);
        std::wstring origDllW(origDllA.begin(), origDllA.end());

        // -----------------------------------------------------------------
        // 1. Try to get the already-loaded module (injection method)
        // -----------------------------------------------------------------
        HMODULE original = nullptr;
        for (int i = 0; i < 100; ++i) {
            original = GetModuleHandle(origDllW.c_str());
            if (original) {
                Logger::debug("Got handle to already-loaded EOS SDK after %d ms", i * 100);
                break;
            }
            Sleep(100);
        }

        // -----------------------------------------------------------------
        // 2. Try to load from current directory
        // -----------------------------------------------------------------
        if (!original) {
            Logger::debug("GetModuleHandle failed, trying LoadLibrary from current directory");
            const auto originalDllPath = getDLLparentDir(hModule) / origDllW;
            original = LoadLibrary(originalDllPath.c_str());
        }

        // -----------------------------------------------------------------
        // 3. Recursive search (depth 10)
        // -----------------------------------------------------------------
        if (!original) {
            Logger::debug("Searching recursively for %ls in game directory...", origDllW.c_str());
            const auto gameDir = getDLLparentDir(hModule);
            original = FindEOSSDKRecursive(gameDir, origDllW, 10);
        }

        // -----------------------------------------------------------------
        // 4. Custom path from config file
        // -----------------------------------------------------------------
        if (!original) {
            std::string customPathA = Config::GetCustomEOSPath();
            if (!customPathA.empty()) {
                std::wstring customPathW(customPathA.begin(), customPathA.end());
                if (fs::exists(customPathW)) {
                    original = LoadLibrary(customPathW.c_str());
                    if (original) {
                        Logger::info("Loaded original EOS SDK from custom path: %s", customPathA.c_str());
                    } else {
                        Logger::error("Failed to load from custom path: %s", customPathA.c_str());
                    }
                } else {
                    Logger::warn("Custom EOS path does not exist: %s", customPathA.c_str());
                }
            }
        }

        // -----------------------------------------------------------------
        // 5. Hardcoded subfolder fallback (common engine paths only)
        // -----------------------------------------------------------------
        if (!original) {
            const std::vector<std::wstring> subfolders = {
                L"Engine/Binaries/ThirdParty/EOS/Win32/",
                L"Engine/Binaries/ThirdParty/EOS/Win64/",
                L"Binaries/Win32/",
                L"Binaries/Win64/",
            };
            for (const auto& sub : subfolders) {
                auto path = getDLLparentDir(hModule) / sub / origDllW;
                HMODULE temp = LoadLibrary(path.c_str());
                if (temp) {
                    original = temp;
                    Logger::info("Found original EOS SDK in: %ls", path.c_str());
                    break;
                }
            }
        }

        if (original) {
            originalDLL = original;
            Logger::info("Successfully obtained original EOS SDK: %s", SCREAM_API_ORIG_DLL);

            if (EOS_Compat::detectSDKVersion((HMODULE)originalDLL)) {
                EOS_Compat::logCompatibilityInfo();
            } else {
                Logger::error("Failed to detect game's EOS SDK version");
            }

            Logger::debug("Calling EOS_Hooks::InitializeHooks...");
            if (EOS_Hooks::InitializeHooks((HMODULE)originalDLL)) {
                Logger::info("MinHook hooking system initialized - all EOS functions hooked");
            } else {
                Logger::error("Failed to initialize MinHook hooking system!");
            }
        } else {
            Logger::error("Failed to locate original EOS SDK: %s", SCREAM_API_ORIG_DLL);
            Logger::error("Make sure the game has loaded the EOS SDK (or use proxy method)");
        }

        Logger::debug("DLL init complete");
        Logger::info("Waiting for game to create EOS Platform via EOS_Platform_Create hook");

        // Achievement manager init runs UNCONDITIONALLY. Overlay is opt-in
        // via Config::EnableOverlay(). This decouples achievements from the
        // overlay so that unlock requests, PipeServer notifications, and EOS
        // notification subscription all work with the overlay disabled.
        //
        // Use a detached thread instead of std::async: std::future blocks in its
        // destructor until the task completes, and a static local future would block
        // at DLL unload — after hooks are already torn down. A detached thread exits
        // freely without blocking destroy().
        Logger::debug("Scheduling achievement manager initialization with platform polling");
        std::thread([hModule]() {
            const int MAX_WAIT_SECONDS = 60;
            const int POLL_INTERVAL_MS = 1000;
            int elapsedSeconds = 0;

            Logger::info("Waiting for game to initialize EOS platform...");

            while (elapsedSeconds < MAX_WAIT_SECONDS) {
                Sleep(POLL_INTERVAL_MS);
                elapsedSeconds++;

                if (Util::isEOSPlatformReady()) {
                    Logger::info("EOS Platform detected as ready after %d seconds", elapsedSeconds);
                    if (Config::EnableOverlay()) {
                        Logger::info("Initializing overlay + achievement manager");
                        AchievementManager::initWithOverlay(hModule);
                    } else {
                        Logger::info("Initializing achievement manager (overlay disabled)");
                        AchievementManager::init();
                    }
                    return;
                }

                if (elapsedSeconds % 10 == 0) {
                    Logger::debug("Still waiting for EOS platform... (%d/%d seconds)",
                                  elapsedSeconds, MAX_WAIT_SECONDS);
                }
            }

            Logger::error("Timed out waiting for EOS platform after %d seconds", MAX_WAIT_SECONDS);
            Logger::error("Achievements will not be available");
            Logger::warn("The game may not use EOS Achievements or requires manual initialization");
        }).detach();
    }

    void destroy() {
        Logger::info("Game requested to shutdown the EOS SDK");

        PipeServer::Stop();
        EOS_Hooks::ShutdownHooks();

        if (Config::EnableOverlay()) {
            Logger::info("Shutting down overlay");
            Overlay::Shutdown();
            Logger::info("Overlay shutdown complete");
        }
        Logger::info("Freeing original DLL");
        FreeLibrary(originalDLL);
        Logger::flush();
        isScreamAPIinitialized = false;
    }
}