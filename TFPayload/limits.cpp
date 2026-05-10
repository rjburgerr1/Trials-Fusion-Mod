#include "pch.h"
#include "limits.h"
#include "logging.h"
#include "base-address.h"
#include "prevent-finish.h"
#include "respawn.h"
#include <Windows.h>

namespace Limits {
    // ============================================================================
    // Game Memory Addresses - UPLAY VERSION (RVA offsets - base 0x700000)
    // ============================================================================

    // Limit bypass patch locations - Uplay
    static constexpr uintptr_t TIME_LIMIT_CMP_RVA_UPLAY = 0x276e08;
    static constexpr uintptr_t FAULT_LIMIT_CMP_RVA_UPLAY = 0x276e4f;
    static constexpr uintptr_t COMPLETION_COND_FAULT_JNC_RVA_UPLAY = 0x231567;
    static constexpr uintptr_t COMPLETION_COND_TIME_JNC_RVA_UPLAY = 0x23155e;
    static constexpr uintptr_t COMPLETION_COND_TIME2_JC_RVA_UPLAY = 0x23158e;
    static constexpr uintptr_t FINISH_FAULT_CHECK_JC_RVA_UPLAY = 0x276e53;  // JC in CheckTimeAndFaultLimits that returns faultout
    static constexpr uintptr_t FINISH_THRESHOLD_CHECK_JNZ_RVA_UPLAY = 0x276e73;  // JNZ after CheckAnyTimerThresholdExceeded

    // UpdateRaceEndState patch locations - Uplay
    static constexpr uintptr_t UPDATE_RACE_END_PUSH_RVA_UPLAY = 0x280d93;

    // RaceUpdate timer check locations - Uplay
    static constexpr uintptr_t RACEUPDATE_TIMER_CHECK_RVA_UPLAY = 0x28f1b6;

    // Multiplayer limit checks - Uplay
    static constexpr uintptr_t MULTIPLAYER_MODE1_TIME_CMP_RVA_UPLAY = 0x231484;
    static constexpr uintptr_t MULTIPLAYER_MODE1_TIME_JNC_RVA_UPLAY = 0x23148b;
    static constexpr uintptr_t MULTIPLAYER_MODE1_FAULT_CALL_RVA_UPLAY = 0x23148f;
    static constexpr uintptr_t MULTIPLAYER_MODE1_FAULT_JNZ_RVA_UPLAY = 0x231496;
    static constexpr uintptr_t MULTIPLAYER_MODE2_TIME_CMP_RVA_UPLAY = 0x2313d4;
    static constexpr uintptr_t MULTIPLAYER_MODE2_TIME_JNC_RVA_UPLAY = 0x2313db;
    static constexpr uintptr_t MULTIPLAYER_MODE2_FAULT_CALL_RVA_UPLAY = 0x2313df;
    static constexpr uintptr_t MULTIPLAYER_MODE2_FAULT_JNZ_RVA_UPLAY = 0x2313e6;

    // InitObjectStruct for RestoreRaceEndSuccess - Uplay
    static constexpr uintptr_t INIT_OBJECT_STRUCT_RVA_UPLAY = 0x55a00;

    // ============================================================================
    // Game Memory Addresses - STEAM VERSION (RVA offsets - base 0x140000)
    // ============================================================================

    // Limit bypass patch locations - Steam
    static constexpr uintptr_t TIME_LIMIT_CMP_RVA_STEAM = 0x276888;
    static constexpr uintptr_t FAULT_LIMIT_CMP_RVA_STEAM = 0x2768cf;
    static constexpr uintptr_t COMPLETION_COND_FAULT_JNC_RVA_STEAM = 0x230cb7;
    static constexpr uintptr_t COMPLETION_COND_TIME_JNC_RVA_STEAM = 0x230cae;
    static constexpr uintptr_t COMPLETION_COND_TIME2_JC_RVA_STEAM = 0x230cde;
    static constexpr uintptr_t FINISH_FAULT_CHECK_JC_RVA_STEAM = 0x2768d3;  // JC in CheckTimeAndFaultLimits that returns faultout
    static constexpr uintptr_t FINISH_THRESHOLD_CHECK_JNZ_RVA_STEAM = 0x2768f3;  // JNZ after CheckAnyTimerThresholdExceeded

    // UpdateRaceEndState patch locations - Steam
    static constexpr uintptr_t UPDATE_RACE_END_PUSH_RVA_STEAM = 0x2807d3;

    // RaceUpdate timer check locations - Steam
    static constexpr uintptr_t RACEUPDATE_TIMER_CHECK_RVA_STEAM = 0x28eb46;

    // Multiplayer limit checks - Steam
    static constexpr uintptr_t MULTIPLAYER_MODE1_TIME_CMP_RVA_STEAM = 0x230bd4;
    static constexpr uintptr_t MULTIPLAYER_MODE1_TIME_JNC_RVA_STEAM = 0x230bdb;
    static constexpr uintptr_t MULTIPLAYER_MODE1_FAULT_CALL_RVA_STEAM = 0x230bdf;
    static constexpr uintptr_t MULTIPLAYER_MODE1_FAULT_JNZ_RVA_STEAM = 0x230be6;
    static constexpr uintptr_t MULTIPLAYER_MODE2_TIME_CMP_RVA_STEAM = 0x230b24;
    static constexpr uintptr_t MULTIPLAYER_MODE2_TIME_JNC_RVA_STEAM = 0x230b2b;
    static constexpr uintptr_t MULTIPLAYER_MODE2_FAULT_CALL_RVA_STEAM = 0x230b2f;
    static constexpr uintptr_t MULTIPLAYER_MODE2_FAULT_JNZ_RVA_STEAM = 0x230b36;

