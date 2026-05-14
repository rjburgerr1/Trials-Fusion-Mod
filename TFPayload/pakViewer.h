#pragma once

#include <cstdint>

namespace PakViewer {
    bool InitializeRuntimeHooks(uintptr_t baseAddress);
    void ShutdownRuntimeHooks();
    void Toggle();
    bool IsVisible();
    void Render();
}
