#include "pch.h"
#include "achievement_manager.h"
#include "ScreamAPI.h"
#include "util.h"
#include "eos-sdk/eos_achievements.h"
#include "eos-sdk/eos_stats.h"
#include "PipeServer.h"
#include "Overlay.h"
#include "eos_compat.h"
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

// UE5.4+ OSSv2: set to true when init() or TryInitFromFallback() has run,
// preventing duplicate initialization from the other path.
static std::atomic<bool> g_fallbackInitDone{false};

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
//
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
//
// TaggedClientData encapsulates the bit-0 arithmetic so the convention is
// impossible to misuse (no bare | 1 / & ~1 literals scattered at call sites).
struct TaggedClientData {
    void* raw;

    // Mark a pointer as a stat-ingest follow-up call.
    static TaggedClientData tag(void* p) {
        return TaggedClientData{ reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(p) | 1) };
    }
    // Plain (untagged) pointer.
    static TaggedClientData plain(void* p) {
        return TaggedClientData{ p };
    }
    // Strip the tag bit and return the original pointer.
    void* untag() const {
        return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(raw) & ~uintptr_t(1));
    }
    // True if this pointer was tagged via tag().
    bool isTagged() const {
        return (reinterpret_cast<uintptr_t>(raw) & 1) != 0;
    }
};
static void EOS_CALL OnUnlockAchievementsComplete(const EOS_Achievements_OnUnlockAchievementsCompleteCallbackInfo* Data) {
    TaggedClientData cd{Data->ClientData};
    bool isStatIngestFollowUp = cd.isTagged();
    auto* achievement = static_cast<Overlay_Achievement*>(cd.untag());
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
    // G4: Capture the real UnlockTime from the EOS V2 notification.
    // This is the authoritative timestamp - it comes directly from the server
    // and reflects when EOS actually recorded the unlock, not when our code ran.
    int64_t unlockTime = Data->UnlockTime;
    findAchievement(Data->AchievementId, [unlockTime](Overlay_Achievement& achievement) {
        achievement.UnlockState = UnlockState::Unlocked;
        achievement.UnlockTime = unlockTime;  // G4: store the server-provided timestamp
    });
    PipeServer::NotifyUnlock(Data->AchievementId, unlockTime);
}

