#include "pch.h"
#include "eos-sdk/eos_init.h"
#include "ScreamAPI.h"
#include "eos_intercept.h"

EOS_DECLARE_FUNC(EOS_EResult) EOS_Initialize(const EOS_InitializeOptions* Options) {
    Logger::info("[INTERCEPT] >>> EOS_Initialize CALLED! <<<");

    if (Options) {
        Logger::info("[INTERCEPT]   ApiVersion=%d, ProductName=%s, ProductVersion=%s",
            Options->ApiVersion,
            Options->ProductName ? Options->ProductName : "NULL",
            Options->ProductVersion ? Options->ProductVersion : "NULL");

        // Capture the game's custom memory allocator for use in entitlement/ownership data.
        // This ensures memory we allocate can be safely freed by the game (same heap).
        GameAlloc::CaptureFromOptions(Options);
        if (GameAlloc::HasAllocator()) {
            Logger::info("[INTERCEPT]   Game allocator captured: Alloc=%p, Free=%p, Realloc=%p",
                (void*)Options->AllocateMemoryFunction,
                (void*)Options->ReleaseMemoryFunction,
                (void*)Options->ReallocateMemoryFunction);
        } else {
            Logger::info("[INTERCEPT]   Game allocator: none provided (will use CRT fallback)");
        }
    } else {
        Logger::warn("[INTERCEPT] EOS_Initialize called with NULL Options!");
    }

    static auto original = ScreamAPI::proxyFunction(&EOS_Initialize, "EOS_Initialize");
    Logger::info("[INTERCEPT] EOS_Initialize - Forwarding to original _o.dll...");
    EOS_EResult result = original(Options);
    Logger::info("[INTERCEPT] EOS_Initialize - Returned: %d", static_cast<int>(result));
    return result;
}
