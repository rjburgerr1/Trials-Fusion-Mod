#include "pch.h"
#include "gear-customization.h"
#include "base-address.h"
#include "gamemode.h"
#include "logging.h"
#include "respawn.h"
#include <Windows.h>
#include <MinHook.h>
#include <algorithm>
#include <cstring>
#include <mutex>
#include <unordered_map>

namespace GearCustomization {
    static constexpr uintptr_t GLOBAL_STRUCT_RVA_UPLAY = 0x104b308;
    static constexpr uintptr_t GLOBAL_STRUCT_RVA_STEAM = 0x104d308;
    static constexpr uintptr_t HIDE_SCENE_OBJECT_FOR_GEAR_VISUAL_RVA_UPLAY = 0x2cc660;
    static constexpr uintptr_t HIDE_SCENE_OBJECT_FOR_GEAR_VISUAL_RVA_STEAM = 0x2cbca0;
    static constexpr uintptr_t APPLY_BIKE_HIDDEN_OBJECT_MESH_TINT_RVA_UPLAY = 0x2226d0;
    static constexpr uintptr_t APPLY_BIKE_HIDDEN_OBJECT_MESH_TINT_RVA_STEAM = 0x221fc0;
    static constexpr uintptr_t GET_GEAR_SLOT_ENTRY_RVA_UPLAY = 0x23a50;
    static constexpr uintptr_t GET_GEAR_SLOT_ENTRY_RVA_STEAM = 0x23b70;
    static constexpr uintptr_t GET_HIDDEN_OBJECT_ENTRY_RVA_UPLAY = 0x25600;
    static constexpr uintptr_t GET_HIDDEN_OBJECT_ENTRY_RVA_STEAM = 0x25890;
    static constexpr uintptr_t COLLECT_SCENE_OBJECTS_BY_TYPE_RVA_UPLAY = 0x6a3000;
    static constexpr uintptr_t COLLECT_SCENE_OBJECTS_BY_TYPE_RVA_STEAM = 0x6a19f0;
    static constexpr uintptr_t REFRESH_SCENE_OBJECT_BINDING_RVA_UPLAY = 0x6cf080;
    static constexpr uintptr_t REFRESH_SCENE_OBJECT_BINDING_RVA_STEAM = 0x6cd200;

    static constexpr uintptr_t BIKE_VISUAL_CATALOG_OFFSET = 0x114;
    static constexpr uintptr_t BIKE_SCENE_ROOT_OFFSET = 0x678;
    static constexpr uintptr_t BIKE_APPEARANCE_DATA_OFFSET = 0x9ec;
    static constexpr int APPEARANCE_WORD_COUNT = 16;
    static constexpr int RIDER_SLOT_COUNT = 3;
    static constexpr int BIKE_SLOT_COUNT = 2;
    static constexpr uint32_t MAX_GEAR_SET_CHILDREN = 256;
    static constexpr uint32_t MAX_SCENE_OBJECT_SNAPSHOT = 4092;

    typedef void(__cdecl* HideSceneObjectForGearVisualFunc)(void* sceneRoot, int hiddenObjectItemId);
    typedef uint32_t(__thiscall* ApplyBikeHiddenObjectMeshTintFunc)(void* bikeEntity, uint32_t itemId, uint32_t packedColor);
    typedef void* (__thiscall* GetGearSlotEntryFunc)(void* catalog, void* outEntry, uint32_t setId);
    typedef void* (__thiscall* GetHiddenObjectEntryFunc)(void* catalog, void* outEntry, uint32_t itemId);
    typedef void(__thiscall* CollectSceneObjectsByTypeFunc)(
        void* sceneRoot,
        void* outSnapshot,
        uint32_t objectType,
        uint32_t objectSubtype,
        uint32_t useVirtualType,
        uint32_t requiredFlags);
    typedef void(__thiscall* RefreshSceneObjectBindingFunc)(void* sceneObject, void* sceneRoot);

    struct SceneObjectSnapshot {
        void* objects[MAX_SCENE_OBJECT_SNAPSHOT] = {};
        uint32_t count = 0;
    };

    struct HiddenObjectVisualPayload {
        uint32_t material = 0;
        uint32_t variant = 0;
        uint32_t payload = 0;
    };

    struct PendingHiddenObjectPayloadPatch {
        uint16_t targetItemId = 0;
        uint16_t sourceItemId = 0;
    };

    struct ActiveHiddenObjectPayloadPatch {
        uint16_t targetItemId = 0;
        HiddenObjectVisualPayload restorePayload = {};
    };

    static bool g_initialized = false;
    static uintptr_t g_baseAddress = 0;
    static void** g_globalStructPtr = nullptr;

    static HideSceneObjectForGearVisualFunc g_hideSceneObjectForGearVisual = nullptr;
    static ApplyBikeHiddenObjectMeshTintFunc g_applyBikeHiddenObjectMeshTint = nullptr;
    static ApplyBikeHiddenObjectMeshTintFunc g_OriginalApplyBikeHiddenObjectMeshTint = nullptr;
    static GetGearSlotEntryFunc g_getGearSlotEntry = nullptr;
    static GetHiddenObjectEntryFunc g_getHiddenObjectEntry = nullptr;
    static CollectSceneObjectsByTypeFunc g_collectSceneObjectsByType = nullptr;
    static RefreshSceneObjectBindingFunc g_refreshSceneObjectBinding = nullptr;

    static bool g_applyBikeTintHookInstalled = false;
    static std::mutex g_catalogMutationMutex;
    static std::unordered_map<uint16_t, uint32_t> g_bikeChildColorOverrides;
    static std::unordered_map<uint16_t, HiddenObjectVisualPayload> g_hiddenObjectOriginalPayloads;
    static std::vector<PendingHiddenObjectPayloadPatch> g_pendingHiddenObjectPayloadPatches;
    static std::vector<ActiveHiddenObjectPayloadPatch> g_activeHiddenObjectPayloadPatches;

    static volatile LONG g_pendingHiddenObjectPayloadPatch = 0;
    static volatile LONG g_pendingBikeGearSetChildReplacement = 0;
    static uint16_t g_pendingReplaceBikeChildFromItemId = 0;
    static uint16_t g_pendingReplaceBikeChildToItemId = 0;

    static volatile LONG g_pendingAppearanceUpdate = 0;
    static uint16_t g_pendingAppearanceData[APPEARANCE_WORD_COUNT] = {};
    static std::mutex g_pendingAppearanceMutex;

    static volatile LONG g_pendingBikeChildOverride = 0;
    static uint16_t g_pendingHideBikeChildItemId = 0;
    static uint16_t g_pendingApplyBikeChildItemId = 0;
    static uint32_t g_pendingBikeChildPackedColor = 0;

    static uintptr_t GetGlobalStructRVA() {
        return BaseAddress::IsSteamVersion() ? GLOBAL_STRUCT_RVA_STEAM : GLOBAL_STRUCT_RVA_UPLAY;
    }

    static uintptr_t GetHideSceneObjectForGearVisualRVA() {
        return BaseAddress::IsSteamVersion() ? HIDE_SCENE_OBJECT_FOR_GEAR_VISUAL_RVA_STEAM : HIDE_SCENE_OBJECT_FOR_GEAR_VISUAL_RVA_UPLAY;
    }

