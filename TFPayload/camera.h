#pragma once
#include <cstdint>

namespace Camera {
    // Initialize the camera system with the game's base address
    bool Initialize(uintptr_t baseAddress);

    // Shutdown and cleanup
    void Shutdown();

    // Set camera mode to a specific value
    // Mode 0 = Follow Camera
    // Mode 1 = Automatic Camera  
    // Mode 2 = Free Camera
    // This replicates what the in-game keybind does (uses 0x64/0x28 callback system)
    bool SetMode(int mode);

    // Cycle to the next camera mode (0 -> 1 -> 2 -> 0)
    // This calls the game's CycleHUD function directly
    bool CycleMode();

    // Check for hotkeys (O = cycle camera mode)
    void CheckHotkey();
}