    // InitObjectStruct for RestoreRaceEndSuccess - Steam
    static constexpr uintptr_t INIT_OBJECT_STRUCT_RVA_STEAM = 0x55a70;

    // ============================================================================
    // Helper functions to get correct RVA based on detected version
    // ============================================================================

    static uintptr_t GetTimeLimitCmpRVA() {
        return BaseAddress::IsSteamVersion() ? TIME_LIMIT_CMP_RVA_STEAM : TIME_LIMIT_CMP_RVA_UPLAY;
    }

    static uintptr_t GetFaultLimitCmpRVA() {
        return BaseAddress::IsSteamVersion() ? FAULT_LIMIT_CMP_RVA_STEAM : FAULT_LIMIT_CMP_RVA_UPLAY;
    }

    static uintptr_t GetCompletionCondFaultJncRVA() {
        return BaseAddress::IsSteamVersion() ? COMPLETION_COND_FAULT_JNC_RVA_STEAM : COMPLETION_COND_FAULT_JNC_RVA_UPLAY;
    }

    static uintptr_t GetCompletionCondTimeJncRVA() {
        return BaseAddress::IsSteamVersion() ? COMPLETION_COND_TIME_JNC_RVA_STEAM : COMPLETION_COND_TIME_JNC_RVA_UPLAY;
    }

    static uintptr_t GetCompletionCondTime2JcRVA() {
        return BaseAddress::IsSteamVersion() ? COMPLETION_COND_TIME2_JC_RVA_STEAM : COMPLETION_COND_TIME2_JC_RVA_UPLAY;
    }

    static uintptr_t GetFinishFaultCheckJcRVA() {
        return BaseAddress::IsSteamVersion() ? FINISH_FAULT_CHECK_JC_RVA_STEAM : FINISH_FAULT_CHECK_JC_RVA_UPLAY;
    }
    
    static uintptr_t GetFinishThresholdCheckJnzRVA() {
        return BaseAddress::IsSteamVersion() ? FINISH_THRESHOLD_CHECK_JNZ_RVA_STEAM : FINISH_THRESHOLD_CHECK_JNZ_RVA_UPLAY;
    }

    static uintptr_t GetUpdateRaceEndPushRVA() {
        return BaseAddress::IsSteamVersion() ? UPDATE_RACE_END_PUSH_RVA_STEAM : UPDATE_RACE_END_PUSH_RVA_UPLAY;
    }

    static uintptr_t GetRaceUpdateTimerCheckRVA() {
        return BaseAddress::IsSteamVersion() ? RACEUPDATE_TIMER_CHECK_RVA_STEAM : RACEUPDATE_TIMER_CHECK_RVA_UPLAY;
    }

    static uintptr_t GetMultiplayerMode1TimeCmpRVA() {
        return BaseAddress::IsSteamVersion() ? MULTIPLAYER_MODE1_TIME_CMP_RVA_STEAM : MULTIPLAYER_MODE1_TIME_CMP_RVA_UPLAY;
    }

    static uintptr_t GetMultiplayerMode1TimeJncRVA() {
        return BaseAddress::IsSteamVersion() ? MULTIPLAYER_MODE1_TIME_JNC_RVA_STEAM : MULTIPLAYER_MODE1_TIME_JNC_RVA_UPLAY;
    }

    static uintptr_t GetMultiplayerMode1FaultCallRVA() {
        return BaseAddress::IsSteamVersion() ? MULTIPLAYER_MODE1_FAULT_CALL_RVA_STEAM : MULTIPLAYER_MODE1_FAULT_CALL_RVA_UPLAY;
    }

    static uintptr_t GetMultiplayerMode1FaultJnzRVA() {
        return BaseAddress::IsSteamVersion() ? MULTIPLAYER_MODE1_FAULT_JNZ_RVA_STEAM : MULTIPLAYER_MODE1_FAULT_JNZ_RVA_UPLAY;
    }

    static uintptr_t GetMultiplayerMode2TimeCmpRVA() {
        return BaseAddress::IsSteamVersion() ? MULTIPLAYER_MODE2_TIME_CMP_RVA_STEAM : MULTIPLAYER_MODE2_TIME_CMP_RVA_UPLAY;
    }

    static uintptr_t GetMultiplayerMode2TimeJncRVA() {
        return BaseAddress::IsSteamVersion() ? MULTIPLAYER_MODE2_TIME_JNC_RVA_STEAM : MULTIPLAYER_MODE2_TIME_JNC_RVA_UPLAY;
    }

    static uintptr_t GetMultiplayerMode2FaultCallRVA() {
        return BaseAddress::IsSteamVersion() ? MULTIPLAYER_MODE2_FAULT_CALL_RVA_STEAM : MULTIPLAYER_MODE2_FAULT_CALL_RVA_UPLAY;
    }

    static uintptr_t GetMultiplayerMode2FaultJnzRVA() {
        return BaseAddress::IsSteamVersion() ? MULTIPLAYER_MODE2_FAULT_JNZ_RVA_STEAM : MULTIPLAYER_MODE2_FAULT_JNZ_RVA_UPLAY;
    }

    static uintptr_t GetInitObjectStructRVA() {
        return BaseAddress::IsSteamVersion() ? INIT_OBJECT_STRUCT_RVA_STEAM : INIT_OBJECT_STRUCT_RVA_UPLAY;
    }

