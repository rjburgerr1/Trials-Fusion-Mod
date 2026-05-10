#include "pch.h"
#include "respawn.h"
#include "logging.h"
#include "keybindings.h"
#include "base-address.h"
#include "prevent-finish.h"
#include "limits.h"
#include <iostream>
#include <Windows.h>

namespace Respawn {
    // ============================================================================
    // Game Memory Addresses - UPLAY VERSION (RVA offsets - base 0x700000)
    // ============================================================================

    // Core function addresses - Uplay
    static constexpr uintptr_t GLOBAL_STRUCT_RVA_UPLAY = 0x104b308;
    static constexpr uintptr_t HANDLE_PLAYER_RESPAWN_RVA_UPLAY = 0x205ae0;
    static constexpr uintptr_t EXECUTE_TASK_WITH_LOCKING_RVA_UPLAY = 0x56e50;
    static constexpr uintptr_t EXECUTE_ASYNC_TASK_RVA_UPLAY = 0x59830;

    // ============================================================================
    // Game Memory Addresses - STEAM VERSION (RVA offsets - base 0x140000)
    // ============================================================================

    // Core function addresses - Steam
    static constexpr uintptr_t GLOBAL_STRUCT_RVA_STEAM = 0x104d308;
    static constexpr uintptr_t HANDLE_PLAYER_RESPAWN_RVA_STEAM = 0x205420;
    static constexpr uintptr_t EXECUTE_TASK_WITH_LOCKING_RVA_STEAM = 0x14b50;
    static constexpr uintptr_t EXECUTE_ASYNC_TASK_RVA_STEAM = 0x7e1360;

    // ============================================================================
    // Helper functions to get correct RVA based on detected version
    // ============================================================================

    static uintptr_t GetGlobalStructRVA() {
        return BaseAddress::IsSteamVersion() ? GLOBAL_STRUCT_RVA_STEAM : GLOBAL_STRUCT_RVA_UPLAY;
    }

    static uintptr_t GetHandlePlayerRespawnRVA() {
        return BaseAddress::IsSteamVersion() ? HANDLE_PLAYER_RESPAWN_RVA_STEAM : HANDLE_PLAYER_RESPAWN_RVA_UPLAY;
    }

    static uintptr_t GetExecuteTaskWithLockingRVA() {
        return BaseAddress::IsSteamVersion() ? EXECUTE_TASK_WITH_LOCKING_RVA_STEAM : EXECUTE_TASK_WITH_LOCKING_RVA_UPLAY;
    }

    static uintptr_t GetExecuteAsyncTaskRVA() {
        return BaseAddress::IsSteamVersion() ? EXECUTE_ASYNC_TASK_RVA_STEAM : EXECUTE_ASYNC_TASK_RVA_UPLAY;
    }

    // Structure offsets
    static constexpr uintptr_t FAULT_COUNTER_OFFSET = 0x898;
    static constexpr uintptr_t GAME_MANAGER_TIME_OFFSET = 0x14;

    // Checkpoint patch locations - Uplay
    static constexpr uintptr_t PROCESS_CHECKPOINT_FINISH_CHECK_RVA_UPLAY = 0x228db2;
    static constexpr uintptr_t PROCESS_CHECKPOINT_EARLY_CHECK_RVA_UPLAY = 0x228d88;
    static constexpr uintptr_t UPDATE_CHECKPOINTS_CALL_RVA_UPLAY = 0x2297ac;
    static constexpr uintptr_t UPDATE_CHECKPOINTS_FIRST_CALL_RVA_UPLAY = 0x229529;
    static constexpr uintptr_t PROCESS_CHECKPOINT_REACHED_RVA_UPLAY = 0x228b60;

    // Checkpoint patch locations - Steam
    static constexpr uintptr_t PROCESS_CHECKPOINT_FINISH_CHECK_RVA_STEAM = 0x228682;
    static constexpr uintptr_t PROCESS_CHECKPOINT_EARLY_CHECK_RVA_STEAM = 0x228658;
    static constexpr uintptr_t UPDATE_CHECKPOINTS_CALL_RVA_STEAM = 0x22907c;
    static constexpr uintptr_t UPDATE_CHECKPOINTS_FIRST_CALL_RVA_STEAM = 0x228df9;
    static constexpr uintptr_t PROCESS_CHECKPOINT_REACHED_RVA_STEAM = 0x228430;

    // Helper functions for checkpoint patch RVAs
    static uintptr_t GetProcessCheckpointFinishCheckRVA() {
        return BaseAddress::IsSteamVersion() ? PROCESS_CHECKPOINT_FINISH_CHECK_RVA_STEAM : PROCESS_CHECKPOINT_FINISH_CHECK_RVA_UPLAY;
    }

    static uintptr_t GetProcessCheckpointEarlyCheckRVA() {
        return BaseAddress::IsSteamVersion() ? PROCESS_CHECKPOINT_EARLY_CHECK_RVA_STEAM : PROCESS_CHECKPOINT_EARLY_CHECK_RVA_UPLAY;
    }

    static uintptr_t GetUpdateCheckpointsCallRVA() {
        return BaseAddress::IsSteamVersion() ? UPDATE_CHECKPOINTS_CALL_RVA_STEAM : UPDATE_CHECKPOINTS_CALL_RVA_UPLAY;
    }

    static uintptr_t GetUpdateCheckpointsFirstCallRVA() {
        return BaseAddress::IsSteamVersion() ? UPDATE_CHECKPOINTS_FIRST_CALL_RVA_STEAM : UPDATE_CHECKPOINTS_FIRST_CALL_RVA_UPLAY;
    }

    static uintptr_t GetProcessCheckpointReachedRVA() {
        return BaseAddress::IsSteamVersion() ? PROCESS_CHECKPOINT_REACHED_RVA_STEAM : PROCESS_CHECKPOINT_REACHED_RVA_UPLAY;
    }

    // Checkpoint structure offsets
    static constexpr uintptr_t CHECKPOINT_INNER_PTR_OFFSET = 0x00;
    static constexpr uintptr_t INNER_TO_FLAGS_OFFSET = 0x44;
    static constexpr uintptr_t FLAGS_OFFSET_8 = 0x08;
    static constexpr uintptr_t FLAGS_OFFSET_A = 0x0A;
    static constexpr uint16_t CHECKPOINT_TRIGGERED_BIT = 0x10;

    // ============================================================================
    // Function Pointer Types
    // ============================================================================

