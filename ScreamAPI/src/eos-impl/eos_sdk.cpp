#include "pch.h"
#include "eos-sdk/eos_sdk.h"
#include "eos-sdk/eos_stats.h"
#include "ScreamAPI.h"

EOS_DECLARE_FUNC(EOS_HAuth) EOS_Platform_GetAuthInterface(EOS_HPlatform Handle){
	EOS_IMPLEMENT_FUNC(EOS_Platform_GetAuthInterface, Handle);
}

EOS_DECLARE_FUNC(EOS_HAchievements) EOS_Platform_GetAchievementsInterface(EOS_HPlatform Handle){
	EOS_IMPLEMENT_FUNC(EOS_Platform_GetAchievementsInterface, Handle);
}

EOS_DECLARE_FUNC(EOS_HConnect) EOS_Platform_GetConnectInterface(EOS_HPlatform Handle){
	EOS_IMPLEMENT_FUNC(EOS_Platform_GetConnectInterface, Handle);
}

// Added by stat-gated achievement unlock patch.
// Provides the direct-call proxy for EOS_Platform_GetStatsInterface so that
// internal ScreamAPI code (Util::getHStats in util.cpp) can resolve the symbol
// at link time. Without this, util.obj throws LNK2001.
//
// Note: this is separate from the MinHook hook installed in eos_hooks.cpp
// (Hooks::Platform_GetStatsInterface, line 298). The hook intercepts the
// game's calls; this proxy lets our own code call the function directly.
EOS_DECLARE_FUNC(EOS_HStats) EOS_Platform_GetStatsInterface(EOS_HPlatform Handle){
	EOS_IMPLEMENT_FUNC(EOS_Platform_GetStatsInterface, Handle);
}
