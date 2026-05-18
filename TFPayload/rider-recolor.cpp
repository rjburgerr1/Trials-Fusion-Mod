#include "pch.h"
#include "rider-recolor.h"
#include "respawn.h"
#include "logging.h"
#include "base-address.h"
#include <Windows.h>
#include <MinHook.h>
#include <sstream>
#include <cstddef>

namespace RiderRecolor {
    namespace {
        static bool g_initialized = false;
        static uintptr_t g_baseAddress = 0;

        // Combined bike+rider race entity offsets.
        static constexpr uintptr_t SCENE_HOST_OFFSET = 0x44;
        static constexpr uintptr_t MATERIAL_OVERRIDE_BASE_OFFSET = 0x534;
        static constexpr uintptr_t SCENE_ROOT_OFFSET = 0x678;
        static constexpr size_t MATERIAL_OVERRIDE_COUNT = 6;

        // Rider customization state:
        //   *(g_pGameManager + 0x120) + 0x10 -> rider customization state.
        static constexpr uintptr_t GAME_MANAGER_RVA_UPLAY = 0x0104b308;
        static constexpr uintptr_t GAME_MANAGER_RVA_STEAM = 0x0104d308;
        static constexpr uintptr_t CUSTOMIZATION_OWNER_OFFSET = 0x120;
        static constexpr uintptr_t CUSTOMIZATION_STATE_OFFSET = 0x10;
        static constexpr uintptr_t ACTIVE_RIDER_GEAR_OFFSET = 0x108;
        static constexpr uintptr_t RIDER_GEAR_COLORS_OFFSET = 0x110;
        static constexpr uintptr_t RIDER_GEAR_COLORS_NEW_OFFSET = 0x129;
        static constexpr uintptr_t EXPANDED_RIDER_COLOR_TABLE_OFFSET = 0xd18;

        // Current proven torso lane.
        static constexpr uint8_t TORSO_COLOR_GROUP = 1;
        static constexpr uint8_t TORSO_BODY_SLOT = 1;
        static constexpr uintptr_t TORSO_GEAR_OFFSET =
            ACTIVE_RIDER_GEAR_OFFSET + TORSO_BODY_SLOT * sizeof(uint16_t);
        static constexpr uintptr_t TORSO_COLOR_OFFSET =
            RIDER_GEAR_COLORS_OFFSET + ((TORSO_COLOR_GROUP - 1) * 3 + TORSO_BODY_SLOT) * sizeof(uint32_t);

        // Uplay 0x00A025A0 -> Steam 0x00441960.
        static constexpr uintptr_t SET_RIDER_GEAR_CUSTOM_COLOR_RVA_UPLAY = 0x003025a0;
        static constexpr uintptr_t SET_RIDER_GEAR_CUSTOM_COLOR_RVA_STEAM = 0x00301960;

        // Uplay 0x00914080 -> Steam 0x00353970.
        static constexpr uintptr_t INIT_MATERIAL_OVERRIDES_RVA_UPLAY = 0x00214080;
        static constexpr uintptr_t INIT_MATERIAL_OVERRIDES_RVA_STEAM = 0x00213970;

        // Uplay 0x009CC270 -> Steam 0x0040B8B0.
        // Game-owned rider visual manager frame helper; this receives the real
        // RiderVisualManager* in ECX and is called from the normal update path.
        static constexpr uintptr_t RIDER_VISUAL_UPDATE_HELPER_RVA_UPLAY = 0x002cc270;
        static constexpr uintptr_t RIDER_VISUAL_UPDATE_HELPER_RVA_STEAM = 0x002cb8b0;

        // Uplay 0x009CE720 -> Steam 0x0040DD60.
        static constexpr uintptr_t REBUILD_RIDER_GEAR_GROUP_RVA_UPLAY = 0x002ce720;
        static constexpr uintptr_t REBUILD_RIDER_GEAR_GROUP_RVA_STEAM = 0x002cdd60;

