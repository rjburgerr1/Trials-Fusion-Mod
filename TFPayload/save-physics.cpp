#include "pch.h"
#include "save-physics.h"
#include <iostream>
#include <cstring>
#include <chrono>
#include <Windows.h>

#define Log(...) std::cout << __VA_ARGS__ << std::endl

namespace SavePhysics {
    // Constants
    static constexpr int MAX_SAVE_SLOTS = 10;
    
    // RVA offsets from Ghidra analysis
    static constexpr uintptr_t GLOBAL_STRUCT_RVA = 0x104b308;
    
    // Game function RVAs (from Ghidra) - NOTE: Names are misleading!
    static constexpr uintptr_t SET_BIKE_POSITION_RVA = 0x936540;
    static constexpr uintptr_t SET_BIKE_DIRECTION_RVA = 0x936570;  // Called "SetBikeVelocity" but sets direction!
    static constexpr uintptr_t SET_BIKE_ROTATION_RVA = 0x9365a0;
    static constexpr uintptr_t SET_BIKE_RADIUS_RVA = 0x9362d0;     // Called "SetBikeFieldOfView" but sets radius!
    
    // Physics state memory layout
    static constexpr size_t PHYSICS_START_OFFSET = 0x40;
    static constexpr size_t PHYSICS_END_OFFSET = 0xC0;
    static constexpr size_t PHYSICS_SIZE = PHYSICS_END_OFFSET - PHYSICS_START_OFFSET;
    
    // Global state
    static bool g_initialized = false;
    static uintptr_t g_baseAddress = 0;
    static void** g_globalStructPtr = nullptr;
    static BikePhysicsState g_saveSlots[MAX_SAVE_SLOTS];
    
    // Function pointers
    static SetBikePositionFn g_setBikePosition = nullptr;
    static SetBikeDirectionFn g_setBikeDirection = nullptr;  // "SetBikeVelocity"
    static SetBikeRotationFn g_setBikeRotation = nullptr;
    static SetBikeRadiusFn g_setBikeRadius = nullptr;        // "SetBikeFieldOfView"

    // ============================================================================
    // BikePhysicsState Implementation
    // ============================================================================

    BikePhysicsState::BikePhysicsState() {
        Clear();
    }

    void BikePhysicsState::Clear() {
        memset(position, 0, sizeof(position));
        memset(direction, 0, sizeof(direction));
        memset(rotation, 0, sizeof(rotation));
        radius = 0.0f;
        memset(prevPosition, 0, sizeof(prevPosition));
        memset(prevDirection, 0, sizeof(prevDirection));
        memset(collisionData, 0, sizeof(collisionData));
        memset(additionalData, 0, sizeof(additionalData));
        isValid = false;
        timestamp = 0;
    }

    // ============================================================================
    // Internal Helper Functions
    // ============================================================================

    // Get current timestamp in milliseconds
    static uint64_t GetTimestamp() {
        using namespace std::chrono;
        return duration_cast<milliseconds>(
            system_clock::now().time_since_epoch()
        ).count();
    }

    // Get the vehicle physics pointer from the game's global structure
    static void* GetVehiclePhysicsPointer() {
        if (!g_globalStructPtr || IsBadReadPtr(g_globalStructPtr, sizeof(void*))) {
            return nullptr;
        }
        
        void* globalStruct = *g_globalStructPtr;
        if (!globalStruct || IsBadReadPtr(globalStruct, sizeof(void*))) {
            return nullptr;
        }
        
        // The vehicle/physics state is at offset +0xe8 from global struct
        uintptr_t vehiclePtrAddr = reinterpret_cast<uintptr_t>(globalStruct) + 0xe8;
        if (IsBadReadPtr((void*)vehiclePtrAddr, sizeof(void*))) {
            return nullptr;
        }
        
        void* vehiclePtr = *reinterpret_cast<void**>(vehiclePtrAddr);
        if (!vehiclePtr) {
            return nullptr;
        }
        
        // Verify we can read the physics data
        if (IsBadReadPtr(vehiclePtr, PHYSICS_END_OFFSET)) {
            return nullptr;
        }
        
        return vehiclePtr;
    }