    typedef void(__thiscall* HandlePlayerRespawnFunc)(void* thisPtr, char param1, uint32_t param2, int param3, char param4);
    typedef int(__fastcall* ExecuteTaskWithLockingFunc)(int encryptedDataPtr);
    typedef void(__fastcall* ExecuteAsyncTaskFunc)(void* encryptedDataPtr);

    // ============================================================================
    // Global State
    // ============================================================================

    static bool g_initialized = false;
    static uintptr_t g_baseAddress = 0;
    static void** g_globalStructPtr = nullptr;
    static HandlePlayerRespawnFunc g_handlePlayerRespawnFunc = nullptr;
    static ExecuteTaskWithLockingFunc g_executeTaskWithLocking = nullptr;
    static ExecuteAsyncTaskFunc g_executeAsyncTask = nullptr;

    // Hotkey state
    static bool g_numberKeysPressed[10] = { false };

    // Forward declaration
    bool RespawnAtCheckpointOffset(int offset);

    // ============================================================================
    // SEH-safe Wrapper Functions
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

    static int CallExecuteTaskWithLockingInternal(int encryptedDataPtr, bool* success) {
        __try {
            int result = g_executeTaskWithLocking(encryptedDataPtr);
            *success = true;
            return result;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            *success = false;
            return 0;
        }
    }

    static void CallExecuteAsyncTaskWithValueAsm(void* structPtr, int value) {
        __asm {
            push value
            mov ecx, structPtr
            call g_executeAsyncTask
        }
    }

