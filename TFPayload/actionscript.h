#pragma once
#include <Windows.h>

// ActionScript messaging system for Trials Fusion
// This allows calling ActionScript methods from C++ code

namespace ActionScript {
    // Base address for the game in Ghidra
    constexpr uintptr_t GAME_BASE = 0x00700000;
    
    // Message handler pointer (at DAT_0174b308 + 0x100)
    constexpr uintptr_t MESSAGE_HANDLER_PTR = 0x0174b308 - GAME_BASE;
    constexpr uintptr_t MESSAGE_HANDLER_OFFSET = 0x100;
    
    // Function addresses (Ghidra addresses converted to RVAs)
    constexpr uintptr_t ALLOCATE_MEMORY_ADDR = 0x0070c340 - GAME_BASE;
    constexpr uintptr_t CREATE_MESSAGE_ADDR = 0x0074a4d0 - GAME_BASE;
    constexpr uintptr_t CREATE_MESSAGE_2PARAMS_ADDR = 0x0074a1b0 - GAME_BASE;  // CreateMessageAndSend (2 params)
    constexpr uintptr_t CREATE_MESSAGE_PARAMS_ADDR = 0x0074a2f0 - GAME_BASE;
    constexpr uintptr_t SEND_MESSAGE_ADDR = 0x00d7a040 - GAME_BASE;
    constexpr uintptr_t SET_BOOL_VALUE_ADDR = 0x00ccf670 - GAME_BASE;
    constexpr uintptr_t SET_FLOAT_VALUE_ADDR = 0x00ccf5b0 - GAME_BASE;
    constexpr uintptr_t INIT_STRING_EMPTY_ADDR = 0x00ccfc60 - GAME_BASE;
    constexpr uintptr_t SET_STRING_FROM_CSTR_ADDR = 0x00ccfa60 - GAME_BASE;
    constexpr uintptr_t SET_GAME_STATE_ADDR = 0x00946400 - GAME_BASE;  // Function that sets race state
    constexpr uintptr_t HANDLE_RACE_FINISH_ADDR = 0x00976e80 - GAME_BASE;  // HandleRaceFinish function
    constexpr uintptr_t RESET_TO_CHECKPOINT_ADDR = 0x0092f0a0 - GAME_BASE;  // ResetAllRidersToCheckpoint function
    constexpr uintptr_t HANDLE_PLAYER_RESPAWN_ADDR = 0x00905ae0 - GAME_BASE;  // HandlePlayerRespawn function
    constexpr uintptr_t GET_FIRST_ENTITY_ADDR = 0x0095f000 - GAME_BASE;  // GetFirstEntityFromList function
    
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
    bool Initialize();
    
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
