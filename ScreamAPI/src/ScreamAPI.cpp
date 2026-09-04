#include "pch.h"
#include "ScreamAPI.h"
#include "constants.h"
#include "util.h"
#include <Overlay.h>
#include "achievement_manager.h"
#include "eos_compat.h"
#include "eos_hooks.h"
#include "eos_intercept.h"
#include "PipeServer.h"
#include "eos-sdk/eos_init.h"
#include "eos-sdk/eos_types.h"
#include "MinHook.h"
#include <vector>
#include <filesystem>
#include <string>
#include <atomic>
#include <condition_variable>

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

// == WaitForEOSLoad: lazy EOS SDK discovery ==
// Inspired by AchUnlocker (unknowncheats). When ScreamAPI starts in hook
// mode and cannot find the SDK at startup (Koaloader injection on a UE5.4+
// game that loads EOSSDK lazily), this module installs a LoadLibraryExW
// hook and waits for the game to load the SDK. When it loads, we initialize
// EOS_Compat detection and MinHook-based hooks against the freshly-loaded
// module. Bounded to a 30-second timeout so we do not pin a thread forever
// on games that never use EOS.
namespace WaitForEOSLoad {
    static std::atomic<HMODULE> g_pendingModule{nullptr};
    static std::atomic<bool> g_armed{false};
    static std::atomic<bool> g_fired{false};
    static HMODULE (WINAPI *g_origLoadLibraryExW)(LPCWSTR, HANDLE, DWORD) = nullptr;

    static HMODULE WINAPI HookedLoadLibraryExW(LPCWSTR lpLibFileName, HANDLE hFile, DWORD dwFlags) {
        HMODULE hLib = g_origLoadLibraryExW(lpLibFileName, hFile, dwFlags);
        if (hLib && lpLibFileName && !g_fired.load(std::memory_order_relaxed)) {
            LPCWSTR name = wcsrchr(lpLibFileName, L'\\');
            name = name ? name + 1 : lpLibFileName;
            if (_wcsicmp(name, L"EOSSDK-Win64-Shipping.dll") == 0 ||
                _wcsicmp(name, L"EOSSDK-Win32-Shipping.dll") == 0) {
                g_pendingModule.store(hLib, std::memory_order_release);
                Logger::info("[WAIT] LoadLibraryExW hook: game loaded %ls (handle=%p)", name, hLib);
            }
        }
        return hLib;
    }

    static void InstallLoadLibraryHook() {
        if (g_armed.exchange(true)) return;
        HMODULE hk32 = GetModuleHandleW(L"kernelbase.dll");
        if (!hk32) hk32 = GetModuleHandleW(L"kernel32.dll");
        if (!hk32) {
            Logger::error("[WAIT] Cannot find kernelbase/kernel32 to hook LoadLibraryExW");
            return;
        }
        void* target = GetProcAddress(hk32, "LoadLibraryExW");
        if (!target) {
            Logger::error("[WAIT] LoadLibraryExW not found in kernel module");
            return;
        }
        // Initialize MinHook early. EOS_Hooks::InitializeHooks() later
        // tolerates MH_ERROR_ALREADY_INITIALIZED, so this is safe.
        MH_STATUS mi = MH_Initialize();
        if (mi != MH_OK && mi != MH_ERROR_ALREADY_INITIALIZED) {
            Logger::error("[WAIT] MH_Initialize failed: %d", mi);
            return;
        }
        MH_STATUS s = MH_CreateHook(target, (void*)&HookedLoadLibraryExW, (void**)&g_origLoadLibraryExW);
        if (s != MH_OK) {
            Logger::error("[WAIT] MH_CreateHook(LoadLibraryExW) failed: %d", s);
            return;
        }
        s = MH_EnableHook(target);
        if (s != MH_OK) {
            Logger::error("[WAIT] MH_EnableHook(LoadLibraryExW) failed: %d", s);
            return;
        }
        Logger::info("[WAIT] LoadLibraryExW hook installed -- waiting for game to load EOSSDK");
    }