    // ============================================================================
    // Default Limits
    // ============================================================================

    static constexpr uint32_t DEFAULT_TIME_LIMIT = 0x1A5E0;    // 30 minutes
    static constexpr uint32_t DEFAULT_FAULT_LIMIT = 0x1F4;     // 500 faults

    // ============================================================================
    // Global State
    // ============================================================================

    static bool g_initialized = false;
    static uintptr_t g_baseAddress = 0;

    // ============================================================================
    // Forward Declarations
    // ============================================================================

    static bool DisableCompletionConditionCheck();
    static bool EnableCompletionConditionCheck();
    static bool DisableTimeCompletionCheck();
    static bool EnableTimeCompletionCheck();

    // ============================================================================
    // Initialization
    // ============================================================================

    bool Initialize(uintptr_t baseAddress) {
        if (g_initialized) {
            LOG_WARNING("[Limits] Already initialized");
            return true;
        }

        if (baseAddress == 0) {
            LOG_ERROR("[Limits] Invalid base address");
            return false;
        }

        g_baseAddress = baseAddress;
        g_initialized = true;

        // Log version detection
        if (BaseAddress::IsSteamVersion()) {
            LOG_INFO("[Limits] Steam version detected - using Steam addresses");
        } else {
            LOG_INFO("[Limits] Uplay version detected - using Uplay addresses");
        }

        return true;
    }

    void Shutdown() {
        if (!g_initialized) {
            return;
        }

        g_initialized = false;
        g_baseAddress = 0;

        LOG_VERBOSE("[Limits] Shutdown complete");
    }

    // ============================================================================
    // Limit Value Modification Functions
    // ============================================================================

