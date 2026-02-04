#pragma once

namespace PreventFinish {
    void Initialize();

    bool IsEnabled();
    void Enable();
    void Disable();
    void Toggle();

    // Call this every frame/tick to monitor game state
    void Update();

    // Safe instant finish that respects prevent-finish setting
    void SafeInstantFinish();

    // Get the current status string for UI display
    const char* GetStatusString();

    // Notify system that a checkpoint skip action occurred
    void NotifyCheckpointSkip();

    // Notify system that fault count was manually reduced
    void NotifyFaultReduction();

    // Notify system that time was manually reduced
    void NotifyTimeReduction();

    // Notify system that a bike value was modified
    void NotifyBikeModification();

    // Notify system that a rider value was modified
    void NotifyRiderModification();

    // Check if bike/rider values have been modified from defaults
    // Returns true if ANY bike or rider value differs from default
    bool CheckForModifiedValues();
    
    // Reset all bike/rider values to defaults and re-enable finishing
    // This is called by the on-screen reset button
    void ResetToAllowFinish();
}