    // Read physics state from memory into structure
    static bool ReadPhysicsFromMemory(void* vehiclePtr, BikePhysicsState& state) {
        if (!vehiclePtr) {
            return false;
        }

        __try {
            uintptr_t base = reinterpret_cast<uintptr_t>(vehiclePtr);
            
            // Read current state
            memcpy(state.position, reinterpret_cast<void*>(base + 0x58), sizeof(state.position));
            memcpy(state.direction, reinterpret_cast<void*>(base + 0x64), sizeof(state.direction));
            memcpy(state.rotation, reinterpret_cast<void*>(base + 0x70), sizeof(state.rotation));
            state.radius = *reinterpret_cast<float*>(base + 0x7C);
            
            // Read previous state (managed by the game)
            memcpy(state.prevPosition, reinterpret_cast<void*>(base + 0x40), sizeof(state.prevPosition));
            memcpy(state.prevDirection, reinterpret_cast<void*>(base + 0x4C), sizeof(state.prevDirection));
            
            // Read collision data
            memcpy(state.collisionData, reinterpret_cast<void*>(base + 0x54), sizeof(state.collisionData));
            
            // Read additional data (from +0x80 to +0xA4)
            memcpy(state.additionalData, reinterpret_cast<void*>(base + 0x80), sizeof(state.additionalData));
            
            state.isValid = true;
            state.timestamp = GetTimestamp();
            
            return true;
        }
        __except(EXCEPTION_EXECUTE_HANDLER) {
            Log("[SavePhysics ERROR] Memory access violation during read!");
            return false;
        }
    }

    // Write physics state using direct memory writes (safer than calling game functions)
    static bool WritePhysicsDirectly(void* vehiclePtr, const BikePhysicsState& state) {
        if (!vehiclePtr || !state.isValid) {
            Log("[SavePhysics ERROR] Invalid vehicle pointer or state");
            return false;
        }

        __try {
            uintptr_t base = reinterpret_cast<uintptr_t>(vehiclePtr);
            
            // Verify we can write to the memory locations
            if (IsBadWritePtr(reinterpret_cast<void*>(base + 0x40), 0x80)) {
                Log("[SavePhysics ERROR] Cannot write to vehicle memory!");
                return false;
            }
            
            // Write position
            memcpy(reinterpret_cast<void*>(base + 0x58), state.position, sizeof(state.position));
            
            // Write direction  
            memcpy(reinterpret_cast<void*>(base + 0x64), state.direction, sizeof(state.direction));
            
            // Write rotation
            memcpy(reinterpret_cast<void*>(base + 0x70), state.rotation, sizeof(state.rotation));
            
            // Write radius
            *reinterpret_cast<float*>(base + 0x7C) = state.radius;
            
            // Write previous position
            memcpy(reinterpret_cast<void*>(base + 0x40), state.prevPosition, sizeof(state.prevPosition));
            
            // Write previous direction
            memcpy(reinterpret_cast<void*>(base + 0x4C), state.prevDirection, sizeof(state.prevDirection));
            
            // Write collision data
            memcpy(reinterpret_cast<void*>(base + 0x54), state.collisionData, sizeof(state.collisionData));
            
            // Write additional data
            memcpy(reinterpret_cast<void*>(base + 0x80), state.additionalData, sizeof(state.additionalData));
            
            return true;
        }
        __except(EXCEPTION_EXECUTE_HANDLER) {
            DWORD exceptionCode = GetExceptionCode();
            Log("[SavePhysics ERROR] Exception during state restoration!");
            Log("[SavePhysics ERROR] Exception code: 0x" << std::hex << exceptionCode);
            Log("[SavePhysics ERROR] Vehicle pointer: " << vehiclePtr);
            return false;
        }
    }

    // ============================================================================
    // Public API Implementation
    // ============================================================================

