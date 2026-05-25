#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace GearCustomization {
    bool Initialize(uintptr_t baseAddress);
    void Shutdown();

    void ProcessPendingMainThread();

    bool HasPendingReloadMutation();
    bool ApplyPendingHiddenObjectPayloadPatches();
    void RestoreActiveHiddenObjectPayloadPatches();

    // Experimental one-off bike child-item override. Hides one child item and optionally applies another.
    bool QueueBikeChildItemOverride(
        uint16_t hideItemId,
        uint16_t applyItemId,
        uint32_t packedColor,
        uint16_t extraHideItemId = 0);

    // Live 32-byte rider/bike appearance block helpers. QueueAppearanceUpdate applies
    // gear-set child visuals directly and never invokes the unsafe same-bike reload path.
    bool GetCurrentAppearanceData(uint16_t outAppearance[16]);
    bool QueueAppearanceUpdate(const uint16_t appearanceData[16]);

    // Log read-only snapshots for reverse-engineering.
    bool DumpBikeGearSetEntry(uint16_t setId);
    bool DumpHiddenObjectEntry(uint16_t itemId);

    // Experimental: copy the visual payload fields from one hidden-object entry onto another.
    bool CopyHiddenObjectVisualPayload(uint16_t targetItemId, uint16_t sourceItemId, bool* outQueued = nullptr);
    bool RestoreHiddenObjectVisualPayload(uint16_t targetItemId);

    bool GetBikeGearSetChildren(uint16_t setId, std::vector<uint16_t>* outChildren);
    bool ReplaceCurrentBikeGearSetChild(uint16_t fromItemId, uint16_t toItemId);
    bool GetHiddenObjectName(uint16_t itemId, std::string* outName);

    void SetBikeChildColorOverride(uint16_t itemId, uint32_t packedColor);
    void ClearBikeChildColorOverride(uint16_t itemId);
}
