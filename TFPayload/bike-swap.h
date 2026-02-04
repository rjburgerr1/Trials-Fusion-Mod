#pragma once
#include <cstdint>
#include <vector>
#include <string>

namespace BikeSwap {
    // =============================================================================
    // PUBLIC API
    // =============================================================================
    
    // Initialize the bike swap system with the game's base address
    bool Initialize(uintptr_t baseAddress);

    // Shutdown and cleanup
    void Shutdown();

    // Check if bike swap is currently available (must be in a race)
    bool IsSwapAvailable();

    // Get the current bike ID (0-based index)
    int GetCurrentBikeId();

    // Get total number of available bikes
    int GetTotalBikeCount();

    // Swap to a specific bike by ID (0-based index)
    // Returns true if successful, false if bike ID is invalid or swap failed
    bool SwapToBike(int bikeId);

    // Swap to the next bike in the list (wraps around)
    bool SwapToNextBike();

    // Swap to the previous bike in the list (wraps around)
    bool SwapToPreviousBike();

    // Get the name of a bike by ID (returns empty string if invalid)
    std::string GetBikeName(int bikeId);

    // Get the name of the current bike
    std::string GetCurrentBikeName();

    // Process bike swap hotkeys
    void CheckHotkey();

    // Debug function to dump bike list info
    void DebugDumpBikeInfo();

    // Manual step-by-step bike swap for debugging (calls each function individually)
    bool SwapToBikeManual(int bikeId);

    // Simple bike swap - changes bike ID and triggers respawn (safer, uses game's main thread)
    bool SwapToBikeSimple(int bikeId);
}
