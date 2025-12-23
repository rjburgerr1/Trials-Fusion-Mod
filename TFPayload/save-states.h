#pragma once
#include <cstdint>

namespace SaveStates {
    // Initialize the save state system with the game's base address
    bool Initialize(uintptr_t baseAddress);

    // Shutdown and cleanup
    void Shutdown();

    // Save the current game state to a slot (0-9)
    bool SaveState(int slot);

    // Load a saved state from a slot (0-9)
    bool LoadState(int slot);

    // Quick save to slot 0
    bool QuickSave();

    // Quick load from slot 0
    bool QuickLoad();

    // Check if a slot has a saved state
    bool HasSavedState(int slot);

    // Clear a save slot
    void ClearSlot(int slot);

    // Clear all save slots
    void ClearAllSlots();

    // Check for hotkeys (F5=quick save, F6=quick load, 1-9 for slots)
    void CheckHotkeys();

    // Get info about save state system
    struct SaveStateInfo {
        int totalSlots;
        int usedSlots;
        size_t memoryPerSlot;
        size_t totalMemoryUsed;
    };
    SaveStateInfo GetInfo();
}
