#pragma once
#include "pch.h"
#include "Overlay_types.h"
#include <mutex>
#include <atomic>

namespace AchievementManager {

// init(): EOS-side achievement manager init. Runs with OR without overlay.
//   - Queries achievement definitions + player achievements
//   - Subscribes to EOS unlock notifications (V2 if SDK >= 1.14, else V1)
//   - PipeServer starts automatically once player achievements are loaded
//     so EpicGUI keeps working when the in-game overlay is disabled.
//
// initWithOverlay(): calls Overlay::Init (DX11/DX12 ImGui layer, kiero,
//   icon downloader) AND then init(). Use only when Config::EnableOverlay()
//   is true; otherwise call init() directly.
void init();
void initWithOverlay(void* hModule);

// TryInitFromFallback(): Called from intercept wrappers when UE5.4+ OSSv2
//   fallback handles are captured. Triggers init() or initWithOverlay()
//   if the polling thread already timed out without initializing.
//   Safe to call multiple times (atomic guard). Must be called AFTER
//   ScreamAPI::init() has set up the polling thread.
void TryInitFromFallback(void* hModule);

void findAchievement(const char* achievementID, std::function<void(Overlay_Achievement&)> callback);
void unlockAchievement(Overlay_Achievement* achievement);
void refresh();   // manually refresh definitions and player achievements
std::mutex& GetAchievementsMutex();

}