    // Spawns a detached polling thread. Returns when SDK loads or timeout.
    static void StartPollingAndInit() {
        InstallLoadLibraryHook();
        std::thread([]() {
            constexpr int POLL_MS = 250;
            constexpr int TIMEOUT_MS = 30000;
            int elapsed = 0;
            while (elapsed < TIMEOUT_MS) {
                HMODULE m = g_pendingModule.load(std::memory_order_acquire);
                if (!m) {
                    // Double-check via GetModuleHandle in case our hook missed
                    // a load that happened before installation.
                    m = GetModuleHandleW(L"EOSSDK-Win64-Shipping.dll");
                    if (!m) m = GetModuleHandleW(L"EOSSDK-Win32-Shipping.dll");
                    if (m) g_pendingModule.store(m, std::memory_order_release);
                }
                if (m) {
                    if (!g_fired.exchange(true)) {
                        Logger::info("[WAIT] EOS SDK loaded after %d ms -- initializing hooks", elapsed);
                        ScreamAPI::originalDLL = m;
                        if (EOS_Compat::detectSDKVersion(m)) {
                            EOS_Compat::logCompatibilityInfo();
                        }
                        // Capture achievement originals for ForceAchievementsConfiguration
                        auto qDefs = (Intercept::Achievements_QueryDefinitions_t)
                            EOS_Resolve::resolve(m, "EOS_Achievements_QueryDefinitions");
                        auto qPlayer = (Intercept::Achievements_QueryPlayerAchievements_t)
                            EOS_Resolve::resolve(m, "EOS_Achievements_QueryPlayerAchievements");
                        auto unlock = (Intercept::Achievements_UnlockAchievements_t)
                            EOS_Resolve::resolve(m, "EOS_Achievements_UnlockAchievements");
                        Intercept::SetAchievementsOriginals(qDefs, qPlayer, unlock);
                        if (EOS_Hooks::InitializeHooks(m)) {
                            Logger::info("[WAIT] Hooks installed successfully via lazy SDK discovery");
                        } else {
                            Logger::error("[WAIT] InitializeHooks failed on lazy-loaded SDK");
                        }
                    }
                    return;
                }
                Sleep(POLL_MS);
                elapsed += POLL_MS;
                if (elapsed % 5000 == 0) {
                    Logger::info("[WAIT] Still waiting for EOS SDK load (%d/%d ms)", elapsed, TIMEOUT_MS);
                }
            }
            Logger::error("[WAIT] Timed out after %d ms -- EOS SDK never loaded", TIMEOUT_MS);
            Logger::warn("[WAIT] Game may not use EOS, or uses a different DLL name. Check log for clues.");
        }).detach();
    }
} // namespace WaitForEOSLoad

