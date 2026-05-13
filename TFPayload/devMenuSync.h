// devMenuSync.h
// This file handles synchronization between ImGui DevMenu and actual game memory
#pragma once

#include <Windows.h>
#include <unordered_map>
#include <type_traits>
#include "logging.h"

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
    bool Initialize(uintptr_t baseAddress);

    // Shutdown
    void Shutdown();

    // Get the memory location for a specific tweakable ID
    TweakableMemoryInfo* GetMemoryInfo(int tweakableId);

    template<typename T>
    bool TryReadRaw(void* valuePtr, T& outValue) {
        __try {
            outValue = *(T*)valuePtr;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    template<typename T>
    bool TryWriteRaw(void* valuePtr, const T& value, T& beforeValue, T& afterValue) {
        __try {
            beforeValue = *(T*)valuePtr;
            *(T*)valuePtr = value;
            afterValue = *(T*)valuePtr;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    // Read value from game memory
    template<typename T>
    bool ReadValue(int tweakableId, T& outValue) {
        auto* info = GetMemoryInfo(tweakableId);
        if (!info || !info->isValid || !info->valuePtr) {
            LOG_WARNING("[DevMenuSync] ReadValue rejected for ID=" << tweakableId
                << " info=" << (info ? "present" : "missing")
                << " valid=" << (info && info->isValid ? "true" : "false")
                << " ptr=0x" << std::hex << (info && info->valuePtr ? (uintptr_t)info->valuePtr : 0) << std::dec);
            return false;
        }

        if (!TryReadRaw<T>(info->valuePtr, outValue)) {
            LOG_WARNING("[DevMenuSync] ReadValue exception for ID=" << tweakableId
                << " ptr=0x" << std::hex << (uintptr_t)info->valuePtr << std::dec);
            return false;
        }

        return true;
    }

    // Write value to game memory
    template<typename T>
    bool WriteValue(int tweakableId, const T& value) {
        auto* info = GetMemoryInfo(tweakableId);
        if (!info || !info->isValid || !info->valuePtr) {
            LOG_WARNING("[DevMenuSync] WriteValue rejected for ID=" << tweakableId
                << " info=" << (info ? "present" : "missing")
                << " valid=" << (info && info->isValid ? "true" : "false")
                << " ptr=0x" << std::hex << (info && info->valuePtr ? (uintptr_t)info->valuePtr : 0) << std::dec);
            return false;
        }

        T beforeValue{};
        T afterValue{};
        if (!TryWriteRaw<T>(info->valuePtr, value, beforeValue, afterValue)) {
            LOG_WARNING("[DevMenuSync] WriteValue exception for ID=" << tweakableId
                << " ptr=0x" << std::hex << (uintptr_t)info->valuePtr << std::dec);
            return false;
        }

        LOG_VERBOSE("[DevMenuSync] WriteValue ID=" << tweakableId
            << " ptr=0x" << std::hex << (uintptr_t)info->valuePtr << std::dec
            << " before=" << (std::is_same<T, unsigned char>::value ? (int)beforeValue : beforeValue)
            << " requested=" << (std::is_same<T, unsigned char>::value ? (int)value : value)
            << " after=" << (std::is_same<T, unsigned char>::value ? (int)afterValue : afterValue));
        return true;
    }

    // Sync all ImGui tweakables FROM game memory (read game -> update UI)
    void SyncFromGame(bool captureDefaults = false);

    // Sync all ImGui tweakables TO game memory (write UI -> update game)
    void SyncToGame();

    // Force RedLynx content-pack gates open after the tweakable map is available
    bool ForceContentPackAvailability();

    // Scan game memory and build the memory map
    bool ScanGameMemory(uintptr_t baseAddress);

    // Debug: print info about a specific tweakable ID
    void DebugPrintTweakable(int id);
}
