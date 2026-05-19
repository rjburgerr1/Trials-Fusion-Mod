#pragma once

#include <Windows.h>

namespace Fmod {
    bool Initialize(uintptr_t baseAddress);
    void Shutdown();
    void Update();
    void InvalidateCachedPointers(DWORD suppressApplyMs = 5000);

    bool SetEventsMuted(bool muted);
    bool ToggleEventsMuted();
    bool AreEventsMuted();

    bool IsMuteOnStartupEnabled();
    void SetMuteOnStartupEnabled(bool enabled);
    bool ToggleMuteOnStartup();

    const char* GetStatusString();
    const char* GetStartupMuteStatusString();
    int GetLastResult();
}