namespace ScreamAPI
{
    HMODULE thisDLL = nullptr;
    HMODULE originalDLL = nullptr;
    bool isScreamAPIinitialized = false;
    bool isProxyMode = false;

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
                     logPath.generic_wstring(),
                     Config::AppendLog());
        PipeServer::SetLogPath(logPath.generic_wstring());

        // A1: SDK log path = same dir as ScreamAPI.log, with _SDK suffix.
        auto sdkLogPath = logPath;
        sdkLogPath.replace_filename(L"ScreamAPI_SDK.log");
        // TruncateSDKLog: opt-in to delete ScreamAPI_SDK.log on launch so the
        // SDK starts fresh. The SDK's own logger always appends, which can grow
        // unbounded across sessions; this gives users a clean-slate option.
        if (Config::TruncateSDKLog()) {
            std::error_code ec;
            std::filesystem::remove(sdkLogPath, ec);
            Logger::info("[SDKLOG] Truncated %ls on launch (TruncateSDKLog=true)",
                sdkLogPath.c_str());
        }
        // SetSDKLogPath is now in Intercept:: namespace
        Intercept::SetSDKLogPath(sdkLogPath.generic_wstring());
        Logger::info("[SDKLOG] EOS SDK log will be written to: %ls", sdkLogPath.c_str());

        Logger::info("Epic Unlocker v" SCREAM_API_VERSION);

        // -- Static SDK capability check (runs once at DLL load) -----
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
        // Detect proxy mode: if the original EOS DLL name is already loaded
        // and it's OUR DLL, we're in proxy mode (DLL rename method).
        // In proxy mode, the eos-impl/*.cpp forwarding handles interception
        // and MinHook is not needed.
        // -----------------------------------------------------------------
        HMODULE hExisting = GetModuleHandleA(SCREAM_API_ORIG_DLL);
        if (hExisting == thisDLL) {
            isProxyMode = true;
            Logger::info("Proxy mode detected - skipping MinHook initialization");
            Logger::info("EOS functions will be intercepted via DLL export forwarding (eos-impl/)");

            // Still need to find and store originalDLL for GetProcAddress lookups
            // In proxy mode, the original DLL has been renamed to _o.dll
            // Try to find it by common proxy naming conventions
            std::string proxyDllA = std::string(SCREAM_API_ORIG_DLL);
            // Strip .dll and append _o.dll
            size_t dotPos = proxyDllA.rfind('.');
            if (dotPos != std::string::npos) {
                proxyDllA = proxyDllA.substr(0, dotPos) + "_o.dll";
            } else {
                proxyDllA += "_o";
            }
            std::wstring proxyDllW(proxyDllA.begin(), proxyDllA.end());

            HMODULE original = nullptr;

            // Try to load the _o.dll from current directory
            const auto originalDllPath = getDLLparentDir(hModule) / proxyDllW;
            original = LoadLibrary(originalDllPath.c_str());

            if (!original) {
                // Try GetModuleHandle in case it's already loaded
                original = GetModuleHandle(proxyDllW.c_str());
            }

            if (!original) {
                // Recursive search
                original = FindEOSSDKRecursive(getDLLparentDir(hModule), proxyDllW, 10);
            }

            if (original) {
                originalDLL = original;
                Logger::info("Proxy mode: loaded original EOS SDK: %s", proxyDllA.c_str());

                if (EOS_Compat::detectSDKVersion((HMODULE)originalDLL)) {
                    EOS_Compat::logCompatibilityInfo();
                }
            } else {
                Logger::warn("Proxy mode: could not find original DLL (%s) - proxy forwarding may fail for some functions", proxyDllA.c_str());
            }

            // Store achievement originals for ForceAchievementsConfiguration (proxy mode)
            if (originalDLL) {
                auto qDefs = (Intercept::Achievements_QueryDefinitions_t)
                    EOS_Resolve::resolve((HMODULE)originalDLL, "EOS_Achievements_QueryDefinitions");
                auto qPlayer = (Intercept::Achievements_QueryPlayerAchievements_t)
                    EOS_Resolve::resolve((HMODULE)originalDLL, "EOS_Achievements_QueryPlayerAchievements");
                auto unlock = (Intercept::Achievements_UnlockAchievements_t)
                    EOS_Resolve::resolve((HMODULE)originalDLL, "EOS_Achievements_UnlockAchievements");
                Intercept::SetAchievementsOriginals(qDefs, qPlayer, unlock);
            }

        } else {
            // -- Hook mode -----------------------------------------------
            // Not-Proxy mode: use MinHook to intercept EOS SDK calls.

            // -----------------------------------------------------------------
            // 1. Try to get the already-loaded module (injection method)
            // -----------------------------------------------------------------
            // When WaitForEOSLoad=true, do a single quick check (we have a
            // 30-second polling fallback inside StartPollingAndInit).
            // When WaitForEOSLoad=false, keep the existing 10s poll.
            // -----------------------------------------------------------------
            const bool waitMode = Config::WaitForEOSLoad();
            HMODULE original = nullptr;
            if (waitMode) {
                original = GetModuleHandle(origDllW.c_str());
                if (original) {
                    Logger::info("Got handle to already-loaded EOS SDK (WaitForEOSLoad quick check)");
                }
            } else {
                for (int i = 0; i < 100; ++i) {
                    original = GetModuleHandle(origDllW.c_str());
                    if (original) {
                        Logger::debug("Got handle to already-loaded EOS SDK after %d ms", i * 100);
                        break;
                    }
                    Sleep(100);
                }
            }

            // -----------------------------------------------------------------
            // When WaitForEOSLoad=true, skip the aggressive LoadLibrary paths
            // (current dir, recursive search, custom path, hardcoded subfolders).
            // We want to wait for the GAME to load the SDK, not load it ourselves
            // -- otherwise the SDK is mapped before the game loader has set
            // up its internal state, which can prevent the fallback handle
            // capture from working correctly on UE5.4+ OSSv2 games.
            // -----------------------------------------------------------------
            if (!waitMode) {
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
            } // end if (!waitMode)

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
                if (Config::WaitForEOSLoad()) {
                    Logger::info("WaitForEOSLoad=true -- installing LoadLibraryExW hook and waiting up to 30s");
                    Logger::info("(typical of UE5.4+ games with Koaloader that load the SDK lazily)");
                    WaitForEOSLoad::StartPollingAndInit();
                } else {
                    Logger::error("Failed to locate original EOS SDK: %s", SCREAM_API_ORIG_DLL);
                    Logger::error("Make sure the game has loaded the EOS SDK (or use proxy method)");
                    Logger::warn("Tip: set WaitForEOSLoad=True in [ScreamAPI] if this game loads the SDK after init (e.g. UE5.4+ with Koaloader)");
                }
            }
        }

        Logger::debug("DLL init complete");
        Logger::info("Waiting for EOS Platform (via Platform_Create hook or Tick/GetInterface fallback)");

        // Achievement manager init runs UNCONDITIONALLY. Overlay is opt-in
        // via Config::EnableOverlay(). This decouples achievements from the
        // overlay so that unlock requests, PipeServer notifications, and EOS
        // notification subscription all work with the overlay disabled.
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

            // -- 60-second timeout reached ---------------------------------
            // For pre-UE5.4 games: EOS_Platform_Create was never called, game
            // probably doesn't use achievements. Give up.
            //
            // For UE5.4+ OSSv2 games: Platform was created internally by the
            // engine. Our intercept wrappers may have captured valid handles
            // from the game's EOS_Achievements_* calls. Check the fallback
            // path before giving up.

            if (Util::HasFallbackHandles()) {
                Logger::info("Platform polling timed out, but UE5.4+ OSSv2 fallback handles are available");
                Logger::info("Initializing achievement manager via fallback path");
                if (Config::EnableOverlay()) {
                    AchievementManager::initWithOverlay(hModule);
                } else {
                    AchievementManager::init();
                }
            } else {
                Logger::error("Timed out waiting for EOS platform after %d seconds", MAX_WAIT_SECONDS);
                Logger::error("Achievements will not be available");
                Logger::warn("The game may not use EOS Achievements or requires manual initialization");
            }
        }).detach();
    }

    void destroy() {
        Logger::info("Game requested to shutdown the EOS SDK");

        PipeServer::Stop();

        // Only shutdown hooks if we're in hook mode
        if (!isProxyMode) {
            EOS_Hooks::ShutdownHooks();
        }

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
