#pragma once
#include <cstdint>

namespace Limits {
    // =============================================================================
    // PUBLIC API - Limit Validation Control
    // =============================================================================
    
    // Initialize the limits system with the game's base address
    bool Initialize(uintptr_t baseAddress);

    // Shutdown and cleanup
    void Shutdown();

    // ============================================================================
    // Limit Value Modification Functions
    // ============================================================================

    bool SetFaultLimit(uint32_t newLimit);
    uint32_t GetFaultLimit();
    bool SetTimeLimit(uint32_t newLimit);
    uint32_t GetTimeLimit();
    bool DisableFaultLimit();
    bool DisableTimeLimit();
    bool RestoreDefaultLimits();

    // ============================================================================
    // Individual Limit Bypass Functions
    // ============================================================================

    // Fault limit validation (single player completion condition)
    bool DisableFaultValidation();
    bool EnableFaultValidation();
    bool IsFaultValidationDisabled();

    // Time limit validation (single player completion condition)
    bool DisableTimeValidation();
    bool EnableTimeValidation();
    bool IsTimeValidationDisabled();

    // Time limit check #2 (timer freeze control)
    bool DisableTimeCompletionCheck2();
    bool EnableTimeCompletionCheck2();

    // Race update timer freeze (30 minute freeze)
    bool DisableRaceUpdateTimerFreeze();
    bool EnableRaceUpdateTimerFreeze();

    // Multiplayer limit checks
    bool DisableMultiplayerTimeChecks();
    bool EnableMultiplayerTimeChecks();
    bool DisableMultiplayerFaultChecks();
    bool EnableMultiplayerFaultChecks();

    // Finish message fault check bypass (patches CheckTimeAndFaultLimits)
    bool DisableFinishFaultCheck();
    bool EnableFinishFaultCheck();
    bool IsFinishFaultCheckDisabled();

    // Finish threshold check bypass (patches CheckAnyTimerThresholdExceeded branch)
    bool DisableFinishThresholdCheck();
    bool EnableFinishThresholdCheck();
    bool IsFinishThresholdCheckDisabled();

    // Race end state patches (for finish message handling)
    bool ForceRaceEndSuccess();
    bool RestoreRaceEndSuccess();

    // ============================================================================
    // Instant Limit Trigger Functions
    // ============================================================================

    // Trigger instant timeout by setting time past limit and re-enabling validation
    bool InstantTimeOut();

    // Trigger instant faultout by setting faults past limit and re-enabling validation
    bool InstantFaultOut();

    // ============================================================================
    // Master Limit Control Functions
    // ============================================================================

    // Disable ALL limit validation (fault + time + multiplayer + finish checks)
    bool DisableAllLimitValidation();

    // Enable ALL limit validation (restore normal behavior)
    bool EnableAllLimitValidation();
}
