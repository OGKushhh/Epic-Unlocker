#include "pch.h"
#include "eos-sdk/eos_sdk.h"
#include "eos-sdk/eos_stats.h"
#include "ScreamAPI.h"
#include "eos_intercept.h"

EOS_DECLARE_FUNC(EOS_HAuth) EOS_Platform_GetAuthInterface(EOS_HPlatform Handle){
    static auto original = ScreamAPI::proxyFunction(&EOS_Platform_GetAuthInterface, "EOS_Platform_GetAuthInterface");
    return Intercept::Platform_GetAuthInterface(original, Handle);
}

EOS_DECLARE_FUNC(EOS_HAchievements) EOS_Platform_GetAchievementsInterface(EOS_HPlatform Handle){
    static auto original = ScreamAPI::proxyFunction(&EOS_Platform_GetAchievementsInterface, "EOS_Platform_GetAchievementsInterface");
    return Intercept::Platform_GetAchievementsInterface(original, Handle);
}

EOS_DECLARE_FUNC(EOS_HConnect) EOS_Platform_GetConnectInterface(EOS_HPlatform Handle){
    static auto original = ScreamAPI::proxyFunction(&EOS_Platform_GetConnectInterface, "EOS_Platform_GetConnectInterface");
    return Intercept::Platform_GetConnectInterface(original, Handle);
}

// Added by stat-gated achievement unlock patch.
// Provides the direct-call proxy for EOS_Platform_GetStatsInterface so that
// internal ScreamAPI code (Util::getHStats in util.cpp) can resolve the symbol
// at link time. Without this, util.obj throws LNK2001.
EOS_DECLARE_FUNC(EOS_HStats) EOS_Platform_GetStatsInterface(EOS_HPlatform Handle){
    static auto original = ScreamAPI::proxyFunction(&EOS_Platform_GetStatsInterface, "EOS_Platform_GetStatsInterface");
    return Intercept::Platform_GetStatsInterface(original, Handle);
}

EOS_DECLARE_FUNC(EOS_HEcom) EOS_Platform_GetEcomInterface(EOS_HPlatform Handle){
    static auto original = ScreamAPI::proxyFunction(&EOS_Platform_GetEcomInterface, "EOS_Platform_GetEcomInterface");
    return Intercept::Platform_GetEcomInterface(original, Handle);
}

EOS_DECLARE_FUNC(EOS_HUI) EOS_Platform_GetUIInterface(EOS_HPlatform Handle){
    static auto original = ScreamAPI::proxyFunction(&EOS_Platform_GetUIInterface, "EOS_Platform_GetUIInterface");
    return Intercept::Platform_GetUIInterface(original, Handle);
}
