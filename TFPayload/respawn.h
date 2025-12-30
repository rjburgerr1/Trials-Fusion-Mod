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

    // Check for hotkeys (Q = current, E = next, W = previous)
    void CheckHotkey();

    // Get the current bike/rider pointer
    void* GetBikePointer();
}
