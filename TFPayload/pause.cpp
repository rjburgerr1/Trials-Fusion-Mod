#include "pch.h"
#include "pause.h"
#include "logging.h"
#include <iostream>
#include <Windows.h>

namespace Pause {
    // Function pointer types matching the game's calling convention
    typedef void(__fastcall* PauseGameCallback_t)(void* inGameServicePtr);
    typedef void(__fastcall* ResumeGameCallback_t)(void* inGameServicePtr);

    // RVA offsets
    static constexpr uintptr_t PAUSE_CALLBACK_RVA = 0x467950;
    static constexpr uintptr_t RESUME_CALLBACK_RVA = 0x467960;
    static constexpr uintptr_t GLOBAL_STRUCT_RVA = 0x104b308;

    // Runtime addresses
    static uintptr_t g_baseAddress = 0;
    static PauseGameCallback_t g_pauseCallback = nullptr;
    static ResumeGameCallback_t g_resumeCallback = nullptr;
    static void** g_globalStructPtr = nullptr;

    // State tracking
    static bool g_initialized = false;
    static bool g_key0WasPressed = false;

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

        // Calculate addresses
        g_pauseCallback = reinterpret_cast<PauseGameCallback_t>(baseAddress + PAUSE_CALLBACK_RVA);
        g_resumeCallback = reinterpret_cast<ResumeGameCallback_t>(baseAddress + RESUME_CALLBACK_RVA);
        g_globalStructPtr = reinterpret_cast<void**>(baseAddress + GLOBAL_STRUCT_RVA);

        // Verify addresses are valid
        if (IsBadReadPtr(g_globalStructPtr, sizeof(void*))) {
            LOG_ERROR("[Pause] Invalid global struct pointer at 0x" << std::hex << (baseAddress + GLOBAL_STRUCT_RVA));
            return false;
        }

        g_initialized = true;
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
        g_key0WasPressed = false;
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

        // Check if '0' key is pressed (VK code is 0x30)
        bool key0IsPressed = (GetAsyncKeyState(0x30) & 0x8000) != 0;

        // Only trigger on key press (not held)
        if (key0IsPressed && !g_key0WasPressed) {
            LOG_VERBOSE("[Pause] '0' key pressed - toggling pause");
            TogglePause();
        }

        g_key0WasPressed = key0IsPressed;
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