    static uintptr_t GetApplyBikeHiddenObjectMeshTintRVA() {
        return BaseAddress::IsSteamVersion() ? APPLY_BIKE_HIDDEN_OBJECT_MESH_TINT_RVA_STEAM : APPLY_BIKE_HIDDEN_OBJECT_MESH_TINT_RVA_UPLAY;
    }

    static uintptr_t GetGearSlotEntryRVA() {
        return BaseAddress::IsSteamVersion() ? GET_GEAR_SLOT_ENTRY_RVA_STEAM : GET_GEAR_SLOT_ENTRY_RVA_UPLAY;
    }

    static uintptr_t GetHiddenObjectEntryRVA() {
        return BaseAddress::IsSteamVersion() ? GET_HIDDEN_OBJECT_ENTRY_RVA_STEAM : GET_HIDDEN_OBJECT_ENTRY_RVA_UPLAY;
    }

    static uintptr_t GetCollectSceneObjectsByTypeRVA() {
        return BaseAddress::IsSteamVersion() ? COLLECT_SCENE_OBJECTS_BY_TYPE_RVA_STEAM : COLLECT_SCENE_OBJECTS_BY_TYPE_RVA_UPLAY;
    }

    static uintptr_t GetRefreshSceneObjectBindingRVA() {
        return BaseAddress::IsSteamVersion() ? REFRESH_SCENE_OBJECT_BINDING_RVA_STEAM : REFRESH_SCENE_OBJECT_BINDING_RVA_UPLAY;
    }

    static bool IsGameStateSafeForCustomization() {
        if (!GameMode::IsGameModeActive() || !GameMode::IsPlaying()) {
            return false;
        }
        if (GameMode::IsInMultiplayerMode() || GameMode::IsWatchingReplay() || GameMode::IsRaceFinished()) {
            return false;
        }
        return Respawn::GetCheckpointCount() > 0;
    }

    static uint32_t ReadAppearanceDword(const uint16_t appearance[APPEARANCE_WORD_COUNT], int wordIndex) {
        uint32_t value = 0;
        if (!appearance || wordIndex < 0 || wordIndex + 1 >= APPEARANCE_WORD_COUNT) {
            return 0;
        }

        memcpy(&value, appearance + wordIndex, sizeof(value));
        return value;
    }

    static void WriteAppearanceDword(uint16_t appearance[APPEARANCE_WORD_COUNT], int wordIndex, uint32_t value) {
        if (!appearance || wordIndex < 0 || wordIndex + 1 >= APPEARANCE_WORD_COUNT) {
            return;
        }

        memcpy(appearance + wordIndex, &value, sizeof(value));
    }

    static void* GetGameManagerStruct() {
        if (!g_globalStructPtr || IsBadReadPtr(g_globalStructPtr, sizeof(void*))) {
            return nullptr;
        }

        void* gameManager = *g_globalStructPtr;
        if (!gameManager || IsBadReadPtr(gameManager, 0x200)) {
            return nullptr;
        }
        return gameManager;
    }

    static void* GetBikeVisualCatalog() {
        void* gameManager = GetGameManagerStruct();
        if (!gameManager) {
            return nullptr;
        }

        uintptr_t addr = reinterpret_cast<uintptr_t>(gameManager) + BIKE_VISUAL_CATALOG_OFFSET;
        if (IsBadReadPtr(reinterpret_cast<void*>(addr), sizeof(void*))) {
            return nullptr;
        }

        void* catalog = *reinterpret_cast<void**>(addr);
        if (!catalog || IsBadReadPtr(catalog, 0x40)) {
            return nullptr;
        }
        return catalog;
    }