    static bool CallExecuteAsyncTaskWithValue(void* structPtr, int value) {
        __try {
            CallExecuteAsyncTaskWithValueAsm(structPtr, value);
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
    // Game Object Access Helpers
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
        if (!bikePtr || IsBadReadPtr(bikePtr, 0x900)) {
            return nullptr;
        }

        return bikePtr;
    }

    // ============================================================================
    // Checkpoint Navigation Helpers
    // ============================================================================

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

    // ============================================================================
    // Fault Counter Functions
    // ============================================================================

    int GetFaultCount() {
        if (!g_initialized || !g_executeTaskWithLocking) {
            return -1;
        }

        void* bikePtr = GetBikePointer();
        if (!bikePtr) {
            return -1;
        }

        uintptr_t faultCounterAddr = reinterpret_cast<uintptr_t>(bikePtr) + FAULT_COUNTER_OFFSET;
        if (IsBadReadPtr((void*)faultCounterAddr, 0x10)) {
            return -1;
        }

        bool success = false;
        int faultCount = CallExecuteTaskWithLockingInternal(static_cast<int>(faultCounterAddr), &success);

        if (!success) {
            LOG_ERROR("[Fault] Exception reading fault counter");
            return -1;
        }

        return faultCount;
    }

    bool IncrementFaultCounterBy(int amount) {
        if (!g_initialized) {
            LOG_ERROR("[Fault] Not initialized");
            return false;
        }

        int currentFaults = GetFaultCount();
        if (currentFaults < 0) {
            LOG_ERROR("[Fault] Could not read current fault count");
            return false;
        }

        LOG_VERBOSE("[Fault] Current faults: " << currentFaults);

        void* bikePtr = GetBikePointer();
        if (!bikePtr) {
            LOG_ERROR("[Fault] Could not get bike pointer");
            return false;
        }

        uintptr_t faultCounterAddr = reinterpret_cast<uintptr_t>(bikePtr) + FAULT_COUNTER_OFFSET;
        int newFaults = currentFaults + amount;

        if (newFaults < 0) {
            newFaults = 0;
        }

        void* structPtr = reinterpret_cast<void*>(faultCounterAddr);
        bool success = CallExecuteAsyncTaskWithValue(structPtr, newFaults);

        if (success) {
            LOG_VERBOSE("[Fault] Changed faults from " << currentFaults << " to " << newFaults << " (delta: " << amount << ")");
        }
        else {
            LOG_ERROR("[Fault] Exception in ExecuteAsyncTask");
        }

        return success;
    }

    bool IncrementFaultCounter() {
        return IncrementFaultCounterBy(1);
    }

    bool SetFaultCounterValue(int value) {
        if (!g_initialized) {
            LOG_ERROR("[Fault] Not initialized");
            return false;
        }

        void* bikePtr = GetBikePointer();
        if (!bikePtr) {
            LOG_ERROR("[Fault] Could not get bike pointer");
            return false;
        }

        uintptr_t faultCounterAddr = reinterpret_cast<uintptr_t>(bikePtr) + FAULT_COUNTER_OFFSET;

        if (value < 0) {
            value = 0;
        }

        int currentFaults = GetFaultCount();
        void* structPtr = reinterpret_cast<void*>(faultCounterAddr);
        bool success = CallExecuteAsyncTaskWithValue(structPtr, value);

        if (success) {
            LOG_VERBOSE("[Fault] Set faults from " << currentFaults << " to " << value);
        }
        else {
            LOG_ERROR("[Fault] Exception in ExecuteAsyncTask");
        }

        return success;
    }

    void DebugFaultCounterPath() {
        LOG_INFO("[Fault] === DEBUG FAULT COUNTER ===");
        LOG_INFO("[Fault] Base address: 0x" << std::hex << g_baseAddress);

        void* bikePtr = GetBikePointer();
        if (bikePtr) {
            LOG_INFO("[Fault] Bike pointer: 0x" << std::hex << reinterpret_cast<uintptr_t>(bikePtr));

            uintptr_t faultCounterAddr = reinterpret_cast<uintptr_t>(bikePtr) + FAULT_COUNTER_OFFSET;
            LOG_INFO("[Fault] Fault counter struct @ bike+0x898 = 0x" << std::hex << faultCounterAddr);

            if (!IsBadReadPtr((void*)faultCounterAddr, 0x10)) {
                LOG_INFO("[Fault] Raw encrypted bytes:");
                uint8_t* bytes = reinterpret_cast<uint8_t*>(faultCounterAddr);
                for (int i = 0; i < 16; i++) {
                    LOG_INFO("[Fault]   +0x" << std::hex << i << " = 0x" << std::hex << (int)bytes[i]);
                }
            }

            int faultCount = GetFaultCount();
            LOG_INFO("[Fault] Decrypted fault count: " << std::dec << faultCount);
        }
        else {
            LOG_ERROR("[Fault] Could not get bike pointer");
        }

        LOG_INFO("[Fault] === END DEBUG ===");
    }

    // ============================================================================
    // Time Counter Functions
    // ============================================================================

    static void* GetTimerStructPointer() {
        void* manager = GetGameManager();
        if (!manager) {
            return nullptr;
        }

        uintptr_t timeAddr = reinterpret_cast<uintptr_t>(manager) + GAME_MANAGER_TIME_OFFSET;
        if (IsBadReadPtr((void*)timeAddr, 0x10)) {
            return nullptr;
        }

        return reinterpret_cast<void*>(timeAddr);
    }

    int GetRaceTimeMs() {
        if (!g_initialized || !g_executeTaskWithLocking) {
            return -1;
        }

        void* timeStructPtr = GetTimerStructPointer();
        if (!timeStructPtr) {
            return -1;
        }

        bool success = false;
        int timeMs = CallExecuteTaskWithLockingInternal(reinterpret_cast<int>(timeStructPtr), &success);

        if (!success) {
            LOG_ERROR("[Time] Exception reading time counter");
            return -1;
        }

        return timeMs;
    }

    bool SetRaceTimeMs(int timeMs) {
        if (!g_initialized) {
            LOG_ERROR("[Time] Not initialized");
            return false;
        }

        void* timeStructPtr = GetTimerStructPointer();
        if (!timeStructPtr) {
            LOG_ERROR("[Time] Could not get timer struct pointer");
            return false;
        }

        if (timeMs < 0) {
            timeMs = 0;
        }

        int currentTime = GetRaceTimeMs();
        bool success = CallExecuteAsyncTaskWithValue(timeStructPtr, timeMs);

        if (success) {
            LOG_VERBOSE("[Time] Set time from " << currentTime << " frames to " << timeMs << " frames");
        }
        else {
            LOG_ERROR("[Time] Exception in ExecuteAsyncTask");
        }

        return success;
    }

    bool AdjustRaceTimeMs(int deltaMs) {
        if (!g_initialized) {
            LOG_ERROR("[Time] Not initialized");
            return false;
        }

        int currentTime = GetRaceTimeMs();
        if (currentTime < 0) {
            LOG_ERROR("[Time] Could not read current time");
            return false;
        }

        int newTime = currentTime + deltaMs;
        if (newTime < 0) {
            newTime = 0;
        }

        void* timeStructPtr = GetTimerStructPointer();
        if (!timeStructPtr) {
            LOG_ERROR("[Time] Could not get timer struct pointer");
            return false;
        }

        bool success = CallExecuteAsyncTaskWithValue(timeStructPtr, newTime);

        if (success) {
            LOG_VERBOSE("[Time] Adjusted time from " << currentTime << " frames to " << newTime << " frames (delta: " << deltaMs << " frames)");
        }
        else {
            LOG_ERROR("[Time] Exception in ExecuteAsyncTask");
        }

        return success;
    }

    void DebugTimeCounter() {
        LOG_INFO("[Time] === DEBUG TIME COUNTER ===");

        void* manager = GetGameManager();
        if (!manager) {
            LOG_ERROR("[Time] Could not get game manager");
            return;
        }

        LOG_INFO("[Time] GameManager = 0x" << std::hex << reinterpret_cast<uintptr_t>(manager));

        uintptr_t timeCounterAddr = reinterpret_cast<uintptr_t>(manager) + GAME_MANAGER_TIME_OFFSET;
        LOG_INFO("[Time] timeCounter struct (GameManager+0x14) = 0x" << std::hex << timeCounterAddr);

        if (!IsBadReadPtr((void*)timeCounterAddr, 0x10)) {
            LOG_INFO("[Time] Raw encrypted bytes:");
            uint8_t* bytes = reinterpret_cast<uint8_t*>(timeCounterAddr);
            for (int i = 0; i < 16; i++) {
                LOG_INFO("[Time]   +0x" << std::hex << i << " = 0x" << std::hex << (int)bytes[i]);
            }
        }

        int timeMs = GetRaceTimeMs();
        if (timeMs >= 0) {
            int totalSeconds = timeMs / 1000;
            int minutes = totalSeconds / 60;
            int seconds = totalSeconds % 60;
            int ms = timeMs % 1000;
            LOG_INFO("[Time] Decrypted time: " << std::dec << timeMs << "ms (" << minutes << ":" << (seconds < 10 ? "0" : "") << seconds << "." << ms << ")");
        }
        else {
            LOG_ERROR("[Time] Could not read time");
        }

        LOG_INFO("[Time] === END DEBUG ===");
    }

    // ============================================================================
    // Public API
    // ============================================================================

    bool Initialize(uintptr_t baseAddress) {
        if (g_initialized) {
            LOG_WARNING("[Respawn] Already initialized");
            return true;
        }

        if (baseAddress == 0) {
            LOG_ERROR("[Respawn] Invalid base address");
            return false;
        }

        if (BaseAddress::IsSteamVersion()) {
            LOG_INFO("[Respawn] Steam version detected - using Steam addresses");
        }
        else {
            LOG_INFO("[Respawn] Uplay version detected - using Uplay addresses");
        }

        g_baseAddress = baseAddress;
        g_globalStructPtr = reinterpret_cast<void**>(baseAddress + GetGlobalStructRVA());
        g_handlePlayerRespawnFunc = reinterpret_cast<HandlePlayerRespawnFunc>(baseAddress + GetHandlePlayerRespawnRVA());
        g_executeTaskWithLocking = reinterpret_cast<ExecuteTaskWithLockingFunc>(baseAddress + GetExecuteTaskWithLockingRVA());
        g_executeAsyncTask = reinterpret_cast<ExecuteAsyncTaskFunc>(baseAddress + GetExecuteAsyncTaskRVA());

        if (IsBadReadPtr(g_globalStructPtr, sizeof(void*))) {
            LOG_ERROR("[Respawn] Invalid global struct pointer");
            return false;
        }

        g_initialized = true;

        // Initialize the limits subsystem
        Limits::Initialize(baseAddress);

        return true;
    }

    void Shutdown() {
        if (!g_initialized) {
            return;
        }

        Limits::Shutdown();

        g_initialized = false;
        g_globalStructPtr = nullptr;
        g_handlePlayerRespawnFunc = nullptr;
        g_executeTaskWithLocking = nullptr;
        g_executeAsyncTask = nullptr;

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

            PreventFinish::NotifyCheckpointSkip();
        }

        return RespawnAtCheckpointIndex(targetIndex);
    }

