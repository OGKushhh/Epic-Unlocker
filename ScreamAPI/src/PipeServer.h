#pragma once
#include <string>

namespace PipeServer {
    void SetLogPath(const std::wstring& path); // Call right after Logger::init
    void Start();          // Call after achievements are loaded
    void Stop();           // Call from ScreamAPI::destroy()
    void NotifyUnlock(const char* achievementId, int64_t unlockTime);  // Call after each unlock (G4: with server-provided timestamp)
    void SendUpdatedList();  // Call from queryPlayerAchievementsComplete when refresh is complete
}
