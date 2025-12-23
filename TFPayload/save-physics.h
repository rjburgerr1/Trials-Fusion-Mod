#pragma once
#include <cstdint>

namespace SavePhysics {
    // Bike physics state structure for serialization
    // Based on reverse engineering the actual SetBike* functions
    struct BikePhysicsState {
        // Primary state (controlled by SetBike functions)
        float position[3];          // +0x58, +0x5C, +0x60: Current position (SetBikePosition)
        float direction[3];         // +0x64, +0x68, +0x6C: Current bike direction/orientation (SetBikeVelocity - MISNAMED!)
        float rotation[3];          // +0x70, +0x74, +0x78: Rotation matrix (SetBikeRotation)
        float radius;               // +0x7C: Entity radius (SetBikeFieldOfView - MISNAMED!)
        
        // Previous state (automatically updated by SetBike functions)
        float prevPosition[3];      // +0x40, +0x44, +0x48: Previous position
        float prevDirection[3];     // +0x4C, +0x50, +0x54: Previous direction/orientation
        
        // Additional state data (collision, forces, etc.)
        float collisionData[3];     // +0x54, +0x58, +0x5C: Collision/force vectors
        uint8_t additionalData[36]; // +0x80 to +0xA4: Extra physics data
        
        // Metadata
        bool isValid;
        uint64_t timestamp;         // When this state was saved
        
        BikePhysicsState();
        void Clear();
        bool IsValid() const { return isValid; }
    };

    // Save slot information
    struct SlotInfo {
        bool hasData;
        uint64_t timestamp;
        float position[3];          // Preview of bike position
        float direction[3];         // Preview of bike direction
    };

    // Function pointers to game functions (note: names are misleading!)
    typedef void(__thiscall* SetBikePositionFn)(void* bikePtr, float* position);
    typedef void(__thiscall* SetBikeDirectionFn)(void* bikePtr, float* direction);  // Actually called "SetBikeVelocity"
    typedef void(__thiscall* SetBikeRotationFn)(void* bikePtr, float* rotation);
    typedef void(__thiscall* SetBikeRadiusFn)(void* bikePtr, float radius);  // Actually called "SetBikeFieldOfView"

    // Initialize the physics save system
    bool Initialize(uintptr_t baseAddress);

    // Shutdown and cleanup
    void Shutdown();

    // Check if system is initialized
    bool IsInitialized();

    // Save current bike physics to a slot (0-9)
    bool SavePhysics(int slot);

    // Load bike physics from a slot (0-9)
    bool LoadPhysics(int slot);

    // Quick save to slot 0
    bool QuickSavePhysics();

    // Quick load from slot 0
    bool QuickLoadPhysics();

    // Get the current bike physics state (without saving)
    bool GetCurrentPhysicsState(BikePhysicsState& state);

    // Set bike physics state using the proper game functions
    bool SetPhysicsState(const BikePhysicsState& state);

    // Check if a slot has saved physics
    bool HasSavedPhysics(int slot);

    // Get information about a save slot
    SlotInfo GetSlotInfo(int slot);

    // Clear a save slot
    void ClearSlot(int slot);

    // Clear all save slots
    void ClearAllSlots();

    // Export a physics state to binary data
    bool ExportState(int slot, void* buffer, size_t bufferSize);

    // Import a physics state from binary data
    bool ImportState(int slot, const void* buffer, size_t bufferSize);

    // Get the size needed for export buffer
    size_t GetExportSize();

    // System information
    struct PhysicsInfo {
        int totalSlots;
        int usedSlots;
        size_t stateSize;
        size_t totalMemoryUsed;
        bool initialized;
    };
    PhysicsInfo GetInfo();

    // Validation and debugging
    bool ValidatePhysicsPointer();
    void LogPhysicsState(const char* prefix = "");
    void CompareStates(int slot1, int slot2);
}
