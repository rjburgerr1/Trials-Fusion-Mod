#pragma once
#include <cstdint>

namespace Pause {
    // Initialize the pause system with the game's base address
    bool Initialize(uintptr_t baseAddress);

    // Shutdown and cleanup
    void Shutdown();

    // Toggle pause state (pause if playing, resume if paused)
    void TogglePause();

    // Check for hotkey press (0 key)
    void CheckHotkey();

    // Get current pause state
    bool IsPaused();
}