    bool Initialize(uintptr_t baseAddress) {
        if (g_initialized) {
            Log("[SavePhysics] Already initialized");
            return true;
        }

        if (baseAddress == 0) {
            Log("[SavePhysics ERROR] Invalid base address");
            return false;
        }

        g_baseAddress = baseAddress;
        g_globalStructPtr = reinterpret_cast<void**>(baseAddress + GLOBAL_STRUCT_RVA);

        // Verify we can access the global structure
        if (IsBadReadPtr(g_globalStructPtr, sizeof(void*))) {
            Log("[SavePhysics ERROR] Invalid global struct pointer");
            return false;
        }

        // Initialize function pointers
        g_setBikePosition = reinterpret_cast<SetBikePositionFn>(baseAddress + SET_BIKE_POSITION_RVA);
        g_setBikeDirection = reinterpret_cast<SetBikeDirectionFn>(baseAddress + SET_BIKE_DIRECTION_RVA);
        g_setBikeRotation = reinterpret_cast<SetBikeRotationFn>(baseAddress + SET_BIKE_ROTATION_RVA);
        g_setBikeRadius = reinterpret_cast<SetBikeRadiusFn>(baseAddress + SET_BIKE_RADIUS_RVA);

        // Initialize save slots
        for (int i = 0; i < MAX_SAVE_SLOTS; i++) {
            g_saveSlots[i].Clear();
        }

        g_initialized = true;

        // Initialization messages made verbose (suppressed by default)
        // Log("[SavePhysics] ================================================");
        // Log("[SavePhysics] Bike Physics Save System Initialized");
        // Log("[SavePhysics] ================================================");
        // Log("[SavePhysics] Using game functions:");
        // Log("[SavePhysics]   SetBikePosition   @ " << std::hex << (void*)g_setBikePosition);
        // Log("[SavePhysics]   SetBike[Direction] @ " << std::hex << (void*)g_setBikeDirection << " (misnamed 'Velocity')");
        // Log("[SavePhysics]   SetBikeRotation   @ " << std::hex << (void*)g_setBikeRotation);
        // Log("[SavePhysics]   SetBike[Radius]   @ " << std::hex << (void*)g_setBikeRadius << " (misnamed 'FOV')");
        // Log("[SavePhysics] Save slots available: " << std::dec << MAX_SAVE_SLOTS);
        // Log("[SavePhysics] Physics state size: " << sizeof(BikePhysicsState) << " bytes");
        // Log("[SavePhysics] ================================================");

        return true;
    }

    void Shutdown() {
        if (!g_initialized) {
            return;
        }

        Log("[SavePhysics] Shutting down...");
        
        ClearAllSlots();
        
        g_initialized = false;
        g_baseAddress = 0;
        g_globalStructPtr = nullptr;
        g_setBikePosition = nullptr;
        g_setBikeDirection = nullptr;
        g_setBikeRotation = nullptr;
        g_setBikeRadius = nullptr;
        
        Log("[SavePhysics] Shutdown complete");
    }

    bool IsInitialized() {
        return g_initialized;
    }

    bool SavePhysics(int slot) {
        if (!g_initialized) {
            Log("[SavePhysics ERROR] Not initialized!");
            return false;
        }

        if (slot < 0 || slot >= MAX_SAVE_SLOTS) {
            Log("[SavePhysics ERROR] Invalid slot: " << slot);
            return false;
        }

        void* vehiclePtr = GetVehiclePhysicsPointer();
        if (!vehiclePtr) {
            Log("[SavePhysics ERROR] Cannot access vehicle physics");
            return false;
        }

        if (!ReadPhysicsFromMemory(vehiclePtr, g_saveSlots[slot])) {
            Log("[SavePhysics ERROR] Failed to read physics state");
            return false;
        }

        Log("[SavePhysics] ================================");
        Log("[SavePhysics] SAVED to slot " << slot);
        Log("[SavePhysics] Position:  (" 
            << g_saveSlots[slot].position[0] << ", "
            << g_saveSlots[slot].position[1] << ", "
            << g_saveSlots[slot].position[2] << ")");
        Log("[SavePhysics] Direction: ("
            << g_saveSlots[slot].direction[0] << ", "
            << g_saveSlots[slot].direction[1] << ", "
            << g_saveSlots[slot].direction[2] << ")");
        Log("[SavePhysics] Rotation:  ("
            << g_saveSlots[slot].rotation[0] << ", "
            << g_saveSlots[slot].rotation[1] << ", "
            << g_saveSlots[slot].rotation[2] << ")");
        Log("[SavePhysics] Radius: " << g_saveSlots[slot].radius);
        Log("[SavePhysics] ================================");

        return true;
    }

