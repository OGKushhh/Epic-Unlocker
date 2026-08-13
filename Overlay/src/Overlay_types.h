#pragma once
#include "pch.h"
#include <cstdint>

enum class UnlockState{
        Locked,
        Unlocked,
        Unlocking
};

/**
 * A single stat threshold that must be satisfied to unlock a stat-gated achievement.
 * Captured from EOS_Achievements_DefinitionV2::StatThresholds during QueryDefinitions.
 */
struct StatThreshold{
        char* Name;        // Stat name (heap-allocated, owned by Overlay_Achievement)
        int32_t Threshold; // Value the stat must reach to satisfy the gate
};

struct Overlay_Achievement{
        const char* AchievementId;
        bool IsHidden;
        UnlockState UnlockState;
        const char* UnlockedDescription;
        const char* UnlockedDisplayName;
        const char* UnlockedIconURL;
        ID3D11ShaderResourceView* IconTexture;
        // Stat-gate metadata: if StatThresholdsCount > 0, the achievement cannot be
        // unlocked via EOS_Achievements_UnlockAchievements alone. The stat(s) must be
        // ingested past their threshold(s) via EOS_Stats_IngestStat, which triggers a
        // server-side achievement unlock.
        uint32_t StatThresholdsCount;
        StatThreshold* StatThresholds;
};

typedef std::vector<Overlay_Achievement> Achievements;

typedef void (UnlockAchievementFunction)(Overlay_Achievement*);