static void EOS_CALL OnAchievementsUnlocked(const EOS_Achievements_OnAchievementsUnlockedCallbackInfo* Data) {
    // V1 notification doesn't carry UnlockTime - look up the achievement's
    // existing UnlockTime (set by queryPlayerAchievementsComplete or V2).
    // If still -1 (truly fresh), use current time as best-effort.
    for (uint32_t i = 0; i < Data->AchievementsCount; i++) {
        const char* achId = Data->AchievementIds[i];
        int64_t unlockTime = -1;
        findAchievement(achId, [&unlockTime](Overlay_Achievement& achievement) {
            achievement.UnlockState = UnlockState::Unlocked;
            unlockTime = achievement.UnlockTime;
            if (unlockTime <= 0) {
                // V1 has no timestamp - use current time as best-effort.
                // V2 (if it fires later) will overwrite with the real value.
                unlockTime = (int64_t)time(nullptr);
                achievement.UnlockTime = unlockTime;
            }
        });
        PipeServer::NotifyUnlock(achId, unlockTime);
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
        TaggedClientData taggedClientData = TaggedClientData::tag(achievement);
        EOS_Achievements_UnlockAchievements(getHAchievements(), &Options, taggedClientData.raw, OnUnlockAchievementsComplete);
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
    // NOTE: PipeServer::Start() was previously called here, gated behind
    // query success. That was a bug: when auth fails (EOS_InvalidAuth),
    // the GUI could never connect because the pipe never opened.
    // PipeServer is now started unconditionally in init() so the GUI
    // can connect immediately, even before the user logs in.

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

        // G4: Capture the actual UnlockTime value (POSIX epoch seconds, -1 = not unlocked).
        // Previously we only used this as a boolean (unlocked vs locked).
        int64_t capturedUnlockTime = OutAchievement->UnlockTime;

        if (capturedUnlockTime != -1) {
            findAchievement(OutAchievement->AchievementId, [capturedProgress, thresholdLabel, capturedUnlockTime](Overlay_Achievement& achievement) {
                achievement.UnlockState = UnlockState::Unlocked;
                achievement.Progress = 1.0f;
                achievement.UnlockTime = capturedUnlockTime;  // G4: store the real timestamp
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
                achievement.UnlockTime = -1;  // G4: explicit not-unlocked
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
                        statThresholds,
                        0.0f,       // Progress (filled later by player query)
                        nullptr,    // StatThresholdLabel (filled later)
                        -1          // UnlockTime (G4: -1 = not unlocked)
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
                statThresholds,
                0.0f,       // Progress (filled later by player query)
                nullptr,    // StatThresholdLabel (filled later)
                -1          // UnlockTime (G4: -1 = not unlocked)
            }
        );

        EOS_Achievements_Definition_Release(OutDefinition);
    }

    // ApiVersion=1 — backward-compatible (see ForceAchievementsConfiguration)
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

    // ApiVersion=1 is the safe backward-compatible choice (see ForceAchievementsConfiguration).
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

    // Fix #4: On SDK < 1.14, AddNotifyAchievementsUnlockedV2 doesn't exist.
    // Skip V2 subscription entirely if the feature is not available.
    if (!EOS_Compat::isFeatureAvailable("AchievementsUnlockedV2")) {
        Logger::info("[ACH] SDK < 1.14.0: V2 achievement notifications not available, using deprecated V1");
        useDeprecated = true;
    }

    if (!useDeprecated) {
        try {
            EOS_Achievements_AddNotifyAchievementsUnlockedV2Options NotifyOptions = {};
            NotifyOptions.ApiVersion = EOS_ACHIEVEMENTS_ADDNOTIFYACHIEVEMENTSUNLOCKEDV2_API_LATEST;
            EOS_Achievements_AddNotifyAchievementsUnlockedV2(getHAchievements(), &NotifyOptions, nullptr, OnAchievementsUnlockedV2);
            Logger::debug("[ACH] Subscribed to V2 unlock notifications");
            return;
        } catch (ScreamAPI::FunctionNotFoundException) {
            useDeprecated = true;
        }
    }

    // Deprecated V1 notification subscription
    try {
        EOS_Achievements_AddNotifyAchievementsUnlockedOptions NotifyOptions = {
            EOS_ACHIEVEMENTS_ADDNOTIFYACHIEVEMENTSUNLOCKED_API_LATEST
        };
        EOS_Achievements_AddNotifyAchievementsUnlocked(getHAchievements(), &NotifyOptions, nullptr, OnAchievementsUnlocked);
        Logger::debug("[ACH] Subscribed to deprecated unlock notifications");
    } catch (ScreamAPI::FunctionNotFoundException) {
        Logger::warn("[ACH] Deprecated V1 notification function not found either");
    }
}

// ----------------------------------------------------------------------------
// Initialization
// ----------------------------------------------------------------------------

void init() {
    // Runs with OR without the overlay. The overlay (Overlay::Init, ImGui,
    // kiero, icon loader) is opt-in via Config::EnableOverlay() and is
    // wired up only by initWithOverlay(). This function handles the EOS
    // achievement side: definitions query, player-achievements query,
    // notification subscription. PipeServer starts unconditionally so
    // EpicGUI can connect immediately — even before the user logs in or
    // achievement queries succeed. The GUI will see an empty list until
    // SendUpdatedList() is called from queryPlayerAchievementsComplete.
    static bool initialized = false;

    if (!initialized) {
        initialized = true;
        g_fallbackInitDone.store(true, std::memory_order_relaxed); // prevent TryInitFromFallback
        Logger::info("Achievement Manager: Initializing (overlay state irrelevant)");
        Logger::debug("[ACH] init(): Starting achievement manager initialization");

        // Wire up the Overlay::achievements pointer so PipeServer can read
        // the list even when the overlay is disabled. Overlay::Init (which
        // also sets this pointer) only runs when Config::EnableOverlay() is
        // true. Without this, SendAchList and CmdUnlockAll in PipeServer
        // see a null pointer and send an empty list to the GUI.
        // The overlay-specific parts (kiero, ImGui, DX hooks) are NOT
        // initialized here — only the data pointer is set.
        Overlay::achievements = &achievements;
        Overlay::unlockAchievement = unlockAchievement;
        Logger::debug("[ACH] init(): Set Overlay::achievements=%p, unlockAchievement=%p",
            (void*)Overlay::achievements, (void*)Overlay::unlockAchievement);

        // Start the named-pipe server BEFORE any achievement queries fire.
        // Previously this was called from queryPlayerAchievementsComplete's
        // success branch, which meant that if the EOS backend rejected auth
        // (EOS_InvalidAuth — common during offline / partial-login states),
        // the pipe never opened and the GUI could not connect at all.
        // Starting here guarantees the GUI can always attach, see the log
        // path, and send refresh commands even when queries are failing.
        PipeServer::Start();

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

// -- UE5.4+ OSSv2 Fallback Init --------------------------------------
// Called from eos_achievements.cpp intercept wrappers when fallback handles
// are captured. If the 60-second polling thread already timed out without
// initializing the achievement manager, this triggers init now.
//
// The atomic guard ensures this only fires once, even if multiple intercept
// wrappers call it concurrently. g_fallbackInitDone is also set by init()
// to prevent this from firing after a successful normal-path init.

void TryInitFromFallback(void* hModule) {
    if (g_fallbackInitDone.exchange(true)) return; // already initialized

    Logger::info("[ACH] TryInitFromFallback: triggering achievement manager init from intercept capture");
    if (hModule && Config::EnableOverlay()) {
        initWithOverlay(hModule);
    } else {
        init();
    }
}

} // namespace AchievementManager