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

    // Copy the current 32-byte live appearance block from bike+0x9ec.
    bool GetCurrentAppearanceData(uint16_t outAppearance[16]);

    // Write the current bike's 32-byte live appearance block without rebuilding visuals.
    bool WriteCurrentAppearanceData(const uint16_t appearanceData[16]);

    // Queue a main-thread pass that writes the appearance block and reapplies tint slots only.
    bool QueueCurrentAppearanceTintRefresh(const uint16_t appearanceData[16]);

    // Queue a same-bike visual rebuild using caller-supplied appearance data.
    bool QueueCurrentAppearanceReload(const uint16_t appearanceData[16]);

    // Queue a narrower same-bike visual rebuild that skips settings/state/rider setup.
    bool QueueCurrentVisualOnlyReload(const uint16_t appearanceData[16]);

    // Staged swap/status shown by the render overlay after a bike swap
    bool GetSwapStatus(float* secondsRemaining, float* progress01, std::string* statusText);

    // Process bike swap hotkeys
    void CheckHotkey();

    // Debug function to dump bike list info
    void DebugDumpBikeInfo();
}
