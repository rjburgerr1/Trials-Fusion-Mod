#include "pch.h"
#include "respawn.h"
#include "logging.h"
#include <iostream>
#include <Windows.h>

namespace Respawn {
    // RVA offsets from Ghidra analysis (base address 0x700000)
    static constexpr uintptr_t GLOBAL_STRUCT_RVA = 0x104b308;  // DAT_0174b308
    static constexpr uintptr_t HANDLE_PLAYER_RESPAWN_RVA = 0x205ae0;  // 0x00905ae0 - 0x00700000

    // Function pointer types
    typedef void(__thiscall* HandlePlayerRespawnFunc)(void* thisPtr, char param1, uint32_t param2, int param3, char param4);

    // State tracking
    static bool g_initialized = false;
    static uintptr_t g_baseAddress = 0;
    static void** g_globalStructPtr = nullptr;
    static HandlePlayerRespawnFunc g_handlePlayerRespawnFunc = nullptr;

    // Hotkey state
    static bool g_qKeyPressed = false;
    static bool g_eKeyPressed = false;  // Next checkpoint
    static bool g_wKeyPressed = false;  // Previous checkpoint
    static bool g_fKeyPressed = false;  // Forward 5 checkpoints
    static bool g_numberKeysPressed[10] = { false };  // 0-9 keys for checkpoint jump

    // Forward declarations
    bool RespawnAtCheckpointOffset(int offset);

    // ============================================================================
    // SEH-safe wrapper functions (no C++ objects that require unwinding)
    // ============================================================================

