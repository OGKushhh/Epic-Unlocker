#include "pch.h"
#include "achievement_manager.h"
#include "ScreamAPI.h"
#include "util.h"
#include "eos-sdk/eos_achievements.h"
#include "eos-sdk/eos_stats.h"
#include "PipeServer.h"
#include "Overlay.h"
#include <future>
#include <atomic>
#include <thread>
#include <mutex>
#include <vector>
#include <cstdint>        // uintptr_t for tagged-ClientData convention

using namespace Util;

namespace AchievementManager {

// Forward declaration
void queryAchievementDefinitions();
void queryAchievementDefinitionsWithRetry(int delayMs = 0);

Achievements achievements;
static std::mutex g_achMutex;  // guards achievements vector

std::mutex& GetAchievementsMutex() { return g_achMutex; }

// Free heap-allocated stat threshold arrays + StatThresholdLabel strings in an
// achievement vector. Called before clearing/repopulating the achievements list
// to prevent leaks.
static void freeAchievementStatThresholds(Achievements& vec) {
    for (auto& ach : vec) {
        if (ach.StatThresholds != nullptr && ach.StatThresholdsCount > 0) {
            for (uint32_t j = 0; j < ach.StatThresholdsCount; j++) {
                delete[] ach.StatThresholds[j].Name;
            }
            delete[] ach.StatThresholds;
            ach.StatThresholds = nullptr;
            ach.StatThresholdsCount = 0;
        }
        // A3: free the StatThresholdLabel string (heap-allocated in
        // queryPlayerAchievementsComplete). Safe to delete nullptr.
        delete[] ach.StatThresholdLabel;
        ach.StatThresholdLabel = nullptr;
        // A3: reset Progress too (will be repopulated).
        ach.Progress = 0.0f;
    }
}

// Track if we've already retried due to missing user
static std::atomic<bool> waitingForUser{false};
static std::atomic<bool> definitionsQueried{false};
static std::atomic<bool> playerAchievementsQueried{false};

// ----------------------------------------------------------------------------
// Helper: Log achievement statistics to the log file
// ----------------------------------------------------------------------------
static void LogAchievementStats() {
    if (achievements.empty()) {
        Logger::ach("No achievements loaded yet.");
        return;
    }

    int total = (int)achievements.size();
    int unlocked = 0;
    for (const auto& ach : achievements) {
        if (ach.UnlockState == UnlockState::Unlocked) unlocked++;
    }
    float percent = (total > 0) ? (unlocked * 100.0f / total) : 0.0f;

    Logger::ach("========================================");
    Logger::ach("Achievement Statistics");
    Logger::ach("  Total achievements: %d", total);
    Logger::ach("  Unlocked: %d", unlocked);
    Logger::ach("  Locked: %d", total - unlocked);
    Logger::ach("  Progress: %.1f%%", percent);
    Logger::ach("========================================");
}

void printAchievementDefinition(EOS_Achievements_DefinitionV2* definition) {
    if (definition == nullptr) {
        Logger::ach("Invalid Achievement Definition");
    } else {
        std::stringstream ss;
        ss
            << "[Achievement Definition]\n"
            << "\t\t\t""AchievementId: " << definition->AchievementId << "\n"
            << "\t\t\t""IsHidden: " << definition->bIsHidden << "\n"
            << "\t\t\t""FlavorText: " << definition->FlavorText << "\n"
            << "\t\t\t""LockedDescription: " << definition->LockedDescription << "\n"
            << "\t\t\t""LockedDisplayName: " << definition->LockedDisplayName << "\n"
            << "\t\t\t""LockedIconURL: " << definition->LockedIconURL << "\n"
            << "\t\t\t""UnlockedDescription: " << definition->UnlockedDescription << "\n"
            << "\t\t\t""UnlockedDisplayName: " << definition->UnlockedDisplayName << "\n"
            << "\t\t\t""UnlockedIconURL: " << definition->UnlockedIconURL << "\n"
            << "\t\t\t""StatThresholdsCount: " << definition->StatThresholdsCount << "\n";

        for (unsigned int i = 0; i < definition->StatThresholdsCount; i++) {
            ss
                << "\t\t\t\t""[StatThreshold] "
                << "Name: " << definition->StatThresholds[i].Name << "; "
                << "Threshold: " << definition->StatThresholds[i].Threshold;
        }
        Logger::ach("%s", ss.str().c_str());
    }
}

void printPlayerAchievement(EOS_Achievements_PlayerAchievement* achievement) {
    if (achievement == nullptr) {
        Logger::ach("Invalid Player Achievement");
    } else {
        std::stringstream ss;
        ss
            << "[Player Achievement]\n"
            << "\t\t\t""AchievementId: " << achievement->AchievementId << "\n"
            << "\t\t\t""Description: " << achievement->Description << "\n"
            << "\t\t\t""DisplayName: " << achievement->DisplayName << "\n"
            << "\t\t\t""FlavorText: " << achievement->FlavorText << "\n"
            << "\t\t\t""IconURL: " << achievement->IconURL << "\n"
            << "\t\t\t""Progress: " << achievement->Progress << "\n"
            << "\t\t\t""StatInfoCount: " << achievement->StatInfoCount << "\n";

        for (int i = 0; i < achievement->StatInfoCount; i++) {
            ss
                << "\t\t\t\t""[StatInfo] "
                << "Name: " << achievement->StatInfo[i].Name << "; "
                << "CurrentValue: " << achievement->StatInfo[i].CurrentValue << "; "
                << "ThresholdValue: " << achievement->StatInfo[i].ThresholdValue;
        }
        Logger::ach("%s", ss.str().c_str());
    }
}

void findAchievement(const char* achievementID, std::function<void(Overlay_Achievement&)> callback) {
    for (auto& achievement : achievements) {
        if (!strcmp(achievement.AchievementId, achievementID)) {
            callback(achievement);
            return;
        }
    }
    Logger::error("Could not find achievement with id: %s", achievementID);
}

// ----------------------------------------------------------------------------
// Static callbacks for achievement operations
// ----------------------------------------------------------------------------

// Tagged-ClientData convention for stat-ingest follow-up calls.
// When the belt-and-suspenders direct-unlock call is made after a successful
// stat ingest (inside OnIngestStatComplete), we set bit 0 of the ClientData
// pointer to mark it as a "stat-ingest follow-up" call. This lets
// OnUnlockAchievementsComplete tolerate EOS_NotConfigured for that specific
// call, because the server hasn't yet processed the stat crossing.
//
// Why this matters: without the tag, OnUnlockAchievementsComplete would
// unconditionally reset the achievement state to Locked on any non-Success
// result. For stat-gated achievements, the belt-and-suspenders call typically
// returns EOS_NotConfigured (server hasn't evaluated the threshold crossing
// yet), which would cause both the EpicGUI and the ImGui overlay to briefly
// flicker from "Unlocking" back to "Locked" before the server-side unlock
// notification arrives ~10-30s later and flips it to "Unlocked".
//
// With the tag, the handler keeps the state in Unlocking for the
// EOS_NotConfigured case, eliminating the flicker in both UIs.
static void EOS_CALL OnUnlockAchievementsComplete(const EOS_Achievements_OnUnlockAchievementsCompleteCallbackInfo* Data) {
    bool isStatIngestFollowUp = ((uintptr_t)Data->ClientData & 1) != 0;
    auto achievement = (Overlay_Achievement*)((uintptr_t)Data->ClientData & ~(uintptr_t)1);
    if (Data->ResultCode == EOS_EResult::EOS_Success) {
        Logger::info("Successfully unlocked the achievement: %s", achievement->AchievementId);
        // State will be flipped to Unlocked by OnAchievementsUnlockedV2 when the
        // server-side notification arrives. Leave it as Unlocking in the meantime.
    } else if (isStatIngestFollowUp && Data->ResultCode == EOS_EResult::EOS_NotConfigured) {
        // Expected case: stat ingest succeeded, but server hasn't evaluated the
        // threshold crossing yet. Keep state as Unlocking; the server-side
        // unlock notification (OnAchievementsUnlockedV2) will flip it to
        // Unlocked within ~10-30 seconds. This prevents the GUI/overlay
        // flicker from Locked -> Unlocking -> Unlocked.
        Logger::info("[STAT] Direct unlock returned EOS_NotConfigured (expected) - waiting for server-side evaluation: %s",
            achievement->AchievementId);
    } else {
        achievement->UnlockState = UnlockState::Locked;
        Logger::error("Failed to unlock the achievement: %s. Error string: %s",
            achievement->AchievementId,
            EOS_EResult_ToString(Data->ResultCode));
    }
}

static void EOS_CALL OnAchievementsUnlockedV2(const EOS_Achievements_OnAchievementsUnlockedCallbackV2Info* Data) {
    findAchievement(Data->AchievementId, [](Overlay_Achievement& achievement) {
        achievement.UnlockState = UnlockState::Unlocked;
    });
    PipeServer::NotifyUnlock(Data->AchievementId);
}

static void EOS_CALL OnAchievementsUnlocked(const EOS_Achievements_OnAchievementsUnlockedCallbackInfo* Data) {
    for (uint32_t i = 0; i < Data->AchievementsCount; i++) {
        findAchievement(Data->AchievementIds[i], [](Overlay_Achievement& achievement) {
            achievement.UnlockState = UnlockState::Unlocked;
        });
        PipeServer::NotifyUnlock(Data->AchievementIds[i]);
    }
}

// ----------------------------------------------------------------------------
// Stat-gated achievement unlock via EOS_Stats_IngestStat
// ----------------------------------------------------------------------------
// Stat-gated achievements (StatThresholdsCount > 0) cannot be unlocked via
// EOS_Achievements_UnlockAchievements — the SDK returns EOS_NotConfigured
// because the stat dependency is not satisfied. To unlock them, we must
// ingest the stat value past its threshold via EOS_Stats_IngestStat, which
// triggers a server-side achievement unlock evaluation.
//
// Flow:
//   1. Build EOS_Stats_IngestData array from achievement->StatThresholds
//   2. Call EOS_Stats_IngestStat with IngestAmount = Threshold
//   3. On success, optionally call EOS_Achievements_UnlockAchievements as a
//      belt-and-suspenders fallback (the server usually auto-unlocks)
// ----------------------------------------------------------------------------

static void EOS_CALL OnIngestStatComplete(const EOS_Stats_IngestStatCompleteCallbackInfo* Data) {
    auto achievement = (Overlay_Achievement*)Data->ClientData;

    if (Data->ResultCode == EOS_EResult::EOS_Success) {
        Logger::info("[STAT] Stat ingest succeeded for achievement: %s", achievement->AchievementId);
        Logger::info("[STAT] Server will evaluate achievement unlock (may take a few seconds)");

        // As a belt-and-suspenders approach, also issue a direct unlock request.
        // The server may have already auto-unlocked the achievement based on the
        // stat crossing its threshold, but this covers cases where the server
        // requires an explicit unlock call.
        EOS_Achievements_UnlockAchievementsOptions Options = {
            EOS_ACHIEVEMENTS_UNLOCKACHIEVEMENTS_API_LATEST,
            getProductUserId(),
            &achievement->AchievementId,
            1
        };
        // Tag the ClientData pointer with bit 0 = 1 to mark this as a stat-ingest
        // follow-up call. OnUnlockAchievementsComplete will detect the tag and
        // tolerate EOS_NotConfigured (which is the expected result here, since
        // the server hasn't yet processed the stat crossing). This prevents the
        // GUI/overlay from flickering back to "Locked" before the server-side
        // unlock notification arrives.
        void* taggedClientData = (void*)((uintptr_t)achievement | 1);
        EOS_Achievements_UnlockAchievements(getHAchievements(), &Options, taggedClientData, OnUnlockAchievementsComplete);
    } else {
        achievement->UnlockState = UnlockState::Locked;
        Logger::error("[STAT] Stat ingest FAILED for achievement: %s. Error: %s",
            achievement->AchievementId,
            EOS_EResult_ToString(Data->ResultCode));
        Logger::error("[STAT] The achievement may remain locked. Check ScreamAPI.log for details.");
    }
}

static void unlockAchievementViaStatIngest(Overlay_Achievement* achievement) {
    Logger::info("[STAT] Attempting stat-gated unlock for: %s (StatThresholdsCount=%u)",
        achievement->AchievementId, achievement->StatThresholdsCount);

    auto hStats = getHStats();
    if (hStats == nullptr) {
        Logger::error("[STAT] Cannot ingest stat — EOS Stats interface is NULL");
        Logger::error("[STAT] The game may not have initialized the Stats subsystem.");
        Logger::error("[STAT] Falling back to direct unlock (will likely fail with EOS_NotConfigured)");

        // Fall back to direct unlock — it will fail, but the error message
        // will be consistent with the previous behavior.
        EOS_Achievements_UnlockAchievementsOptions Options = {
            EOS_ACHIEVEMENTS_UNLOCKACHIEVEMENTS_API_LATEST,
            getProductUserId(),
            &achievement->AchievementId,
            1
        };
        EOS_Achievements_UnlockAchievements(getHAchievements(), &Options, achievement, OnUnlockAchievementsComplete);
        return;
    }

    auto userId = getProductUserId();
    if (userId == nullptr) {
        Logger::error("[STAT] Cannot ingest stat — user not logged in");
        achievement->UnlockState = UnlockState::Locked;
        return;
    }

    // Build ingest data array — one entry per stat threshold
    // Use a vector to ensure the array outlives the EOS_Stats_IngestStat call
    std::vector<EOS_Stats_IngestData> ingestData;
    ingestData.reserve(achievement->StatThresholdsCount);

    for (uint32_t i = 0; i < achievement->StatThresholdsCount; i++) {
        auto& threshold = achievement->StatThresholds[i];
        // Ingest exactly the threshold amount. Since the stat is typically at 0
        // for bugged achievements, this pushes it to exactly the threshold value.
        // The EOS backend evaluates stat >= threshold, so this is sufficient.
        // Using threshold (not threshold+1) to avoid overshooting on MIN-type stats.
        int32_t ingestAmount = threshold.Threshold;

        EOS_Stats_IngestData data;
        data.ApiVersion = EOS_STATS_INGESTDATA_API_LATEST;
        data.StatName = threshold.Name;
        data.IngestAmount = ingestAmount;
        ingestData.push_back(data);

        Logger::info("[STAT]   Ingesting stat '%s' += %d (threshold: %d)",
            threshold.Name, ingestAmount, threshold.Threshold);
    }

    EOS_Stats_IngestStatOptions options;
    options.ApiVersion = EOS_STATS_INGESTSTAT_API_LATEST;
    options.LocalUserId = userId;
    options.Stats = ingestData.data();
    options.StatsCount = (uint32_t)ingestData.size();
    options.TargetUserId = userId;

    Logger::info("[STAT] Submitting EOS_Stats_IngestStat for %u stat(s)...", options.StatsCount);
    EOS_Stats_IngestStat(hStats, &options, achievement, OnIngestStatComplete);
}

void unlockAchievement(Overlay_Achievement* achievement) {
    achievement->UnlockState = UnlockState::Unlocking;

    // ── Stat-gated achievements ───────────────────────────────────────────
    // If the achievement has stat thresholds, we must ingest the stat(s) past
    // their threshold(s) to trigger a server-side unlock. Direct unlock via
    // EOS_Achievements_UnlockAchievements returns EOS_NotConfigured for these.
    // ─────────────────────────────────────────────────────────────────────
    if (achievement->StatThresholdsCount > 0 && achievement->StatThresholds != nullptr) {
        Logger::info("[ACH] Achievement '%s' is stat-gated (%u threshold(s)) — using stat ingest path",
            achievement->AchievementId, achievement->StatThresholdsCount);
        unlockAchievementViaStatIngest(achievement);
        return;
    }

    // ── Direct unlock path (non-stat-gated achievements) ──────────────────
    EOS_Achievements_UnlockAchievementsOptions Options = {
        EOS_ACHIEVEMENTS_UNLOCKACHIEVEMENTS_API_LATEST,
        getProductUserId(),
        &achievement->AchievementId,
        1
    };

    EOS_Achievements_UnlockAchievements(getHAchievements(), &Options, achievement, OnUnlockAchievementsComplete);
}

// ----------------------------------------------------------------------------
// Player achievements query callback (with retry on authentication errors)
// ----------------------------------------------------------------------------

void EOS_CALL queryPlayerAchievementsComplete(const EOS_Achievements_OnQueryPlayerAchievementsCompleteCallbackInfo* Data) {
    Logger::debug("queryPlayerAchievementsComplete");

    if (Data->ResultCode != EOS_EResult::EOS_Success) {
        Logger::error("Failed to query player achievements. Result string: %s",
            EOS_EResult_ToString(Data->ResultCode));

        // If authentication failed, try again later
        if (Data->ResultCode == EOS_EResult::EOS_InvalidUser || Data->ResultCode == EOS_EResult::EOS_InvalidAuth) {
            Logger::debug("User not authenticated – will retry in 5 seconds");
            std::thread([]() {
                Sleep(5000);
                EOS_Achievements_QueryPlayerAchievementsOptions QueryOptions = {
                    1,                             // ApiVersion (legacy)
                    getProductUserId()             // UserId
                };
                EOS_Achievements_QueryPlayerAchievements(
                    getHAchievements(),
                    &QueryOptions,
                    nullptr,
                    queryPlayerAchievementsComplete
                );
            }).detach();
        }
        return;
    }

    playerAchievementsQueried = true;
    Logger::debug("[ACH] Player achievements query succeeded");
    PipeServer::Start(); // achievements list is now complete — open pipe for GUI

    static EOS_Achievements_GetPlayerAchievementCountOptions GetCountOptions{
        EOS_ACHIEVEMENTS_GETPLAYERACHIEVEMENTCOUNT_API_LATEST,
        getProductUserId()
    };
    auto playerAchievementsCount = EOS_Achievements_GetPlayerAchievementCount(getHAchievements(), &GetCountOptions);
    for (unsigned int i = 0; i < playerAchievementsCount; i++) {
        EOS_Achievements_CopyPlayerAchievementByIndexOptions CopyAchievementOptions{
            EOS_ACHIEVEMENTS_COPYPLAYERACHIEVEMENTBYINDEX_API_LATEST,
            getProductUserId(),      // TargetUserId
            i,                       // AchievementIndex
            getProductUserId()       // LocalUserId (for modern SDK)
        };
        EOS_Achievements_PlayerAchievement* OutAchievement;
        auto result = EOS_Achievements_CopyPlayerAchievementByIndex(
            getHAchievements(),
            &CopyAchievementOptions,
            &OutAchievement
        );
        if (result != EOS_EResult::EOS_Success) {
            Logger::error("Failed to copy player achievement by index %d. "
                "Result string: %s", i, EOS_EResult_ToString(result));
            continue;
        }

        printPlayerAchievement(OutAchievement);

        // A3: Capture Progress (0..1) and build a human-readable stat threshold
        // label (e.g. "12/50 kills") from StatInfo. We copy these onto the
        // matching Overlay_Achievement so the GUI can render a real progress bar.
        const float capturedProgress = OutAchievement->Progress;
        std::string thresholdLabel;
        if (OutAchievement->StatInfoCount > 0) {
            for (int i = 0; i < OutAchievement->StatInfoCount; i++) {
                if (i > 0) thresholdLabel += ", ";
                const auto& si = OutAchievement->StatInfo[i];
                thresholdLabel += std::to_string(si.CurrentValue);
                thresholdLabel += "/";
                thresholdLabel += std::to_string(si.ThresholdValue);
                if (si.Name) {
                    thresholdLabel += " ";
                    thresholdLabel += si.Name;
                }
            }
        }

        if (OutAchievement->UnlockTime != -1) {
            findAchievement(OutAchievement->AchievementId, [capturedProgress, thresholdLabel](Overlay_Achievement& achievement) {
                achievement.UnlockState = UnlockState::Unlocked;
                achievement.Progress = 1.0f;
                delete[] achievement.StatThresholdLabel;
                if (thresholdLabel.empty()) {
                    achievement.StatThresholdLabel = nullptr;
                } else {
                    char* buf = new char[thresholdLabel.size() + 1];
                    strcpy_s(buf, thresholdLabel.size() + 1, thresholdLabel.c_str());
                    achievement.StatThresholdLabel = buf;
                }
            });
        } else {
            // Not unlocked — record the progress + threshold label so the GUI
            // can render a partial progress bar.
            findAchievement(OutAchievement->AchievementId, [capturedProgress, thresholdLabel](Overlay_Achievement& achievement) {
                achievement.Progress = capturedProgress;
                delete[] achievement.StatThresholdLabel;
                if (thresholdLabel.empty()) {
                    achievement.StatThresholdLabel = nullptr;
                } else {
                    char* buf = new char[thresholdLabel.size() + 1];
                    strcpy_s(buf, thresholdLabel.size() + 1, thresholdLabel.c_str());
                    achievement.StatThresholdLabel = buf;
                }
            });
        }

        EOS_Achievements_PlayerAchievement_Release(OutAchievement);
    }

    // Log achievement statistics after processing
    LogAchievementStats();

    // Notify GUI that the updated list is ready
    PipeServer::SendUpdatedList();
}

// ----------------------------------------------------------------------------
// Definitions query callback (with retry on authentication errors)
// ----------------------------------------------------------------------------

void EOS_CALL queryDefinitionsComplete(const EOS_Achievements_OnQueryDefinitionsCompleteCallbackInfo* Data) {
    Logger::debug("[ACH] queryDefinitionsComplete called with ResultCode: %d", Data->ResultCode);

    if (Data->ResultCode != EOS_EResult::EOS_Success) {
        Logger::error("[ACH] Failed to query achievement definitions. Result: %s",
            EOS_EResult_ToString(Data->ResultCode));

        // If authentication failed, retry later
        if (Data->ResultCode == EOS_EResult::EOS_InvalidAuth || Data->ResultCode == EOS_EResult::EOS_InvalidUser) {
            Logger::debug("[ACH] Authentication required – will retry in 5 seconds");
            std::thread([]() {
                Sleep(5000);
                queryAchievementDefinitions();
            }).detach();
        }
        return;
    }

    definitionsQueried = true;
    Logger::debug("[ACH] Achievement definitions query succeeded");

    // Clear existing achievements before repopulating (in case of retry)
    // Free stat threshold arrays first to prevent memory leak on refresh.
    freeAchievementStatThresholds(achievements);
    achievements.clear();

    static EOS_Achievements_GetAchievementDefinitionCountOptions GetCountOptions{
        EOS_ACHIEVEMENTS_GETACHIEVEMENTDEFINITIONCOUNT_API_LATEST
    };
    auto achievementDefinitionCount = EOS_Achievements_GetAchievementDefinitionCount(getHAchievements(), &GetCountOptions);
    for (uint32_t i = 0; i < achievementDefinitionCount; i++) {
        static bool useDeprecated = false;

        if (!useDeprecated) {
            try {
                EOS_Achievements_CopyAchievementDefinitionV2ByIndexOptions DefinitionOptions{
                    EOS_ACHIEVEMENTS_COPYACHIEVEMENTDEFINITIONV2BYINDEX_API_LATEST,
                    i
                };
                EOS_Achievements_DefinitionV2* OutDefinition;
                auto result = EOS_Achievements_CopyAchievementDefinitionV2ByIndex(getHAchievements(), &DefinitionOptions, &OutDefinition);
                if (result != EOS_EResult::EOS_Success) {
                    Logger::error("Failed to copy achievement definition by index %d. "
                        "Result string: %s", i, EOS_EResult_ToString(result));
                    continue;
                }

                printAchievementDefinition(OutDefinition);

                // Capture stat thresholds for stat-gated achievement unlocking.
                // These are used by unlockAchievementViaStatIngest() to ingest
                // stat values past their thresholds via EOS_Stats_IngestStat.
                StatThreshold* statThresholds = nullptr;
                uint32_t statThresholdsCount = OutDefinition->StatThresholdsCount;
                if (statThresholdsCount > 0 && OutDefinition->StatThresholds != nullptr) {
                    statThresholds = new StatThreshold[statThresholdsCount];
                    for (uint32_t j = 0; j < statThresholdsCount; j++) {
                        statThresholds[j].Name = copy_c_string(OutDefinition->StatThresholds[j].Name);
                        statThresholds[j].Threshold = OutDefinition->StatThresholds[j].Threshold;
                    }
                    Logger::debug("[ACH]   Captured %u stat threshold(s) for '%s'",
                        statThresholdsCount, OutDefinition->AchievementId);
                }

                achievements.push_back(
                    {
                        copy_c_string(OutDefinition->AchievementId),
                        (bool)OutDefinition->bIsHidden,
                        UnlockState::Locked,
                        copy_c_string(OutDefinition->UnlockedDescription),
                        copy_c_string(OutDefinition->UnlockedDisplayName),
                        copy_c_string(OutDefinition->UnlockedIconURL),
                        nullptr,
                        statThresholdsCount,
                        statThresholds
                    }
                );

                EOS_Achievements_DefinitionV2_Release(OutDefinition);
                continue;
            } catch (ScreamAPI::FunctionNotFoundException) {
                useDeprecated = true;
            }
        }

        EOS_Achievements_CopyAchievementDefinitionByIndexOptions DefinitionOptions{
            EOS_ACHIEVEMENTS_COPYDEFINITIONBYINDEX_API_LATEST,
            i
        };
        EOS_Achievements_Definition* OutDefinition;
        auto result = EOS_Achievements_CopyAchievementDefinitionByIndex(getHAchievements(), &DefinitionOptions, &OutDefinition);
        if (result != EOS_EResult::EOS_Success) {
            Logger::error("Failed to copy (deprecated) achievement definition by index %d. "
                "Result string: %s", i, EOS_EResult_ToString(result));
            continue;
        }

        // Capture stat thresholds (deprecated V1 struct also has these fields)
        StatThreshold* statThresholds = nullptr;
        uint32_t statThresholdsCount = (uint32_t)OutDefinition->StatThresholdsCount;
        if (statThresholdsCount > 0 && OutDefinition->StatThresholds != nullptr) {
            statThresholds = new StatThreshold[statThresholdsCount];
            for (uint32_t j = 0; j < statThresholdsCount; j++) {
                statThresholds[j].Name = copy_c_string(OutDefinition->StatThresholds[j].Name);
                statThresholds[j].Threshold = OutDefinition->StatThresholds[j].Threshold;
            }
        }

        achievements.push_back(
            {
                copy_c_string(OutDefinition->AchievementId),
                (bool)OutDefinition->bIsHidden,
                UnlockState::Locked,
                copy_c_string(OutDefinition->CompletionDescription),
                copy_c_string(OutDefinition->DisplayName),
                copy_c_string(OutDefinition->UnlockedIconId),
                nullptr,
                statThresholdsCount,
                statThresholds
            }
        );

        EOS_Achievements_Definition_Release(OutDefinition);
    }

    // Query player achievements – use legacy version (1) to allow NULL user ID
    EOS_Achievements_QueryPlayerAchievementsOptions QueryAchievementsOptions = {
        1,                             // ApiVersion (legacy)
        getProductUserId()             // UserId (TargetUserId)
    };

    EOS_Achievements_QueryPlayerAchievements(
        getHAchievements(),
        &QueryAchievementsOptions,
        nullptr,
        queryPlayerAchievementsComplete
    );
}

// ----------------------------------------------------------------------------
// Query definitions (legacy API version) with retry if user not logged in
// ----------------------------------------------------------------------------

void queryAchievementDefinitions() {
    auto productUserId = getProductUserId();
    auto epicAccountId = getEpicAccountId();
    auto hAchievements = getHAchievements();

    Logger::debug("[ACH] queryAchievementDefinitions called");
    Logger::debug("[ACH]   ProductUserId: %p", productUserId);
    Logger::debug("[ACH]   EpicAccountId: %p", epicAccountId);
    Logger::debug("[ACH]   HAchievements: %p", hAchievements);

    if (hAchievements == nullptr) {
        Logger::error("[ACH] Cannot query achievements - HAchievements is NULL");
        return;
    }

    // If no user is logged in yet, schedule a retry
    if (productUserId == nullptr && epicAccountId == nullptr) {
        if (!waitingForUser.exchange(true)) {
            Logger::warn("[ACH] Both ProductUserId and EpicAccountId are NULL - user not logged in yet");
            Logger::warn("[ACH] Will retry definitions query every 5 seconds until login");
            queryAchievementDefinitionsWithRetry(5000);
        } else {
            Logger::debug("[ACH] Already waiting for user login, skipping duplicate retry schedule");
        }
        return;
    }

    // User exists – proceed with query
    waitingForUser = false;
    definitionsQueried = false;

    // Legacy API version (1) – matches v1.10.2 behavior
    EOS_Achievements_QueryDefinitionsOptions QueryDefinitionsOptions = {
        1,                             // ApiVersion
        productUserId,                 // TargetUserId (LocalUserId in modern)
        epicAccountId,                 // EpicUserId_DEPRECATED
        nullptr,                       // HiddenAchievementIds_DEPRECATED
        0                              // HiddenAchievementsCount_DEPRECATED
    };

    Logger::debug("[ACH] Calling EOS_Achievements_QueryDefinitions");
    EOS_Achievements_QueryDefinitions(
        hAchievements,
        &QueryDefinitionsOptions,
        nullptr,
        queryDefinitionsComplete
    );
}

// Retry helper: sleeps and then calls queryAchievementDefinitions again
void queryAchievementDefinitionsWithRetry(int delayMs) {
    std::thread([delayMs]() {
        Sleep(delayMs);
        // Check again if user is now logged in
        auto productUserId = getProductUserId();
        auto epicAccountId = getEpicAccountId();
        if (productUserId != nullptr || epicAccountId != nullptr) {
            Logger::info("[ACH] User logged in detected, retrying achievement definitions query");
            queryAchievementDefinitions();
        } else {
            // Still no user – keep retrying
            Logger::debug("[ACH] User still not logged in, will retry definitions again in 5 seconds");
            queryAchievementDefinitionsWithRetry(5000);
        }
    }).detach();
}

// ----------------------------------------------------------------------------
// Notifications
// ----------------------------------------------------------------------------

void subscribeToAchievementNotifications() {
    static bool useDeprecated = false;

    if (!useDeprecated) {
        try {
            EOS_Achievements_AddNotifyAchievementsUnlockedV2Options NotifyOptions = {
                EOS_ACHIEVEMENTS_ADDNOTIFYACHIEVEMENTSUNLOCKEDV2_API_LATEST
            };
            EOS_Achievements_AddNotifyAchievementsUnlockedV2(getHAchievements(), &NotifyOptions, nullptr, OnAchievementsUnlockedV2);
            Logger::debug("[ACH] Subscribed to V2 unlock notifications");
            return;
        } catch (ScreamAPI::FunctionNotFoundException) {
            useDeprecated = true;
        }
    }

    EOS_Achievements_AddNotifyAchievementsUnlockedOptions NotifyOptions = {
        EOS_ACHIEVEMENTS_ADDNOTIFYACHIEVEMENTSUNLOCKED_API_LATEST
    };
    EOS_Achievements_AddNotifyAchievementsUnlocked(getHAchievements(), &NotifyOptions, nullptr, OnAchievementsUnlocked);
    Logger::debug("[ACH] Subscribed to deprecated unlock notifications");
}

// ----------------------------------------------------------------------------
// Initialization
// ----------------------------------------------------------------------------

void init() {
    static bool init = false;

    if (!init && Config::EnableOverlay()) {
        init = true;
        Logger::ovrly("Achievement Manager: Initializing...");
        Logger::debug("[ACH] init(): Starting achievement manager initialization");

        queryAchievementDefinitions();
        subscribeToAchievementNotifications();
        Logger::debug("[ACH] init(): Achievement manager initialization complete");
    }
}

// Public function to manually retry (call from login hook if needed)
void retryAchievementQueries() {
    if (!definitionsQueried) {
        Logger::info("[ACH] Manual retry triggered – re-querying definitions");
        queryAchievementDefinitions();
    } else if (!playerAchievementsQueried) {
        Logger::info("[ACH] Manual retry triggered – re-querying player achievements");
        EOS_Achievements_QueryPlayerAchievementsOptions QueryOptions = {
            1,
            getProductUserId()
        };
        EOS_Achievements_QueryPlayerAchievements(
            getHAchievements(),
            &QueryOptions,
            nullptr,
            queryPlayerAchievementsComplete
        );
    } else {
        Logger::debug("[ACH] Manual retry called but both definitions and player achievements already loaded");
    }
}

void refresh() {
    Logger::info("[ACH] Manual refresh triggered");

    // Reset internal state flags so that a fresh query can run
    definitionsQueried = false;
    playerAchievementsQueried = false;
    waitingForUser = false;

    // Clear existing achievements – the query callback will repopulate
    // Free stat threshold arrays first to prevent memory leak on refresh.
    freeAchievementStatThresholds(achievements);
    achievements.clear();

    // Start a fresh definitions query
    queryAchievementDefinitions();
}

void initWithOverlay(void* hModule) {
    Logger::ovrly("Achievement Manager: initWithOverlay called");
    Overlay::Init((HMODULE)hModule, &achievements, unlockAchievement);
    init();
    Logger::ovrly("Achievement Manager: initWithOverlay complete");
}

} // namespace AchievementManager