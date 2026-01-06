#pragma once
#include <cstdint>

namespace Respawn {
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
    // The game encrypts the fault counter using XOR encryption at bike+0x898
    // These functions use the game's own encryption/decryption routines
    // ============================================================================

    // Increment the fault counter by 1 (uses game's encryption)
    bool IncrementFaultCounter();

    // Increment the fault counter by a specific amount (can be negative to decrease)
    bool IncrementFaultCounterBy(int amount);

    // Get the current fault count (decrypted)
    int GetFaultCount();

    // Set the fault counter to an exact value
    bool SetFaultCounterValue(int value);

    // Debug: dump fault counter information
    void DebugFaultCounterPath();

    // ============================================================================
    // Encrypted Time Counter Functions
    // The game encrypts the race timer using XOR encryption at *(globalStruct+0x158)+0xd0
    // Time values are in milliseconds
    // ============================================================================

    // Get the current race time in milliseconds (decrypted)
    int GetRaceTimeMs();

    // Set the race time in milliseconds
    bool SetRaceTimeMs(int timeMs);

    // Add/subtract time in milliseconds (positive = add, negative = subtract)
    bool AdjustRaceTimeMs(int deltaMs);

    // Debug: dump time counter information
    void DebugTimeCounter();

    // ============================================================================
    // Limit Modification Functions
    // These patch the game's executable to change fault/time limits
    // ============================================================================

    // Set the fault limit (default is 500)
    bool SetFaultLimit(uint32_t newLimit);

    // Get the current fault limit
    uint32_t GetFaultLimit();

    // Set the time limit (default is 0x1A5E0 which represents 30 minutes)
    bool SetTimeLimit(uint32_t newLimit);

    // Get the current time limit
    uint32_t GetTimeLimit();

    // Disable fault limit entirely (set to max value)
    bool DisableFaultLimit();

    // Disable time limit entirely (set to max value)
    bool DisableTimeLimit();

    // Test if limit patching is working (writes test values and restores)
    void TestLimitPatching();

    // ============================================================================
    // Limit Bypass Functions (based on Ghidra disassembly)
    // These completely disable limit validation by changing JC to JMP
    // ============================================================================

    // Disable fault limit validation (change JC to JMP)
    bool DisableFaultValidation();

    // Disable time limit validation (change JC to JMP)
    bool DisableTimeValidation();

    // Re-enable validations (change JMP back to JC)
    bool EnableFaultValidation();
    bool EnableTimeValidation();

    // Master function to disable all limit validation
    bool DisableAllLimitValidation();

    // Master function to restore all limit validation
    bool EnableAllLimitValidation();

    // Additional individual validation control functions (used by toggle buttons)
    bool DisableRaceUpdateTimerFreeze();
    bool DisableTimeCompletionCheck2();

    // Check if fault limit validation is disabled
    bool IsFaultValidationDisabled();

    // Check if time limit validation is disabled
    bool IsTimeValidationDisabled();

    // ============================================================================
    // Instant Finish Functions
    // These trigger instant race completion with specific finish types
    // ============================================================================

    // Trigger instant time out (finish with timeout condition)
    bool InstantTimeOut();

    // Trigger instant fault out (finish with fault limit condition)
    bool InstantFaultOut();

    // Trigger instant normal finish (finish without limit exceeded)
    bool InstantFinish();

    // ============================================================================
    // Checkpoint Enable/Disable Functions
    // These allow enabling/disabling individual checkpoints so they won't trigger
    // The game checks bit 17 at trigger_object+0xC to determine if checkpoint is active
    // ============================================================================

    // Check if a checkpoint at the given index is enabled
    bool IsCheckpointEnabled(int index);

    // Enable a checkpoint at the given index (allows it to trigger)
    bool EnableCheckpoint(int index);

    // Disable a checkpoint at the given index (prevents it from triggering)
    bool DisableCheckpoint(int index);

    // Toggle a checkpoint's enabled state at the given index
    bool ToggleCheckpoint(int index);

    // Get the checkpoint pointer at a given index (for advanced use)
    void* GetCheckpointPointer(int index);

    // Debug: dump checkpoint structure information
    void DebugCheckpointStructure(int index);

    // ============================================================================
    // Finish Line (Last Checkpoint) Enable/Disable Functions
    // The finish line is the last checkpoint in the list. Disabling it prevents
    // the race from ending when you cross the finish line.
    // ============================================================================

    // Check if the finish line (last checkpoint) is enabled
    bool IsFinishLineEnabled();

    // Enable the finish line (allows race to end normally)
    bool EnableFinishLine();

    // Disable the finish line (prevents race from ending when crossed)
    bool DisableFinishLine();

    // Toggle the finish line enabled state
    bool ToggleFinishLine();

    // Internal functions for patching finish line check (used by Enable/DisableFinishLine)
    bool PatchFinishLineCheck();
    bool UnpatchFinishLineCheck();
    bool PatchCheckpointEarlyReturn();
    bool UnpatchCheckpointEarlyReturn();
    bool PatchUpdateCheckpointsCall();
    bool UnpatchUpdateCheckpointsCall();
}
