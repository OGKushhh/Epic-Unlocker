#pragma once

#define WIN32_LEAN_AND_MEAN // Exclude not needed win api definitions
#include <Windows.h>

// ScreamAPI.h & ScreamAPI.cpp
#include <filesystem>
#include <functional>
#include <sstream>
#include <vector>
#include <string>
#include <thread>
#include <Logger.h>

// Ecom
#include "eos-sdk/eos_base.h"

// Include the linker exports based on the target architecture (32 vs 64)
// LegacyExports are special linker exports aimed to support older versions of EOS SDK
//
// Fix #1: Use compile-time SCREAMAPI_MIN_SDK_VERSION guard to select the correct
// export list. LinkerExports contains ALL known EOS functions (including those
// introduced in SDK 1.14+ like CopyAchievementDefinitionV2ByIndex). If the game
// ships with an older SDK DLL that doesn't export those symbols, the PE forwarder
// chain becomes a dangling reference -> crash on first call.
//
// SCREAMAPI_MIN_SDK_VERSION: encoded as MAJOR*10000 + MINOR*100 + PATCH
//   e.g. SDK 1.13.0 = 11300, SDK 1.14.0 = 11400, SDK 1.15.0 = 11500
//   Default: 0 (include all exports = current behavior for newer SDKs)
//
// Build examples:
//   cl /DSCREAMAPI_MIN_SDK_VERSION=11300 ...  -> legacy exports only (for 32-bit FO:NV)
//   cl /DSCREAMAPI_MIN_SDK_VERSION=11400 ...  -> exports up to 1.14
//   (undefined or 0)                          -> full LinkerExports (for modern SDK 1.16+)

#ifndef SCREAMAPI_MIN_SDK_VERSION
#define SCREAMAPI_MIN_SDK_VERSION 0  // Default: assume modern SDK, include all exports
#endif

#ifdef _WIN64
    #if SCREAMAPI_MIN_SDK_VERSION > 0 && SCREAMAPI_MIN_SDK_VERSION < 11400
        #include "LinkerExports/LegacyExports64.h"
    #else
        #include "LinkerExports/LinkerExports64.h"
        #include "LinkerExports/LegacyExports64.h"
    #endif
#else
    #if SCREAMAPI_MIN_SDK_VERSION > 0 && SCREAMAPI_MIN_SDK_VERSION < 11400
        #include "LinkerExports/LegacyExports32.h"
    #else
        #include "LinkerExports/LinkerExports32.h"
        #include "LinkerExports/LegacyExports32.h"
    #endif
#endif