    // ============================================================================
    // Checkpoint Enable/Disable Functions
    // ============================================================================

    void* GetCheckpointPointer(int index) {
        if (!g_initialized) {
            return nullptr;
        }

        void* manager = GetGameManager();
        if (!manager) {
            return nullptr;
        }

        void* listBase = GetCheckpointListBase(manager);
        if (!listBase) {
            return nullptr;
        }

        int totalCheckpoints = CountCheckpoints(listBase);
        if (index < 0 || index >= totalCheckpoints) {
            return nullptr;
        }

        return GetCheckpointAtIndex(listBase, index);
    }

    void DebugCheckpointStructure(int index) {
        LOG_INFO("[Checkpoint] === DEBUG CHECKPOINT " << index << " ===");

        void* checkpoint = GetCheckpointPointer(index);
        if (!checkpoint) {
            LOG_ERROR("[Checkpoint] Could not get checkpoint at index " << index);
            return;
        }

        LOG_INFO("[Checkpoint] Checkpoint ptr: 0x" << std::hex << reinterpret_cast<uintptr_t>(checkpoint));

        if (!IsBadReadPtr(checkpoint, 0x80)) {
            LOG_INFO("[Checkpoint] First 0x80 bytes:");
            uint32_t* data = reinterpret_cast<uint32_t*>(checkpoint);
            for (int i = 0; i < 0x80 / 4; i++) {
                if (i % 4 == 0) {
                    LOG_INFO("  +0x" << std::hex << (i * 4) << ": ");
                }
                LOG_INFO("    [" << i << "] = 0x" << std::hex << data[i]);
            }
        }

        if (!IsBadReadPtr(checkpoint, 0x60)) {
            uint8_t* byteData = reinterpret_cast<uint8_t*>(checkpoint);
            LOG_INFO("[Checkpoint] Checking enable flags:");
            LOG_INFO("[Checkpoint]   checkpoint+0x5C = 0x" << std::hex << (int)byteData[0x5C]);
            LOG_INFO("[Checkpoint]   checkpoint+0x5D = 0x" << std::hex << (int)byteData[0x5D]);
            LOG_INFO("[Checkpoint]   checkpoint+0x5E = 0x" << std::hex << (int)byteData[0x5E]);
            LOG_INFO("[Checkpoint]   checkpoint+0x5F = 0x" << std::hex << (int)byteData[0x5F]);
        }

        void* innerPtr = *reinterpret_cast<void**>(checkpoint);
        LOG_INFO("[Checkpoint] checkpoint[0] (inner ptr): 0x" << std::hex << reinterpret_cast<uintptr_t>(innerPtr));

        if (innerPtr && !IsBadReadPtr(innerPtr, 0x70)) {
            uint8_t* innerBytes = reinterpret_cast<uint8_t*>(innerPtr);
            LOG_INFO("[Checkpoint] Inner struct enable flags:");
            LOG_INFO("[Checkpoint]   inner+0x5C = 0x" << std::hex << (int)innerBytes[0x5C]);
            LOG_INFO("[Checkpoint]   inner+0x5D = 0x" << std::hex << (int)innerBytes[0x5D]);
            LOG_INFO("[Checkpoint]   inner+0x5E = 0x" << std::hex << (int)innerBytes[0x5E]);
            LOG_INFO("[Checkpoint]   inner+0x5F = 0x" << std::hex << (int)innerBytes[0x5F]);

            uintptr_t flagsPtrAddr = reinterpret_cast<uintptr_t>(innerPtr) + 0x44;
            if (!IsBadReadPtr((void*)flagsPtrAddr, sizeof(void*))) {
                void* flagsStruct = *reinterpret_cast<void**>(flagsPtrAddr);
                LOG_INFO("[Checkpoint] inner+0x44 (flags struct): 0x" << std::hex << reinterpret_cast<uintptr_t>(flagsStruct));

                if (flagsStruct && !IsBadReadPtr(flagsStruct, 0x20)) {
                    uint16_t* flags8 = reinterpret_cast<uint16_t*>(reinterpret_cast<uintptr_t>(flagsStruct) + 0x8);
                    uint16_t* flagsA = reinterpret_cast<uint16_t*>(reinterpret_cast<uintptr_t>(flagsStruct) + 0xA);
                    LOG_INFO("[Checkpoint] flagsStruct+0x8: 0x" << std::hex << *flags8);
                    LOG_INFO("[Checkpoint] flagsStruct+0xA: 0x" << std::hex << *flagsA);
                }
            }
        }

        LOG_INFO("[Checkpoint] === END DEBUG ===");
    }

    bool IsCheckpointEnabled(int index) {
        if (!g_initialized) {
            LOG_ERROR("[Checkpoint] Not initialized");
            return false;
        }

        void* checkpoint = GetCheckpointPointer(index);
        if (!checkpoint) {
            LOG_ERROR("[Checkpoint] Could not get checkpoint at index " << index);
            return false;
        }

        if (IsBadReadPtr(checkpoint, sizeof(void*))) {
            LOG_ERROR("[Checkpoint] Cannot read checkpoint");
            return false;
        }

        void* innerPtr = *reinterpret_cast<void**>(checkpoint);
        if (!innerPtr || IsBadReadPtr(innerPtr, 0x48)) {
            LOG_ERROR("[Checkpoint] Invalid inner pointer");
            return false;
        }

        uintptr_t flagsPtrAddr = reinterpret_cast<uintptr_t>(innerPtr) + INNER_TO_FLAGS_OFFSET;
        if (IsBadReadPtr((void*)flagsPtrAddr, sizeof(void*))) {
            LOG_ERROR("[Checkpoint] Cannot read flags pointer");
            return false;
        }

        void* flagsStruct = *reinterpret_cast<void**>(flagsPtrAddr);
        if (!flagsStruct || IsBadReadPtr(flagsStruct, 0x10)) {
            LOG_ERROR("[Checkpoint] Invalid flags struct");
            return false;
        }

        uint16_t* flags = reinterpret_cast<uint16_t*>(reinterpret_cast<uintptr_t>(flagsStruct) + FLAGS_OFFSET_8);

        bool hasTriggeredBit = (*flags & CHECKPOINT_TRIGGERED_BIT) != 0;
        return !hasTriggeredBit;
    }

