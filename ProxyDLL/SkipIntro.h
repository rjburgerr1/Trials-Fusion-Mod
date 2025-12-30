// SkipIntro.h
// Skip intro videos by hooking the video loader
#pragma once
#include <Windows.h>

namespace SkipIntro {
    // Initialize the video loader hook
    bool Initialize();
    
    // Cleanup
    void Shutdown();
}