        static constexpr size_t OBJECT_SCAN_BYTES = 0x400;
        static constexpr size_t POINTER_SCAN_BYTES = 0x100;
        static constexpr uintptr_t TRANSFORM_INLINE_BEGIN = 0x50;
        static constexpr uintptr_t TRANSFORM_INLINE_END = 0x7c;

        using SetRiderGearCustomColorFunc =
            void(__thiscall*)(void* thisPtr, uint8_t colorGroup, uint8_t bodySlot, uint32_t rgb24);
        using InitMaterialOverridesFunc = void(__fastcall*)(void* riderEntity);
        using RiderVisualUpdateHelperFunc = void(__fastcall*)(void* thisPtr);
        using RebuildRiderGearGroupFunc = void(__thiscall*)(void* thisPtr, uint16_t gearId);

        static Color3 g_requestedColors[3] = {
            { 1.0f, 1.0f, 1.0f },
            { 1.0f, 1.0f, 1.0f },
            { 1.0f, 1.0f, 1.0f }
        };
        static volatile LONG g_pendingRiderGearRebuild = 0;
        static bool g_riderVisualHookInstalled = false;
        static void* g_riderVisualUpdateTarget = nullptr;
        static void* g_lastRiderVisualManager = nullptr;
        static RiderVisualUpdateHelperFunc g_originalRiderVisualUpdateHelper = nullptr;
        static RebuildRiderGearGroupFunc g_rebuildRiderGearGroup = nullptr;

        static void __fastcall Hook_RiderVisualUpdateHelper(void* thisPtr);

        static uintptr_t GetGameManagerRva() {
            return BaseAddress::IsSteamVersion()
                ? GAME_MANAGER_RVA_STEAM
                : GAME_MANAGER_RVA_UPLAY;
        }

        static uintptr_t GetSetRiderGearCustomColorRva() {
            return BaseAddress::IsSteamVersion()
                ? SET_RIDER_GEAR_CUSTOM_COLOR_RVA_STEAM
                : SET_RIDER_GEAR_CUSTOM_COLOR_RVA_UPLAY;
        }

        static uintptr_t GetInitMaterialOverridesRva() {
            return BaseAddress::IsSteamVersion()
                ? INIT_MATERIAL_OVERRIDES_RVA_STEAM
                : INIT_MATERIAL_OVERRIDES_RVA_UPLAY;
        }

        static uintptr_t GetRiderVisualUpdateHelperRva() {
            return BaseAddress::IsSteamVersion()
                ? RIDER_VISUAL_UPDATE_HELPER_RVA_STEAM
                : RIDER_VISUAL_UPDATE_HELPER_RVA_UPLAY;
        }

        static uintptr_t GetRebuildRiderGearGroupRva() {
            return BaseAddress::IsSteamVersion()
                ? REBUILD_RIDER_GEAR_GROUP_RVA_STEAM
                : REBUILD_RIDER_GEAR_GROUP_RVA_UPLAY;
        }

        static bool TryReadPointer(void* base, uintptr_t offset, void*& outValue) {
            outValue = nullptr;
            if (!base) {
                return false;
            }

            __try {
                outValue = *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(base) + offset);
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                outValue = nullptr;
                return false;
            }
        }

