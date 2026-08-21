#include "pch.h"
#include "eos-sdk/eos_init.h"
#include "eos-sdk/eos_types.h"
#include "ScreamAPI.h"
#include "eos_intercept.h"

EOS_DECLARE_FUNC(EOS_HPlatform) EOS_Platform_Create(const EOS_Platform_Options* Options){
    Logger::info("[INTERCEPT] >>> EOS_Platform_Create CALLED! <<<");
    if (Options) {
        Logger::info("[INTERCEPT]   ProductId=%s, SandboxId=%s",
            Options->ProductId ? Options->ProductId : "NULL",
            Options->SandboxId ? Options->SandboxId : "NULL");
    }
    static auto original = ScreamAPI::proxyFunction(&EOS_Platform_Create, "EOS_Platform_Create");
    // Store originals for ForceAchievementsConfiguration
    static bool originalsStored = false;
    if (!originalsStored) {
        originalsStored = true;
        auto qDefs = ScreamAPI::proxyFunction(&EOS_Achievements_QueryDefinitions, "EOS_Achievements_QueryDefinitions");
        auto qPlayer = ScreamAPI::proxyFunction(&EOS_Achievements_QueryPlayerAchievements, "EOS_Achievements_QueryPlayerAchievements");
        auto unlock = ScreamAPI::proxyFunction(&EOS_Achievements_UnlockAchievements, "EOS_Achievements_UnlockAchievements");
        Intercept::SetAchievementsOriginals(qDefs, qPlayer, unlock);
    }
    return Intercept::Platform_Create(original, Options);
}

EOS_DECLARE_FUNC(void) EOS_Platform_Release(EOS_HPlatform Handle){
    static auto original = ScreamAPI::proxyFunction(&EOS_Platform_Release, "EOS_Platform_Release");
    Intercept::Platform_Release(original, Handle);
}

EOS_DECLARE_FUNC(void) EOS_Platform_Tick(EOS_HPlatform Handle){
    static auto original = ScreamAPI::proxyFunction(&EOS_Platform_Tick, "EOS_Platform_Tick");
    Intercept::Platform_Tick(original, Handle);
}