    bool EnableCheckpoint(int index) {
        if (!g_initialized) {
            LOG_ERROR("[Checkpoint] Not initialized");
            return false;
        }

        void* checkpoint = GetCheckpointPointer(index);
        if (!checkpoint) {
            LOG_ERROR("[Checkpoint] Could not get checkpoint at index " << index);
            return false;
        }

        if (IsBadReadPtr(checkpoint, sizeof(void*))) {
            LOG_ERROR("[Checkpoint] Cannot read checkpoint");
            return false;
        }

        void* innerPtr = *reinterpret_cast<void**>(checkpoint);
        if (!innerPtr || IsBadReadPtr(innerPtr, 0x48)) {
            LOG_ERROR("[Checkpoint] Invalid inner pointer");
            return false;
        }

        uintptr_t flagsPtrAddr = reinterpret_cast<uintptr_t>(innerPtr) + INNER_TO_FLAGS_OFFSET;
        if (IsBadReadPtr((void*)flagsPtrAddr, sizeof(void*))) {
            LOG_ERROR("[Checkpoint] Cannot read flags pointer");
            return false;
        }

        void* flagsStruct = *reinterpret_cast<void**>(flagsPtrAddr);
        if (!flagsStruct || IsBadWritePtr(flagsStruct, 0x10)) {
            LOG_ERROR("[Checkpoint] Invalid/unwritable flags struct");
            return false;
        }

        uint16_t* flags8 = reinterpret_cast<uint16_t*>(reinterpret_cast<uintptr_t>(flagsStruct) + FLAGS_OFFSET_8);
        uint16_t* flagsA = reinterpret_cast<uint16_t*>(reinterpret_cast<uintptr_t>(flagsStruct) + FLAGS_OFFSET_A);

        *flags8 &= ~CHECKPOINT_TRIGGERED_BIT;
        *flagsA &= ~CHECKPOINT_TRIGGERED_BIT;

        LOG_INFO("[Checkpoint] Enabled checkpoint " << index << " (cleared trigger bits)");
        return true;
    }

    bool DisableCheckpoint(int index) {
        if (!g_initialized) {
            LOG_ERROR("[Checkpoint] Not initialized");
            return false;
        }

        void* checkpoint = GetCheckpointPointer(index);
        if (!checkpoint) {
            LOG_ERROR("[Checkpoint] Could not get checkpoint at index " << index);
            return false;
        }

        if (IsBadReadPtr(checkpoint, sizeof(void*))) {
            LOG_ERROR("[Checkpoint] Cannot read checkpoint");
            return false;
        }

        void* innerPtr = *reinterpret_cast<void**>(checkpoint);
        if (!innerPtr) {
            LOG_ERROR("[Checkpoint] Inner pointer is null");
            return false;
        }

        if (IsBadReadPtr(innerPtr, 0x60)) {
            LOG_ERROR("[Checkpoint] Cannot read inner pointer memory");
            return false;
        }

        if (!IsBadWritePtr(checkpoint, 0x60)) {
            uint8_t* checkpointBytes = reinterpret_cast<uint8_t*>(checkpoint);
            checkpointBytes[0x5C] = 0;
            checkpointBytes[0x5D] = 0;
            LOG_VERBOSE("[Checkpoint] Set checkpoint+0x5C/5D to 0");
        }

        if (!IsBadWritePtr(innerPtr, 0x60)) {
            uint8_t* innerBytes = reinterpret_cast<uint8_t*>(innerPtr);
            innerBytes[0x5C] = 0;
            innerBytes[0x5D] = 0;
            LOG_VERBOSE("[Checkpoint] Set inner+0x5C/5D to 0");
        }

        uintptr_t flagsPtrAddr = reinterpret_cast<uintptr_t>(innerPtr) + INNER_TO_FLAGS_OFFSET;
        if (IsBadReadPtr((void*)flagsPtrAddr, sizeof(void*))) {
            LOG_ERROR("[Checkpoint] Cannot read flags pointer");
            return false;
        }

        void* flagsStruct = *reinterpret_cast<void**>(flagsPtrAddr);
        if (!flagsStruct) {
            LOG_ERROR("[Checkpoint] Flags struct is null");
            return false;
        }

        if (IsBadWritePtr(flagsStruct, 0x10)) {
            LOG_ERROR("[Checkpoint] Cannot write to flags struct");
            return false;
        }

        uint16_t* flags8 = reinterpret_cast<uint16_t*>(reinterpret_cast<uintptr_t>(flagsStruct) + FLAGS_OFFSET_8);
        uint16_t* flagsA = reinterpret_cast<uint16_t*>(reinterpret_cast<uintptr_t>(flagsStruct) + FLAGS_OFFSET_A);

        *flags8 |= CHECKPOINT_TRIGGERED_BIT;
        *flagsA |= CHECKPOINT_TRIGGERED_BIT;

        LOG_INFO("[Checkpoint] Disabled checkpoint " << index << " (set trigger bits)");
        return true;
    }

    bool ToggleCheckpoint(int index) {
        if (IsCheckpointEnabled(index)) {
            return DisableCheckpoint(index);
        }
        else {
            return EnableCheckpoint(index);
        }
    }

