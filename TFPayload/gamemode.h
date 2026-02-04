#pragma once
#include <Windows.h>
#include <cstdint>
#include <string>

namespace GameMode {

// ============================================================================
// Game State enumeration - The high-level state of the game
// Stored at: *(*(g_pGameManager + 0xd4) + 4)
// ============================================================================
enum class GameState : int {
    Menus = 0,      // In menus (main menu, garage, track select, etc.)
    Play = 1,       // Playing a track
    Edit = 2,       // In track editor (editing mode)
    Replay = 3,     // Watching a replay
    Finished = 4,   // Race finished (showing results)
    Editor = 5,     // In editor (possibly test ride?)
    Invalid = -1    // Unknown/invalid state
};

// ============================================================================
// Track Type enumeration - What kind of track is loaded
// Stored at: *(*(g_pGameManager + 0x1a8) + 0x14)
// ============================================================================
enum class TrackType : int {
    Unknown = -1,
    SP_Trials = 0,        // Single Player Trials (normal tracks)
    SP_Motocross = 1,     // Single Player Motocross (FMX tracks)
    MP_Super = 2,         // Multiplayer Supercross
    SP_SkillGame = 3,     // Skill Games
    SP_Endurance = 4,     // Endurance tracks (long tracks)
    FMX = 5,              // FMX-XXX
    MP_XCross = 6         // Multiplayer Cross
};

// Initialize the game mode tracking system
bool Initialize(DWORD_PTR baseAddress);

// ============================================================================
// Game State functions
// ============================================================================

// Get the current high-level game state
GameState GetCurrentGameState();

// Get a string representation of the game state
const char* GetGameStateString();
const char* GetGameStateString(GameState state);

// Convenience checks for game state
bool IsInMenus();
bool IsPlaying();
bool IsInEditor();
bool IsWatchingReplay();
bool IsRaceFinished();

// ============================================================================
// Track Type functions (existing)
// ============================================================================

// Check if a game mode is currently active (in a track/race)
bool IsGameModeActive();

// Check if currently in multiplayer mode
bool IsInMultiplayerMode();

// Get the current track type
TrackType GetCurrentTrackType();

// Get a string representation of the current track type
const char* GetTrackTypeString();
const char* GetTrackTypeString(TrackType type);

// Check if current mode is single player track type
bool IsSinglePlayerTrack();

// Check if current mode is multiplayer track type
bool IsMultiplayerTrack();

// Check if current mode is a skill game
bool IsSkillGame();

// Check if current mode is FMX related
bool IsFMX();

// ============================================================================
// Debug
// ============================================================================

// Debug: Print current game mode state
void DebugPrintState();

} // namespace GameMode
