#pragma once
#include <cstdint>

namespace SaveBike {
    // Actual bike entity physics state
    struct BikeEntityState {
        // Bike physics state (0x000 to 0xA00)
        // Includes steering, forces, velocities, and other physics
        uint8_t fullState[0xA00];  // ~2.5KB
        
        // Metadata
        bool isValid;
        uint64_t timestamp;
        
        BikeEntityState();
        void Clear();
        bool IsValid() const { return isValid; }
    };

    // Initialize the bike save system
    bool Initialize(uintptr_t baseAddress);
    void Shutdown();
    bool IsInitialized();

    // Save/load bike entity state
    bool SaveBike(int slot);
    bool LoadBike(int slot);
    bool QuickSaveBike();
    bool QuickLoadBike();

    // Utilities
    bool HasSavedBike(int slot);
    void ClearSlot(int slot);
    void ClearAllSlots();
    
    // Debug
    void LogBikePointers();
}
