#pragma once

#include <cstdint>

namespace RiderRecolor {
    enum class Region : uint8_t {
        Legs = 0,
        Torso = 1,
        Head = 2
    };

    struct RiderEntitySnapshot {
        void* entity = nullptr;
        void* sceneRoot = nullptr;
        void* sceneHost = nullptr;
        void* attachmentContainerA = nullptr;
        void* attachmentContainerB = nullptr;
        void* serializedSceneRegistry = nullptr;
        void* materialOverrides[6] = {};
    };

    struct RiderCustomizationSnapshot {
        void* customizationState = nullptr;
        uint16_t activeRiderGear[3] = {};
        uint32_t riderGearColors[12] = {};
        uint8_t riderGearColorsNew[16] = {};
        uint16_t expandedColorTable[32] = {};
    };

    struct Color3 {
        float r = 1.0f;
        float g = 1.0f;
        float b = 1.0f;
    };

    // Initializes module-local address state. No hooks are installed yet.
    bool Initialize(uintptr_t baseAddress);
    void Shutdown();

    // Current live bike+rider race entity.
    void* GetCurrentRiderEntity();

    // Captures the rider-facing fields we currently understand from Ghidra.
    bool CaptureCurrentSnapshot(RiderEntitySnapshot& outSnapshot);
    bool CaptureCurrentCustomizationSnapshot(RiderCustomizationSnapshot& outSnapshot);

    // Development helpers used while we finish the live tint bridge.
    void DebugDumpCurrentRiderState();
    bool CaptureTorsoProbeBaseline();
    bool DiffTorsoProbeAgainstBaseline();
    void ClearTorsoProbeBaseline();

    // Stable public API for the three rider regions. Torso currently writes the
    // verified customization color table only; live material refresh remains
    // disabled until the in-track scene graph target is proven safe.
    bool SetRegionColor(Region region, const Color3& color);
    bool GetRequestedRegionColor(Region region, Color3& outColor);
    bool GetCurrentRegionColor(Region region, Color3& outColor);
    bool SetLegsColor(const Color3& color);
    bool SetTorsoColor(const Color3& color);
    bool SetHeadColor(const Color3& color);
    bool RefreshTorsoMaterial();
    void ProcessQueuedRebuildOnGameThread();

    // The final arbitrary RGB writer is intentionally deferred until we prove
    // the live rider-material target. This prevents guessed writes from being
    // mistaken for supported functionality.
    bool SupportsLiveRgbRecolor();
}
