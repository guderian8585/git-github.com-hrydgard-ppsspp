// SPDX-FileCopyrightText: 2019-2022 Connor McLaughlin <stenzek@gmail.com>
// SPDX-License-Identifier: (GPL-2.0 OR GPL-3.0 OR CC-BY-NC-ND-4.0)

// Derived from Duckstation's RetroAchievements implementation by stenzek as can be seen
// above, relicensed to GPL 2.0.
// Modifications and deletions have been made where needed.
// Refactoring it into a more PPSSPP-like style may or may not be a good idea -
// it'll make it harder to copy new achievement features from Duckstation.

#pragma once

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <set>
#include <mutex>

#include "Common/StringUtils.h"
#include "Common/CommonTypes.h"

#include "ext/rcheevos/include/rc_client.h"

class Path;
class PointerWrap;
class FileLoader;

namespace Achievements {

struct Statistics {
	// Debug stats
	int badMemoryAccessCount;
};

// RAIntegration only exists for Windows, so no point checking it on other platforms.
#ifdef WITH_RAINTEGRATION

bool IsUsingRAIntegration();

#else

static inline bool IsUsingRAIntegration()
{
	return false;
}

#endif

// Returns true if the user is logged in properly, and everything is set up for playing games with achievements.
bool IsLoggedIn();

// Returns true if in a game, and achievements are active in the current game.
bool IsActive();

// Returns true if unofficial achievements are enabled.
bool UnofficialEnabled();

// Returns true if the emulator should hold off on executing game code, such as during game identification.
bool IsBlockingExecution();

// Returns true if features such as save states should be disabled.
// This should only be used for controlling functionality of the following things, which are banned in Challenge/Hardcore mode:
//
// * Savestates
// * Slowdown time (though hard to fully prevent, could use crazy post shaders or software rendering...)
bool ChallengeModeActive();

// Same as ChallengeModeActive but comes with a convenient user message. Don't use for every-frame checks or UI enablement,
// only for shortcut keys and commands. You should look up the message in I18NCat::ACHIEVEMENTS.
// If no message is specified, a standard "This feature is not available in Challenge Mode" message will be shown.
// Also returns true if challenge mode is active.
bool WarnUserIfChallengeModeActive(const char *message = nullptr);

// The new API is so much nicer that we can use it directly instead of wrapping it. So let's expose the client.
// Will of course return nullptr if not active.
rc_client_t *GetClient();

void Initialize();
void UpdateSettings();

/// Called when the system is being shut down. If Shutdown() returns false, the shutdown should be aborted if possible.
bool Shutdown();

void DownloadImageIfMissing(const std::string &cache_key, std::string &&url);

/// Called once a frame at vsync time on the CPU thread, during gameplay.
void FrameUpdate();

/// Called every frame to let background processing happen.
void Idle();

/// Saves/loads state.
void DoState(PointerWrap &p);

/// Returns true if the current game has any achievements or leaderboards.
bool HasAchievementsOrLeaderboards();

bool LoginAsync(const char *username, const char *password);
void Logout();

void SetGame(const Path &path, FileLoader *fileLoader);
void ChangeUMD(const Path &path);  // for in-game UMD change
void UnloadGame();  // Call when leaving a game.

Statistics GetStatistics();

std::string GetGameAchievementSummary();

std::set<uint32_t> GetActiveChallengeIDs();

} // namespace Achievements
