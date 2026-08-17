#pragma once
#include "pch.h"

// Shared EOS export resolver. Used by three callers that previously each
// had their own implementation:
//   - ScreamAPI::proxyFunction     (proxy-mode DLL forwarding, ScreamAPI.h)
//   - EOS_Compat::detectSDKVersion (version probing, eos_compat.cpp)
//   - eos_hooks.cpp INSTALL_HOOK   (hook installation)
//
// 64-bit: exports are undecorated, only bare name is tried.
// 32-bit __stdcall: exports are decorated as _Name@paramBytes. We walk
//   the standard decoration sizes used by the EOS SDK (4, 8, 12, 16, 20)
//   plus headroom up to 40 bytes for future SDK additions.
//
// RESOLUTION ORDER ON 32-BIT (IMPORTANT):
//   We try DECORATED names first, then fall back to the bare name.
//   This matches the behavior of the original GetDecoratedName() table
//   that shipped in v3/v4. The v5 refactor switched to "bare name first"
//   and that caused a regression on EOSSDK-Win32-Shipping.dll, which
//   exports BOTH forms: the decorated name is the real __stdcall entry
//   point the game's import table binds to, while the bare name is a
//   forwarder/alias entry that does NOT receive calls routed through
//   the decorated import. Hooking the bare-name entry made the hook
//   silently ineffective for achievement queries (EOS_InvalidAuth).
//   Restoring "decorated first" fixes this while keeping the
//   table-free walker.
//
// On success, logs which form resolved at debug level (so users can audit
// whether a specific _Name@N or the bare name worked). On failure,
// returns nullptr silently — the caller decides whether to log an error.
namespace EOS_Resolve {
    inline void* resolve(HMODULE module, const char* baseName) {
        if (!module || !baseName) return nullptr;

#ifdef _WIN64
        // 64-bit: exports are undecorated. Only the bare name exists.
        if (void* p = GetProcAddress(module, baseName)) {
            Logger::debug("[RESOLVE] %s via bare name -> %p", baseName, p);
            return p;
        }
        Logger::debug("[RESOLVE] %s not found (64-bit, no decoration tried)", baseName);
        return nullptr;
#else
        // 32-bit __stdcall: try DECORATED names first.
        // EOSSDK-Win32-Shipping.dll exports both _Name@N (real entry) and
        // the bare Name (forwarder/alias). The game's import table binds
        // to the decorated name, so we must hook the decorated entry for
        // MinHook to actually intercept calls. Trying the bare name first
        // (as v5 did) hooks the forwarder, which silently fails to
        // intercept achievement-related calls.
        static constexpr int kParamSizes[] = { 4, 8, 12, 16, 20, 24, 28, 32, 36, 40 };
        char decorated[256];
        for (int n : kParamSizes) {
            snprintf(decorated, sizeof(decorated), "_%s@%d", baseName, n);
            if (void* p = GetProcAddress(module, decorated)) {
                Logger::debug("[RESOLVE] %s via %s -> %p", baseName, decorated, p);
                return p;
            }
        }
        // Fall back to bare name (rare: some 32-bit SDKs export undecorated
        // names via .def file with no decorated alias).
        if (void* p = GetProcAddress(module, baseName)) {
            Logger::debug("[RESOLVE] %s via bare name (fallback) -> %p", baseName, p);
            return p;
        }
        Logger::debug("[RESOLVE] %s not found (tried _%s@{4..40} + bare)", baseName, baseName);
        return nullptr;
#endif
    }
}
