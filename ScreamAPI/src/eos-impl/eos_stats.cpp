#include "pch.h"
#include "eos-sdk/eos_stats.h"
#include "ScreamAPI.h"

// Proxy implementations for EOS Stats interface functions.
// These resolve the real function pointer from the original EOS SDK DLL at
// runtime via GetProcAddress (ScreamAPI::proxyFunction), allowing internal
// code (e.g., achievement_manager.cpp) to call them directly.
//
// Used by the stat-gated achievement unlock feature: when an achievement has
// StatThresholdsCount > 0, we call EOS_Stats_IngestStat to push the stat
// value past its threshold, triggering a server-side achievement unlock.

EOS_DECLARE_FUNC(void) EOS_Stats_IngestStat(EOS_HStats Handle, const EOS_Stats_IngestStatOptions* Options, void* ClientData, const EOS_Stats_OnIngestStatCompleteCallback CompletionDelegate){
	EOS_IMPLEMENT_FUNC(EOS_Stats_IngestStat, Handle, Options, ClientData, CompletionDelegate);
}

EOS_DECLARE_FUNC(void) EOS_Stats_QueryStats(EOS_HStats Handle, const EOS_Stats_QueryStatsOptions* Options, void* ClientData, const EOS_Stats_OnQueryStatsCompleteCallback CompletionDelegate){
	EOS_IMPLEMENT_FUNC(EOS_Stats_QueryStats, Handle, Options, ClientData, CompletionDelegate);
}

EOS_DECLARE_FUNC(uint32_t) EOS_Stats_GetStatsCount(EOS_HStats Handle, const EOS_Stats_GetStatCountOptions* Options){
	EOS_IMPLEMENT_FUNC(EOS_Stats_GetStatsCount, Handle, Options);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Stats_CopyStatByIndex(EOS_HStats Handle, const EOS_Stats_CopyStatByIndexOptions* Options, EOS_Stats_Stat** OutStat){
	EOS_IMPLEMENT_FUNC(EOS_Stats_CopyStatByIndex, Handle, Options, OutStat);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Stats_CopyStatByName(EOS_HStats Handle, const EOS_Stats_CopyStatByNameOptions* Options, EOS_Stats_Stat** OutStat){
	EOS_IMPLEMENT_FUNC(EOS_Stats_CopyStatByName, Handle, Options, OutStat);
}

EOS_DECLARE_FUNC(void) EOS_Stats_Stat_Release(EOS_Stats_Stat* Stat){
	EOS_IMPLEMENT_FUNC(EOS_Stats_Stat_Release, Stat);
}
