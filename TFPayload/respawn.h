#pragma once
#include <cstdint>

namespace Respawn {
    // =============================================================================
    // PUBLIC API
    // =============================================================================
    
    // Initialize the respawn system with the game's base address
    bool Initialize(uintptr_t baseAddress);

    // Shutdown and cleanup
    void Shutdown();

    // Respawn the rider/bike at the current checkpoint
    bool RespawnAtCheckpoint();

    // Respawn at the next checkpoint
    bool RespawnAtNextCheckpoint();

    // Respawn at the previous checkpoint
    bool RespawnAtPreviousCheckpoint();

    // Respawn at a specific checkpoint by index (0-based)
    bool RespawnAtCheckpointIndex(int index);

    // Get the total number of checkpoints in the current track
    int GetCheckpointCount();

    // Get the current checkpoint index
    int GetCurrentCheckpointIndex();

    // Check for hotkeys (Q = current, E = next, W = previous, G = increment fault)
    void CheckHotkey();

    // Get the current bike/rider pointer
    void* GetBikePointer();

    // ============================================================================
    // Encrypted Fault Counter Functions
    // ============================================================================

    bool IncrementFaultCounter();
    bool IncrementFaultCounterBy(int amount);
    int GetFaultCount();
    bool SetFaultCounterValue(int value);
    void DebugFaultCounterPath();

    // ============================================================================
    // Encrypted Time Counter Functions
    // ============================================================================

    int GetRaceTimeMs();
    bool SetRaceTimeMs(int timeMs);
    bool AdjustRaceTimeMs(int deltaMs);
    void DebugTimeCounter();

    // ============================================================================
    // Limit Modification Functions
    // ============================================================================

    bool SetFaultLimit(uint32_t newLimit);
    uint32_t GetFaultLimit();
    bool SetTimeLimit(uint32_t newLimit);
    uint32_t GetTimeLimit();
    bool DisableFaultLimit();
    bool DisableTimeLimit();
    void TestLimitPatching();

    // ============================================================================
    // Limit Bypass Functions
    // ============================================================================

    bool DisableFaultValidation();
    bool DisableTimeValidation();
    bool EnableFaultValidation();
    bool EnableTimeValidation();
    bool DisableAllLimitValidation();
    bool EnableAllLimitValidation();
    bool DisableRaceUpdateTimerFreeze();
    bool DisableTimeCompletionCheck2();
    bool IsFaultValidationDisabled();
    bool IsTimeValidationDisabled();

    // ============================================================================
    // Instant Finish Functions
    // ============================================================================

    bool InstantTimeOut();
    bool InstantFaultOut();
    bool InstantFinish();

    // ============================================================================
    // Checkpoint Enable/Disable Functions
    // ============================================================================

    bool IsCheckpointEnabled(int index);
    bool EnableCheckpoint(int index);
    bool DisableCheckpoint(int index);
    bool ToggleCheckpoint(int index);
    void* GetCheckpointPointer(int index);
    void DebugCheckpointStructure(int index);

    // ============================================================================
    // Finish Line Functions
    // ============================================================================

    bool IsFinishLineEnabled();
    bool EnableFinishLine();
    bool DisableFinishLine();
    bool ToggleFinishLine();
    bool PatchFinishLineCheck();
    bool UnpatchFinishLineCheck();
    bool PatchCheckpointEarlyReturn();
    bool UnpatchCheckpointEarlyReturn();
    bool PatchUpdateCheckpointsCall();
    bool UnpatchUpdateCheckpointsCall();
}
