#pragma once
#include <cstdint>

namespace SaveStates {
    bool Initialize(uintptr_t baseAddress);
    void Shutdown();
    void CheckHotkey();

    bool CaptureSlot();
    bool RestoreSlotNativeOnly();
    void DebugDumpSlot();

    // Called from the game's frame-update hook. Hotkeys only enqueue work.
    void ProcessPendingMainThread();
}