    bool PatchUpdateCheckpointsCall() {
        if (!g_initialized) {
            LOG_ERROR("[UpdateCheckpointsPatch] Not initialized");
            return false;
        }

        uintptr_t patchAddr = g_baseAddress + GetUpdateCheckpointsCallRVA();

        LOG_VERBOSE("[UpdateCheckpointsPatch] Patching checkpoint processing block at 0x" << std::hex << patchAddr);

        if (IsBadReadPtr((void*)patchAddr, 24)) {
            LOG_ERROR("[UpdateCheckpointsPatch] Cannot read patch address");
            return false;
        }

        uint8_t* bytes = reinterpret_cast<uint8_t*>(patchAddr);

        if (bytes[0] == 0xEB) {
            LOG_VERBOSE("[UpdateCheckpointsPatch] Already patched");
            return true;
        }

        if (bytes[0] != 0x51) {
            LOG_ERROR("[UpdateCheckpointsPatch] Expected PUSH ECX (0x51), found 0x" << std::hex << (int)bytes[0]);
            return false;
        }

        DWORD oldProtect;
        if (!VirtualProtect((void*)patchAddr, 24, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            LOG_ERROR("[UpdateCheckpointsPatch] Failed to change memory protection");
            return false;
        }

        bytes[0] = 0xEB;
        bytes[1] = 0x15;

        for (int i = 2; i < 24; i++) {
            bytes[i] = 0x90;
        }

        VirtualProtect((void*)patchAddr, 24, oldProtect, &oldProtect);

        LOG_VERBOSE("[UpdateCheckpointsPatch] SUCCESS! Added JMP to skip checkpoint processing!");
        return true;
    }

    bool UnpatchUpdateCheckpointsCall() {
        if (!g_initialized) {
            LOG_ERROR("[UpdateCheckpointsUnpatch] Not initialized");
            return false;
        }

        uintptr_t patchAddr = g_baseAddress + GetUpdateCheckpointsCallRVA();

        LOG_VERBOSE("[UpdateCheckpointsUnpatch] Restoring checkpoint processing block at 0x" << std::hex << patchAddr);

        if (IsBadReadPtr((void*)patchAddr, 24)) {
            LOG_ERROR("[UpdateCheckpointsUnpatch] Cannot read patch address");
            return false;
        }

        uint8_t* bytes = reinterpret_cast<uint8_t*>(patchAddr);

        if (bytes[0] == 0x51) {
            LOG_VERBOSE("[UpdateCheckpointsUnpatch] Already restored");
            return true;
        }

        if (bytes[0] != 0xEB) {
            LOG_ERROR("[UpdateCheckpointsUnpatch] Unexpected bytes: 0x" << std::hex << (int)bytes[0]);
            return false;
        }

        DWORD oldProtect;
        if (!VirtualProtect((void*)patchAddr, 24, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            LOG_ERROR("[UpdateCheckpointsUnpatch] Failed to change memory protection");
            return false;
        }

        bytes[0] = 0x51;
        bytes[1] = 0xF3;
        bytes[2] = 0x0F;
        bytes[3] = 0x11;
        bytes[4] = 0x04;
        bytes[5] = 0x24;
        bytes[6] = 0x57;
        bytes[7] = 0x8B;
        bytes[8] = 0xCE;

        uintptr_t callInstructionAddr = patchAddr + 9;
        uintptr_t callTarget = g_baseAddress + GetProcessCheckpointReachedRVA();
        int32_t callOffset = static_cast<int32_t>(callTarget - (callInstructionAddr + 5));
        bytes[9] = 0xE8;
        *reinterpret_cast<int32_t*>(&bytes[10]) = callOffset;

        bytes[14] = 0x8B;
        bytes[15] = 0x5D;
        bytes[16] = 0xDC;
        bytes[17] = 0x89;
        bytes[18] = 0xBE;
        bytes[19] = 0xDC;
        bytes[20] = 0x01;
        bytes[21] = 0x00;
        bytes[22] = 0x00;

        VirtualProtect((void*)patchAddr, 24, oldProtect, &oldProtect);

        LOG_VERBOSE("[UpdateCheckpointsUnpatch] SUCCESS! Restored checkpoint processing block!");
        return true;
    }

    bool PatchUpdateCheckpointsFirstCall() {
        if (!g_initialized) {
            LOG_ERROR("[UpdateCheckpointsFirstPatch] Not initialized");
            return false;
        }

        uintptr_t patchAddr = g_baseAddress + GetUpdateCheckpointsFirstCallRVA();

        LOG_VERBOSE("[UpdateCheckpointsFirstPatch] Patching first checkpoint call at 0x" << std::hex << patchAddr);

        if (IsBadReadPtr((void*)patchAddr, 23)) {
            LOG_ERROR("[UpdateCheckpointsFirstPatch] Cannot read patch address");
            return false;
        }

        uint8_t* bytes = reinterpret_cast<uint8_t*>(patchAddr);

        if (bytes[0] == 0xEB) {
            LOG_VERBOSE("[UpdateCheckpointsFirstPatch] Already patched");
            return true;
        }

        if (bytes[0] != 0x0F || bytes[1] != 0x57 || bytes[2] != 0xC0) {
            LOG_ERROR("[UpdateCheckpointsFirstPatch] Expected XORPS (0F 57 C0), found 0x"
                << std::hex << (int)bytes[0] << " 0x" << (int)bytes[1] << " 0x" << (int)bytes[2]);
            return false;
        }

        DWORD oldProtect;
        if (!VirtualProtect((void*)patchAddr, 23, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            LOG_ERROR("[UpdateCheckpointsFirstPatch] Failed to change memory protection");
            return false;
        }

        bytes[0] = 0xEB;
        bytes[1] = 0x15;

        for (int i = 2; i < 23; i++) {
            bytes[i] = 0x90;
        }

        VirtualProtect((void*)patchAddr, 23, oldProtect, &oldProtect);

        LOG_VERBOSE("[UpdateCheckpointsFirstPatch] SUCCESS! Added JMP to skip first checkpoint call!");
        return true;
    }

    bool UnpatchUpdateCheckpointsFirstCall() {
        if (!g_initialized) {
            LOG_ERROR("[UpdateCheckpointsFirstUnpatch] Not initialized");
            return false;
        }

        uintptr_t patchAddr = g_baseAddress + GetUpdateCheckpointsFirstCallRVA();

        LOG_VERBOSE("[UpdateCheckpointsFirstUnpatch] Restoring first checkpoint call at 0x" << std::hex << patchAddr);

        if (IsBadReadPtr((void*)patchAddr, 23)) {
            LOG_ERROR("[UpdateCheckpointsFirstUnpatch] Cannot read patch address");
            return false;
        }

        uint8_t* bytes = reinterpret_cast<uint8_t*>(patchAddr);

        if (bytes[0] == 0x0F && bytes[1] == 0x57) {
            LOG_VERBOSE("[UpdateCheckpointsFirstUnpatch] Already restored");
            return true;
        }

        if (bytes[0] != 0xEB) {
            LOG_ERROR("[UpdateCheckpointsFirstUnpatch] Unexpected bytes: 0x" << std::hex << (int)bytes[0]);
            return false;
        }

        DWORD oldProtect;
        if (!VirtualProtect((void*)patchAddr, 23, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            LOG_ERROR("[UpdateCheckpointsFirstUnpatch] Failed to change memory protection");
            return false;
        }

        bytes[0] = 0x0F;
        bytes[1] = 0x57;
        bytes[2] = 0xC0;
        bytes[3] = 0x51;
        bytes[4] = 0x8B;
        bytes[5] = 0x8E;
        bytes[6] = 0xDC;
        bytes[7] = 0x01;
        bytes[8] = 0x00;
        bytes[9] = 0x00;
        bytes[10] = 0xF3;
        bytes[11] = 0x0F;
        bytes[12] = 0x11;
        bytes[13] = 0x04;
        bytes[14] = 0x24;
        bytes[15] = 0x51;
        bytes[16] = 0x8B;
        bytes[17] = 0xCE;

        uintptr_t callInstructionAddr = patchAddr + 18;
        uintptr_t callTarget = g_baseAddress + GetProcessCheckpointReachedRVA();
        int32_t callOffset = static_cast<int32_t>(callTarget - (callInstructionAddr + 5));
        bytes[18] = 0xE8;
        *reinterpret_cast<int32_t*>(&bytes[19]) = callOffset;

        VirtualProtect((void*)patchAddr, 23, oldProtect, &oldProtect);

        LOG_VERBOSE("[UpdateCheckpointsFirstUnpatch] SUCCESS! Restored first checkpoint call!");
        return true;
    }

    bool PatchCheckpointEarlyReturn() {
        if (!g_initialized) {
            LOG_ERROR("[CheckpointEarlyReturn] Not initialized");
            return false;
        }

        uintptr_t patchAddr = g_baseAddress + GetProcessCheckpointEarlyCheckRVA() + 0xD;

        LOG_VERBOSE("[CheckpointEarlyReturn] Patching checkpoint early return at 0x" << std::hex << patchAddr);

        if (IsBadReadPtr((void*)patchAddr, 13)) {
            LOG_ERROR("[CheckpointEarlyReturn] Cannot read patch address");
            return false;
        }

        uint8_t* bytes = reinterpret_cast<uint8_t*>(patchAddr);

        if (bytes[0] == 0x66 && bytes[1] == 0xF7) {
            LOG_VERBOSE("[CheckpointEarlyReturn] Already patched");
            return true;
        }

        if (bytes[0] != 0xB9 || bytes[1] != 0x10) {
            LOG_ERROR("[CheckpointEarlyReturn] Unexpected bytes: 0x" << std::hex << (int)bytes[0] << " 0x" << (int)bytes[1]);
            return false;
        }

        DWORD oldProtect;
        if (!VirtualProtect((void*)patchAddr, 13, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            LOG_ERROR("[CheckpointEarlyReturn] Failed to change memory protection");
            return false;
        }

        int32_t jumpOffset = 0x21F;

        bytes[0] = 0x66;
        bytes[1] = 0xF7;
        bytes[2] = 0x40;
        bytes[3] = 0x08;
        bytes[4] = 0x10;
        bytes[5] = 0x00;
        bytes[6] = 0x0F;
        bytes[7] = 0x85;
        *reinterpret_cast<int32_t*>(&bytes[8]) = jumpOffset;
        bytes[12] = 0x90;

        VirtualProtect((void*)patchAddr, 13, oldProtect, &oldProtect);

        LOG_VERBOSE("[CheckpointEarlyReturn] SUCCESS! Added early return check for triggered checkpoints!");
        return true;
    }

    bool UnpatchCheckpointEarlyReturn() {
        if (!g_initialized) {
            LOG_ERROR("[CheckpointEarlyReturnRestore] Not initialized");
            return false;
        }

        uintptr_t patchAddr = g_baseAddress + GetProcessCheckpointEarlyCheckRVA() + 0xD;

        LOG_VERBOSE("[CheckpointEarlyReturnRestore] Restoring original checkpoint code at 0x" << std::hex << patchAddr);

        if (IsBadReadPtr((void*)patchAddr, 13)) {
            LOG_ERROR("[CheckpointEarlyReturnRestore] Cannot read patch address");
            return false;
        }

        uint8_t* bytes = reinterpret_cast<uint8_t*>(patchAddr);

        if (bytes[0] == 0xB9 && bytes[1] == 0x10) {
            LOG_VERBOSE("[CheckpointEarlyReturnRestore] Already restored");
            return true;
        }

        if (bytes[0] != 0x66 || bytes[1] != 0xF7) {
            LOG_ERROR("[CheckpointEarlyReturnRestore] Unexpected bytes: 0x" << std::hex << (int)bytes[0] << " 0x" << (int)bytes[1]);
            return false;
        }

        DWORD oldProtect;
        if (!VirtualProtect((void*)patchAddr, 13, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            LOG_ERROR("[CheckpointEarlyReturnRestore] Failed to change memory protection");
            return false;
        }

        bytes[0] = 0xB9;
        bytes[1] = 0x10;
        bytes[2] = 0x00;
        bytes[3] = 0x00;
        bytes[4] = 0x00;
        bytes[5] = 0x66;
        bytes[6] = 0x09;
        bytes[7] = 0x48;
        bytes[8] = 0x0A;
        bytes[9] = 0x66;
        bytes[10] = 0x09;
        bytes[11] = 0x48;
        bytes[12] = 0x08;

        VirtualProtect((void*)patchAddr, 13, oldProtect, &oldProtect);

        LOG_VERBOSE("[CheckpointEarlyReturnRestore] SUCCESS! Restored original checkpoint processing!");
        return true;
    }

    bool PatchFinishLineCheck() {
        if (!g_initialized) {
            LOG_ERROR("[FinishLinePatch] Not initialized");
            return false;
        }

        uintptr_t jnzAddr = g_baseAddress + GetProcessCheckpointFinishCheckRVA();

        LOG_VERBOSE("[FinishLinePatch] Patching finish line check at 0x" << std::hex << jnzAddr);

        if (IsBadReadPtr((void*)jnzAddr, 6)) {
            LOG_ERROR("[FinishLinePatch] Cannot read JNZ address");
            return false;
        }

        uint8_t* bytes = reinterpret_cast<uint8_t*>(jnzAddr);

        if (bytes[0] == 0xEB) {
            LOG_VERBOSE("[FinishLinePatch] Already patched (found JMP)");
            return true;
        }

        if (bytes[0] == 0x75 || (bytes[0] == 0x0F && bytes[1] == 0x85)) {
            DWORD oldProtect;
            int patchSize = (bytes[0] == 0x75) ? 2 : 6;

            if (!VirtualProtect((void*)jnzAddr, patchSize, PAGE_EXECUTE_READWRITE, &oldProtect)) {
                LOG_ERROR("[FinishLinePatch] Failed to change memory protection");
                return false;
            }

            if (patchSize == 2) {
                bytes[0] = 0xEB;
            }
            else {
                bytes[0] = 0xE9;
            }

            VirtualProtect((void*)jnzAddr, patchSize, oldProtect, &oldProtect);

            LOG_VERBOSE("[FinishLinePatch] SUCCESS! Converted JNZ to JMP - finish line won't trigger finish logic!");
            return true;
        }

        LOG_ERROR("[FinishLinePatch] Unexpected instruction: 0x" << std::hex << (int)bytes[0]);
        return false;
    }

    bool UnpatchFinishLineCheck() {
        if (!g_initialized) {
            LOG_ERROR("[FinishLineUnpatch] Not initialized");
            return false;
        }

        uintptr_t jmpAddr = g_baseAddress + GetProcessCheckpointFinishCheckRVA();

        LOG_VERBOSE("[FinishLineUnpatch] Restoring finish line check at 0x" << std::hex << jmpAddr);

        if (IsBadReadPtr((void*)jmpAddr, 6)) {
            LOG_ERROR("[FinishLineUnpatch] Cannot read JMP address");
            return false;
        }

        uint8_t* bytes = reinterpret_cast<uint8_t*>(jmpAddr);

        if (bytes[0] == 0x75 || (bytes[0] == 0x0F && bytes[1] == 0x85)) {
            LOG_VERBOSE("[FinishLineUnpatch] Already restored (found JNZ)");
            return true;
        }

        if (bytes[0] != 0xEB && bytes[0] != 0xE9) {
            LOG_ERROR("[FinishLineUnpatch] Unexpected instruction: 0x" << std::hex << (int)bytes[0]);
            return false;
        }

        DWORD oldProtect;
        int patchSize = (bytes[0] == 0xEB) ? 2 : 6;

        if (!VirtualProtect((void*)jmpAddr, patchSize, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            LOG_ERROR("[FinishLineUnpatch] Failed to change memory protection");
            return false;
        }

        if (patchSize == 2) {
            bytes[0] = 0x75;
        }
        else {
            bytes[0] = 0x0F;
            bytes[1] = 0x85;
        }

        VirtualProtect((void*)jmpAddr, patchSize, oldProtect, &oldProtect);

        LOG_VERBOSE("[FinishLineUnpatch] SUCCESS! Restored JNZ - finish line will trigger finish logic normally!");
        return true;
    }

    void CheckHotkey() {
        if (!g_initialized) {
            return;
        }

        bool ctrlPressed = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;

        // Respawn at current checkpoint
        if (Keybindings::IsActionPressed(Keybindings::Action::RespawnAtCheckpoint)) {
            RespawnAtCheckpoint();
        }

        // Previous checkpoint
        if (Keybindings::IsActionPressed(Keybindings::Action::RespawnPrevCheckpoint)) {
            RespawnAtPreviousCheckpoint();
        }

        // Next checkpoint
        if (Keybindings::IsActionPressed(Keybindings::Action::RespawnNextCheckpoint)) {
            RespawnAtNextCheckpoint();
        }

        // Forward 5 checkpoints
        if (Keybindings::IsActionPressed(Keybindings::Action::RespawnForward5)) {
            RespawnAtCheckpointOffset(5);
        }

        // Increment fault counter
        if (Keybindings::IsActionPressed(Keybindings::Action::IncrementFault)) {
            IncrementFaultCounter();
        }

        // Debug fault counter
        if (Keybindings::IsActionPressed(Keybindings::Action::DebugFaultCounter)) {
            DebugFaultCounterPath();
        }

        // Add 100 faults
        if (Keybindings::IsActionPressed(Keybindings::Action::Add100Faults)) {
            IncrementFaultCounterBy(100);
        }

        // Subtract 100 faults
        if (Keybindings::IsActionPressed(Keybindings::Action::Subtract100Faults)) {
            IncrementFaultCounterBy(-100);
            PreventFinish::NotifyFaultReduction();
        }

        // Reset faults to 0
        if (Keybindings::IsActionPressed(Keybindings::Action::ResetFaults)) {
            SetFaultCounterValue(0);
            PreventFinish::NotifyFaultReduction();
        }

        // Debug time counter
        if (Keybindings::IsActionPressed(Keybindings::Action::DebugTimeCounter)) {
            DebugTimeCounter();
        }

        // Add 60 seconds / 3600 frames at 60fps
        if (Keybindings::IsActionPressed(Keybindings::Action::Add60Seconds)) {
            AdjustRaceTimeMs(3600);
        }

        // Subtract 60 seconds / 3600 frames at 60fps
        if (Keybindings::IsActionPressed(Keybindings::Action::Subtract60Seconds)) {
            AdjustRaceTimeMs(-3600);
            PreventFinish::NotifyTimeReduction();
        }

        // Add 10 minute / 36000 frames at 60fps
        if (Keybindings::IsActionPressed(Keybindings::Action::Add10Minute)) {
            AdjustRaceTimeMs(36000);
        }

        // Reset time to 0
        if (Keybindings::IsActionPressed(Keybindings::Action::ResetTime)) {
            SetRaceTimeMs(0);
            PreventFinish::NotifyTimeReduction();
        }

        // Toggle ALL limit validation (F4) - now uses Limits namespace
        if (Keybindings::IsActionPressed(Keybindings::Action::ToggleLimitValidation)) {
            // Check current state by looking at one of the patches
            if (Limits::IsFaultValidationDisabled() || Limits::IsTimeValidationDisabled()) {
                // Currently disabled, so enable
                Limits::EnableAllLimitValidation();
            }
            else {
                // Currently enabled, so disable
                Limits::DisableAllLimitValidation();
            }
        }

        // Ctrl+0-9 = Jump to checkpoint
        if (ctrlPressed) {
            for (int i = 0; i < 10; i++) {
                int vkCode = '0' + i;
                bool keyIsPressed = (GetAsyncKeyState(vkCode) & 0x8000) != 0;

                if (keyIsPressed && !g_numberKeysPressed[i]) {
                    LOG_VERBOSE("[Respawn] Ctrl+" << i << " pressed, jumping to checkpoint " << i);
                    int currentCp = GetCurrentCheckpointIndex();
                    if (i != currentCp) {
                        PreventFinish::NotifyCheckpointSkip();
                    }
                    RespawnAtCheckpointIndex(i);
                }

                g_numberKeysPressed[i] = keyIsPressed;
            }
        }
        else {
            for (int i = 0; i < 10; i++) {
                g_numberKeysPressed[i] = false;
            }
        }
    }
}