    bool SetFaultLimit(uint32_t newLimit) {
        if (!g_initialized) {
            LOG_ERROR("[Limits] Not initialized");
            return false;
        }

        uintptr_t patchAddr = g_baseAddress + GetFaultLimitCmpRVA();

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

        uintptr_t patchAddr = g_baseAddress + GetFaultLimitCmpRVA();
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

        uintptr_t patchAddr = g_baseAddress + GetTimeLimitCmpRVA();

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

        uintptr_t patchAddr = g_baseAddress + GetTimeLimitCmpRVA();
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

    // ============================================================================
    // Completion Condition Check Functions
    // ============================================================================

    static bool DisableCompletionConditionCheck() {
        if (!g_initialized) {
            LOG_ERROR("[CompletionFix] Not initialized");
            return false;
        }

        uintptr_t jncAddr = g_baseAddress + GetCompletionCondFaultJncRVA();

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

    static bool EnableCompletionConditionCheck() {
        if (!g_initialized) {
            LOG_ERROR("[CompletionRestore] Not initialized");
            return false;
        }

        uintptr_t jncAddr = g_baseAddress + GetCompletionCondFaultJncRVA();

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

    static bool DisableTimeCompletionCheck() {
        if (!g_initialized) {
            LOG_ERROR("[TimeFix] Not initialized");
            return false;
        }

        uintptr_t jncAddr = g_baseAddress + GetCompletionCondTimeJncRVA();

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

    static bool EnableTimeCompletionCheck() {
        if (!g_initialized) {
            LOG_ERROR("[TimeRestore] Not initialized");
            return false;
        }

        uintptr_t jncAddr = g_baseAddress + GetCompletionCondTimeJncRVA();

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

    bool DisableTimeCompletionCheck2() {
        if (!g_initialized) {
            LOG_ERROR("[TimeFix2] Not initialized");
            return false;
        }

        uintptr_t jcAddr = g_baseAddress + GetCompletionCondTime2JcRVA();

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

    bool EnableTimeCompletionCheck2() {
        if (!g_initialized) {
            LOG_ERROR("[TimeRestore2] Not initialized");
            return false;
        }

        uintptr_t jcAddr = g_baseAddress + GetCompletionCondTime2JcRVA();

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

    // ============================================================================
    // Race End State Functions
    // ============================================================================

    bool ForceRaceEndSuccess() {
        if (!g_initialized) {
            LOG_ERROR("[FinalFix] Not initialized");
            return false;
        }

        uintptr_t pushAddr = g_baseAddress + GetUpdateRaceEndPushRVA();

        LOG_VERBOSE("[FinalFix] Patching UpdateRaceEndState at 0x" << std::hex << pushAddr);

        if (IsBadReadPtr((void*)pushAddr, 9)) {
            LOG_ERROR("[FinalFix] Cannot read patch address");
            return false;
        }

        uint8_t* bytes = reinterpret_cast<uint8_t*>(pushAddr);

        if (bytes[0] == 0x31 && bytes[1] == 0xC0) {
            LOG_VERBOSE("[FinalFix] Already patched (found XOR EAX,EAX)");
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

        // Instead of testing EAX and conditionally skipping, just always push 0 (success)
        bytes[0] = 0x31;  // XOR
        bytes[1] = 0xC0;  // EAX, EAX (sets EAX to 0)
        bytes[2] = 0x50;  // PUSH EAX (push 0 = success)
        bytes[3] = 0x6A;  // PUSH
        bytes[4] = 0x01;  // 0x1
        bytes[5] = 0x8B;  // MOV
        bytes[6] = 0xCF;  // ECX, EDI
        bytes[7] = 0x90;  // NOP
        bytes[8] = 0x90;  // NOP

        VirtualProtect((void*)pushAddr, 9, oldProtect, &oldProtect);

        LOG_VERBOSE("[LimitBypass] SUCCESS! Will skip finish message if limits exceeded");
        return true;
    }

    bool RestoreRaceEndSuccess() {
        if (!g_initialized) {
            LOG_ERROR("[FinalRestore] Not initialized");
            return false;
        }

        uintptr_t pushAddr = g_baseAddress + GetUpdateRaceEndPushRVA();

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

        // Check if it's patched (has XOR EAX,EAX)
        if (bytes[0] != 0x31 || bytes[1] != 0xC0) {
            LOG_ERROR("[FinalRestore] Unexpected bytes: 0x" << std::hex << (int)bytes[0] << " " << (int)bytes[1]);
            return false;
        }

        DWORD oldProtect;
        if (!VirtualProtect((void*)pushAddr, 10, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            LOG_ERROR("[FinalRestore] Failed to change memory protection");
            return false;
        }

        // Restore original bytes:
        // PUSH EAX           (50)
        // PUSH 0x1           (6A 01)
        // MOV ECX,EDI        (8B CF)
        // CALL <target>      (E8 xx xx xx xx)
        //
        // The CALL target is at InitializeObjectStruct
        // We need to calculate the relative offset at runtime
        
        uintptr_t callInstrAddr = pushAddr + 5;  // Address of the CALL instruction
        uintptr_t callTarget = g_baseAddress + GetInitObjectStructRVA();
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

    // ============================================================================
    // Race Update Timer Freeze Functions
    // ============================================================================

    bool DisableRaceUpdateTimerFreeze() {
        if (!g_initialized) {
            LOG_ERROR("[TimerFreezeFix] Not initialized");
            return false;
        }

        uintptr_t jncAddr = g_baseAddress + GetRaceUpdateTimerCheckRVA() + 6;

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

    bool EnableRaceUpdateTimerFreeze() {
        if (!g_initialized) {
            LOG_ERROR("[TimerFreezeRestore] Not initialized");
            return false;
        }

        uintptr_t jncAddr = g_baseAddress + GetRaceUpdateTimerCheckRVA() + 6;

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

    // ============================================================================
    // Multiplayer Limit Check Functions
    // ============================================================================

    bool DisableMultiplayerTimeChecks() {
        if (!g_initialized) {
            LOG_ERROR("[MPTimeFix] Not initialized");
            return false;
        }

        bool success = true;

        // Patch multiplayer mode 1 time check
        uintptr_t mp1JncAddr = g_baseAddress + GetMultiplayerMode1TimeJncRVA();
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
        uintptr_t mp2JncAddr = g_baseAddress + GetMultiplayerMode2TimeJncRVA();
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

    bool EnableMultiplayerTimeChecks() {
        if (!g_initialized) {
            LOG_ERROR("[MPTimeRestore] Not initialized");
            return false;
        }

        bool success = true;

        // Restore multiplayer mode 1 time check
        uintptr_t mp1JncAddr = g_baseAddress + GetMultiplayerMode1TimeJncRVA();
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
        uintptr_t mp2JncAddr = g_baseAddress + GetMultiplayerMode2TimeJncRVA();
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

    bool DisableMultiplayerFaultChecks() {
        if (!g_initialized) {
            LOG_ERROR("[MPFaultFix] Not initialized");
            return false;
        }

        bool success = true;

        // Patch multiplayer mode 1 fault check (NOP the JNZ that respawns on fault >= 500)
        uintptr_t mp1JnzAddr = g_baseAddress + GetMultiplayerMode1FaultJnzRVA();
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
        uintptr_t mp2JnzAddr = g_baseAddress + GetMultiplayerMode2FaultJnzRVA();
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

    bool EnableMultiplayerFaultChecks() {
        if (!g_initialized) {
            LOG_ERROR("[MPFaultRestore] Not initialized");
            return false;
        }

        bool success = true;

        // Restore multiplayer mode 1 fault check
        uintptr_t mp1JnzAddr = g_baseAddress + GetMultiplayerMode1FaultJnzRVA();
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
        uintptr_t mp2JnzAddr = g_baseAddress + GetMultiplayerMode2FaultJnzRVA();
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

    // ============================================================================
    // Finish Message Fault Check Bypass (patches CheckTimeAndFaultLimits)
    // ============================================================================

    bool DisableFinishFaultCheck() {
        if (!g_initialized) {
            LOG_ERROR("[FinishFaultCheck] Not initialized");
            return false;
        }

        uintptr_t jcAddr = g_baseAddress + GetFinishFaultCheckJcRVA();

        LOG_VERBOSE("[FinishFaultCheck] Patching finish fault check at 0x" << std::hex << jcAddr);

        if (IsBadReadPtr((void*)jcAddr, 2)) {
            LOG_ERROR("[FinishFaultCheck] Cannot read JC address");
            return false;
        }

        uint8_t* bytes = reinterpret_cast<uint8_t*>(jcAddr);

        // Check if already patched (JMP instead of JC)
        if (bytes[0] == 0xEB) {
            LOG_VERBOSE("[FinishFaultCheck] Already patched (found JMP)");
            return true;
        }

        // Verify it's a JC instruction (0x72)
        if (bytes[0] != 0x72) {
            LOG_ERROR("[FinishFaultCheck] Expected JC (0x72), found 0x" << std::hex << (int)bytes[0]);
            return false;
        }

        DWORD oldProtect;
        if (!VirtualProtect((void*)jcAddr, 2, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            LOG_ERROR("[FinishFaultCheck] Failed to change memory protection");
            return false;
        }

        // Change JC to JMP (keep same offset)
        bytes[0] = 0xEB;  // JMP short (was JC)
        // bytes[1] stays the same - the jump offset

        VirtualProtect((void*)jcAddr, 2, oldProtect, &oldProtect);

        LOG_INFO("[FinishFaultCheck] SUCCESS! Finish fault check bypassed - will always show success on finish");
        return true;
    }

    bool EnableFinishFaultCheck() {
        if (!g_initialized) {
            LOG_ERROR("[FinishFaultCheck] Not initialized");
            return false;
        }

        uintptr_t jcAddr = g_baseAddress + GetFinishFaultCheckJcRVA();

        LOG_VERBOSE("[FinishFaultCheck] Restoring finish fault check at 0x" << std::hex << jcAddr);

        if (IsBadReadPtr((void*)jcAddr, 2)) {
            LOG_ERROR("[FinishFaultCheck] Cannot read JC address");
            return false;
        }

        uint8_t* bytes = reinterpret_cast<uint8_t*>(jcAddr);

        // Check if already restored (JC instead of JMP)
        if (bytes[0] == 0x72) {
            LOG_VERBOSE("[FinishFaultCheck] Already restored (found JC)");
            return true;
        }

        // Verify it's a JMP instruction (0xEB)
        if (bytes[0] != 0xEB) {
            LOG_ERROR("[FinishFaultCheck] Unexpected byte 0x" << std::hex << (int)bytes[0]);
            return false;
        }

        DWORD oldProtect;
        if (!VirtualProtect((void*)jcAddr, 2, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            LOG_ERROR("[FinishFaultCheck] Failed to change memory protection");
            return false;
        }

        // Change JMP back to JC
        bytes[0] = 0x72;  // JC short (was JMP)
        // bytes[1] stays the same - the jump offset

        VirtualProtect((void*)jcAddr, 2, oldProtect, &oldProtect);

        LOG_INFO("[FinishFaultCheck] SUCCESS! Finish fault check restored - will show fault message if >= 500 faults");
        return true;
    }

    bool IsFinishFaultCheckDisabled() {
        if (!g_initialized) {
            return false;
        }

        uintptr_t jcAddr = g_baseAddress + GetFinishFaultCheckJcRVA();

        if (IsBadReadPtr((void*)jcAddr, 2)) {
            return false;
        }

        uint8_t* bytes = reinterpret_cast<uint8_t*>(jcAddr);

        // If it's JMP (0xEB), the finish fault check is disabled
        return (bytes[0] == 0xEB);
    }

    // ============================================================================
    // Finish Threshold Check Bypass (patches CheckAnyTimerThresholdExceeded branch)
    // ============================================================================

    bool DisableFinishThresholdCheck() {
        if (!g_initialized) {
            LOG_ERROR("[FinishThreshold] Not initialized");
            return false;
        }

        uintptr_t jnzAddr = g_baseAddress + GetFinishThresholdCheckJnzRVA();

        LOG_VERBOSE("[FinishThreshold] Patching threshold check at 0x" << std::hex << jnzAddr);

        if (IsBadReadPtr((void*)jnzAddr, 2)) {
            LOG_ERROR("[FinishThreshold] Cannot read JNZ address");
            return false;
        }

        uint8_t* bytes = reinterpret_cast<uint8_t*>(jnzAddr);

        // Check if already patched (NOPs)
        if (bytes[0] == 0x90 && bytes[1] == 0x90) {
            LOG_VERBOSE("[FinishThreshold] Already patched (found NOPs)");
            return true;
        }

        // Verify it's a JNZ instruction (0x75 for short JNZ)
        if (bytes[0] != 0x75) {
            LOG_ERROR("[FinishThreshold] Expected JNZ (0x75), found 0x" << std::hex << (int)bytes[0]);
            return false;
        }

        DWORD oldProtect;
        if (!VirtualProtect((void*)jnzAddr, 2, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            LOG_ERROR("[FinishThreshold] Failed to change memory protection");
            return false;
        }

        // NOP out the JNZ so it always falls through to load success
        bytes[0] = 0x90;
        bytes[1] = 0x90;

        VirtualProtect((void*)jnzAddr, 2, oldProtect, &oldProtect);

        LOG_INFO("[FinishThreshold] SUCCESS! Threshold check bypassed - always returns success");
        return true;
    }

    bool EnableFinishThresholdCheck() {
        if (!g_initialized) {
            LOG_ERROR("[FinishThreshold] Not initialized");
            return false;
        }

        uintptr_t jnzAddr = g_baseAddress + GetFinishThresholdCheckJnzRVA();

        LOG_VERBOSE("[FinishThreshold] Restoring threshold check at 0x" << std::hex << jnzAddr);

        if (IsBadReadPtr((void*)jnzAddr, 2)) {
            LOG_ERROR("[FinishThreshold] Cannot read JNZ address");
            return false;
        }

        uint8_t* bytes = reinterpret_cast<uint8_t*>(jnzAddr);

        // Check if already restored (JNZ)
        if (bytes[0] == 0x75) {
            LOG_VERBOSE("[FinishThreshold] Already restored (found JNZ)");
            return true;
        }

        // Verify it's NOPed
        if (bytes[0] != 0x90) {
            LOG_ERROR("[FinishThreshold] Unexpected byte 0x" << std::hex << (int)bytes[0]);
            return false;
        }

        DWORD oldProtect;
        if (!VirtualProtect((void*)jnzAddr, 2, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            LOG_ERROR("[FinishThreshold] Failed to change memory protection");
            return false;
        }

        // Restore JNZ
        bytes[0] = 0x75;  // JNZ short
        bytes[1] = 0x03;  // Jump offset (+3 bytes to 0x976e78)

        VirtualProtect((void*)jnzAddr, 2, oldProtect, &oldProtect);

        LOG_INFO("[FinishThreshold] SUCCESS! Threshold check restored");
        return true;
    }

    bool IsFinishThresholdCheckDisabled() {
        if (!g_initialized) {
            return false;
        }

        uintptr_t jnzAddr = g_baseAddress + GetFinishThresholdCheckJnzRVA();

        if (IsBadReadPtr((void*)jnzAddr, 2)) {
            return false;
        }

        uint8_t* bytes = reinterpret_cast<uint8_t*>(jnzAddr);

        // If it's NOPed (0x90), the threshold check is disabled
        return (bytes[0] == 0x90);
    }

    // ============================================================================
    // Fault/Time Validation State Query Functions
    // ============================================================================

    bool IsFaultValidationDisabled() {
        if (!g_initialized) {
            return false;
        }

        // Check if the fault limit check is patched (NOPed out)
        uintptr_t jncAddr = g_baseAddress + GetCompletionCondFaultJncRVA();

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
        uintptr_t jncAddr = g_baseAddress + GetCompletionCondTimeJncRVA();

        if (IsBadReadPtr((void*)jncAddr, 2)) {
            return false;
        }

        uint8_t* bytes = reinterpret_cast<uint8_t*>(jncAddr);

        // If first two bytes are NOPs, the time validation is disabled
        return (bytes[0] == 0x90 && bytes[1] == 0x90);
    }

    // ============================================================================
    // Combined Fault/Time Validation Functions
    // ============================================================================

    bool DisableFaultValidation() {
        bool success = true;
        success &= DisableCompletionConditionCheck();
        success &= DisableFinishFaultCheck();
        return success;
    }

    bool EnableFaultValidation() {
        bool success = true;
        success &= EnableCompletionConditionCheck();
        success &= EnableFinishFaultCheck();
        return success;
    }

    bool DisableTimeValidation() {
        bool success = true;
        success &= DisableTimeCompletionCheck();
        success &= DisableTimeCompletionCheck2();
        return success;
    }

    bool EnableTimeValidation() {
        bool success = true;
        success &= EnableTimeCompletionCheck();
        success &= EnableTimeCompletionCheck2();
        return success;
    }

    // ============================================================================
    // Instant Limit Trigger Functions
    // ============================================================================

    bool InstantTimeOut() {
        LOG_INFO("[InstantTimeOut] === TRIGGERING INSTANT TIMEOUT ===");

        // Check and log current state
        LOG_INFO("[InstantTimeOut] Current validation states:");
        LOG_INFO("[InstantTimeOut]   - Time validation disabled: " << (IsTimeValidationDisabled() ? "YES" : "NO"));
        LOG_INFO("[InstantTimeOut]   - Finish threshold check disabled: " << (IsFinishThresholdCheckDisabled() ? "YES" : "NO"));

        // CRITICAL: Restore the time limit value to 108000 (0x1a5e0) if it was changed
        // This is necessary because DisableTimeLimit() sets it to 0x7FFFFFFF
        // The game compares time against this patched value, so if it's huge, timeout never triggers
        uint32_t currentLimit = GetTimeLimit();
        if (currentLimit != DEFAULT_TIME_LIMIT) {
            LOG_INFO("[InstantTimeOut] Time limit is " << currentLimit << " (0x" << std::hex << currentLimit << std::dec << "), restoring to " << DEFAULT_TIME_LIMIT);
            SetTimeLimit(DEFAULT_TIME_LIMIT);
        }

        // Re-enable time validation if it's disabled
        if (IsTimeValidationDisabled()) {
            LOG_INFO("[InstantTimeOut] Time validation is DISABLED - re-enabling it");
            EnableTimeValidation();
        }

        // CRITICAL: Also re-enable the finish threshold check (handles both time AND fault)
        // This is the check that determines whether to show timeout/faultout vs success
        if (IsFinishThresholdCheckDisabled()) {
            LOG_INFO("[InstantTimeOut] Finish threshold check is DISABLED - re-enabling it");
            EnableFinishThresholdCheck();
        }

        // Verify the checks are now enabled
        LOG_INFO("[InstantTimeOut] After re-enabling:");
        LOG_INFO("[InstantTimeOut]   - Time validation disabled: " << (IsTimeValidationDisabled() ? "YES" : "NO"));
        LOG_INFO("[InstantTimeOut]   - Finish threshold check disabled: " << (IsFinishThresholdCheckDisabled() ? "YES" : "NO"));

        // Get current time
        int currentTime = Respawn::GetRaceTimeMs();
        LOG_INFO("[InstantTimeOut] Current time: " << std::dec << currentTime << " frames");

        // Get time limit (should now be 108000)
        uint32_t timeLimit = GetTimeLimit();
        LOG_INFO("[InstantTimeOut] Time limit: " << std::dec << timeLimit << " frames (0x" << std::hex << timeLimit << std::dec << ")");

        // Set time to 108001 frames (limit is 108000 frames = 30 minutes at 60fps)
        const int TIMEOUT_COUNT = 108001;
        if (!Respawn::SetRaceTimeMs(TIMEOUT_COUNT)) {
            LOG_ERROR("[InstantTimeOut] Failed to set time to " << TIMEOUT_COUNT << " frames");
            return false;
        }

        LOG_INFO("[InstantTimeOut] Time set to " << TIMEOUT_COUNT << " frames (limit is " << timeLimit << ")");
        LOG_INFO("[InstantTimeOut] Time exceeds limit by " << (TIMEOUT_COUNT - timeLimit) << " frames");
        LOG_INFO("[InstantTimeOut] Timeout should trigger on next frame/finish");
        LOG_INFO("[InstantTimeOut] === END INSTANT TIMEOUT ===");
        return true;
    }

    bool InstantFaultOut() {
        LOG_INFO("[InstantFaultOut] === TRIGGERING INSTANT FAULTOUT ===");

        // Check and log current state
        LOG_INFO("[InstantFaultOut] Current validation states:");
        LOG_INFO("[InstantFaultOut]   - Fault validation disabled: " << (IsFaultValidationDisabled() ? "YES" : "NO"));
        LOG_INFO("[InstantFaultOut]   - Finish fault check disabled: " << (IsFinishFaultCheckDisabled() ? "YES" : "NO"));

        // CRITICAL: Restore the fault limit value to 500 (0x1f4) if it was changed
        // This is necessary because DisableFaultLimit() sets it to 0x7FFFFFFF
        uint32_t currentLimit = GetFaultLimit();
        if (currentLimit != DEFAULT_FAULT_LIMIT) {
            LOG_INFO("[InstantFaultOut] Fault limit is " << currentLimit << ", restoring to " << DEFAULT_FAULT_LIMIT);
            SetFaultLimit(DEFAULT_FAULT_LIMIT);
        }

        // Re-enable fault validation if it's disabled
        if (IsFaultValidationDisabled()) {
            LOG_INFO("[InstantFaultOut] Fault validation is DISABLED - re-enabling it");
            EnableFaultValidation();
        }

        // CRITICAL: Also re-enable the finish fault check
        // This is the check that determines whether to show faultout vs success
        if (IsFinishFaultCheckDisabled()) {
            LOG_INFO("[InstantFaultOut] Finish fault check is DISABLED - re-enabling it");
            EnableFinishFaultCheck();
        }

        // Verify the checks are now enabled
        LOG_INFO("[InstantFaultOut] After re-enabling:");
        LOG_INFO("[InstantFaultOut]   - Fault validation disabled: " << (IsFaultValidationDisabled() ? "YES" : "NO"));
        LOG_INFO("[InstantFaultOut]   - Finish fault check disabled: " << (IsFinishFaultCheckDisabled() ? "YES" : "NO"));

        // Get current faults
        int currentFaults = Respawn::GetFaultCount();
        LOG_INFO("[InstantFaultOut] Current faults: " << std::dec << currentFaults);

        // Get fault limit (should now be 500)
        uint32_t faultLimit = GetFaultLimit();
        LOG_INFO("[InstantFaultOut] Fault limit: " << std::dec << faultLimit);

        // Set faults to 501 (limit is 500)
        const int FAULTOUT_COUNT = 501;
        if (!Respawn::SetFaultCounterValue(FAULTOUT_COUNT)) {
            LOG_ERROR("[InstantFaultOut] Failed to set fault count to " << FAULTOUT_COUNT);
            return false;
        }

        LOG_INFO("[InstantFaultOut] Faults set to " << FAULTOUT_COUNT << " (limit is " << faultLimit << ")");
        LOG_INFO("[InstantFaultOut] Faults exceed limit by " << (FAULTOUT_COUNT - faultLimit));
        LOG_INFO("[InstantFaultOut] Faultout should trigger on next frame/finish");
        LOG_INFO("[InstantFaultOut] === END INSTANT FAULTOUT ===");
        return true;
    }

    // ============================================================================
    // Master Limit Control Functions
    // ============================================================================

    bool DisableAllLimitValidation() {
        LOG_INFO("[LimitBypass] Disable Fault (500) + Time (30Min) Limits ");

        // CRITICAL: Disable prevent-finish hook so it doesn't convert finish messages to faultout
        PreventFinish::Disable();

        // Set limits to maximum values so the game thinks limits are never exceeded
        bool faultLimitSuccess = DisableFaultLimit();
        bool timeLimitSuccess = DisableTimeLimit();

        bool faultConditionSuccess = DisableCompletionConditionCheck();
        bool timeConditionSuccess = DisableTimeCompletionCheck();
        bool timeCondition2Success = DisableTimeCompletionCheck2();
        bool timerFreezeSuccess = DisableRaceUpdateTimerFreeze();
        bool multiplayerTimeSuccess = DisableMultiplayerTimeChecks();
        bool multiplayerFaultSuccess = DisableMultiplayerFaultChecks();
        // CRITICAL: DisableFinishFaultCheck() prevents the initial "too many faults" message
        // when crossing the finish line with 500+ faults (direct fault count check)
        bool finishFaultCheckSuccess = DisableFinishFaultCheck();
        // CRITICAL: DisableFinishThresholdCheck() prevents the SECOND "too many faults" message
        // from CheckAnyTimerThresholdExceeded path
        bool finishThresholdSuccess = DisableFinishThresholdCheck();

        if (faultConditionSuccess && timeConditionSuccess && timeCondition2Success &&
            timerFreezeSuccess && multiplayerTimeSuccess &&
            multiplayerFaultSuccess && finishFaultCheckSuccess && finishThresholdSuccess) {
            LOG_VERBOSE("[LimitBypass] SUCCESS! All patches applied:");
            LOG_VERBOSE("[LimitBypass]   1. Fault limit check disabled (SP)");
            LOG_VERBOSE("[LimitBypass]   2. Time limit check #1 disabled (SP)");
            LOG_VERBOSE("[LimitBypass]   3. Time limit check #2 disabled (SP)");
            LOG_VERBOSE("[LimitBypass]   4. Timer freeze DISABLED");
            LOG_VERBOSE("[LimitBypass]   5. Multiplayer time checks DISABLED");
            LOG_VERBOSE("[LimitBypass]   6. Multiplayer fault checks DISABLED");
            LOG_VERBOSE("[LimitBypass]   7. Finish fault check BYPASSED");
            LOG_VERBOSE("[LimitBypass]   8. Finish threshold check BYPASSED");
            LOG_VERBOSE("[LimitBypass] You can now play indefinitely in SP/MP!");
            LOG_VERBOSE("[LimitBypass] Note: MP may crash on track 2 finish with exceeded limits");
            return true;
        }
        else {
            LOG_ERROR("[LimitBypass] Patch FAILED");
            LOG_ERROR("[LimitBypass]   Fault (SP): " << (faultConditionSuccess ? "OK" : "FAIL"));
            LOG_ERROR("[LimitBypass]   Time1 (SP): " << (timeConditionSuccess ? "OK" : "FAIL"));
            LOG_ERROR("[LimitBypass]   Time2 (SP): " << (timeCondition2Success ? "OK" : "FAIL"));
            LOG_ERROR("[LimitBypass]   TimerFreeze: " << (timerFreezeSuccess ? "OK" : "FAIL"));
            LOG_ERROR("[LimitBypass]   MP Time: " << (multiplayerTimeSuccess ? "OK" : "FAIL"));
            LOG_ERROR("[LimitBypass]   MP Fault: " << (multiplayerFaultSuccess ? "OK" : "FAIL"));
            LOG_ERROR("[LimitBypass]   FinishFault: " << (finishFaultCheckSuccess ? "OK" : "FAIL"));
            LOG_ERROR("[LimitBypass]   FinishThreshold: " << (finishThresholdSuccess ? "OK" : "FAIL"));
            return false;
        }
    }

    bool EnableAllLimitValidation() {
        LOG_INFO("[LimitRestore] Enable Fault (500) + Time (30Min) Limits ");

        // Restore default limit values
        bool faultLimitSuccess = RestoreDefaultLimits(); // This sets both fault and time back to defaults

        bool faultConditionSuccess = EnableCompletionConditionCheck();
        bool timeConditionSuccess = EnableTimeCompletionCheck();
        bool timeCondition2Success = EnableTimeCompletionCheck2();
        bool timerFreezeSuccess = EnableRaceUpdateTimerFreeze();
        bool multiplayerTimeSuccess = EnableMultiplayerTimeChecks();
        bool multiplayerFaultSuccess = EnableMultiplayerFaultChecks();
        bool finishFaultCheckSuccess = EnableFinishFaultCheck();
        bool finishThresholdSuccess = EnableFinishThresholdCheck();

        if (faultConditionSuccess && timeConditionSuccess && timeCondition2Success &&
            timerFreezeSuccess && multiplayerTimeSuccess &&
            multiplayerFaultSuccess && finishFaultCheckSuccess && finishThresholdSuccess) {
            LOG_VERBOSE("[LimitRestore] SUCCESS! All validations restored:");
            LOG_VERBOSE("[LimitRestore]   1. Fault limit check enabled (SP)");
            LOG_VERBOSE("[LimitRestore]   2. Time limit check #1 enabled (SP)");
            LOG_VERBOSE("[LimitRestore]   3. Time limit check #2 enabled (SP)");
            LOG_VERBOSE("[LimitRestore]   4. Timer freeze ENABLED");
            LOG_VERBOSE("[LimitRestore]   5. Multiplayer time checks ENABLED");
            LOG_VERBOSE("[LimitRestore]   6. Multiplayer fault checks ENABLED");
            LOG_VERBOSE("[LimitRestore]   7. Finish fault check RESTORED");
            LOG_VERBOSE("[LimitRestore]   8. Finish threshold check RESTORED");
            LOG_VERBOSE("[LimitRestore] Limits are now being enforced normally!");
            return true;
        }
        else {
            LOG_ERROR("[LimitRestore] Restore FAILED");
            LOG_ERROR("[LimitRestore]   Fault (SP): " << (faultConditionSuccess ? "OK" : "FAIL"));
            LOG_ERROR("[LimitRestore]   Time1 (SP): " << (timeConditionSuccess ? "OK" : "FAIL"));
            LOG_ERROR("[LimitRestore]   Time2 (SP): " << (timeCondition2Success ? "OK" : "FAIL"));
            LOG_ERROR("[LimitRestore]   TimerFreeze: " << (timerFreezeSuccess ? "OK" : "FAIL"));
            LOG_ERROR("[LimitRestore]   MP Time: " << (multiplayerTimeSuccess ? "OK" : "FAIL"));
            LOG_ERROR("[LimitRestore]   MP Fault: " << (multiplayerFaultSuccess ? "OK" : "FAIL"));
            LOG_ERROR("[LimitRestore]   FinishFault: " << (finishFaultCheckSuccess ? "OK" : "FAIL"));
            LOG_ERROR("[LimitRestore]   FinishThreshold: " << (finishThresholdSuccess ? "OK" : "FAIL"));
            return false;
        }
    }
}