        template <typename T>
        static bool TryReadValue(void* base, uintptr_t offset, T& outValue) {
            outValue = T{};
            if (!base) {
                return false;
            }

            __try {
                outValue = *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(base) + offset);
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                outValue = T{};
                return false;
            }
        }

        template <typename T>
        static bool TryWriteValue(void* base, uintptr_t offset, const T& value) {
            if (!base) {
                return false;
            }

            __try {
                *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(base) + offset) = value;
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                return false;
            }
        }

        template <typename T, size_t N>
        static bool TryReadArray(void* base, uintptr_t offset, T(&outValues)[N]) {
            if (!base) {
                return false;
            }

            __try {
                auto* source = reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(base) + offset);
                for (size_t i = 0; i < N; ++i) {
                    outValues[i] = source[i];
                }
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                for (size_t i = 0; i < N; ++i) {
                    outValues[i] = T{};
                }
                return false;
            }
        }

        static bool TryWriteRgbFloatTriple(void* base, size_t offset, const Color3& color) {
            if (!base) {
                return false;
            }

            __try {
                auto* floats = reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(base) + offset);
                floats[0] = color.r;
                floats[1] = color.g;
                floats[2] = color.b;
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                return false;
            }
        }

        static bool LooksReadable(void* value) {
            if (!value) {
                return false;
            }

            __try {
                volatile uint8_t probe = *reinterpret_cast<uint8_t*>(value);
                (void)probe;
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                return false;
            }
        }

        static bool LooksLikeGameObject(void* value) {
            if (!LooksReadable(value) || g_baseAddress == 0) {
                return false;
            }

            void* vtable = nullptr;
            if (!TryReadPointer(value, 0, vtable) || !vtable) {
                return false;
            }

            const uintptr_t vtableAddress = reinterpret_cast<uintptr_t>(vtable);
            return vtableAddress >= g_baseAddress
                && vtableAddress < g_baseAddress + 0x02000000;
        }

        static bool ApproxEqual(float a, float b, float epsilon = 0.006f) {
            const float delta = a - b;
            return delta >= -epsilon && delta <= epsilon;
        }

        static uint8_t ClampColorByte(float value) {
            if (value < 0.0f) {
                value = 0.0f;
            }
            if (value > 1.0f) {
                value = 1.0f;
            }
            return static_cast<uint8_t>(value * 255.0f + 0.5f);
        }

        static uint32_t PackRgb24(const Color3& color) {
            return (static_cast<uint32_t>(ClampColorByte(color.r)) << 16)
                | (static_cast<uint32_t>(ClampColorByte(color.g)) << 8)
                | static_cast<uint32_t>(ClampColorByte(color.b));
        }

        static Color3 UnpackRiderColor(uint32_t packedColor) {
            return {
                static_cast<float>((packedColor >> 16) & 0xff) / 255.0f,
                static_cast<float>((packedColor >> 8) & 0xff) / 255.0f,
                static_cast<float>(packedColor & 0xff) / 255.0f
            };
        }

        static void* GetCustomizationState(void** outGameManager = nullptr, void** outOwner = nullptr) {
            if (g_baseAddress == 0) {
                return nullptr;
            }

            void* gameManager = nullptr;
            if (!TryReadPointer(reinterpret_cast<void*>(g_baseAddress), GetGameManagerRva(), gameManager) || !gameManager) {
                return nullptr;
            }
            if (outGameManager) {
                *outGameManager = gameManager;
            }

            void* owner = nullptr;
            if (!TryReadPointer(gameManager, CUSTOMIZATION_OWNER_OFFSET, owner) || !owner) {
                return nullptr;
            }
            if (outOwner) {
                *outOwner = owner;
            }

            void* customizationState = nullptr;
            TryReadPointer(owner, CUSTOMIZATION_STATE_OFFSET, customizationState);
            return customizationState;
        }

        static bool TrySetRiderGearCustomColor(
            void* customizationState,
            uint8_t colorGroup,
            uint8_t bodySlot,
            uint32_t rgb24
        ) {
            if (!customizationState || g_baseAddress == 0) {
                return false;
            }

            auto setRiderGearCustomColor = reinterpret_cast<SetRiderGearCustomColorFunc>(
                g_baseAddress + GetSetRiderGearCustomColorRva()
            );

            __try {
                setRiderGearCustomColor(customizationState, colorGroup, bodySlot, rgb24);
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                return false;
            }
        }

        static bool TryInitMaterialOverrides(void* entity) {
            if (!entity || g_baseAddress == 0) {
                return false;
            }

            auto initMaterialOverrides = reinterpret_cast<InitMaterialOverridesFunc>(
                g_baseAddress + GetInitMaterialOverridesRva()
            );

            __try {
                initMaterialOverrides(entity);
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                return false;
            }
        }

        static bool TryCallRebuildRiderGearGroup(void* riderVisualManager, uint16_t gearId) {
            if (!riderVisualManager || gearId == 0 || !g_rebuildRiderGearGroup) {
                return false;
            }

            __try {
                g_rebuildRiderGearGroup(riderVisualManager, gearId);
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                return false;
            }
        }

        static void QueueRiderGearRebuild(uint16_t gearId) {
            if (gearId == 0) {
                return;
            }
            InterlockedExchange(&g_pendingRiderGearRebuild, static_cast<LONG>(gearId));
        }

        static void ProcessPendingRiderGearRebuild(void* riderVisualManager) {
            if (!riderVisualManager || !LooksReadable(riderVisualManager)) {
                return;
            }

            g_lastRiderVisualManager = riderVisualManager;
            const LONG gearId = InterlockedExchange(&g_pendingRiderGearRebuild, 0);
            if (gearId <= 0) {
                return;
            }

            if (TryCallRebuildRiderGearGroup(riderVisualManager, static_cast<uint16_t>(gearId))) {
                LOG_INFO("[RiderRecolor] Rebuilt rider gear through live visual manager"
                    << " manager=0x" << std::hex << reinterpret_cast<uintptr_t>(riderVisualManager)
                    << " gearId=0x" << gearId);
            }
            else {
                LOG_WARNING("[RiderRecolor] Rider gear rebuild faulted"
                    << " manager=0x" << std::hex << reinterpret_cast<uintptr_t>(riderVisualManager)
                    << " gearId=0x" << gearId);
            }
        }

        static void __fastcall Hook_RiderVisualUpdateHelper(void* thisPtr) {
            if (g_originalRiderVisualUpdateHelper) {
                g_originalRiderVisualUpdateHelper(thisPtr);
            }

            ProcessPendingRiderGearRebuild(thisPtr);
        }

        static void InstallRiderVisualHook() {
            if (g_baseAddress == 0 || g_riderVisualHookInstalled) {
                return;
            }

            g_riderVisualUpdateTarget = reinterpret_cast<void*>(g_baseAddress + GetRiderVisualUpdateHelperRva());
            g_rebuildRiderGearGroup = reinterpret_cast<RebuildRiderGearGroupFunc>(
                g_baseAddress + GetRebuildRiderGearGroupRva()
            );

            MH_STATUS status = MH_CreateHook(
                g_riderVisualUpdateTarget,
                reinterpret_cast<LPVOID>(&Hook_RiderVisualUpdateHelper),
                reinterpret_cast<LPVOID*>(&g_originalRiderVisualUpdateHelper)
            );
            if (status != MH_OK) {
                LOG_WARNING("[RiderRecolor] Rider visual update hook create failed: " << status);
                return;
            }

            status = MH_EnableHook(g_riderVisualUpdateTarget);
            if (status != MH_OK) {
                LOG_WARNING("[RiderRecolor] Rider visual update hook enable failed: " << status);
                return;
            }

            g_riderVisualHookInstalled = true;
            LOG_INFO("[RiderRecolor] Rider visual update hook installed"
                << " update=0x" << std::hex << reinterpret_cast<uintptr_t>(g_riderVisualUpdateTarget)
                << " rebuild=0x" << reinterpret_cast<uintptr_t>(g_rebuildRiderGearGroup));
        }

        static bool IsColorLike(float value) {
            return value >= 0.0f && value <= 1.25f;
        }

        static void LogColorLikeTriples(const char* label, void* base, size_t byteCount, size_t& emitted) {
            if (!base || emitted >= 24) {
                return;
            }

            for (size_t offset = 0; offset + sizeof(float) * 3 <= byteCount && emitted < 24; offset += sizeof(float)) {
                float r = 0.0f;
                float g = 0.0f;
                float b = 0.0f;
                if (!TryReadValue(base, offset, r)
                    || !TryReadValue(base, offset + sizeof(float), g)
                    || !TryReadValue(base, offset + sizeof(float) * 2, b)
                    || !IsColorLike(r)
                    || !IsColorLike(g)
                    || !IsColorLike(b)) {
                    continue;
                }

                const bool hasSignal = r > 0.01f || g > 0.01f || b > 0.01f;
                const bool notUnitTriple = !ApproxEqual(r, 1.0f) || !ApproxEqual(g, 1.0f) || !ApproxEqual(b, 1.0f);
                if (!hasSignal || !notUnitTriple) {
                    continue;
                }

                LOG_INFO("[RiderRecolor] color-like float triple"
                    << " target=" << label
                    << " base=0x" << std::hex << reinterpret_cast<uintptr_t>(base)
                    << " offset=0x" << offset
                    << " value=(" << std::dec << r << "," << g << "," << b << ")");
                ++emitted;
            }
        }

        static bool TryPatchRgbFloatTriples(
            const char* label,
            void* base,
            size_t byteCount,
            const Color3& oldColor,
            const Color3& newColor
        ) {
            bool patchedAny = false;
            if (!base) {
                return false;
            }

            for (size_t offset = 0; offset + sizeof(float) * 3 <= byteCount; offset += sizeof(float)) {
                float r = 0.0f;
                float g = 0.0f;
                float b = 0.0f;
                if (!TryReadValue(base, offset, r)
                    || !TryReadValue(base, offset + sizeof(float), g)
                    || !TryReadValue(base, offset + sizeof(float) * 2, b)
                    || !ApproxEqual(r, oldColor.r)
                    || !ApproxEqual(g, oldColor.g)
                    || !ApproxEqual(b, oldColor.b)) {
                    continue;
                }

                LOG_INFO("[RiderRecolor] Patching live RGB float triple"
                    << " target=" << label
                    << " base=0x" << std::hex << reinterpret_cast<uintptr_t>(base)
                    << " offset=0x" << offset
                    << " old=(" << std::dec << r << "," << g << "," << b << ")"
                    << " new=(" << newColor.r << "," << newColor.g << "," << newColor.b << ")");
                patchedAny = TryWriteRgbFloatTriple(base, offset, newColor) || patchedAny;
            }

            return patchedAny;
        }

        static bool WasSeen(void** seen, size_t seenCount, void* value) {
            for (size_t i = 0; i < seenCount; ++i) {
                if (seen[i] == value) {
                    return true;
                }
            }
            return false;
        }

        static bool TryPatchPointerGraphRgbFloats(
            const char* label,
            void* root,
            const Color3& oldColor,
            const Color3& newColor,
            size_t& colorTripleLogCount
        ) {
            struct QueueEntry {
                void* ptr;
                uint8_t depth;
            };

            QueueEntry queue[512] = {};
            void* seen[512] = {};
            size_t head = 0;
            size_t tail = 0;
            size_t seenCount = 0;
            bool patchedAny = false;

            if (!LooksLikeGameObject(root)) {
                return false;
            }

            queue[tail++] = { root, 0 };
            seen[seenCount++] = root;

            while (head < tail) {
                const QueueEntry current = queue[head++];
                patchedAny = TryPatchRgbFloatTriples(
                    label,
                    current.ptr,
                    OBJECT_SCAN_BYTES,
                    oldColor,
                    newColor
                ) || patchedAny;

                if (!patchedAny) {
                    LogColorLikeTriples(label, current.ptr, 0x180, colorTripleLogCount);
                }

                if (current.depth >= 3) {
                    continue;
                }

                for (size_t offset = 0; offset <= POINTER_SCAN_BYTES && tail < _countof(queue); offset += sizeof(void*)) {
                    if (offset >= TRANSFORM_INLINE_BEGIN && offset <= TRANSFORM_INLINE_END) {
                        continue;
                    }

                    void* child = nullptr;
                    if (!TryReadPointer(current.ptr, offset, child)
                        || !LooksLikeGameObject(child)
                        || WasSeen(seen, seenCount, child)) {
                        continue;
                    }

                    queue[tail++] = { child, static_cast<uint8_t>(current.depth + 1) };
                    if (seenCount < _countof(seen)) {
                        seen[seenCount++] = child;
                    }
                }
            }

            LOG_INFO("[RiderRecolor] pointer graph RGB scan"
                << " rootLabel=" << label
                << " root=0x" << std::hex << reinterpret_cast<uintptr_t>(root)
                << " visited=" << std::dec << seenCount
                << " patched=" << patchedAny);
            return patchedAny;
        }

        static bool TryPatchLiveTorsoMaterialColor(uint32_t oldPackedColor, uint32_t newPackedColor) {
            RiderEntitySnapshot snapshot{};
            if (!CaptureCurrentSnapshot(snapshot)) {
                LOG_WARNING("[RiderRecolor] Live torso material patch failed: rider snapshot unavailable");
                return false;
            }

            const Color3 oldColor = UnpackRiderColor(oldPackedColor);
            const Color3 newColor = UnpackRiderColor(newPackedColor);
            bool patchedAny = false;
            size_t colorTripleLogCount = 0;

            void* torsoOverride = snapshot.materialOverrides[1];
            patchedAny = TryPatchRgbFloatTriples(
                "torsoOverride",
                torsoOverride,
                OBJECT_SCAN_BYTES,
                oldColor,
                newColor
            ) || patchedAny;

            patchedAny = TryPatchPointerGraphRgbFloats(
                "torsoOverride",
                torsoOverride,
                oldColor,
                newColor,
                colorTripleLogCount
            ) || patchedAny;

            patchedAny = TryPatchPointerGraphRgbFloats(
                "sceneRoot",
                snapshot.sceneRoot,
                oldColor,
                newColor,
                colorTripleLogCount
            ) || patchedAny;

            LOG_INFO("[RiderRecolor] Live torso material float patch"
                << " oldPacked=0x" << std::hex << oldPackedColor
                << " newPacked=0x" << newPackedColor
                << " patched=" << std::dec << patchedAny);
            return patchedAny;
        }

        static bool TryRefreshRiderEntityMaterials() {
            void* entity = GetCurrentRiderEntity();
            if (!entity) {
                LOG_WARNING("[RiderRecolor] Entity material refresh failed: rider entity unavailable");
                return false;
            }

            if (!TryInitMaterialOverrides(entity)) {
                LOG_WARNING("[RiderRecolor] Entity material override refresh faulted");
                return false;
            }

            LOG_INFO("[RiderRecolor] Refreshed entity material overrides"
                << " entity=0x" << std::hex << reinterpret_cast<uintptr_t>(entity));
            return true;
        }

        static size_t RegionToIndex(Region region) {
            return static_cast<size_t>(region);
        }

        static const char* RegionName(Region region) {
            switch (region) {
            case Region::Legs:
                return "legs";
            case Region::Torso:
                return "torso";
            case Region::Head:
                return "head";
            default:
                return "unknown";
            }
        }
    }

    bool Initialize(uintptr_t baseAddress) {
        g_baseAddress = baseAddress;
        g_initialized = baseAddress != 0;
        InstallRiderVisualHook();
        LOG_INFO("[RiderRecolor] Initialized" << (g_initialized ? "" : " (invalid base address)"));
        return g_initialized;
    }

    void Shutdown() {
        if (g_riderVisualHookInstalled && g_riderVisualUpdateTarget) {
            MH_DisableHook(g_riderVisualUpdateTarget);
        }
        g_riderVisualHookInstalled = false;
        g_riderVisualUpdateTarget = nullptr;
        g_originalRiderVisualUpdateHelper = nullptr;
        g_rebuildRiderGearGroup = nullptr;
        g_lastRiderVisualManager = nullptr;
        InterlockedExchange(&g_pendingRiderGearRebuild, 0);
        g_initialized = false;
        g_baseAddress = 0;
    }

    void* GetCurrentRiderEntity() {
        return Respawn::GetBikePointer();
    }

    bool CaptureCurrentSnapshot(RiderEntitySnapshot& outSnapshot) {
        outSnapshot = RiderEntitySnapshot{};

        void* entity = GetCurrentRiderEntity();
        if (!entity) {
            return false;
        }

        outSnapshot.entity = entity;
        TryReadPointer(entity, SCENE_ROOT_OFFSET, outSnapshot.sceneRoot);
        TryReadPointer(entity, SCENE_HOST_OFFSET, outSnapshot.sceneHost);

        for (size_t i = 0; i < MATERIAL_OVERRIDE_COUNT; ++i) {
            TryReadPointer(
                entity,
                MATERIAL_OVERRIDE_BASE_OFFSET + i * sizeof(void*),
                outSnapshot.materialOverrides[i]
            );
        }

        return outSnapshot.sceneRoot != nullptr;
    }

    bool CaptureCurrentCustomizationSnapshot(RiderCustomizationSnapshot& outSnapshot) {
        outSnapshot = RiderCustomizationSnapshot{};

        void* customizationState = GetCustomizationState();
        if (!customizationState) {
            return false;
        }

        outSnapshot.customizationState = customizationState;
        return TryReadArray(customizationState, ACTIVE_RIDER_GEAR_OFFSET, outSnapshot.activeRiderGear)
            && TryReadArray(customizationState, RIDER_GEAR_COLORS_OFFSET, outSnapshot.riderGearColors)
            && TryReadArray(customizationState, RIDER_GEAR_COLORS_NEW_OFFSET, outSnapshot.riderGearColorsNew)
            && TryReadArray(customizationState, EXPANDED_RIDER_COLOR_TABLE_OFFSET, outSnapshot.expandedColorTable);
    }

    void DebugDumpCurrentRiderState() {
        RiderEntitySnapshot snapshot{};
        if (!CaptureCurrentSnapshot(snapshot)) {
            LOG_WARNING("[RiderRecolor] No current rider entity available");
            return;
        }

        LOG_INFO("[RiderRecolor] ===== Rider entity snapshot =====");
        LOG_INFO("[RiderRecolor] entity:              0x" << std::hex << reinterpret_cast<uintptr_t>(snapshot.entity));
        LOG_INFO("[RiderRecolor] scene host (+0x44):  0x" << std::hex << reinterpret_cast<uintptr_t>(snapshot.sceneHost));
        LOG_INFO("[RiderRecolor] scene root (+0x678): 0x" << std::hex << reinterpret_cast<uintptr_t>(snapshot.sceneRoot));

        for (size_t i = 0; i < MATERIAL_OVERRIDE_COUNT; ++i) {
            LOG_INFO("[RiderRecolor] material override [" << std::dec << i << "] (+0x"
                << std::hex << (MATERIAL_OVERRIDE_BASE_OFFSET + i * sizeof(void*))
                << "): 0x" << reinterpret_cast<uintptr_t>(snapshot.materialOverrides[i]));
        }

        RiderCustomizationSnapshot customization{};
        if (CaptureCurrentCustomizationSnapshot(customization)) {
            LOG_INFO("[RiderRecolor] customization state: 0x" << std::hex
                << reinterpret_cast<uintptr_t>(customization.customizationState));
            LOG_INFO("[RiderRecolor] active rider gear: "
                << std::dec
                << customization.activeRiderGear[0] << ", "
                << customization.activeRiderGear[1] << ", "
                << customization.activeRiderGear[2]);

            std::ostringstream colors;
            colors << "[RiderRecolor] riderGearColors (+0x110):";
            for (uint32_t value : customization.riderGearColors) {
                colors << " " << std::hex << value;
            }
            LOG_INFO(colors.str());
        }
        else {
            void* gameManager = nullptr;
            void* owner = nullptr;
            void* customizationState = GetCustomizationState(&gameManager, &owner);
            LOG_INFO("[RiderRecolor] customization state unavailable"
                << " gameManager=0x" << std::hex << reinterpret_cast<uintptr_t>(gameManager)
                << " owner=0x" << reinterpret_cast<uintptr_t>(owner)
                << " state=0x" << reinterpret_cast<uintptr_t>(customizationState));
        }

        LOG_INFO("[RiderRecolor] ===============================");
    }

    bool CaptureTorsoProbeBaseline() {
        LOG_INFO("[RiderRecolor] Torso baseline probe was removed; use DebugDumpCurrentRiderState");
        RiderEntitySnapshot snapshot{};
        return CaptureCurrentSnapshot(snapshot);
    }

    bool DiffTorsoProbeAgainstBaseline() {
        LOG_INFO("[RiderRecolor] Torso diff probe was removed; live RGB scan runs when setting torso color");
        return false;
    }

    void ClearTorsoProbeBaseline() {
        LOG_INFO("[RiderRecolor] Torso baseline probe was removed");
    }

    bool SetRegionColor(Region region, const Color3& color) {
        const size_t index = RegionToIndex(region);
        if (index >= _countof(g_requestedColors)) {
            return false;
        }

        g_requestedColors[index] = color;
        if (region != Region::Torso) {
            LOG_INFO("[RiderRecolor] Requested " << RegionName(region)
                << " color = (" << color.r << ", " << color.g << ", " << color.b << ")"
                << " [only torso is mapped]");
            return false;
        }

        void* customizationState = GetCustomizationState();
        if (!customizationState) {
            LOG_WARNING("[RiderRecolor] Torso color write failed: customization state unavailable");
            return false;
        }

        const uint32_t rgb24 = PackRgb24(color);
        const uint32_t expectedPackedColor = 0x01000000u | rgb24;
        uint16_t torsoGearId = 0;
        TryReadValue(customizationState, TORSO_GEAR_OFFSET, torsoGearId);

        if (!TryWriteValue(customizationState, TORSO_COLOR_OFFSET, expectedPackedColor)) {
            LOG_WARNING("[RiderRecolor] Torso color table write faulted");
            return false;
        }

        uint32_t livePackedColor = 0;
        TryReadValue(customizationState, TORSO_COLOR_OFFSET, livePackedColor);

        LOG_INFO("[RiderRecolor] Set torso color table entry"
            << " customization=0x" << std::hex << reinterpret_cast<uintptr_t>(customizationState)
            << " offset=0x" << TORSO_COLOR_OFFSET
            << " group=" << std::dec << static_cast<unsigned>(TORSO_COLOR_GROUP)
            << " bodySlot=" << static_cast<unsigned>(TORSO_BODY_SLOT)
            << " torsoGear=0x" << std::hex << torsoGearId
            << " rgb24=0x" << std::hex << rgb24
            << " expectedPacked=0x" << expectedPackedColor
            << " livePacked=0x" << livePackedColor
            << " liveRefresh=queued");

        QueueRiderGearRebuild(torsoGearId);
        return true;
    }

    bool GetRequestedRegionColor(Region region, Color3& outColor) {
        const size_t index = RegionToIndex(region);
        if (index >= _countof(g_requestedColors)) {
            return false;
        }

        outColor = g_requestedColors[index];
        return true;
    }

    bool GetCurrentRegionColor(Region region, Color3& outColor) {
        outColor = Color3{};
        if (region != Region::Torso) {
            return false;
        }

        void* customizationState = GetCustomizationState();
        if (!customizationState) {
            return false;
        }

        uint32_t packedColor = 0;
        if (!TryReadValue(customizationState, TORSO_COLOR_OFFSET, packedColor)) {
            return false;
        }

        outColor = UnpackRiderColor(packedColor);
        return true;
    }

    bool SetLegsColor(const Color3& color) {
        return SetRegionColor(Region::Legs, color);
    }

    bool SetTorsoColor(const Color3& color) {
        return SetRegionColor(Region::Torso, color);
    }

    bool SetHeadColor(const Color3& color) {
        return SetRegionColor(Region::Head, color);
    }

    bool RefreshTorsoMaterial() {
        void* customizationState = GetCustomizationState();
        if (!customizationState) {
            return false;
        }

        uint16_t torsoGearId = 0;
        if (!TryReadValue(customizationState, TORSO_GEAR_OFFSET, torsoGearId) || torsoGearId == 0) {
            return false;
        }

        QueueRiderGearRebuild(torsoGearId);
        return true;
    }

    void ProcessQueuedRebuildOnGameThread() {
        ProcessPendingRiderGearRebuild(g_lastRiderVisualManager);
    }

    bool SupportsLiveRgbRecolor() {
        return g_riderVisualHookInstalled;
    }
}
