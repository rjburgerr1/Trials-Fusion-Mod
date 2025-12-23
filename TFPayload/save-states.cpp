#include "pch.h"
#include "save-states.h"
#include <iostream>
#include <Windows.h>
#include <vector>
#include <cstring>

#define Log(...) std::cout << __VA_ARGS__ << std::endl

namespace SaveStates {
    // Constants
    static constexpr int MAX_SAVE_SLOTS = 10;

    // RVA offsets from Ghidra analysis
    static constexpr uintptr_t GLOBAL_STRUCT_RVA = 0x104b308;

    // Offsets within the vehicle physics structure
    // Save as much state as we can find that's likely safe
    static constexpr size_t STATE_START_OFFSET = 0x40;
    static constexpr size_t STATE_SIZE = 0x80;  // Save from +0x40 to +0xC0

    // Structure to hold a saved state
    struct SavedState {
        bool used;
        uint8_t data[STATE_SIZE];
        uintptr_t baseAddress;
        
        SavedState() : used(false), baseAddress(0) {
            memset(data, 0, sizeof(data));
        }
    };

    // State tracking
    static bool g_initialized = false;
    static uintptr_t g_baseAddress = 0;
    static void** g_globalStructPtr = nullptr;
    static SavedState g_saveSlots[MAX_SAVE_SLOTS];
    
    // Hotkey state tracking
    struct HotkeyState {
        bool f7 = false;
        bool f8 = false;
        bool key1 = false;
        bool key2 = false;
        bool key3 = false;
        bool key4 = false;
        bool key5 = false;
        bool key6 = false;
        bool key7 = false;
        bool key8 = false;
        bool key9 = false;
    };
    static HotkeyState g_hotkeyState;

    // Get the vehicle physics state pointer
    void* GetVehiclePhysicsPointer() {
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
        if (IsBadReadPtr(vehiclePtr, STATE_START_OFFSET + STATE_SIZE)) {
            return nullptr;
        }
        
        return vehiclePtr;
    }

    void LogPhysicsState(void* vehiclePtr, const char* prefix) {
        uintptr_t base = reinterpret_cast<uintptr_t>(vehiclePtr);
        
        // Previous position at +0x40, +0x44, +0x48
        float* prevPos = reinterpret_cast<float*>(base + 0x40);
        Log(prefix << " PrevPos: (" << prevPos[0] << ", " << prevPos[1] << ", " << prevPos[2] << ")");
        
        // Previous "something" at +0x4c, +0x50, +0x54
        float* prevOther = reinterpret_cast<float*>(base + 0x4c);
        Log(prefix << " PrevOther: (" << prevOther[0] << ", " << prevOther[1] << ", " << prevOther[2] << ")");
        
        // Current position at +0x58, +0x5c, +0x60
        float* curPos = reinterpret_cast<float*>(base + 0x58);
        Log(prefix << " CurPos: (" << curPos[0] << ", " << curPos[1] << ", " << curPos[2] << ")");
        
        // Second position at +0x64, +0x68, +0x6c
        float* pos2 = reinterpret_cast<float*>(base + 0x64);
        Log(prefix << " Pos2: (" << pos2[0] << ", " << pos2[1] << ", " << pos2[2] << ")");
        
        // Rotation at +0x70, +0x74, +0x78
        float* rotation = reinterpret_cast<float*>(base + 0x70);
        Log(prefix << " Rotation: (" << rotation[0] << ", " << rotation[1] << ", " << rotation[2] << ")");
    }

    bool Initialize(uintptr_t baseAddress) {
        if (g_initialized) {
            Log("[SaveStates] Already initialized");
            return true;
        }

        if (baseAddress == 0) {
            Log("[SaveStates ERROR] Invalid base address");
            return false;
        }

        g_baseAddress = baseAddress;
        g_globalStructPtr = reinterpret_cast<void**>(baseAddress + GLOBAL_STRUCT_RVA);

        // Verify we can access the global structure
        if (IsBadReadPtr(g_globalStructPtr, sizeof(void*))) {
            Log("[SaveStates ERROR] Invalid global struct pointer");
            return false;
        }

        // Initialize save slots
        for (int i = 0; i < MAX_SAVE_SLOTS; i++) {
            g_saveSlots[i] = SavedState();
        }

        g_initialized = true;

        // Initialization messages made verbose (suppressed by default)
        // Log("[SaveStates] Initialized successfully");
        // Log("[SaveStates] - Saves physics state (128 bytes from +0x40)");
        // Log("[SaveStates] - Save slots: " << MAX_SAVE_SLOTS);
        // Log("[SaveStates] - Quick Save: F7");
        // Log("[SaveStates] - Quick Load: F8");
        // Log("[SaveStates] - EXPERIMENTAL: May not preserve all state!");

        return true;
    }

