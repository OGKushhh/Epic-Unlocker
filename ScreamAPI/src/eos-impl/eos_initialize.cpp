#include "pch.h"
#include "eos-sdk/eos_init.h"
#include "ScreamAPI.h"

EOS_DECLARE_FUNC(EOS_EResult) EOS_Initialize(const EOS_InitializeOptions* Options) {
    Logger::info("[INTERCEPT] >>> EOS_Initialize CALLED! <<<");

    if (Options) {
        Logger::info("[INTERCEPT]   ApiVersion=%d, ProductName=%s, ProductVersion=%s",
            Options->ApiVersion,
            Options->ProductName ? Options->ProductName : "NULL",
            Options->ProductVersion ? Options->ProductVersion : "NULL");
    } else {
        Logger::warn("[INTERCEPT] EOS_Initialize called with NULL Options!");
    }

    static auto original = ScreamAPI::proxyFunction(&EOS_Initialize, "EOS_Initialize");
    Logger::info("[INTERCEPT] EOS_Initialize - Forwarding to original _o.dll...");
    EOS_EResult result = original(Options);
    Logger::info("[INTERCEPT] EOS_Initialize - Returned: %d", static_cast<int>(result));
    return result;
}