    static bool CallHandlePlayerRespawnInternal(void* bikePtr, char param1, uint32_t param2, int param3, char param4) {
        __try {
            g_handlePlayerRespawnFunc(bikePtr, param1, param2, param3, param4);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    static bool CallRespawnFunction(void* bikePtr, char param1, uint32_t param2, int param3, char param4) {
        bool result = CallHandlePlayerRespawnInternal(bikePtr, param1, param2, param3, param4);
        if (!result) {
            LOG_ERROR("[Respawn] Exception in HandlePlayerRespawn");
        }
        return result;
    }

    // ============================================================================
    // Helper functions
    // ============================================================================

    static void* GetGameManager() {
        if (!g_globalStructPtr || IsBadReadPtr(g_globalStructPtr, sizeof(void*))) {
            return nullptr;
        }

        void* globalStruct = *g_globalStructPtr;
        if (!globalStruct || IsBadReadPtr(globalStruct, 0x100)) {
            return nullptr;
        }

        uintptr_t managerAddr = reinterpret_cast<uintptr_t>(globalStruct) + 0xdc;
        if (IsBadReadPtr((void*)managerAddr, sizeof(void*))) {
            return nullptr;
        }

        void* manager = *reinterpret_cast<void**>(managerAddr);
        if (!manager || IsBadReadPtr(manager, 0x1000)) {
            return nullptr;
        }

        return manager;
    }

    void* GetBikePointer() {
        void* manager = GetGameManager();
        if (!manager) {
            return nullptr;
        }

        uintptr_t bikeArrayStructAddr = reinterpret_cast<uintptr_t>(manager) + 0x2f0;
        if (IsBadReadPtr((void*)bikeArrayStructAddr, sizeof(void*))) {
            return nullptr;
        }

        void* bikeArrayStruct = *reinterpret_cast<void**>(bikeArrayStructAddr);
        if (!bikeArrayStruct || IsBadReadPtr(bikeArrayStruct, 0x40)) {
            return nullptr;
        }

        int count = *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(bikeArrayStruct) + 0x34);
        if (count <= 0) {
            return nullptr;
        }

        uintptr_t firstBikeAddr = reinterpret_cast<uintptr_t>(bikeArrayStruct) + 0x14;
        if (IsBadReadPtr((void*)firstBikeAddr, sizeof(void*))) {
            return nullptr;
        }

        void* bikePtr = *reinterpret_cast<void**>(firstBikeAddr);
        if (!bikePtr || IsBadReadPtr(bikePtr, 0x200)) {
            return nullptr;
        }

        return bikePtr;
    }

    static void* GetCheckpointListBase(void* manager) {
        if (!manager) {
            return nullptr;
        }

        uintptr_t listAddr = reinterpret_cast<uintptr_t>(manager) + 0x938;
        if (IsBadReadPtr((void*)listAddr, sizeof(void*))) {
            return nullptr;
        }

        return reinterpret_cast<void*>(listAddr);
    }

    static void* GetFirstListNode(void* listBase) {
        if (!listBase || IsBadReadPtr(listBase, sizeof(void*))) {
            return nullptr;
        }

        void* firstNode = *reinterpret_cast<void**>(listBase);
        if (!firstNode || IsBadReadPtr(firstNode, 0x10)) {
            return nullptr;
        }

        return firstNode;
    }

    static void* GetCheckpointFromNode(void* node) {
        if (!node || IsBadReadPtr(node, 0x10)) {
            return nullptr;
        }

        uintptr_t dataAddr = reinterpret_cast<uintptr_t>(node) + 0x8;
        void* checkpoint = *reinterpret_cast<void**>(dataAddr);
        
        if (!checkpoint || IsBadReadPtr(checkpoint, 0x160)) {
            return nullptr;
        }

        return checkpoint;
    }

    static void* GetNextListNode(void* node) {
        if (!node || IsBadReadPtr(node, 0x10)) {
            return nullptr;
        }

        uintptr_t nextAddr = reinterpret_cast<uintptr_t>(node) + 0x4;
        void* nextNode = *reinterpret_cast<void**>(nextAddr);
        
        return nextNode;
    }

    static void* GetCheckpointAtIndex(void* listBase, int index) {
        void* node = GetFirstListNode(listBase);
        
        for (int i = 0; node != nullptr && i < index; i++) {
            node = GetNextListNode(node);
            if (node && IsBadReadPtr(node, 0x10)) {
                return nullptr;
            }
        }

        if (!node) {
            return nullptr;
        }

        return GetCheckpointFromNode(node);
    }

    static int CountCheckpoints(void* listBase) {
        void* node = GetFirstListNode(listBase);
        void* firstNode = node;
        int count = 0;

        while (node != nullptr && count < 1000) {
            count++;
            node = GetNextListNode(node);
            
            if (node == firstNode) {
                break;
            }
            
            if (node && IsBadReadPtr(node, 0x10)) {
                break;
            }
        }

        return count;
    }

    static void* GetCurrentCheckpoint(void* bikePtr) {
        if (!bikePtr || IsBadReadPtr(bikePtr, 0x200)) {
            return nullptr;
        }

        uintptr_t checkpointAddr = reinterpret_cast<uintptr_t>(bikePtr) + 0x1dc;
        if (IsBadReadPtr((void*)checkpointAddr, sizeof(void*))) {
            return nullptr;
        }

        void* checkpoint = *reinterpret_cast<void**>(checkpointAddr);
        if (!checkpoint || IsBadReadPtr(checkpoint, 0x160)) {
            return nullptr;
        }

        return checkpoint;
    }

    static int FindCheckpointNodeIndex(void* listBase, void* targetCheckpoint) {
        void* node = GetFirstListNode(listBase);
        void* firstNode = node;
        int index = 0;

        while (node != nullptr && index < 1000) {
            void* checkpoint = GetCheckpointFromNode(node);
            if (checkpoint == targetCheckpoint) {
                return index;
            }

            index++;
            node = GetNextListNode(node);
            
            if (node == firstNode) {
                break;
            }
            
            if (node && IsBadReadPtr(node, 0x10)) {
                break;
            }
        }

        return -1;
    }

    // Public API
    bool Initialize(uintptr_t baseAddress) {
        if (g_initialized) {
            LOG_WARNING("[Respawn] Already initialized");
            return true;
        }

        if (baseAddress == 0) {
            LOG_ERROR("[Respawn] Invalid base address");
            return false;
        }

        g_baseAddress = baseAddress;
        g_globalStructPtr = reinterpret_cast<void**>(baseAddress + GLOBAL_STRUCT_RVA);
        g_handlePlayerRespawnFunc = reinterpret_cast<HandlePlayerRespawnFunc>(baseAddress + HANDLE_PLAYER_RESPAWN_RVA);

        if (IsBadReadPtr(g_globalStructPtr, sizeof(void*))) {
            LOG_ERROR("[Respawn] Invalid global struct pointer");
            return false;
        }

        g_initialized = true;

        LOG_VERBOSE("[Respawn] Initialized successfully");
        LOG_VERBOSE("[Respawn] - Hotkey: Q = Respawn at current checkpoint");
        LOG_VERBOSE("[Respawn] - Hotkey: W = Respawn at previous checkpoint");
        LOG_VERBOSE("[Respawn] - Hotkey: E = Respawn at next checkpoint");
        LOG_VERBOSE("[Respawn] - Hotkey: F = Forward 5 checkpoints");
        LOG_VERBOSE("[Respawn] - Hotkey: Ctrl+0-9 = Jump to checkpoint 0-9");
        LOG_VERBOSE("[Respawn] - HandlePlayerRespawn @ 0x" << std::hex << (g_baseAddress + HANDLE_PLAYER_RESPAWN_RVA));

        return true;
    }

    void Shutdown() {
        if (!g_initialized) {
            return;
        }

        g_initialized = false;
        g_globalStructPtr = nullptr;
        g_handlePlayerRespawnFunc = nullptr;

        LOG_VERBOSE("[Respawn] Shutdown complete");
    }

    bool RespawnAtCheckpoint() {
        return RespawnAtCheckpointOffset(0);
    }

    bool RespawnAtNextCheckpoint() {
        return RespawnAtCheckpointOffset(1);
    }

    bool RespawnAtPreviousCheckpoint() {
        return RespawnAtCheckpointOffset(-1);
    }

    bool RespawnAtCheckpointIndex(int index) {
        if (!g_initialized) {
            LOG_ERROR("[Respawn] Not initialized");
            return false;
        }

        void* manager = GetGameManager();
        if (!manager) {
            LOG_ERROR("[Respawn] Could not get game manager");
            return false;
        }

        void* bikePtr = GetBikePointer();
        if (!bikePtr) {
            LOG_ERROR("[Respawn] Could not get player bike pointer");
            return false;
        }

        void* listBase = GetCheckpointListBase(manager);
        if (!listBase) {
            LOG_ERROR("[Respawn] Could not get checkpoint list base");
            return false;
        }

        int totalCheckpoints = CountCheckpoints(listBase);
        if (index < 0 || index >= totalCheckpoints) {
            LOG_ERROR("[Respawn] Invalid checkpoint index " << std::dec << index << " (total: " << totalCheckpoints << ")");
            return false;
        }

        void* targetCheckpoint = GetCheckpointAtIndex(listBase, index);
        if (!targetCheckpoint) {
            LOG_ERROR("[Respawn] Could not get checkpoint at index " << std::dec << index);
            return false;
        }

        void* currentCheckpoint = GetCurrentCheckpoint(bikePtr);
        if (targetCheckpoint != currentCheckpoint) {
            uintptr_t checkpointPtrAddr = reinterpret_cast<uintptr_t>(bikePtr) + 0x1dc;
            if (!IsBadWritePtr((void*)checkpointPtrAddr, sizeof(void*))) {
                *reinterpret_cast<void**>(checkpointPtrAddr) = targetCheckpoint;
            }
        }

        LOG_VERBOSE("[Respawn] Calling HandlePlayerRespawn for checkpoint index " << std::dec << index);
        bool success = CallRespawnFunction(bikePtr, 0, 0x802, reinterpret_cast<int>(targetCheckpoint), 1);
        
        if (success) {
            LOG_VERBOSE("[Respawn] Respawn successful at checkpoint " << std::dec << index);
        }

        return success;
    }

    int GetCheckpointCount() {
        if (!g_initialized) {
            return 0;
        }

        void* manager = GetGameManager();
        if (!manager) {
            return 0;
        }

        void* listBase = GetCheckpointListBase(manager);
        if (!listBase) {
            return 0;
        }

        return CountCheckpoints(listBase);
    }

    int GetCurrentCheckpointIndex() {
        if (!g_initialized) {
            return -1;
        }

        void* manager = GetGameManager();
        if (!manager) {
            return -1;
        }

        void* bikePtr = GetBikePointer();
        if (!bikePtr) {
            return -1;
        }

        void* listBase = GetCheckpointListBase(manager);
        if (!listBase) {
            return -1;
        }

        void* currentCheckpoint = GetCurrentCheckpoint(bikePtr);
        if (!currentCheckpoint) {
            return -1;
        }

        return FindCheckpointNodeIndex(listBase, currentCheckpoint);
    }

    bool RespawnAtCheckpointOffset(int offset) {
        if (!g_initialized) {
            LOG_ERROR("[Respawn] Not initialized");
            return false;
        }

        void* manager = GetGameManager();
        if (!manager) {
            LOG_ERROR("[Respawn] Could not get game manager");
            return false;
        }

        void* bikePtr = GetBikePointer();
        if (!bikePtr) {
            LOG_ERROR("[Respawn] Could not get player bike pointer");
            return false;
        }

        void* listBase = GetCheckpointListBase(manager);
        if (!listBase) {
            LOG_ERROR("[Respawn] Could not get checkpoint list base");
            return false;
        }

        void* currentCheckpoint = GetCurrentCheckpoint(bikePtr);
        if (!currentCheckpoint) {
            LOG_ERROR("[Respawn] Could not get current checkpoint");
            return false;
        }

        int currentIndex = FindCheckpointNodeIndex(listBase, currentCheckpoint);
        if (currentIndex < 0) {
            LOG_ERROR("[Respawn] Could not find current checkpoint in list");
            return false;
        }

        int totalCheckpoints = CountCheckpoints(listBase);
        int targetIndex = currentIndex + offset;

        if (targetIndex < 0) {
            targetIndex = 0;
        }
        if (targetIndex >= totalCheckpoints) {
            targetIndex = totalCheckpoints - 1;
        }

        void* targetCheckpoint = GetCheckpointAtIndex(listBase, targetIndex);
        if (!targetCheckpoint) {
            LOG_ERROR("[Respawn] Could not get checkpoint at index " << std::dec << targetIndex);
            return false;
        }

        if (offset != 0 && targetCheckpoint != currentCheckpoint) {
            uintptr_t checkpointPtrAddr = reinterpret_cast<uintptr_t>(bikePtr) + 0x1dc;
            if (!IsBadWritePtr((void*)checkpointPtrAddr, sizeof(void*))) {
                *reinterpret_cast<void**>(checkpointPtrAddr) = targetCheckpoint;
            }
        }

        return RespawnAtCheckpointIndex(targetIndex);
    }

    void CheckHotkey() {
        if (!g_initialized) {
            return;
        }

        // Check if Ctrl is held down
        bool ctrlPressed = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;

        // Check Q key - current checkpoint
        bool qKeyIsPressed = (GetAsyncKeyState('Q') & 0x8000) != 0;
        if (qKeyIsPressed && !g_qKeyPressed) {
            RespawnAtCheckpoint();
        }
        g_qKeyPressed = qKeyIsPressed;

        // Check W key - previous checkpoint
        bool wKeyIsPressed = (GetAsyncKeyState('W') & 0x8000) != 0;
        if (wKeyIsPressed && !g_wKeyPressed) {
            RespawnAtPreviousCheckpoint();
        }
        g_wKeyPressed = wKeyIsPressed;

        // Check E key - next checkpoint
        bool eKeyIsPressed = (GetAsyncKeyState('E') & 0x8000) != 0;
        if (eKeyIsPressed && !g_eKeyPressed) {
            RespawnAtNextCheckpoint();
        }
        g_eKeyPressed = eKeyIsPressed;

        // Check R key - forward 5 checkpoints
        bool fKeyIsPressed = (GetAsyncKeyState('F') & 0x8000) != 0;
        if (fKeyIsPressed && !g_fKeyPressed) {
            RespawnAtCheckpointOffset(5);
        }
        g_fKeyPressed = fKeyIsPressed;

        // Check Ctrl+0-9 for direct checkpoint jump
        if (ctrlPressed) {
            for (int i = 0; i < 10; i++) {
                // Use '0' + i for keys 0-9
                int vkCode = '0' + i;
                bool keyIsPressed = (GetAsyncKeyState(vkCode) & 0x8000) != 0;
                
                if (keyIsPressed && !g_numberKeysPressed[i]) {
                    LOG_VERBOSE("[Respawn] Ctrl+" << i << " pressed, jumping to checkpoint " << i);
                    RespawnAtCheckpointIndex(i);
                }
                
                g_numberKeysPressed[i] = keyIsPressed;
            }
        } else {
            // Reset number key states when Ctrl is not pressed
            for (int i = 0; i < 10; i++) {
                g_numberKeysPressed[i] = false;
            }
        }
    }
}
