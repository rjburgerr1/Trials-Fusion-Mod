// devMenuSync.h
// This file handles synchronization between ImGui DevMenu and actual game memory
#pragma once

#include <Windows.h>
#include <unordered_map>

namespace DevMenuSync {
    // Structure to hold game memory information for a tweakable
    struct TweakableMemoryInfo {
        void* valuePtr;      // Pointer to the actual value in game memory
        int type;            // 1=Bool, 2=Int, 3=Float
        bool isValid;        // Whether we successfully found this tweakable in game memory
    };

    // Map of tweakable ID -> game memory location
    extern std::unordered_map<int, TweakableMemoryInfo> g_tweakableMemoryMap;

    // Initialize the sync system - scans game memory to find all tweakables
    bool Initialize();

    // Shutdown
    void Shutdown();

    // Get the memory location for a specific tweakable ID
    TweakableMemoryInfo* GetMemoryInfo(int tweakableId);

    // Read value from game memory
    template<typename T>
    bool ReadValue(int tweakableId, T& outValue) {
        auto* info = GetMemoryInfo(tweakableId);
        if (!info || !info->isValid || !info->valuePtr) {
            return false;
        }

        __try {
            outValue = *(T*)info->valuePtr;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    // Write value to game memory
    template<typename T>
    bool WriteValue(int tweakableId, const T& value) {
        auto* info = GetMemoryInfo(tweakableId);
        if (!info || !info->isValid || !info->valuePtr) {
            return false;
        }

        __try {
            *(T*)info->valuePtr = value;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    // Sync all ImGui tweakables FROM game memory (read game -> update UI)
    void SyncFromGame();

    // Sync all ImGui tweakables TO game memory (write UI -> update game)
    void SyncToGame();

    // Scan game memory and build the memory map
    bool ScanGameMemory();
}
