#include "pch.h"
#include "save-bike.h"
#include "logging.h"
#include <iostream>
#include <cstring>
#include <chrono>
#include <Windows.h>

namespace SaveBike {
    // Constants
    static constexpr int MAX_SAVE_SLOTS = 10;
    static constexpr uintptr_t GLOBAL_STRUCT_RVA = 0x104b308;
    static constexpr size_t BIKE_STATE_SIZE = 0xA00;  // Save first ~2.5KB (0x000 to 0xA00)
    
    // Global state
    static bool g_initialized = false;
    static uintptr_t g_baseAddress = 0;
    static void** g_globalStructPtr = nullptr;
    static BikeEntityState g_saveSlots[MAX_SAVE_SLOTS];

    // ============================================================================
    // BikeEntityState Implementation
    // ============================================================================

    BikeEntityState::BikeEntityState() {
        Clear();
    }

    void BikeEntityState::Clear() {
        memset(fullState, 0, sizeof(fullState));
        isValid = false;
        timestamp = 0;
    }

    // ============================================================================
    // Internal Helper Functions
    // ============================================================================

    static uint64_t GetTimestamp() {
        using namespace std::chrono;
        return duration_cast<milliseconds>(
            system_clock::now().time_since_epoch()
        ).count();
    }

    // Get the bike physics object (same as what save-physics uses)
    // This is at global+0xE8, NOT at (global+0xE8)+0x84
    static void* GetBikePhysicsPointer() {
        if (!g_globalStructPtr || IsBadReadPtr(g_globalStructPtr, sizeof(void*))) {
            LOG_VERBOSE("[SaveBike DEBUG] Global struct pointer invalid");
            return nullptr;
        }
        
        void* globalStruct = *g_globalStructPtr;
        if (!globalStruct || IsBadReadPtr(globalStruct, sizeof(void*))) {
            LOG_VERBOSE("[SaveBike DEBUG] Global struct invalid");
            return nullptr;
        }
        
        // The bike physics object is at offset +0xe8 from global struct
        uintptr_t bikePhysicsPtrAddr = reinterpret_cast<uintptr_t>(globalStruct) + 0xe8;
        if (IsBadReadPtr((void*)bikePhysicsPtrAddr, sizeof(void*))) {
            LOG_VERBOSE("[SaveBike DEBUG] Cannot read from global+0xE8");
            return nullptr;
        }
        
        void* bikePhysicsPtr = *reinterpret_cast<void**>(bikePhysicsPtrAddr);
        if (!bikePhysicsPtr) {
            LOG_VERBOSE("[SaveBike DEBUG] Bike physics pointer is null");
            return nullptr;
        }
        
        // Verify we can read the full state
        if (IsBadReadPtr(bikePhysicsPtr, BIKE_STATE_SIZE)) {
            LOG_VERBOSE("[SaveBike DEBUG] Cannot read " << BIKE_STATE_SIZE << " bytes from bike physics");
            return nullptr;
        }
        
        return bikePhysicsPtr;
    }

