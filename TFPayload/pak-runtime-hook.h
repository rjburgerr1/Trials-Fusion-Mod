#pragma once

#include <Windows.h>

namespace PakRuntimeHook {
    bool Initialize(DWORD_PTR baseAddress);
    void Shutdown();
}