    bool LoadPhysics(int slot) {
        if (!g_initialized) {
            Log("[SavePhysics ERROR] Not initialized!");
            return false;
        }

        if (slot < 0 || slot >= MAX_SAVE_SLOTS) {
            Log("[SavePhysics ERROR] Invalid slot: " << slot);
            return false;
        }

        if (!g_saveSlots[slot].isValid) {
            Log("[SavePhysics ERROR] Slot " << slot << " is empty!");
            return false;
        }

        void* vehiclePtr = GetVehiclePhysicsPointer();
        if (!vehiclePtr) {
            Log("[SavePhysics ERROR] Cannot access vehicle physics");
            return false;
        }

        if (!WritePhysicsDirectly(vehiclePtr, g_saveSlots[slot])) {
            Log("[SavePhysics ERROR] Failed to write physics state");
            return false;
        }

        Log("[SavePhysics] ================================");
        Log("[SavePhysics] LOADED from slot " << slot);
        Log("[SavePhysics] Position:  ("
            << g_saveSlots[slot].position[0] << ", "
            << g_saveSlots[slot].position[1] << ", "
            << g_saveSlots[slot].position[2] << ")");
        Log("[SavePhysics] Direction: ("
            << g_saveSlots[slot].direction[0] << ", "
            << g_saveSlots[slot].direction[1] << ", "
            << g_saveSlots[slot].direction[2] << ")");
        Log("[SavePhysics] Rotation:  ("
            << g_saveSlots[slot].rotation[0] << ", "
            << g_saveSlots[slot].rotation[1] << ", "
            << g_saveSlots[slot].rotation[2] << ")");
        Log("[SavePhysics] Radius: " << g_saveSlots[slot].radius);
        Log("[SavePhysics] ================================");

        return true;
    }

    bool QuickSavePhysics() {
        Log("[SavePhysics] === QUICK SAVE ===");
        return SavePhysics(0);
    }

    bool QuickLoadPhysics() {
        Log("[SavePhysics] === QUICK LOAD ===");
        return LoadPhysics(0);
    }

    bool GetCurrentPhysicsState(BikePhysicsState& state) {
        if (!g_initialized) {
            return false;
        }

        void* vehiclePtr = GetVehiclePhysicsPointer();
        if (!vehiclePtr) {
            return false;
        }

        return ReadPhysicsFromMemory(vehiclePtr, state);
    }

    bool SetPhysicsState(const BikePhysicsState& state) {
        if (!g_initialized) {
            return false;
        }

        void* vehiclePtr = GetVehiclePhysicsPointer();
        if (!vehiclePtr) {
            return false;
        }

        return WritePhysicsDirectly(vehiclePtr, state);
    }

    bool HasSavedPhysics(int slot) {
        if (slot < 0 || slot >= MAX_SAVE_SLOTS) {
            return false;
        }
        return g_saveSlots[slot].isValid;
    }

    SlotInfo GetSlotInfo(int slot) {
        SlotInfo info;
        info.hasData = false;
        info.timestamp = 0;
        memset(info.position, 0, sizeof(info.position));
        memset(info.direction, 0, sizeof(info.direction));

        if (slot >= 0 && slot < MAX_SAVE_SLOTS && g_saveSlots[slot].isValid) {
            info.hasData = true;
            info.timestamp = g_saveSlots[slot].timestamp;
            memcpy(info.position, g_saveSlots[slot].position, sizeof(info.position));
            memcpy(info.direction, g_saveSlots[slot].direction, sizeof(info.direction));
        }

        return info;
    }

    void ClearSlot(int slot) {
        if (slot < 0 || slot >= MAX_SAVE_SLOTS) {
            return;
        }
        g_saveSlots[slot].Clear();
        Log("[SavePhysics] Cleared slot " << slot);
    }

    void ClearAllSlots() {
        for (int i = 0; i < MAX_SAVE_SLOTS; i++) {
            g_saveSlots[i].Clear();
        }
        Log("[SavePhysics] All slots cleared");
    }

    bool ExportState(int slot, void* buffer, size_t bufferSize) {
        if (slot < 0 || slot >= MAX_SAVE_SLOTS) {
            return false;
        }

        if (!g_saveSlots[slot].isValid) {
            return false;
        }

        if (bufferSize < sizeof(BikePhysicsState)) {
            return false;
        }

        memcpy(buffer, &g_saveSlots[slot], sizeof(BikePhysicsState));
        return true;
    }

    bool ImportState(int slot, const void* buffer, size_t bufferSize) {
        if (slot < 0 || slot >= MAX_SAVE_SLOTS) {
            return false;
        }

        if (bufferSize < sizeof(BikePhysicsState)) {
            return false;
        }

        memcpy(&g_saveSlots[slot], buffer, sizeof(BikePhysicsState));
        
        // Validate the imported state
        if (!g_saveSlots[slot].isValid) {
            Log("[SavePhysics] Imported state is marked as invalid");
            return false;
        }

        Log("[SavePhysics] Imported state to slot " << slot);
        return true;
    }

    size_t GetExportSize() {
        return sizeof(BikePhysicsState);
    }

