#pragma once
#include <Windows.h>

// ActionScript messaging system for Trials Fusion
// This allows calling ActionScript methods from C++ code

namespace ActionScript {
    // Base addresses for the game in Ghidra
    constexpr uintptr_t UPLAY_GAME_BASE = 0x00700000;
    constexpr uintptr_t STEAM_GAME_BASE = 0x00140000;
    
    // Legacy alias for backward compatibility
    constexpr uintptr_t GAME_BASE = UPLAY_GAME_BASE;

    namespace Uplay {
        constexpr uintptr_t MESSAGE_HANDLER_PTR = 0x0174b308 - UPLAY_GAME_BASE;
        constexpr uintptr_t ALLOCATE_MEMORY_ADDR = 0x0070c340 - UPLAY_GAME_BASE;
        constexpr uintptr_t CREATE_MESSAGE_ADDR = 0x0074a4d0 - UPLAY_GAME_BASE;
        constexpr uintptr_t CREATE_MESSAGE_2PARAMS_ADDR = 0x0074a1b0 - UPLAY_GAME_BASE;
        constexpr uintptr_t CREATE_MESSAGE_PARAMS_ADDR = 0x0074a2f0 - UPLAY_GAME_BASE;
        constexpr uintptr_t SEND_MESSAGE_ADDR = 0x00d7a040 - UPLAY_GAME_BASE;
        constexpr uintptr_t SET_BOOL_VALUE_ADDR = 0x00ccf670 - UPLAY_GAME_BASE;
        constexpr uintptr_t SET_FLOAT_VALUE_ADDR = 0x00ccf5b0 - UPLAY_GAME_BASE;
        constexpr uintptr_t INIT_STRING_EMPTY_ADDR = 0x00ccfc60 - UPLAY_GAME_BASE;
        constexpr uintptr_t SET_STRING_FROM_CSTR_ADDR = 0x00ccfa60 - UPLAY_GAME_BASE;
        constexpr uintptr_t SET_GAME_STATE_ADDR = 0x00946400 - UPLAY_GAME_BASE;
        constexpr uintptr_t HANDLE_RACE_FINISH_ADDR = 0x00976e80 - UPLAY_GAME_BASE;
        constexpr uintptr_t RESET_TO_CHECKPOINT_ADDR = 0x0092f0a0 - UPLAY_GAME_BASE;
        constexpr uintptr_t HANDLE_PLAYER_RESPAWN_ADDR = 0x00905ae0 - UPLAY_GAME_BASE;
        constexpr uintptr_t GET_FIRST_ENTITY_ADDR = 0x0095f000 - UPLAY_GAME_BASE;
        constexpr uintptr_t GAME_MANAGER_PTR = 0x0174b308 - UPLAY_GAME_BASE;
    }
    
    namespace Steam {
        // All core functions mapped from Steam Ghidra dump
        constexpr uintptr_t MESSAGE_HANDLER_PTR = 0x0118d308 - STEAM_GAME_BASE;  // Same as GAME_MANAGER_PTR = 0x0104d308
        constexpr uintptr_t ALLOCATE_MEMORY_ADDR = 0x0014c530 - STEAM_GAME_BASE;  // AllocateMemory = 0x0000c530
        constexpr uintptr_t CREATE_MESSAGE_ADDR = 0x0018a6a0 - STEAM_GAME_BASE;  // CreateMessageObject = 0x0004a6a0
        constexpr uintptr_t CREATE_MESSAGE_2PARAMS_ADDR = 0x0018a380 - STEAM_GAME_BASE;  // CreateMessage2Params = 0x0004a380
        constexpr uintptr_t CREATE_MESSAGE_PARAMS_ADDR = 0x0018a4c0 - STEAM_GAME_BASE;  // CreateMessageParams = 0x0004a4c0
        constexpr uintptr_t SEND_MESSAGE_ADDR = 0x007b8b10 - STEAM_GAME_BASE;  // SendMessage = 0x00678b10
        constexpr uintptr_t SET_BOOL_VALUE_ADDR = 0x0070e890 - STEAM_GAME_BASE;  // SetBoolValue = 0x005ce890
        constexpr uintptr_t SET_FLOAT_VALUE_ADDR = 0x0070e7d0 - STEAM_GAME_BASE;  // SetFloatValue = 0x005ce7d0
        constexpr uintptr_t INIT_STRING_EMPTY_ADDR = 0x0070ee80 - STEAM_GAME_BASE;  // InitStringEmpty = 0x005cee80
        constexpr uintptr_t SET_STRING_FROM_CSTR_ADDR = 0x0070ec80 - STEAM_GAME_BASE;  // SetStringFromCStr = 0x005cec80
        constexpr uintptr_t SET_GAME_STATE_ADDR = 0x00385b90 - STEAM_GAME_BASE;  // SetGameState = 0x00245b90
        constexpr uintptr_t HANDLE_RACE_FINISH_ADDR = 0x003b6900 - STEAM_GAME_BASE;  // HandleRaceFinish = 0x276900
        constexpr uintptr_t RESET_TO_CHECKPOINT_ADDR = 0x0036e8b0 - STEAM_GAME_BASE;  // ResetToCheckpoint = 0x0022e8b0
        constexpr uintptr_t HANDLE_PLAYER_RESPAWN_ADDR = 0x00345420 - STEAM_GAME_BASE;  // HandlePlayerRespawn = 0x00205420
        constexpr uintptr_t GET_FIRST_ENTITY_ADDR = 0x0039eb20 - STEAM_GAME_BASE;  // GetFirstEntity = 0x0025eb20
        constexpr uintptr_t GAME_MANAGER_PTR = 0x0118d308 - STEAM_GAME_BASE;  // DAT_0118d308 from decompilation
    }
    