    static bool WriteAppearanceDataToBike(void* bikeEntity, const uint16_t appearanceData[APPEARANCE_WORD_COUNT]) {
        if (!bikeEntity || !appearanceData) {
            return false;
        }

        uintptr_t appearanceDataAddr = reinterpret_cast<uintptr_t>(bikeEntity) + BIKE_APPEARANCE_DATA_OFFSET;
        if (IsBadWritePtr(reinterpret_cast<void*>(appearanceDataAddr), sizeof(uint16_t) * APPEARANCE_WORD_COUNT)) {
            return false;
        }

        __try {
            memcpy(
                reinterpret_cast<void*>(appearanceDataAddr),
                appearanceData,
                sizeof(uint16_t) * APPEARANCE_WORD_COUNT);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    static bool CallApplyBikeHiddenObjectMeshTint_Inner(void* bikeEntity, uint16_t itemId, uint32_t packedColor, DWORD* exceptionCode) {
        *exceptionCode = 0;
        __try {
            return (g_applyBikeHiddenObjectMeshTint(bikeEntity, itemId, packedColor) & 0xff) != 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            *exceptionCode = GetExceptionCode();
            return false;
        }
    }

    static bool CallApplyBikeHiddenObjectMeshTint(void* bikeEntity, uint16_t itemId, uint32_t packedColor) {
        DWORD exceptionCode = 0;
        bool result = CallApplyBikeHiddenObjectMeshTint_Inner(bikeEntity, itemId, packedColor, &exceptionCode);
        if (exceptionCode != 0) {
            LOG_ERROR("[GearCustomization] Exception in BikeVisuals_ApplyBikeHiddenObjectMeshTint: 0x" << std::hex << exceptionCode);
        }
        return result;
    }

    static bool CallHideSceneObjectForGearVisual_Inner(void* sceneRoot, uint16_t itemId, DWORD* exceptionCode) {
        *exceptionCode = 0;
        __try {
            g_hideSceneObjectForGearVisual(sceneRoot, itemId);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            *exceptionCode = GetExceptionCode();
            return false;
        }
    }

    static bool CallHideSceneObjectForGearVisual(void* sceneRoot, uint16_t itemId) {
        DWORD exceptionCode = 0;
        bool result = CallHideSceneObjectForGearVisual_Inner(sceneRoot, itemId, &exceptionCode);
        if (!result) {
            LOG_ERROR("[GearCustomization] Exception in HideSceneObjectForGearVisual: 0x" << std::hex << exceptionCode);
        }
        return result;
    }

    static bool CallCollectSceneObjectsByType_Inner(void* sceneRoot, SceneObjectSnapshot* snapshot, DWORD* exceptionCode) {
        *exceptionCode = 0;
        __try {
            g_collectSceneObjectsByType(sceneRoot, snapshot, 1, 6, 0, 0);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            *exceptionCode = GetExceptionCode();
            return false;
        }
    }

    static bool CaptureAppearanceSceneObjects(void* sceneRoot, SceneObjectSnapshot* snapshot) {
        if (!sceneRoot || !snapshot || !g_collectSceneObjectsByType) {
            return false;
        }

        memset(snapshot, 0, sizeof(*snapshot));
        DWORD exceptionCode = 0;
        const bool result = CallCollectSceneObjectsByType_Inner(sceneRoot, snapshot, &exceptionCode);
        if (!result) {
            LOG_ERROR("[GearCustomization] Exception in CollectSceneObjectsByType: 0x" << std::hex << exceptionCode);
            return false;
        }

        if (snapshot->count > MAX_SCENE_OBJECT_SNAPSHOT) {
            LOG_WARNING("[GearCustomization] Scene-object snapshot count was out of range: " << snapshot->count);
            snapshot->count = MAX_SCENE_OBJECT_SNAPSHOT;
        }
        return true;
    }

    static bool CallRefreshSceneObjectBinding_Inner(void* sceneObject, void* sceneRoot, DWORD* exceptionCode) {
        *exceptionCode = 0;
        __try {
            g_refreshSceneObjectBinding(sceneObject, sceneRoot);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            *exceptionCode = GetExceptionCode();
            return false;
        }
    }

    static void RefreshAppearanceSceneObjects(void* sceneRoot, const SceneObjectSnapshot& snapshot) {
        if (!sceneRoot || !g_refreshSceneObjectBinding || snapshot.count == 0) {
            return;
        }

        uint32_t refreshed = 0;
        for (uint32_t i = 0; i < snapshot.count && i < MAX_SCENE_OBJECT_SNAPSHOT; ++i) {
            void* sceneObject = snapshot.objects[i];
            if (!sceneObject || IsBadReadPtr(sceneObject, 0x60)) {
                continue;
            }

            DWORD exceptionCode = 0;
            if (CallRefreshSceneObjectBinding_Inner(sceneObject, sceneRoot, &exceptionCode)) {
                ++refreshed;
            }
            else {
                LOG_WARNING("[GearCustomization] Scene-object refresh exception: 0x" << std::hex << exceptionCode);
            }
        }

        LOG_INFO("[GearCustomization] Refreshed " << std::dec << refreshed
            << " appearance scene object(s) after direct apply");
    }

    static bool CallGetGearSlotEntry_Inner(void* catalog, void* outEntry, uint16_t setId, DWORD* exceptionCode) {
        *exceptionCode = 0;
        __try {
            g_getGearSlotEntry(catalog, outEntry, setId);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            *exceptionCode = GetExceptionCode();
            return false;
        }
    }

    static bool CallGetGearSlotEntry(void* catalog, void* outEntry, uint16_t setId) {
        DWORD exceptionCode = 0;
        const bool result = CallGetGearSlotEntry_Inner(catalog, outEntry, setId, &exceptionCode);
        if (!result) {
            LOG_ERROR("[GearCustomization] Exception in BikeVisualCatalog_GetGearSlotEntry: 0x" << std::hex << exceptionCode);
        }
        return result;
    }

    static bool CallGetHiddenObjectEntry_Inner(void* catalog, void* outEntry, uint16_t itemId, DWORD* exceptionCode) {
        *exceptionCode = 0;
        __try {
            g_getHiddenObjectEntry(catalog, outEntry, itemId);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            *exceptionCode = GetExceptionCode();
            return false;
        }
    }

    static bool CallGetHiddenObjectEntry(void* catalog, void* outEntry, uint16_t itemId) {
        DWORD exceptionCode = 0;
        const bool result = CallGetHiddenObjectEntry_Inner(catalog, outEntry, itemId, &exceptionCode);
        if (!result) {
            LOG_ERROR("[GearCustomization] Exception in BikeVisualCatalog_GetHiddenObjectEntry: 0x" << std::hex << exceptionCode);
        }
        return result;
    }

    static uint32_t __fastcall Hook_ApplyBikeHiddenObjectMeshTint(
        void* bikeEntity,
        void* edx_unused,
        uint32_t itemId,
        uint32_t packedColor) {
        uint32_t color = packedColor;
        {
            std::lock_guard<std::mutex> lock(g_catalogMutationMutex);
            const auto overrideIt = g_bikeChildColorOverrides.find(static_cast<uint16_t>(itemId));
            if (overrideIt != g_bikeChildColorOverrides.end()) {
                color = overrideIt->second;
            }
        }

        return g_OriginalApplyBikeHiddenObjectMeshTint
            ? g_OriginalApplyBikeHiddenObjectMeshTint(bikeEntity, itemId, color)
            : 0;
    }

    bool Initialize(uintptr_t baseAddress) {
        g_baseAddress = baseAddress;
        g_globalStructPtr = reinterpret_cast<void**>(baseAddress + GetGlobalStructRVA());
        g_hideSceneObjectForGearVisual = reinterpret_cast<HideSceneObjectForGearVisualFunc>(
            baseAddress + GetHideSceneObjectForGearVisualRVA());
        g_applyBikeHiddenObjectMeshTint = reinterpret_cast<ApplyBikeHiddenObjectMeshTintFunc>(
            baseAddress + GetApplyBikeHiddenObjectMeshTintRVA());
        g_getGearSlotEntry = reinterpret_cast<GetGearSlotEntryFunc>(
            baseAddress + GetGearSlotEntryRVA());
        g_getHiddenObjectEntry = reinterpret_cast<GetHiddenObjectEntryFunc>(
            baseAddress + GetHiddenObjectEntryRVA());
        g_collectSceneObjectsByType = reinterpret_cast<CollectSceneObjectsByTypeFunc>(
            baseAddress + GetCollectSceneObjectsByTypeRVA());
        g_refreshSceneObjectBinding = reinterpret_cast<RefreshSceneObjectBindingFunc>(
            baseAddress + GetRefreshSceneObjectBindingRVA());

        g_applyBikeTintHookInstalled = false;
        if (g_applyBikeHiddenObjectMeshTint) {
            MH_STATUS hookStatus = MH_CreateHook(
                reinterpret_cast<LPVOID>(g_applyBikeHiddenObjectMeshTint),
                reinterpret_cast<LPVOID>(&Hook_ApplyBikeHiddenObjectMeshTint),
                reinterpret_cast<LPVOID*>(&g_OriginalApplyBikeHiddenObjectMeshTint));
            if (hookStatus == MH_OK) {
                hookStatus = MH_EnableHook(reinterpret_cast<LPVOID>(g_applyBikeHiddenObjectMeshTint));
                if (hookStatus == MH_OK) {
                    g_applyBikeTintHookInstalled = true;
                    LOG_INFO("[GearCustomization] Bike child tint hook installed");
                }
            }
            else if (hookStatus != MH_ERROR_ALREADY_CREATED) {
                LOG_WARNING("[GearCustomization] Failed to create bike child tint hook: " << MH_StatusToString(hookStatus));
            }
        }

        g_initialized = true;
        LOG_INFO("[GearCustomization] Initialized");
        return true;
    }

    void Shutdown() {
        if (g_applyBikeTintHookInstalled && g_applyBikeHiddenObjectMeshTint) {
            MH_DisableHook(reinterpret_cast<LPVOID>(g_applyBikeHiddenObjectMeshTint));
            MH_RemoveHook(reinterpret_cast<LPVOID>(g_applyBikeHiddenObjectMeshTint));
        }

        g_initialized = false;
        g_baseAddress = 0;
        g_globalStructPtr = nullptr;
        g_hideSceneObjectForGearVisual = nullptr;
        g_applyBikeHiddenObjectMeshTint = nullptr;
        g_OriginalApplyBikeHiddenObjectMeshTint = nullptr;
        g_getGearSlotEntry = nullptr;
        g_getHiddenObjectEntry = nullptr;
        g_collectSceneObjectsByType = nullptr;
        g_refreshSceneObjectBinding = nullptr;
        g_applyBikeTintHookInstalled = false;
        {
            std::lock_guard<std::mutex> lock(g_catalogMutationMutex);
            g_bikeChildColorOverrides.clear();
            g_hiddenObjectOriginalPayloads.clear();
            g_pendingHiddenObjectPayloadPatches.clear();
            g_activeHiddenObjectPayloadPatches.clear();
        }
        g_pendingHiddenObjectPayloadPatch = 0;
        g_pendingBikeGearSetChildReplacement = 0;
        g_pendingAppearanceUpdate = 0;
        g_pendingBikeChildOverride = 0;
    }

    bool HasPendingReloadMutation() {
        return InterlockedCompareExchange(&g_pendingHiddenObjectPayloadPatch, 0, 0) != 0
            || InterlockedCompareExchange(&g_pendingBikeGearSetChildReplacement, 0, 0) != 0;
    }

    static bool CopyHiddenObjectVisualPayloadOnGameThread(uint16_t targetItemId, uint16_t sourceItemId) {
        if (!g_initialized || !g_getHiddenObjectEntry) {
            LOG_ERROR("[GearCustomization] Hidden-object payload patch unavailable because module is not ready");
            return false;
        }

        void* catalog = GetBikeVisualCatalog();
        if (!catalog) {
            LOG_ERROR("[GearCustomization] Hidden-object payload patch could not get bike visual catalog");
            return false;
        }

        void* targetWrapper[2] = {};
        void* sourceWrapper[2] = {};
        if (!CallGetHiddenObjectEntry(catalog, targetWrapper, targetItemId)
            || !CallGetHiddenObjectEntry(catalog, sourceWrapper, sourceItemId)) {
            return false;
        }

        uint8_t* target = reinterpret_cast<uint8_t*>(targetWrapper[0]);
        uint8_t* source = reinterpret_cast<uint8_t*>(sourceWrapper[0]);
        if (!target || !source || IsBadWritePtr(target, 0x18) || IsBadReadPtr(source, 0x18)) {
            LOG_ERROR("[GearCustomization] Hidden-object payload patch could not access the requested entries");
            return false;
        }

        std::lock_guard<std::mutex> lock(g_catalogMutationMutex);
        const auto captureOriginalPayload = [](uint16_t itemId, uint8_t* entry) {
            auto existing = g_hiddenObjectOriginalPayloads.find(itemId);
            if (existing != g_hiddenObjectOriginalPayloads.end()) {
                return existing->second;
            }

            HiddenObjectVisualPayload original = {};
            original.material = *reinterpret_cast<uint32_t*>(entry + 0x0c);
            original.variant = *reinterpret_cast<uint32_t*>(entry + 0x10);
            original.payload = *reinterpret_cast<uint32_t*>(entry + 0x14);
            g_hiddenObjectOriginalPayloads[itemId] = original;
            LOG_INFO("[GearCustomization] Captured original hidden-object payload " << itemId
                << " material=0x" << std::hex << original.material
                << " variant=0x" << original.variant
                << " payload=0x" << original.payload);
            return original;
        };

        captureOriginalPayload(targetItemId, target);
        const HiddenObjectVisualPayload sourceOriginal = captureOriginalPayload(sourceItemId, source);
        const HiddenObjectVisualPayload targetOriginal = g_hiddenObjectOriginalPayloads[targetItemId];

        *reinterpret_cast<uint32_t*>(target + 0x0c) = sourceOriginal.material;
        *reinterpret_cast<uint32_t*>(target + 0x10) = sourceOriginal.variant;
        *reinterpret_cast<uint32_t*>(target + 0x14) = sourceOriginal.payload;

        ActiveHiddenObjectPayloadPatch activePatch = {};
        activePatch.targetItemId = targetItemId;
        activePatch.restorePayload = targetOriginal;
        g_activeHiddenObjectPayloadPatches.push_back(activePatch);

        LOG_INFO("[GearCustomization] Copied hidden-object visual payload " << sourceItemId
            << " -> " << targetItemId
            << " material=0x" << std::hex << sourceOriginal.material
            << " variant=0x" << sourceOriginal.variant
            << " payload=0x" << sourceOriginal.payload);
        return true;
    }

    bool ApplyPendingHiddenObjectPayloadPatches() {
        if (InterlockedCompareExchange(&g_pendingHiddenObjectPayloadPatch, 0, 0) == 0) {
            return true;
        }

        std::vector<PendingHiddenObjectPayloadPatch> pendingPatches;
        {
            std::lock_guard<std::mutex> lock(g_catalogMutationMutex);
            pendingPatches = g_pendingHiddenObjectPayloadPatches;
            g_pendingHiddenObjectPayloadPatches.clear();
        }
        InterlockedExchange(&g_pendingHiddenObjectPayloadPatch, 0);

        LOG_INFO("[GearCustomization] Applying " << pendingPatches.size()
            << " deferred hidden-object payload patch(es) immediately before reload");
        for (const PendingHiddenObjectPayloadPatch& patch : pendingPatches) {
            if (!CopyHiddenObjectVisualPayloadOnGameThread(patch.targetItemId, patch.sourceItemId)) {
                LOG_WARNING("[GearCustomization] Deferred hidden-object payload patch failed");
                RestoreActiveHiddenObjectPayloadPatches();
                return false;
            }
        }
        return true;
    }

    void RestoreActiveHiddenObjectPayloadPatches() {
        std::vector<ActiveHiddenObjectPayloadPatch> activePatches;
        {
            std::lock_guard<std::mutex> lock(g_catalogMutationMutex);
            activePatches.swap(g_activeHiddenObjectPayloadPatches);
        }

        if (activePatches.empty() || !g_getHiddenObjectEntry) {
            return;
        }

        void* catalog = GetBikeVisualCatalog();
        if (!catalog) {
            LOG_WARNING("[GearCustomization] Could not restore hidden-object payload patch: catalog unavailable");
            std::lock_guard<std::mutex> lock(g_catalogMutationMutex);
            g_activeHiddenObjectPayloadPatches.insert(
                g_activeHiddenObjectPayloadPatches.end(),
                activePatches.begin(),
                activePatches.end());
            return;
        }

        for (const ActiveHiddenObjectPayloadPatch& activePatch : activePatches) {
            void* targetWrapper[2] = {};
            if (!CallGetHiddenObjectEntry(catalog, targetWrapper, activePatch.targetItemId)) {
                LOG_WARNING("[GearCustomization] Could not restore hidden-object payload patch "
                    << activePatch.targetItemId << ": target unavailable");
                continue;
            }

            uint8_t* target = reinterpret_cast<uint8_t*>(targetWrapper[0]);
            if (!target || IsBadWritePtr(target, 0x18)) {
                LOG_WARNING("[GearCustomization] Could not restore hidden-object payload patch "
                    << activePatch.targetItemId << ": target unwritable");
                continue;
            }

            const HiddenObjectVisualPayload restore = activePatch.restorePayload;
            *reinterpret_cast<uint32_t*>(target + 0x0c) = restore.material;
            *reinterpret_cast<uint32_t*>(target + 0x10) = restore.variant;
            *reinterpret_cast<uint32_t*>(target + 0x14) = restore.payload;
            LOG_INFO("[GearCustomization] Restored hidden-object visual payload "
                << activePatch.targetItemId
                << " material=0x" << std::hex << restore.material
                << " variant=0x" << restore.variant
                << " payload=0x" << restore.payload);
        }
    }

    static bool ReadHiddenObjectVisualPayload(uint16_t itemId, HiddenObjectVisualPayload* outPayload) {
        if (!outPayload || !g_initialized || !g_getHiddenObjectEntry || itemId == 0) {
            return false;
        }

        void* catalog = GetBikeVisualCatalog();
        if (!catalog) {
            return false;
        }

        void* wrapper[2] = {};
        if (!CallGetHiddenObjectEntry(catalog, wrapper, itemId)) {
            return false;
        }

        uint8_t* entry = reinterpret_cast<uint8_t*>(wrapper[0]);
        if (!entry || IsBadReadPtr(entry, 0x18)) {
            return false;
        }

        outPayload->material = *reinterpret_cast<uint32_t*>(entry + 0x0c);
        outPayload->variant = *reinterpret_cast<uint32_t*>(entry + 0x10);
        outPayload->payload = *reinterpret_cast<uint32_t*>(entry + 0x14);
        return true;
    }

    bool CopyHiddenObjectVisualPayload(uint16_t targetItemId, uint16_t sourceItemId, bool* outQueued) {
        if (outQueued) {
            *outQueued = false;
        }

        if (!g_initialized || !g_getHiddenObjectEntry) {
            LOG_ERROR("[GearCustomization] Hidden-object payload patch unavailable because module is not ready");
            return false;
        }

        if (targetItemId == 0 || sourceItemId == 0) {
            LOG_WARNING("[GearCustomization] Hidden-object payload patch requires non-zero IDs");
            return false;
        }

        if (!IsGameStateSafeForCustomization()) {
            LOG_WARNING("[GearCustomization] Hidden-object payload patch unavailable in current game state");
            return false;
        }

        HiddenObjectVisualPayload sourcePayload = {};
        if (!ReadHiddenObjectVisualPayload(sourceItemId, &sourcePayload)) {
            LOG_WARNING("[GearCustomization] Hidden-object payload patch could not inspect source "
                << sourceItemId);
            return false;
        }

        if (sourcePayload.payload == 0) {
            LOG_INFO("[GearCustomization] Skipping hidden-object payload patch "
                << sourceItemId << " -> " << targetItemId
                << " because source has no visual payload object"
                << " material=0x" << std::hex << sourcePayload.material
                << " variant=0x" << sourcePayload.variant
                << " payload=0x" << sourcePayload.payload);
            return true;
        }

        {
            std::lock_guard<std::mutex> lock(g_catalogMutationMutex);
            for (auto it = g_pendingHiddenObjectPayloadPatches.begin();
                it != g_pendingHiddenObjectPayloadPatches.end();) {
                if (it->targetItemId == targetItemId) {
                    it = g_pendingHiddenObjectPayloadPatches.erase(it);
                }
                else {
                    ++it;
                }
            }

            PendingHiddenObjectPayloadPatch patch = {};
            patch.targetItemId = targetItemId;
            patch.sourceItemId = sourceItemId;
            g_pendingHiddenObjectPayloadPatches.push_back(patch);
        }
        InterlockedExchange(&g_pendingHiddenObjectPayloadPatch, 1);
        if (outQueued) {
            *outQueued = true;
        }
        LOG_INFO("[GearCustomization] Queued hidden-object visual payload patch "
            << sourceItemId << " -> " << targetItemId);
        return true;
    }

    bool GetBikeGearSetChildren(uint16_t setId, std::vector<uint16_t>* outChildren) {
        if (!outChildren || !g_initialized || !g_getGearSlotEntry) {
            return false;
        }

        void* catalog = GetBikeVisualCatalog();
        if (!catalog) {
            return false;
        }

        void* entryWrapper[2] = {};
        if (!CallGetGearSlotEntry(catalog, entryWrapper, setId)) {
            return false;
        }

        uint8_t* entry = reinterpret_cast<uint8_t*>(entryWrapper[0]);
        if (!entry || IsBadReadPtr(entry, 0x1c)) {
            return false;
        }

        const uint32_t childCount = *reinterpret_cast<uint32_t*>(entry + 0x14);
        uint16_t* childItems = *reinterpret_cast<uint16_t**>(entry + 0x18);
        if (childCount == 0
            || childCount > MAX_GEAR_SET_CHILDREN
            || !childItems
            || reinterpret_cast<uintptr_t>(childItems) < 0x10000
            || IsBadReadPtr(childItems, sizeof(uint16_t) * childCount)) {
            return false;
        }

        outChildren->assign(childItems, childItems + childCount);
        return true;
    }

    bool GetCurrentAppearanceData(uint16_t outAppearance[APPEARANCE_WORD_COUNT]) {
        if (!outAppearance) {
            return false;
        }

        void* bikeEntity = Respawn::GetBikePointer();
        if (!bikeEntity) {
            return false;
        }

        uintptr_t appearanceDataAddr = reinterpret_cast<uintptr_t>(bikeEntity) + BIKE_APPEARANCE_DATA_OFFSET;
        if (IsBadReadPtr(reinterpret_cast<void*>(appearanceDataAddr), sizeof(uint16_t) * APPEARANCE_WORD_COUNT)) {
            return false;
        }

        __try {
            memcpy(
                outAppearance,
                reinterpret_cast<void*>(appearanceDataAddr),
                sizeof(uint16_t) * APPEARANCE_WORD_COUNT);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    static bool GetGearSetChildrenOrEmpty(uint16_t setId, std::vector<uint16_t>* outChildren) {
        if (!outChildren) {
            return false;
        }

        outChildren->clear();
        if (setId == 0) {
            return true;
        }

        if (!GetBikeGearSetChildren(setId, outChildren)) {
            LOG_WARNING("[GearCustomization] Could not read gear-set children for set " << setId);
            return false;
        }

        return true;
    }

    static bool ApplyGearSetChildList(
        void* bikeEntity,
        uint16_t setId,
        const std::vector<uint16_t>& children,
        uint32_t packedColor) {
        if (setId == 0 || children.empty()) {
            return true;
        }

        bool allApplied = true;
        for (uint16_t childItemId : children) {
            HiddenObjectVisualPayload payload = {};
            const bool hasPayload = ReadHiddenObjectVisualPayload(childItemId, &payload);
            std::string hiddenName;
            GetHiddenObjectName(childItemId, &hiddenName);
            LOG_INFO("[GearCustomization] Applying child item " << childItemId
                << " set=" << setId
                << " name=" << (hiddenName.empty() ? "<unknown>" : hiddenName)
                << " material=0x" << std::hex << (hasPayload ? payload.material : 0)
                << " variant=0x" << (hasPayload ? payload.variant : 0)
                << " payload=0x" << (hasPayload ? payload.payload : 0)
                << " color=0x" << packedColor);
            if (!CallApplyBikeHiddenObjectMeshTint(bikeEntity, childItemId, packedColor)) {
                LOG_WARNING("[GearCustomization] Could not apply child item " << childItemId
                    << " from gear set " << setId);
                allApplied = false;
            }
        }

        return allApplied;
    }

    static bool ApplyAppearanceSlot(
        const char* slotName,
        void* bikeEntity,
        uint16_t oldSetId,
        uint16_t newSetId,
        uint32_t oldParam,
        uint32_t newParam) {
        const bool setChanged = oldSetId != newSetId;
        const bool paramChanged = oldParam != newParam;
        if (!setChanged && !paramChanged) {
            return true;
        }

        std::vector<uint16_t> newChildren;
        if (!GetGearSetChildrenOrEmpty(newSetId, &newChildren)) {
            return false;
        }

        std::vector<uint16_t> oldChildren;
        if (setChanged && oldSetId != 0 && !GetGearSetChildrenOrEmpty(oldSetId, &oldChildren)) {
            LOG_WARNING("[GearCustomization] Could not read previous " << slotName
                << " children for set " << oldSetId
                << "; applying without pre-hide");
            oldChildren.clear();
        }

        LOG_INFO("[GearCustomization] Applying " << slotName
            << " set " << oldSetId << " -> " << newSetId
            << " param=0x" << std::hex << newParam);
        if (!oldChildren.empty()) {
            LOG_INFO("[GearCustomization] Previous " << slotName
                << " set " << std::dec << oldSetId
                << " had " << oldChildren.size() << " child item(s)");
            for (uint16_t childItemId : oldChildren) {
                HiddenObjectVisualPayload payload = {};
                const bool hasPayload = ReadHiddenObjectVisualPayload(childItemId, &payload);
                std::string hiddenName;
                GetHiddenObjectName(childItemId, &hiddenName);
                LOG_INFO("[GearCustomization] Previous child item " << childItemId
                    << " set=" << oldSetId
                    << " name=" << (hiddenName.empty() ? "<unknown>" : hiddenName)
                    << " material=0x" << std::hex << (hasPayload ? payload.material : 0)
                    << " variant=0x" << (hasPayload ? payload.variant : 0)
                    << " payload=0x" << (hasPayload ? payload.payload : 0));
            }
        }

        if (newSetId != 0 && !ApplyGearSetChildList(bikeEntity, newSetId, newChildren, newParam)) {
            LOG_WARNING("[GearCustomization] " << slotName
                << " apply failed; keeping previous visible children");
            return false;
        }

        if (setChanged && oldSetId != 0) {
            LOG_INFO("[GearCustomization] Skipped old " << slotName
                << " child cleanup for stability after applying set " << newSetId);
        }
        return true;
    }

    static bool ApplyAppearanceUpdateOnGameThread(const uint16_t requestedAppearance[APPEARANCE_WORD_COUNT]) {
        if (!requestedAppearance) {
            return false;
        }

        if (!g_initialized || !g_hideSceneObjectForGearVisual || !g_applyBikeHiddenObjectMeshTint) {
            LOG_ERROR("[GearCustomization] Appearance update unavailable because module is not ready");
            return false;
        }

        if (!IsGameStateSafeForCustomization()) {
            LOG_WARNING("[GearCustomization] Appearance update unavailable in current game state");
            return false;
        }

        void* bikeEntity = Respawn::GetBikePointer();
        if (!bikeEntity || IsBadReadPtr(bikeEntity, 0xb20)) {
            LOG_ERROR("[GearCustomization] Appearance update skipped because the current bike is unavailable");
            return false;
        }

        uintptr_t sceneRootAddr = reinterpret_cast<uintptr_t>(bikeEntity) + BIKE_SCENE_ROOT_OFFSET;
        if (IsBadReadPtr(reinterpret_cast<void*>(sceneRootAddr), sizeof(void*))) {
            LOG_ERROR("[GearCustomization] Appearance update could not read scene root");
            return false;
        }

        void* sceneRoot = *reinterpret_cast<void**>(sceneRootAddr);
        if (!sceneRoot || IsBadReadPtr(sceneRoot, 0x60)) {
            LOG_ERROR("[GearCustomization] Appearance update has no readable scene root");
            return false;
        }

        SceneObjectSnapshot sceneSnapshot = {};
        const bool hasSceneSnapshot = CaptureAppearanceSceneObjects(sceneRoot, &sceneSnapshot);
        if (!hasSceneSnapshot) {
            LOG_WARNING("[GearCustomization] Appearance scene-object snapshot unavailable; applying without post-refresh");
        }

        uint16_t currentAppearance[APPEARANCE_WORD_COUNT] = {};
        if (!GetCurrentAppearanceData(currentAppearance)) {
            LOG_ERROR("[GearCustomization] Appearance update could not read current appearance");
            return false;
        }

        uint16_t appliedAppearance[APPEARANCE_WORD_COUNT] = {};
        memcpy(appliedAppearance, requestedAppearance, sizeof(appliedAppearance));

        bool allSlotsApplied = true;
        static const char* kRiderSlotNames[RIDER_SLOT_COUNT] = {
            "rider helmet",
            "rider top",
            "rider bottom"
        };
        for (int slot = 0; slot < RIDER_SLOT_COUNT; ++slot) {
            const uint16_t oldSetId = currentAppearance[slot];
            const uint16_t newSetId = requestedAppearance[slot];
            const int colorWordIndex = 4 + slot * 2;
            const uint32_t oldColor = ReadAppearanceDword(currentAppearance, colorWordIndex);
            const uint32_t newColor = ReadAppearanceDword(requestedAppearance, colorWordIndex);

            if (!ApplyAppearanceSlot(
                    kRiderSlotNames[slot],
                    bikeEntity,
                    oldSetId,
                    newSetId,
                    oldColor,
                    newColor)) {
                appliedAppearance[slot] = oldSetId;
                WriteAppearanceDword(appliedAppearance, colorWordIndex, oldColor);
                allSlotsApplied = false;
            }
        }

        static const char* kBikeSlotNames[BIKE_SLOT_COUNT] = {
            "bike rims",
            "bike body kit"
        };
        for (int slot = 0; slot < BIKE_SLOT_COUNT; ++slot) {
            const int setWordIndex = 10 + slot;
            const int paramWordIndex = 12 + slot * 2;
            const uint16_t oldSetId = currentAppearance[setWordIndex];
            const uint16_t newSetId = requestedAppearance[setWordIndex];
            const uint32_t oldParam = ReadAppearanceDword(currentAppearance, paramWordIndex);
            const uint32_t newParam = ReadAppearanceDword(requestedAppearance, paramWordIndex);

            if (!ApplyAppearanceSlot(
                    kBikeSlotNames[slot],
                    bikeEntity,
                    oldSetId,
                    newSetId,
                    oldParam,
                    newParam)) {
                appliedAppearance[setWordIndex] = oldSetId;
                WriteAppearanceDword(appliedAppearance, paramWordIndex, oldParam);
                allSlotsApplied = false;
            }
        }

        if (!WriteAppearanceDataToBike(bikeEntity, appliedAppearance)) {
            LOG_ERROR("[GearCustomization] Appearance update could not write live appearance data");
            return false;
        }

        if (hasSceneSnapshot) {
            RefreshAppearanceSceneObjects(sceneRoot, sceneSnapshot);
        }

        LOG_INFO("[GearCustomization] Appearance update "
            << (allSlotsApplied ? "complete" : "partially applied")
            << " via direct gear-set child path");
        Logging::WriteImmediate("[GearCustomization] direct appearance update complete");
        return allSlotsApplied;
    }

    bool QueueAppearanceUpdate(const uint16_t appearanceData[APPEARANCE_WORD_COUNT]) {
        Logging::WriteImmediate("[GearCustomization] QueueAppearanceUpdate entered");
        if (!appearanceData) {
            Logging::WriteImmediate("[GearCustomization] appearance update rejected: null data");
            return false;
        }

        if (!g_initialized) {
            Logging::WriteImmediate("[GearCustomization] appearance update rejected: module not ready");
            LOG_ERROR("[GearCustomization] Appearance update unavailable because module is not ready");
            return false;
        }

        if (!IsGameStateSafeForCustomization()) {
            Logging::WriteImmediate("[GearCustomization] appearance update rejected: unsafe game state");
            LOG_WARNING("[GearCustomization] Appearance update unavailable in current game state");
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(g_pendingAppearanceMutex);
            memcpy(g_pendingAppearanceData, appearanceData, sizeof(g_pendingAppearanceData));
        }

        const LONG alreadyPending = InterlockedExchange(&g_pendingAppearanceUpdate, 1);
        Logging::WriteImmediate(alreadyPending
            ? "[GearCustomization] replaced pending direct appearance update"
            : "[GearCustomization] queued direct appearance update");
        LOG_INFO("[GearCustomization] Queued direct appearance update");
        return true;
    }

    static bool ReplaceCurrentBikeGearSetChildOnGameThread(uint16_t fromItemId, uint16_t toItemId) {
        uint16_t appearance[16] = {};
        if (!GetCurrentAppearanceData(appearance)) {
            LOG_ERROR("[GearCustomization] Gear-set child replacement could not read current appearance");
            return false;
        }

        void* bikeEntity = Respawn::GetBikePointer();
        if (!bikeEntity || IsBadReadPtr(bikeEntity, 0xb20)) {
            LOG_ERROR("[GearCustomization] Gear-set child replacement skipped because the current bike is unavailable");
            return false;
        }

        uintptr_t sceneRootAddr = reinterpret_cast<uintptr_t>(bikeEntity) + BIKE_SCENE_ROOT_OFFSET;
        if (IsBadReadPtr(reinterpret_cast<void*>(sceneRootAddr), sizeof(void*))) {
            LOG_ERROR("[GearCustomization] Gear-set child replacement could not read scene root");
            return false;
        }

        void* sceneRoot = *reinterpret_cast<void**>(sceneRootAddr);
        SceneObjectSnapshot sceneSnapshot = {};
        const bool hasSceneSnapshot = sceneRoot && !IsBadReadPtr(sceneRoot, 0x60)
            && CaptureAppearanceSceneObjects(sceneRoot, &sceneSnapshot);

        void* catalog = GetBikeVisualCatalog();
        if (!catalog) {
            LOG_ERROR("[GearCustomization] Gear-set child replacement could not get bike visual catalog");
            return false;
        }

        bool replaced = false;
        for (int slot = 0; slot < 2; ++slot) {
            const uint16_t setId = appearance[10 + slot];
            if (setId == 0) {
                continue;
            }

            void* entryWrapper[2] = {};
            if (!CallGetGearSlotEntry(catalog, entryWrapper, setId)) {
                continue;
            }

            uint8_t* entry = reinterpret_cast<uint8_t*>(entryWrapper[0]);
            if (!entry || IsBadReadPtr(entry, 0x1c)) {
                continue;
            }

            const uint32_t childCount = *reinterpret_cast<uint32_t*>(entry + 0x14);
            uint16_t* childItems = *reinterpret_cast<uint16_t**>(entry + 0x18);
            if (childCount == 0 || !childItems
                || IsBadWritePtr(childItems, sizeof(uint16_t) * childCount)) {
                continue;
            }

            std::lock_guard<std::mutex> lock(g_catalogMutationMutex);
            for (uint32_t childIndex = 0; childIndex < childCount; ++childIndex) {
                if (childItems[childIndex] == fromItemId) {
                    childItems[childIndex] = toItemId;
                    replaced = true;
                    LOG_INFO("[GearCustomization] Replaced gear-set child in set " << setId
                        << " index=" << childIndex
                        << " " << fromItemId << " -> " << toItemId);
                }
            }
        }

        if (!replaced) {
            LOG_WARNING("[GearCustomization] Gear-set child replacement found no active child " << fromItemId);
        }
        else if (hasSceneSnapshot) {
            RefreshAppearanceSceneObjects(sceneRoot, sceneSnapshot);
        }
        return replaced;
    }

    bool ReplaceCurrentBikeGearSetChild(uint16_t fromItemId, uint16_t toItemId) {
        if (!g_initialized || !g_getGearSlotEntry || fromItemId == 0 || toItemId == 0) {
            return false;
        }

        if (!IsGameStateSafeForCustomization()) {
            LOG_WARNING("[GearCustomization] Gear-set child replacement unavailable in current game state");
            return false;
        }

        if (InterlockedCompareExchange(&g_pendingBikeGearSetChildReplacement, 0, 0) != 0) {
            LOG_WARNING("[GearCustomization] Gear-set child replacement already pending");
            return false;
        }

        g_pendingReplaceBikeChildFromItemId = fromItemId;
        g_pendingReplaceBikeChildToItemId = toItemId;
        InterlockedExchange(&g_pendingBikeGearSetChildReplacement, 1);
        LOG_INFO("[GearCustomization] Queued gear-set child replacement "
            << fromItemId << " -> " << toItemId);
        return true;
    }

    bool QueueBikeChildItemOverride(uint16_t hideItemId, uint16_t applyItemId, uint32_t packedColor) {
        if (!g_initialized || !g_hideSceneObjectForGearVisual || !g_applyBikeHiddenObjectMeshTint) {
            LOG_ERROR("[GearCustomization] Bike child override unavailable because module is not ready");
            return false;
        }

        if (!IsGameStateSafeForCustomization()) {
            LOG_WARNING("[GearCustomization] Bike child override unavailable in current game state");
            return false;
        }

        if (InterlockedCompareExchange(&g_pendingBikeChildOverride, 0, 0) != 0) {
            LOG_WARNING("[GearCustomization] Bike child override already pending");
            return false;
        }

        g_pendingHideBikeChildItemId = hideItemId;
        g_pendingApplyBikeChildItemId = applyItemId;
        g_pendingBikeChildPackedColor = packedColor & 0x00ffffff;
        InterlockedExchange(&g_pendingBikeChildOverride, 1);
        LOG_INFO("[GearCustomization] Queued bike child override hide="
            << hideItemId << " apply=" << applyItemId);
        return true;
    }

    void ProcessPendingMainThread() {
        if (InterlockedCompareExchange(&g_pendingHiddenObjectPayloadPatch, 0, 0) != 0) {
            if (!ApplyPendingHiddenObjectPayloadPatches()) {
                LOG_WARNING("[GearCustomization] Pending hidden-object payload patch failed");
            }
            return;
        }

        if (InterlockedExchange(&g_pendingAppearanceUpdate, 0) != 0) {
            uint16_t appearanceData[APPEARANCE_WORD_COUNT] = {};
            {
                std::lock_guard<std::mutex> lock(g_pendingAppearanceMutex);
                memcpy(appearanceData, g_pendingAppearanceData, sizeof(appearanceData));
            }

            Logging::WriteImmediate("[GearCustomization] processing direct appearance update");
            if (!ApplyAppearanceUpdateOnGameThread(appearanceData)) {
                LOG_WARNING("[GearCustomization] Pending direct appearance update failed");
                Logging::WriteImmediate("[GearCustomization] direct appearance update failed");
            }
            return;
        }

        if (InterlockedExchange(&g_pendingBikeGearSetChildReplacement, 0) != 0) {
            if (!ReplaceCurrentBikeGearSetChildOnGameThread(
                    g_pendingReplaceBikeChildFromItemId,
                    g_pendingReplaceBikeChildToItemId)) {
                LOG_WARNING("[GearCustomization] Pending gear-set child replacement failed");
            }
            return;
        }

        if (InterlockedExchange(&g_pendingBikeChildOverride, 0) != 0) {
            void* bikeEntity = Respawn::GetBikePointer();
            if (!bikeEntity || IsBadReadPtr(bikeEntity, 0xb20)) {
                LOG_ERROR("[GearCustomization] Bike child override skipped because the current bike is unavailable");
                return;
            }

            uintptr_t sceneRootAddr = reinterpret_cast<uintptr_t>(bikeEntity) + 0x678;
            if (IsBadReadPtr(reinterpret_cast<void*>(sceneRootAddr), sizeof(void*))) {
                LOG_ERROR("[GearCustomization] Bike child override could not read scene root");
                return;
            }

            void* sceneRoot = *reinterpret_cast<void**>(sceneRootAddr);
            if (!sceneRoot) {
                LOG_ERROR("[GearCustomization] Bike child override has no scene root");
                return;
            }

            LOG_INFO("[GearCustomization] Applying bike child item " << g_pendingApplyBikeChildItemId);
            if (!CallApplyBikeHiddenObjectMeshTint(
                    bikeEntity,
                    g_pendingApplyBikeChildItemId,
                    g_pendingBikeChildPackedColor)) {
                LOG_WARNING("[GearCustomization] Bike child item " << g_pendingApplyBikeChildItemId << " could not be applied");
                return;
            }

            if (g_pendingHideBikeChildItemId != 0
                && g_pendingHideBikeChildItemId != g_pendingApplyBikeChildItemId) {
                LOG_INFO("[GearCustomization] Replacement applied; hiding previous bike child item " << g_pendingHideBikeChildItemId);
                if (!CallHideSceneObjectForGearVisual(sceneRoot, g_pendingHideBikeChildItemId)) {
                    LOG_WARNING("[GearCustomization] Bike child item " << g_pendingHideBikeChildItemId << " could not be hidden");
                }
            }
        }
    }

    bool DumpBikeGearSetEntry(uint16_t setId) {
        if (!g_initialized || !g_getGearSlotEntry) {
            LOG_ERROR("[GearCustomization] Gear-set dump unavailable because module is not ready");
            return false;
        }

        void* catalog = GetBikeVisualCatalog();
        if (!catalog) {
            LOG_ERROR("[GearCustomization] Gear-set dump could not get bike visual catalog");
            return false;
        }

        void* entryWrapper[2] = {};
        if (!CallGetGearSlotEntry(catalog, entryWrapper, setId)) {
            return false;
        }

        uint8_t* entry = reinterpret_cast<uint8_t*>(entryWrapper[0]);
        if (!entry || IsBadReadPtr(entry, 0x20)) {
            LOG_ERROR("[GearCustomization] Gear-set entry " << setId << " is unavailable");
            return false;
        }

        LOG_INFO("[GearCustomization] Gear-set dump for set " << setId);
        for (uintptr_t offset = 0; offset < 0x20; offset += 4) {
            const uint32_t value = *reinterpret_cast<uint32_t*>(entry + offset);
            LOG_INFO("[GearCustomization]   +" << std::hex << offset << " = 0x" << value);
        }
        return true;
    }

    bool DumpHiddenObjectEntry(uint16_t itemId) {
        if (!g_initialized || !g_getHiddenObjectEntry) {
            LOG_ERROR("[GearCustomization] Hidden-object dump unavailable because module is not ready");
            return false;
        }

        void* catalog = GetBikeVisualCatalog();
        if (!catalog) {
            LOG_ERROR("[GearCustomization] Hidden-object dump could not get bike visual catalog");
            return false;
        }

        void* entryWrapper[2] = {};
        if (!CallGetHiddenObjectEntry(catalog, entryWrapper, itemId)) {
            return false;
        }

        uint8_t* entry = reinterpret_cast<uint8_t*>(entryWrapper[0]);
        if (!entry || IsBadReadPtr(entry, 0x20)) {
            LOG_ERROR("[GearCustomization] Hidden-object entry " << itemId << " is unavailable");
            return false;
        }

        LOG_INFO("[GearCustomization] Hidden-object dump for item " << itemId);
        for (uintptr_t offset = 0; offset < 0x20; offset += 4) {
            const uint32_t value = *reinterpret_cast<uint32_t*>(entry + offset);
            LOG_INFO("[GearCustomization]   +" << std::hex << offset << " = 0x" << value);
        }
        return true;
    }

    bool GetHiddenObjectName(uint16_t itemId, std::string* outName) {
        if (!outName || !g_initialized || !g_getHiddenObjectEntry) {
            return false;
        }

        void* catalog = GetBikeVisualCatalog();
        if (!catalog) {
            return false;
        }

        void* entryWrapper[2] = {};
        if (!CallGetHiddenObjectEntry(catalog, entryWrapper, itemId)) {
            return false;
        }

        uint8_t* entry = reinterpret_cast<uint8_t*>(entryWrapper[0]);
        if (!entry || IsBadReadPtr(entry, 0x08)) {
            return false;
        }

        uint8_t* nameHolder = *reinterpret_cast<uint8_t**>(entry + 0x04);
        if (!nameHolder || IsBadReadPtr(nameHolder + 0x0c, 1)) {
            return false;
        }

        *outName = reinterpret_cast<const char*>(nameHolder + 0x0c);
        return true;
    }

    void SetBikeChildColorOverride(uint16_t itemId, uint32_t packedColor) {
        if (itemId == 0) {
            return;
        }
        std::lock_guard<std::mutex> lock(g_catalogMutationMutex);
        g_bikeChildColorOverrides[itemId] = packedColor & 0x00ffffff;
    }

    void ClearBikeChildColorOverride(uint16_t itemId) {
        std::lock_guard<std::mutex> lock(g_catalogMutationMutex);
        g_bikeChildColorOverrides.erase(itemId);
    }
}