    PhysicsInfo GetInfo() {
        PhysicsInfo info;
        info.totalSlots = MAX_SAVE_SLOTS;
        info.usedSlots = 0;
        info.stateSize = sizeof(BikePhysicsState);
        info.initialized = g_initialized;

        for (int i = 0; i < MAX_SAVE_SLOTS; i++) {
            if (g_saveSlots[i].isValid) {
                info.usedSlots++;
            }
        }

        info.totalMemoryUsed = info.usedSlots * info.stateSize;

        return info;
    }

    bool ValidatePhysicsPointer() {
        if (!g_initialized) {
            Log("[SavePhysics] System not initialized");
            return false;
        }

        void* vehiclePtr = GetVehiclePhysicsPointer();
        if (!vehiclePtr) {
            Log("[SavePhysics] Invalid vehicle physics pointer");
            return false;
        }

        Log("[SavePhysics] Vehicle physics pointer is VALID: " << vehiclePtr);
        return true;
    }

    void LogPhysicsState(const char* prefix) {
        if (!g_initialized) {
            Log("[SavePhysics] Not initialized");
            return;
        }

        void* vehiclePtr = GetVehiclePhysicsPointer();
        if (!vehiclePtr) {
            Log("[SavePhysics] Cannot access vehicle physics");
            return;
        }

        BikePhysicsState state;
        if (!ReadPhysicsFromMemory(vehiclePtr, state)) {
            Log("[SavePhysics] Failed to read current state");
            return;
        }

        std::string pre = prefix ? std::string(prefix) + " " : "";

        Log(pre << "================================================");
        Log(pre << "Current Bike Physics State");
        Log(pre << "================================================");
        Log(pre << "Position:       (" << state.position[0] << ", " << state.position[1] << ", " << state.position[2] << ")");
        Log(pre << "Direction:      (" << state.direction[0] << ", " << state.direction[1] << ", " << state.direction[2] << ")");
        Log(pre << "Rotation:       (" << state.rotation[0] << ", " << state.rotation[1] << ", " << state.rotation[2] << ")");
        Log(pre << "Radius:         " << state.radius);
        Log(pre << "Prev Position:  (" << state.prevPosition[0] << ", " << state.prevPosition[1] << ", " << state.prevPosition[2] << ")");
        Log(pre << "Prev Direction: (" << state.prevDirection[0] << ", " << state.prevDirection[1] << ", " << state.prevDirection[2] << ")");
        Log(pre << "================================================");
    }

    void CompareStates(int slot1, int slot2) {
        if (slot1 < 0 || slot1 >= MAX_SAVE_SLOTS || slot2 < 0 || slot2 >= MAX_SAVE_SLOTS) {
            Log("[SavePhysics] Invalid slot numbers");
            return;
        }

        if (!g_saveSlots[slot1].isValid || !g_saveSlots[slot2].isValid) {
            Log("[SavePhysics] One or both slots are empty");
            return;
        }

        const BikePhysicsState& s1 = g_saveSlots[slot1];
        const BikePhysicsState& s2 = g_saveSlots[slot2];

        Log("[SavePhysics] ================================================");
        Log("[SavePhysics] Comparing Slot " << slot1 << " vs Slot " << slot2);
        Log("[SavePhysics] ================================================");
        
        // Compare positions
        float posDiff = 0.0f;
        for (int i = 0; i < 3; i++) {
            float diff = s1.position[i] - s2.position[i];
            posDiff += diff * diff;
        }
        posDiff = sqrtf(posDiff);
        Log("[SavePhysics] Position difference: " << posDiff);

        // Compare directions
        float dirDiff = 0.0f;
        for (int i = 0; i < 3; i++) {
            float diff = s1.direction[i] - s2.direction[i];
            dirDiff += diff * diff;
        }
        dirDiff = sqrtf(dirDiff);
        Log("[SavePhysics] Direction difference: " << dirDiff);

        // Compare rotations
        float rotDiff = 0.0f;
        for (int i = 0; i < 3; i++) {
            float diff = s1.rotation[i] - s2.rotation[i];
            rotDiff += diff * diff;
        }
        rotDiff = sqrtf(rotDiff);
        Log("[SavePhysics] Rotation difference: " << rotDiff);
        
        // Compare radius
        float radiusDiff = fabsf(s1.radius - s2.radius);
        Log("[SavePhysics] Radius difference: " << radiusDiff);

        Log("[SavePhysics] ================================================");
    }
}
