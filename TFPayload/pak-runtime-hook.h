#pragma once

#include <Windows.h>
#include <cstdint>

namespace PakRuntimeHook {
    bool Initialize(uintptr_t baseAddress);
    void Shutdown();
    void CheckHotkey();
    void UpdateOnGameFrame();
    void QueueObjectCollectionReload();
    void SetRuntimeCodexGfxScale(float scale, bool queueReload);
    float GetRuntimeCodexGfxScale();
}