    // Safe memory operations (SEH-enabled, no C++ objects)
    static bool SafeMemcpyRead(void* dest, const void* src, size_t size) {
        __try {
            memcpy(dest, src, size);
            return true;
        }
        __except(EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    static bool SafeMemcpyWrite(void* dest, const void* src, size_t size) {
        __try {
            if (IsBadWritePtr(dest, size)) {
                return false;
            }
            memcpy(dest, src, size);
            return true;
        }
        __except(EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    // Read bike state from memory
    static bool ReadBikeFromMemory(void* bikePtr, BikeEntityState& state) {
        if (!bikePtr) {
            return false;
        }

        if (!SafeMemcpyRead(state.fullState, bikePtr, BIKE_STATE_SIZE)) {
            LOG_ERROR("[SaveBike ERROR] Memory access violation during read!");
            return false;
        }

        state.isValid = true;
        state.timestamp = GetTimestamp();
        return true;
    }

    // Write bike state to memory
    static bool WriteBikeToMemory(void* bikePtr, const BikeEntityState& state) {
        if (!bikePtr || !state.isValid) {
            return false;
        }

        if (!SafeMemcpyWrite(bikePtr, state.fullState, BIKE_STATE_SIZE)) {
            LOG_ERROR("[SaveBike ERROR] Cannot write to bike memory!");
            return false;
        }

        return true;
    }

    // ============================================================================
    // Public API Implementation
    // ============================================================================

    bool Initialize(uintptr_t baseAddress) {
        if (g_initialized) {
            LOG_VERBOSE("[SaveBike] Already initialized");
            return true;
        }

        if (baseAddress == 0) {
            LOG_ERROR("[SaveBike ERROR] Invalid base address");
            return false;
        }

        g_baseAddress = baseAddress;
        g_globalStructPtr = reinterpret_cast<void**>(baseAddress + GLOBAL_STRUCT_RVA);

        if (IsBadReadPtr(g_globalStructPtr, sizeof(void*))) {
            LOG_ERROR("[SaveBike ERROR] Invalid global struct pointer");
            return false;
        }

        for (int i = 0; i < MAX_SAVE_SLOTS; i++) {
            g_saveSlots[i].Clear();
        }

        g_initialized = true;

        LOG_VERBOSE("[SaveBike] ================================================");
        LOG_VERBOSE("[SaveBike] Bike Entity Save System Initialized");
        LOG_VERBOSE("[SaveBike] ================================================");
        LOG_VERBOSE("[SaveBike] Saves actual bike entity (not camera)");
        LOG_VERBOSE("[SaveBike] Save slots: " << MAX_SAVE_SLOTS);
        LOG_VERBOSE("[SaveBike] State size: " << BIKE_STATE_SIZE << " bytes");
        LOG_VERBOSE("[SaveBike] ================================================");

        return true;
    }

    void Shutdown() {
        if (!g_initialized) {
            return;
        }

        LOG_VERBOSE("[SaveBike] Shutting down...");
        ClearAllSlots();
        
        g_initialized = false;
        g_baseAddress = 0;
        g_globalStructPtr = nullptr;
        
        LOG_VERBOSE("[SaveBike] Shutdown complete");
    }

    bool IsInitialized() {
        return g_initialized;
    }

    bool SaveBike(int slot) {
        if (!g_initialized) {
            LOG_ERROR("[SaveBike ERROR] Not initialized!");
            return false;
        }

        if (slot < 0 || slot >= MAX_SAVE_SLOTS) {
            LOG_ERROR("[SaveBike ERROR] Invalid slot: " << slot);
            return false;
        }

        void* bikePtr = GetBikePhysicsPointer();
        if (!bikePtr) {
            LOG_ERROR("[SaveBike ERROR] Cannot access bike entity");
            return false;
        }

        if (!ReadBikeFromMemory(bikePtr, g_saveSlots[slot])) {
            LOG_ERROR("[SaveBike ERROR] Failed to read bike state");
            return false;
        }

        LOG_INFO("[SaveBike] ================================");
        LOG_INFO("[SaveBike] SAVED bike entity to slot " << slot);
        LOG_INFO("[SaveBike] Bike pointer: " << bikePtr);
        LOG_INFO("[SaveBike] State size: " << BIKE_STATE_SIZE << " bytes");
        LOG_INFO("[SaveBike] ================================");

        return true;
    }

    bool LoadBike(int slot) {
        if (!g_initialized) {
            LOG_ERROR("[SaveBike ERROR] Not initialized!");
            return false;
        }

        if (slot < 0 || slot >= MAX_SAVE_SLOTS) {
            LOG_ERROR("[SaveBike ERROR] Invalid slot: " << slot);
            return false;
        }

        if (!g_saveSlots[slot].isValid) {
            LOG_ERROR("[SaveBike ERROR] Slot " << slot << " is empty!");
            return false;
        }

        void* bikePtr = GetBikePhysicsPointer();
        if (!bikePtr) {
            LOG_ERROR("[SaveBike ERROR] Cannot access bike entity");
            return false;
        }

        if (!WriteBikeToMemory(bikePtr, g_saveSlots[slot])) {
            LOG_ERROR("[SaveBike ERROR] Failed to write bike state");
            return false;
        }

        LOG_INFO("[SaveBike] ================================");
        LOG_INFO("[SaveBike] LOADED bike entity from slot " << slot);
        LOG_INFO("[SaveBike] Bike pointer: " << bikePtr);
        LOG_INFO("[SaveBike] ================================");

        return true;
    }

    bool QuickSaveBike() {
        LOG_INFO("[SaveBike] === QUICK SAVE BIKE ===");
        return SaveBike(0);
    }

    bool QuickLoadBike() {
        LOG_INFO("[SaveBike] === QUICK LOAD BIKE ===");
        return LoadBike(0);
    }

    bool HasSavedBike(int slot) {
        if (slot < 0 || slot >= MAX_SAVE_SLOTS) {
            return false;
        }
        return g_saveSlots[slot].isValid;
    }

    void ClearSlot(int slot) {
        if (slot < 0 || slot >= MAX_SAVE_SLOTS) {
            return;
        }
        g_saveSlots[slot].Clear();
        LOG_INFO("[SaveBike] Cleared slot " << slot);
    }

    void ClearAllSlots() {
        for (int i = 0; i < MAX_SAVE_SLOTS; i++) {
            g_saveSlots[i].Clear();
        }
        LOG_INFO("[SaveBike] All slots cleared");
    }

    void LogBikePointers() {
        LOG_INFO("[SaveBike] === POINTER DEBUG ===");
        
        void* bikePtr = GetBikePhysicsPointer();
        LOG_INFO("[SaveBike] Bike physics pointer: " << bikePtr);
            
            if (bikePtr) {
                // Sample some values to see what's there
                uintptr_t base = reinterpret_cast<uintptr_t>(bikePtr);
                LOG_INFO("[SaveBike] Bike+0x00: " << std::hex << *reinterpret_cast<uint32_t*>(base + 0x00));
                LOG_INFO("[SaveBike] Bike+0x04: " << std::hex << *reinterpret_cast<uint32_t*>(base + 0x04));
                LOG_INFO("[SaveBike] Bike+0x10: " << std::hex << *reinterpret_cast<uint32_t*>(base + 0x10));
                
                // Try to read some floats that might be velocity/position
                LOG_INFO("[SaveBike] Possible floats:");
                for (int i = 0; i < 16; i++) {
                    float val = *reinterpret_cast<float*>(base + i * 4);
                    if (val != 0.0f && val > -10000.0f && val < 10000.0f) {
                        LOG_INFO("[SaveBike]   +0x" << std::hex << (i*4) << std::dec << ": " << val);
                    }
                }
            }
        
        LOG_INFO("[SaveBike] ======================");
    }
}