    void Shutdown() {
        if (!g_initialized) {
            return;
        }

        Log("[SaveStates] Shutting down...");
        
        for (int i = 0; i < MAX_SAVE_SLOTS; i++) {
            g_saveSlots[i].used = false;
        }
        
        g_initialized = false;
        g_globalStructPtr = nullptr;
    }

    bool SaveState(int slot) {
        if (!g_initialized) {
            Log("[SaveStates ERROR] Not initialized!");
            return false;
        }

        if (slot < 0 || slot >= MAX_SAVE_SLOTS) {
            Log("[SaveStates ERROR] Invalid slot: " << slot);
            return false;
        }

        void* vehiclePtr = GetVehiclePhysicsPointer();
        
        if (!vehiclePtr) {
            Log("[SaveStates ERROR] Cannot access vehicle physics");
            return false;
        }

        __try {
            uintptr_t base = reinterpret_cast<uintptr_t>(vehiclePtr) + STATE_START_OFFSET;
            
            // Copy the state data
            memcpy(g_saveSlots[slot].data, reinterpret_cast<void*>(base), STATE_SIZE);
            
            g_saveSlots[slot].used = true;
            g_saveSlots[slot].baseAddress = g_baseAddress;

            Log("[SaveStates] === SAVED SLOT " << slot << " ===");
            LogPhysicsState(vehiclePtr, "[SaveStates]");
            
            return true;
        }
        __except(EXCEPTION_EXECUTE_HANDLER) {
            Log("[SaveStates ERROR] Memory access violation during save!");
            return false;
        }
    }

    bool LoadState(int slot) {
        if (!g_initialized) {
            Log("[SaveStates ERROR] Not initialized!");
            return false;
        }

        if (slot < 0 || slot >= MAX_SAVE_SLOTS) {
            Log("[SaveStates ERROR] Invalid slot: " << slot);
            return false;
        }

        if (!g_saveSlots[slot].used) {
            Log("[SaveStates ERROR] Slot " << slot << " is empty!");
            return false;
        }

        if (g_saveSlots[slot].baseAddress != g_baseAddress) {
            Log("[SaveStates ERROR] Save state is from a different game session!");
            return false;
        }

        void* vehiclePtr = GetVehiclePhysicsPointer();
        
        if (!vehiclePtr) {
            Log("[SaveStates ERROR] Cannot access vehicle physics");
            return false;
        }

        __try {
            uintptr_t base = reinterpret_cast<uintptr_t>(vehiclePtr) + STATE_START_OFFSET;
            
            // Restore the saved state
            memcpy(reinterpret_cast<void*>(base), g_saveSlots[slot].data, STATE_SIZE);

            Log("[SaveStates] === LOADED SLOT " << slot << " ===");
            LogPhysicsState(vehiclePtr, "[SaveStates]");
            
            return true;
        }
        __except(EXCEPTION_EXECUTE_HANDLER) {
            Log("[SaveStates ERROR] Memory access violation during load!");
            return false;
        }
    }

    bool QuickSave() {
        Log("[SaveStates] === QUICK SAVE ===");
        return SaveState(0);
    }

    bool QuickLoad() {
        Log("[SaveStates] === QUICK LOAD ===");
        return LoadState(0);
    }

    bool HasSavedState(int slot) {
        if (slot < 0 || slot >= MAX_SAVE_SLOTS) {
            return false;
        }
        return g_saveSlots[slot].used;
    }