    // Message handler pointer (at DAT_0174b308 + 0x100)
    constexpr uintptr_t MESSAGE_HANDLER_PTR = Uplay::MESSAGE_HANDLER_PTR;
    constexpr uintptr_t MESSAGE_HANDLER_OFFSET = 0x100;
    
    // Function addresses (Ghidra addresses converted to RVAs) - UPLAY VERSION
    constexpr uintptr_t ALLOCATE_MEMORY_ADDR = Uplay::ALLOCATE_MEMORY_ADDR;
    constexpr uintptr_t CREATE_MESSAGE_ADDR = Uplay::CREATE_MESSAGE_ADDR;
    constexpr uintptr_t CREATE_MESSAGE_2PARAMS_ADDR = Uplay::CREATE_MESSAGE_2PARAMS_ADDR;  // CreateMessageAndSend (2 params)
    constexpr uintptr_t CREATE_MESSAGE_PARAMS_ADDR = Uplay::CREATE_MESSAGE_PARAMS_ADDR;
    constexpr uintptr_t SEND_MESSAGE_ADDR = Uplay::SEND_MESSAGE_ADDR;
    constexpr uintptr_t SET_BOOL_VALUE_ADDR = Uplay::SET_BOOL_VALUE_ADDR;
    constexpr uintptr_t SET_FLOAT_VALUE_ADDR = Uplay::SET_FLOAT_VALUE_ADDR;
    constexpr uintptr_t INIT_STRING_EMPTY_ADDR = Uplay::INIT_STRING_EMPTY_ADDR;
    constexpr uintptr_t SET_STRING_FROM_CSTR_ADDR = Uplay::SET_STRING_FROM_CSTR_ADDR;
    constexpr uintptr_t SET_GAME_STATE_ADDR = Uplay::SET_GAME_STATE_ADDR;  // Function that sets race state
    constexpr uintptr_t HANDLE_RACE_FINISH_ADDR = Uplay::HANDLE_RACE_FINISH_ADDR;  // HandleRaceFinish function
    constexpr uintptr_t RESET_TO_CHECKPOINT_ADDR = Uplay::RESET_TO_CHECKPOINT_ADDR;  // ResetAllRidersToCheckpoint function
    constexpr uintptr_t HANDLE_PLAYER_RESPAWN_ADDR = Uplay::HANDLE_PLAYER_RESPAWN_ADDR;  // HandlePlayerRespawn function
    constexpr uintptr_t GET_FIRST_ENTITY_ADDR = Uplay::GET_FIRST_ENTITY_ADDR;  // GetFirstEntityFromList function
    
    // Message structure size
    constexpr size_t MESSAGE_SIZE = 0x134;
    
    // Parameter structure sizes (from decompilation)
    constexpr size_t PARAM_SIZE = 0x34; // Size of each parameter buffer (52 bytes)
    
    // Message channel for UI commands
    constexpr int UI_CHANNEL = 6;
    
    // Function type definitions
    typedef void* (__cdecl* AllocateMemory_t)(int size, int param2);
    typedef void* (__thiscall* CreateMessageObject_t)(void* thisPtr, const char* methodName);
    typedef void* (__thiscall* CreateMessage2Params_t)(void* thisPtr, const char* methodName, void* param1, void* param2);
    typedef void* (__thiscall* CreateMessageWithParameters_t)(void* thisPtr, const char* methodName, void* param1, void* param2, void* param3);
    typedef void (__thiscall* SendMessage_t)(void* thisPtr, int messagePtr, int channel, int param3);
    typedef void (__thiscall* SetBoolValue_t)(void* thisPtr, bool value);
    typedef void (__thiscall* SetFloatValue_t)(void* thisPtr, float value);
    typedef void (__fastcall* InitStringEmpty_t)(void* param1);
    typedef void (__thiscall* SetStringFromCStr_t)(void* thisPtr, const char* str);
    typedef void (__thiscall* SetGameState_t)(void* thisPtr, int state);
    typedef void (__thiscall* HandleRaceFinish_t)(void* thisPtr, int param_1, void* param_2, void* param_3);
    typedef void* (__fastcall* GetFirstEntity_t)(void* gameManager);
    
    // Initialize the ActionScript messaging system
    bool Initialize(uintptr_t baseAddress);
    
    // Show/hide loading screen
    // loadingText: text to display (e.g., "MENU_GENERAL_LOADING")
    bool ShowLoadingScreen(bool show, const char* loadingText = "MENU_GENERAL_LOADING");
    
    // Show start countdown (like race countdown)
    // countdownValue: the number to show (3, 2, 1, 0, or -1)
    bool ShowStartCountdown(int countdownValue = 3);
    
    // Call the native game function directly (bypasses ActionScript)
    bool ShowStartCountdownDirect(int countdownValue = 3);
    
    // Show a full countdown sequence (3, 2, 1, GO) with delays
    bool ShowFullCountdownSequence();
    
    // Directly finish the race by setting game state to 9 (RACE_FINISHED)
    bool FinishRaceDirect();
    
    // Call HandleRaceFinish directly (tries both state 6->finish and direct finish)
    bool CallHandleRaceFinish();
    
    // Get the message handler pointer
    void* GetMessageHandler();
}
