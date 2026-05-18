#pragma once
#include <cstdint>

namespace Camera {
    // Initialize the camera system with the game's base address
    bool Initialize(uintptr_t baseAddress);

    // Shutdown and cleanup
    void Shutdown();

    // Cycle to the next camera mode (0 -> 1 -> 2 -> 0)
    // This calls the game's CycleHUD function directly
    bool CycleMode();

    // Check for hotkeys (O = cycle camera mode)
    void CheckHotkey();
}
