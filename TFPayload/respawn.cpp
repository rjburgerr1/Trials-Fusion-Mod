#include "pch.h"
#include "respawn.h"
#include "logging.h"
#include "keybindings.h"
#include <iostream>
#include <Windows.h>

namespace Respawn {
    // ============================================================================
    // Game Memory Addresses (RVA offsets from base 0x700000)
    // ============================================================================

    static constexpr uintptr_t GLOBAL_STRUCT_RVA = 0x104b308;
    static constexpr uintptr_t HANDLE_PLAYER_RESPAWN_RVA = 0x205ae0;
    static constexpr uintptr_t EXECUTE_TASK_WITH_LOCKING_RVA = 0x56e50;
    static constexpr uintptr_t EXECUTE_ASYNC_TASK_RVA = 0x59830;

    static constexpr uintptr_t FAULT_COUNTER_OFFSET = 0x898;
    static constexpr uintptr_t GAME_MANAGER_TIME_OFFSET = 0x14;

    // Limit bypass patch locations
    static constexpr uintptr_t TIME_LIMIT_CMP_RVA = 0x276e08;
    static constexpr uintptr_t FAULT_LIMIT_CMP_RVA = 0x276e4f;
    static constexpr uintptr_t COMPLETION_COND_FAULT_JNC_RVA = 0x231567;
    static constexpr uintptr_t COMPLETION_COND_TIME_JNC_RVA = 0x23155e;
    static constexpr uintptr_t COMPLETION_COND_TIME2_JC_RVA = 0x23158e;
    static constexpr uintptr_t UPDATE_RACE_END_PUSH_RVA = 0x280d93;
    static constexpr uintptr_t RACEUPDATE_TIMER_CHECK_RVA = 0x28f1b6;

    // Multiplayer limit checks (in check_race_completion_conditions)
    static constexpr uintptr_t MULTIPLAYER_MODE1_TIME_CMP_RVA = 0x231484;  // 0x931484 - 0x700000
    static constexpr uintptr_t MULTIPLAYER_MODE1_TIME_JNC_RVA = 0x23148b;  // 0x93148b - 0x700000
    static constexpr uintptr_t MULTIPLAYER_MODE1_FAULT_CALL_RVA = 0x23148f; // 0x93148f - 0x700000 (CALL CheckTimerThreshold)
    static constexpr uintptr_t MULTIPLAYER_MODE1_FAULT_JNZ_RVA = 0x231496;  // 0x931496 - 0x700000 (JNZ respawn)
    static constexpr uintptr_t MULTIPLAYER_MODE2_TIME_CMP_RVA = 0x2313d4;  // 0x9313d4 - 0x700000
    static constexpr uintptr_t MULTIPLAYER_MODE2_TIME_JNC_RVA = 0x2313db;  // 0x9313db - 0x700000
    static constexpr uintptr_t MULTIPLAYER_MODE2_FAULT_CALL_RVA = 0x2313df; // 0x9313df - 0x700000 (CALL CheckTimerThreshold)
    static constexpr uintptr_t MULTIPLAYER_MODE2_FAULT_JNZ_RVA = 0x2313e6;  // 0x9313e6 - 0x700000 (JNZ respawn)

    // Finish line check patch location
    static constexpr uintptr_t PROCESS_CHECKPOINT_FINISH_CHECK_RVA = 0x228db2; // JNZ at 0x928db2
    
    // Checkpoint trigger bit check location - add early return if checkpoint already triggered
    static constexpr uintptr_t PROCESS_CHECKPOINT_EARLY_CHECK_RVA = 0x228d88; // MOV EAX,dword ptr [ESI] at 0x928d88
    
    // UpdateCheckpointsInRange call to ProcessCheckpointReached location (second call in loop)
    static constexpr uintptr_t UPDATE_CHECKPOINTS_CALL_RVA = 0x2297ac; // PUSH ECX at 0x9297ac (start of block to skip)
    
    // UpdateCheckpointsInRange first call to ProcessCheckpointReached (at start of function)
    // This call happens when the current checkpoint equals the first valid element
    static constexpr uintptr_t UPDATE_CHECKPOINTS_FIRST_CALL_RVA = 0x229529; // XORPS XMM0,XMM0 at 0x929529 (start of first call block)

    // Default limits
    static constexpr uint32_t DEFAULT_TIME_LIMIT = 0x1A5E0;    // 30 minutes
    static constexpr uint32_t DEFAULT_FAULT_LIMIT = 0x1F4;     // 500 faults

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

    // Limit enforcement state (to prevent spam)
    static bool g_faultOutTriggered = false;
    static bool g_timeOutTriggered = false;

    // Forward declarations
    bool RespawnAtCheckpointOffset(int offset);
    bool PatchUpdateCheckpointsFirstCall();
    bool UnpatchUpdateCheckpointsFirstCall();
    bool PatchUpdateCheckpointsCall();
    bool UnpatchUpdateCheckpointsCall();
    bool PatchFinishLineCheck();
    bool UnpatchFinishLineCheck();
    bool PatchCheckpointEarlyReturn();
    bool UnpatchCheckpointEarlyReturn();

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
    // Limit Modification Functions
    // ============================================================================

    bool SetFaultLimit(uint32_t newLimit) {
        if (!g_initialized) {
            LOG_ERROR("[Limits] Not initialized");
            return false;
        }

        uintptr_t patchAddr = g_baseAddress + FAULT_LIMIT_CMP_RVA;

        DWORD oldProtect;
        if (!VirtualProtect((void*)patchAddr, sizeof(uint32_t), PAGE_EXECUTE_READWRITE, &oldProtect)) {
            LOG_ERROR("[Limits] Failed to change memory protection for fault limit");
            return false;
        }

        uint32_t currentLimit = *reinterpret_cast<uint32_t*>(patchAddr);
        *reinterpret_cast<uint32_t*>(patchAddr) = newLimit;
        VirtualProtect((void*)patchAddr, sizeof(uint32_t), oldProtect, &oldProtect);

        LOG_INFO("[Limits] Changed fault limit from " << std::dec << currentLimit << " to " << newLimit);
        return true;
    }

    uint32_t GetFaultLimit() {
        if (!g_initialized) {
            return DEFAULT_FAULT_LIMIT;
        }

        uintptr_t patchAddr = g_baseAddress + FAULT_LIMIT_CMP_RVA;
        if (IsBadReadPtr((void*)patchAddr, sizeof(uint32_t))) {
            return DEFAULT_FAULT_LIMIT;
        }

        return *reinterpret_cast<uint32_t*>(patchAddr);
    }

    bool SetTimeLimit(uint32_t newLimit) {
        if (!g_initialized) {
            LOG_ERROR("[Limits] Not initialized");
            return false;
        }

        uintptr_t patchAddr = g_baseAddress + TIME_LIMIT_CMP_RVA;

        DWORD oldProtect;
        if (!VirtualProtect((void*)patchAddr, sizeof(uint32_t), PAGE_EXECUTE_READWRITE, &oldProtect)) {
            LOG_ERROR("[Limits] Failed to change memory protection for time limit");
            return false;
        }

        uint32_t currentLimit = *reinterpret_cast<uint32_t*>(patchAddr);
        *reinterpret_cast<uint32_t*>(patchAddr) = newLimit;
        VirtualProtect((void*)patchAddr, sizeof(uint32_t), oldProtect, &oldProtect);

        LOG_INFO("[Limits] Changed time limit from " << std::dec << currentLimit << " to " << newLimit);
        return true;
    }

    uint32_t GetTimeLimit() {
        if (!g_initialized) {
            return DEFAULT_TIME_LIMIT;
        }

        uintptr_t patchAddr = g_baseAddress + TIME_LIMIT_CMP_RVA;
        if (IsBadReadPtr((void*)patchAddr, sizeof(uint32_t))) {
            return DEFAULT_TIME_LIMIT;
        }

        return *reinterpret_cast<uint32_t*>(patchAddr);
    }

    bool DisableFaultLimit() {
        return SetFaultLimit(0x7FFFFFFF);
    }

    bool DisableTimeLimit() {
        return SetTimeLimit(0x7FFFFFFF);
    }

    bool RestoreDefaultLimits() {
        bool success = true;
        success &= SetFaultLimit(DEFAULT_FAULT_LIMIT);
        success &= SetTimeLimit(DEFAULT_TIME_LIMIT);
        return success;
    }

