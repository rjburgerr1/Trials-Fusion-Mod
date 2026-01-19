#include "pch.h"
#include "pause.h"
#include "logging.h"
#include "keybindings.h"
#include "base-address.h"
#include <iostream>
#include <Windows.h>

namespace Pause {
    // Function pointer types matching the game's calling convention
    typedef void(__fastcall* PauseGameCallback_t)(void* inGameServicePtr);
    typedef void(__fastcall* ResumeGameCallback_t)(void* inGameServicePtr);

    // ============================================================================
    // RVA offsets - Version-specific addresses
    // Ghidra base: Uplay = 0x700000, Steam = 0x140000
    // ============================================================================
    namespace Uplay {
        // PauseGameCallback: Ghidra 0x00B67950 - 0x700000 = 0x467950
        static constexpr uintptr_t PAUSE_CALLBACK_RVA = 0x467950;
        // ResumeGameCallback: Ghidra 0x00B67960 - 0x700000 = 0x467960
        static constexpr uintptr_t RESUME_CALLBACK_RVA = 0x467960;
        // g_pGameManager: Ghidra 0x0174B308 - 0x700000 = 0x104B308
        static constexpr uintptr_t GLOBAL_STRUCT_RVA = 0x104b308;
    }
    
    namespace Steam {
        // PauseGameCallback: Ghidra 0x005A72C0 - 0x140000 = 0x4672C0
        static constexpr uintptr_t PAUSE_CALLBACK_RVA = 0x4672C0;
        // ResumeGameCallback: Ghidra 0x005A72D0 - 0x140000 = 0x4672D0
        static constexpr uintptr_t RESUME_CALLBACK_RVA = 0x4672D0;
        // g_pGameManager: Ghidra 0x0118D308 - 0x140000 = 0x104D308
        static constexpr uintptr_t GLOBAL_STRUCT_RVA = 0x104d308;
    }

    // Runtime addresses
    static uintptr_t g_baseAddress = 0;
    static PauseGameCallback_t g_pauseCallback = nullptr;
    static ResumeGameCallback_t g_resumeCallback = nullptr;
    static void** g_globalStructPtr = nullptr;

    // State tracking
    static bool g_initialized = false;

    bool Initialize(uintptr_t baseAddress) {
        if (g_initialized) {
            LOG_VERBOSE("[Pause] Already initialized");
            return true;
        }

        if (baseAddress == 0) {
            LOG_ERROR("[Pause] Invalid base address");
            return false;
        }

        g_baseAddress = baseAddress;

        // Select version-specific RVAs
        uintptr_t pauseCallbackRVA;
        uintptr_t resumeCallbackRVA;
        uintptr_t globalStructRVA;
        
        if (BaseAddress::IsSteamVersion()) {
            LOG_VERBOSE("[Pause] Steam version detected - using Steam addresses");
            pauseCallbackRVA = Steam::PAUSE_CALLBACK_RVA;
            resumeCallbackRVA = Steam::RESUME_CALLBACK_RVA;
            globalStructRVA = Steam::GLOBAL_STRUCT_RVA;
        } else {
            LOG_VERBOSE("[Pause] Uplay version detected - using Uplay addresses");
            pauseCallbackRVA = Uplay::PAUSE_CALLBACK_RVA;
            resumeCallbackRVA = Uplay::RESUME_CALLBACK_RVA;
            globalStructRVA = Uplay::GLOBAL_STRUCT_RVA;
        }

        // Calculate addresses
        g_pauseCallback = reinterpret_cast<PauseGameCallback_t>(baseAddress + pauseCallbackRVA);
        g_resumeCallback = reinterpret_cast<ResumeGameCallback_t>(baseAddress + resumeCallbackRVA);
        g_globalStructPtr = reinterpret_cast<void**>(baseAddress + globalStructRVA);

        LOG_VERBOSE("[Pause] PauseCallback at 0x" << std::hex << (baseAddress + pauseCallbackRVA));
        LOG_VERBOSE("[Pause] ResumeCallback at 0x" << std::hex << (baseAddress + resumeCallbackRVA));
        LOG_VERBOSE("[Pause] GlobalStruct at 0x" << std::hex << (baseAddress + globalStructRVA));

        // Verify addresses are valid
        if (IsBadReadPtr(g_globalStructPtr, sizeof(void*))) {
            LOG_ERROR("[Pause] Invalid global struct pointer at 0x" << std::hex << (baseAddress + globalStructRVA));
            return false;
        }

        g_initialized = true;
        LOG_VERBOSE("[Pause] Initialized successfully");
        return true;
    }

    void Shutdown() {
        if (!g_initialized) {
            return;
        }

        LOG_VERBOSE("[Pause] Shutting down...");
        
        g_initialized = false;
        g_pauseCallback = nullptr;
        g_resumeCallback = nullptr;
        g_globalStructPtr = nullptr;
    }

    void TogglePause() {
        if (!g_initialized) {
            LOG_ERROR("[Pause] Not initialized!");
            return;
        }

        // Get the InGameService pointer from the global structure
        void* globalStruct = *g_globalStructPtr;
        if (!globalStruct || IsBadReadPtr(globalStruct, sizeof(void*))) {
            LOG_ERROR("[Pause] Invalid global structure pointer");
            return;
        }

        // The InGameService pointer is at offset +0x174 in the global structure
        void* inGameServicePtr = *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(globalStruct) + 0x174);
        
        if (!inGameServicePtr || IsBadReadPtr(inGameServicePtr, 0x2f0)) {
            LOG_ERROR("[Pause] Invalid InGameService pointer");
            return;
        }

        // Read the current pause state from the game
        uint8_t currentPauseFlag = *reinterpret_cast<uint8_t*>(reinterpret_cast<uintptr_t>(inGameServicePtr) + 0x2e8);
        
        // Toggle based on ACTUAL game state
        if (currentPauseFlag == 0) {
            // Game is not paused, so pause it
            LOG_VERBOSE("[Pause] Pause Game");
            g_pauseCallback(inGameServicePtr);
        } else {
            // Game is paused, so resume it
            LOG_VERBOSE("[Pause] Resume Game");
            g_resumeCallback(inGameServicePtr);
        }

        // Read back the pause flag to verify
        uint8_t newPauseFlag = *reinterpret_cast<uint8_t*>(reinterpret_cast<uintptr_t>(inGameServicePtr) + 0x2e8);
    }

    void CheckHotkey() {
        if (!g_initialized) {
            return;
        }

        // Use the keybindings system instead of hardcoded key
        if (Keybindings::IsActionPressed(Keybindings::Action::TogglePause)) {
            std::string keyName = Keybindings::GetKeyName(Keybindings::GetKey(Keybindings::Action::TogglePause));
            LOG_VERBOSE("[Pause] '" << keyName << "' key pressed - toggling pause");
            TogglePause();
        }
    }

    bool IsPaused() {
        if (!g_initialized) {
            return false;
        }

        // Get the InGameService pointer from the global structure
        void* globalStruct = *g_globalStructPtr;
        if (!globalStruct || IsBadReadPtr(globalStruct, sizeof(void*))) {
            return false;
        }

        void* inGameServicePtr = *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(globalStruct) + 0x174);
        if (!inGameServicePtr || IsBadReadPtr(inGameServicePtr, 0x2f0)) {
            return false;
        }

        // Read the actual pause state from the game
        uint8_t pauseFlag = *reinterpret_cast<uint8_t*>(reinterpret_cast<uintptr_t>(inGameServicePtr) + 0x2e8);
        return pauseFlag != 0;
    }
}
