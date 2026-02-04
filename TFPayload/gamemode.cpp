#include "pch.h"
#include "gamemode.h"
#include "logging.h"
#include "base-address.h"

namespace GameMode {

// ============================================================================
// Game Memory Addresses - UPLAY VERSION (RVA offsets - Ghidra base 0x700000)
// ============================================================================

// g_pGameManager: Ghidra 0x0174b308, RVA = 0x0174b308 - 0x700000 = 0x104B308
static constexpr uintptr_t G_PGAMEMANAGER_RVA_UPLAY = 0x104B308;

// IsGameModeActive flag: Ghidra 0x016d4f1c, RVA = 0x016d4f1c - 0x700000 = 0xFD4F1C
static constexpr uintptr_t IS_GAMEMODE_ACTIVE_RVA_UPLAY = 0xFD4F1C;

// ============================================================================
// Game Memory Addresses - STEAM VERSION (RVA offsets)
// TODO: Verify these addresses in Steam Ghidra project
// ============================================================================

static constexpr uintptr_t G_PGAMEMANAGER_RVA_STEAM = 0x104D308;  // Estimated based on Uplay pattern - verify!
static constexpr uintptr_t IS_GAMEMODE_ACTIVE_RVA_STEAM = 0xFD6F1C;  // Estimated - verify!

// ============================================================================
// Memory Offsets (consistent across versions)
// ============================================================================

// GameState: *(*(g_pGameManager + 0xD4) + 0x4)
static constexpr uintptr_t GAMESTATE_PTR_OFFSET = 0xD4;
static constexpr uintptr_t GAMESTATE_VALUE_OFFSET = 0x4;

// TrackType: *(*(g_pGameManager + 0x1A8) + 0x14)
static constexpr uintptr_t TRACKTYPE_PTR_OFFSET = 0x1A8;
static constexpr uintptr_t TRACKTYPE_VALUE_OFFSET = 0x14;

// Multiplayer check: *(*(g_pGameManager + 0xEC) + 0x24) == 2
static constexpr uintptr_t MULTIPLAYER_PTR_OFFSET = 0xEC;
static constexpr uintptr_t MULTIPLAYER_VALUE_OFFSET = 0x24;

// ============================================================================
// Helper functions to get correct RVA based on detected version
// ============================================================================

static uintptr_t GetGameManagerRVA() {
    return BaseAddress::IsSteamVersion() ? G_PGAMEMANAGER_RVA_STEAM : G_PGAMEMANAGER_RVA_UPLAY;
}

static uintptr_t GetIsGameModeActiveRVA() {
    return BaseAddress::IsSteamVersion() ? IS_GAMEMODE_ACTIVE_RVA_STEAM : IS_GAMEMODE_ACTIVE_RVA_UPLAY;
}

// ============================================================================
// Local state
// ============================================================================

static DWORD_PTR g_baseAddress = 0;
static bool g_initialized = false;

// Cached pointers (resolved once)
static void** g_ppGameManager = nullptr;
static uint8_t* g_pIsGameModeActive = nullptr;

// ============================================================================
// Safe memory read helpers (isolated for __try/__except compatibility)
// These functions must not use any C++ objects with destructors
// ============================================================================

#pragma warning(push)
#pragma warning(disable: 4733) // Inline asm assigning to 'FS:0' : handler not registered as safe handler

static bool SafeValidatePointers() {
    __try {
        volatile void* test1 = *g_ppGameManager;
        volatile uint8_t test2 = *g_pIsGameModeActive;
        (void)test1;
        (void)test2;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SafeReadGameModeActive(bool* outValue) {
    __try {
        *outValue = (*g_pIsGameModeActive != 0);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        *outValue = false;
        return false;
    }
}

static bool SafeReadGameState(int* outValue) {
    __try {
        void* gameManager = *g_ppGameManager;
        if (!gameManager) {
            *outValue = -1;
            return true; // Not an error, just no manager yet
        }

        void* ptr1 = *(void**)((uint8_t*)gameManager + GAMESTATE_PTR_OFFSET);
        if (!ptr1) {
            *outValue = -1;
            return true;
        }

        *outValue = *(int*)((uint8_t*)ptr1 + GAMESTATE_VALUE_OFFSET);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        *outValue = -1;
        return false;
    }
}

static bool SafeReadTrackType(int* outValue) {
    __try {
        void* gameManager = *g_ppGameManager;
        if (!gameManager) {
            *outValue = -1;
            return true;
        }

        void* ptr1 = *(void**)((uint8_t*)gameManager + TRACKTYPE_PTR_OFFSET);
        if (!ptr1) {
            *outValue = -1;
            return true;
        }

        *outValue = *(int*)((uint8_t*)ptr1 + TRACKTYPE_VALUE_OFFSET);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        *outValue = -1;
        return false;
    }
}

static bool SafeReadMultiplayerValue(int* outValue) {
    __try {
        void* gameManager = *g_ppGameManager;
        if (!gameManager) {
            *outValue = 0;
            return true;
        }

        void* ptr1 = *(void**)((uint8_t*)gameManager + MULTIPLAYER_PTR_OFFSET);
        if (!ptr1) {
            *outValue = 0;
            return true;
        }

        *outValue = *(int*)((uint8_t*)ptr1 + MULTIPLAYER_VALUE_OFFSET);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        *outValue = 0;
        return false;
    }
}

// Debug helper - reads pointer chain and returns debug info
struct DebugPointerInfo {
    uintptr_t ppGameManager;
    uintptr_t gameManager;
    uintptr_t ptr_d4;
    int stateValue;
    bool success;
};

static DebugPointerInfo SafeReadDebugPointers() {
    DebugPointerInfo info = {0};
    info.ppGameManager = (uintptr_t)g_ppGameManager;
    
    __try {
        info.gameManager = (uintptr_t)*g_ppGameManager;
        if (info.gameManager) {
            info.ptr_d4 = (uintptr_t)*(void**)((uint8_t*)info.gameManager + GAMESTATE_PTR_OFFSET);
            if (info.ptr_d4) {
                info.stateValue = *(int*)((uint8_t*)info.ptr_d4 + GAMESTATE_VALUE_OFFSET);
            } else {
                info.stateValue = -999; // ptr_d4 is null
            }
        } else {
            info.ptr_d4 = 0;
            info.stateValue = -998; // gameManager is null
        }
        info.success = true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        info.success = false;
    }
    return info;
}

#pragma warning(pop)

// ============================================================================
// Implementation - Initialization
// ============================================================================

bool Initialize(DWORD_PTR baseAddress) {
    if (g_initialized) {
        return true;
    }

    g_baseAddress = baseAddress;

    // Resolve pointers
    g_ppGameManager = (void**)(g_baseAddress + GetGameManagerRVA());
    g_pIsGameModeActive = (uint8_t*)(g_baseAddress + GetIsGameModeActiveRVA());

    // Validate pointers are readable
    if (!SafeValidatePointers()) {
        LOG_ERROR("[GameMode] Failed to validate memory pointers - addresses may be incorrect");
        if (BaseAddress::IsSteamVersion()) {
            LOG_WARNING("[GameMode] Steam version detected - address estimates may need verification");
        }
        return false;
    }

    g_initialized = true;
    LOG_INFO("[GameMode] Initialized successfully");
    LOG_VERBOSE("[GameMode] g_pGameManager @ 0x" << std::hex << (uintptr_t)g_ppGameManager);
    LOG_VERBOSE("[GameMode] IsGameModeActive @ 0x" << std::hex << (uintptr_t)g_pIsGameModeActive);

    return true;
}

// ============================================================================
// Implementation - Game State
// ============================================================================

GameState GetCurrentGameState() {
    if (!g_initialized || !g_ppGameManager) {
        return GameState::Invalid;
    }

    int stateValue = -1;
    if (!SafeReadGameState(&stateValue)) {
        return GameState::Invalid;
    }

    // Validate range (0-5)
    if (stateValue < 0 || stateValue > 5) {
        return GameState::Invalid;
    }

    return static_cast<GameState>(stateValue);
}

const char* GetGameStateString(GameState state) {
    switch (state) {
        case GameState::Menus:    return "MENUS";
        case GameState::Play:     return "PLAY";
        case GameState::Edit:     return "EDIT";
        case GameState::Replay:   return "REPLAY";
        case GameState::Finished: return "FINISHED";
        case GameState::Editor:   return "EDITOR";
        case GameState::Invalid:  return "INVALID";
        default:                  return "UNKNOWN";
    }
}

const char* GetGameStateString() {
    return GetGameStateString(GetCurrentGameState());
}

bool IsInMenus() {
    return GetCurrentGameState() == GameState::Menus;
}

bool IsPlaying() {
    return GetCurrentGameState() == GameState::Play;
}

bool IsInEditor() {
    GameState state = GetCurrentGameState();
    return state == GameState::Edit || state == GameState::Editor;
}

bool IsWatchingReplay() {
    return GetCurrentGameState() == GameState::Replay;
}

bool IsRaceFinished() {
    return GetCurrentGameState() == GameState::Finished;
}

// ============================================================================
// Implementation - Track Type & Game Mode
// ============================================================================

bool IsGameModeActive() {
    if (!g_initialized || !g_pIsGameModeActive) {
        return false;
    }

    bool active = false;
    SafeReadGameModeActive(&active);
    return active;
}

bool IsInMultiplayerMode() {
    if (!g_initialized || !g_ppGameManager) {
        return false;
    }

    // Check if game mode is active first
    if (!IsGameModeActive()) {
        return false;
    }

    int value = 0;
    if (!SafeReadMultiplayerValue(&value)) {
        return false;
    }

    // Value of 2 means multiplayer
    return value == 2;
}

TrackType GetCurrentTrackType() {
    if (!g_initialized || !g_ppGameManager) {
        return TrackType::Unknown;
    }

    int trackType = -1;
    if (!SafeReadTrackType(&trackType)) {
        return TrackType::Unknown;
    }

    // Validate range
    if (trackType < -1 || trackType > 6) {
        return TrackType::Unknown;
    }

    return static_cast<TrackType>(trackType);
}

const char* GetTrackTypeString(TrackType type) {
    switch (type) {
        case TrackType::Unknown:      return "Unknown";
        case TrackType::SP_Trials:    return "SP_TRIALS";
        case TrackType::SP_Motocross: return "SP_MOTOCROSS";
        case TrackType::MP_Super:     return "MP_SUPER";
        case TrackType::SP_SkillGame: return "SP_SKILLGAME";
        case TrackType::SP_Endurance: return "SP_ENDURANCE";
        case TrackType::FMX:          return "FMX";
        case TrackType::MP_XCross:    return "MP_X_CROSS";
        default:                      return "Invalid";
    }
}

const char* GetTrackTypeString() {
    return GetTrackTypeString(GetCurrentTrackType());
}

bool IsSinglePlayerTrack() {
    TrackType type = GetCurrentTrackType();
    return type == TrackType::SP_Trials || 
           type == TrackType::SP_Motocross || 
           type == TrackType::SP_SkillGame || 
           type == TrackType::SP_Endurance ||
           type == TrackType::FMX;
}

bool IsMultiplayerTrack() {
    TrackType type = GetCurrentTrackType();
    return type == TrackType::MP_Super || type == TrackType::MP_XCross;
}

bool IsSkillGame() {
    return GetCurrentTrackType() == TrackType::SP_SkillGame;
}

bool IsFMX() {
    TrackType type = GetCurrentTrackType();
    return type == TrackType::SP_Motocross || type == TrackType::FMX;
}

// ============================================================================
// Debug
// ============================================================================

void DebugPrintState() {
    LOG_INFO("[GameMode] === Current State ===");
    
    // Debug pointer chain
    DebugPointerInfo dbg = SafeReadDebugPointers();
    LOG_INFO("[GameMode] g_ppGameManager = 0x" << std::hex << dbg.ppGameManager);
    LOG_INFO("[GameMode] *g_ppGameManager (gameManager) = 0x" << std::hex << dbg.gameManager);
    LOG_INFO("[GameMode] *(gameManager + 0xD4) = 0x" << std::hex << dbg.ptr_d4);
    LOG_INFO("[GameMode] *(ptr_d4 + 0x4) = " << std::dec << dbg.stateValue << (dbg.success ? "" : " [EXCEPTION]"));
    LOG_INFO("[GameMode] ---");
    
    LOG_INFO("[GameMode] Game State: " << GetGameStateString() << " (" << static_cast<int>(GetCurrentGameState()) << ")");
    LOG_INFO("[GameMode] Game Mode Active: " << (IsGameModeActive() ? "Yes" : "No"));
    LOG_INFO("[GameMode] In Multiplayer Mode: " << (IsInMultiplayerMode() ? "Yes" : "No"));
    LOG_INFO("[GameMode] Track Type: " << GetTrackTypeString() << " (" << static_cast<int>(GetCurrentTrackType()) << ")");
    LOG_INFO("[GameMode] ---");
    LOG_INFO("[GameMode] IsInMenus: " << (IsInMenus() ? "Yes" : "No"));
    LOG_INFO("[GameMode] IsPlaying: " << (IsPlaying() ? "Yes" : "No"));
    LOG_INFO("[GameMode] IsInEditor: " << (IsInEditor() ? "Yes" : "No"));
    LOG_INFO("[GameMode] IsWatchingReplay: " << (IsWatchingReplay() ? "Yes" : "No"));
    LOG_INFO("[GameMode] IsRaceFinished: " << (IsRaceFinished() ? "Yes" : "No"));
    LOG_INFO("[GameMode] ---");
    LOG_INFO("[GameMode] IsSinglePlayerTrack: " << (IsSinglePlayerTrack() ? "Yes" : "No"));
    LOG_INFO("[GameMode] IsMultiplayerTrack: " << (IsMultiplayerTrack() ? "Yes" : "No"));
    LOG_INFO("[GameMode] IsSkillGame: " << (IsSkillGame() ? "Yes" : "No"));
    LOG_INFO("[GameMode] IsFMX: " << (IsFMX() ? "Yes" : "No"));
}

} // namespace GameMode