    // Limit Bypass Functions
    bool DisableCompletionConditionCheck() {
        if (!g_initialized) {
            LOG_ERROR("[CompletionFix] Not initialized");
            return false;
        }

        uintptr_t jncAddr = g_baseAddress + COMPLETION_COND_FAULT_JNC_RVA;

        LOG_VERBOSE("[CompletionFix] Patching fault limit check at 0x" << std::hex << jncAddr);

        if (IsBadReadPtr((void*)jncAddr, 6)) {
            LOG_ERROR("[CompletionFix] Cannot read JNC address");
            return false;
        }

        uint8_t* bytes = reinterpret_cast<uint8_t*>(jncAddr);

        if (bytes[0] == 0x90 && bytes[1] == 0x90) {
            LOG_VERBOSE("[CompletionFix] Already patched (found NOPs)");
            return true;
        }

        bool isShortJNC = (bytes[0] == 0x73);
        bool isLongJNC = (bytes[0] == 0x0F && bytes[1] == 0x83);

        if (!isShortJNC && !isLongJNC) {
            LOG_ERROR("[CompletionFix] Expected JNC, found 0x"
                << std::hex << (int)bytes[0] << " " << (int)bytes[1]);
            return false;
        }

        DWORD oldProtect;
        int patchSize = isShortJNC ? 2 : 6;

        if (!VirtualProtect((void*)jncAddr, patchSize, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            LOG_ERROR("[CompletionFix] Failed to change memory protection");
            return false;
        }

        for (int i = 0; i < patchSize; i++) {
            bytes[i] = 0x90;
        }

        VirtualProtect((void*)jncAddr, patchSize, oldProtect, &oldProtect);

        LOG_VERBOSE("[CompletionFix] SUCCESS! Fault limit check disabled");
        return true;
    }

    bool DisableTimeCompletionCheck() {
        if (!g_initialized) {
            LOG_ERROR("[TimeFix] Not initialized");
            return false;
        }

        uintptr_t jncAddr = g_baseAddress + COMPLETION_COND_TIME_JNC_RVA;

        LOG_VERBOSE("[TimeFix] Patching time limit check #1 at 0x" << std::hex << jncAddr);

        if (IsBadReadPtr((void*)jncAddr, 6)) {
            LOG_ERROR("[TimeFix] Cannot read JNC address");
            return false;
        }

        uint8_t* bytes = reinterpret_cast<uint8_t*>(jncAddr);

        if (bytes[0] == 0x90 && bytes[1] == 0x90) {
            LOG_INFO("[TimeFix] Already patched (found NOPs)");
            return true;
        }

        bool isShortJNC = (bytes[0] == 0x73);
        bool isLongJNC = (bytes[0] == 0x0F && bytes[1] == 0x83);

        if (!isShortJNC && !isLongJNC) {
            LOG_ERROR("[TimeFix] Expected JNC, found 0x"
                << std::hex << (int)bytes[0] << " " << (int)bytes[1]);
            return false;
        }

        DWORD oldProtect;
        int patchSize = isShortJNC ? 2 : 6;

        if (!VirtualProtect((void*)jncAddr, patchSize, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            LOG_ERROR("[TimeFix] Failed to change memory protection");
            return false;
        }

        for (int i = 0; i < patchSize; i++) {
            bytes[i] = 0x90;
        }

        VirtualProtect((void*)jncAddr, patchSize, oldProtect, &oldProtect);

        LOG_VERBOSE("[TimeFix] SUCCESS! Time limit check #1 disabled");
        return true;
    }

    bool DisableTimeCompletionCheck2() {
        if (!g_initialized) {
            LOG_ERROR("[TimeFix2] Not initialized");
            return false;
        }

        uintptr_t jcAddr = g_baseAddress + COMPLETION_COND_TIME2_JC_RVA;

        LOG_VERBOSE("[TimeFix2] Patching time limit check #2 at 0x" << std::hex << jcAddr);

        if (IsBadReadPtr((void*)jcAddr, 2)) {
            LOG_ERROR("[TimeFix2] Cannot read JC address");
            return false;
        }

        uint8_t* bytes = reinterpret_cast<uint8_t*>(jcAddr);

        if ((bytes[0] == 0xEB && bytes[1] == 0x13) || (bytes[0] == 0x90 && bytes[1] == 0x90)) {
            LOG_VERBOSE("[TimeFix2] Already patched (found " << (bytes[0] == 0xEB ? "JMP" : "NOPs") << ")");
            return true;
        }

        if (bytes[0] != 0x72) {
            LOG_ERROR("[TimeFix2] Expected JC (0x72), found 0x" << std::hex << (int)bytes[0]);
            return false;
        }

        DWORD oldProtect;
        if (!VirtualProtect((void*)jcAddr, 2, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            LOG_ERROR("[TimeFix2] Failed to change memory protection");
            return false;
        }

        bytes[0] = 0xEB;
        bytes[1] = 0x13;

        VirtualProtect((void*)jcAddr, 2, oldProtect, &oldProtect);

        LOG_VERBOSE("[TimeFix2] SUCCESS! Changed JC to JMP - timer won't freeze!");
        return true;
    }

    bool ForceRaceEndSuccess() {
        if (!g_initialized) {
            LOG_ERROR("[FinalFix] Not initialized");
            return false;
        }

        uintptr_t pushAddr = g_baseAddress + UPDATE_RACE_END_PUSH_RVA;

        LOG_VERBOSE("[FinalFix] Patching UpdateRaceEndState at 0x" << std::hex << pushAddr);

        if (IsBadReadPtr((void*)pushAddr, 9)) {
            LOG_ERROR("[FinalFix] Cannot read patch address");
            return false;
        }

        uint8_t* bytes = reinterpret_cast<uint8_t*>(pushAddr);

        if (bytes[0] == 0x85 && bytes[1] == 0xC0) {
            LOG_VERBOSE("[FinalFix] Already patched (found TEST EAX,EAX)");
            return true;
        }

        if (bytes[0] != 0x50) {
            LOG_ERROR("[FinalFix] Expected PUSH EAX (0x50), found 0x" << std::hex << (int)bytes[0]);
            return false;
        }

        DWORD oldProtect;
        if (!VirtualProtect((void*)pushAddr, 9, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            LOG_ERROR("[FinalFix] Failed to change memory protection");
            return false;
        }

        bytes[0] = 0x85;  // TEST
        bytes[1] = 0xC0;  // EAX, EAX
        bytes[2] = 0x75;  // JNZ
        bytes[3] = 0x25;  // +0x25
        bytes[4] = 0x6A;  // PUSH
        bytes[5] = 0x00;  // 0
        bytes[6] = 0x6A;  // PUSH
        bytes[7] = 0x01;  // 0x1
        bytes[8] = 0x8B;  // MOV

        VirtualProtect((void*)pushAddr, 9, oldProtect, &oldProtect);

        LOG_VERBOSE("[LimitBypass] SUCCESS! Will skip finish message if limits exceeded");
        return true;
    }

    bool DisableRaceUpdateTimerFreeze() {
        if (!g_initialized) {
            LOG_ERROR("[TimerFreezeFix] Not initialized");
            return false;
        }

        uintptr_t jncAddr = g_baseAddress + RACEUPDATE_TIMER_CHECK_RVA + 6;

        LOG_VERBOSE("[TimerFreezeFix] Patching timer freeze JNC in RaceUpdate at 0x" << std::hex << jncAddr);

        if (IsBadReadPtr((void*)jncAddr, 6)) {
            LOG_ERROR("[TimerFreezeFix] Cannot read JNC address");
            return false;
        }

        uint8_t* bytes = reinterpret_cast<uint8_t*>(jncAddr);

        if (bytes[0] == 0x90 && bytes[1] == 0x90) {
            LOG_VERBOSE("[TimerFreezeFix] Already patched (found NOPs)");
            return true;
        }

        DWORD oldProtect;
        int patchSize;

        if (bytes[0] == 0x73) {
            patchSize = 2;
            LOG_VERBOSE("[TimerFreezeFix] Found short JNC (73 " << std::hex << (int)bytes[1] << ")");
        }
        else if (bytes[0] == 0x0F && bytes[1] == 0x83) {
            patchSize = 6;
            LOG_VERBOSE("[TimerFreezeFix] Found long JNC (0F 83)");
        }
        else {
            LOG_ERROR("[TimerFreezeFix] Expected JNC (73 or 0F 83), found: "
                << std::hex << (int)bytes[0] << " " << (int)bytes[1]);
            return false;
        }

        if (!VirtualProtect((void*)jncAddr, patchSize, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            LOG_ERROR("[TimerFreezeFix] Failed to change memory protection");
            return false;
        }

        for (int i = 0; i < patchSize; i++) {
            bytes[i] = 0x90;
        }

        VirtualProtect((void*)jncAddr, patchSize, oldProtect, &oldProtect);

        LOG_VERBOSE("[TimerFreezeFix] SUCCESS! NOPed " << std::dec << patchSize << " bytes - Timer will continue past 30 minutes!");
        return true;
    }

    bool DisableMultiplayerTimeChecks() {
        if (!g_initialized) {
            LOG_ERROR("[MPTimeFix] Not initialized");
            return false;
        }

        bool success = true;

        // Patch multiplayer mode 1 time check
        uintptr_t mp1JncAddr = g_baseAddress + MULTIPLAYER_MODE1_TIME_JNC_RVA;
        LOG_VERBOSE("[MPTimeFix] Patching multiplayer mode 1 time check at 0x" << std::hex << mp1JncAddr);

        if (IsBadReadPtr((void*)mp1JncAddr, 6)) {
            LOG_ERROR("[MPTimeFix] Cannot read MP1 JNC address");
            success = false;
        }
        else {
            uint8_t* bytes = reinterpret_cast<uint8_t*>(mp1JncAddr);

            if (bytes[0] == 0x90 && bytes[1] == 0x90) {
                LOG_VERBOSE("[MPTimeFix] MP1 already patched");
            }
            else {
                bool isShortJNC = (bytes[0] == 0x73);
                bool isLongJNC = (bytes[0] == 0x0F && bytes[1] == 0x83);

                if (isShortJNC || isLongJNC) {
                    DWORD oldProtect;
                    int patchSize = isShortJNC ? 2 : 6;

                    if (VirtualProtect((void*)mp1JncAddr, patchSize, PAGE_EXECUTE_READWRITE, &oldProtect)) {
                        for (int i = 0; i < patchSize; i++) {
                            bytes[i] = 0x90;
                        }
                        VirtualProtect((void*)mp1JncAddr, patchSize, oldProtect, &oldProtect);
                        LOG_VERBOSE("[MPTimeFix] MP1 time check disabled");
                    }
                    else {
                        LOG_ERROR("[MPTimeFix] Failed to patch MP1");
                        success = false;
                    }
                }
                else {
                    LOG_ERROR("[MPTimeFix] MP1: Expected JNC, found 0x" << std::hex << (int)bytes[0]);
                    success = false;
                }
            }
        }

        // Patch multiplayer mode 2 time check
        uintptr_t mp2JncAddr = g_baseAddress + MULTIPLAYER_MODE2_TIME_JNC_RVA;
        LOG_VERBOSE("[MPTimeFix] Patching multiplayer mode 2 time check at 0x" << std::hex << mp2JncAddr);

        if (IsBadReadPtr((void*)mp2JncAddr, 6)) {
            LOG_ERROR("[MPTimeFix] Cannot read MP2 JNC address");
            success = false;
        }
        else {
            uint8_t* bytes = reinterpret_cast<uint8_t*>(mp2JncAddr);

            if (bytes[0] == 0x90 && bytes[1] == 0x90) {
                LOG_VERBOSE("[MPTimeFix] MP2 already patched");
            }
            else {
                bool isShortJNC = (bytes[0] == 0x73);
                bool isLongJNC = (bytes[0] == 0x0F && bytes[1] == 0x83);

                if (isShortJNC || isLongJNC) {
                    DWORD oldProtect;
                    int patchSize = isShortJNC ? 2 : 6;

                    if (VirtualProtect((void*)mp2JncAddr, patchSize, PAGE_EXECUTE_READWRITE, &oldProtect)) {
                        for (int i = 0; i < patchSize; i++) {
                            bytes[i] = 0x90;
                        }
                        VirtualProtect((void*)mp2JncAddr, patchSize, oldProtect, &oldProtect);
                        LOG_VERBOSE("[MPTimeFix] MP2 time check disabled");
                    }
                    else {
                        LOG_ERROR("[MPTimeFix] Failed to patch MP2");
                        success = false;
                    }
                }
                else {
                    LOG_ERROR("[MPTimeFix] MP2: Expected JNC, found 0x" << std::hex << (int)bytes[0]);
                    success = false;
                }
            }
        }

        if (success) {
            LOG_VERBOSE("[MPTimeFix] SUCCESS! Multiplayer time checks disabled");
        }

        return success;
    }

    bool DisableMultiplayerFaultChecks() {
        if (!g_initialized) {
            LOG_ERROR("[MPFaultFix] Not initialized");
            return false;
        }

        bool success = true;

        // Patch multiplayer mode 1 fault check (NOP the JNZ that respawns on fault >= 500)
        uintptr_t mp1JnzAddr = g_baseAddress + MULTIPLAYER_MODE1_FAULT_JNZ_RVA;
        LOG_VERBOSE("[MPFaultFix] Patching multiplayer mode 1 fault check at 0x" << std::hex << mp1JnzAddr);

        if (IsBadReadPtr((void*)mp1JnzAddr, 6)) {
            LOG_ERROR("[MPFaultFix] Cannot read MP1 JNZ address");
            success = false;
        }
        else {
            uint8_t* bytes = reinterpret_cast<uint8_t*>(mp1JnzAddr);

            if (bytes[0] == 0x90 && bytes[1] == 0x90) {
                LOG_VERBOSE("[MPFaultFix] MP1 already patched");
            }
            else {
                // Check for short JNZ (0x75) or long JNZ (0x0F 0x85)
                bool isShortJNZ = (bytes[0] == 0x75);
                bool isLongJNZ = (bytes[0] == 0x0F && bytes[1] == 0x85);

                if (isShortJNZ || isLongJNZ) {
                    DWORD oldProtect;
                    int patchSize = isShortJNZ ? 2 : 6;

                    if (VirtualProtect((void*)mp1JnzAddr, patchSize, PAGE_EXECUTE_READWRITE, &oldProtect)) {
                        for (int i = 0; i < patchSize; i++) {
                            bytes[i] = 0x90;
                        }
                        VirtualProtect((void*)mp1JnzAddr, patchSize, oldProtect, &oldProtect);
                        LOG_VERBOSE("[MPFaultFix] MP1 fault check disabled");
                    }
                    else {
                        LOG_ERROR("[MPFaultFix] Failed to patch MP1");
                        success = false;
                    }
                }
                else {
                    LOG_ERROR("[MPFaultFix] MP1: Expected JNZ, found 0x" << std::hex << (int)bytes[0]);
                    success = false;
                }
            }
        }

        // Patch multiplayer mode 2 fault check
        uintptr_t mp2JnzAddr = g_baseAddress + MULTIPLAYER_MODE2_FAULT_JNZ_RVA;
        LOG_VERBOSE("[MPFaultFix] Patching multiplayer mode 2 fault check at 0x" << std::hex << mp2JnzAddr);

        if (IsBadReadPtr((void*)mp2JnzAddr, 6)) {
            LOG_ERROR("[MPFaultFix] Cannot read MP2 JNZ address");
            success = false;
        }
        else {
            uint8_t* bytes = reinterpret_cast<uint8_t*>(mp2JnzAddr);

            if (bytes[0] == 0x90 && bytes[1] == 0x90) {
                LOG_VERBOSE("[MPFaultFix] MP2 already patched");
            }
            else {
                bool isShortJNZ = (bytes[0] == 0x75);
                bool isLongJNZ = (bytes[0] == 0x0F && bytes[1] == 0x85);

                if (isShortJNZ || isLongJNZ) {
                    DWORD oldProtect;
                    int patchSize = isShortJNZ ? 2 : 6;

                    if (VirtualProtect((void*)mp2JnzAddr, patchSize, PAGE_EXECUTE_READWRITE, &oldProtect)) {
                        for (int i = 0; i < patchSize; i++) {
                            bytes[i] = 0x90;
                        }
                        VirtualProtect((void*)mp2JnzAddr, patchSize, oldProtect, &oldProtect);
                        LOG_VERBOSE("[MPFaultFix] MP2 fault check disabled");
                    }
                    else {
                        LOG_ERROR("[MPFaultFix] Failed to patch MP2");
                        success = false;
                    }
                }
                else {
                    LOG_ERROR("[MPFaultFix] MP2: Expected JNZ, found 0x" << std::hex << (int)bytes[0]);
                    success = false;
                }
            }
        }

        if (success) {
            LOG_VERBOSE("[MPFaultFix] SUCCESS! Multiplayer fault checks disabled");
        }

        return success;
    }

    bool DisableAllLimitValidation() {
        LOG_INFO("[LimitBypass] Disable Fault (500) + Time (30Min) Limits ");

        bool faultConditionSuccess = DisableCompletionConditionCheck();
        bool timeConditionSuccess = DisableTimeCompletionCheck();
        bool timeCondition2Success = DisableTimeCompletionCheck2();
        bool messageSuccess = ForceRaceEndSuccess();
        bool timerFreezeSuccess = DisableRaceUpdateTimerFreeze();
        bool multiplayerTimeSuccess = DisableMultiplayerTimeChecks();
        bool multiplayerFaultSuccess = DisableMultiplayerFaultChecks();

        if (faultConditionSuccess && timeConditionSuccess && timeCondition2Success &&
            messageSuccess && timerFreezeSuccess && multiplayerTimeSuccess &&
            multiplayerFaultSuccess) {
            LOG_VERBOSE("[LimitBypass] SUCCESS! All patches applied:");
            LOG_VERBOSE("[LimitBypass]   1. Fault limit check disabled (SP)");
            LOG_VERBOSE("[LimitBypass]   2. Time limit check #1 disabled (SP)");
            LOG_VERBOSE("[LimitBypass]   3. Time limit check #2 disabled (SP)");
            LOG_VERBOSE("[LimitBypass]   4. Finish message patched");
            LOG_VERBOSE("[LimitBypass]   5. Timer freeze DISABLED");
            LOG_VERBOSE("[LimitBypass]   6. Multiplayer time checks DISABLED");
            LOG_VERBOSE("[LimitBypass]   7. Multiplayer fault checks DISABLED");
            LOG_VERBOSE("[LimitBypass] You can now play indefinitely in SP/MP!");
            LOG_VERBOSE("[LimitBypass] Note: MP may crash on track 2 finish with exceeded limits");
            return true;
        }
        else {
            LOG_ERROR("[LimitBypass] Patch FAILED");
            LOG_ERROR("[LimitBypass]   Fault (SP): " << (faultConditionSuccess ? "OK" : "FAIL"));
            LOG_ERROR("[LimitBypass]   Time1 (SP): " << (timeConditionSuccess ? "OK" : "FAIL"));
            LOG_ERROR("[LimitBypass]   Time2 (SP): " << (timeCondition2Success ? "OK" : "FAIL"));
            LOG_ERROR("[LimitBypass]   Message: " << (messageSuccess ? "OK" : "FAIL"));
            LOG_ERROR("[LimitBypass]   TimerFreeze: " << (timerFreezeSuccess ? "OK" : "FAIL"));
            LOG_ERROR("[LimitBypass]   MP Time: " << (multiplayerTimeSuccess ? "OK" : "FAIL"));
            LOG_ERROR("[LimitBypass]   MP Fault: " << (multiplayerFaultSuccess ? "OK" : "FAIL"));
            return false;
        }
    }

    // ============================================================================
    // Enable Validation Functions (restore original bytes)
    // ============================================================================

    bool EnableCompletionConditionCheck() {
        if (!g_initialized) {
            LOG_ERROR("[CompletionRestore] Not initialized");
            return false;
        }

        uintptr_t jncAddr = g_baseAddress + COMPLETION_COND_FAULT_JNC_RVA;

        LOG_VERBOSE("[CompletionRestore] Restoring fault limit check at 0x" << std::hex << jncAddr);

        if (IsBadReadPtr((void*)jncAddr, 6)) {
            LOG_ERROR("[CompletionRestore] Cannot read JNC address");
            return false;
        }

        uint8_t* bytes = reinterpret_cast<uint8_t*>(jncAddr);

        // Check if already restored (has JNC instruction)
        if ((bytes[0] == 0x73) || (bytes[0] == 0x0F && bytes[1] == 0x83)) {
            LOG_VERBOSE("[CompletionRestore] Already restored (found JNC)");
            return true;
        }

        // Check if it's NOPed (needs restoration)
        if (bytes[0] != 0x90) {
            LOG_ERROR("[CompletionRestore] Unexpected bytes at address: 0x" << std::hex << (int)bytes[0]);
            return false;
        }

        DWORD oldProtect;
        // Restore to short JNC (0x73 0x09) - jumps 9 bytes if NC
        if (!VirtualProtect((void*)jncAddr, 2, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            LOG_ERROR("[CompletionRestore] Failed to change memory protection");
            return false;
        }

        bytes[0] = 0x73;  // JNC (short jump)
        bytes[1] = 0x1F;  // Jump offset (+31 bytes, to 0x931588)

        VirtualProtect((void*)jncAddr, 2, oldProtect, &oldProtect);

        LOG_VERBOSE("[CompletionRestore] SUCCESS! Fault limit check restored");
        return true;
    }

    bool EnableTimeCompletionCheck() {
        if (!g_initialized) {
            LOG_ERROR("[TimeRestore] Not initialized");
            return false;
        }

        uintptr_t jncAddr = g_baseAddress + COMPLETION_COND_TIME_JNC_RVA;

        LOG_VERBOSE("[TimeRestore] Restoring time limit check #1 at 0x" << std::hex << jncAddr);

        if (IsBadReadPtr((void*)jncAddr, 6)) {
            LOG_ERROR("[TimeRestore] Cannot read JNC address");
            return false;
        }

        uint8_t* bytes = reinterpret_cast<uint8_t*>(jncAddr);

        // Check if already restored
        if ((bytes[0] == 0x73) || (bytes[0] == 0x0F && bytes[1] == 0x83)) {
            LOG_VERBOSE("[TimeRestore] Already restored (found JNC)");
            return true;
        }

        if (bytes[0] != 0x90) {
            LOG_ERROR("[TimeRestore] Unexpected bytes at address: 0x" << std::hex << (int)bytes[0]);
            return false;
        }

        DWORD oldProtect;
        // Restore to short JNC (0x73 0x30) - jumps 48 bytes if NC
        if (!VirtualProtect((void*)jncAddr, 2, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            LOG_ERROR("[TimeRestore] Failed to change memory protection");
            return false;
        }

        bytes[0] = 0x73;  // JNC (short jump)
        bytes[1] = 0x30;  // Jump offset (+48 bytes)

        VirtualProtect((void*)jncAddr, 2, oldProtect, &oldProtect);

        LOG_VERBOSE("[TimeRestore] SUCCESS! Time limit check #1 restored");
        return true;
    }

    bool EnableTimeCompletionCheck2() {
        if (!g_initialized) {
            LOG_ERROR("[TimeRestore2] Not initialized");
            return false;
        }

        uintptr_t jcAddr = g_baseAddress + COMPLETION_COND_TIME2_JC_RVA;

        LOG_VERBOSE("[TimeRestore2] Restoring time limit check #2 at 0x" << std::hex << jcAddr);

        if (IsBadReadPtr((void*)jcAddr, 2)) {
            LOG_ERROR("[TimeRestore2] Cannot read JC address");
            return false;
        }

        uint8_t* bytes = reinterpret_cast<uint8_t*>(jcAddr);

        // Check if already restored (has JC instruction)
        if (bytes[0] == 0x72) {
            LOG_VERBOSE("[TimeRestore2] Already restored (found JC)");
            return true;
        }

        // Check if it's JMP (needs restoration to JC)
        if (bytes[0] != 0xEB && bytes[0] != 0x90) {
            LOG_ERROR("[TimeRestore2] Unexpected bytes: 0x" << std::hex << (int)bytes[0]);
            return false;
        }

        DWORD oldProtect;
        if (!VirtualProtect((void*)jcAddr, 2, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            LOG_ERROR("[TimeRestore2] Failed to change memory protection");
            return false;
        }

        bytes[0] = 0x72;  // JC (jump if carry)
        bytes[1] = 0x13;  // Jump offset (+19 bytes)

        VirtualProtect((void*)jcAddr, 2, oldProtect, &oldProtect);

        LOG_VERBOSE("[TimeRestore2] SUCCESS! Changed JMP back to JC - timer will freeze correctly!");
        return true;
    }

    bool RestoreRaceEndSuccess() {
        if (!g_initialized) {
            LOG_ERROR("[FinalRestore] Not initialized");
            return false;
        }

        uintptr_t pushAddr = g_baseAddress + UPDATE_RACE_END_PUSH_RVA;

        LOG_VERBOSE("[FinalRestore] Restoring UpdateRaceEndState at 0x" << std::hex << pushAddr);

        if (IsBadReadPtr((void*)pushAddr, 9)) {
            LOG_ERROR("[FinalRestore] Cannot read patch address");
            return false;
        }

        uint8_t* bytes = reinterpret_cast<uint8_t*>(pushAddr);

        // Check if already restored (has PUSH EAX)
        if (bytes[0] == 0x50) {
            LOG_VERBOSE("[FinalRestore] Already restored (found PUSH EAX)");
            return true;
        }

        // Check if it's patched (has TEST EAX,EAX)
        if (bytes[0] != 0x85 || bytes[1] != 0xC0) {
            LOG_ERROR("[FinalRestore] Unexpected bytes: 0x" << std::hex << (int)bytes[0] << " " << (int)bytes[1]);
            return false;
        }

        DWORD oldProtect;
        if (!VirtualProtect((void*)pushAddr, 9, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            LOG_ERROR("[FinalRestore] Failed to change memory protection");
            return false;
        }

        // Restore original bytes:
        // 00980d93: PUSH EAX           (50)
        // 00980d94: PUSH 0x1           (6A 01)
        // 00980d96: MOV ECX,EDI        (8B CF)
        // 00980d98: CALL <target>      (E8 xx xx xx xx)
        //
        // The CALL target is at RVA 0x55a00 (InitializeObjectStruct)
        // We need to calculate the relative offset at runtime
        
        uintptr_t callInstrAddr = pushAddr + 5;  // Address of the CALL instruction
        uintptr_t callTarget = g_baseAddress + 0x55a00;  // InitializeObjectStruct
        uintptr_t nextInstrAddr = callInstrAddr + 5;  // Address after CALL
        int32_t relativeOffset = (int32_t)(callTarget - nextInstrAddr);
        
        bytes[0] = 0x50;  // PUSH EAX
        bytes[1] = 0x6A;  // PUSH
        bytes[2] = 0x01;  // 0x1
        bytes[3] = 0x8B;  // MOV
        bytes[4] = 0xCF;  // ECX, EDI
        bytes[5] = 0xE8;  // CALL (near)
        // Write the relative offset as little-endian
        *reinterpret_cast<int32_t*>(&bytes[6]) = relativeOffset;

        VirtualProtect((void*)pushAddr, 10, oldProtect, &oldProtect);

        LOG_VERBOSE("[FinalRestore] SUCCESS! Finish message restored (CALL offset: 0x" << std::hex << relativeOffset << ")");
        return true;
    }

    bool EnableRaceUpdateTimerFreeze() {
        if (!g_initialized) {
            LOG_ERROR("[TimerFreezeRestore] Not initialized");
            return false;
        }

        uintptr_t jncAddr = g_baseAddress + RACEUPDATE_TIMER_CHECK_RVA + 6;

        LOG_VERBOSE("[TimerFreezeRestore] Restoring timer freeze JNC in RaceUpdate at 0x" << std::hex << jncAddr);

        if (IsBadReadPtr((void*)jncAddr, 6)) {
            LOG_ERROR("[TimerFreezeRestore] Cannot read JNC address");
            return false;
        }

        uint8_t* bytes = reinterpret_cast<uint8_t*>(jncAddr);

        // Check if already restored
        if ((bytes[0] == 0x73) || (bytes[0] == 0x0F && bytes[1] == 0x83)) {
            LOG_VERBOSE("[TimerFreezeRestore] Already restored (found JNC)");
            return true;
        }

        if (bytes[0] != 0x90) {
            LOG_ERROR("[TimerFreezeRestore] Unexpected bytes: 0x" << std::hex << (int)bytes[0]);
            return false;
        }

        DWORD oldProtect;
        // Restore to short JNC (0x73 0x12) - jumps 18 bytes if NC
        if (!VirtualProtect((void*)jncAddr, 2, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            LOG_ERROR("[TimerFreezeRestore] Failed to change memory protection");
            return false;
        }

        bytes[0] = 0x73;  // JNC (short jump)
        bytes[1] = 0x12;  // Jump offset (+18 bytes)

        VirtualProtect((void*)jncAddr, 2, oldProtect, &oldProtect);

        LOG_VERBOSE("[TimerFreezeRestore] SUCCESS! Timer will freeze at 30 minutes again!");
        return true;
    }

    bool EnableMultiplayerTimeChecks() {
        if (!g_initialized) {
            LOG_ERROR("[MPTimeRestore] Not initialized");
            return false;
        }

        bool success = true;

        // Restore multiplayer mode 1 time check
        uintptr_t mp1JncAddr = g_baseAddress + MULTIPLAYER_MODE1_TIME_JNC_RVA;
        LOG_VERBOSE("[MPTimeRestore] Restoring multiplayer mode 1 time check at 0x" << std::hex << mp1JncAddr);

        if (IsBadReadPtr((void*)mp1JncAddr, 6)) {
            LOG_ERROR("[MPTimeRestore] Cannot read MP1 JNC address");
            success = false;
        }
        else {
            uint8_t* bytes = reinterpret_cast<uint8_t*>(mp1JncAddr);

            if ((bytes[0] == 0x73) || (bytes[0] == 0x0F && bytes[1] == 0x83)) {
                LOG_VERBOSE("[MPTimeRestore] MP1 already restored");
            }
            else if (bytes[0] == 0x90) {
                DWORD oldProtect;
                if (VirtualProtect((void*)mp1JncAddr, 2, PAGE_EXECUTE_READWRITE, &oldProtect)) {
                    bytes[0] = 0x73;  // JNC (short)
                    bytes[1] = 0x04;  // Jump offset
                    VirtualProtect((void*)mp1JncAddr, 2, oldProtect, &oldProtect);
                    LOG_VERBOSE("[MPTimeRestore] MP1 time check restored");
                }
                else {
                    LOG_ERROR("[MPTimeRestore] Failed to patch MP1");
                    success = false;
                }
            }
        }

        // Restore multiplayer mode 2 time check
        uintptr_t mp2JncAddr = g_baseAddress + MULTIPLAYER_MODE2_TIME_JNC_RVA;
        LOG_VERBOSE("[MPTimeRestore] Restoring multiplayer mode 2 time check at 0x" << std::hex << mp2JncAddr);

        if (IsBadReadPtr((void*)mp2JncAddr, 6)) {
            LOG_ERROR("[MPTimeRestore] Cannot read MP2 JNC address");
            success = false;
        }
        else {
            uint8_t* bytes = reinterpret_cast<uint8_t*>(mp2JncAddr);

            if ((bytes[0] == 0x73) || (bytes[0] == 0x0F && bytes[1] == 0x83)) {
                LOG_VERBOSE("[MPTimeRestore] MP2 already restored");
            }
            else if (bytes[0] == 0x90) {
                DWORD oldProtect;
                if (VirtualProtect((void*)mp2JncAddr, 2, PAGE_EXECUTE_READWRITE, &oldProtect)) {
                    bytes[0] = 0x73;  // JNC (short)
                    bytes[1] = 0x04;  // Jump offset
                    VirtualProtect((void*)mp2JncAddr, 2, oldProtect, &oldProtect);
                    LOG_VERBOSE("[MPTimeRestore] MP2 time check restored");
                }
                else {
                    LOG_ERROR("[MPTimeRestore] Failed to patch MP2");
                    success = false;
                }
            }
        }

        if (success) {
            LOG_VERBOSE("[MPTimeRestore] SUCCESS! Multiplayer time checks restored");
        }

        return success;
    }

    bool EnableMultiplayerFaultChecks() {
        if (!g_initialized) {
            LOG_ERROR("[MPFaultRestore] Not initialized");
            return false;
        }

        bool success = true;

        // Restore multiplayer mode 1 fault check
        uintptr_t mp1JnzAddr = g_baseAddress + MULTIPLAYER_MODE1_FAULT_JNZ_RVA;
        LOG_VERBOSE("[MPFaultRestore] Restoring multiplayer mode 1 fault check at 0x" << std::hex << mp1JnzAddr);

        if (IsBadReadPtr((void*)mp1JnzAddr, 6)) {
            LOG_ERROR("[MPFaultRestore] Cannot read MP1 JNZ address");
            success = false;
        }
        else {
            uint8_t* bytes = reinterpret_cast<uint8_t*>(mp1JnzAddr);

            if ((bytes[0] == 0x75) || (bytes[0] == 0x0F && bytes[1] == 0x85)) {
                LOG_VERBOSE("[MPFaultRestore] MP1 already restored");
            }
            else if (bytes[0] == 0x90) {
                DWORD oldProtect;
                if (VirtualProtect((void*)mp1JnzAddr, 2, PAGE_EXECUTE_READWRITE, &oldProtect)) {
                    bytes[0] = 0x75;  // JNZ (short)
                    bytes[1] = 0xC9;  // Jump offset
                    VirtualProtect((void*)mp1JnzAddr, 2, oldProtect, &oldProtect);
                    LOG_VERBOSE("[MPFaultRestore] MP1 fault check restored");
                }
                else {
                    LOG_ERROR("[MPFaultRestore] Failed to patch MP1");
                    success = false;
                }
            }
        }

        // Restore multiplayer mode 2 fault check
        uintptr_t mp2JnzAddr = g_baseAddress + MULTIPLAYER_MODE2_FAULT_JNZ_RVA;
        LOG_VERBOSE("[MPFaultRestore] Restoring multiplayer mode 2 fault check at 0x" << std::hex << mp2JnzAddr);

        if (IsBadReadPtr((void*)mp2JnzAddr, 6)) {
            LOG_ERROR("[MPFaultRestore] Cannot read MP2 JNZ address");
            success = false;
        }
        else {
            uint8_t* bytes = reinterpret_cast<uint8_t*>(mp2JnzAddr);

            if ((bytes[0] == 0x75) || (bytes[0] == 0x0F && bytes[1] == 0x85)) {
                LOG_VERBOSE("[MPFaultRestore] MP2 already restored");
            }
            else if (bytes[0] == 0x90) {
                DWORD oldProtect;
                if (VirtualProtect((void*)mp2JnzAddr, 2, PAGE_EXECUTE_READWRITE, &oldProtect)) {
                    bytes[0] = 0x75;  // JNZ (short)
                    bytes[1] = 0xC9;  // Jump offset
                    VirtualProtect((void*)mp2JnzAddr, 2, oldProtect, &oldProtect);
                    LOG_VERBOSE("[MPFaultRestore] MP2 fault check restored");
                }
                else {
                    LOG_ERROR("[MPFaultRestore] Failed to patch MP2");
                    success = false;
                }
            }
        }

        if (success) {
            LOG_VERBOSE("[MPFaultRestore] SUCCESS! Multiplayer fault checks restored");
        }

        return success;
    }

    bool EnableFaultValidation() {
        return EnableCompletionConditionCheck();
    }

    bool EnableTimeValidation() {
        bool success = true;
        success &= EnableTimeCompletionCheck();
        success &= EnableTimeCompletionCheck2();
        return success;
    }

    bool EnableAllLimitValidation() {
        LOG_INFO("[LimitRestore] Enable Fault (500) + Time (30Min) Limits ");

        bool faultConditionSuccess = EnableCompletionConditionCheck();
        bool timeConditionSuccess = EnableTimeCompletionCheck();
        bool timeCondition2Success = EnableTimeCompletionCheck2();
        bool messageSuccess = RestoreRaceEndSuccess();
        bool timerFreezeSuccess = EnableRaceUpdateTimerFreeze();
        bool multiplayerTimeSuccess = EnableMultiplayerTimeChecks();
        bool multiplayerFaultSuccess = EnableMultiplayerFaultChecks();

        if (faultConditionSuccess && timeConditionSuccess && timeCondition2Success &&
            messageSuccess && timerFreezeSuccess && multiplayerTimeSuccess &&
            multiplayerFaultSuccess) {
            LOG_VERBOSE("[LimitRestore] SUCCESS! All validations restored:");
            LOG_VERBOSE("[LimitRestore]   1. Fault limit check enabled (SP)");
            LOG_VERBOSE("[LimitRestore]   2. Time limit check #1 enabled (SP)");
            LOG_VERBOSE("[LimitRestore]   3. Time limit check #2 enabled (SP)");
            LOG_VERBOSE("[LimitRestore]   4. Finish message restored");
            LOG_VERBOSE("[LimitRestore]   5. Timer freeze ENABLED");
            LOG_VERBOSE("[LimitRestore]   6. Multiplayer time checks ENABLED");
            LOG_VERBOSE("[LimitRestore]   7. Multiplayer fault checks ENABLED");
            LOG_VERBOSE("[LimitRestore] Limits are now being enforced normally!");
            return true;
        }
        else {
            LOG_ERROR("[LimitRestore] Restore FAILED");
            LOG_ERROR("[LimitRestore]   Fault (SP): " << (faultConditionSuccess ? "OK" : "FAIL"));
            LOG_ERROR("[LimitRestore]   Time1 (SP): " << (timeConditionSuccess ? "OK" : "FAIL"));
            LOG_ERROR("[LimitRestore]   Time2 (SP): " << (timeCondition2Success ? "OK" : "FAIL"));
            LOG_ERROR("[LimitRestore]   Message: " << (messageSuccess ? "OK" : "FAIL"));
            LOG_ERROR("[LimitRestore]   TimerFreeze: " << (timerFreezeSuccess ? "OK" : "FAIL"));
            LOG_ERROR("[LimitRestore]   MP Time: " << (multiplayerTimeSuccess ? "OK" : "FAIL"));
            LOG_ERROR("[LimitRestore]   MP Fault: " << (multiplayerFaultSuccess ? "OK" : "FAIL"));
            return false;
        }
    }

    bool DisableFaultValidation() {
        return DisableCompletionConditionCheck();
    }

    bool DisableTimeValidation() {
        bool success = true;
        success &= DisableTimeCompletionCheck();
        success &= DisableTimeCompletionCheck2();
        return success;
    }

    // ============================================================================
    // Limit Validation State Query Functions
    // ============================================================================

    bool IsFaultValidationDisabled() {
        if (!g_initialized) {
            return false;
        }

        // Check if the fault limit check is patched (NOPed out)
        uintptr_t jncAddr = g_baseAddress + COMPLETION_COND_FAULT_JNC_RVA;

        if (IsBadReadPtr((void*)jncAddr, 2)) {
            return false;
        }

        uint8_t* bytes = reinterpret_cast<uint8_t*>(jncAddr);

        // If first two bytes are NOPs, the fault validation is disabled
        return (bytes[0] == 0x90 && bytes[1] == 0x90);
    }

    bool IsTimeValidationDisabled() {
        if (!g_initialized) {
            return false;
        }

        // Check if the time limit check is patched (NOPed out)
        uintptr_t jncAddr = g_baseAddress + COMPLETION_COND_TIME_JNC_RVA;

        if (IsBadReadPtr((void*)jncAddr, 2)) {
            return false;
        }

        uint8_t* bytes = reinterpret_cast<uint8_t*>(jncAddr);

        // If first two bytes are NOPs, the time validation is disabled
        return (bytes[0] == 0x90 && bytes[1] == 0x90);
    }

    // ============================================================================
    // Instant Finish Functions
    // ============================================================================

    typedef void(__thiscall* SendMessageFunc)(void* messageQueue, int messagePtr, int param2, int param3);
    typedef void*(__thiscall* InitializeObjectStructFunc)(void* obj, int p1, int p2, int p3, int p4);  // Returns obj in EAX
    typedef void*(__cdecl* AllocateMemoryFunc)(int size);  // Returns pointer in EAX
    typedef int(__thiscall* CalculatePlayerValueFunc)(void* manager);  // Takes manager as 'this'

    static constexpr uintptr_t SEND_MESSAGE_RVA = 0x67a040;           // SendMessage @ 0xd7a040
    static constexpr uintptr_t INIT_OBJECT_STRUCT_RVA = 0x55a00;      // InitializeObjectStruct @ 0x755a00
    static constexpr uintptr_t ALLOCATE_MEMORY_RVA = 0xc340;          // AllocateMemory @ 0x70c340
    static constexpr uintptr_t CALCULATE_PLAYER_VALUE_RVA = 0x256f50;  // CalculatePlayerValue @ 0x956f50

    // SEH-safe helper functions
    static void* SafeAllocateMemory(AllocateMemoryFunc func, int size, bool* success) {
        __try {
            void* result = func(size);
            *success = true;
            return result;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            *success = false;
            return nullptr;
        }
    }

    static int SafeCalculatePlayerValue(CalculatePlayerValueFunc func, void* manager, bool* success) {
        __try {
            int result = func(manager);
            *success = true;
            return result;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            *success = false;
            return 0;
        }
    }

    static void* SafeInitObjectStruct(InitializeObjectStructFunc func, void* obj, int p1, int p2, int p3, int p4, bool* success) {
        __try {
            void* result = func(obj, p1, p2, p3, p4);
            *success = true;
            return result;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            *success = false;
            return nullptr;
        }
    }

    static void SafeSendMessage(SendMessageFunc func, void* messageQueue, int messagePtr, bool* success) {
        __try {
            func(messageQueue, messagePtr, 0, 0);
            *success = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            *success = false;
        }
    }

    bool TriggerRaceFinish(int finishType) {
        if (!g_initialized) {
            LOG_ERROR("[InstantFinish] Not initialized");
            return false;
        }

        void* manager = GetGameManager();
        if (!manager) {
            LOG_ERROR("[InstantFinish] Could not get game manager");
            return false;
        }

        // Get message queue from global struct at +0x100
        if (IsBadReadPtr(g_globalStructPtr, sizeof(void*))) {
            LOG_ERROR("[InstantFinish] Cannot read global struct pointer");
            return false;
        }

        void* globalStruct = *g_globalStructPtr;
        if (!globalStruct) {
            LOG_ERROR("[InstantFinish] Global struct is null");
            return false;
        }

        uintptr_t messageQueueAddr = reinterpret_cast<uintptr_t>(globalStruct) + 0x100;
        if (IsBadReadPtr((void*)messageQueueAddr, sizeof(void*))) {
            LOG_ERROR("[InstantFinish] Cannot read message queue address");
            return false;
        }

        void* messageQueue = *reinterpret_cast<void**>(messageQueueAddr);
        if (!messageQueue) {
            LOG_ERROR("[InstantFinish] Message queue is null");
            return false;
        }

        LOG_VERBOSE("[InstantFinish] Manager: 0x" << std::hex << reinterpret_cast<uintptr_t>(manager));
        LOG_VERBOSE("[InstantFinish] MessageQueue: 0x" << std::hex << reinterpret_cast<uintptr_t>(messageQueue));

        // Get function pointers
        SendMessageFunc sendMessage = reinterpret_cast<SendMessageFunc>(g_baseAddress + SEND_MESSAGE_RVA);
        AllocateMemoryFunc allocateMemory = reinterpret_cast<AllocateMemoryFunc>(g_baseAddress + ALLOCATE_MEMORY_RVA);
        InitializeObjectStructFunc initObjectStruct = reinterpret_cast<InitializeObjectStructFunc>(g_baseAddress + INIT_OBJECT_STRUCT_RVA);
        CalculatePlayerValueFunc calculatePlayerValue = reinterpret_cast<CalculatePlayerValueFunc>(g_baseAddress + CALCULATE_PLAYER_VALUE_RVA);

        // Allocate message object (0x18 bytes)
        bool success = false;
        void* messageObj = SafeAllocateMemory(allocateMemory, 0x18, &success);
        if (!success || !messageObj) {
            LOG_ERROR("[InstantFinish] Failed to allocate memory");
            return false;
        }

        // Calculate player value
        int playerValue = SafeCalculatePlayerValue(calculatePlayerValue, manager, &success);
        if (!success) {
            LOG_ERROR("[InstantFinish] Failed to calculate player value");
            return false;
        }
        LOG_VERBOSE("[InstantFinish] PlayerValue: " << std::dec << playerValue);

        // Initialize message object
        // Stack pushes (right to left): playerValue, 0, 0, finishType, 1
        // Function signature: InitializeObjectStruct(this=obj, p1=1, p2=finishType, p3=0, p4=playerValue)
        // finishType: 0 = normal, 1 = timeout, 2 = faultout
        void* messagePtr = SafeInitObjectStruct(initObjectStruct, messageObj, 1, finishType, 0, playerValue, &success);
        if (!success || !messagePtr) {
            LOG_ERROR("[InstantFinish] Failed to initialize message object");
            return false;
        }
        LOG_VERBOSE("[InstantFinish] MessagePtr: 0x" << std::hex << reinterpret_cast<uintptr_t>(messagePtr));

        // Send the message
        SafeSendMessage(sendMessage, messageQueue, reinterpret_cast<int>(messagePtr), &success);
        if (!success) {
            LOG_ERROR("[InstantFinish] Failed to send message");
            return false;
        }

        const char* finishTypeStr = "unknown";
        if (finishType == 0) finishTypeStr = "normal finish";
        else if (finishType == 1) finishTypeStr = "timeout";
        else if (finishType == 2) finishTypeStr = "faultout";

        LOG_VERBOSE("[InstantFinish] SUCCESS! Triggered " << finishTypeStr);
        return true;
    }

    bool InstantTimeOut() {
        return TriggerRaceFinish(1);
    }

    bool InstantFaultOut() {
        return TriggerRaceFinish(2);
    }

    bool InstantFinish() {
        return TriggerRaceFinish(0);
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

        g_baseAddress = baseAddress;
        g_globalStructPtr = reinterpret_cast<void**>(baseAddress + GLOBAL_STRUCT_RVA);
        g_handlePlayerRespawnFunc = reinterpret_cast<HandlePlayerRespawnFunc>(baseAddress + HANDLE_PLAYER_RESPAWN_RVA);
        g_executeTaskWithLocking = reinterpret_cast<ExecuteTaskWithLockingFunc>(baseAddress + EXECUTE_TASK_WITH_LOCKING_RVA);
        g_executeAsyncTask = reinterpret_cast<ExecuteAsyncTaskFunc>(baseAddress + EXECUTE_ASYNC_TASK_RVA);

        if (IsBadReadPtr(g_globalStructPtr, sizeof(void*))) {
            LOG_ERROR("[Respawn] Invalid global struct pointer");
            return false;
        }

        g_initialized = true;

        return true;
    }

    void Shutdown() {
        if (!g_initialized) {
            return;
        }

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
        }

        return RespawnAtCheckpointIndex(targetIndex);
    }

    // ============================================================================
    // Checkpoint Enable/Disable Functions
    // ============================================================================

    // The checkpoint structure from the list at manager+0x938 contains:
    // +0x00: Pointer to another structure (we'll call it triggerStruct)
    // The triggerStruct+0x44 contains a pointer to flags structure
    // 
    // Looking at ProcessCheckpointReached:
    //   if ((*param_1 != 0) && (iVar9 = *(int *)(*param_1 + 0x44), iVar9 != 0)) {
    //     *(ushort *)(iVar9 + 10) = *(ushort *)(iVar9 + 10) | 0x10;
    //     *(ushort *)(iVar9 + 8) = *(ushort *)(iVar9 + 8) | 0x10;
    //   }
    // 
    // This shows checkpoint[0] -> triggerStruct, triggerStruct+0x44 -> flagsStruct
    // The flags at flagsStruct+8 and flagsStruct+10 control checkpoint state
    
    // Offset within checkpoint to get the inner struct pointer
    static constexpr uintptr_t CHECKPOINT_INNER_PTR_OFFSET = 0x00;
    // Offset within inner struct to get the flags struct
    static constexpr uintptr_t INNER_TO_FLAGS_OFFSET = 0x44;
    // Offsets within flags struct for the enable bits
    static constexpr uintptr_t FLAGS_OFFSET_8 = 0x08;  // ushort
    static constexpr uintptr_t FLAGS_OFFSET_A = 0x0A;  // ushort (offset 10 decimal)
    // The bit that's set when checkpoint is triggered (0x10 = bit 4)
    static constexpr uint16_t CHECKPOINT_TRIGGERED_BIT = 0x10;

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

    // Debug function to dump checkpoint structure
    void DebugCheckpointStructure(int index) {
        LOG_INFO("[Checkpoint] === DEBUG CHECKPOINT " << index << " ===");
        
        void* checkpoint = GetCheckpointPointer(index);
        if (!checkpoint) {
            LOG_ERROR("[Checkpoint] Could not get checkpoint at index " << index);
            return;
        }
        
        LOG_INFO("[Checkpoint] Checkpoint ptr: 0x" << std::hex << reinterpret_cast<uintptr_t>(checkpoint));
        
        // Dump first 0x80 bytes of checkpoint structure
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
        
        // Check offsets 0x5C and 0x5D which may be enable flags
        if (!IsBadReadPtr(checkpoint, 0x60)) {
            uint8_t* byteData = reinterpret_cast<uint8_t*>(checkpoint);
            LOG_INFO("[Checkpoint] Checking enable flags:");
            LOG_INFO("[Checkpoint]   checkpoint+0x5C = 0x" << std::hex << (int)byteData[0x5C]);
            LOG_INFO("[Checkpoint]   checkpoint+0x5D = 0x" << std::hex << (int)byteData[0x5D]);
            LOG_INFO("[Checkpoint]   checkpoint+0x5E = 0x" << std::hex << (int)byteData[0x5E]);
            LOG_INFO("[Checkpoint]   checkpoint+0x5F = 0x" << std::hex << (int)byteData[0x5F]);
        }
        
        // Try to follow the pointer at offset 0
        void* innerPtr = *reinterpret_cast<void**>(checkpoint);
        LOG_INFO("[Checkpoint] checkpoint[0] (inner ptr): 0x" << std::hex << reinterpret_cast<uintptr_t>(innerPtr));
        
        if (innerPtr && !IsBadReadPtr(innerPtr, 0x70)) {
            // Check +0x5C, +0x5D in inner struct too
            uint8_t* innerBytes = reinterpret_cast<uint8_t*>(innerPtr);
            LOG_INFO("[Checkpoint] Inner struct enable flags:");
            LOG_INFO("[Checkpoint]   inner+0x5C = 0x" << std::hex << (int)innerBytes[0x5C]);
            LOG_INFO("[Checkpoint]   inner+0x5D = 0x" << std::hex << (int)innerBytes[0x5D]);
            LOG_INFO("[Checkpoint]   inner+0x5E = 0x" << std::hex << (int)innerBytes[0x5E]);
            LOG_INFO("[Checkpoint]   inner+0x5F = 0x" << std::hex << (int)innerBytes[0x5F]);
            
            // Try to get flags struct at inner+0x44
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

        // Get inner pointer at checkpoint+0
        if (IsBadReadPtr(checkpoint, sizeof(void*))) {
            LOG_ERROR("[Checkpoint] Cannot read checkpoint");
            return false;
        }
        
        void* innerPtr = *reinterpret_cast<void**>(checkpoint);
        if (!innerPtr || IsBadReadPtr(innerPtr, 0x48)) {
            LOG_ERROR("[Checkpoint] Invalid inner pointer");
            return false;
        }

        // Get flags struct at inner+0x44
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

        // Read flags at offset 8 - if bit 4 (0x10) is set, checkpoint has been triggered/disabled
        uint16_t* flags = reinterpret_cast<uint16_t*>(reinterpret_cast<uintptr_t>(flagsStruct) + FLAGS_OFFSET_8);
        
        // If bit 4 is NOT set, checkpoint is enabled (hasn't been triggered yet)
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

        // Get inner pointer at checkpoint+0
        if (IsBadReadPtr(checkpoint, sizeof(void*))) {
            LOG_ERROR("[Checkpoint] Cannot read checkpoint");
            return false;
        }
        
        void* innerPtr = *reinterpret_cast<void**>(checkpoint);
        if (!innerPtr || IsBadReadPtr(innerPtr, 0x48)) {
            LOG_ERROR("[Checkpoint] Invalid inner pointer");
            return false;
        }

        // Get flags struct at inner+0x44
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

        // Clear bit 4 at both offset 8 and A to enable
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

        // Get inner pointer at checkpoint+0
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

        // Try setting bytes at checkpoint+0x5C and checkpoint+0x5D to 0 to disable collision
        if (!IsBadWritePtr(checkpoint, 0x60)) {
            uint8_t* checkpointBytes = reinterpret_cast<uint8_t*>(checkpoint);
            checkpointBytes[0x5C] = 0;
            checkpointBytes[0x5D] = 0;
            LOG_VERBOSE("[Checkpoint] Set checkpoint+0x5C/5D to 0");
        }
        
        // Also try inner+0x5C and inner+0x5D
        if (!IsBadWritePtr(innerPtr, 0x60)) {
            uint8_t* innerBytes = reinterpret_cast<uint8_t*>(innerPtr);
            innerBytes[0x5C] = 0;
            innerBytes[0x5D] = 0;
            LOG_VERBOSE("[Checkpoint] Set inner+0x5C/5D to 0");
        }

        // Get flags struct at inner+0x44
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

        // Set bit 4 at both offset 8 and A to disable (mark as triggered)
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
        } else {
            return EnableCheckpoint(index);
        }
    }

    // ============================================================================
    // Finish Line (Last Checkpoint) Enable/Disable Functions
    // ============================================================================

    bool IsFinishLineEnabled() {
        if (!g_initialized) {
            LOG_ERROR("[FinishLine] Not initialized");
            return true; // Assume enabled if not initialized
        }

        int checkpointCount = GetCheckpointCount();
        if (checkpointCount <= 0) {
            LOG_ERROR("[FinishLine] No checkpoints on this track");
            return true; // Assume enabled if no checkpoints
        }

        int finishLineIndex = checkpointCount - 1;
        return IsCheckpointEnabled(finishLineIndex);
    }

    bool EnableFinishLine() {
        if (!g_initialized) {
            LOG_ERROR("[FinishLine] Not initialized");
            return false;
        }

        int checkpointCount = GetCheckpointCount();
        if (checkpointCount <= 0) {
            LOG_ERROR("[FinishLine] No checkpoints on this track");
            return false;
        }

        int finishLineIndex = checkpointCount - 1;
        
        // First, enable the checkpoint (clears triggered bit 0x10)
        bool result = EnableCheckpoint(finishLineIndex);
        
        // Second, unpatch the code to restore finish logic
        if (!UnpatchFinishLineCheck()) {
            LOG_ERROR("[FinishLine] Failed to unpatch finish line check");
            return false;
        }
        
        // Third, restore the SECOND call to ProcessCheckpointReached in UpdateCheckpointsInRange
        if (!UnpatchUpdateCheckpointsCall()) {
            LOG_ERROR("[FinishLine] Failed to unpatch second UpdateCheckpoints call");
            return false;
        }
        
        // Finally, restore the FIRST call to ProcessCheckpointReached in UpdateCheckpointsInRange  
        if (!UnpatchUpdateCheckpointsFirstCall()) {
            LOG_ERROR("[FinishLine] Failed to unpatch first UpdateCheckpoints call");
            return false;
        }
        
        if (result) {
            LOG_INFO("[FinishLine] Finish line ENABLED (checkpoint " << finishLineIndex << ") - Race will end when crossed");
        }
        
        return result;
    }

    bool DisableFinishLine() {
        if (!g_initialized) {
            LOG_ERROR("[FinishLine] Not initialized");
            return false;
        }

        // Check if we're in a race (have a valid bike pointer)
        void* bikePtr = GetBikePointer();
        if (!bikePtr) {
            LOG_ERROR("[FinishLine] Not in a race - no bike pointer");
            return false;
        }

        int checkpointCount = GetCheckpointCount();
        if (checkpointCount <= 0) {
            LOG_ERROR("[FinishLine] No checkpoints on this track");
            return false;
        }

        int finishLineIndex = checkpointCount - 1;
        
        // First, NOP out the FIRST call to ProcessCheckpointReached in UpdateCheckpointsInRange
        // This call is at 0x92953b and happens when current checkpoint equals first valid element
        if (!PatchUpdateCheckpointsFirstCall()) {
            LOG_ERROR("[FinishLine] Failed to patch first UpdateCheckpoints call");
            return false;
        }
        
        // Second, NOP out the SECOND call to ProcessCheckpointReached in UpdateCheckpointsInRange
        // This call is in the main loop at 0x9297b5
        if (!PatchUpdateCheckpointsCall()) {
            LOG_ERROR("[FinishLine] Failed to patch second UpdateCheckpoints call");
            return false;
        }
        
        // Third, patch the code to skip finish logic
        if (!PatchFinishLineCheck()) {
            LOG_ERROR("[FinishLine] Failed to patch finish line check");
            return false;
        }
        
        // Finally, disable the checkpoint (sets triggered bit 0x10)
        bool result = DisableCheckpoint(finishLineIndex);
        
        if (result) {
            LOG_INFO("[FinishLine] Finish line DISABLED (checkpoint " << finishLineIndex << ") - Race will NOT end when crossed");
        }
        
        return result;
    }

    bool PatchUpdateCheckpointsCall() {
        if (!g_initialized) {
            LOG_ERROR("[UpdateCheckpointsPatch] Not initialized");
            return false;
        }

        // We need to skip the entire checkpoint processing block when finish line is disabled
        //
        // Original code at 0x9297ac-0x9297c3:
        //   009297ac: PUSH ECX                     
        //   009297ad: MOVSS dword ptr [ESP],XMM0  
        //   009297b2: PUSH EDI                     ; Push checkpoint pointer
        //   009297b3: MOV ECX,ESI                  ; this = bike
        //   009297b5: CALL 0x00928b60              ; ProcessCheckpointReached
        //   009297ba: MOV EBX,dword ptr [EBP + -0x24]
        //   009297bd: MOV dword ptr [ESI + 0x1dc],EDI  ; Update current checkpoint
        //   009297c3: ... (continue)
        //
        // We want to jump from 0x9297ac directly to 0x9297c3 to skip all of this
        // Jump distance: 0x9297c3 - 0x9297ac = 0x17 bytes
        // JMP short can handle this (2 bytes: EB 15 - offset is 0x15 because it's relative to next instruction)
        
        uintptr_t patchAddr = g_baseAddress + UPDATE_CHECKPOINTS_CALL_RVA; // 0x9297ac (PUSH ECX)
        
        LOG_VERBOSE("[UpdateCheckpointsPatch] Patching checkpoint processing block at 0x" << std::hex << patchAddr);
        
        if (IsBadReadPtr((void*)patchAddr, 24)) {
            LOG_ERROR("[UpdateCheckpointsPatch] Cannot read patch address");
            return false;
        }
        
        uint8_t* bytes = reinterpret_cast<uint8_t*>(patchAddr);
        
        // Check if already patched (starts with JMP)
        if (bytes[0] == 0xEB) {
            LOG_VERBOSE("[UpdateCheckpointsPatch] Already patched");
            return true;
        }
        
        // Verify it's a PUSH ECX instruction
        if (bytes[0] != 0x51) {
            LOG_ERROR("[UpdateCheckpointsPatch] Expected PUSH ECX (0x51), found 0x" << std::hex << (int)bytes[0]);
            return false;
        }
        
        DWORD oldProtect;
        if (!VirtualProtect((void*)patchAddr, 24, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            LOG_ERROR("[UpdateCheckpointsPatch] Failed to change memory protection");
            return false;
        }
        
        // JMP short to 0x9297c3
        // From 0x9297ac, next instruction would be 0x9297ae
        // Target is 0x9297c3
        // Offset = 0x9297c3 - 0x9297ae = 0x15
        bytes[0] = 0xEB;  // JMP short
        bytes[1] = 0x15;  // Offset (+21 bytes)
        
        // Fill rest with NOPs for safety
        for (int i = 2; i < 24; i++) {
            bytes[i] = 0x90;  // NOP
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
        
        uintptr_t patchAddr = g_baseAddress + UPDATE_CHECKPOINTS_CALL_RVA;
        
        LOG_VERBOSE("[UpdateCheckpointsUnpatch] Restoring checkpoint processing block at 0x" << std::hex << patchAddr);
        
        if (IsBadReadPtr((void*)patchAddr, 24)) {
            LOG_ERROR("[UpdateCheckpointsUnpatch] Cannot read patch address");
            return false;
        }
        
        uint8_t* bytes = reinterpret_cast<uint8_t*>(patchAddr);
        
        // Check if already restored
        if (bytes[0] == 0x51) {
            LOG_VERBOSE("[UpdateCheckpointsUnpatch] Already restored");
            return true;
        }
        
        // Verify it's a JMP
        if (bytes[0] != 0xEB) {
            LOG_ERROR("[UpdateCheckpointsUnpatch] Unexpected bytes: 0x" << std::hex << (int)bytes[0]);
            return false;
        }
        
        DWORD oldProtect;
        if (!VirtualProtect((void*)patchAddr, 24, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            LOG_ERROR("[UpdateCheckpointsUnpatch] Failed to change memory protection");
            return false;
        }
        
        // Restore original code:
        // 009297ac: PUSH ECX (1 byte: 51)
        bytes[0] = 0x51;
        
        // 009297ad: MOVSS dword ptr [ESP],XMM0 (5 bytes: F3 0F 11 04 24)
        bytes[1] = 0xF3;
        bytes[2] = 0x0F;
        bytes[3] = 0x11;
        bytes[4] = 0x04;
        bytes[5] = 0x24;
        
        // 009297b2: PUSH EDI (1 byte: 57)
        bytes[6] = 0x57;
        
        // 009297b3: MOV ECX,ESI (2 bytes: 8B CE)
        bytes[7] = 0x8B;
        bytes[8] = 0xCE;
        
        // 009297b5: CALL 0x00928b60 (5 bytes: E8 + offset)
        int32_t callOffset = 0x928b60 - (0x9297b5 + 5);
        bytes[9] = 0xE8;
        *reinterpret_cast<int32_t*>(&bytes[10]) = callOffset;
        
        // 009297ba: MOV EBX,dword ptr [EBP + -0x24] (3 bytes: 8B 5D DC)
        bytes[14] = 0x8B;
        bytes[15] = 0x5D;
        bytes[16] = 0xDC;
        
        // 009297bd: MOV dword ptr [ESI + 0x1dc],EDI (6 bytes: 89 BE DC 01 00 00)
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

    // Patch the FIRST call to ProcessCheckpointReached in UpdateCheckpointsInRange
    // This call happens at 0x92953b when current checkpoint equals first valid element
    bool PatchUpdateCheckpointsFirstCall() {
        if (!g_initialized) {
            LOG_ERROR("[UpdateCheckpointsFirstPatch] Not initialized");
            return false;
        }

        // Original code at 0x929529-0x929540:
        //   00929529: XORPS XMM0,XMM0         ; 3 bytes: 0F 57 C0
        //   0092952c: PUSH ECX               ; 1 byte: 51
        //   0092952d: MOV ECX,[ESI+0x1dc]    ; 6 bytes: 8B 8E DC 01 00 00
        //   00929533: MOVSS [ESP],XMM0       ; 5 bytes: F3 0F 11 04 24
        //   00929538: PUSH ECX               ; 1 byte: 51
        //   00929539: MOV ECX,ESI            ; 2 bytes: 8B CE
        //   0092953b: CALL 0x00928b60        ; 5 bytes: E8 xx xx xx xx
        //   00929540: MOV EAX,...            ; next instruction
        //
        // Total: 23 bytes from 0x929529 to 0x929540
        // We want to jump from 0x929529 to 0x929540
        // JMP short offset = 0x929540 - 0x92952b = 0x15 (21 bytes)
        
        uintptr_t patchAddr = g_baseAddress + UPDATE_CHECKPOINTS_FIRST_CALL_RVA;
        
        LOG_VERBOSE("[UpdateCheckpointsFirstPatch] Patching first checkpoint call at 0x" << std::hex << patchAddr);
        
        if (IsBadReadPtr((void*)patchAddr, 23)) {
            LOG_ERROR("[UpdateCheckpointsFirstPatch] Cannot read patch address");
            return false;
        }
        
        uint8_t* bytes = reinterpret_cast<uint8_t*>(patchAddr);
        
        // Check if already patched (starts with JMP)
        if (bytes[0] == 0xEB) {
            LOG_VERBOSE("[UpdateCheckpointsFirstPatch] Already patched");
            return true;
        }
        
        // Verify we have the expected XORPS instruction (0F 57 C0)
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
        
        // JMP short to 0x929540
        // From 0x929529, next instruction would be 0x92952b
        // Target is 0x929540
        // Offset = 0x929540 - 0x92952b = 0x15 (21 bytes)
        bytes[0] = 0xEB;  // JMP short
        bytes[1] = 0x15;  // Offset (+21 bytes)
        
        // Fill rest with NOPs
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
        
        uintptr_t patchAddr = g_baseAddress + UPDATE_CHECKPOINTS_FIRST_CALL_RVA;
        
        LOG_VERBOSE("[UpdateCheckpointsFirstUnpatch] Restoring first checkpoint call at 0x" << std::hex << patchAddr);
        
        if (IsBadReadPtr((void*)patchAddr, 23)) {
            LOG_ERROR("[UpdateCheckpointsFirstUnpatch] Cannot read patch address");
            return false;
        }
        
        uint8_t* bytes = reinterpret_cast<uint8_t*>(patchAddr);
        
        // Check if already restored (has XORPS)
        if (bytes[0] == 0x0F && bytes[1] == 0x57) {
            LOG_VERBOSE("[UpdateCheckpointsFirstUnpatch] Already restored");
            return true;
        }
        
        // Verify it's our JMP
        if (bytes[0] != 0xEB) {
            LOG_ERROR("[UpdateCheckpointsFirstUnpatch] Unexpected bytes: 0x" << std::hex << (int)bytes[0]);
            return false;
        }
        
        DWORD oldProtect;
        if (!VirtualProtect((void*)patchAddr, 23, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            LOG_ERROR("[UpdateCheckpointsFirstUnpatch] Failed to change memory protection");
            return false;
        }
        
        // Restore original code:
        // 00929529: XORPS XMM0,XMM0 (3 bytes: 0F 57 C0)
        bytes[0] = 0x0F;
        bytes[1] = 0x57;
        bytes[2] = 0xC0;
        
        // 0092952c: PUSH ECX (1 byte: 51)
        bytes[3] = 0x51;
        
        // 0092952d: MOV ECX,dword ptr [ESI + 0x1dc] (6 bytes: 8B 8E DC 01 00 00)
        bytes[4] = 0x8B;
        bytes[5] = 0x8E;
        bytes[6] = 0xDC;
        bytes[7] = 0x01;
        bytes[8] = 0x00;
        bytes[9] = 0x00;
        
        // 00929533: MOVSS dword ptr [ESP],XMM0 (5 bytes: F3 0F 11 04 24)
        bytes[10] = 0xF3;
        bytes[11] = 0x0F;
        bytes[12] = 0x11;
        bytes[13] = 0x04;
        bytes[14] = 0x24;
        
        // 00929538: PUSH ECX (1 byte: 51)
        bytes[15] = 0x51;
        
        // 00929539: MOV ECX,ESI (2 bytes: 8B CE)
        bytes[16] = 0x8B;
        bytes[17] = 0xCE;
        
        // 0092953b: CALL 0x00928b60 (5 bytes: E8 + offset)
        // Calculate relative call offset: target - (call_addr + 5)
        int32_t callOffset = 0x928b60 - (0x92953b + 5);
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

        // We need to add a check at 0x928d88 that tests if the checkpoint has bit 0x10 set
        // If it does, we should jump to the exit (0x928fc0) to skip all processing
        // 
        // Original code at 0x928d88:
        //   00928d88: MOV EAX,dword ptr [ESI]       ; Get checkpoint inner pointer
        //   00928d8a: TEST EAX,EAX                  ; Check if null
        //   00928d8c: JZ 0x00928da2                 ; Skip if null
        //   00928d8e: MOV EAX,dword ptr [EAX + 0x44] ; Get flags struct pointer  
        //   00928d91: TEST EAX,EAX                  ; Check if null
        //   00928d93: JZ 0x00928da2                 ; Skip if null
        //   00928d95: MOV ECX,0x10                  ; Load bit mask
        //   00928d9a: OR word ptr [EAX + 0xa],CX    ; Set triggered bit at offset 0xA
        //   00928d9e: OR word ptr [EAX + 0x8],CX    ; Set triggered bit at offset 0x8
        //
        // We want to insert a check BEFORE the OR instructions to see if the bit is already set,
        // and if so, jump to 0x928fc0 (the function exit)
        //
        // New code at 0x928d95:
        //   00928d95: TEST word ptr [EAX + 0x8],0x10  ; Check if already triggered (6 bytes: 66 F7 40 08 10 00)
        //   00928d9b: JNZ 0x00928fc0                  ; Jump to exit if triggered (6 bytes: 0F 85 1F 02 00 00)
        //   00928da1: NOP                             ; Padding (1 byte: 90)
        //   00928da2: ... (continue with rest)
        //
        // This requires 13 bytes total (0x928d95 to 0x928da1)
        // Original has: MOV ECX,0x10 (5 bytes) + OR [EAX+0xa],CX (4 bytes) + OR [EAX+0x8],CX (4 bytes) = 13 bytes
        // Perfect match!
        
        uintptr_t patchAddr = g_baseAddress + PROCESS_CHECKPOINT_EARLY_CHECK_RVA + 0xD; // Start at 0x928d95
        
        LOG_VERBOSE("[CheckpointEarlyReturn] Patching checkpoint early return at 0x" << std::hex << patchAddr);
        
        if (IsBadReadPtr((void*)patchAddr, 13)) {
            LOG_ERROR("[CheckpointEarlyReturn] Cannot read patch address");
            return false;
        }
        
        uint8_t* bytes = reinterpret_cast<uint8_t*>(patchAddr);
        
        // Check if already patched (first bytes should be TEST)
        if (bytes[0] == 0x66 && bytes[1] == 0xF7) {
            LOG_VERBOSE("[CheckpointEarlyReturn] Already patched");
            return true;
        }
        
        // Verify we have the original code
        if (bytes[0] != 0xB9 || bytes[1] != 0x10) { // MOV ECX, 0x10
            LOG_ERROR("[CheckpointEarlyReturn] Unexpected bytes: 0x" << std::hex << (int)bytes[0] << " 0x" << (int)bytes[1]);
            return false;
        }
        
        DWORD oldProtect;
        if (!VirtualProtect((void*)patchAddr, 13, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            LOG_ERROR("[CheckpointEarlyReturn] Failed to change memory protection");
            return false;
        }
        
        // Calculate jump offset to 0x928fc0 from 0x928d9b (after the JNZ instruction)
        // JNZ target = 0x928fc0, JNZ at 0x928d9b, next instruction at 0x928da1
        // Offset = 0x928fc0 - 0x928da1 = 0x21F
        int32_t jumpOffset = 0x21F;
        
        // Write new code:
        // TEST word ptr [EAX + 0x8], 0x10  (6 bytes)
        bytes[0] = 0x66;  // Operand size prefix for word operation
        bytes[1] = 0xF7;  // TEST r/m16, imm16
        bytes[2] = 0x40;  // ModR/M byte: [EAX + disp8]
        bytes[3] = 0x08;  // Displacement: +0x8
        bytes[4] = 0x10;  // Immediate value low byte
        bytes[5] = 0x00;  // Immediate value high byte
        
        // JNZ 0x928fc0 (6 bytes)
        bytes[6] = 0x0F;  // Two-byte opcode prefix
        bytes[7] = 0x85;  // JNZ (near)
        *reinterpret_cast<int32_t*>(&bytes[8]) = jumpOffset; // Offset (little-endian)
        
        // NOP (1 byte)
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
        
        uintptr_t patchAddr = g_baseAddress + PROCESS_CHECKPOINT_EARLY_CHECK_RVA + 0xD;
        
        LOG_VERBOSE("[CheckpointEarlyReturnRestore] Restoring original checkpoint code at 0x" << std::hex << patchAddr);
        
        if (IsBadReadPtr((void*)patchAddr, 13)) {
            LOG_ERROR("[CheckpointEarlyReturnRestore] Cannot read patch address");
            return false;
        }
        
        uint8_t* bytes = reinterpret_cast<uint8_t*>(patchAddr);
        
        // Check if already restored
        if (bytes[0] == 0xB9 && bytes[1] == 0x10) {
            LOG_VERBOSE("[CheckpointEarlyReturnRestore] Already restored");
            return true;
        }
        
        // Verify it's patched code
        if (bytes[0] != 0x66 || bytes[1] != 0xF7) {
            LOG_ERROR("[CheckpointEarlyReturnRestore] Unexpected bytes: 0x" << std::hex << (int)bytes[0] << " 0x" << (int)bytes[1]);
            return false;
        }
        
        DWORD oldProtect;
        if (!VirtualProtect((void*)patchAddr, 13, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            LOG_ERROR("[CheckpointEarlyReturnRestore] Failed to change memory protection");
            return false;
        }
        
        // Restore original code:
        // MOV ECX, 0x10 (5 bytes)
        bytes[0] = 0xB9;  // MOV ECX, imm32
        bytes[1] = 0x10;  
        bytes[2] = 0x00;
        bytes[3] = 0x00;
        bytes[4] = 0x00;
        
        // OR word ptr [EAX + 0xA], CX (4 bytes)
        bytes[5] = 0x66;  // Operand size prefix
        bytes[6] = 0x09;  // OR r/m16, r16
        bytes[7] = 0x48;  // ModR/M: [EAX + disp8], CX
        bytes[8] = 0x0A;  // Displacement: +0xA
        
        // OR word ptr [EAX + 0x8], CX (4 bytes)
        bytes[9] = 0x66;   // Operand size prefix
        bytes[10] = 0x09;  // OR r/m16, r16  
        bytes[11] = 0x48;  // ModR/M: [EAX + disp8], CX
        bytes[12] = 0x08;  // Displacement: +0x8
        
        VirtualProtect((void*)patchAddr, 13, oldProtect, &oldProtect);
        
        LOG_VERBOSE("[CheckpointEarlyReturnRestore] SUCCESS! Restored original checkpoint processing!");
        return true;
    }

    bool PatchFinishLineCheck() {
        if (!g_initialized) {
            LOG_ERROR("[FinishLinePatch] Not initialized");
            return false;
        }

        // This patches the JNZ instruction that skips the finish logic
        // When finish line is disabled, we want to ALWAYS skip (convert JNZ to JMP)
        uintptr_t jnzAddr = g_baseAddress + PROCESS_CHECKPOINT_FINISH_CHECK_RVA;
        
        LOG_VERBOSE("[FinishLinePatch] Patching finish line check at 0x" << std::hex << jnzAddr);
        
        if (IsBadReadPtr((void*)jnzAddr, 6)) {
            LOG_ERROR("[FinishLinePatch] Cannot read JNZ address");
            return false;
        }
        
        uint8_t* bytes = reinterpret_cast<uint8_t*>(jnzAddr);
        
        // Check what we have
        if (bytes[0] == 0xEB) {
            LOG_VERBOSE("[FinishLinePatch] Already patched (found JMP)");
            return true;
        }
        
        if (bytes[0] == 0x75 || (bytes[0] == 0x0F && bytes[1] == 0x85)) {
            // It's JNZ, we need to convert to JMP
            DWORD oldProtect;
            int patchSize = (bytes[0] == 0x75) ? 2 : 6;
            
            if (!VirtualProtect((void*)jnzAddr, patchSize, PAGE_EXECUTE_READWRITE, &oldProtect)) {
                LOG_ERROR("[FinishLinePatch] Failed to change memory protection");
                return false;
            }
            
            if (patchSize == 2) {
                // Short JNZ -> Short JMP
                bytes[0] = 0xEB;  // JMP (short)
                // Keep the same offset (bytes[1])
            } else {
                // Long JNZ -> Long JMP
                bytes[0] = 0xE9;  // JMP (near)
                // Keep the same offset (bytes[1-4])
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
        
        uintptr_t jmpAddr = g_baseAddress + PROCESS_CHECKPOINT_FINISH_CHECK_RVA;
        
        LOG_VERBOSE("[FinishLineUnpatch] Restoring finish line check at 0x" << std::hex << jmpAddr);
        
        if (IsBadReadPtr((void*)jmpAddr, 6)) {
            LOG_ERROR("[FinishLineUnpatch] Cannot read JMP address");
            return false;
        }
        
        uint8_t* bytes = reinterpret_cast<uint8_t*>(jmpAddr);
        
        // Check if already restored
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
            // Short JMP -> Short JNZ
            bytes[0] = 0x75;  // JNZ (short)
        } else {
            // Long JMP -> Long JNZ
            bytes[0] = 0x0F;
            bytes[1] = 0x85;
        }
        
        VirtualProtect((void*)jmpAddr, patchSize, oldProtect, &oldProtect);
        
        LOG_VERBOSE("[FinishLineUnpatch] SUCCESS! Restored JNZ - finish line will trigger finish logic normally!");
        return true;
    }

    bool ToggleFinishLine() {
        if (IsFinishLineEnabled()) {
            return DisableFinishLine();
        } else {
            return EnableFinishLine();
        }
    }

    void CheckHotkey() {
        if (!g_initialized) {
            return;
        }

        // Enforce fault limit - if validation is enabled and faults exceed limit, trigger fault-out (once)
        if (!IsFaultValidationDisabled()) {
            int currentFaults = GetFaultCount();
            uint32_t faultLimit = GetFaultLimit();
            if (currentFaults >= (int)faultLimit) {
                if (!g_faultOutTriggered) {
                    LOG_VERBOSE("[LimitEnforce] Faults (" << currentFaults << ") >= limit (" << faultLimit << "). Triggering fault-out!");
                    InstantFaultOut();
                    g_faultOutTriggered = true;
                }
            } else {
                // Reset flag when faults go back under limit
                g_faultOutTriggered = false;
            }
        } else {
            // Reset flag when validation is disabled
            g_faultOutTriggered = false;
        }

        // Enforce time limit - if validation is enabled and time exceeds limit, trigger time-out (once)
        if (!IsTimeValidationDisabled()) {
            int currentTimeMs = GetRaceTimeMs();
            uint32_t timeLimit = GetTimeLimit();
            // Convert time limit from game ticks to ms (60 ticks/sec)
            int timeLimitMs = (int)(timeLimit * 1000 / 60);
            if (currentTimeMs >= timeLimitMs) {
                if (!g_timeOutTriggered) {
                    LOG_VERBOSE("[LimitEnforce] Time (" << currentTimeMs/1000 << "s) >= limit (" << timeLimitMs/1000 << "s). Triggering time-out!");
                    InstantTimeOut();
                    g_timeOutTriggered = true;
                }
            } else {
                // Reset flag when time goes back under limit
                g_timeOutTriggered = false;
            }
        } else {
            // Reset flag when validation is disabled
            g_timeOutTriggered = false;
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
        }

        // Reset faults to 0
        if (Keybindings::IsActionPressed(Keybindings::Action::ResetFaults)) {
            SetFaultCounterValue(0);
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
        }

        // Add 10 minute / 36000 frames at 60fps
        if (Keybindings::IsActionPressed(Keybindings::Action::Add10Minute)) {
            AdjustRaceTimeMs(36000);
        }

        // Reset time to 0
        if (Keybindings::IsActionPressed(Keybindings::Action::ResetTime)) {
            SetRaceTimeMs(0);
        }

        // Toggle ALL limit validation (F4)
        if (Keybindings::IsActionPressed(Keybindings::Action::ToggleLimitValidation)) {
            // Check current state by looking at one of the patches
            if (IsFaultValidationDisabled() || IsTimeValidationDisabled()) {
                // Currently disabled, so enable
                EnableAllLimitValidation();
            } else {
                // Currently enabled, so disable
                DisableAllLimitValidation();
            }
        }

        // Ctrl+0-9 = Jump to checkpoint
        if (ctrlPressed) {
            for (int i = 0; i < 10; i++) {
                int vkCode = '0' + i;
                bool keyIsPressed = (GetAsyncKeyState(vkCode) & 0x8000) != 0;

                if (keyIsPressed && !g_numberKeysPressed[i]) {
                    LOG_VERBOSE("[Respawn] Ctrl+" << i << " pressed, jumping to checkpoint " << i);
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