    void ClearSlot(int slot) {
        if (slot < 0 || slot >= MAX_SAVE_SLOTS) {
            return;
        }
        g_saveSlots[slot].used = false;
        Log("[SaveStates] Cleared slot " << slot);
    }

    void ClearAllSlots() {
        for (int i = 0; i < MAX_SAVE_SLOTS; i++) {
            ClearSlot(i);
        }
        Log("[SaveStates] All slots cleared");
    }

    void CheckHotkeys() {
        if (!g_initialized) {
            return;
        }

        bool shiftPressed = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;

        // F7 - Quick Save
        bool f7IsPressed = (GetAsyncKeyState(VK_F7) & 0x8000) != 0;
        if (f7IsPressed && !g_hotkeyState.f7) {
            QuickSave();
        }
        g_hotkeyState.f7 = f7IsPressed;

        // F8 - Quick Load
        bool f8IsPressed = (GetAsyncKeyState(VK_F8) & 0x8000) != 0;
        if (f8IsPressed && !g_hotkeyState.f8) {
            QuickLoad();
        }
        g_hotkeyState.f8 = f8IsPressed;

        // Number keys 1-9
        bool key1IsPressed = (GetAsyncKeyState('1') & 0x8000) != 0;
        if (key1IsPressed && !g_hotkeyState.key1) {
            shiftPressed ? SaveState(1) : LoadState(1);
        }
        g_hotkeyState.key1 = key1IsPressed;

        bool key2IsPressed = (GetAsyncKeyState('2') & 0x8000) != 0;
        if (key2IsPressed && !g_hotkeyState.key2) {
            shiftPressed ? SaveState(2) : LoadState(2);
        }
        g_hotkeyState.key2 = key2IsPressed;

        bool key3IsPressed = (GetAsyncKeyState('3') & 0x8000) != 0;
        if (key3IsPressed && !g_hotkeyState.key3) {
            shiftPressed ? SaveState(3) : LoadState(3);
        }
        g_hotkeyState.key3 = key3IsPressed;

        bool key4IsPressed = (GetAsyncKeyState('4') & 0x8000) != 0;
        if (key4IsPressed && !g_hotkeyState.key4) {
            shiftPressed ? SaveState(4) : LoadState(4);
        }
        g_hotkeyState.key4 = key4IsPressed;

        bool key5IsPressed = (GetAsyncKeyState('5') & 0x8000) != 0;
        if (key5IsPressed && !g_hotkeyState.key5) {
            shiftPressed ? SaveState(5) : LoadState(5);
        }
        g_hotkeyState.key5 = key5IsPressed;

        bool key6IsPressed = (GetAsyncKeyState('6') & 0x8000) != 0;
        if (key6IsPressed && !g_hotkeyState.key6) {
            shiftPressed ? SaveState(6) : LoadState(6);
        }
        g_hotkeyState.key6 = key6IsPressed;

        bool key7IsPressed = (GetAsyncKeyState('7') & 0x8000) != 0;
        if (key7IsPressed && !g_hotkeyState.key7) {
            shiftPressed ? SaveState(7) : LoadState(7);
        }
        g_hotkeyState.key7 = key7IsPressed;

        bool key8IsPressed = (GetAsyncKeyState('8') & 0x8000) != 0;
        if (key8IsPressed && !g_hotkeyState.key8) {
            shiftPressed ? SaveState(8) : LoadState(8);
        }
        g_hotkeyState.key8 = key8IsPressed;

        bool key9IsPressed = (GetAsyncKeyState('9') & 0x8000) != 0;
        if (key9IsPressed && !g_hotkeyState.key9) {
            shiftPressed ? SaveState(9) : LoadState(9);
        }
        g_hotkeyState.key9 = key9IsPressed;
    }

    SaveStateInfo GetInfo() {
        SaveStateInfo info;
        info.totalSlots = MAX_SAVE_SLOTS;
        info.usedSlots = 0;
        info.memoryPerSlot = STATE_SIZE;
        
        for (int i = 0; i < MAX_SAVE_SLOTS; i++) {
            if (g_saveSlots[i].used) {
                info.usedSlots++;
            }
        }
        
        info.totalMemoryUsed = info.usedSlots * STATE_SIZE;
        
        return info;
    }
}
