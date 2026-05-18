#pragma once

#include <Windows.h>
#include <cstdint>

namespace Sound {
    bool Initialize(uintptr_t baseAddress);
    bool IsAvailable();
    bool IsEventSystemMuted();
    bool IsEventLoggingEnabled();
    void SetEventLoggingEnabled(bool enabled);
    bool MuteEventSystem(bool mute);
    bool ToggleEventSystemMute();
}
