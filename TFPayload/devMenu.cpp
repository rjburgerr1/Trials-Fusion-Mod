#include "pch.h"
#include "devMenu.h"
#include "devMenuSync.h"
#include "imgui/imgui.h"
#include "logging.h"
#include "respawn.h"
#include "gear-customization.h"
#include "limits.h"
#include "actionscript.h"
#include "keybindings.h"
#include "multiplayer.h"
#include "money.h"
#include "host-join.h"
#include "base-address.h"
#include "prevent-finish.h"
#include "fmod.h"
#ifdef DEVELOPMENT_MODE
#include "ui-view-explorer.h"
#endif
#include "file-unlock.h"
#include "bike-item-catalog.generated.h"
#include "gear-set-catalog.generated.h"
#include <MinHook.h>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <initializer_list>
#include <Windows.h>
#include <Xinput.h>

// Global instance
DevMenu* g_DevMenu = nullptr;

// Global checkpoint slider for dynamic range updates
static std::shared_ptr<TweakableInt> g_checkpointIndexSlider = nullptr;
static int g_lastCheckpointCount = -1;
static std::shared_ptr<TweakableButton> g_preventFinishLabel = nullptr;

// Global toggle buttons for limit controls (need to update labels when limits change)
static std::shared_ptr<TweakableButton> g_toggleFaultLimitButton = nullptr;
static std::shared_ptr<TweakableButton> g_toggleTimeLimitButton = nullptr;
static bool g_waitingForGamepadBind = false;
static bool g_gamepadCaptureReady = false;
static Keybindings::Action g_waitingGamepadAction = Keybindings::Action::InstantFinish;
static void UpdateGamepadBindingCapture();

// Helper to check if an ID belongs to Bike folder
static bool IsBikeId(int id) {
    // Bike folder ID: 169
    // Bike sub-items: 170-213 (includes Engine, Transmission, Properties, Suspension subfolders)
    return id >= 169 && id <= 213;
}

// Helper to check if an ID belongs to Rider folder
static bool IsRiderId(int id) {
    // Rider folder ID: 214
    // Rider sub-items: 215-217 (includes Properties subfolder)
    return id >= 214 && id <= 217;
}

static bool IsStartupGateId(int id) {
    return id == 80   // AllBikesUnlocked
        || id == 132  // Enable Advert events for content packs
        || id == 133  // ContentPack Owned
        || id == 134  // ContentPack Coming Soon
        || id == 344  // FMXTricksUnlocked
        || id == 491; // AllTracksUnlocked
}

namespace {
    static constexpr uintptr_t GAME_MANAGER_GLOBAL_RVA_UPLAY = 0x104b308;
    static constexpr uintptr_t GAME_MANAGER_GLOBAL_RVA_STEAM = 0x104d308;
    static constexpr uintptr_t GAME_MANAGER_EDITOR_MANAGER_OFFSET = 0x104;
    static constexpr uintptr_t COLLECT_SCENE_OBJECTS_BY_TYPE_RVA_UPLAY = 0x6a3000;
    static constexpr uintptr_t COLLECT_SCENE_OBJECTS_BY_TYPE_RVA_STEAM = 0x6a19f0;
    static constexpr uintptr_t UPDATE_OBJECT_VARIATION_TRANSFORM_RVA_UPLAY = 0x148250;
    static constexpr uintptr_t UPDATE_OBJECT_VARIATION_TRANSFORM_RVA_STEAM = 0x147e80;
    static constexpr uintptr_t CREATE_OBJECT_VARIATION_RVA_UPLAY = 0x14a570;
    static constexpr uintptr_t CREATE_OBJECT_VARIATION_RVA_STEAM = 0x14a1a0;
    static constexpr uintptr_t RENDER_GRAPHICS_AND_UPDATE_CAMERA_RVA_UPLAY = 0x2027d0;
    static constexpr uintptr_t RENDER_GRAPHICS_AND_UPDATE_CAMERA_RVA_STEAM = 0x202180;
    static constexpr uintptr_t APPLY_SCENE_OBJECT_TRANSFORM_RVA_UPLAY = 0x2028b0;
    static constexpr uintptr_t APPLY_SCENE_OBJECT_TRANSFORM_RVA_STEAM = 0x202260;
    static constexpr uintptr_t SCALE_SELECTED_OBJECTS_DELTA_RVA_UPLAY = 0x0f3db0;
    static constexpr uintptr_t SCALE_SELECTED_OBJECTS_DELTA_RVA_STEAM = 0x0f3740;
    static constexpr uintptr_t RESET_SELECTED_OBJECTS_SCALE_RVA_UPLAY = 0x0f3e20;
    static constexpr uintptr_t RESET_SELECTED_OBJECTS_SCALE_RVA_STEAM = 0x0f37b0;
    static constexpr uintptr_t SET_BACKING_OBJECT_SCALE_RVA_UPLAY = 0x1fdc10;
    static constexpr uintptr_t SET_BACKING_OBJECT_SCALE_RVA_STEAM = 0x1fd570;
    static constexpr uintptr_t SYNC_SCENE_OBJECT_TRANSFORM_RVA_UPLAY = 0x65c610;
    static constexpr uintptr_t SYNC_SCENE_OBJECT_TRANSFORM_RVA_STEAM = 0x65b210;
    static constexpr uintptr_t REFRESH_EDITOR_OBJECT_VISUAL_RVA_UPLAY = 0x096480;
    static constexpr uintptr_t REFRESH_EDITOR_OBJECT_VISUAL_RVA_STEAM = 0x095fe0;
    static constexpr uintptr_t REFRESH_SELECTION_CONSTRAINTS_RVA_UPLAY = 0x0c6b40;
    static constexpr uintptr_t REFRESH_SELECTION_CONSTRAINTS_RVA_STEAM = 0x0c6760;
    static constexpr uintptr_t REFRESH_SELECTION_BOUNDS_RVA_UPLAY = 0x138ee0;
    static constexpr uintptr_t REFRESH_SELECTION_BOUNDS_RVA_STEAM = 0x138b10;
    static constexpr uintptr_t PROCESS_SELECTION_CONSTRAINTS_RVA_UPLAY = 0x137c30;
    static constexpr uintptr_t PROCESS_SELECTION_CONSTRAINTS_RVA_STEAM = 0x137860;
    static constexpr uintptr_t CREATE_MESH_INSTANCES_RVA_UPLAY = 0x5428a0;
    static constexpr uintptr_t CREATE_MESH_INSTANCES_RVA_STEAM = 0x541ea0;
    static constexpr uintptr_t APPLY_MATERIAL_COLOR_OVERRIDES_RVA_UPLAY = 0x5424d0;
    static constexpr uintptr_t APPLY_MATERIAL_COLOR_OVERRIDES_RVA_STEAM = 0x541ad0;
    static constexpr uintptr_t TRACK_EVENT_APPLY_SCENE_OBJECT_RVA_UPLAY = 0x788f50;
    static constexpr uintptr_t TRACK_EVENT_APPLY_SCENE_OBJECT_RVA_STEAM = 0x787d70;
    static constexpr uintptr_t BUILD_OBJECT_VALUES_JSON_RVA_UPLAY = 0xc20380;
    static constexpr uintptr_t BUILD_OBJECT_VALUES_JSON_RVA_STEAM = 0x65fa40;
    static constexpr uintptr_t BUILD_TRACK_OBJECT_LISTS_RVA_UPLAY = 0x0a1c40;
    static constexpr uintptr_t BUILD_TRACK_OBJECT_LISTS_RVA_STEAM = 0x1e1710;
    static constexpr uintptr_t UPLOAD_OBJECT_VALUES_GLOBAL_RVA_UPLAY = 0x105125c;
    static constexpr uintptr_t UPLOAD_OBJECT_VALUES_GLOBAL_RVA_STEAM = 0x105325c;
    static constexpr uintptr_t SERIALIZE_TRACK_DATA_RVA_UPLAY = 0x2a9d90;
    static constexpr uintptr_t SERIALIZE_TRACK_DATA_RVA_STEAM = 0x3e9770;
    static constexpr uintptr_t SERIALIZE_TRACK_DATA_TO_PACKET_RVA_UPLAY = 0x2a8b40;
    static constexpr uintptr_t SERIALIZE_TRACK_DATA_TO_PACKET_RVA_STEAM = 0x3e8520;
    static constexpr uintptr_t PROCESS_TRACK_HASHES_AND_SAVE_RVA_UPLAY = 0x51bb20;
    static constexpr uintptr_t PROCESS_TRACK_HASHES_AND_SAVE_RVA_STEAM = 0x65b240;
    static constexpr size_t EDITOR_MANAGER_SCAN_SIZE = 0x800;
    static constexpr uint32_t MAX_EDITOR_SCENE_OBJECT_SNAPSHOT = 4092;

    typedef void(__thiscall* CollectSceneObjectsByTypeFunc)(
        void* sceneRoot,
        void* outSnapshot,
        uint32_t objectType,
        uint32_t objectSubtype,
        uint32_t useVirtualType,
        uint32_t requiredFlags);

    typedef void(__fastcall* UpdateObjectVariationTransformFunc)(int variationController);
    typedef void(__thiscall* CreateObjectVariationFunc)(void* variationController, int variationIndex);
    typedef void(__thiscall* RenderGraphicsAndUpdateCameraFunc)(void* sceneObject);
    typedef void(__cdecl* ApplySceneObjectTransformFunc)(
        void* sceneObject,
        const float* position,
        const float* rotation,
        const float* scale);
    typedef void(__thiscall* SceneObjectSetScaleFunc)(void* sceneObject, const float* scale);
    typedef void(__thiscall* SceneObjectSetPositionFunc)(void* sceneObject, const float* position);
    typedef void(__thiscall* ScaleSelectedObjectsDeltaFunc)(void* selectionManager, float delta);
    typedef void(__fastcall* ResetSelectedObjectsScaleFunc)(int selectionManager);
    typedef void(__thiscall* SetBackingObjectScaleFunc)(void* backingObject, float scale);
    typedef void(__thiscall* SyncSceneObjectTransformFunc)(
        void* sceneObject,
        const float* position,
        const float* transformBasis,
        const float* rotationOrScale);
    typedef void(__thiscall* RefreshEditorObjectVisualFunc)(void* editorManager, void* sceneObject);
    typedef void(__fastcall* RefreshSelectionConstraintsFunc)(int selectionManager);
    typedef void(__fastcall* RefreshSelectionBoundsFunc)(int selectionManager);
    typedef void(__thiscall* ProcessSelectionConstraintsFunc)(void* selectionManager, char updatePhysics);
    typedef void(__cdecl* CreateMeshInstancesFunc)(void* sceneObject, void* overrideVector, float blend);
    typedef void(__cdecl* ApplyMaterialColorOverridesFunc)(void* sceneObject, void* overrideEntry, float blend);
    typedef void(__thiscall* TrackEventApplySceneObjectFunc)(void* dispatcher, void* sceneObject, void* eventId, void* eventParam);
    typedef void(__cdecl* BuildObjectValuesJsonFunc)(void* outValue);
    typedef void(__thiscall* BuildTrackObjectListsFunc)(void* thisPtr, int* objectList, int* valueList);
    typedef uint32_t(__thiscall* SerializeTrackDataFunc)(void* thisPtr, void* outStream, char includeBikeState);
    typedef uint32_t(__cdecl* SerializeTrackDataToPacketFunc)(int packetBuilder);
    typedef uint32_t(__thiscall* ProcessTrackHashesAndSaveFunc)(void* thisPtr, uint32_t callbackOrTask, int* outputStream);
    typedef bool(__thiscall* SceneObjectGetMaterialParameterFunc)(void* sceneObject, uint32_t parameterHash, void* outParameter, uint32_t selector);
    typedef void(__thiscall* MaterialParameterSetBytesFunc)(void* parameterOwner, void* parameterHandle, const void* value, uint32_t size);

    struct EditorSceneObjectSnapshot {
        void* objects[MAX_EDITOR_SCENE_OBJECT_SNAPSHOT] = {};
        uint32_t count = 0;
    };

    struct EditorObjectCandidate {
        uintptr_t sourceOffset = 0;
        uintptr_t object = 0;
        uintptr_t vtable = 0;
        uintptr_t sceneResource = 0;
        uint32_t ownerValue18 = 0;
        uint32_t ownerKey1c = 0;
        int score = 0;
    };

    struct SelectedEditorObject {
        int index = 0;
        uintptr_t listNode = 0;
        uintptr_t selectedObject = 0;
        uintptr_t mappedObject = 0;
        uintptr_t editorTransform = 0;
        uintptr_t editorScaleBackingObject = 0;
        uintptr_t parentObject = 0;
        uintptr_t mappedParentObject = 0;
        uintptr_t sceneHolder = 0;
        uintptr_t resourceContainer = 0;
        uintptr_t resourceSceneRoot = 0;
        uintptr_t firstMeshSceneObject = 0;
        uintptr_t firstVisibilitySceneObject = 0;
        uintptr_t firstLightSceneObject = 0;
        uint32_t meshSceneObjectCount = 0;
        uint32_t visibilitySceneObjectCount = 0;
        uint32_t lightSceneObjectCount = 0;
        bool meshUsedReversedTypeSubtypeFallback = false;
        bool visibilityUsedReversedTypeSubtypeFallback = false;
        bool lightUsedReversedTypeSubtypeFallback = false;
        uint32_t selectedObjectMovementState04 = 0;
        uint16_t selectedObjectType = 0;
        uint32_t selectedObjectChildMode = 0;
        uintptr_t selectedObjectChildren = 0;
        uint32_t selectedObjectFlags0c = 0;
        uint32_t selectedObjectFlags = 0;
        uint32_t childCount20 = 0;
        uint32_t parentKey1c = 0;
        uint32_t sceneHolderFlags08 = 0;
        uint32_t sceneHolderVariationMask20 = 0;
        float rotationRadians = 0.0f;
        float unknownEcRaw = 0.0f;
        float sceneHolderBuoyancyRaw = 0.0f;
        float frictionRaw = 0.0f;
        float objectGravityRaw = 0.0f;
        float lightIntensityRaw = 0.0f;
        float lightRangeRaw = 0.0f;
        float meshOffset94Raw = 0.0f;
        float meshOffset9cRaw = 0.0f;
        bool hasRotation = false;
        bool hasLightRange = false;
        bool hasSceneHolderBuoyancy = false;
        bool hasFriction = false;
        bool hasObjectGravity = false;
        bool hasLightIntensity = false;
        bool hasMeshOffset94 = false;
        bool hasMeshOffset9c = false;
        bool visible = true;
        bool hasVisible = false;
        bool sceneHolderVisible = true;
        bool hasSceneHolderVisible = false;
        bool contactResponseEnabled = true;
        bool shadowTypeDynamicCandidate = false;
        bool hasShadowType = false;
        bool lightEnabled = false;
        bool hasLightEnabled = false;
        bool selectedObjectPhysicsEnabled = false;
        bool hasSelectedObjectPhysicsEnabled = false;
        bool movingOrRotatingCandidate = false;
        bool horizontalAlign = false;
        bool verticalAlign = false;
        bool sceneHolderBit0 = false;
        bool lockedToDrivingLineCandidate = false;
        bool sceneHolderBit3 = false;
        bool sceneHolderBit5 = false;
        bool sceneHolderBit6 = false;
        bool fastObjectCandidate = false;
        bool sceneHolderBit12 = false;
    };

    struct SceneNodeSnapshot {
        uintptr_t address = 0;
        uintptr_t via = 0;
        int depth = 0;
        uint16_t typeAndSubtype = 0;
        uint32_t flags0c = 0;
        uint32_t childMode = 0;
        uintptr_t childStorage = 0;
        uintptr_t vtable = 0;
        float scaleEc = 0.0f;
        uint8_t physicsByteC9 = 0;
        bool readable = false;
    };

    struct PendingEditorNudgeRestore {
        uintptr_t sceneObject = 0;
        float position[3] = {};
        uint32_t restoreAfterTick = 0;
    };

    struct MaterialColorOverrideEntry {
        uint32_t selector = 0;
        uint8_t pad04[12] = {};
        uint8_t slot = 0;
        uint8_t sourceMode = 0;
        uint8_t pad12[14] = {};
        float color[4] = {};
        uint8_t overrideMode = 0;
        uint8_t secondaryMode = 0;
        uint8_t pad32[14] = {};
        uint32_t extra[4] = {};
        uint32_t colorCount = 0;
        uint8_t pad54[12] = {};
    };

    struct MaterialColorOverrideVector {
        uint32_t count = 0;
        uint32_t capacity = 0;
        MaterialColorOverrideEntry* entries = nullptr;
    };

    struct MaterialParameterLookup {
        uint8_t found = 0;
        uint8_t pad01[11] = {};
        uint32_t count = 1;
        float* value = nullptr;
        void* owner = nullptr;
        uint32_t unknown18 = 0;
    };

    struct EditorPublishScaleOverride {
        bool active = false;
        uintptr_t selectedObject = 0;
        uintptr_t mappedObject = 0;
        uintptr_t editorScaleBackingObject = 0;
        uintptr_t sceneHolder = 0;
        uintptr_t firstMeshSceneObject = 0;
        float scale[3] = { 1.0f, 1.0f, 1.0f };
        uint32_t updatedTick = 0;
    };

    static_assert(sizeof(MaterialColorOverrideEntry) == 0x60, "material override entry must match engine layout");
    static_assert(sizeof(MaterialParameterLookup) == 0x1c, "material parameter lookup must match engine layout");

    static PendingEditorNudgeRestore g_pendingEditorNudgeRestore = {};
    static float g_editorInspectorAutoScale = 1.0f;
    static float g_editorInspectorLastAppliedScale = -1.0f;
    static float g_editorInspectorAxisScale[3] = { 1.0f, 1.0f, 1.0f };
    static uintptr_t g_editorInspectorAxisScaleObject = 0;
    static float g_editorMaterialTestColor[3] = { 1.0f, 0.0f, 1.0f };
    static int g_editorMaterialTestSelector = 1;
    static int g_editorMaterialTestSlot = 0;
    static int g_editorMaterialTestSourceMode = 0;
    static int g_editorMaterialTestOverrideMode = 0;
    static int g_editorMaterialTestSecondaryMode = 0;
    static bool g_editorMaterialTestRefreshAfterApply = true;
    static bool g_editorMaterialStickyEnabled = false;
    static bool g_editorMaterialStickyUseAutoSelectors = true;
    static uint32_t g_editorMaterialStickyLastMonitorTick = 0;
    static std::vector<uintptr_t> g_editorMaterialStickySceneObjects;
    static bool g_editorMaterialCreateMeshHookInstalled = false;
    static CreateMeshInstancesFunc g_originalCreateMeshInstances = nullptr;
    static bool g_editorMaterialApplyOverrideHookInstalled = false;
    static ApplyMaterialColorOverridesFunc g_originalApplyMaterialColorOverrides = nullptr;
    static constexpr size_t MAX_STICKY_MATERIAL_PARAM_OWNERS = 64;
    static uintptr_t g_editorMaterialStickyParamOwners[MAX_STICKY_MATERIAL_PARAM_OWNERS] = {};
    static volatile LONG g_editorMaterialStickyParamOwnerCount = 0;
    static bool g_editorMaterialParamSetterHookInstalled = false;
    static uintptr_t g_editorMaterialParamSetterAddress = 0;
    static MaterialParameterSetBytesFunc g_originalMaterialParameterSetBytes = nullptr;
    static bool g_editorMaterialTrackEventHookInstalled = false;
    static TrackEventApplySceneObjectFunc g_originalTrackEventApplySceneObject = nullptr;
    static bool g_editorUploadTrackListTraceHookInstalled = false;
    static bool g_editorUploadTraceArmed = false;
    static BuildTrackObjectListsFunc g_originalBuildTrackObjectLists = nullptr;
    static volatile LONG g_editorUploadTraceCount = 0;
    static constexpr uint32_t EDITOR_UPLOAD_TRACE_MAX_OBJECTS = 16;
    static constexpr uint32_t EDITOR_UPLOAD_TRACE_MAX_VALUES = 16;
    static constexpr uint32_t EDITOR_UPLOAD_TRACE_VALUE_RECORD_SIZE = 0x50;
    static constexpr uint32_t EDITOR_PUBLISH_SCALE_MAX_OVERRIDES = 64;
    static constexpr uint32_t EDITOR_PUBLISH_SCALE_VALUE_OFFSET = 0x30;
    static bool g_editorPublishScalePatchEnabled = false;
    static bool g_editorNativeUniformScaleBackingEnabled = true;
    static bool g_editorSyncSavedPlacementOnScaleEnabled = true;
    static EditorPublishScaleOverride g_editorPublishScaleOverrides[EDITOR_PUBLISH_SCALE_MAX_OVERRIDES] = {};
    static bool g_editorTrackSaveTraceHooksInstalled = false;
    static SerializeTrackDataFunc g_originalSerializeTrackData = nullptr;
    static SerializeTrackDataToPacketFunc g_originalSerializeTrackDataToPacket = nullptr;
    static ProcessTrackHashesAndSaveFunc g_originalProcessTrackHashesAndSave = nullptr;
    static volatile LONG g_editorTrackSaveTraceCount = 0;
    static uint32_t g_editorUploadTraceSnapshotIndex = 0;
    static uint32_t g_editorUploadTraceSnapshotObjectCount = 0;
    static uint32_t g_editorUploadTraceSnapshotValueCount = 0;
    static uintptr_t g_editorUploadTraceSnapshotObjectEntries = 0;
    static uintptr_t g_editorUploadTraceSnapshotValueEntries = 0;
    static uintptr_t g_editorUploadTraceSnapshotObjects[EDITOR_UPLOAD_TRACE_MAX_OBJECTS] = {};
    static uint8_t g_editorUploadTraceSnapshotValues
        [EDITOR_UPLOAD_TRACE_MAX_VALUES][EDITOR_UPLOAD_TRACE_VALUE_RECORD_SIZE] = {};
    static bool g_editorUploadTraceSnapshotValid = false;
    static bool g_editorMaterialTrackEventTraceArmed = false;
    static volatile LONG g_editorMaterialTrackEventTraceCount = 0;
    static volatile LONG g_editorMaterialTrackEventAnyTraceCount = 0;
    static volatile LONG g_editorMaterialParamSetterHookDepth = 0;
    static bool g_editorMaterialTraceEnabled = false;
    static uintptr_t g_editorMaterialTraceSceneObject = 0;
    static uintptr_t g_editorMaterialTraceOwner = 0;
    static uintptr_t g_editorMaterialTraceValuePtr = 0;
    static uint32_t g_editorMaterialTraceSelector = 0;
    static uint8_t g_editorMaterialTraceSlot = 0;
    static float g_editorMaterialTraceLastValue[4] = {};
    static uint32_t g_editorMaterialTraceLastMonitorTick = 0;
    static int g_editorMaterialTraceChangeCount = 0;
    static bool g_editorMaterialSetterHookWarningLogged = false;
    static constexpr uint32_t EDITOR_MATERIAL_STICKY_MONITOR_INTERVAL_MS = 100;
    static constexpr float EDITOR_AUTO_SCALE_MIN = 0.01f;
    static constexpr float EDITOR_AUTO_SCALE_MAX = 50.0f;
    static constexpr float EDITOR_AUTO_SCALE_NUDGE_EPSILON = 0.001f;
    static constexpr uint32_t EDITOR_AUTO_SCALE_RESTORE_DELAY_MS = 75;
    static constexpr float EDITOR_SCALE_HOTKEY_DOUBLINGS_PER_SECOND = 1.0f;

    static bool SafeReadMemory(uintptr_t address, void* out, size_t size) {
        if (address == 0 || out == nullptr || IsBadReadPtr(reinterpret_cast<void*>(address), size)) {
            return false;
        }

        __try {
            memcpy(out, reinterpret_cast<void*>(address), size);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    static bool SafeWriteMemory(uintptr_t address, const void* value, size_t size) {
        if (address == 0 || value == nullptr || IsBadWritePtr(reinterpret_cast<void*>(address), size)) {
            return false;
        }

        __try {
            memcpy(reinterpret_cast<void*>(address), value, size);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    template<typename T>
    static bool SafeReadValue(uintptr_t address, T& out) {
        return SafeReadMemory(address, &out, sizeof(T));
    }

    template<typename T>
    static bool SafeWriteValue(uintptr_t address, const T& value) {
        return SafeWriteMemory(address, &value, sizeof(T));
    }

    static bool IsReadableRange(uintptr_t address, size_t size) {
        return address != 0 && !IsBadReadPtr(reinterpret_cast<void*>(address), size);
    }

    static std::string HexAddress(uintptr_t value) {
        std::ostringstream ss;
        ss << "0x" << std::hex << std::uppercase << value;
        return ss.str();
    }

    static std::string SafeReadCString(uintptr_t address, size_t maxLen = 96) {
        if (address == 0 || IsBadReadPtr(reinterpret_cast<void*>(address), 1)) {
            return "<null>";
        }

        std::string value;
        value.reserve(maxLen);
        for (size_t i = 0; i < maxLen; ++i) {
            char ch = '\0';
            if (!SafeReadValue(address + i, ch)) {
                return value.empty() ? "<unreadable>" : value;
            }
            if (ch == '\0') {
                return value;
            }
            if (static_cast<unsigned char>(ch) < 0x20 || static_cast<unsigned char>(ch) > 0x7e) {
                value.push_back('?');
            }
            else {
                value.push_back(ch);
            }
        }
        value += "...";
        return value;
    }

    static int VariationIndexFromMask(uint32_t mask) {
        if (mask != 1 && mask != 2 && mask != 4 && mask != 8) {
            return -1;
        }

        int index = 0;
        while (mask > 1) {
            mask >>= 1;
            ++index;
        }
        return index;
    }

    static uintptr_t GetGameManagerGlobalRva() {
        return BaseAddress::IsSteamVersion() ? GAME_MANAGER_GLOBAL_RVA_STEAM : GAME_MANAGER_GLOBAL_RVA_UPLAY;
    }

    static uintptr_t GetCollectSceneObjectsByTypeRva() {
        return BaseAddress::IsSteamVersion()
            ? COLLECT_SCENE_OBJECTS_BY_TYPE_RVA_STEAM
            : COLLECT_SCENE_OBJECTS_BY_TYPE_RVA_UPLAY;
    }

    static uintptr_t GetUpdateObjectVariationTransformRva() {
        return BaseAddress::IsSteamVersion()
            ? UPDATE_OBJECT_VARIATION_TRANSFORM_RVA_STEAM
            : UPDATE_OBJECT_VARIATION_TRANSFORM_RVA_UPLAY;
    }

    static uintptr_t GetCreateObjectVariationRva() {
        return BaseAddress::IsSteamVersion()
            ? CREATE_OBJECT_VARIATION_RVA_STEAM
            : CREATE_OBJECT_VARIATION_RVA_UPLAY;
    }

    static uintptr_t GetRenderGraphicsAndUpdateCameraRva() {
        return BaseAddress::IsSteamVersion()
            ? RENDER_GRAPHICS_AND_UPDATE_CAMERA_RVA_STEAM
            : RENDER_GRAPHICS_AND_UPDATE_CAMERA_RVA_UPLAY;
    }

    static uintptr_t GetApplySceneObjectTransformRva() {
        return BaseAddress::IsSteamVersion()
            ? APPLY_SCENE_OBJECT_TRANSFORM_RVA_STEAM
            : APPLY_SCENE_OBJECT_TRANSFORM_RVA_UPLAY;
    }

    static uintptr_t GetScaleSelectedObjectsDeltaRva() {
        return BaseAddress::IsSteamVersion()
            ? SCALE_SELECTED_OBJECTS_DELTA_RVA_STEAM
            : SCALE_SELECTED_OBJECTS_DELTA_RVA_UPLAY;
    }

    static uintptr_t GetResetSelectedObjectsScaleRva() {
        return BaseAddress::IsSteamVersion()
            ? RESET_SELECTED_OBJECTS_SCALE_RVA_STEAM
            : RESET_SELECTED_OBJECTS_SCALE_RVA_UPLAY;
    }

    static uintptr_t GetSetBackingObjectScaleRva() {
        return BaseAddress::IsSteamVersion()
            ? SET_BACKING_OBJECT_SCALE_RVA_STEAM
            : SET_BACKING_OBJECT_SCALE_RVA_UPLAY;
    }

    static uintptr_t GetSyncSceneObjectTransformRva() {
        return BaseAddress::IsSteamVersion()
            ? SYNC_SCENE_OBJECT_TRANSFORM_RVA_STEAM
            : SYNC_SCENE_OBJECT_TRANSFORM_RVA_UPLAY;
    }

    static uintptr_t GetRefreshEditorObjectVisualRva() {
        return BaseAddress::IsSteamVersion()
            ? REFRESH_EDITOR_OBJECT_VISUAL_RVA_STEAM
            : REFRESH_EDITOR_OBJECT_VISUAL_RVA_UPLAY;
    }

    static uintptr_t GetRefreshSelectionConstraintsRva() {
        return BaseAddress::IsSteamVersion()
            ? REFRESH_SELECTION_CONSTRAINTS_RVA_STEAM
            : REFRESH_SELECTION_CONSTRAINTS_RVA_UPLAY;
    }

    static uintptr_t GetRefreshSelectionBoundsRva() {
        return BaseAddress::IsSteamVersion()
            ? REFRESH_SELECTION_BOUNDS_RVA_STEAM
            : REFRESH_SELECTION_BOUNDS_RVA_UPLAY;
    }

    static uintptr_t GetProcessSelectionConstraintsRva() {
        return BaseAddress::IsSteamVersion()
            ? PROCESS_SELECTION_CONSTRAINTS_RVA_STEAM
            : PROCESS_SELECTION_CONSTRAINTS_RVA_UPLAY;
    }

    static uintptr_t GetCreateMeshInstancesRva() {
        return BaseAddress::IsSteamVersion()
            ? CREATE_MESH_INSTANCES_RVA_STEAM
            : CREATE_MESH_INSTANCES_RVA_UPLAY;
    }

    static uintptr_t GetApplyMaterialColorOverridesRva() {
        return BaseAddress::IsSteamVersion()
            ? APPLY_MATERIAL_COLOR_OVERRIDES_RVA_STEAM
            : APPLY_MATERIAL_COLOR_OVERRIDES_RVA_UPLAY;
    }

    static uintptr_t GetTrackEventApplySceneObjectRva() {
        return BaseAddress::IsSteamVersion()
            ? TRACK_EVENT_APPLY_SCENE_OBJECT_RVA_STEAM
            : TRACK_EVENT_APPLY_SCENE_OBJECT_RVA_UPLAY;
    }

    static uintptr_t GetBuildObjectValuesJsonRva() {
        return BaseAddress::IsSteamVersion()
            ? BUILD_OBJECT_VALUES_JSON_RVA_STEAM
            : BUILD_OBJECT_VALUES_JSON_RVA_UPLAY;
    }

    static uintptr_t GetBuildTrackObjectListsRva() {
        return BaseAddress::IsSteamVersion()
            ? BUILD_TRACK_OBJECT_LISTS_RVA_STEAM
            : BUILD_TRACK_OBJECT_LISTS_RVA_UPLAY;
    }

    static uintptr_t GetUploadObjectValuesGlobalRva() {
        return BaseAddress::IsSteamVersion()
            ? UPLOAD_OBJECT_VALUES_GLOBAL_RVA_STEAM
            : UPLOAD_OBJECT_VALUES_GLOBAL_RVA_UPLAY;
    }

    static uintptr_t GetSerializeTrackDataRva() {
        return BaseAddress::IsSteamVersion()
            ? SERIALIZE_TRACK_DATA_RVA_STEAM
            : SERIALIZE_TRACK_DATA_RVA_UPLAY;
    }

    static uintptr_t GetSerializeTrackDataToPacketRva() {
        return BaseAddress::IsSteamVersion()
            ? SERIALIZE_TRACK_DATA_TO_PACKET_RVA_STEAM
            : SERIALIZE_TRACK_DATA_TO_PACKET_RVA_UPLAY;
    }

    static uintptr_t GetProcessTrackHashesAndSaveRva() {
        return BaseAddress::IsSteamVersion()
            ? PROCESS_TRACK_HASHES_AND_SAVE_RVA_STEAM
            : PROCESS_TRACK_HASHES_AND_SAVE_RVA_UPLAY;
    }

    static uintptr_t ResolveGameManagerForEditorInspector() {
        const uintptr_t baseAddress = reinterpret_cast<uintptr_t>(GetModuleHandle(NULL));
        uintptr_t globalStruct = 0;
        if (!SafeReadValue(baseAddress + GetGameManagerGlobalRva(), globalStruct)
            || !IsReadableRange(globalStruct, 0x200)) {
            return 0;
        }

        return globalStruct;
    }

    static uintptr_t ResolveEditorManagerForInspector() {
        const uintptr_t gameManager = ResolveGameManagerForEditorInspector();
        uintptr_t editorManager = 0;
        if (gameManager == 0
            || !SafeReadValue(gameManager + GAME_MANAGER_EDITOR_MANAGER_OFFSET, editorManager)
            || !IsReadableRange(editorManager, EDITOR_MANAGER_SCAN_SIZE)) {
            return 0;
        }

        return editorManager;
    }

    static uintptr_t ResolveEditorSelectionManagerForInspector() {
        const uintptr_t editorManager = ResolveEditorManagerForInspector();
        return editorManager != 0 ? editorManager + 0x28 : 0;
    }

    static uintptr_t ResolveEntityManagerForEditorInspector() {
        const uintptr_t gameManager = ResolveGameManagerForEditorInspector();
        uintptr_t entityManager = 0;
        if (gameManager == 0
            || !SafeReadValue(gameManager + 0xdc, entityManager)
            || !IsReadableRange(entityManager, 0xf00)) {
            return 0;
        }
        return entityManager;
    }

    static bool MarkEditorTransformDirty(uint32_t flags = 0x200) {
        const uintptr_t editorManager = ResolveEditorManagerForInspector();
        if (editorManager == 0 || !IsReadableRange(editorManager + 0x1fc, sizeof(uint32_t))) {
            return false;
        }

        uint32_t currentFlags = 0;
        if (!SafeReadValue(editorManager + 0x1f8, currentFlags)) {
            return false;
        }

        const uint8_t clearPending = 0;
        const uint32_t newFlags = currentFlags | flags;
        const bool clearedPending = SafeWriteMemory(editorManager + 0x1f1, &clearPending, sizeof(clearPending));
        const bool wroteFlags = SafeWriteValue(editorManager + 0x1f8, newFlags);
        LOG_VERBOSE("[EditorInspector] MarkEditorTransformDirty editorManager="
            << HexAddress(editorManager)
            << " flags=0x" << std::hex << std::uppercase << currentFlags
            << " -> 0x" << newFlags << std::dec
            << " clearedPending=" << clearedPending
            << " wroteFlags=" << wroteFlags);
        return wroteFlags;
    }

    static CollectSceneObjectsByTypeFunc ResolveCollectSceneObjectsByType() {
        const uintptr_t baseAddress = reinterpret_cast<uintptr_t>(GetModuleHandle(NULL));
        return reinterpret_cast<CollectSceneObjectsByTypeFunc>(baseAddress + GetCollectSceneObjectsByTypeRva());
    }

    static UpdateObjectVariationTransformFunc ResolveUpdateObjectVariationTransform() {
        const uintptr_t baseAddress = reinterpret_cast<uintptr_t>(GetModuleHandle(NULL));
        return reinterpret_cast<UpdateObjectVariationTransformFunc>(
            baseAddress + GetUpdateObjectVariationTransformRva());
    }

    static CreateObjectVariationFunc ResolveCreateObjectVariation() {
        const uintptr_t baseAddress = reinterpret_cast<uintptr_t>(GetModuleHandle(NULL));
        return reinterpret_cast<CreateObjectVariationFunc>(baseAddress + GetCreateObjectVariationRva());
    }

    static RenderGraphicsAndUpdateCameraFunc ResolveRenderGraphicsAndUpdateCamera() {
        const uintptr_t baseAddress = reinterpret_cast<uintptr_t>(GetModuleHandle(NULL));
        return reinterpret_cast<RenderGraphicsAndUpdateCameraFunc>(
            baseAddress + GetRenderGraphicsAndUpdateCameraRva());
    }

    static ApplySceneObjectTransformFunc ResolveApplySceneObjectTransform() {
        const uintptr_t baseAddress = reinterpret_cast<uintptr_t>(GetModuleHandle(NULL));
        return reinterpret_cast<ApplySceneObjectTransformFunc>(
            baseAddress + GetApplySceneObjectTransformRva());
    }

    static ScaleSelectedObjectsDeltaFunc ResolveScaleSelectedObjectsDelta() {
        const uintptr_t baseAddress = reinterpret_cast<uintptr_t>(GetModuleHandle(NULL));
        return reinterpret_cast<ScaleSelectedObjectsDeltaFunc>(
            baseAddress + GetScaleSelectedObjectsDeltaRva());
    }

    static ResetSelectedObjectsScaleFunc ResolveResetSelectedObjectsScale() {
        const uintptr_t baseAddress = reinterpret_cast<uintptr_t>(GetModuleHandle(NULL));
        return reinterpret_cast<ResetSelectedObjectsScaleFunc>(
            baseAddress + GetResetSelectedObjectsScaleRva());
    }

    static SetBackingObjectScaleFunc ResolveSetBackingObjectScale() {
        const uintptr_t baseAddress = reinterpret_cast<uintptr_t>(GetModuleHandle(NULL));
        return reinterpret_cast<SetBackingObjectScaleFunc>(baseAddress + GetSetBackingObjectScaleRva());
    }

    static SyncSceneObjectTransformFunc ResolveSyncSceneObjectTransform() {
        const uintptr_t baseAddress = reinterpret_cast<uintptr_t>(GetModuleHandle(NULL));
        return reinterpret_cast<SyncSceneObjectTransformFunc>(baseAddress + GetSyncSceneObjectTransformRva());
    }

    static RefreshEditorObjectVisualFunc ResolveRefreshEditorObjectVisual() {
        const uintptr_t baseAddress = reinterpret_cast<uintptr_t>(GetModuleHandle(NULL));
        return reinterpret_cast<RefreshEditorObjectVisualFunc>(baseAddress + GetRefreshEditorObjectVisualRva());
    }

    static RefreshSelectionConstraintsFunc ResolveRefreshSelectionConstraints() {
        const uintptr_t baseAddress = reinterpret_cast<uintptr_t>(GetModuleHandle(NULL));
        return reinterpret_cast<RefreshSelectionConstraintsFunc>(
            baseAddress + GetRefreshSelectionConstraintsRva());
    }

    static RefreshSelectionBoundsFunc ResolveRefreshSelectionBounds() {
        const uintptr_t baseAddress = reinterpret_cast<uintptr_t>(GetModuleHandle(NULL));
        return reinterpret_cast<RefreshSelectionBoundsFunc>(
            baseAddress + GetRefreshSelectionBoundsRva());
    }

    static ProcessSelectionConstraintsFunc ResolveProcessSelectionConstraints() {
        const uintptr_t baseAddress = reinterpret_cast<uintptr_t>(GetModuleHandle(NULL));
        return reinterpret_cast<ProcessSelectionConstraintsFunc>(
            baseAddress + GetProcessSelectionConstraintsRva());
    }

    static CreateMeshInstancesFunc ResolveCreateMeshInstances() {
        const uintptr_t baseAddress = reinterpret_cast<uintptr_t>(GetModuleHandle(NULL));
        return reinterpret_cast<CreateMeshInstancesFunc>(baseAddress + GetCreateMeshInstancesRva());
    }

    static ApplyMaterialColorOverridesFunc ResolveApplyMaterialColorOverrides() {
        const uintptr_t baseAddress = reinterpret_cast<uintptr_t>(GetModuleHandle(NULL));
        return reinterpret_cast<ApplyMaterialColorOverridesFunc>(baseAddress + GetApplyMaterialColorOverridesRva());
    }

    static TrackEventApplySceneObjectFunc ResolveTrackEventApplySceneObject() {
        const uintptr_t baseAddress = reinterpret_cast<uintptr_t>(GetModuleHandle(NULL));
        return reinterpret_cast<TrackEventApplySceneObjectFunc>(
            baseAddress + GetTrackEventApplySceneObjectRva());
    }

    static BuildObjectValuesJsonFunc ResolveBuildObjectValuesJson() {
        const uintptr_t baseAddress = reinterpret_cast<uintptr_t>(GetModuleHandle(NULL));
        return reinterpret_cast<BuildObjectValuesJsonFunc>(
            baseAddress + GetBuildObjectValuesJsonRva());
    }

    static BuildTrackObjectListsFunc ResolveBuildTrackObjectLists() {
        const uintptr_t baseAddress = reinterpret_cast<uintptr_t>(GetModuleHandle(NULL));
        return reinterpret_cast<BuildTrackObjectListsFunc>(
            baseAddress + GetBuildTrackObjectListsRva());
    }

    static uintptr_t ResolveUploadObjectValuesGlobal() {
        const uintptr_t baseAddress = reinterpret_cast<uintptr_t>(GetModuleHandle(NULL));
        return baseAddress + GetUploadObjectValuesGlobalRva();
    }

    static SerializeTrackDataFunc ResolveSerializeTrackData() {
        const uintptr_t baseAddress = reinterpret_cast<uintptr_t>(GetModuleHandle(NULL));
        return reinterpret_cast<SerializeTrackDataFunc>(baseAddress + GetSerializeTrackDataRva());
    }

    static SerializeTrackDataToPacketFunc ResolveSerializeTrackDataToPacket() {
        const uintptr_t baseAddress = reinterpret_cast<uintptr_t>(GetModuleHandle(NULL));
        return reinterpret_cast<SerializeTrackDataToPacketFunc>(baseAddress + GetSerializeTrackDataToPacketRva());
    }

    static ProcessTrackHashesAndSaveFunc ResolveProcessTrackHashesAndSave() {
        const uintptr_t baseAddress = reinterpret_cast<uintptr_t>(GetModuleHandle(NULL));
        return reinterpret_cast<ProcessTrackHashesAndSaveFunc>(baseAddress + GetProcessTrackHashesAndSaveRva());
    }

    static bool ForceSceneObjectVisualRefresh(uintptr_t sceneObject);

    static const char* DescribeMaterialColorSlot(uint8_t slot) {
        if (slot == 0) {
            return "diffuse";
        }
        if (slot == 1) {
            return "diffuse2";
        }
        return "colorN";
    }

    static uint32_t GetMaterialColorParameterHash(uint8_t slot) {
        if (slot == 0) {
            return 0x55e806b2; // diffuse
        }
        if (slot == 1) {
            return 0x27d930b8; // diffuse2
        }
        return 0;
    }

    static bool ApplyMaterialColorOverrideToSceneObject(
        uintptr_t sceneObject,
        uint32_t selector,
        uint8_t slot,
        uint8_t sourceMode,
        uint8_t overrideMode,
        uint8_t secondaryMode,
        const float color[3],
        bool refreshAfterApply) {
        if (sceneObject == 0 || color == nullptr || !IsReadableRange(sceneObject, sizeof(uintptr_t))) {
            return false;
        }

        CreateMeshInstancesFunc createMeshInstances = ResolveCreateMeshInstances();
        if (!createMeshInstances) {
            return false;
        }

        MaterialColorOverrideEntry entry = {};
        entry.selector = selector;
        entry.slot = slot;
        entry.sourceMode = sourceMode;
        entry.color[0] = color[0];
        entry.color[1] = color[1];
        entry.color[2] = color[2];
        entry.color[3] = 1.0f;
        entry.overrideMode = overrideMode;
        entry.secondaryMode = secondaryMode;
        entry.colorCount = 1;

        MaterialColorOverrideVector vector = {};
        vector.count = 1;
        vector.capacity = 1;
        vector.entries = &entry;

        __try {
            createMeshInstances(
                reinterpret_cast<void*>(sceneObject),
                &vector,
                1.0f);
            if (refreshAfterApply) {
                ForceSceneObjectVisualRefresh(sceneObject);
            }
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    static void CollectMaterialSelectorCandidates(uintptr_t sceneObject, std::vector<uint32_t>& selectors);
    static void AppendRawDwordFields(std::ostringstream& ss, const char* title, uintptr_t baseAddress, uintptr_t byteCount);
    static void AppendTrackEventMaterialBackingReport(std::ostringstream& ss, const SelectedEditorObject& selected);
    static void EnsureMaterialParameterSetterHookInstalled(uintptr_t setterAddress);

    static int ApplyMaterialColorOverrideToObjects(
        const std::vector<uintptr_t>& sceneObjects,
        uint32_t selector,
        uint8_t slot,
        uint8_t sourceMode,
        uint8_t overrideMode,
        uint8_t secondaryMode,
        const float color[3],
        bool refreshAfterApply) {
        int appliedCount = 0;
        for (uintptr_t sceneObject : sceneObjects) {
            if (ApplyMaterialColorOverrideToSceneObject(
                sceneObject,
                selector,
                slot,
                sourceMode,
                overrideMode,
                secondaryMode,
                color,
                refreshAfterApply)) {
                ++appliedCount;
            }
        }
        return appliedCount;
    }

    static int ApplyMaterialColorAutoSelectorsToObjects(
        const std::vector<uintptr_t>& sceneObjects,
        uint32_t manualSelector,
        uint8_t slot,
        uint8_t sourceMode,
        uint8_t overrideMode,
        uint8_t secondaryMode,
        const float color[3],
        bool refreshAfterApply,
        int* outAttempts = nullptr) {
        int attempts = 0;
        int appliedCount = 0;
        for (uintptr_t sceneObject : sceneObjects) {
            std::vector<uint32_t> selectors;
            CollectMaterialSelectorCandidates(sceneObject, selectors);
            if (manualSelector != 0
                && std::find(selectors.begin(), selectors.end(), manualSelector) == selectors.end()) {
                selectors.push_back(manualSelector);
            }

            for (uint32_t selector : selectors) {
                ++attempts;
                if (ApplyMaterialColorOverrideToSceneObject(
                    sceneObject,
                    selector,
                    slot,
                    sourceMode,
                    overrideMode,
                    secondaryMode,
                    color,
                    refreshAfterApply)) {
                    ++appliedCount;
                }
            }
        }

        if (outAttempts) {
            *outAttempts = attempts;
        }
        return appliedCount;
    }

    static bool SetSerializedMeshColorFields(uintptr_t meshSceneObject, const float color[3], bool refreshAfterApply) {
        if (meshSceneObject == 0
            || color == nullptr
            || !IsReadableRange(meshSceneObject + 0x9c, sizeof(float))) {
            return false;
        }

        const float rgba[4] = {
            color[0],
            color[1],
            color[2],
            1.0f,
        };

        const bool wrote = SafeWriteMemory(meshSceneObject + 0x90, rgba, sizeof(rgba));
        if (wrote) {
            MarkEditorTransformDirty();
            if (refreshAfterApply) {
                ForceSceneObjectVisualRefresh(meshSceneObject);
            }
        }
        return wrote;
    }

    static int SetSerializedMeshColorFieldsOnObjects(
        const std::vector<uintptr_t>& sceneObjects,
        const float color[3],
        bool refreshAfterApply) {
        int appliedCount = 0;
        for (uintptr_t sceneObject : sceneObjects) {
            if (SetSerializedMeshColorFields(sceneObject, color, refreshAfterApply)) {
                ++appliedCount;
            }
        }
        return appliedCount;
    }

    static bool IsStickyMaterialSceneObject(uintptr_t sceneObject) {
        return g_editorMaterialStickyEnabled
            && sceneObject != 0
            && std::find(
                g_editorMaterialStickySceneObjects.begin(),
                g_editorMaterialStickySceneObjects.end(),
                sceneObject) != g_editorMaterialStickySceneObjects.end();
    }

    static bool IsStickyMaterialParameterOwner(uintptr_t parameterOwner) {
        if (!g_editorMaterialStickyEnabled || parameterOwner == 0) {
            return false;
        }

        const LONG count = g_editorMaterialStickyParamOwnerCount;
        const LONG clampedCount = count < 0
            ? 0
            : (count > static_cast<LONG>(MAX_STICKY_MATERIAL_PARAM_OWNERS)
                ? static_cast<LONG>(MAX_STICKY_MATERIAL_PARAM_OWNERS)
                : count);
        for (LONG i = 0; i < clampedCount; ++i) {
            if (g_editorMaterialStickyParamOwners[i] == parameterOwner) {
                return true;
            }
        }
        return false;
    }

    static bool Float4Different(const float a[4], const float b[4], float epsilon = 0.0001f) {
        for (int i = 0; i < 4; ++i) {
            if (std::fabs(a[i] - b[i]) > epsilon) {
                return true;
            }
        }
        return false;
    }

    static bool LookupMaterialParameter(
        uintptr_t sceneObject,
        uint32_t selector,
        uint8_t slot,
        MaterialParameterLookup& outLookup,
        uintptr_t& outOwner,
        uintptr_t& outSetterAddress,
        uintptr_t& outGetterAddress) {
        outLookup = {};
        outOwner = 0;
        outSetterAddress = 0;
        outGetterAddress = 0;
        if (sceneObject == 0 || !IsReadableRange(sceneObject, sizeof(uintptr_t))) {
            return false;
        }

        const uint32_t parameterHash = GetMaterialColorParameterHash(slot);
        if (parameterHash == 0) {
            return false;
        }

        uintptr_t vtable = 0;
        uintptr_t getterAddress = 0;
        if (!SafeReadValue(sceneObject, vtable)
            || !IsReadableRange(vtable + 0x64, sizeof(uintptr_t))
            || !SafeReadValue(vtable + 0x64, getterAddress)
            || getterAddress == 0) {
            return false;
        }
        outGetterAddress = getterAddress;

        MaterialParameterLookup lookup = {};
        lookup.count = 1;
        __try {
            SceneObjectGetMaterialParameterFunc getter =
                reinterpret_cast<SceneObjectGetMaterialParameterFunc>(getterAddress);
            if (!getter(reinterpret_cast<void*>(sceneObject), parameterHash, &lookup, selector)
                || lookup.owner == nullptr) {
                return false;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }

        outLookup = lookup;
        outOwner = reinterpret_cast<uintptr_t>(lookup.owner);
        uintptr_t ownerVtable = 0;
        return SafeReadValue(outOwner, ownerVtable)
            && IsReadableRange(ownerVtable + 0x74, sizeof(uintptr_t))
            && SafeReadValue(ownerVtable + 0x74, outSetterAddress)
            && outSetterAddress != 0;
    }

    static bool LookupMaterialParameterOwner(
        uintptr_t sceneObject,
        uint32_t selector,
        uint8_t slot,
        uintptr_t& outOwner,
        uintptr_t& outSetterAddress) {
        MaterialParameterLookup lookup = {};
        uintptr_t getterAddress = 0;
        return LookupMaterialParameter(
            sceneObject,
            selector,
            slot,
            lookup,
            outOwner,
            outSetterAddress,
            getterAddress);
    }

    static bool WriteMaterialParameterValuePtr(
        uintptr_t sceneObject,
        uint32_t selector,
        uint8_t slot,
        const float color[3],
        bool refreshAfterApply) {
        if (color == nullptr) {
            return false;
        }

        MaterialParameterLookup lookup = {};
        uintptr_t owner = 0;
        uintptr_t setterAddress = 0;
        uintptr_t getterAddress = 0;
        if (!LookupMaterialParameter(
            sceneObject,
            selector,
            slot,
            lookup,
            owner,
            setterAddress,
            getterAddress)
            || lookup.value == nullptr
            || !IsReadableRange(reinterpret_cast<uintptr_t>(lookup.value), sizeof(float) * 4)) {
            return false;
        }

        const float rgba[4] = {
            color[0],
            color[1],
            color[2],
            1.0f,
        };
        const bool wrote = SafeWriteMemory(reinterpret_cast<uintptr_t>(lookup.value), rgba, sizeof(rgba));
        if (wrote) {
            MarkEditorTransformDirty();
            if (refreshAfterApply) {
                ForceSceneObjectVisualRefresh(sceneObject);
            }
            LOG_INFO("[EditorInspector] Material value pointer write sceneObject="
                << HexAddress(sceneObject)
                << " selector=0x" << std::hex << std::uppercase << selector << std::dec
                << " slot=" << static_cast<uint32_t>(slot)
                << " owner=" << HexAddress(owner)
                << " valuePtr=" << HexAddress(reinterpret_cast<uintptr_t>(lookup.value)));
        }
        return wrote;
    }

    static int WriteMaterialParameterValuePtrsToObjects(
        const std::vector<uintptr_t>& sceneObjects,
        uint32_t manualSelector,
        uint8_t slot,
        const float color[3],
        bool refreshAfterApply,
        int* outAttempts = nullptr) {
        int attempts = 0;
        int appliedCount = 0;
        for (uintptr_t sceneObject : sceneObjects) {
            std::vector<uint32_t> selectors;
            CollectMaterialSelectorCandidates(sceneObject, selectors);
            if (manualSelector != 0
                && std::find(selectors.begin(), selectors.end(), manualSelector) == selectors.end()) {
                selectors.push_back(manualSelector);
            }

            for (uint32_t selector : selectors) {
                ++attempts;
                if (WriteMaterialParameterValuePtr(
                    sceneObject,
                    selector,
                    slot,
                    color,
                    refreshAfterApply)) {
                    ++appliedCount;
                }
            }
        }
        if (outAttempts) {
            *outAttempts = attempts;
        }
        return appliedCount;
    }

    static bool ArmMaterialParameterTrace(
        const std::vector<uintptr_t>& sceneObjects,
        uint32_t manualSelector,
        uint8_t slot) {
        for (uintptr_t sceneObject : sceneObjects) {
            std::vector<uint32_t> selectors;
            CollectMaterialSelectorCandidates(sceneObject, selectors);
            if (manualSelector != 0
                && std::find(selectors.begin(), selectors.end(), manualSelector) == selectors.end()) {
                selectors.push_back(manualSelector);
            }

            for (uint32_t selector : selectors) {
                MaterialParameterLookup lookup = {};
                uintptr_t owner = 0;
                uintptr_t setterAddress = 0;
                uintptr_t getterAddress = 0;
                if (!LookupMaterialParameter(
                    sceneObject,
                    selector,
                    slot,
                    lookup,
                    owner,
                    setterAddress,
                    getterAddress)
                    || lookup.value == nullptr) {
                    continue;
                }

                g_editorMaterialTraceEnabled = true;
                g_editorMaterialTraceSceneObject = sceneObject;
                g_editorMaterialTraceOwner = owner;
                g_editorMaterialTraceValuePtr = reinterpret_cast<uintptr_t>(lookup.value);
                g_editorMaterialTraceSelector = selector;
                g_editorMaterialTraceSlot = slot;
                g_editorMaterialTraceLastMonitorTick = 0;
                g_editorMaterialTraceChangeCount = 0;
                memset(g_editorMaterialTraceLastValue, 0, sizeof(g_editorMaterialTraceLastValue));
                if (IsReadableRange(g_editorMaterialTraceValuePtr, sizeof(g_editorMaterialTraceLastValue))) {
                    SafeReadMemory(
                        g_editorMaterialTraceValuePtr,
                        g_editorMaterialTraceLastValue,
                        sizeof(g_editorMaterialTraceLastValue));
                }

                LOG_INFO("[EditorInspector] Material trace armed sceneObject="
                    << HexAddress(sceneObject)
                    << " selector=0x" << std::hex << std::uppercase << selector << std::dec
                    << " slot=" << static_cast<uint32_t>(slot)
                    << " owner=" << HexAddress(owner)
                    << " valuePtr=" << HexAddress(g_editorMaterialTraceValuePtr)
                    << " setter=" << HexAddress(setterAddress)
                    << " current=(" << g_editorMaterialTraceLastValue[0]
                    << "," << g_editorMaterialTraceLastValue[1]
                    << "," << g_editorMaterialTraceLastValue[2]
                    << "," << g_editorMaterialTraceLastValue[3]
                    << ")");
                return true;
            }
        }
        return false;
    }

    static void __fastcall Hook_MaterialParameterSetBytes(
        void* parameterOwner,
        void* edxUnused,
        void* parameterHandle,
        const void* value,
        uint32_t size) {
        (void)edxUnused;
        if (!g_originalMaterialParameterSetBytes) {
            return;
        }

        g_originalMaterialParameterSetBytes(parameterOwner, parameterHandle, value, size);
    }

    static void EnsureMaterialParameterSetterHookInstalled(uintptr_t setterAddress) {
        (void)setterAddress;
        if (!g_editorMaterialSetterHookWarningLogged) {
            g_editorMaterialSetterHookWarningLogged = true;
            LOG_INFO("[EditorInspector] Material parameter vtable+0x74 hook disabled; signature is not a simple value setter");
        }
    }

    static void EnsureCreateMeshInstancesHookInstalled();
    static void EnsureApplyMaterialColorOverridesHookInstalled();

    static void UpdateStickyMaterialParameterOwners(const std::vector<uintptr_t>& sceneObjects) {
        uintptr_t owners[MAX_STICKY_MATERIAL_PARAM_OWNERS] = {};
        size_t ownerCount = 0;
        for (uintptr_t sceneObject : sceneObjects) {
            std::vector<uint32_t> selectors;
            CollectMaterialSelectorCandidates(sceneObject, selectors);
            const uint32_t manualSelector = static_cast<uint32_t>(g_editorMaterialTestSelector);
            if (manualSelector != 0
                && std::find(selectors.begin(), selectors.end(), manualSelector) == selectors.end()) {
                selectors.push_back(manualSelector);
            }

            const uint8_t baseSlot = static_cast<uint8_t>(g_editorMaterialTestSlot);
            for (uint32_t selector : selectors) {
                for (uint32_t slotOffset = 0; slotOffset < 2; ++slotOffset) {
                    uintptr_t owner = 0;
                    uintptr_t setterAddress = 0;
                    if (LookupMaterialParameterOwner(
                        sceneObject,
                        selector,
                        static_cast<uint8_t>(baseSlot + slotOffset),
                        owner,
                        setterAddress)) {
                        bool seen = false;
                        for (size_t i = 0; i < ownerCount; ++i) {
                            if (owners[i] == owner) {
                                seen = true;
                                break;
                            }
                        }
                        if (!seen && ownerCount < MAX_STICKY_MATERIAL_PARAM_OWNERS) {
                            owners[ownerCount++] = owner;
                        }
                        EnsureMaterialParameterSetterHookInstalled(setterAddress);
                    }
                }
            }
        }

        InterlockedExchange(&g_editorMaterialStickyParamOwnerCount, 0);
        for (size_t i = 0; i < ownerCount; ++i) {
            g_editorMaterialStickyParamOwners[i] = owners[i];
        }
        for (size_t i = ownerCount; i < MAX_STICKY_MATERIAL_PARAM_OWNERS; ++i) {
            g_editorMaterialStickyParamOwners[i] = 0;
        }
        InterlockedExchange(&g_editorMaterialStickyParamOwnerCount, static_cast<LONG>(ownerCount));
    }

    static int ApplyStickyMaterialColorOnce(const std::vector<uintptr_t>& sceneObjects, int* outAttempts) {
        const uint32_t manualSelector = static_cast<uint32_t>(g_editorMaterialTestSelector);
        return g_editorMaterialStickyUseAutoSelectors
            ? ApplyMaterialColorAutoSelectorsToObjects(
                sceneObjects,
                manualSelector,
                static_cast<uint8_t>(g_editorMaterialTestSlot),
                static_cast<uint8_t>(g_editorMaterialTestSourceMode),
                static_cast<uint8_t>(g_editorMaterialTestOverrideMode),
                static_cast<uint8_t>(g_editorMaterialTestSecondaryMode),
                g_editorMaterialTestColor,
                g_editorMaterialTestRefreshAfterApply,
                outAttempts)
            : ApplyMaterialColorOverrideToObjects(
                sceneObjects,
                manualSelector,
                static_cast<uint8_t>(g_editorMaterialTestSlot),
                static_cast<uint8_t>(g_editorMaterialTestSourceMode),
                static_cast<uint8_t>(g_editorMaterialTestOverrideMode),
                static_cast<uint8_t>(g_editorMaterialTestSecondaryMode),
                g_editorMaterialTestColor,
                g_editorMaterialTestRefreshAfterApply);
    }

    static int ArmStickyEditorMaterialOverride(const std::vector<uintptr_t>& sceneObjects, int* outAttempts) {
        if (outAttempts) {
            *outAttempts = 0;
        }
        if (sceneObjects.empty()) {
            g_editorMaterialStickyEnabled = false;
            g_editorMaterialStickySceneObjects.clear();
            InterlockedExchange(&g_editorMaterialStickyParamOwnerCount, 0);
            return 0;
        }

        g_editorMaterialStickyEnabled = true;
        g_editorMaterialStickySceneObjects = sceneObjects;
        g_editorMaterialStickyLastMonitorTick = 0;
        EnsureCreateMeshInstancesHookInstalled();
        EnsureApplyMaterialColorOverridesHookInstalled();
        UpdateStickyMaterialParameterOwners(sceneObjects);

        return ApplyStickyMaterialColorOnce(sceneObjects, outAttempts);
    }

    static uint32_t BuildStickyMaterialOverrideEntries(void* sceneObject, MaterialColorOverrideEntry* entries, uint32_t maxEntries) {
        if (!sceneObject || !entries || maxEntries == 0) {
            return 0;
        }

        std::vector<uint32_t> selectors;
        if (g_editorMaterialStickyUseAutoSelectors) {
            CollectMaterialSelectorCandidates(reinterpret_cast<uintptr_t>(sceneObject), selectors);
        }
        const uint32_t manualSelector = static_cast<uint32_t>(g_editorMaterialTestSelector);
        if (manualSelector != 0
            && std::find(selectors.begin(), selectors.end(), manualSelector) == selectors.end()) {
            selectors.push_back(manualSelector);
        }
        if (selectors.empty()) {
            return 0;
        }

        uint32_t entryCount = 0;
        const uint8_t baseSlot = static_cast<uint8_t>(g_editorMaterialTestSlot);
        for (uint32_t selector : selectors) {
            for (uint32_t slotOffset = 0; slotOffset < 2 && entryCount < maxEntries; ++slotOffset) {
                MaterialColorOverrideEntry& entry = entries[entryCount++];
                entry = {};
                entry.selector = selector;
                entry.slot = static_cast<uint8_t>(baseSlot + slotOffset);
                entry.sourceMode = static_cast<uint8_t>(g_editorMaterialTestSourceMode);
                entry.color[0] = g_editorMaterialTestColor[0];
                entry.color[1] = g_editorMaterialTestColor[1];
                entry.color[2] = g_editorMaterialTestColor[2];
                entry.color[3] = 1.0f;
                entry.overrideMode = static_cast<uint8_t>(g_editorMaterialTestOverrideMode);
                entry.secondaryMode = static_cast<uint8_t>(g_editorMaterialTestSecondaryMode);
                entry.colorCount = 1;
            }
        }
        return entryCount;
    }

    static void ApplyMaterialOverrideInsideCreateMeshHook(void* sceneObject) {
        if (!g_originalCreateMeshInstances || !IsStickyMaterialSceneObject(reinterpret_cast<uintptr_t>(sceneObject))) {
            return;
        }

        MaterialColorOverrideEntry entries[16] = {};
        const uint32_t entryCount = BuildStickyMaterialOverrideEntries(sceneObject, entries, 16);
        if (entryCount == 0) {
            return;
        }

        MaterialColorOverrideVector vector = {};
        vector.count = entryCount;
        vector.capacity = entryCount;
        vector.entries = entries;

        g_originalCreateMeshInstances(sceneObject, &vector, 1.0f);
    }

    static void ApplyMaterialOverrideInsideApplyOverrideHook(void* sceneObject) {
        if (!g_originalApplyMaterialColorOverrides || !IsStickyMaterialSceneObject(reinterpret_cast<uintptr_t>(sceneObject))) {
            return;
        }

        MaterialColorOverrideEntry entries[16] = {};
        const uint32_t entryCount = BuildStickyMaterialOverrideEntries(sceneObject, entries, 16);
        for (uint32_t i = 0; i < entryCount; ++i) {
            g_originalApplyMaterialColorOverrides(sceneObject, &entries[i], 1.0f);
        }
    }

    static void __cdecl Hook_CreateMeshInstances(void* sceneObject, void* overrideVector, float blend) {
        if (g_originalCreateMeshInstances) {
            g_originalCreateMeshInstances(sceneObject, overrideVector, blend);
        }
        ApplyMaterialOverrideInsideCreateMeshHook(sceneObject);
    }

    static void __cdecl Hook_ApplyMaterialColorOverrides(void* sceneObject, void* overrideEntry, float blend) {
        if (g_originalApplyMaterialColorOverrides) {
            g_originalApplyMaterialColorOverrides(sceneObject, overrideEntry, blend);
        }
        ApplyMaterialOverrideInsideApplyOverrideHook(sceneObject);
    }

    static void EnsureCreateMeshInstancesHookInstalled() {
        if (g_editorMaterialCreateMeshHookInstalled) {
            return;
        }

        CreateMeshInstancesFunc createMeshInstances = ResolveCreateMeshInstances();
        if (!createMeshInstances) {
            return;
        }

        MH_STATUS hookStatus = MH_CreateHook(
            reinterpret_cast<LPVOID>(createMeshInstances),
            reinterpret_cast<LPVOID>(&Hook_CreateMeshInstances),
            reinterpret_cast<LPVOID*>(&g_originalCreateMeshInstances));
        if (hookStatus == MH_OK || hookStatus == MH_ERROR_ALREADY_CREATED) {
            hookStatus = MH_EnableHook(reinterpret_cast<LPVOID>(createMeshInstances));
            if (hookStatus == MH_OK || hookStatus == MH_ERROR_ENABLED) {
                g_editorMaterialCreateMeshHookInstalled = true;
                LOG_INFO("[EditorInspector] create_mesh_instances hook installed for material color test");
            }
        }
        else {
            LOG_WARNING("[EditorInspector] Failed to create create_mesh_instances hook: "
                << MH_StatusToString(hookStatus));
        }
    }

    static void EnsureApplyMaterialColorOverridesHookInstalled() {
        if (g_editorMaterialApplyOverrideHookInstalled) {
            return;
        }

        ApplyMaterialColorOverridesFunc applyOverrides = ResolveApplyMaterialColorOverrides();
        if (!applyOverrides) {
            return;
        }

        MH_STATUS hookStatus = MH_CreateHook(
            reinterpret_cast<LPVOID>(applyOverrides),
            reinterpret_cast<LPVOID>(&Hook_ApplyMaterialColorOverrides),
            reinterpret_cast<LPVOID*>(&g_originalApplyMaterialColorOverrides));
        if (hookStatus == MH_OK || hookStatus == MH_ERROR_ALREADY_CREATED) {
            hookStatus = MH_EnableHook(reinterpret_cast<LPVOID>(applyOverrides));
            if (hookStatus == MH_OK || hookStatus == MH_ERROR_ENABLED) {
                g_editorMaterialApplyOverrideHookInstalled = true;
                LOG_INFO("[EditorInspector] MeshInstance_ApplyMaterialColorOverrides hook installed for material color test");
            }
        }
        else {
            LOG_WARNING("[EditorInspector] Failed to create MeshInstance_ApplyMaterialColorOverrides hook: "
                << MH_StatusToString(hookStatus));
        }
    }

    static void LogTrackObjectListSnapshot(const char* phase, int* objectList, int* valueList) {
        if (!g_editorUploadTraceArmed) {
            return;
        }

        int objectCount = 0;
        int objectCapacity = 0;
        uintptr_t objectEntries = 0;
        int valueCount = 0;
        int valueCapacity = 0;
        uintptr_t valueEntries = 0;
        if (objectList && IsReadableRange(reinterpret_cast<uintptr_t>(objectList), 0x0c)) {
            SafeReadValue(reinterpret_cast<uintptr_t>(objectList), objectCount);
            SafeReadValue(reinterpret_cast<uintptr_t>(objectList) + 0x04, objectCapacity);
            SafeReadValue(reinterpret_cast<uintptr_t>(objectList) + 0x08, objectEntries);
        }
        if (valueList && IsReadableRange(reinterpret_cast<uintptr_t>(valueList), 0x0c)) {
            SafeReadValue(reinterpret_cast<uintptr_t>(valueList), valueCount);
            SafeReadValue(reinterpret_cast<uintptr_t>(valueList) + 0x04, valueCapacity);
            SafeReadValue(reinterpret_cast<uintptr_t>(valueList) + 0x08, valueEntries);
        }

        LOG_INFO("[EditorUploadTrace] BuildTrackObjectLists " << phase
            << " objectCount=" << objectCount
            << " objectCapacity=" << objectCapacity
            << " objectEntries=" << (objectEntries ? HexAddress(objectEntries) : "<null>")
            << " valueCount=" << valueCount
            << " valueCapacity=" << valueCapacity
            << " valueEntries=" << (valueEntries ? HexAddress(valueEntries) : "<null>"));
    }

    static void CaptureTrackObjectListSnapshot(LONG traceIndex, int* objectList, int* valueList) {
        int objectCount = 0;
        uintptr_t objectEntries = 0;
        int valueCount = 0;
        uintptr_t valueEntries = 0;
        if (objectList && IsReadableRange(reinterpret_cast<uintptr_t>(objectList), 0x0c)) {
            SafeReadValue(reinterpret_cast<uintptr_t>(objectList), objectCount);
            SafeReadValue(reinterpret_cast<uintptr_t>(objectList) + 0x08, objectEntries);
        }
        if (valueList && IsReadableRange(reinterpret_cast<uintptr_t>(valueList), 0x0c)) {
            SafeReadValue(reinterpret_cast<uintptr_t>(valueList), valueCount);
            SafeReadValue(reinterpret_cast<uintptr_t>(valueList) + 0x08, valueEntries);
        }

        g_editorUploadTraceSnapshotValid = false;
        g_editorUploadTraceSnapshotIndex = static_cast<uint32_t>(traceIndex);
        g_editorUploadTraceSnapshotObjectCount = objectCount > 0
            ? static_cast<uint32_t>(objectCount)
            : 0;
        g_editorUploadTraceSnapshotValueCount = valueCount > 0
            ? static_cast<uint32_t>(valueCount)
            : 0;
        g_editorUploadTraceSnapshotObjectEntries = objectEntries;
        g_editorUploadTraceSnapshotValueEntries = valueEntries;
        memset(g_editorUploadTraceSnapshotObjects, 0, sizeof(g_editorUploadTraceSnapshotObjects));
        memset(g_editorUploadTraceSnapshotValues, 0, sizeof(g_editorUploadTraceSnapshotValues));

        const uint32_t objectCopyCount =
            g_editorUploadTraceSnapshotObjectCount < EDITOR_UPLOAD_TRACE_MAX_OBJECTS
            ? g_editorUploadTraceSnapshotObjectCount
            : EDITOR_UPLOAD_TRACE_MAX_OBJECTS;
        const uint32_t valueCopyCount =
            g_editorUploadTraceSnapshotValueCount < EDITOR_UPLOAD_TRACE_MAX_VALUES
            ? g_editorUploadTraceSnapshotValueCount
            : EDITOR_UPLOAD_TRACE_MAX_VALUES;

        bool copiedAny = false;
        if (objectEntries != 0
            && objectCopyCount != 0
            && IsReadableRange(objectEntries, objectCopyCount * sizeof(uintptr_t))) {
            copiedAny = SafeReadMemory(
                objectEntries,
                g_editorUploadTraceSnapshotObjects,
                objectCopyCount * sizeof(uintptr_t)) || copiedAny;
        }
        if (valueEntries != 0
            && valueCopyCount != 0
            && IsReadableRange(
                valueEntries,
                static_cast<size_t>(valueCopyCount) * EDITOR_UPLOAD_TRACE_VALUE_RECORD_SIZE)) {
            copiedAny = SafeReadMemory(
                valueEntries,
                g_editorUploadTraceSnapshotValues,
                static_cast<size_t>(valueCopyCount) * EDITOR_UPLOAD_TRACE_VALUE_RECORD_SIZE) || copiedAny;
        }

        g_editorUploadTraceSnapshotValid = copiedAny;
        LOG_INFO("[EditorUploadTrace] captured flat snapshot index=" << traceIndex
            << " objects=" << g_editorUploadTraceSnapshotObjectCount
            << " values=" << g_editorUploadTraceSnapshotValueCount
            << " copied=" << copiedAny);
    }

    static bool PublishScaleOverrideMatches(
        const EditorPublishScaleOverride& overrideEntry,
        uintptr_t objectPtr) {
        return overrideEntry.active
            && objectPtr != 0
            && (objectPtr == overrideEntry.selectedObject
                || objectPtr == overrideEntry.mappedObject
                || objectPtr == overrideEntry.editorScaleBackingObject
                || objectPtr == overrideEntry.sceneHolder
                || objectPtr == overrideEntry.firstMeshSceneObject);
    }

    static const EditorPublishScaleOverride* FindSingleActivePublishScaleOverride() {
        const EditorPublishScaleOverride* singleOverride = nullptr;
        const uint32_t now = GetTickCount();
        for (uint32_t i = 0; i < EDITOR_PUBLISH_SCALE_MAX_OVERRIDES; ++i) {
            const EditorPublishScaleOverride& overrideEntry = g_editorPublishScaleOverrides[i];
            if (!overrideEntry.active) {
                continue;
            }

            if (overrideEntry.updatedTick != 0
                && static_cast<uint32_t>(now - overrideEntry.updatedTick) > 30u * 60u * 1000u) {
                continue;
            }

            if (singleOverride != nullptr) {
                return nullptr;
            }
            singleOverride = &overrideEntry;
        }

        return singleOverride;
    }

    static bool HasActivePublishScaleOverride() {
        return FindSingleActivePublishScaleOverride() != nullptr;
    }

    static const EditorPublishScaleOverride* FindPublishScaleOverride(uintptr_t objectPtr) {
        const uint32_t now = GetTickCount();
        for (uint32_t i = 0; i < EDITOR_PUBLISH_SCALE_MAX_OVERRIDES; ++i) {
            const EditorPublishScaleOverride& overrideEntry = g_editorPublishScaleOverrides[i];
            if (!PublishScaleOverrideMatches(overrideEntry, objectPtr)) {
                continue;
            }

            if (overrideEntry.updatedTick != 0
                && static_cast<uint32_t>(now - overrideEntry.updatedTick) > 30u * 60u * 1000u) {
                continue;
            }

            return &overrideEntry;
        }

        return nullptr;
    }

    static void PatchPublishScaleValueRecord(
        uint32_t index,
        uintptr_t objectPtr,
        uintptr_t scaleAddress,
        const EditorPublishScaleOverride& overrideEntry,
        const char* matchReason) {
        float oldScale[3] = {};
        SafeReadMemory(scaleAddress, oldScale, sizeof(oldScale));
        if (SafeWriteMemory(scaleAddress, overrideEntry.scale, sizeof(overrideEntry.scale))) {
            LOG_INFO("[EditorUploadTrace] patched publish/save scale record index=" << index
                << " reason=" << matchReason
                << " object=" << (objectPtr ? HexAddress(objectPtr) : "<null>")
                << " old=(" << oldScale[0] << ", " << oldScale[1] << ", " << oldScale[2] << ")"
                << " new=(" << overrideEntry.scale[0] << ", " << overrideEntry.scale[1]
                << ", " << overrideEntry.scale[2] << ")"
                << " selected=" << (overrideEntry.selectedObject
                    ? HexAddress(overrideEntry.selectedObject)
                    : "<null>")
                << " mapped=" << (overrideEntry.mappedObject
                    ? HexAddress(overrideEntry.mappedObject)
                    : "<null>")
                << " backing=" << (overrideEntry.editorScaleBackingObject
                    ? HexAddress(overrideEntry.editorScaleBackingObject)
                    : "<null>")
                << " holder=" << (overrideEntry.sceneHolder
                    ? HexAddress(overrideEntry.sceneHolder)
                    : "<null>")
                << " mesh=" << (overrideEntry.firstMeshSceneObject
                    ? HexAddress(overrideEntry.firstMeshSceneObject)
                    : "<null>"));
        }
        else {
            LOG_WARNING("[EditorUploadTrace] failed to patch publish/save scale record index=" << index
                << " object=" << (objectPtr ? HexAddress(objectPtr) : "<null>")
                << " address=" << HexAddress(scaleAddress));
        }
    }

    static void PatchPublishScaleValueRecords(int* objectList, int* valueList) {
        if (!g_editorPublishScalePatchEnabled || objectList == nullptr || valueList == nullptr) {
            return;
        }

        int objectCount = 0;
        uintptr_t objectEntries = 0;
        int valueCount = 0;
        uintptr_t valueEntries = 0;
        if (!IsReadableRange(reinterpret_cast<uintptr_t>(objectList), 0x0c)
            || !IsReadableRange(reinterpret_cast<uintptr_t>(valueList), 0x0c)
            || !SafeReadValue(reinterpret_cast<uintptr_t>(objectList), objectCount)
            || !SafeReadValue(reinterpret_cast<uintptr_t>(objectList) + 0x08, objectEntries)
            || !SafeReadValue(reinterpret_cast<uintptr_t>(valueList), valueCount)
            || !SafeReadValue(reinterpret_cast<uintptr_t>(valueList) + 0x08, valueEntries)
            || objectCount <= 0
            || valueCount <= 0
            || objectEntries == 0
            || valueEntries == 0) {
            return;
        }

        const uint32_t pairCount = static_cast<uint32_t>(objectCount < valueCount ? objectCount : valueCount);
        if (!IsReadableRange(objectEntries, pairCount * sizeof(uintptr_t))
            || !IsReadableRange(
                valueEntries,
                static_cast<size_t>(pairCount) * EDITOR_UPLOAD_TRACE_VALUE_RECORD_SIZE)) {
            return;
        }

        const EditorPublishScaleOverride* singleOverride =
            pairCount == 1 ? FindSingleActivePublishScaleOverride() : nullptr;
        for (uint32_t i = 0; i < pairCount; ++i) {
            uintptr_t objectPtr = 0;
            if (!SafeReadValue(objectEntries + i * sizeof(uintptr_t), objectPtr)) {
                continue;
            }

            const EditorPublishScaleOverride* overrideEntry = FindPublishScaleOverride(objectPtr);
            const char* matchReason = "object-pointer";
            if (overrideEntry == nullptr && singleOverride != nullptr) {
                overrideEntry = singleOverride;
                matchReason = "single-record-fallback";
            }
            if (overrideEntry == nullptr) {
                continue;
            }

            const uintptr_t scaleAddress =
                valueEntries
                + static_cast<uintptr_t>(i) * EDITOR_UPLOAD_TRACE_VALUE_RECORD_SIZE
                + EDITOR_PUBLISH_SCALE_VALUE_OFFSET;
            PatchPublishScaleValueRecord(i, objectPtr, scaleAddress, *overrideEntry, matchReason);
        }
    }

    static void DumpEditorUploadTraceSnapshot() {
        if (!g_editorUploadTraceSnapshotValid) {
            LOG_WARNING("[EditorUploadTrace] no flat upload snapshot captured yet");
            return;
        }

        LOG_INFO("[EditorUploadTrace] flat snapshot dump index=" << g_editorUploadTraceSnapshotIndex
            << " objectCount=" << g_editorUploadTraceSnapshotObjectCount
            << " valueCount=" << g_editorUploadTraceSnapshotValueCount
            << " objectEntries=" << (g_editorUploadTraceSnapshotObjectEntries
                ? HexAddress(g_editorUploadTraceSnapshotObjectEntries)
                : "<null>")
            << " valueEntries=" << (g_editorUploadTraceSnapshotValueEntries
                ? HexAddress(g_editorUploadTraceSnapshotValueEntries)
                : "<null>"));

        const uint32_t objectDumpCount =
            g_editorUploadTraceSnapshotObjectCount < EDITOR_UPLOAD_TRACE_MAX_OBJECTS
            ? g_editorUploadTraceSnapshotObjectCount
            : EDITOR_UPLOAD_TRACE_MAX_OBJECTS;
        for (uint32_t i = 0; i < objectDumpCount; ++i) {
            LOG_INFO("[EditorUploadTrace] objectPtr[" << i << "]="
                << (g_editorUploadTraceSnapshotObjects[i]
                    ? HexAddress(g_editorUploadTraceSnapshotObjects[i])
                    : "<null>"));
        }

        const uint32_t valueDumpCount =
            g_editorUploadTraceSnapshotValueCount < EDITOR_UPLOAD_TRACE_MAX_VALUES
            ? g_editorUploadTraceSnapshotValueCount
            : EDITOR_UPLOAD_TRACE_MAX_VALUES;
        for (uint32_t i = 0; i < valueDumpCount; ++i) {
            const uint8_t* record = g_editorUploadTraceSnapshotValues[i];
            std::ostringstream hexLine;
            hexLine << "[EditorUploadTrace] valueRecord[" << i << "] bytes";
            for (uint32_t offset = 0; offset < EDITOR_UPLOAD_TRACE_VALUE_RECORD_SIZE; ++offset) {
                if ((offset % 16) == 0) {
                    hexLine << " |" << std::hex << std::uppercase;
                    if (offset < 0x10) {
                        hexLine << "0";
                    }
                    hexLine << offset << ":";
                }
                const uint32_t byteValue = record[offset];
                if (byteValue < 0x10) {
                    hexLine << " 0";
                }
                else {
                    hexLine << " ";
                }
                hexLine << byteValue;
            }
            LOG_INFO(hexLine.str());

            for (uint32_t offset = 0; offset + sizeof(uint32_t) <= EDITOR_UPLOAD_TRACE_VALUE_RECORD_SIZE; offset += 4) {
                uint32_t raw = 0;
                float asFloat = 0.0f;
                memcpy(&raw, record + offset, sizeof(raw));
                memcpy(&asFloat, record + offset, sizeof(asFloat));
                LOG_INFO("[EditorUploadTrace] valueRecord[" << i << "] +0x"
                    << std::hex << std::uppercase << offset << std::dec
                    << " u32=0x" << std::hex << std::uppercase << raw << std::dec
                    << " float=" << asFloat);
            }
        }
    }

    static void __fastcall Hook_BuildTrackObjectLists(void* thisPtr, void* edxUnused, int* objectList, int* valueList) {
        (void)edxUnused;
        const LONG traceIndex = g_editorUploadTraceArmed
            ? InterlockedIncrement(&g_editorUploadTraceCount)
            : 0;
        if (traceIndex > 0 && traceIndex <= 16) {
            LogTrackObjectListSnapshot("before", objectList, valueList);
        }
        if (g_originalBuildTrackObjectLists) {
            g_originalBuildTrackObjectLists(thisPtr, objectList, valueList);
        }
        PatchPublishScaleValueRecords(objectList, valueList);
        if (traceIndex > 0 && traceIndex <= 16) {
            LogTrackObjectListSnapshot("after", objectList, valueList);
            CaptureTrackObjectListSnapshot(traceIndex, objectList, valueList);
            if (traceIndex == 16) {
                g_editorUploadTraceArmed = false;
                LOG_INFO("[EditorUploadTrace] track object list trace auto-disarmed after 16 captures");
            }
        }
    }

    static void EnsureEditorUploadTraceHooksInstalled() {
        if (g_editorUploadTrackListTraceHookInstalled) {
            return;
        }

        BuildTrackObjectListsFunc trackObjectsTarget = ResolveBuildTrackObjectLists();
        bool installed = false;

        if (trackObjectsTarget) {
            MH_STATUS hookStatus = MH_CreateHook(
                reinterpret_cast<LPVOID>(trackObjectsTarget),
                reinterpret_cast<LPVOID>(&Hook_BuildTrackObjectLists),
                reinterpret_cast<LPVOID*>(&g_originalBuildTrackObjectLists));
            if (hookStatus == MH_OK || hookStatus == MH_ERROR_ALREADY_CREATED) {
                hookStatus = MH_EnableHook(reinterpret_cast<LPVOID>(trackObjectsTarget));
                installed = hookStatus == MH_OK || hookStatus == MH_ERROR_ENABLED;
            }
            else {
                LOG_WARNING("[EditorUploadTrace] Failed to create BuildTrackObjectLists hook: "
                    << MH_StatusToString(hookStatus));
            }
        }

        g_editorUploadTrackListTraceHookInstalled = installed;
        LOG_INFO("[EditorUploadTrace] track-list hook " << (installed ? "installed" : "not installed")
            << " BuildTrackObjectLists=0x" << std::hex << std::uppercase << GetBuildTrackObjectListsRva() << std::dec);
    }

    static void LogEditorTrackSaveTrace(
        const char* name,
        LONG index,
        void* thisPtr,
        void* arg0,
        uintptr_t arg1,
        uint32_t result,
        bool afterCall) {
        if (!HasActivePublishScaleOverride() && !g_editorUploadTraceArmed) {
            return;
        }

        LOG_INFO("[EditorTrackSaveTrace] " << (afterCall ? "after " : "before ")
            << name
            << " index=" << index
            << " this=" << (thisPtr ? HexAddress(reinterpret_cast<uintptr_t>(thisPtr)) : "<null>")
            << " arg0=" << (arg0 ? HexAddress(reinterpret_cast<uintptr_t>(arg0)) : "<null>")
            << " arg1=0x" << std::hex << std::uppercase << arg1 << std::dec
            << " result=0x" << std::hex << std::uppercase << result << std::dec);
    }

    static uint32_t __fastcall Hook_SerializeTrackData(
        void* thisPtr,
        void* edxUnused,
        void* outStream,
        char includeBikeState) {
        (void)edxUnused;
        const LONG traceIndex = InterlockedIncrement(&g_editorTrackSaveTraceCount);
        LogEditorTrackSaveTrace(
            "SerializeTrackData",
            traceIndex,
            thisPtr,
            outStream,
            static_cast<uintptr_t>(static_cast<unsigned char>(includeBikeState)),
            0,
            false);
        uint32_t result = 0;
        if (g_originalSerializeTrackData) {
            result = g_originalSerializeTrackData(thisPtr, outStream, includeBikeState);
        }
        LogEditorTrackSaveTrace(
            "SerializeTrackData",
            traceIndex,
            thisPtr,
            outStream,
            static_cast<uintptr_t>(static_cast<unsigned char>(includeBikeState)),
            result,
            true);
        return result;
    }

    static uint32_t __cdecl Hook_SerializeTrackDataToPacket(int packetBuilder) {
        const LONG traceIndex = InterlockedIncrement(&g_editorTrackSaveTraceCount);
        LogEditorTrackSaveTrace(
            "SerializeTrackDataToPacket",
            traceIndex,
            nullptr,
            reinterpret_cast<void*>(packetBuilder),
            0,
            0,
            false);
        uint32_t result = 0;
        if (g_originalSerializeTrackDataToPacket) {
            result = g_originalSerializeTrackDataToPacket(packetBuilder);
        }
        LogEditorTrackSaveTrace(
            "SerializeTrackDataToPacket",
            traceIndex,
            nullptr,
            reinterpret_cast<void*>(packetBuilder),
            0,
            result,
            true);
        return result;
    }

    static uint32_t __fastcall Hook_ProcessTrackHashesAndSave(
        void* thisPtr,
        void* edxUnused,
        uint32_t callbackOrTask,
        int* outputStream) {
        (void)edxUnused;
        const LONG traceIndex = InterlockedIncrement(&g_editorTrackSaveTraceCount);
        LogEditorTrackSaveTrace(
            "ProcessTrackHashesAndSave",
            traceIndex,
            thisPtr,
            outputStream,
            callbackOrTask,
            0,
            false);
        uint32_t result = 0;
        if (g_originalProcessTrackHashesAndSave) {
            result = g_originalProcessTrackHashesAndSave(thisPtr, callbackOrTask, outputStream);
        }
        LogEditorTrackSaveTrace(
            "ProcessTrackHashesAndSave",
            traceIndex,
            thisPtr,
            outputStream,
            callbackOrTask,
            result,
            true);
        return result;
    }

    static bool CreateAndEnableTraceHook(
        LPVOID target,
        LPVOID detour,
        LPVOID* original,
        const char* name) {
        if (target == nullptr || detour == nullptr || original == nullptr) {
            return false;
        }

        MH_STATUS hookStatus = MH_CreateHook(target, detour, original);
        if (hookStatus != MH_OK && hookStatus != MH_ERROR_ALREADY_CREATED) {
            LOG_WARNING("[EditorTrackSaveTrace] Failed to create " << name
                << " hook: " << MH_StatusToString(hookStatus));
            return false;
        }

        hookStatus = MH_EnableHook(target);
        if (hookStatus != MH_OK && hookStatus != MH_ERROR_ENABLED) {
            LOG_WARNING("[EditorTrackSaveTrace] Failed to enable " << name
                << " hook: " << MH_StatusToString(hookStatus));
            return false;
        }

        return true;
    }

    static void EnsureEditorTrackSaveTraceHooksInstalled() {
        LOG_INFO("[EditorTrackSaveTrace] serializer hooks disabled; previous live hook path was unsafe during save");
        return;
        if (g_editorTrackSaveTraceHooksInstalled) {
            return;
        }

        bool installedAny = false;
        installedAny = CreateAndEnableTraceHook(
            reinterpret_cast<LPVOID>(ResolveSerializeTrackData()),
            reinterpret_cast<LPVOID>(&Hook_SerializeTrackData),
            reinterpret_cast<LPVOID*>(&g_originalSerializeTrackData),
            "SerializeTrackData") || installedAny;
        installedAny = CreateAndEnableTraceHook(
            reinterpret_cast<LPVOID>(ResolveSerializeTrackDataToPacket()),
            reinterpret_cast<LPVOID>(&Hook_SerializeTrackDataToPacket),
            reinterpret_cast<LPVOID*>(&g_originalSerializeTrackDataToPacket),
            "SerializeTrackDataToPacket") || installedAny;
        installedAny = CreateAndEnableTraceHook(
            reinterpret_cast<LPVOID>(ResolveProcessTrackHashesAndSave()),
            reinterpret_cast<LPVOID>(&Hook_ProcessTrackHashesAndSave),
            reinterpret_cast<LPVOID*>(&g_originalProcessTrackHashesAndSave),
            "ProcessTrackHashesAndSave") || installedAny;

        g_editorTrackSaveTraceHooksInstalled = installedAny;
        LOG_INFO("[EditorTrackSaveTrace] hooks " << (installedAny ? "installed" : "not installed")
            << " SerializeTrackData=0x" << std::hex << std::uppercase << GetSerializeTrackDataRva()
            << " SerializeTrackDataToPacket=0x" << GetSerializeTrackDataToPacketRva()
            << " ProcessTrackHashesAndSave=0x" << GetProcessTrackHashesAndSaveRva()
            << std::dec);
    }

    static void RememberEditorPublishScaleOverride(
        const SelectedEditorObject& selected,
        const float scale[3]) {
        if (!g_editorPublishScalePatchEnabled || scale == nullptr || selected.selectedObject == 0) {
            return;
        }

        if (g_editorPublishScalePatchEnabled) {
            EnsureEditorUploadTraceHooksInstalled();
        }
        if (g_editorPublishScalePatchEnabled && !g_editorUploadTrackListTraceHookInstalled) {
            return;
        }

        uint32_t slot = EDITOR_PUBLISH_SCALE_MAX_OVERRIDES;
        uint32_t oldestSlot = 0;
        uint32_t oldestTick = 0xffffffffu;
        for (uint32_t i = 0; i < EDITOR_PUBLISH_SCALE_MAX_OVERRIDES; ++i) {
            EditorPublishScaleOverride& overrideEntry = g_editorPublishScaleOverrides[i];
            if (overrideEntry.active
                && (overrideEntry.selectedObject == selected.selectedObject
                    || overrideEntry.mappedObject == selected.mappedObject
                    || overrideEntry.editorScaleBackingObject == selected.editorScaleBackingObject
                    || overrideEntry.sceneHolder == selected.sceneHolder
                    || overrideEntry.firstMeshSceneObject == selected.firstMeshSceneObject)) {
                slot = i;
                break;
            }

            if (!overrideEntry.active) {
                slot = i;
                break;
            }

            if (overrideEntry.updatedTick < oldestTick) {
                oldestTick = overrideEntry.updatedTick;
                oldestSlot = i;
            }
        }

        if (slot == EDITOR_PUBLISH_SCALE_MAX_OVERRIDES) {
            slot = oldestSlot;
        }

        EditorPublishScaleOverride& overrideEntry = g_editorPublishScaleOverrides[slot];
        overrideEntry.active = true;
        overrideEntry.selectedObject = selected.selectedObject;
        overrideEntry.mappedObject = selected.mappedObject;
        overrideEntry.editorScaleBackingObject = selected.editorScaleBackingObject;
        overrideEntry.sceneHolder = selected.sceneHolder;
        overrideEntry.firstMeshSceneObject = selected.firstMeshSceneObject;
        overrideEntry.scale[0] = scale[0];
        overrideEntry.scale[1] = scale[1];
        overrideEntry.scale[2] = scale[2];
        overrideEntry.updatedTick = GetTickCount();

        LOG_VERBOSE("[EditorUploadTrace] remembered publish scale slot=" << slot
            << " selected=" << HexAddress(selected.selectedObject)
            << " mapped=" << (selected.mappedObject ? HexAddress(selected.mappedObject) : "<null>")
            << " backing=" << (selected.editorScaleBackingObject ? HexAddress(selected.editorScaleBackingObject) : "<null>")
            << " holder=" << (selected.sceneHolder ? HexAddress(selected.sceneHolder) : "<null>")
            << " mesh=" << (selected.firstMeshSceneObject ? HexAddress(selected.firstMeshSceneObject) : "<null>")
            << " scale=(" << scale[0] << ", " << scale[1] << ", " << scale[2] << ")");
    }

    static bool CaptureEngineSceneObjects(
        uintptr_t sceneRoot,
        uint32_t objectType,
        uint32_t objectSubtype,
        uint32_t useVirtualType,
        std::vector<uintptr_t>& outObjects) {
        outObjects.clear();
        if (sceneRoot == 0 || !IsReadableRange(sceneRoot, 0x24)) {
            return false;
        }

        CollectSceneObjectsByTypeFunc collect = ResolveCollectSceneObjectsByType();
        if (!collect) {
            return false;
        }

        EditorSceneObjectSnapshot snapshot = {};
        __try {
            collect(
                reinterpret_cast<void*>(sceneRoot),
                &snapshot,
                objectType,
                objectSubtype,
                useVirtualType,
                0);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }

        if (snapshot.count > MAX_EDITOR_SCENE_OBJECT_SNAPSHOT) {
            snapshot.count = MAX_EDITOR_SCENE_OBJECT_SNAPSHOT;
        }

        for (uint32_t i = 0; i < snapshot.count; ++i) {
            uintptr_t object = reinterpret_cast<uintptr_t>(snapshot.objects[i]);
            if (object != 0 && IsReadableRange(object, 0x10)) {
                outObjects.push_back(object);
            }
        }
        return true;
    }

    static bool CaptureEngineSceneObjects(
        uintptr_t sceneRoot,
        uint32_t objectType,
        uint32_t objectSubtype,
        std::vector<uintptr_t>& outObjects) {
        return CaptureEngineSceneObjects(sceneRoot, objectType, objectSubtype, 0, outObjects);
    }

    static void AppendUniqueSceneObjects(std::vector<uintptr_t>& target, const std::vector<uintptr_t>& source) {
        for (uintptr_t sceneObject : source) {
            if (sceneObject == 0) {
                continue;
            }
            if (std::find(target.begin(), target.end(), sceneObject) == target.end()) {
                target.push_back(sceneObject);
            }
        }
    }

    static void CollectMaterialColorTestSceneObjects(const SelectedEditorObject& selected, std::vector<uintptr_t>& outObjects) {
        outObjects.clear();
        const uintptr_t roots[] = {
            selected.selectedObject,
            selected.resourceSceneRoot,
            selected.firstMeshSceneObject,
        };

        const struct {
            uint32_t type;
            uint32_t subtype;
            uint32_t useVirtualType;
        } queries[] = {
            {1, 3, 0},
            {3, 1, 0},
            {1, 2, 1},
            {1, 2, 0},
            {1, 15, 0},
            {1, 1, 0},
        };

        for (uintptr_t root : roots) {
            for (const auto& query : queries) {
                std::vector<uintptr_t> found;
                CaptureEngineSceneObjects(root, query.type, query.subtype, query.useVirtualType, found);
                AppendUniqueSceneObjects(outObjects, found);
            }
        }
    }

    static void CollectMaterialSelectorCandidates(uintptr_t sceneObject, std::vector<uint32_t>& selectors) {
        const uintptr_t offsets[] = {
            0x20,
            0x24,
            0x38,
            0x58,
        };

        for (uintptr_t offset : offsets) {
            uint32_t value = 0;
            if (SafeReadValue(sceneObject + offset, value)
                && value != 0
                && std::find(selectors.begin(), selectors.end(), value) == selectors.end()) {
                selectors.push_back(value);
            }
        }
    }

    static std::string BuildMaterialSelectorSummary(uintptr_t sceneObject) {
        if (sceneObject == 0 || !IsReadableRange(sceneObject, 0x5c)) {
            return "<unavailable>";
        }

        uint32_t value20 = 0;
        uint32_t value24 = 0;
        uint32_t value38 = 0;
        uint32_t value58 = 0;
        SafeReadValue(sceneObject + 0x20, value20);
        SafeReadValue(sceneObject + 0x24, value24);
        SafeReadValue(sceneObject + 0x38, value38);
        SafeReadValue(sceneObject + 0x58, value58);

        std::ostringstream ss;
        ss << "+20=0x" << std::hex << std::uppercase << value20
            << " +24=0x" << value24
            << " +38=0x" << value38
            << " +58=0x" << value58
            << std::dec;
        return ss.str();
    }

    static void AppendMaterialOwnerUpstreamSummary(std::ostringstream& ss, uintptr_t owner) {
        if (owner == 0 || !IsReadableRange(owner, 0x1b4)) {
            ss << "    ownerUpstream=<unavailable>\n";
            return;
        }

        uintptr_t owner104 = 0;
        uintptr_t owner174 = 0;
        uintptr_t owner17c = 0;
        uintptr_t owner1ac = 0;
        uintptr_t owner1b0 = 0;
        uint32_t owner1b4 = 0;
        uintptr_t owner1d0 = 0;
        uintptr_t owner1d4 = 0;
        SafeReadValue(owner + 0x104, owner104);
        SafeReadValue(owner + 0x174, owner174);
        SafeReadValue(owner + 0x17c, owner17c);
        SafeReadValue(owner + 0x1ac, owner1ac);
        SafeReadValue(owner + 0x1b0, owner1b0);
        SafeReadValue(owner + 0x1b4, owner1b4);
        SafeReadValue(owner + 0x1d0, owner1d0);
        SafeReadValue(owner + 0x1d4, owner1d4);

        ss << "    ownerUpstream:"
            << " +104=" << (owner104 ? HexAddress(owner104) : "<null>")
            << " +174=" << (owner174 ? HexAddress(owner174) : "<null>")
            << " +17c=" << (owner17c ? HexAddress(owner17c) : "<null>")
            << " +1ac(ptr)=" << (owner1ac ? HexAddress(owner1ac) : "<null>")
            << " +1b0(ptr)=" << (owner1b0 ? HexAddress(owner1b0) : "<null>")
            << " +1b4(count?)=" << owner1b4
            << " +1d0(ptr)=" << (owner1d0 ? HexAddress(owner1d0) : "<null>")
            << " +1d4(ptr)=" << (owner1d4 ? HexAddress(owner1d4) : "<null>")
            << "\n";

        if (owner104 != 0 && IsReadableRange(owner104, 0x18)) {
            uintptr_t owner104_0c = 0;
            uintptr_t owner104_14 = 0;
            SafeReadValue(owner104 + 0x0c, owner104_0c);
            SafeReadValue(owner104 + 0x14, owner104_14);
            ss << "    owner+104:"
                << " [0c]=" << (owner104_0c ? HexAddress(owner104_0c) : "<null>")
                << " [14]=" << (owner104_14 ? HexAddress(owner104_14) : "<null>");
            if (owner104_0c != 0 && IsReadableRange(owner104_0c + 0x14, sizeof(uintptr_t))) {
                uintptr_t owner104_0c_14 = 0;
                SafeReadValue(owner104_0c + 0x14, owner104_0c_14);
                ss << " [[0c]+14]=" << (owner104_0c_14 ? HexAddress(owner104_0c_14) : "<null>");
                if (owner104_0c_14 != 0 && IsReadableRange(owner104_0c_14 + 0x90, sizeof(uintptr_t))) {
                    uintptr_t owner104_0c_14_90 = 0;
                    SafeReadValue(owner104_0c_14 + 0x90, owner104_0c_14_90);
                    ss << " [[[0c]+14]+90]=" << (owner104_0c_14_90 ? HexAddress(owner104_0c_14_90) : "<null>");
                }
            }
            ss << "\n";
        }

        if (owner1b0 != 0 && owner1b4 != 0 && owner1b4 < 4096 && IsReadableRange(owner1b0, owner1b4 * 8)) {
            const uint32_t entryCount = owner1b4 < 8 ? owner1b4 : 8;
            for (uint32_t i = 0; i < entryCount; ++i) {
                const uintptr_t entry = owner1b0 + i * 8;
                uintptr_t keyOrPtr = 0;
                uintptr_t descriptor = 0;
                SafeReadValue(entry, keyOrPtr);
                SafeReadValue(entry + 4, descriptor);
                ss << "    owner+1b0[" << i << "]:"
                    << " a=" << (keyOrPtr ? HexAddress(keyOrPtr) : "<null>")
                    << " b=" << (descriptor ? HexAddress(descriptor) : "<null>");
                if (descriptor != 0 && IsReadableRange(descriptor, 0x1a0)) {
                    uintptr_t descriptor10 = 0;
                    uintptr_t descriptor48 = 0;
                    uintptr_t descriptor154 = 0;
                    uintptr_t descriptor15c = 0;
                    SafeReadValue(descriptor + 0x10, descriptor10);
                    SafeReadValue(descriptor + 0x48, descriptor48);
                    SafeReadValue(descriptor + 0x154, descriptor154);
                    SafeReadValue(descriptor + 0x15c, descriptor15c);
                    ss << " b+10=" << (descriptor10 ? HexAddress(descriptor10) : "<null>")
                        << " b+48=0x" << std::hex << std::uppercase << descriptor48 << std::dec
                        << " b+154=" << (descriptor154 ? HexAddress(descriptor154) : "<null>")
                        << " b+15c=" << (descriptor15c ? HexAddress(descriptor15c) : "<null>");
                }
                ss << "\n";
            }
        }

        if (owner1ac != 0 && IsReadableRange(owner1ac, 0x80)) {
            AppendRawDwordFields(ss, "    owner+1ac pointer target +0x000..0x07f", owner1ac, 0x80);
        }
        if (owner1b0 != 0 && IsReadableRange(owner1b0, 0x100)) {
            AppendRawDwordFields(ss, "    owner+1b0 pointer target +0x000..0x0ff", owner1b0, 0x100);
        }
        if (owner1d0 != 0 && owner1d0 != owner1ac && IsReadableRange(owner1d0, 0x80)) {
            AppendRawDwordFields(ss, "    owner+1d0 pointer target +0x000..0x07f", owner1d0, 0x80);
        }
        if (owner1d4 != 0 && owner1d4 != owner1b0 && IsReadableRange(owner1d4, 0x100)) {
            AppendRawDwordFields(ss, "    owner+1d4 pointer target +0x000..0x0ff", owner1d4, 0x100);
        }
    }

    static void AppendMaterialParameterLookupReport(
        std::ostringstream& ss,
        const SelectedEditorObject& selected) {
        std::vector<uintptr_t> materialObjects;
        CollectMaterialColorTestSceneObjects(selected, materialObjects);
        ss << "\n[Material Parameter Lookup Candidates]\n";
        ss << "materialObjectCount=" << materialObjects.size() << "\n";

        int objectIndex = 0;
        for (uintptr_t sceneObject : materialObjects) {
            if (objectIndex >= 12) {
                ss << "... truncated material object report after 12 objects\n";
                break;
            }

            ss << "materialObject[" << objectIndex << "]=" << HexAddress(sceneObject)
                << " selectors=" << BuildMaterialSelectorSummary(sceneObject) << "\n";

            std::vector<uint32_t> selectors;
            CollectMaterialSelectorCandidates(sceneObject, selectors);
            const uint32_t manualSelector = static_cast<uint32_t>(g_editorMaterialTestSelector);
            if (manualSelector != 0
                && std::find(selectors.begin(), selectors.end(), manualSelector) == selectors.end()) {
                selectors.push_back(manualSelector);
            }

            int selectorIndex = 0;
            for (uint32_t selector : selectors) {
                if (selectorIndex >= 8) {
                    ss << "  ... truncated selector report after 8 selectors\n";
                    break;
                }
                for (uint8_t slot = 0; slot < 2; ++slot) {
                    MaterialParameterLookup lookup = {};
                    uintptr_t owner = 0;
                    uintptr_t setterAddress = 0;
                    uintptr_t getterAddress = 0;
                    const bool found = LookupMaterialParameter(
                        sceneObject,
                        selector,
                        slot,
                        lookup,
                        owner,
                        setterAddress,
                        getterAddress);
                    if (!found) {
                        continue;
                    }

                    float value[4] = {};
                    if (lookup.value != nullptr
                        && IsReadableRange(reinterpret_cast<uintptr_t>(lookup.value), sizeof(value))) {
                        SafeReadMemory(reinterpret_cast<uintptr_t>(lookup.value), value, sizeof(value));
                    }

                    ss << "  selector=0x" << std::hex << std::uppercase << selector
                        << std::dec
                        << " slot=" << static_cast<uint32_t>(slot)
                        << "(" << DescribeMaterialColorSlot(slot) << ")"
                        << " getter=" << HexAddress(getterAddress)
                        << " owner=" << HexAddress(owner)
                        << " setter=" << HexAddress(setterAddress)
                        << " valuePtr=" << (lookup.value ? HexAddress(reinterpret_cast<uintptr_t>(lookup.value)) : "<null>")
                        << " lookup.count=" << lookup.count
                        << " lookup.unknown18=0x" << std::hex << std::uppercase << lookup.unknown18 << std::dec
                        << " value=(" << value[0] << "," << value[1] << "," << value[2] << "," << value[3] << ")"
                        << "\n";
                    AppendMaterialOwnerUpstreamSummary(ss, owner);
                    AppendRawDwordFields(ss, "    Material Parameter Owner Raw +0x000..0x23f", owner, 0x240);
                    if (owner != 0 && IsReadableRange(owner + 0x240, 0x140)) {
                        AppendRawDwordFields(ss, "    Material Parameter Owner Tail Raw +0x240..0x37f", owner + 0x240, 0x140);
                    }
                    if (lookup.value != nullptr) {
                        const uintptr_t valueAddress = reinterpret_cast<uintptr_t>(lookup.value);
                        const uintptr_t valueWindow = valueAddress >= 0x100 ? valueAddress - 0x100 : valueAddress;
                        AppendRawDwordFields(ss, "    Material Value Pointer Surrounding Raw -0x100..+0xff", valueWindow, 0x200);
                    }
                }
                ++selectorIndex;
            }
            ++objectIndex;
        }
    }

    static uintptr_t ResolveTrackEventMaterialTargetForInspector() {
        const uintptr_t gameManager = ResolveGameManagerForEditorInspector();
        uintptr_t trackManager = 0;
        uintptr_t target = 0;
        if (gameManager == 0
            || !SafeReadValue(gameManager + 0x108, trackManager)
            || !IsReadableRange(trackManager + 0x178, sizeof(uintptr_t))
            || !SafeReadValue(trackManager + 0x174, target)
            || !IsReadableRange(target, 0x120)) {
            return 0;
        }
        return target;
    }

    static uintptr_t FindTrackEventTreeMapNode(uintptr_t map, uintptr_t key) {
        if (map == 0 || !IsReadableRange(map + 0x0c, sizeof(uintptr_t))) {
            return 0;
        }

        uintptr_t node = 0;
        uintptr_t candidate = 0;
        SafeReadValue(map + 0x08, node);
        int guard = 0;
        while (node != 0 && IsReadableRange(node, 0x18) && guard++ < 4096) {
            uintptr_t nodeKey = 0;
            uintptr_t next = 0;
            SafeReadValue(node + 0x10, nodeKey);
            if (nodeKey < key) {
                SafeReadValue(node + 0x04, next);
            }
            else {
                candidate = node;
                SafeReadValue(node, next);
            }
            node = next;
        }

        if (candidate != 0 && candidate != map && IsReadableRange(candidate + 0x14, sizeof(uintptr_t))) {
            uintptr_t candidateKey = 0;
            SafeReadValue(candidate + 0x10, candidateKey);
            if (candidateKey <= key) {
                return candidate;
            }
        }
        return 0;
    }

    static void LogTrackEventMaterialMapState(
        const char* phase,
        uintptr_t dispatcher,
        uintptr_t sceneObject,
        uintptr_t eventId) {
        if (dispatcher == 0 || sceneObject == 0 || phase == nullptr) {
            return;
        }

        const uintptr_t materialMap = dispatcher + 0x4c;
        const uintptr_t meshColorMap = dispatcher + 0x94;
        uintptr_t materialRoot = 0;
        uintptr_t meshColorRoot = 0;
        SafeReadValue(materialMap + 0x08, materialRoot);
        SafeReadValue(meshColorMap + 0x08, meshColorRoot);
        const uintptr_t materialNode = FindTrackEventTreeMapNode(materialMap, sceneObject);
        const uintptr_t meshColorNode = FindTrackEventTreeMapNode(meshColorMap, sceneObject);

        uint32_t materialCount = 0;
        uint32_t materialCapacity = 0;
        uintptr_t materialEntries = 0;
        if (materialNode != 0 && IsReadableRange(materialNode + 0x20, sizeof(uintptr_t))) {
            SafeReadValue(materialNode + 0x14, materialCount);
            SafeReadValue(materialNode + 0x18, materialCapacity);
            SafeReadValue(materialNode + 0x1c, materialEntries);
        }

        float meshColor[4] = {};
        bool hasMeshColor = false;
        if (meshColorNode != 0 && IsReadableRange(meshColorNode + 0x30, sizeof(float) * 4)) {
            hasMeshColor =
                SafeReadValue(meshColorNode + 0x20, meshColor[0])
                && SafeReadValue(meshColorNode + 0x24, meshColor[1])
                && SafeReadValue(meshColorNode + 0x28, meshColor[2])
                && SafeReadValue(meshColorNode + 0x2c, meshColor[3]);
        }

        LOG_INFO("[EditorInspector] TrackEvent material trace "
            << phase
            << " event=0x" << std::hex << std::uppercase << eventId << std::dec
            << " dispatcher=" << HexAddress(dispatcher)
            << " sceneObject=" << HexAddress(sceneObject)
            << " materialRoot=" << (materialRoot ? HexAddress(materialRoot) : "<null>")
            << " materialNode=" << (materialNode ? HexAddress(materialNode) : "<missing>")
            << " materialCount=" << materialCount
            << " materialCapacity=" << materialCapacity
            << " materialEntries=" << (materialEntries ? HexAddress(materialEntries) : "<null>")
            << " meshColorRoot=" << (meshColorRoot ? HexAddress(meshColorRoot) : "<null>")
            << " meshColorNode=" << (meshColorNode ? HexAddress(meshColorNode) : "<missing>")
            << " meshColor="
            << (hasMeshColor ? "(" : "<missing>")
            << (hasMeshColor ? std::to_string(meshColor[0]) : "")
            << (hasMeshColor ? "," : "")
            << (hasMeshColor ? std::to_string(meshColor[1]) : "")
            << (hasMeshColor ? "," : "")
            << (hasMeshColor ? std::to_string(meshColor[2]) : "")
            << (hasMeshColor ? "," : "")
            << (hasMeshColor ? std::to_string(meshColor[3]) : "")
            << (hasMeshColor ? ")" : ""));
    }

    static void __fastcall Hook_TrackEventApplySceneObject(
        void* dispatcher,
        void* edxUnused,
        void* sceneObject,
        void* eventId,
        void* eventParam) {
        (void)edxUnused;

        const uintptr_t eventValue = reinterpret_cast<uintptr_t>(eventId);
        const bool traceArmed = g_editorMaterialTrackEventTraceArmed;
        if (traceArmed) {
            const LONG anyIndex = InterlockedIncrement(&g_editorMaterialTrackEventAnyTraceCount);
            if (anyIndex <= 120) {
                LOG_INFO("[EditorInspector] TrackEvent any trace event=0x"
                    << std::hex << std::uppercase << eventValue << std::dec
                    << " dispatcher=" << HexAddress(reinterpret_cast<uintptr_t>(dispatcher))
                    << " sceneObject=" << HexAddress(reinterpret_cast<uintptr_t>(sceneObject))
                    << " eventParam=" << HexAddress(reinterpret_cast<uintptr_t>(eventParam)));
            }
        }

        const bool shouldTrace = traceArmed && (eventValue == 0x12 || eventValue == 0x13);
        LONG traceIndex = 0;
        if (shouldTrace) {
            traceIndex = InterlockedIncrement(&g_editorMaterialTrackEventTraceCount);
            if (traceIndex <= 80) {
                LogTrackEventMaterialMapState(
                    "before",
                    reinterpret_cast<uintptr_t>(dispatcher),
                    reinterpret_cast<uintptr_t>(sceneObject),
                    eventValue);
            }
        }

        if (g_originalTrackEventApplySceneObject) {
            g_originalTrackEventApplySceneObject(dispatcher, sceneObject, eventId, eventParam);
        }

        if (shouldTrace && traceIndex <= 80) {
            LogTrackEventMaterialMapState(
                "after",
                reinterpret_cast<uintptr_t>(dispatcher),
                reinterpret_cast<uintptr_t>(sceneObject),
                eventValue);
            if (traceIndex == 80) {
                g_editorMaterialTrackEventTraceArmed = false;
                LOG_INFO("[EditorInspector] TrackEvent material trace stopped after 80 material/color events");
            }
        }

        if (traceArmed && InterlockedCompareExchange(&g_editorMaterialTrackEventAnyTraceCount, 0, 0) >= 120) {
            g_editorMaterialTrackEventTraceArmed = false;
            LOG_INFO("[EditorInspector] TrackEvent material trace stopped after 120 total events");
        }
    }

    static void EnsureTrackEventMaterialTraceHookInstalled() {
        if (g_editorMaterialTrackEventHookInstalled) {
            return;
        }

        TrackEventApplySceneObjectFunc target = ResolveTrackEventApplySceneObject();
        if (!target) {
            return;
        }

        MH_STATUS hookStatus = MH_CreateHook(
            reinterpret_cast<LPVOID>(target),
            reinterpret_cast<LPVOID>(&Hook_TrackEventApplySceneObject),
            reinterpret_cast<LPVOID*>(&g_originalTrackEventApplySceneObject));
        if (hookStatus == MH_OK || hookStatus == MH_ERROR_ALREADY_CREATED) {
            hookStatus = MH_EnableHook(reinterpret_cast<LPVOID>(target));
            if (hookStatus == MH_OK || hookStatus == MH_ERROR_ENABLED) {
                g_editorMaterialTrackEventHookInstalled = true;
                LOG_INFO("[EditorInspector] TrackEvent material trace hook installed address="
                    << HexAddress(reinterpret_cast<uintptr_t>(target)));
            }
        }
        else {
            LOG_WARNING("[EditorInspector] Failed to create TrackEvent material trace hook: "
                << MH_StatusToString(hookStatus));
        }
    }

    static void AppendTrackEventMapNodeReport(
        std::ostringstream& ss,
        const char* label,
        uintptr_t map,
        uintptr_t key) {
        uintptr_t root = 0;
        uintptr_t node = FindTrackEventTreeMapNode(map, key);
        SafeReadValue(map + 0x08, root);
        ss << "  " << label
            << " key=" << HexAddress(key)
            << " map=" << (map ? HexAddress(map) : "<null>")
            << " root=" << (root ? HexAddress(root) : "<null>")
            << " node=" << (node ? HexAddress(node) : "<missing>")
            << "\n";

        if (node == 0) {
            return;
        }

        std::ostringstream nodeTitle;
        nodeTitle << "    " << label << " node raw +0x000..0x07f";
        AppendRawDwordFields(ss, nodeTitle.str().c_str(), node, 0x80);

        if (std::strcmp(label, "materialOverrideMap+0x4c") == 0
            && IsReadableRange(node + 0x14, 0x0c)) {
            uint32_t count = 0;
            uint32_t capacity = 0;
            uintptr_t entries = 0;
            SafeReadValue(node + 0x14, count);
            SafeReadValue(node + 0x18, capacity);
            SafeReadValue(node + 0x1c, entries);
            ss << "    material override vector:"
                << " count=" << count
                << " capacity=" << capacity
                << " entries=" << (entries ? HexAddress(entries) : "<null>")
                << "\n";
            if (entries != 0 && count > 0 && count < 64 && IsReadableRange(entries, count * sizeof(MaterialColorOverrideEntry))) {
                const uint32_t dumpCount = count < 4 ? count : 4;
                for (uint32_t i = 0; i < dumpCount; ++i) {
                    std::ostringstream entryTitle;
                    entryTitle << "    material override entry[" << i << "] raw +0x000..0x05f";
                    AppendRawDwordFields(ss, entryTitle.str().c_str(), entries + i * sizeof(MaterialColorOverrideEntry), sizeof(MaterialColorOverrideEntry));
                }
            }
        }
    }

    static void AppendTrackEventMaterialBackingReport(std::ostringstream& ss, const SelectedEditorObject& selected) {
        const uintptr_t target = ResolveTrackEventMaterialTargetForInspector();
        ss << "\n[Track Event Material Backing Maps]\n";
        ss << "trackEventMaterialTarget=" << (target ? HexAddress(target) : "<missing>") << "\n";
        if (target == 0) {
            return;
        }

        const uintptr_t materialOverrideMap = target + 0x4c;
        const uintptr_t meshColorMap = target + 0x94;
        std::vector<uintptr_t> keys;
        auto addKey = [&keys](uintptr_t key) {
            if (key != 0 && std::find(keys.begin(), keys.end(), key) == keys.end()) {
                keys.push_back(key);
            }
        };

        addKey(selected.selectedObject);
        addKey(selected.firstMeshSceneObject);
        addKey(selected.resourceSceneRoot);
        std::vector<uintptr_t> materialObjects;
        CollectMaterialColorTestSceneObjects(selected, materialObjects);
        for (uintptr_t materialObject : materialObjects) {
            addKey(materialObject);
        }

        for (uintptr_t key : keys) {
            AppendTrackEventMapNodeReport(ss, "materialOverrideMap+0x4c", materialOverrideMap, key);
            AppendTrackEventMapNodeReport(ss, "meshColorMap+0x94", meshColorMap, key);
        }
    }

    static bool ApplyVariationControllerTransform(uintptr_t variationController) {
        if (variationController == 0 || !IsReadableRange(variationController, 0x90)) {
            return false;
        }

        UpdateObjectVariationTransformFunc updateTransform = ResolveUpdateObjectVariationTransform();
        if (!updateTransform) {
            return false;
        }

        __try {
            updateTransform(static_cast<int>(variationController));
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    static bool RebuildObjectVariation(uintptr_t variationController, int variationIndex) {
        if (variationController == 0 || !IsReadableRange(variationController, 0x9c)) {
            return false;
        }

        CreateObjectVariationFunc createVariation = ResolveCreateObjectVariation();
        if (!createVariation) {
            return false;
        }

        __try {
            createVariation(reinterpret_cast<void*>(variationController), variationIndex);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    static bool SetSceneObjectScaleVector(uintptr_t sceneObject, const float scale[3]);
    static bool SetSceneObjectScaleVectorWithRefresh(uintptr_t sceneObject, const float scale[3], uintptr_t refreshObject);
    static bool SetSceneObjectScaleVectorAndNudge(
        uintptr_t sceneObject,
        const float scale[3],
        uintptr_t nudgeObject,
        float epsilon);
    static bool SetSceneObjectScaleVectorAndDeferredNudge(
        uintptr_t sceneObject,
        const float scale[3],
        uintptr_t nudgeObject,
        float epsilon,
        uint32_t restoreDelayMs);
    static bool RunUnsafeSelectionPostScaleRefresh(uintptr_t selectionManager);
    static void AppendRawDwordFields(std::ostringstream& ss, const char* title, uintptr_t baseAddress, uintptr_t byteCount);

    static bool SetSceneObjectScaleVector(uintptr_t sceneObject, float uniformScale) {
        float scale[3] = { uniformScale, uniformScale, uniformScale };
        return SetSceneObjectScaleVector(sceneObject, scale);
    }

    static bool SetSceneObjectScaleVector(uintptr_t sceneObject, const float scale[3]) {
        if (sceneObject == 0 || !IsReadableRange(sceneObject, sizeof(uintptr_t))) {
            return false;
        }
        if (scale == nullptr) {
            return false;
        }

        uintptr_t vtable = 0;
        uintptr_t setScaleAddress = 0;
        if (!SafeReadValue(sceneObject, vtable)
            || !IsReadableRange(vtable + 0x1c, sizeof(uintptr_t))
            || !SafeReadValue(vtable + 0x18, setScaleAddress)
            || setScaleAddress == 0) {
            return false;
        }

        uintptr_t transform = 0;
        if (SafeReadValue(sceneObject + 0x28, transform)
            && transform != 0
            && IsReadableRange(transform + 0x30, sizeof(float) * 3)) {
            SafeWriteMemory(transform + 0x30, scale, sizeof(float) * 3);
        }

        __try {
            SceneObjectSetScaleFunc setScale = reinterpret_cast<SceneObjectSetScaleFunc>(setScaleAddress);
            setScale(reinterpret_cast<void*>(sceneObject), scale);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    static bool ReadSceneObjectScaleVector(uintptr_t sceneObject, float outScale[3]) {
        if (sceneObject == 0 || outScale == nullptr || !IsReadableRange(sceneObject + 0x28, sizeof(uintptr_t))) {
            return false;
        }

        uintptr_t transform = 0;
        return SafeReadValue(sceneObject + 0x28, transform)
            && transform != 0
            && IsReadableRange(transform + 0x30, sizeof(float) * 3)
            && SafeReadMemory(transform + 0x30, outScale, sizeof(float) * 3);
    }

    static bool SyncSceneObjectTransformForRefresh(uintptr_t sceneObject) {
        if (sceneObject == 0 || !IsReadableRange(sceneObject + 0x2c, sizeof(uintptr_t))) {
            return false;
        }

        uintptr_t transform = 0;
        if (!SafeReadValue(sceneObject + 0x28, transform)
            || transform == 0
            || !IsReadableRange(transform + 0x14, sizeof(float) * 3)
            || !IsReadableRange(transform + 0x20, sizeof(float) * 4)
            || !IsReadableRange(transform + 0x30, sizeof(float) * 3)) {
            return false;
        }

        SyncSceneObjectTransformFunc syncTransform = ResolveSyncSceneObjectTransform();
        if (!syncTransform) {
            return false;
        }

        __try {
            syncTransform(
                reinterpret_cast<void*>(sceneObject),
                reinterpret_cast<const float*>(transform + 0x14),
                reinterpret_cast<const float*>(transform + 0x30),
                reinterpret_cast<const float*>(transform + 0x20));
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    static bool ApplySceneObjectCurrentTransform(uintptr_t sceneObject) {
        if (sceneObject == 0 || !IsReadableRange(sceneObject + 0x2c, sizeof(uintptr_t))) {
            return false;
        }

        uintptr_t transform = 0;
        if (!SafeReadValue(sceneObject + 0x28, transform)
            || transform == 0
            || !IsReadableRange(transform + 0x3c, sizeof(float) * 3)) {
            return false;
        }

        ApplySceneObjectTransformFunc applyTransform = ResolveApplySceneObjectTransform();
        if (!applyTransform) {
            return false;
        }

        __try {
            applyTransform(
                reinterpret_cast<void*>(sceneObject),
                reinterpret_cast<const float*>(transform + 0x14),
                reinterpret_cast<const float*>(transform + 0x20),
                reinterpret_cast<const float*>(transform + 0x30));
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    static bool ForceSceneObjectVisualRefresh(uintptr_t sceneObject) {
        bool refreshed = ApplySceneObjectCurrentTransform(sceneObject);
        if (!refreshed) {
            refreshed = SyncSceneObjectTransformForRefresh(sceneObject);
        }

        RefreshEditorObjectVisualFunc refreshEditorVisual = ResolveRefreshEditorObjectVisual();
        const uintptr_t editorManager = ResolveEditorManagerForInspector();
        if (refreshEditorVisual
            && editorManager != 0
            && sceneObject != 0
            && IsReadableRange(editorManager, 0x670)
            && IsReadableRange(sceneObject, sizeof(uintptr_t))) {
            __try {
                refreshEditorVisual(
                    reinterpret_cast<void*>(editorManager),
                    reinterpret_cast<void*>(sceneObject));
                refreshed = true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
            }
        }

        return refreshed;
    }

    static bool SetSceneObjectScaleVectorWithRefresh(
        uintptr_t sceneObject,
        float uniformScale,
        uintptr_t refreshObject) {
        float scale[3] = { uniformScale, uniformScale, uniformScale };
        return SetSceneObjectScaleVectorWithRefresh(sceneObject, scale, refreshObject);
    }

    static bool SetSceneObjectScaleVectorWithRefresh(
        uintptr_t sceneObject,
        const float scale[3],
        uintptr_t refreshObject) {
        const bool scaled = SetSceneObjectScaleVector(sceneObject, scale);
        if (!scaled) {
            return false;
        }

        const bool scaledObjectRefreshed = ForceSceneObjectVisualRefresh(sceneObject);
        const bool refreshObjectRefreshed = refreshObject != 0 && refreshObject != sceneObject
            ? ForceSceneObjectVisualRefresh(refreshObject)
            : false;
        return scaledObjectRefreshed || refreshObjectRefreshed;
    }

    static void CommitEditorVisualScaleChange() {
        MarkEditorTransformDirty();
    }

    static bool ReadSceneObjectPosition(uintptr_t sceneObject, float outPosition[3]) {
        if (sceneObject == 0 || outPosition == nullptr || !IsReadableRange(sceneObject + 0x28, sizeof(uintptr_t))) {
            return false;
        }

        uintptr_t transform = 0;
        return SafeReadValue(sceneObject + 0x28, transform)
            && transform != 0
            && IsReadableRange(transform + 0x14, sizeof(float) * 3)
            && SafeReadMemory(transform + 0x14, outPosition, sizeof(float) * 3);
    }

    static bool ReadEditorTransformSavedPosition(uintptr_t editorTransform, float outPosition[3]) {
        return editorTransform != 0
            && outPosition != nullptr
            && IsReadableRange(editorTransform + 0x18, sizeof(float))
            && SafeReadMemory(editorTransform + 0x10, outPosition, sizeof(float) * 3);
    }

    static bool WriteEditorTransformSavedPosition(uintptr_t editorTransform, const float position[3]) {
        return editorTransform != 0
            && position != nullptr
            && IsReadableRange(editorTransform + 0x18, sizeof(float))
            && SafeWriteMemory(editorTransform + 0x10, position, sizeof(float) * 3);
    }

    static bool SyncEditorSavedPlacementFromSceneObject(
        const SelectedEditorObject& selected,
        uintptr_t sceneObject,
        const char* reason) {
        if (!g_editorSyncSavedPlacementOnScaleEnabled) {
            return false;
        }
        if (selected.editorTransform == 0 || sceneObject == 0) {
            LOG_INFO("[EditorInspector] sync saved placement skipped reason="
                << (reason ? reason : "<unknown>")
                << " selectedObject=" << (selected.selectedObject ? HexAddress(selected.selectedObject) : "<null>")
                << " editorTransformEA8=" << (selected.editorTransform ? HexAddress(selected.editorTransform) : "<null>")
                << " sceneObject=" << (sceneObject ? HexAddress(sceneObject) : "<null>"));
            return false;
        }

        float livePosition[3] = {};
        float oldSavedPosition[3] = {};
        const bool readLive = ReadSceneObjectPosition(sceneObject, livePosition);
        const bool readSaved = ReadEditorTransformSavedPosition(selected.editorTransform, oldSavedPosition);
        if (!readLive || !readSaved) {
            uintptr_t sceneTransform = 0;
            SafeReadValue(sceneObject + 0x28, sceneTransform);
            LOG_INFO("[EditorInspector] sync saved placement failed reason="
                << (reason ? reason : "<unknown>")
                << " selectedObject=" << (selected.selectedObject ? HexAddress(selected.selectedObject) : "<null>")
                << " editorTransformEA8=" << HexAddress(selected.editorTransform)
                << " sceneObject=" << HexAddress(sceneObject)
                << " sceneTransform=" << (sceneTransform ? HexAddress(sceneTransform) : "<null>")
                << " readLive=" << readLive
                << " readSaved=" << readSaved);
            return false;
        }

        const bool wrote = WriteEditorTransformSavedPosition(selected.editorTransform, livePosition);
        if (wrote) {
            MarkEditorTransformDirty();
        }

        LOG_INFO("[EditorInspector] sync saved placement reason="
            << (reason ? reason : "<unknown>")
            << " selectedObject=" << (selected.selectedObject ? HexAddress(selected.selectedObject) : "<null>")
            << " editorTransformEA8=" << HexAddress(selected.editorTransform)
            << " sceneObject=" << HexAddress(sceneObject)
            << " oldSaved=(" << oldSavedPosition[0] << ", " << oldSavedPosition[1] << ", " << oldSavedPosition[2] << ")"
            << " live=(" << livePosition[0] << ", " << livePosition[1] << ", " << livePosition[2] << ")"
            << " wrote=" << wrote);
        return wrote;
    }

    static bool SyncEditorSavedPlacementFromBestSceneObject(
        const SelectedEditorObject& selected,
        const char* reason) {
        const uintptr_t candidates[] = {
            selected.selectedObject,
            selected.mappedObject,
            selected.resourceSceneRoot,
            selected.firstMeshSceneObject,
            selected.firstVisibilitySceneObject,
            selected.firstLightSceneObject,
        };

        for (uintptr_t candidate : candidates) {
            if (candidate == 0) {
                continue;
            }
            if (SyncEditorSavedPlacementFromSceneObject(selected, candidate, reason)) {
                return true;
            }
        }
        return false;
    }

    static bool SetSceneObjectPositionVector(uintptr_t sceneObject, const float position[3]) {
        if (sceneObject == 0 || position == nullptr || !IsReadableRange(sceneObject, sizeof(uintptr_t))) {
            return false;
        }

        uintptr_t vtable = 0;
        uintptr_t setPositionAddress = 0;
        if (!SafeReadValue(sceneObject, vtable)
            || !IsReadableRange(vtable + 0x10, sizeof(uintptr_t))
            || !SafeReadValue(vtable + 0x0c, setPositionAddress)
            || setPositionAddress == 0) {
            return false;
        }

        __try {
            SceneObjectSetPositionFunc setPosition = reinterpret_cast<SceneObjectSetPositionFunc>(setPositionAddress);
            setPosition(reinterpret_cast<void*>(sceneObject), position);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    static bool NudgeSceneObjectPosition(uintptr_t sceneObject, float epsilon) {
        float original[3] = {};
        if (!ReadSceneObjectPosition(sceneObject, original)) {
            return false;
        }

        float nudged[3] = { original[0] + epsilon, original[1], original[2] };
        const bool moved = SetSceneObjectPositionVector(sceneObject, nudged);
        const bool restored = moved && SetSceneObjectPositionVector(sceneObject, original);
        return moved && restored;
    }

    static void ProcessPendingEditorNudgeRestore() {
        if (g_pendingEditorNudgeRestore.sceneObject == 0) {
            return;
        }

        const uint32_t now = GetTickCount();
        if (static_cast<int32_t>(now - g_pendingEditorNudgeRestore.restoreAfterTick) < 0) {
            return;
        }

        const uintptr_t sceneObject = g_pendingEditorNudgeRestore.sceneObject;
        float original[3] = {
            g_pendingEditorNudgeRestore.position[0],
            g_pendingEditorNudgeRestore.position[1],
            g_pendingEditorNudgeRestore.position[2],
        };
        g_pendingEditorNudgeRestore = {};

        const bool restored = SetSceneObjectPositionVector(sceneObject, original);
        ForceSceneObjectVisualRefresh(sceneObject);
        LOG_INFO("[EditorInspector] Deferred nudge restore object="
            << HexAddress(sceneObject)
            << " restored=" << restored);
    }

    static bool QueueDeferredSceneObjectNudge(uintptr_t sceneObject, float epsilon, uint32_t delayMs) {
        float original[3] = {};
        if (!ReadSceneObjectPosition(sceneObject, original)) {
            return false;
        }

        float nudged[3] = { original[0] + epsilon, original[1], original[2] };
        if (!SetSceneObjectPositionVector(sceneObject, nudged)) {
            return false;
        }

        g_pendingEditorNudgeRestore.sceneObject = sceneObject;
        g_pendingEditorNudgeRestore.position[0] = original[0];
        g_pendingEditorNudgeRestore.position[1] = original[1];
        g_pendingEditorNudgeRestore.position[2] = original[2];
        g_pendingEditorNudgeRestore.restoreAfterTick = GetTickCount() + delayMs;

        ForceSceneObjectVisualRefresh(sceneObject);
        return true;
    }

    static bool ScaleSelectedObjectsByEditorDelta(uintptr_t selectionManager, float delta) {
        if (selectionManager == 0 || !IsReadableRange(selectionManager, 0x24)) {
            return false;
        }

        ScaleSelectedObjectsDeltaFunc scaleSelected = ResolveScaleSelectedObjectsDelta();
        if (!scaleSelected) {
            return false;
        }

        __try {
            scaleSelected(reinterpret_cast<void*>(selectionManager), delta);
            MarkEditorTransformDirty();
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    static bool ResetSelectedObjectsScaleViaEditor(uintptr_t selectionManager) {
        if (selectionManager == 0 || !IsReadableRange(selectionManager, 0x24)) {
            return false;
        }

        ResetSelectedObjectsScaleFunc resetScale = ResolveResetSelectedObjectsScale();
        if (!resetScale) {
            return false;
        }

        __try {
            resetScale(static_cast<int>(selectionManager));
            MarkEditorTransformDirty();
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    static bool SetBackingObjectScale(uintptr_t backingObject, float scale) {
        if (backingObject == 0 || !IsReadableRange(backingObject, 0x14)) {
            return false;
        }

        SetBackingObjectScaleFunc setScale = ResolveSetBackingObjectScale();
        if (!setScale) {
            return false;
        }

        __try {
            setScale(reinterpret_cast<void*>(backingObject), scale);
            MarkEditorTransformDirty();
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    static bool TryApplyNativeUniformScaleBacking(
        const SelectedEditorObject& selected,
        const float scale[3]) {
        if (!g_editorNativeUniformScaleBackingEnabled
            || scale == nullptr
            || selected.editorScaleBackingObject == 0) {
            return false;
        }

        const float epsilon = 0.0005f;
        if (std::fabs(scale[0] - scale[1]) > epsilon
            || std::fabs(scale[0] - scale[2]) > epsilon) {
            return false;
        }

        const bool applied = SetBackingObjectScale(selected.editorScaleBackingObject, scale[0]);
        LOG_INFO("[EditorInspector] Native uniform scale backing selected="
            << (selected.selectedObject ? HexAddress(selected.selectedObject) : "<null>")
            << " backing=" << HexAddress(selected.editorScaleBackingObject)
            << " scale=" << scale[0]
            << " applied=" << applied);
        return applied;
    }

    static bool TryApplyNativeEditorUniformScaleDelta(float delta) {
        if (!g_editorNativeUniformScaleBackingEnabled || std::fabs(delta) <= 0.0001f) {
            return false;
        }

        const uintptr_t selectionManager = ResolveEditorSelectionManagerForInspector();
        const bool applied = ScaleSelectedObjectsByEditorDelta(selectionManager, delta);
        LOG_INFO("[EditorInspector] Native editor uniform scale delta="
            << delta
            << " selectionManager=" << (selectionManager ? HexAddress(selectionManager) : "<null>")
            << " applied=" << applied);
        return applied;
    }

    static bool RunUnsafeSelectionPostScaleRefresh(uintptr_t selectionManager) {
        if (selectionManager == 0 || !IsReadableRange(selectionManager, 0x68)) {
            return false;
        }

        RefreshSelectionConstraintsFunc refreshConstraints = ResolveRefreshSelectionConstraints();
        RefreshSelectionBoundsFunc refreshBounds = ResolveRefreshSelectionBounds();
        ProcessSelectionConstraintsFunc processConstraints = ResolveProcessSelectionConstraints();
        if (!refreshConstraints || !refreshBounds) {
            return false;
        }

        __try {
            refreshConstraints(static_cast<int>(selectionManager));
            if (processConstraints) {
                processConstraints(reinterpret_cast<void*>(selectionManager), 0);
            }
            refreshBounds(static_cast<int>(selectionManager));
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    static bool SetSceneObjectScaleVectorAndNudge(
        uintptr_t sceneObject,
        float uniformScale,
        uintptr_t nudgeObject,
        float epsilon) {
        float scale[3] = { uniformScale, uniformScale, uniformScale };
        return SetSceneObjectScaleVectorAndNudge(sceneObject, scale, nudgeObject, epsilon);
    }

    static bool SetSceneObjectScaleVectorAndNudge(
        uintptr_t sceneObject,
        const float scale[3],
        uintptr_t nudgeObject,
        float epsilon) {
        const bool scaled = SetSceneObjectScaleVectorWithRefresh(sceneObject, scale, nudgeObject);
        const bool nudged = scaled && NudgeSceneObjectPosition(nudgeObject, epsilon);
        return scaled && nudged;
    }

    static bool SetSceneObjectScaleVectorAndDeferredNudge(
        uintptr_t sceneObject,
        float uniformScale,
        uintptr_t nudgeObject,
        float epsilon,
        uint32_t restoreDelayMs) {
        float scale[3] = { uniformScale, uniformScale, uniformScale };
        return SetSceneObjectScaleVectorAndDeferredNudge(sceneObject, scale, nudgeObject, epsilon, restoreDelayMs);
    }

    static bool SetSceneObjectScaleVectorAndDeferredNudge(
        uintptr_t sceneObject,
        const float scale[3],
        uintptr_t nudgeObject,
        float epsilon,
        uint32_t restoreDelayMs) {
        const bool scaled = SetSceneObjectScaleVectorWithRefresh(sceneObject, scale, nudgeObject);
        const bool nudged = scaled && QueueDeferredSceneObjectNudge(nudgeObject, epsilon, restoreDelayMs);
        return scaled && nudged;
    }

    static uint32_t HashEntityId(uint32_t entityId) {
        uint32_t hash = entityId + ~(entityId << 15);
        hash = (hash >> 10 ^ hash) * 9;
        hash = hash ^ (hash >> 6);
        hash = hash + ~(hash << 11);
        return hash >> 16 ^ hash;
    }

    static uintptr_t LookupHashMapValue(uintptr_t map, uint32_t key, uintptr_t capacityOffset, uintptr_t entriesOffset) {
        uint32_t capacity = 0;
        uintptr_t entries = 0;
        if (map == 0
            || !SafeReadValue(map + capacityOffset, capacity)
            || !SafeReadValue(map + entriesOffset, entries)
            || capacity == 0
            || (capacity & (capacity - 1)) != 0
            || !IsReadableRange(entries, capacity * 0x0c)) {
            return 0;
        }

        uint32_t slot = HashEntityId(key) & (capacity - 1);
        for (uint32_t probe = 0; probe < capacity; ++probe) {
            const uintptr_t entry = entries + slot * 0x0c;
            uint32_t entryKey = 0;
            uintptr_t value = 0;
            uint8_t state = 0;
            SafeReadValue(entry, entryKey);
            SafeReadValue(entry + 0x04, value);
            SafeReadValue(entry + 0x08, state);

            if (state == 2 && entryKey == key) {
                uintptr_t mappedValue = 0;
                if (value != 0 && SafeReadValue(value + 0x08, mappedValue) && IsReadableRange(mappedValue, 0x20)) {
                    return mappedValue;
                }
                return 0;
            }
            if (state != 1 && state != 2) {
                return 0;
            }
            slot = (slot + 1) & (capacity - 1);
        }

        return 0;
    }

    struct EntityMapScanResult {
        uintptr_t managerOffset = 0;
        uintptr_t map = 0;
        uintptr_t key = 0;
        uintptr_t capacityOffset = 0;
        uintptr_t entriesOffset = 0;
        uint32_t capacity = 0;
        uintptr_t entryValue = 0;
        uintptr_t mappedValue = 0;
        std::string keyName;
    };

    static bool LookupHashMapEntryDetails(
        uintptr_t map,
        uint32_t key,
        uintptr_t capacityOffset,
        uintptr_t entriesOffset,
        EntityMapScanResult& result) {
        uint32_t capacity = 0;
        uintptr_t entries = 0;
        if (map == 0
            || !SafeReadValue(map + capacityOffset, capacity)
            || !SafeReadValue(map + entriesOffset, entries)
            || capacity == 0
            || (capacity & (capacity - 1)) != 0
            || !IsReadableRange(entries, capacity * 0x0c)) {
            return false;
        }

        uint32_t slot = HashEntityId(key) & (capacity - 1);
        for (uint32_t probe = 0; probe < capacity; ++probe) {
            const uintptr_t entry = entries + slot * 0x0c;
            uint32_t entryKey = 0;
            uintptr_t value = 0;
            uint8_t state = 0;
            SafeReadValue(entry, entryKey);
            SafeReadValue(entry + 0x04, value);
            SafeReadValue(entry + 0x08, state);

            if (state == 2 && entryKey == key) {
                uintptr_t mappedValue = 0;
                if (value != 0 && IsReadableRange(value, 0x0c)) {
                    SafeReadValue(value + 0x08, mappedValue);
                }
                result.capacity = capacity;
                result.entryValue = value;
                result.mappedValue = mappedValue;
                return value != 0 || mappedValue != 0;
            }
            if (state != 1 && state != 2) {
                return false;
            }
            slot = (slot + 1) & (capacity - 1);
        }

        return false;
    }

    static std::vector<EntityMapScanResult> ScanEntityManagerMapsForSelectedObject(const SelectedEditorObject& selected) {
        std::vector<EntityMapScanResult> results;
        const uintptr_t entityManager = ResolveEntityManagerForEditorInspector();
        if (entityManager == 0 || selected.selectedObject == 0 || !IsReadableRange(entityManager, 0x10)) {
            return results;
        }

        struct CandidateKey {
            const char* name;
            uintptr_t value;
        };
        const CandidateKey keys[] = {
            {"selectedObject", selected.selectedObject},
            {"mappedObjectE74", selected.mappedObject},
            {"editorTransformEA8", selected.editorTransform},
            {"editorScaleBackingE90", selected.editorScaleBackingObject},
            {"parentKey1c", selected.parentObject},
            {"mappedParent", selected.mappedParentObject},
            {"sceneHolder44", selected.sceneHolder},
            {"resourceContainer", selected.resourceContainer},
            {"resourceSceneRoot", selected.resourceSceneRoot},
            {"firstMeshSceneObject", selected.firstMeshSceneObject},
        };
        const uintptr_t layouts[][2] = {
            {0x0c, 0x10},
            {0x04, 0x08},
            {0x08, 0x0c},
        };

        for (uintptr_t offset = 0xc00; offset < 0x1400; offset += sizeof(uintptr_t)) {
            uintptr_t map = 0;
            if (!SafeReadValue(entityManager + offset, map) || !IsReadableRange(map, 0x14)) {
                continue;
            }

            for (const CandidateKey& key : keys) {
                if (key.value == 0) {
                    continue;
                }
                for (const auto& layout : layouts) {
                    EntityMapScanResult result = {};
                    result.managerOffset = offset;
                    result.map = map;
                    result.key = key.value;
                    result.keyName = key.name;
                    result.capacityOffset = layout[0];
                    result.entriesOffset = layout[1];
                    if (LookupHashMapEntryDetails(
                        map,
                        static_cast<uint32_t>(key.value),
                        layout[0],
                        layout[1],
                        result)) {
                        bool duplicate = false;
                        for (const EntityMapScanResult& existing : results) {
                            if (existing.managerOffset == result.managerOffset
                                && existing.key == result.key
                                && existing.capacityOffset == result.capacityOffset
                                && existing.entriesOffset == result.entriesOffset
                                && existing.entryValue == result.entryValue
                                && existing.mappedValue == result.mappedValue) {
                                duplicate = true;
                                break;
                            }
                        }
                        if (!duplicate) {
                            results.push_back(result);
                        }
                    }
                }
            }
        }

        return results;
    }

    static uintptr_t LookupEditorObjectMapValue(uintptr_t selectedObject) {
        const uintptr_t entityManager = ResolveEntityManagerForEditorInspector();
        uintptr_t objectMap = 0;
        if (entityManager == 0
            || !SafeReadValue(entityManager + 0xe74, objectMap)
            || !IsReadableRange(objectMap, 0x14)) {
            return 0;
        }

        return LookupHashMapValue(objectMap, static_cast<uint32_t>(selectedObject), 0x0c, 0x10);
    }

    static uintptr_t LookupEntityManagerSimpleMapValue(uintptr_t managerOffset, uintptr_t selectedObject) {
        const uintptr_t entityManager = ResolveEntityManagerForEditorInspector();
        uintptr_t map = 0;
        if (entityManager == 0
            || !SafeReadValue(entityManager + managerOffset, map)
            || !IsReadableRange(map, 0x14)) {
            return 0;
        }

        return LookupHashMapValue(map, static_cast<uint32_t>(selectedObject), 0x0c, 0x10);
    }

    static uintptr_t LookupEntityManagerSimpleMapValueForAnyKey(
        uintptr_t managerOffset,
        const uintptr_t* keys,
        size_t keyCount,
        uintptr_t* outKey = nullptr) {
        if (keys == nullptr) {
            return 0;
        }

        for (size_t i = 0; i < keyCount; ++i) {
            const uintptr_t key = keys[i];
            if (key == 0) {
                continue;
            }

            bool duplicate = false;
            for (size_t j = 0; j < i; ++j) {
                if (keys[j] == key) {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate) {
                continue;
            }

            const uintptr_t value = LookupEntityManagerSimpleMapValue(managerOffset, key);
            if (value != 0) {
                if (outKey != nullptr) {
                    *outKey = key;
                }
                return value;
            }
        }

        return 0;
    }

    static void AddSceneNodeCandidate(
        uintptr_t sceneObject,
        uintptr_t via,
        int depth,
        std::vector<SceneNodeSnapshot>& outNodes,
        std::vector<uintptr_t>& visited);

    static void AddSceneChildCandidate(
        uintptr_t childCandidate,
        uintptr_t via,
        int depth,
        std::vector<SceneNodeSnapshot>& outNodes,
        std::vector<uintptr_t>& visited) {
        if (childCandidate != 0 && IsReadableRange(childCandidate, 0x24)) {
            AddSceneNodeCandidate(childCandidate, via, depth, outNodes, visited);
        }
    }

    static void AddSceneNodeCandidate(
        uintptr_t sceneObject,
        uintptr_t via,
        int depth,
        std::vector<SceneNodeSnapshot>& outNodes,
        std::vector<uintptr_t>& visited) {
        if (depth > 12 || sceneObject == 0 || outNodes.size() >= 160 || !IsReadableRange(sceneObject, 0x24)) {
            return;
        }

        if (std::find(visited.begin(), visited.end(), sceneObject) != visited.end()) {
            return;
        }
        visited.push_back(sceneObject);

        SceneNodeSnapshot node = {};
        node.address = sceneObject;
        node.via = via;
        node.depth = depth;
        node.readable = true;
        SafeReadValue(sceneObject, node.vtable);
        SafeReadValue(sceneObject + 0x08, node.typeAndSubtype);
        SafeReadValue(sceneObject + 0x0c, node.flags0c);
        SafeReadValue(sceneObject + 0x10, node.childMode);
        SafeReadValue(sceneObject + 0x18, node.childStorage);
        SafeReadValue(sceneObject + 0xec, node.scaleEc);
        SafeReadValue(sceneObject + 0xc9, node.physicsByteC9);
        outNodes.push_back(node);

        if (node.childMode == 0 || node.childStorage == 0 || node.childMode > 4096) {
            return;
        }

        // Engine traversal dereferences +0x18 as child storage, but some editor
        // objects also make the direct pointer interesting. Follow both for discovery.
        AddSceneChildCandidate(node.childStorage, sceneObject + 0x18, depth + 1, outNodes, visited);

        if (IsReadableRange(node.childStorage, sizeof(uintptr_t))) {
            uintptr_t indirectChild = 0;
            SafeReadValue(node.childStorage, indirectChild);
            AddSceneChildCandidate(indirectChild, node.childStorage, depth + 1, outNodes, visited);
        }

        const uint32_t childSlots = node.childMode <= 2 ? node.childMode : node.childMode - 1;
        if (childSlots > 1 && IsReadableRange(node.childStorage, sizeof(uintptr_t) * childSlots)) {
            for (uint32_t i = 0; i < childSlots && outNodes.size() < 160; ++i) {
                uintptr_t child = 0;
                SafeReadValue(node.childStorage + sizeof(uintptr_t) * i, child);
                AddSceneChildCandidate(child, node.childStorage + sizeof(uintptr_t) * i, depth + 1, outNodes, visited);
            }
        }
    }

    static std::vector<SceneNodeSnapshot> BuildSceneNodeSnapshot(uintptr_t root) {
        std::vector<SceneNodeSnapshot> nodes;
        std::vector<uintptr_t> visited;
        AddSceneNodeCandidate(root, 0, 0, nodes, visited);
        return nodes;
    }

    static void CollectSceneObjectsByTypeLocal(
        uintptr_t sceneObject,
        uint8_t wantedType,
        uint8_t wantedSubtype,
        std::vector<uintptr_t>& outObjects,
        int depth = 0) {
        if (depth > 64 || sceneObject == 0 || outObjects.size() >= 128 || !IsReadableRange(sceneObject, 0x24)) {
            return;
        }

        uint16_t typeAndSubtype = 0;
        uint32_t childMode = 0;
        uintptr_t children = 0;
        SafeReadValue(sceneObject + 0x08, typeAndSubtype);
        SafeReadValue(sceneObject + 0x10, childMode);
        SafeReadValue(sceneObject + 0x18, children);

        const uint8_t type = static_cast<uint8_t>(typeAndSubtype & 0xff);
        const uint8_t subtype = static_cast<uint8_t>(typeAndSubtype >> 8);
        if (type == wantedType && subtype == wantedSubtype) {
            outObjects.push_back(sceneObject);
        }

        uint8_t virtualSubtype = subtype;
        SafeReadValue(sceneObject + 0x09, virtualSubtype);
        if (type == wantedType && virtualSubtype == wantedSubtype) {
            outObjects.push_back(sceneObject);
        }

        if (childMode == 0 || children == 0 || childMode > 4096 || !IsReadableRange(children, sizeof(uintptr_t))) {
            return;
        }

        if (IsReadableRange(children, 0x24)) {
            CollectSceneObjectsByTypeLocal(children, wantedType, wantedSubtype, outObjects, depth + 1);
        }

        if (childMode == 1) {
            uintptr_t child = 0;
            SafeReadValue(children, child);
            CollectSceneObjectsByTypeLocal(child, wantedType, wantedSubtype, outObjects, depth + 1);
            return;
        }

        const uint32_t childCount = childMode - 1;
        if (!IsReadableRange(children, sizeof(uintptr_t) * childCount)) {
            return;
        }

        for (uint32_t i = 0; i < childCount && outObjects.size() < 128; ++i) {
            uintptr_t child = 0;
            SafeReadValue(children + sizeof(uintptr_t) * i, child);
            CollectSceneObjectsByTypeLocal(child, wantedType, wantedSubtype, outObjects, depth + 1);
        }
    }

    static std::vector<SelectedEditorObject> ReadSelectedEditorObjects() {
        std::vector<SelectedEditorObject> objects;
        const uintptr_t selectionManager = ResolveEditorSelectionManagerForInspector();
        if (!IsReadableRange(selectionManager, 0x24)) {
            return objects;
        }

        uint32_t count = 0;
        uintptr_t node = 0;
        SafeReadValue(selectionManager + 0x20, count);
        SafeReadValue(selectionManager + 0x18, node);
        if (count > 256) {
            count = 256;
        }

        for (uint32_t i = 0; i < count && node != 0 && IsReadableRange(node, 0x0c); ++i) {
            SelectedEditorObject selected = {};
            selected.index = static_cast<int>(i);
            selected.listNode = node;
            SafeReadValue(node + 0x08, selected.selectedObject);
            selected.mappedObject = LookupEditorObjectMapValue(selected.selectedObject);
            selected.editorTransform = LookupEntityManagerSimpleMapValue(0xea8, selected.selectedObject);
            selected.editorScaleBackingObject = LookupEntityManagerSimpleMapValue(0xe90, selected.selectedObject);
            if (selected.editorTransform != 0 && IsReadableRange(selected.editorTransform, 0x20)) {
                selected.hasRotation = SafeReadValue(selected.editorTransform + 0x1c, selected.rotationRadians);
            }
            if (selected.selectedObject != 0 && IsReadableRange(selected.selectedObject, 0x48)) {
                SafeReadValue(selected.selectedObject + 0x1c, selected.parentKey1c);
                SafeReadValue(selected.selectedObject + 0x20, selected.childCount20);
                SafeReadValue(selected.selectedObject + 0x44, selected.sceneHolder);
                SafeReadValue(selected.selectedObject + 0x04, selected.selectedObjectMovementState04);
                SafeReadValue(selected.selectedObject + 0x08, selected.selectedObjectType);
                SafeReadValue(selected.selectedObject + 0x0c, selected.selectedObjectFlags0c);
                SafeReadValue(selected.selectedObject + 0x10, selected.selectedObjectChildMode);
                SafeReadValue(selected.selectedObject + 0x18, selected.selectedObjectChildren);
                SafeReadValue(selected.selectedObject + 0x20, selected.selectedObjectFlags);
                selected.movingOrRotatingCandidate = selected.selectedObjectMovementState04 == 2;
                selected.selectedObjectPhysicsEnabled = (selected.selectedObjectFlags0c & 0x00010000) != 0;
                selected.hasSelectedObjectPhysicsEnabled = true;
                if (selected.sceneHolder != 0 && IsReadableRange(selected.sceneHolder, 0x20)) {
                    SafeReadValue(selected.sceneHolder, selected.resourceContainer);
                    SafeReadValue(selected.sceneHolder + 0x08, selected.sceneHolderFlags08);
                    SafeReadValue(selected.sceneHolder + 0x20, selected.sceneHolderVariationMask20);
                    selected.hasSceneHolderBuoyancy = SafeReadValue(selected.sceneHolder + 0x1c, selected.sceneHolderBuoyancyRaw);
                    selected.horizontalAlign = ((selected.sceneHolderFlags08 >> 4) & 1) != 0;
                    selected.verticalAlign = ((selected.sceneHolderFlags08 >> 2) & 1) != 0;
                    selected.sceneHolderBit0 = (selected.sceneHolderFlags08 & 0x00000001) != 0;
                    selected.lockedToDrivingLineCandidate = (selected.sceneHolderFlags08 & 0x00000004) != 0;
                    selected.sceneHolderBit3 = (selected.sceneHolderFlags08 & 0x00000008) != 0;
                    selected.sceneHolderBit5 = (selected.sceneHolderFlags08 & 0x00000020) != 0;
                    selected.sceneHolderBit6 = (selected.sceneHolderFlags08 & 0x00000040) != 0;
                    selected.fastObjectCandidate = (selected.sceneHolderFlags08 & 0x00000400) != 0;
                    selected.sceneHolderBit12 = (selected.sceneHolderFlags08 & 0x00001000) != 0;
                    selected.sceneHolderVisible = !selected.sceneHolderBit3;
                    selected.contactResponseEnabled = !selected.sceneHolderBit6;
                    selected.hasSceneHolderVisible = true;
                }
                if (selected.resourceContainer != 0 && IsReadableRange(selected.resourceContainer, 0x6c)) {
                    SafeReadValue(selected.resourceContainer + 0x68, selected.resourceSceneRoot);
                }
                selected.parentObject = static_cast<uintptr_t>(selected.parentKey1c);
                if (selected.parentObject != 0) {
                    selected.mappedParentObject = LookupEditorObjectMapValue(selected.parentObject);
                }

                if (selected.editorTransform == 0) {
                    const uintptr_t transformKeys[] = {
                        selected.mappedObject,
                        selected.parentObject,
                        selected.mappedParentObject,
                        selected.sceneHolder,
                        selected.resourceContainer,
                        selected.resourceSceneRoot,
                    };
                    uintptr_t matchedKey = 0;
                    selected.editorTransform = LookupEntityManagerSimpleMapValueForAnyKey(
                        0xea8,
                        transformKeys,
                        sizeof(transformKeys) / sizeof(transformKeys[0]),
                        &matchedKey);
                    if (selected.editorTransform != 0) {
                        LOG_INFO("[EditorInspector] resolved EA8 transform via alternate key selectedObject="
                            << HexAddress(selected.selectedObject)
                            << " key=" << HexAddress(matchedKey)
                            << " editorTransform=" << HexAddress(selected.editorTransform));
                    }
                }
                if (!selected.hasRotation
                    && selected.editorTransform != 0
                    && IsReadableRange(selected.editorTransform, 0x20)) {
                    selected.hasRotation = SafeReadValue(selected.editorTransform + 0x1c, selected.rotationRadians);
                }

                std::vector<uintptr_t> meshObjects;
                CaptureEngineSceneObjects(selected.selectedObject, 1, 3, meshObjects);
                if (meshObjects.empty()) {
                    CaptureEngineSceneObjects(selected.resourceSceneRoot, 1, 3, meshObjects);
                }
                if (meshObjects.empty()) {
                    CaptureEngineSceneObjects(selected.selectedObject, 3, 1, meshObjects);
                    if (meshObjects.empty()) {
                        CaptureEngineSceneObjects(selected.resourceSceneRoot, 3, 1, meshObjects);
                    }
                    selected.meshUsedReversedTypeSubtypeFallback = !meshObjects.empty();
                }
                selected.meshSceneObjectCount = static_cast<uint32_t>(meshObjects.size());
                if (!meshObjects.empty()) {
                    selected.firstMeshSceneObject = meshObjects.front();
                    selected.hasFriction = SafeReadValue(selected.firstMeshSceneObject + 0x90, selected.frictionRaw);
                    selected.hasMeshOffset94 = SafeReadValue(selected.firstMeshSceneObject + 0x94, selected.meshOffset94Raw);
                    selected.hasObjectGravity = SafeReadValue(selected.firstMeshSceneObject + 0x98, selected.objectGravityRaw);
                    selected.hasMeshOffset9c = SafeReadValue(selected.firstMeshSceneObject + 0x9c, selected.meshOffset9cRaw);
                    selected.hasLightRange = SafeReadValue(selected.firstMeshSceneObject + 0xa4, selected.lightRangeRaw);
                    selected.hasLightIntensity = SafeReadValue(selected.firstMeshSceneObject + 0xb4, selected.lightIntensityRaw);
                    SafeReadValue(selected.firstMeshSceneObject + 0xec, selected.unknownEcRaw);
                    uint8_t physicsByte = 0;
                    if (SafeReadValue(selected.firstMeshSceneObject + 0xc9, physicsByte)) {
                        selected.shadowTypeDynamicCandidate = physicsByte != 0;
                        selected.hasShadowType = true;
                    }
                    uint8_t lightEnabledByte = 0;
                    if (SafeReadValue(selected.firstMeshSceneObject + 0xca, lightEnabledByte)) {
                        selected.lightEnabled = lightEnabledByte != 0;
                        selected.hasLightEnabled = true;
                    }
                }

                std::vector<uintptr_t> visibilityObjects;
                if (selected.editorTransform != 0 && IsReadableRange(selected.editorTransform, sizeof(uintptr_t))) {
                    uintptr_t visibilityRoot = 0;
                    SafeReadValue(selected.editorTransform, visibilityRoot);
                    CaptureEngineSceneObjects(visibilityRoot, 1, 0x0c, visibilityObjects);
                }
                if (visibilityObjects.empty()) {
                    CaptureEngineSceneObjects(selected.selectedObject, 1, 0x0c, visibilityObjects);
                }
                if (visibilityObjects.empty()) {
                    CaptureEngineSceneObjects(selected.resourceSceneRoot, 1, 0x0c, visibilityObjects);
                }
                if (visibilityObjects.empty()) {
                    CaptureEngineSceneObjects(selected.selectedObject, 0x0c, 1, visibilityObjects);
                    if (visibilityObjects.empty()) {
                        CaptureEngineSceneObjects(selected.resourceSceneRoot, 0x0c, 1, visibilityObjects);
                    }
                    selected.visibilityUsedReversedTypeSubtypeFallback = !visibilityObjects.empty();
                }
                selected.visibilitySceneObjectCount = static_cast<uint32_t>(visibilityObjects.size());
                if (!visibilityObjects.empty()) {
                    selected.firstVisibilitySceneObject = visibilityObjects.front();
                    uint32_t visibilityFlags = 0;
                    if (SafeReadValue(selected.firstVisibilitySceneObject + 0x0c, visibilityFlags)) {
                        selected.visible = ((visibilityFlags >> 5) & 1) == 0;
                        selected.hasVisible = true;
                    }
                }

                std::vector<uintptr_t> lightObjects;
                CaptureEngineSceneObjects(selected.selectedObject, 1, 9, lightObjects);
                if (lightObjects.empty()) {
                    CaptureEngineSceneObjects(selected.resourceSceneRoot, 1, 9, lightObjects);
                }
                if (lightObjects.empty()) {
                    CaptureEngineSceneObjects(selected.selectedObject, 9, 1, lightObjects);
                    if (lightObjects.empty()) {
                        CaptureEngineSceneObjects(selected.resourceSceneRoot, 9, 1, lightObjects);
                    }
                    selected.lightUsedReversedTypeSubtypeFallback = !lightObjects.empty();
                }
                selected.lightSceneObjectCount = static_cast<uint32_t>(lightObjects.size());
                if (!lightObjects.empty()) {
                    selected.firstLightSceneObject = lightObjects.front();
                }
            }
            objects.push_back(selected);
            SafeReadValue(node + 0x04, node);
        }

        return objects;
    }

    static float ClampEditorAutoScale(float scale) {
        if (scale < EDITOR_AUTO_SCALE_MIN) {
            return EDITOR_AUTO_SCALE_MIN;
        }
        if (scale > EDITOR_AUTO_SCALE_MAX) {
            return EDITOR_AUTO_SCALE_MAX;
        }
        return scale;
    }

    static bool ApplyEditorVisualScaleFactorToPrimarySelection(float scaleFactor) {
        const std::vector<SelectedEditorObject> selectedObjects = ReadSelectedEditorObjects();
        if (selectedObjects.empty()) {
            return false;
        }

        const SelectedEditorObject& primary = selectedObjects.front();
        if (primary.firstMeshSceneObject == 0 || primary.selectedObject == 0) {
            return false;
        }

        if (primary.firstMeshSceneObject != g_editorInspectorAxisScaleObject) {
            g_editorInspectorAxisScaleObject = primary.firstMeshSceneObject;
            float liveScale[3] = {};
            if (ReadSceneObjectScaleVector(primary.firstMeshSceneObject, liveScale)) {
                g_editorInspectorAxisScale[0] = ClampEditorAutoScale(liveScale[0]);
                g_editorInspectorAxisScale[1] = ClampEditorAutoScale(liveScale[1]);
                g_editorInspectorAxisScale[2] = ClampEditorAutoScale(liveScale[2]);
            }
            g_editorInspectorLastAppliedScale = 1.0f;
            g_editorInspectorAutoScale = 1.0f;
        }

        float scaledAxis[3] = {
            ClampEditorAutoScale(g_editorInspectorAxisScale[0] * scaleFactor),
            ClampEditorAutoScale(g_editorInspectorAxisScale[1] * scaleFactor),
            ClampEditorAutoScale(g_editorInspectorAxisScale[2] * scaleFactor),
        };
        const float clampedScale = ClampEditorAutoScale(g_editorInspectorAutoScale * scaleFactor);
        const float nativeDelta = clampedScale - g_editorInspectorAutoScale;

        bool applied = TryApplyNativeEditorUniformScaleDelta(nativeDelta);
        if (!applied) {
            applied = SetSceneObjectScaleVectorWithRefresh(
                primary.firstMeshSceneObject,
                scaledAxis,
                0);
        }
        if (applied) {
            CommitEditorVisualScaleChange();
            if (!g_editorNativeUniformScaleBackingEnabled) {
                TryApplyNativeUniformScaleBacking(primary, scaledAxis);
            }
            SyncEditorSavedPlacementFromBestSceneObject(primary, "hotkey-scale");
            RememberEditorPublishScaleOverride(primary, scaledAxis);
            g_editorInspectorAutoScale = clampedScale;
            g_editorInspectorLastAppliedScale = clampedScale;
            g_editorInspectorAxisScale[0] = scaledAxis[0];
            g_editorInspectorAxisScale[1] = scaledAxis[1];
            g_editorInspectorAxisScale[2] = scaledAxis[2];
        }
        return applied;
    }

    static void ProcessEditorScaleKeybinds() {
        const bool decreaseHeld = Keybindings::IsActionDown(Keybindings::Action::EditorScaleDecrease);
        const bool increaseHeld = Keybindings::IsActionDown(Keybindings::Action::EditorScaleIncrease);

        static uint32_t lastTick = 0;
        const uint32_t now = GetTickCount();
        if (!decreaseHeld && !increaseHeld) {
            lastTick = now;
            return;
        }

        if (lastTick == 0) {
            lastTick = now;
            return;
        }

        const uint32_t elapsedMs = now - lastTick;
        lastTick = now;
        if (elapsedMs == 0 || decreaseHeld == increaseHeld) {
            return;
        }

        const float dt = static_cast<float>(elapsedMs) / 1000.0f;
        const float direction = increaseHeld ? 1.0f : -1.0f;
        const float factor = std::pow(2.0f, direction * EDITOR_SCALE_HOTKEY_DOUBLINGS_PER_SECOND * dt);
        const float newScale = ClampEditorAutoScale(g_editorInspectorAutoScale * factor);
        if (std::fabs(newScale - g_editorInspectorAutoScale) <= 0.0001f) {
            return;
        }

        const bool applied = ApplyEditorVisualScaleFactorToPrimarySelection(factor);
        LOG_VERBOSE("[EditorInspector] Hotkey visual scale scale="
            << newScale
            << " increaseHeld=" << increaseHeld
            << " decreaseHeld=" << decreaseHeld
            << " applied=" << applied);
    }

    static void ProcessStickyEditorMaterialOverride() {
        if (!g_editorMaterialStickyEnabled) {
            g_editorMaterialStickySceneObjects.clear();
            InterlockedExchange(&g_editorMaterialStickyParamOwnerCount, 0);
            return;
        }

        const uint32_t now = GetTickCount();
        if (g_editorMaterialStickyLastMonitorTick != 0
            && now - g_editorMaterialStickyLastMonitorTick < EDITOR_MATERIAL_STICKY_MONITOR_INTERVAL_MS) {
            return;
        }
        g_editorMaterialStickyLastMonitorTick = now;

        const std::vector<SelectedEditorObject> selectedObjects = ReadSelectedEditorObjects();
        if (selectedObjects.empty()) {
            return;
        }

        if (selectedObjects.front().movingOrRotatingCandidate) {
            g_editorMaterialStickyEnabled = false;
            g_editorMaterialStickySceneObjects.clear();
            InterlockedExchange(&g_editorMaterialStickyParamOwnerCount, 0);
            LOG_INFO("[EditorInspector] Sticky material color override disabled while selected object is moving");
            return;
        }
    }

    static void ProcessMaterialParameterTrace() {
        if (!g_editorMaterialTraceEnabled || g_editorMaterialTraceValuePtr == 0) {
            return;
        }

        const uint32_t now = GetTickCount();
        if (g_editorMaterialTraceLastMonitorTick != 0
            && now - g_editorMaterialTraceLastMonitorTick < 100) {
            return;
        }
        g_editorMaterialTraceLastMonitorTick = now;

        if (!IsReadableRange(g_editorMaterialTraceValuePtr, sizeof(float) * 4)) {
            LOG_INFO("[EditorInspector] Material trace disabled; valuePtr no longer readable "
                << HexAddress(g_editorMaterialTraceValuePtr));
            g_editorMaterialTraceEnabled = false;
            return;
        }

        float current[4] = {};
        if (!SafeReadMemory(g_editorMaterialTraceValuePtr, current, sizeof(current))) {
            return;
        }

        if (Float4Different(current, g_editorMaterialTraceLastValue)) {
            LOG_INFO("[EditorInspector] Material value pointer changed sceneObject="
                << HexAddress(g_editorMaterialTraceSceneObject)
                << " selector=0x" << std::hex << std::uppercase << g_editorMaterialTraceSelector << std::dec
                << " slot=" << static_cast<uint32_t>(g_editorMaterialTraceSlot)
                << " owner=" << HexAddress(g_editorMaterialTraceOwner)
                << " valuePtr=" << HexAddress(g_editorMaterialTraceValuePtr)
                << " old=(" << g_editorMaterialTraceLastValue[0]
                << "," << g_editorMaterialTraceLastValue[1]
                << "," << g_editorMaterialTraceLastValue[2]
                << "," << g_editorMaterialTraceLastValue[3]
                << ") new=(" << current[0]
                << "," << current[1]
                << "," << current[2]
                << "," << current[3]
                << ")");
            memcpy(g_editorMaterialTraceLastValue, current, sizeof(current));
            ++g_editorMaterialTraceChangeCount;
            if (g_editorMaterialTraceChangeCount >= 20) {
                g_editorMaterialTraceEnabled = false;
                LOG_INFO("[EditorInspector] Material value pointer trace stopped after 20 changes");
            }
        }
    }

    static bool LooksLikeEditorObject(uintptr_t object, EditorObjectCandidate& candidate) {
        if (!IsReadableRange(object, 0x70)) {
            return false;
        }

        uintptr_t vtable = 0;
        SafeReadValue(object, vtable);
        if (vtable != 0 && IsReadableRange(vtable, sizeof(uintptr_t))) {
            candidate.score += 2;
            candidate.vtable = vtable;
        }

        uint32_t ownerValue18 = 0;
        uint32_t ownerKey1c = 0;
        uintptr_t sceneResource = 0;
        if (SafeReadValue(object + 0x18, ownerValue18)) {
            candidate.ownerValue18 = ownerValue18;
        }
        if (SafeReadValue(object + 0x1c, ownerKey1c)) {
            candidate.ownerKey1c = ownerKey1c;
            if (ownerKey1c != 0) {
                candidate.score += 1;
            }
        }
        if (SafeReadValue(object + 0x68, sceneResource)) {
            candidate.sceneResource = sceneResource;
            if (sceneResource != 0 && IsReadableRange(sceneResource, 0x20)) {
                candidate.score += 4;
            }
        }

        if (ownerValue18 != 0 && ownerValue18 < 512) {
            candidate.score += 1;
        }

        return candidate.score >= 3;
    }

    static std::vector<EditorObjectCandidate> FindEditorObjectCandidates(uintptr_t editorManager) {
        std::vector<EditorObjectCandidate> candidates;
        if (!IsReadableRange(editorManager, EDITOR_MANAGER_SCAN_SIZE)) {
            return candidates;
        }

        for (uintptr_t offset = 0; offset + sizeof(uintptr_t) <= EDITOR_MANAGER_SCAN_SIZE; offset += sizeof(uintptr_t)) {
            uintptr_t object = 0;
            if (!SafeReadValue(editorManager + offset, object)) {
                continue;
            }

            EditorObjectCandidate candidate = {};
            candidate.sourceOffset = offset;
            candidate.object = object;
            if (LooksLikeEditorObject(object, candidate)) {
                candidates.push_back(candidate);
            }
        }

        std::sort(candidates.begin(), candidates.end(), [](const EditorObjectCandidate& left, const EditorObjectCandidate& right) {
            if (left.score != right.score) {
                return left.score > right.score;
            }
            return left.sourceOffset < right.sourceOffset;
        });

        constexpr size_t MAX_CANDIDATES = 12;
        if (candidates.size() > MAX_CANDIDATES) {
            candidates.resize(MAX_CANDIDATES);
        }
        return candidates;
    }

    static void RenderPointerCopyText(const char* label, uintptr_t value) {
        ImGui::Text("%s: %s", label, value ? HexAddress(value).c_str() : "<none>");
        if (value != 0 && ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Click to copy");
        }
        if (value != 0 && ImGui::IsItemClicked()) {
            ImGui::SetClipboardText(HexAddress(value).c_str());
        }
    }

    static void AppendRawDwordFields(std::ostringstream& ss, const char* title, uintptr_t baseAddress, uintptr_t byteCount) {
        ss << "\n[" << title << "]\n";
        if (baseAddress == 0 || !IsReadableRange(baseAddress, 0x04)) {
            ss << "<unavailable>\n";
            return;
        }

        for (uintptr_t offset = 0; offset < byteCount; offset += 4) {
            uint32_t dwordValue = 0;
            if (!SafeReadValue(baseAddress + offset, dwordValue)) {
                continue;
            }

            float floatValue = 0.0f;
            memcpy(&floatValue, &dwordValue, sizeof(floatValue));

            uintptr_t pointerValue = 0;
            if (offset + sizeof(uintptr_t) <= byteCount) {
                SafeReadValue(baseAddress + offset, pointerValue);
            }

            ss << "+0x";
            ss << std::hex << std::uppercase;
            ss.width(2);
            ss.fill('0');
            ss << offset;
            ss.fill(' ');
            ss << " dword=0x";
            ss.width(8);
            ss.fill('0');
            ss << dwordValue;
            ss.fill(' ');
            ss << std::dec;

            if (std::isfinite(floatValue) && std::fabs(floatValue) < 1000000.0f) {
                ss << " float=" << floatValue;
            }
            if (pointerValue != 0 && IsReadableRange(pointerValue, 0x10)) {
                ss << " ptr=" << HexAddress(pointerValue);
            }
            ss << "\n";
        }
    }

    struct OffsetWatchField {
        const char* name;
        uintptr_t offset;
    };

    static void AppendWatchedOffsetFields(
        std::ostringstream& ss,
        const char* title,
        uintptr_t baseAddress,
        const OffsetWatchField* fields,
        size_t fieldCount) {
        ss << "\n[" << title << "]\n";
        if (baseAddress == 0 || !IsReadableRange(baseAddress, 0x04)) {
            ss << "<unavailable>\n";
            return;
        }

        for (size_t i = 0; i < fieldCount; ++i) {
            uint32_t dwordValue = 0;
            if (!SafeReadValue(baseAddress + fields[i].offset, dwordValue)) {
                continue;
            }

            float floatValue = 0.0f;
            memcpy(&floatValue, &dwordValue, sizeof(floatValue));

            ss << fields[i].name << " +0x"
                << std::hex << std::uppercase << fields[i].offset
                << " dword=0x";
            ss.width(8);
            ss.fill('0');
            ss << dwordValue;
            ss.fill(' ');
            ss << std::dec;
            if (std::isfinite(floatValue) && std::fabs(floatValue) < 1000000.0f) {
                ss << " float=" << floatValue;
            }
            ss << "\n";
        }
    }

    static void AppendSceneHolderFlags08Decode(std::ostringstream& ss, const SelectedEditorObject& selected) {
        ss << "\n[Scene Holder +0x08 Flag Decode]\n";
        if (selected.sceneHolder == 0 || !IsReadableRange(selected.sceneHolder + 0x08, 0x04)) {
            ss << "<unavailable>\n";
            return;
        }

        ss << "flags=0x" << std::hex << std::uppercase << selected.sceneHolderFlags08 << std::dec << "\n";
        ss << "bit0 0x0001 observedUnknown=" << (selected.sceneHolderBit0 ? "true" : "false") << "\n";
        ss << "bit2 0x0004 lockedToDrivingLineCandidate="
            << (selected.lockedToDrivingLineCandidate ? "true" : "false")
            << " / verticalAlignByGhidraCommand="
            << (selected.verticalAlign ? "true" : "false") << "\n";
        ss << "bit3 0x0008 hiddenCandidatePartOfVisibilityMask="
            << (selected.sceneHolderBit3 ? "true" : "false") << "\n";
        ss << "bit4 0x0010 horizontalAlignByGhidraCommand="
            << (selected.horizontalAlign ? "true" : "false") << "\n";
        ss << "bit5 0x0020 observedUnknown=" << (selected.sceneHolderBit5 ? "true" : "false") << "\n";
        ss << "bit6 0x0040 noContactResponseDecorationCandidate="
            << (selected.sceneHolderBit6 ? "true" : "false") << "\n";
        ss << "bit10 0x0400 fastObjectCandidate="
            << (selected.fastObjectCandidate ? "true" : "false") << "\n";
        ss << "bit12 0x1000 baselineEditorFlag="
            << (selected.sceneHolderBit12 ? "true" : "false") << "\n";
        ss << "visibleCandidateBit0x08Clear="
            << (selected.sceneHolderVisible ? "true" : "false") << "\n";
        ss << "contactResponseCandidateBit0x40Clear="
            << (selected.contactResponseEnabled ? "true" : "false") << "\n";
    }

    static void AppendSceneTreeSnapshot(std::ostringstream& ss, const char* title, uintptr_t root) {
        ss << "\n[" << title << "]\n";
        if (root == 0 || !IsReadableRange(root, 0x24)) {
            ss << "<unavailable>\n";
            return;
        }

        const std::vector<SceneNodeSnapshot> nodes = BuildSceneNodeSnapshot(root);
        ss << "nodeCount=" << nodes.size() << "\n";
        for (const SceneNodeSnapshot& node : nodes) {
            const uint32_t type = static_cast<uint32_t>(node.typeAndSubtype & 0xff);
            const uint32_t subtype = static_cast<uint32_t>(node.typeAndSubtype >> 8);
            ss << "depth=" << node.depth
                << " node=" << HexAddress(node.address)
                << " via=" << (node.via ? HexAddress(node.via) : "<root>")
                << " type=" << type
                << " subtype=" << subtype
                << " flags0c=0x" << std::hex << std::uppercase << node.flags0c << std::dec
                << " childMode=" << node.childMode
                << " childStorage=" << (node.childStorage ? HexAddress(node.childStorage) : "<null>")
                << " scaleEc=" << node.scaleEc
                << " physicsC9=" << static_cast<uint32_t>(node.physicsByteC9)
                << "\n";
        }
    }

    static std::string BuildEditorInspectorReport(
        uintptr_t gameManager,
        uintptr_t editorManager,
        uintptr_t selectionManager,
        uintptr_t entityManager,
        const std::vector<SelectedEditorObject>& selectedObjects) {
        std::ostringstream ss;
        ss << "Trials Fusion Mod - Editor Selected Object Inspector\n";
        ss << "gameManager=" << (gameManager ? HexAddress(gameManager) : "<none>") << "\n";
        ss << "editorManager=" << (editorManager ? HexAddress(editorManager) : "<none>") << "\n";
        ss << "selectionManager=" << (selectionManager ? HexAddress(selectionManager) : "<none>") << "\n";
        ss << "entityManager=" << (entityManager ? HexAddress(entityManager) : "<none>") << "\n";
        ss << "selectedCount=" << selectedObjects.size() << "\n";
        if (selectionManager != 0 && IsReadableRange(selectionManager, 0x9c)) {
            uintptr_t liveVariationObject = 0;
            uintptr_t variationSource0 = 0;
            uintptr_t variationSource1 = 0;
            uintptr_t variationSource2 = 0;
            float variationScaleFactor = 0.0f;
            SafeReadValue(selectionManager + 0x64, variationScaleFactor);
            SafeReadValue(selectionManager + 0x8c, liveVariationObject);
            SafeReadValue(selectionManager + 0x90, variationSource0);
            SafeReadValue(selectionManager + 0x94, variationSource1);
            SafeReadValue(selectionManager + 0x98, variationSource2);
            ss << "selectionManager+0x64(selectionBoundingRadius/gizmoScale)=" << variationScaleFactor << "\n";
            ss << "selectionManager+0x8c(liveVariationObject)="
                << (liveVariationObject ? HexAddress(liveVariationObject) : "<null>") << "\n";
            ss << "selectionManager+0x90/94/98(variationSources)="
                << (variationSource0 ? HexAddress(variationSource0) : "<null>") << ", "
                << (variationSource1 ? HexAddress(variationSource1) : "<null>") << ", "
                << (variationSource2 ? HexAddress(variationSource2) : "<null>") << "\n";
        }

        for (const SelectedEditorObject& selected : selectedObjects) {
            ss << "\n[Selected " << selected.index << "]\n";
            ss << "listNode=" << (selected.listNode ? HexAddress(selected.listNode) : "<null>") << "\n";
            ss << "selectedObject=" << (selected.selectedObject ? HexAddress(selected.selectedObject) : "<null>") << "\n";
            ss << "mappedObjectFromEntityManagerE74=" << (selected.mappedObject ? HexAddress(selected.mappedObject) : "<missing>") << "\n";
            ss << "editorTransformFromEntityManagerEA8=" << (selected.editorTransform ? HexAddress(selected.editorTransform) : "<missing>") << "\n";
            ss << "editorScaleBackingObjectFromEntityManagerE90=" << (selected.editorScaleBackingObject ? HexAddress(selected.editorScaleBackingObject) : "<missing>") << "\n";
            if (selected.editorScaleBackingObject != 0 && IsReadableRange(selected.editorScaleBackingObject, 0x14)) {
                float backingScale = 0.0f;
                SafeReadValue(selected.editorScaleBackingObject + 0x10, backingScale);
                ss << "[editorScaleBackingObject]+0x10(scaleScalar)=" << backingScale << "\n";
            }
            const std::vector<EntityMapScanResult> entityMapResults =
                ScanEntityManagerMapsForSelectedObject(selected);
            ss << "entityManagerMapScanC00To13FC.matchCount=" << entityMapResults.size() << "\n";
            for (const EntityMapScanResult& result : entityMapResults) {
                ss << "  entityManager+0x" << std::hex << std::uppercase << result.managerOffset
                    << std::dec
                    << " key=" << result.keyName << "(" << HexAddress(result.key) << ")"
                    << " layout(cap+0x" << std::hex << result.capacityOffset
                    << " entries+0x" << result.entriesOffset << std::dec << ")"
                    << " map=" << HexAddress(result.map)
                    << " capacity=" << result.capacity
                    << " entryValue=" << (result.entryValue ? HexAddress(result.entryValue) : "<null>")
                    << " mappedValue=" << (result.mappedValue ? HexAddress(result.mappedValue) : "<null>")
                    << "\n";
            }
            ss << "selectedObject+0x1c(parentKey)=" << (selected.parentObject ? HexAddress(selected.parentObject) : "<null>") << "\n";
            ss << "mappedParentObject=" << (selected.mappedParentObject ? HexAddress(selected.mappedParentObject) : "<missing>") << "\n";
            ss << "selectedObject.type=" << static_cast<uint32_t>(selected.selectedObjectType & 0xff)
                << " subtype=" << static_cast<uint32_t>(selected.selectedObjectType >> 8) << "\n";
            ss << "selectedObject.movementState04=0x" << std::hex << std::uppercase
                << selected.selectedObjectMovementState04 << std::dec
                << " movingOrRotatingCandidate="
                << (selected.movingOrRotatingCandidate ? "true" : "false") << "\n";
            ss << "selectedObject.flags0c=0x" << std::hex << std::uppercase
                << selected.selectedObjectFlags0c << std::dec
                << " physicsBit0x10000="
                << (selected.selectedObjectPhysicsEnabled ? "true" : "false") << "\n";
            ss << "selectedObject.childModeOrCount=" << selected.selectedObjectChildMode << "\n";
            ss << "selectedObject.children=" << (selected.selectedObjectChildren ? HexAddress(selected.selectedObjectChildren) : "<null>") << "\n";
            ss << "selectedObject.flags=0x" << std::hex << std::uppercase << selected.selectedObjectFlags << std::dec << "\n";
            ss << "selectedObject+0x44(sceneHolder)=" << (selected.sceneHolder ? HexAddress(selected.sceneHolder) : "<null>") << "\n";
            ss << "[sceneHolder]+0x00(resourceContainer)=" << (selected.resourceContainer ? HexAddress(selected.resourceContainer) : "<null>") << "\n";
            ss << "[resourceContainer]+0x68(resourceSceneRoot)=" << (selected.resourceSceneRoot ? HexAddress(selected.resourceSceneRoot) : "<null>") << "\n";

            if (selected.sceneHolder != 0 && IsReadableRange(selected.sceneHolder, 0x20)) {
                uint32_t holder1c = 0;
                float holder1cFloat = 0.0f;
                SafeReadValue(selected.sceneHolder + 0x1c, holder1c);
                memcpy(&holder1cFloat, &holder1c, sizeof(holder1cFloat));
                ss << "[sceneHolder]+0x08(flags)=0x" << std::hex << std::uppercase << selected.sceneHolderFlags08 << std::dec
                    << " horizontalAlign=" << (selected.horizontalAlign ? "true" : "false")
                    << " bit2VerticalOrDrivingLine=" << (selected.verticalAlign ? "true" : "false")
                    << " lockedToDrivingLineCandidate=" << (selected.lockedToDrivingLineCandidate ? "true" : "false")
                    << "\n";
                ss << "[sceneHolder]+0x1c=0x" << std::hex << std::uppercase << holder1c
                    << std::dec << " float=" << holder1cFloat << "\n";
            }

            ss << "\n[Named Editor Properties]\n";
            if (selected.hasRotation) {
                ss << "rotationRadians=" << selected.rotationRadians << "\n";
                ss << "rotationDegrees=" << (selected.rotationRadians * 57.2957795f) << "\n";
            }
            else {
                ss << "rotation=<unavailable>\n";
            }
            ss << "variationMaskFromSceneHolder+0x20=0x" << std::hex << std::uppercase
                << selected.sceneHolderVariationMask20 << std::dec
                << " variationIndexCandidate=";
            if (selected.sceneHolderVariationMask20 == 1 || selected.sceneHolderVariationMask20 == 2
                || selected.sceneHolderVariationMask20 == 4 || selected.sceneHolderVariationMask20 == 8) {
                ss << (VariationIndexFromMask(selected.sceneHolderVariationMask20) + 1);
            }
            else {
                ss << "<unknown>";
            }
            ss << "\n";
            if (selected.hasLightRange) {
                ss << "lightRangeFromFirstMesh+0xa4=" << selected.lightRangeRaw << "\n";
            }
            if (selected.hasSceneHolderBuoyancy) {
                ss << "buoyancyFromSceneHolder+0x1c=" << selected.sceneHolderBuoyancyRaw << "\n";
            }
            if (selected.hasFriction) {
                ss << "unknownFirstMesh+0x90=" << selected.frictionRaw << "\n";
            }
            if (selected.hasObjectGravity) {
                ss << "unknownFirstMesh+0x98=" << selected.objectGravityRaw << "\n";
            }
            if (selected.hasLightIntensity) {
                ss << "lightIntensityFromFirstMesh+0xb4=" << selected.lightIntensityRaw << "\n";
            }
            ss << "unknownFirstMesh+0xec=" << selected.unknownEcRaw << "\n";
            if (selected.hasMeshOffset94) {
                ss << "unknownFirstMesh+0x94=" << selected.meshOffset94Raw << "\n";
            }
            if (selected.hasMeshOffset9c) {
                ss << "unknownFirstMesh+0x9c=" << selected.meshOffset9cRaw << "\n";
            }
            if (selected.hasVisible) {
                ss << "visible=" << (selected.visible ? "true" : "false") << "\n";
            }
            else {
                ss << "visible=<unavailable>\n";
            }
            if (selected.hasSceneHolderVisible) {
                ss << "visibleCandidate=" << (selected.sceneHolderVisible ? "true" : "false")
                    << " derivedFromSceneHolder+0x08Mask0x08Clear\n";
                ss << "contactResponseCandidate=" << (selected.contactResponseEnabled ? "enabled" : "disabled")
                    << " derivedFromSceneHolder+0x08Mask0x40Clear\n";
                ss << "fastObjectCandidate=" << (selected.fastObjectCandidate ? "true" : "false")
                    << " derivedFromSceneHolder+0x08Mask0x0400\n";
                ss << "lockedToDrivingLineCandidate=" << (selected.lockedToDrivingLineCandidate ? "true" : "false")
                    << " derivedFromSceneHolder+0x08Mask0x04\n";
            }
            if (selected.hasShadowType) {
                ss << "shadowTypeCandidate=" << (selected.shadowTypeDynamicCandidate ? "dynamic" : "static")
                    << " derivedFromFirstMesh+0xc9\n";
            }
            else {
                ss << "shadowTypeCandidate=<unavailable>\n";
            }
            if (selected.hasSelectedObjectPhysicsEnabled) {
                ss << "selectedObjectPhysics=" << (selected.selectedObjectPhysicsEnabled ? "enabled" : "disabled")
                    << " derivedFromSelectedObject+0x0cMask0x10000\n";
            }
            if (selected.hasLightEnabled) {
                ss << "lightEnabled=" << (selected.lightEnabled ? "true" : "false")
                    << " derivedFromFirstMesh+0xca\n";
            }
            ss << "firstMeshSceneObject=" << (selected.firstMeshSceneObject ? HexAddress(selected.firstMeshSceneObject) : "<missing>") << "\n";
            ss << "meshSceneObjectCount=" << selected.meshSceneObjectCount << "\n";
            ss << "meshUsedReversedTypeSubtypeFallback="
                << (selected.meshUsedReversedTypeSubtypeFallback ? "true" : "false") << "\n";
            ss << "firstVisibilitySceneObject=" << (selected.firstVisibilitySceneObject ? HexAddress(selected.firstVisibilitySceneObject) : "<missing>") << "\n";
            ss << "visibilitySceneObjectCount=" << selected.visibilitySceneObjectCount << "\n";
            ss << "visibilityUsedReversedTypeSubtypeFallback="
                << (selected.visibilityUsedReversedTypeSubtypeFallback ? "true" : "false") << "\n";
            ss << "firstLightSceneObject=" << (selected.firstLightSceneObject ? HexAddress(selected.firstLightSceneObject) : "<missing>") << "\n";
            ss << "lightSceneObjectCount=" << selected.lightSceneObjectCount << "\n";
            ss << "lightUsedReversedTypeSubtypeFallback="
                << (selected.lightUsedReversedTypeSubtypeFallback ? "true" : "false") << "\n";
            AppendMaterialParameterLookupReport(ss, selected);
            AppendTrackEventMaterialBackingReport(ss, selected);
        }

        if (!selectedObjects.empty()) {
            const SelectedEditorObject& primary = selectedObjects.front();
            static const OffsetWatchField selectedWatch[] = {
                {"typeSubtype", 0x08},
                {"flags0c", 0x0c},
                {"childMode", 0x10},
                {"childStorage", 0x18},
                {"parentKey", 0x1c},
                {"flags20", 0x20},
                {"float24", 0x24},
                {"sceneHolder", 0x44},
                {"candidateB0", 0xb0},
                {"scaleEc", 0xec},
            };
            static const OffsetWatchField holderWatch[] = {
                {"resourceContainer", 0x00},
                {"flags", 0x08},
                {"visibilityAlignFlags", 0x08},
                {"float1c", 0x1c},
                {"variationMask", 0x20},
                {"candidate34", 0x34},
                {"candidate38", 0x38},
                {"candidate40", 0x40},
                {"candidate44", 0x44},
            };
            static const OffsetWatchField meshWatch[] = {
                {"flags0c", 0x0c},
                {"childMode", 0x10},
                {"parent", 0x1c},
                {"meshParentOrBuoyancyOld", 0x1c},
                {"unknown90_gizmoDetachOnWrite", 0x90},
                {"unknown94_gizmoDetachOnWrite", 0x94},
                {"unknown98_gizmoDetachOnWrite", 0x98},
                {"unknown9c_gizmoDetachOnWrite", 0x9c},
                {"lightRangeRaw", 0xa4},
                {"lightIntensityRaw", 0xb4},
                {"shadowTypeByte", 0xc9},
                {"lightEnabledByte", 0xca},
                {"unknownEc", 0xec},
            };
            AppendWatchedOffsetFields(
                ss,
                "Primary Selected Object Attribute Candidates",
                primary.selectedObject,
                selectedWatch,
                sizeof(selectedWatch) / sizeof(selectedWatch[0]));
            AppendWatchedOffsetFields(
                ss,
                "Primary Scene Holder Attribute Candidates",
                primary.sceneHolder,
                holderWatch,
                sizeof(holderWatch) / sizeof(holderWatch[0]));
            AppendSceneHolderFlags08Decode(ss, primary);
            AppendWatchedOffsetFields(
                ss,
                "Primary First Mesh Attribute Candidates",
                primary.firstMeshSceneObject,
                meshWatch,
                sizeof(meshWatch) / sizeof(meshWatch[0]));
            static const OffsetWatchField selectionManagerWatch[] = {
                {"listHead", 0x18},
                {"selectedCount", 0x20},
                {"positionVec3", 0x48},
                {"rotationQuat", 0x54},
                {"selectionBoundingRadius", 0x64},
                {"liveVariationObject", 0x8c},
                {"variationSource0", 0x90},
                {"variationSource1", 0x94},
                {"variationSource2", 0x98},
            };
            AppendWatchedOffsetFields(
                ss,
                "Selection Manager Variation Controller Candidates",
                selectionManager,
                selectionManagerWatch,
                sizeof(selectionManagerWatch) / sizeof(selectionManagerWatch[0]));
            AppendRawDwordFields(ss, "Primary Selected Object Raw +0x000..0x0ff", primary.selectedObject, 0x100);
            AppendRawDwordFields(ss, "Primary Mapped Object Raw +0x000..0x0ff", primary.mappedObject, 0x100);
            AppendRawDwordFields(ss, "Primary Editor Transform EA8 Raw +0x000..0x07f", primary.editorTransform, 0x80);
            AppendRawDwordFields(ss, "Primary Editor Scale Backing E90 Raw +0x000..0x07f", primary.editorScaleBackingObject, 0x80);
            const std::vector<EntityMapScanResult> primaryMapResults =
                ScanEntityManagerMapsForSelectedObject(primary);
            int dumpedMapBackings = 0;
            for (const EntityMapScanResult& result : primaryMapResults) {
                if (result.mappedValue == 0
                    || result.mappedValue == primary.mappedObject
                    || result.mappedValue == primary.editorTransform
                    || result.mappedValue == primary.editorScaleBackingObject) {
                    continue;
                }
                std::ostringstream title;
                title << "Primary EntityManager+0x" << std::hex << std::uppercase
                    << result.managerOffset << std::dec << " Mapped Backing Raw +0x000..0x07f";
                AppendRawDwordFields(ss, title.str().c_str(), result.mappedValue, 0x80);
                if (++dumpedMapBackings >= 8) {
                    break;
                }
            }
            AppendRawDwordFields(ss, "Primary Scene Holder Raw +0x000..0x0bf", primary.sceneHolder, 0xc0);
            AppendRawDwordFields(ss, "Selection Manager Raw +0x000..0x0bf", selectionManager, 0xc0);
            AppendRawDwordFields(ss, "Primary Resource Container Raw +0x000..0x0ff", primary.resourceContainer, 0x100);
            AppendRawDwordFields(ss, "Primary Resource Scene Root Raw +0x000..0x0ff", primary.resourceSceneRoot, 0x100);
            AppendRawDwordFields(ss, "Primary First Mesh Scene Object Raw +0x000..0x12f", primary.firstMeshSceneObject, 0x130);
            AppendSceneTreeSnapshot(ss, "Selected Object Scene Tree Snapshot", primary.selectedObject);
            AppendSceneTreeSnapshot(ss, "Resource Scene Tree Snapshot", primary.resourceSceneRoot);
        }

        return ss.str();
    }
}

static std::string GetKeybindingCategory(Keybindings::Action action) {
    switch (action) {
    case Keybindings::Action::ToggleDevMenu:
    case Keybindings::Action::ToggleKeybindingsMenu:
    case Keybindings::Action::ToggleOverlay:
    case Keybindings::Action::ToggleConsole:
    case Keybindings::Action::ClearConsole:
    case Keybindings::Action::ToggleVerboseLogging:
    case Keybindings::Action::ShowHelpText:
    case Keybindings::Action::DumpTweakables:
        return "General Controls";
    case Keybindings::Action::CycleHUD:
        return "Replay Controls";
    case Keybindings::Action::RespawnAtCheckpoint:
    case Keybindings::Action::RespawnPrevCheckpoint:
    case Keybindings::Action::RespawnNextCheckpoint:
    case Keybindings::Action::RespawnForward5:
    case Keybindings::Action::InstantFinish:
        return "Respawn Controls";
    case Keybindings::Action::CaptureSaveState:
    case Keybindings::Action::RestoreSaveState:
    case Keybindings::Action::DebugSaveState:
        return "Save States";
    case Keybindings::Action::IncrementFault:
    case Keybindings::Action::DebugFaultCounter:
    case Keybindings::Action::Add100Faults:
    case Keybindings::Action::Subtract100Faults:
    case Keybindings::Action::ResetFaults:
    case Keybindings::Action::DebugTimeCounter:
    case Keybindings::Action::Add60Seconds:
    case Keybindings::Action::Subtract60Seconds:
    case Keybindings::Action::Add10Minute:
    case Keybindings::Action::ResetTime:
    case Keybindings::Action::ToggleLimitValidation:
    case Keybindings::Action::RestoreDefaultLimits:
    case Keybindings::Action::DebugLimits:
        return "Fault / Time / Limit Controls";
    case Keybindings::Action::TogglePause:
        return "Pause Controls";
    case Keybindings::Action::ScanLeaderboardByID:
    case Keybindings::Action::ScanCurrentLeaderboard:
    case Keybindings::Action::TestFetchTrackID:
        return "Leaderboard Scanner Controls";
    case Keybindings::Action::StartAutoScroll:
    case Keybindings::Action::Killswitch:
    case Keybindings::Action::CycleSearch:
    case Keybindings::Action::DecreaseScrollDelay:
    case Keybindings::Action::IncreaseScrollDelay:
        return "Track Central Auto-Scroll Controls";
    case Keybindings::Action::SaveMultiplayerLogs:
    case Keybindings::Action::CaptureSessionState:
        return "Multiplayer Monitoring Controls";
    case Keybindings::Action::FullCountdownSequence:
    case Keybindings::Action::ShowSingleCountdown:
    case Keybindings::Action::ToggleLoadScreen:
        return "ActionScript Controls";
    case Keybindings::Action::TogglePatch:
    case Keybindings::Action::TogglePhysicsLogging:
    case Keybindings::Action::DumpPhysicsLog:
    case Keybindings::Action::ModifyXPosition:
        return "Debug / Patch Controls";
    case Keybindings::Action::SwapNextBike:
    case Keybindings::Action::SwapPrevBike:
    case Keybindings::Action::DebugBikeInfo:
        return "Bike Swap Controls";
    case Keybindings::Action::EditorScaleDecrease:
    case Keybindings::Action::EditorScaleIncrease:
        return "Editor Controls";
    case Keybindings::Action::DebugGameState:
        return "Bike / Game Debug Controls";
    default:
        return "Other Controls";
    }
}

std::shared_ptr<TweakableFloat> CreateSyncedFloat(int id, const std::string& name,
    float defaultVal, float minVal, float maxVal) {
    auto tweakable = std::make_shared<TweakableFloat>(id, name, defaultVal, minVal, maxVal);

    // Auto-sync to game memory on change
    tweakable->SetOnChangeCallback([id, name](float newValue) {
        LOG_VERBOSE("Float changed: " << name << " (ID=" << id << ") = " << newValue);
        if (!DevMenuSync::WriteValue<float>(id, newValue)) {
            LOG_WARNING("Failed to write Float ID=" << id << " to game memory!");
            DevMenuSync::DebugPrintTweakable(id);
        }
        else {
            LOG_VERBOSE("Successfully wrote Float ID=" << id << " to game memory");

            // Notify prevent-finish if this is a Bike or Rider value
            if (IsBikeId(id)) {
                PreventFinish::NotifyBikeModification();
            }
            else if (IsRiderId(id)) {
                PreventFinish::NotifyRiderModification();
            }
        }
        });

    return tweakable;
}

std::shared_ptr<TweakableInt> CreateSyncedInt(int id, const std::string& name,
    int defaultVal, int minVal, int maxVal) {
    auto tweakable = std::make_shared<TweakableInt>(id, name, defaultVal, minVal, maxVal);

    tweakable->SetOnChangeCallback([id, name](int newValue) {
        LOG_VERBOSE("Int changed: " << name << " (ID=" << id << ") = " << newValue);
        if (!DevMenuSync::WriteValue<int>(id, newValue)) {
            LOG_WARNING("Failed to write Int ID=" << id << " to game memory!");
            DevMenuSync::DebugPrintTweakable(id);
        }
        else {
            LOG_VERBOSE("Successfully wrote Int ID=" << id << " to game memory");

            // Notify prevent-finish if this is a Bike or Rider value
            if (IsBikeId(id)) {
                PreventFinish::NotifyBikeModification();
            }
            else if (IsRiderId(id)) {
                PreventFinish::NotifyRiderModification();
            }
        }
        });

    return tweakable;
}

std::shared_ptr<TweakableBool> CreateSyncedBool(int id, const std::string& name, bool defaultVal) {
    auto tweakable = std::make_shared<TweakableBool>(id, name, defaultVal);

    tweakable->SetOnChangeCallback([id, name](bool newValue) {
        LOG_VERBOSE("Bool changed: " << name << " (ID=" << id << ") = " << (newValue ? "true" : "false"));

        if (IsStartupGateId(id)) {
            LOG_VERBOSE("Bool ID=" << id << " is startup-managed; ignoring live DevMenu write");
            return;
        }

        // RedLynx tweakable bools are byte-sized and can be packed adjacently.
        unsigned char gameValue = newValue ? 1 : 0;
        if (!DevMenuSync::WriteValue<unsigned char>(id, gameValue)) {
            LOG_WARNING("Failed to write Bool ID=" << id << " to game memory!");
            DevMenuSync::DebugPrintTweakable(id);
        }
        else {
            LOG_VERBOSE("Successfully wrote Bool ID=" << id << " to game memory");

            // Notify prevent-finish if this is a Bike or Rider value
            if (IsBikeId(id)) {
                PreventFinish::NotifyBikeModification();
            }
            else if (IsRiderId(id)) {
                PreventFinish::NotifyRiderModification();
            }
        }
        });

    return tweakable;
}

// TweakableFloat Implementation
void TweakableFloat::Render() {
    float oldValue = m_value;

    // Create a unique ID for ImGui
    std::string label = "##" + m_name + std::to_string(m_id);

    ImGui::PushItemWidth(200.0f);

    // Use slider for float values
    if (ImGui::SliderFloat(label.c_str(), &m_value, m_minValue, m_maxValue, "%.3f")) {
        if (m_onChange) {
            m_onChange(m_value);
        }
    }

    ImGui::PopItemWidth();

    // Show the name after the slider
    ImGui::SameLine();
    ImGui::Text("%s", m_name.c_str());

    // Add reset button
    if (m_value != m_defaultValue) {
        ImGui::SameLine();
        std::string resetLabel = "Reset##" + std::to_string(m_id);
        if (ImGui::SmallButton(resetLabel.c_str())) {
            ResetToDefault();
        }
    }

    // Right-click for manual input
    if (ImGui::IsItemClicked(1)) {
        ImGui::OpenPopup(("Input##" + std::to_string(m_id)).c_str());
    }

    if (ImGui::BeginPopup(("Input##" + std::to_string(m_id)).c_str())) {
        ImGui::Text("Enter value:");
        if (ImGui::InputFloat("##input", &m_value, 0.0f, 0.0f, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue)) {
            if (m_onChange) {
                m_onChange(m_value);
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// TweakableInt Implementation
void TweakableInt::Render() {
    int oldValue = m_value;

    std::string label = "##" + m_name + std::to_string(m_id);

    // If this slider should render inline, use SameLine before rendering
    if (m_renderInline) {
        ImGui::SameLine();
    }

    // Set custom width if specified
    if (m_customWidth > 0.0f) {
        ImGui::PushItemWidth(m_customWidth);
    } else {
        ImGui::PushItemWidth(200.0f);
    }

    // Use slider for int values
    if (ImGui::SliderInt(label.c_str(), &m_value, m_minValue, m_maxValue)) {
        if (m_onChange) {
            m_onChange(m_value);
        }
    }

    ImGui::PopItemWidth();

    // Show the name after the slider (unless hidden)
    if (!m_hideLabel) {
        ImGui::SameLine();
        ImGui::Text("%s", m_name.c_str());
    }

    // Add reset button
    if (m_value != m_defaultValue) {
        ImGui::SameLine();
        std::string resetLabel = "Reset##" + std::to_string(m_id);
        if (ImGui::SmallButton(resetLabel.c_str())) {
            ResetToDefault();
        }
    }

    // Right-click for manual input
    if (ImGui::IsItemClicked(1)) {
        ImGui::OpenPopup(("Input##" + std::to_string(m_id)).c_str());
    }

    if (ImGui::BeginPopup(("Input##" + std::to_string(m_id)).c_str())) {
        ImGui::Text("Enter value:");
        if (ImGui::InputInt("##input", &m_value, 1, 10, ImGuiInputTextFlags_EnterReturnsTrue)) {
            if (m_onChange) {
                m_onChange(m_value);
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// TweakableBool Implementation
void TweakableBool::Render() {
    bool oldValue = m_value;

    std::string label = m_name + "##" + std::to_string(m_id);

    if (ImGui::Checkbox(label.c_str(), &m_value)) {
        if (m_onChange) {
            m_onChange(m_value);
        }
    }

    // Add reset button if value changed
    if (m_value != m_defaultValue) {
        ImGui::SameLine();
        std::string resetLabel = "Reset##" + std::to_string(m_id);
        if (ImGui::SmallButton(resetLabel.c_str())) {
            ResetToDefault();
        }
    }
}

// TweakableButton Implementation
void TweakableButton::Render() {
    std::string label = m_name + "##" + std::to_string(m_id);

    // If this button should render inline, use SameLine before rendering
    if (m_renderInline) {
        ImGui::SameLine();
    }

    // Apply custom colors if set
    if (m_useCustomColors) {
        ImGui::PushStyleColor(ImGuiCol_Button, m_buttonColor);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, m_buttonHoveredColor);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, m_buttonActiveColor);
    }

    // Determine button size
    ImVec2 buttonSize(0, 0);
    if (m_fixedWidth > 0.0f) {
        buttonSize.x = m_fixedWidth;
    }

    if (ImGui::Button(label.c_str(), buttonSize)) {
        if (m_onClick) {
            m_onClick();
        }
    }

    // Pop custom colors if they were set
    if (m_useCustomColors) {
        ImGui::PopStyleColor(3);
    }
}

// TweakableTireColor Implementation
namespace {
    static constexpr uintptr_t TIRE_COLOR_CHAIN_BASE_RVA_STEAM = 0x01057278;
    static constexpr uintptr_t TIRE_COLOR_CHAIN_BASE_RVA_UPLAY = 0x01057278 - 0x2000;

    static float ClampFloat(float value, float minValue, float maxValue) {
        if (value < minValue) {
            return minValue;
        }
        if (value > maxValue) {
            return maxValue;
        }
        return value;
    }

    static bool ResolvePointerChainWithInitialDeref(uintptr_t baseAddress, std::initializer_list<uintptr_t> offsets, uintptr_t& outAddress) {
        __try {
            if (IsBadReadPtr(reinterpret_cast<void*>(baseAddress), sizeof(uintptr_t))) {
                return false;
            }

            // Match the usual Cheat Engine-style chain:
            //   current = *(baseAddress)
            //   current = *(current + offset[n]) for intermediates
            //   result  = current + finalOffset
            uintptr_t current = *reinterpret_cast<uintptr_t*>(baseAddress);
            if (current == 0) {
                return false;
            }

            auto it = offsets.begin();
            while (it != offsets.end()) {
                const uintptr_t offset = *it;
                ++it;

                if (it == offsets.end()) {
                    outAddress = current + offset;
                    return !IsBadReadPtr(reinterpret_cast<void*>(outAddress), sizeof(float));
                }

                uintptr_t pointerAddress = current + offset;
                if (IsBadReadPtr(reinterpret_cast<void*>(pointerAddress), sizeof(uintptr_t))) {
                    return false;
                }

                current = *reinterpret_cast<uintptr_t*>(pointerAddress);
                if (current == 0) {
                    return false;
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }

        return false;
    }

    static bool ResolvePointerChainWithoutInitialDeref(uintptr_t baseAddress, std::initializer_list<uintptr_t> offsets, uintptr_t& outAddress) {
        __try {
            uintptr_t current = baseAddress;
            auto it = offsets.begin();
            while (it != offsets.end()) {
                const uintptr_t offset = *it;
                ++it;

                if (it == offsets.end()) {
                    outAddress = current + offset;
                    return !IsBadReadPtr(reinterpret_cast<void*>(outAddress), sizeof(float));
                }

                uintptr_t pointerAddress = current + offset;
                if (IsBadReadPtr(reinterpret_cast<void*>(pointerAddress), sizeof(uintptr_t))) {
                    return false;
                }

                current = *reinterpret_cast<uintptr_t*>(pointerAddress);
                if (current == 0) {
                    return false;
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }

        return false;
    }

    static bool ResolvePointerChainEitherStyle(uintptr_t baseAddress, std::initializer_list<uintptr_t> offsets, uintptr_t& outAddress) {
        return ResolvePointerChainWithInitialDeref(baseAddress, offsets, outAddress)
            || ResolvePointerChainWithoutInitialDeref(baseAddress, offsets, outAddress);
    }
}

TweakableTireColor::TweakableTireColor(int id, const std::string& name)
    : TweakableItem(id, name, TweakableType::Custom)
    , m_color{ 1.0f, 1.0f, 1.0f }
    , m_defaultColor{ 1.0f, 1.0f, 1.0f }
    , m_brightness(1.0f)
    , m_defaultBrightness(1.0f)
    , m_hasCapturedDefaults(false) {
}

bool TweakableTireColor::ResolveAddresses(uintptr_t& redAddr, uintptr_t& greenAddr, uintptr_t& blueAddr, uintptr_t& brightnessAddr) const {
    uintptr_t moduleBase = reinterpret_cast<uintptr_t>(GetModuleHandle(NULL));
    if (moduleBase == 0) {
        return false;
    }

    const uintptr_t tireColorBaseRva = BaseAddress::IsSteamVersion()
        ? TIRE_COLOR_CHAIN_BASE_RVA_STEAM
        : TIRE_COLOR_CHAIN_BASE_RVA_UPLAY;
    uintptr_t chainBase = moduleBase + tireColorBaseRva;
    return ResolvePointerChainEitherStyle(chainBase, { 0x18, 0x268, 0x74, 0x94, 0x20, 0x50 }, redAddr)
        && ResolvePointerChainEitherStyle(chainBase, { 0x18, 0x268, 0x74, 0x94, 0x20, 0x54 }, greenAddr)
        && ResolvePointerChainEitherStyle(chainBase, { 0x18, 0x268, 0x74, 0x94, 0x20, 0x58 }, blueAddr)
        && ResolvePointerChainEitherStyle(chainBase, { 0x18, 0x268, 0x74, 0x94, 0x20, 0x74 }, brightnessAddr);
}

bool TweakableTireColor::ReadCurrentValues(float outColor[3], float& outBrightness) const {
    uintptr_t redAddr = 0, greenAddr = 0, blueAddr = 0, brightnessAddr = 0;
    if (!ResolveAddresses(redAddr, greenAddr, blueAddr, brightnessAddr)) {
        return false;
    }

    __try {
        outColor[0] = *reinterpret_cast<float*>(redAddr);
        outColor[1] = *reinterpret_cast<float*>(greenAddr);
        outColor[2] = *reinterpret_cast<float*>(blueAddr);
        outBrightness = *reinterpret_cast<float*>(brightnessAddr);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool TweakableTireColor::WriteColor(const float color[3]) const {
    uintptr_t redAddr = 0, greenAddr = 0, blueAddr = 0, brightnessAddr = 0;
    if (!ResolveAddresses(redAddr, greenAddr, blueAddr, brightnessAddr)) {
        return false;
    }

    __try {
        *reinterpret_cast<float*>(redAddr) = color[0];
        *reinterpret_cast<float*>(greenAddr) = color[1];
        *reinterpret_cast<float*>(blueAddr) = color[2];
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool TweakableTireColor::WriteBrightness(float brightness) const {
    uintptr_t redAddr = 0, greenAddr = 0, blueAddr = 0, brightnessAddr = 0;
    if (!ResolveAddresses(redAddr, greenAddr, blueAddr, brightnessAddr)) {
        return false;
    }

    __try {
        *reinterpret_cast<float*>(brightnessAddr) = brightness;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void TweakableTireColor::Render() {
    static constexpr float kTireColorSwatchColumnX = 205.0f;
    static constexpr float kTireColorResetColumnX = 235.0f;
    static constexpr float kTireBrightnessLabelColumnX = 295.0f;
    static constexpr float kTireBrightnessSliderColumnX = 385.0f;

    float liveColor[3] = {};
    float liveBrightness = 1.0f;
    const bool isAvailable = ReadCurrentValues(liveColor, liveBrightness);

    if (!isAvailable) {
        ImGui::TextDisabled("%s unavailable (load into a bike scene first)", m_name.c_str());
        return;
    }

    if (!m_hasCapturedDefaults) {
        for (int i = 0; i < 3; ++i) {
            m_color[i] = liveColor[i];
            m_defaultColor[i] = liveColor[i];
        }
        m_brightness = liveBrightness;
        m_defaultBrightness = liveBrightness;
        m_hasCapturedDefaults = true;
    }

    float normalizedPickerColor[3] = {
        ClampFloat(m_color[0] / 6.0f, 0.0f, 1.0f),
        ClampFloat(m_color[1] / 6.0f, 0.0f, 1.0f),
        ClampFloat(m_color[2] / 6.0f, 0.0f, 1.0f)
    };

    const bool colorChanged =
        fabsf(m_color[0] - m_defaultColor[0]) > 0.0001f ||
        fabsf(m_color[1] - m_defaultColor[1]) > 0.0001f ||
        fabsf(m_color[2] - m_defaultColor[2]) > 0.0001f;
    const bool brightnessChanged = fabsf(m_brightness - m_defaultBrightness) > 0.0001f;

    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s", m_name.c_str());
    ImGui::SameLine(kTireColorSwatchColumnX);

    std::string colorLabel = "##" + m_name + "Color" + std::to_string(m_id);
    if (ImGui::ColorEdit3(
        colorLabel.c_str(),
        normalizedPickerColor,
        ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel
    )) {
        for (int i = 0; i < 3; ++i) {
            m_color[i] = ClampFloat(normalizedPickerColor[i] * 6.0f, 0.0f, 6.0f);
        }
        if (!WriteColor(m_color)) {
            LOG_WARNING("[DevMenu] Failed to write live tire diffuse color");
        }
    }
    if (colorChanged) {
        ImGui::SameLine(kTireColorResetColumnX);
        if (ImGui::SmallButton(("Reset##Color" + std::to_string(m_id)).c_str())) {
            for (int i = 0; i < 3; ++i) {
                m_color[i] = m_defaultColor[i];
            }
            if (!WriteColor(m_color)) {
                LOG_WARNING("[DevMenu] Failed to reset live tire diffuse color");
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Reset color to default");
        }
    }

    ImGui::SameLine(kTireBrightnessLabelColumnX);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Brightness");
    ImGui::SameLine(kTireBrightnessSliderColumnX);

    ImGui::PushItemWidth(200.0f);
    std::string brightnessLabel = "##" + m_name + "Brightness" + std::to_string(m_id);
    if (ImGui::SliderFloat(brightnessLabel.c_str(), &m_brightness, 0.0f, 1.0f, "%.3f")) {
        m_brightness = ClampFloat(m_brightness, 0.0f, 1.0f);
        if (!WriteBrightness(m_brightness)) {
            LOG_WARNING("[DevMenu] Failed to write live tire brightness");
        }
    }
    ImGui::PopItemWidth();
    if (brightnessChanged) {
        ImGui::SameLine();
        if (ImGui::SmallButton(("Reset##Brightness" + std::to_string(m_id)).c_str())) {
            m_brightness = m_defaultBrightness;
            if (!WriteBrightness(m_brightness)) {
                LOG_WARNING("[DevMenu] Failed to reset live tire brightness");
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Reset brightness to default");
        }
    }
}

void TweakableTireColor::Reset() {
    for (int i = 0; i < 3; ++i) {
        m_color[i] = m_defaultColor[i];
    }
    m_brightness = m_defaultBrightness;
}

void TweakableTireColor::ResetToDefault() {
    Reset();
    WriteColor(m_color);
    WriteBrightness(m_brightness);
}

namespace {
    static constexpr int kBikePartMaxColorParts = 3;
    static constexpr float kGearEditorControlColumnX = 190.0f;
    static constexpr float kGearEditorControlWidth = 175.0f;
    static constexpr float kGearEditorColorColumnX = 380.0f;
    static constexpr float kGearEditorVisibilityColumnX = 475.0f;
    static constexpr float kGearEditorVariantColumnX = 570.0f;
    static constexpr float kGearEditorColorSwatchSize = 22.0f;
    static constexpr float kGearEditorVisibilityButtonSize = 24.0f;

    static const BikeItemCatalogEntry* FindBikeCatalogEntry(uint16_t itemId) {
        for (size_t i = 0; i < kBikeItemCatalogCount; ++i) {
            if (kBikeItemCatalog[i].id == itemId) {
                return &kBikeItemCatalog[i];
            }
        }
        return nullptr;
    }

    static const GearSetCatalogEntry* FindGearSetCatalogEntry(uint16_t setId) {
        for (size_t i = 0; i < kGearSetCatalogCount; ++i) {
            if (kGearSetCatalog[i].id == setId) {
                return &kGearSetCatalog[i];
            }
        }
        return nullptr;
    }

    static std::string BikeKeyFromGearSetId(int setId) {
        if (setId <= 0 || setId > 0xffff) {
            return "";
        }

        const GearSetCatalogEntry* entry = FindGearSetCatalogEntry(static_cast<uint16_t>(setId));
        if (!entry || entry->kind != GearSetKind::Bike) {
            return "";
        }

        const std::string genKey(entry->genKey);
        static const char* kBikeKeys[] = {
            "CHEETAH",
            "KINGCOBRA",
            "MINX",
            "TRICKY",
            "FAT",
            "PAPER",
            "UNICORN"
        };
        for (const char* bikeKey : kBikeKeys) {
            if (genKey.find(bikeKey) != std::string::npos) {
                return bikeKey;
            }
        }

        return "";
    }

    static int GearSetVariantIndexFromSuffix(const std::string& suffix) {
        if (suffix == "1") return 1;
        if (suffix == "2") return 2;
        if (suffix == "4") return 3;
        if (suffix == "8") return 4;
        if (suffix == "16") return 5;
        if (suffix == "32") return 6;
        if (suffix == "64") return 7;
        if (suffix == "128") return 8;
        return 0;
    }

    static const char* GearSetFamilyAlias(const std::string& family) {
        if (family == "DIRTRUNNER") return "OUTSIDER";
        if (family == "TECHBOY") return "BIOTECH";
        if (family == "EVEL") return "DASHING HERO";
        if (family == "XPOLICE") return "ENFORCER";
        if (family == "FMX") return "CONTENDER";
        if (family == "GEOLOGIST") return "EXCAVATOR";
        if (family == "HAZMASUIT") return "HAZMAT";
        if (family == "SUPERHERO") return "SUPERHERO";
        if (family == "CHEETAH") return "Pit-Viper";
        if (family == "KINGCOBRA") return "ROACH";
        if (family == "TRICKY") return "FOXBAT";
        if (family == "PAPER") return "RABBIT";
        if (family == "FAT") return "PANDA";
        if (family == "SQUIRREL") return "SQUIRREL";
        if (family == "ULCCLOWN") return "CLOWN";
        if (family == "ULCDRINK") return "DRINK";
        if (family == "ULCRHINO") return "RHINO";
        if (family == "ROACH") return "ROACH";
        if (family == "RABBIT") return "RABBIT";
        if (family == "PITVIPER") return "PIT-VIPER";
        if (family == "RAYMAN") return "RAYMAN";
        if (family == "AC") return "ASSASSIN'S CREED";
        if (family == "BOUNTY") return "BOUNTY HUNTER";
        if (family == "BOUNTY_HUNTER") return "BOUNTY HUNTER";
        if (family == "DAPPER_PUG") return "SPACE PUG";
        if (family == "DAPPER") return "DAPPER";
        if (family == "LUMITECH") return "LUMI-TECH";
        if (family == "LUMITECH_FEMALE") return "LUMI-TECH";
        if (family == "POST_APO") return "SURVIVOR";
        if (family == "POSTAPO") return "RUSTLANDER";
        if (family == "POSTAPO_FEMALE") return "RUSTLANDER";
        if (family == "MALE") return "ELIMINATOR";
        if (family == "MALE_ASSASSIN") return "ELIMINATOR";
        if (family == "FEMALE") return "AVIATRIX";
        if (family == "FEMALE_OUTFIT_1") return "AVIATRIX";
        if (family == "FEMALE_BARBARA") return "BARBARA";
        if (family == "FEMALE_INDIAN") return "APACHE";
        if (family == "FEMALE_ASSASSIN") return "ASSASSIN";
        if (family == "AC_ELISE") return "ASSASSIN'S CREED";
        if (family == "AC_ARNO") return "ASSASSIN'S CREED";
        if (family == "WD") return "WATCH DOGS";
        if (family == "WD_CLARA") return "WATCH DOGS";
        if (family == "WD_AIDEN") return "WATCH DOGS";
        if (family == "DONKEY") return "DONKEY";
        if (family == "UNICORN") return "UNICORN";
        if (family == "MINDSCREW") return "MINDSCREW";
        if (family == "COMPONENTIAL") return "COMPONENTIAL";
        if (family == "EVILITY") return "EVILITY";
        if (family == "GRINDER") return "GRINDER";
        if (family == "HOLOGRAPH") return "HOLOGRAPH";
        if (family == "SUMIE") return "SUMIE";
        if (family == "CLOCKWORK") return "CLOCKWORK";
        if (family == "SPITTING") return "SPITTING IMAGE";
        if (family == "EVO") return "EVO";
        if (family == "GOLD") return "GOLD";
        if (family == "WILDCAT") return "WILDCAT";
        if (family == "STICKER") return "STICKER";
        if (family == "COMIC") return "COMIC";
        if (family == "WASP") return "WASP";
        if (family == "PLAGUEDOCTOR") return "PLAGUE DOCTOR";
        if (family == "TKO_PANDA") return "TKO PANDA CROWN";
        if (family == "BAGGIE") return "BAGGIE CROWN";
        if (family == "CYBORG") return "TITANIUM CRANIUM";
        return nullptr;
    }

    static const char* GearSetExactAlias(const std::string& genKey) {
        if (genKey == "BOUNTY_HUNTER_HELMET") return "BOUNTY HUNTER";
        if (genKey == "POST_APO_HELMET_1") return "SURVIVOR";
        if (genKey == "TKO_PANDA_HELMET_1") return "TKO PANDA CROWN";
        if (genKey == "BAGGIE_HELMET_1") return "BAGGIE CROWN";
        if (genKey == "PLAGUEDOCTOR_HELMET_1") return "PLAGUE DOCTOR";
        if (genKey == "RABBIT_HELMET_1") return "RABBIT CROWN";
        if (genKey == "CYBORG_HELMET_1") return "TITANIUM CRANIUM";
        return nullptr;
    }

    static bool IsPitViperDlcGearSet(const GearSetCatalogEntry& entry) {
        if (entry.kind != GearSetKind::Bike || entry.slot != GearSetSlot::BikeFairings) {
            return false;
        }

        const std::string genKey(entry.genKey);
        return genKey == "GOLD_PITVIPER"
            || genKey == "WD_PITVIPER"
            || genKey == "COMIC_PITVIPER"
            || genKey == "EVO_CHEETAH";
    }

    static bool IsPitViperDlcBikePartFamily(const std::string& family) {
        return family == "GOLD"
            || family == "WD"
            || family == "COMIC"
            || family == "EVO";
    }

    static std::string GearSetFallbackName(const GearSetCatalogEntry& entry) {
        std::string name(entry.genKey);
        std::replace(name.begin(), name.end(), '_', ' ');
        return name;
    }

    static std::string GearSetFallbackFamilyName(const std::string& family) {
        std::string name(family);
        std::replace(name.begin(), name.end(), '_', ' ');
        return name;
    }

    static const char* GearSetSlotSingular(GearSetSlot slot) {
        switch (slot) {
        case GearSetSlot::RiderTop:
            return "Torso";
        case GearSetSlot::RiderBottom:
            return "Legs";
        case GearSetSlot::RiderHelmet:
            return "Helmet";
        case GearSetSlot::BikeFairings:
            return "Fairings";
        case GearSetSlot::BikeWheels:
            return "Wheels";
        default:
            return "Gear";
        }
    }

    static bool ParseGearSetFamilyAndVariant(const GearSetCatalogEntry& entry, std::string& family, int& variant) {
        const std::string genKey(entry.genKey);
        static const char* kSlotTokens[] = {
            "_HELMETS_",
            "_HELMET_",
            "_BOTTOMS_",
            "_BOTTOM_",
            "_TOPS_",
            "_TOP_",
            "_FAIRINGS_",
            "_WHEELS_",
            "_RIM_"
        };

        for (const char* token : kSlotTokens) {
            const std::string slotToken(token);
            const size_t slotPos = genKey.rfind(slotToken);
            if (slotPos == std::string::npos || slotPos == 0) {
                continue;
            }

            family = genKey.substr(0, slotPos);
            variant = GearSetVariantIndexFromSuffix(genKey.substr(slotPos + slotToken.length()));
            return variant > 0;
        }

        static const char* kTerminalSlotTokens[] = {
            "_HELMETS",
            "_HELMET",
            "_BOTTOMS",
            "_BOTTOM",
            "_TOPS",
            "_TOP",
            "_FAIRINGS",
            "_WHEELS"
        };

        for (const char* token : kTerminalSlotTokens) {
            const std::string slotToken(token);
            if (genKey.length() <= slotToken.length()) {
                continue;
            }

            const size_t slotPos = genKey.length() - slotToken.length();
            if (genKey.compare(slotPos, slotToken.length(), slotToken) == 0 && slotPos > 0) {
                family = genKey.substr(0, slotPos);
                variant = 1;
                return true;
            }
        }

        const size_t firstUnderscore = genKey.find('_');
        const size_t lastUnderscore = genKey.rfind('_');
        if (firstUnderscore == std::string::npos || lastUnderscore == std::string::npos || lastUnderscore <= firstUnderscore) {
            return false;
        }

        family = genKey.substr(0, firstUnderscore);
        variant = GearSetVariantIndexFromSuffix(genKey.substr(lastUnderscore + 1));
        return variant > 0;
    }

    static std::string GearSetDisplayName(const GearSetCatalogEntry& entry) {
        if (const char* exactAlias = GearSetExactAlias(entry.genKey)) {
            return exactAlias;
        }

        if (IsPitViperDlcGearSet(entry)) {
            std::ostringstream stream;
            stream << GearSetFallbackName(entry) << " (" << GearSetFamilyAlias("CHEETAH") << " "
                << GearSetSlotSingular(entry.slot) << ")";
            return stream.str();
        }

        std::string family;
        int variant = 0;
        if (ParseGearSetFamilyAndVariant(entry, family, variant)) {
            const char* familyAlias = GearSetFamilyAlias(family);
            if (familyAlias) {
                const bool numberedSingleItem =
                    (family == "EVEL" || family == "HAZMASUIT" || family == "SUPERHERO" ||
                        family == "SQUIRREL" || family == "ULCCLOWN" || family == "ULCDRINK" || family == "ULCRHINO" ||
                        family == "BOUNTY_HUNTER" || family == "POST_APO" || family == "PLAGUEDOCTOR" ||
                        family == "TKO_PANDA" || family == "BAGGIE" || family == "CYBORG") &&
                    variant == 1;

                std::ostringstream aliasStream;
                aliasStream << familyAlias << ' ' << GearSetSlotSingular(entry.slot);
                if (!numberedSingleItem) {
                    aliasStream << ' ' << variant;
                }
                return aliasStream.str();
            }
        }

        std::ostringstream stream;
        stream << entry.id << " - " << GearSetFallbackName(entry);
        return stream.str();
    }

    struct GearSetGroupOption {
        const GearSetCatalogEntry* entry;
        int variant;
    };

    struct GearSetGroup {
        std::string label;
        std::vector<GearSetGroupOption> options;
    };

    static std::string GearSetGroupLabel(const GearSetCatalogEntry& entry) {
        if (const char* exactAlias = GearSetExactAlias(entry.genKey)) {
            return exactAlias;
        }

        if (IsPitViperDlcGearSet(entry)) {
            return GearSetFamilyAlias("CHEETAH");
        }

        std::string family;
        int variant = 0;
        if (ParseGearSetFamilyAndVariant(entry, family, variant)) {
            const char* familyAlias = GearSetFamilyAlias(family);
            if (familyAlias) {
                std::ostringstream stream;
                stream << familyAlias;
                return stream.str();
            }

            return GearSetFallbackFamilyName(family);
        }

        return GearSetFallbackName(entry);
    }

    static std::vector<GearSetGroup> BuildGearSetGroups(GearSetSlot slot) {
        std::vector<GearSetGroup> groups;
        for (size_t i = 0; i < kGearSetCatalogCount; ++i) {
            const GearSetCatalogEntry& entry = kGearSetCatalog[i];
            if (entry.slot != slot) {
                continue;
            }

            std::string family;
            int variant = 0;
            if (!ParseGearSetFamilyAndVariant(entry, family, variant)) {
                variant = 1;
            }

            const std::string groupLabel = GearSetGroupLabel(entry);
            auto groupIt = std::find_if(groups.begin(), groups.end(), [&groupLabel](const GearSetGroup& group) {
                return group.label == groupLabel;
            });
            if (groupIt == groups.end()) {
                groups.push_back({ groupLabel, {} });
                groupIt = groups.end() - 1;
            }

            groupIt->options.push_back({ &entry, variant });
        }

        return groups;
    }

    static std::string GearSetPreview(int setId) {
        if (setId <= 0 || setId > 0xffff) {
            return std::to_string(setId);
        }

        const GearSetCatalogEntry* entry = FindGearSetCatalogEntry(static_cast<uint16_t>(setId));
        if (!entry) {
            std::ostringstream stream;
            stream << setId << " - unknown";
            return stream.str();
        }

        return GearSetDisplayName(*entry);
    }

    static std::string PrettyPartName(const std::string& partKey) {
        static const std::map<std::string, std::string> kAliases = {
            { "BACKSWING", "Back Swing Arm" },
            { "BACK_SWING_ARM", "Back Swing Arm" },
            { "BOY_FRAME", "Frame" },
            { "BOY_PEDAL_ARMS", "Pedal Arms" },
            { "BOY_PEDAL_LEFT", "Pedal Left" },
            { "BOY_PEDAL_RIGHT", "Pedal Right" },
            { "BOY_STEERING", "Steering" },
            { "BOY_SUSPENSION_FRONT", "Front Suspension" },
            { "BOY_TIRE_FRONT", "Front Tire" },
            { "BOY_TIRE_REAR", "Rear Tire" },
            { "ENGINE", "Engine" },
            { "ENGINE_FENDER", "Engine Fender" },
            { "EXHAUST", "Exhaust" },
            { "EXHAUST_PIPE", "Exhaust" },
            { "FENDER_FRONT", "Front Fender" },
            { "FENDER_REAR", "Rear Fender" },
            { "FRAME", "Frame" },
            { "FRONT_FENDER", "Front Fender" },
            { "FRONT_RIM", "Front Rim" },
            { "FRONT_SUSPENSION", "Front Suspension" },
            { "FRONT_TIRE", "Front Tire" },
            { "GRIZZLY_BODY", "Body" },
            { "GRIZZLY_CHASSIS", "Chassis" },
            { "GRIZZLY_EXHAUST", "Exhaust" },
            { "GRIZZLY_HEADLIGHT", "Headlight" },
            { "GRIZZLY_STEERING", "Steering" },
            { "GRIZZLY_SUSPENSION_FRONT", "Front Suspension" },
            { "GRIZZLY_SUSPENSION_REAR", "Rear Suspension" },
            { "HEAD_LIGHT", "Headlight" },
            { "HEADLIGHT", "Headlight" },
            { "REAR_FENDER", "Back Fender" },
            { "REAR_RIM", "Back Rim" },
            { "REAR_TIRE", "Back Wheel" },
            { "RIM_FRONT", "Front Rim" },
            { "RIM_REAR", "Back Rim" },
            { "SEAT", "Seat" },
            { "STEERING", "Steering" },
            { "SUSPENSION_FRONT", "Front Suspension" },
            { "SWING_ARM", "Back Swing Arm" },
            { "TANK", "Tank" },
            { "TIRE_FRONT", "Front Tire" },
            { "TIRE_REAR", "Back Wheel" },
            { "WHEEL_FRONT", "Front Wheel" },
            { "WHEEL_REAR", "Back Wheel" },
        };

        const auto aliasIt = kAliases.find(partKey);
        if (aliasIt != kAliases.end()) {
            return aliasIt->second;
        }

        std::string result;
        bool capitalizeNext = true;
        for (char ch : partKey) {
            if (ch == '_') {
                result.push_back(' ');
                capitalizeNext = true;
                continue;
            }

            const char lower = static_cast<char>(tolower(static_cast<unsigned char>(ch)));
            result.push_back(capitalizeNext ? static_cast<char>(toupper(static_cast<unsigned char>(lower))) : lower);
            capitalizeNext = false;
        }
        return result;
    }

    static int BikeItemVariantIndex(const BikeItemCatalogEntry& entry) {
        const std::string genKey(entry.genKey);
        const size_t lastUnderscore = genKey.rfind('_');
        if (lastUnderscore == std::string::npos) {
            return 1;
        }
        const int variant = GearSetVariantIndexFromSuffix(genKey.substr(lastUnderscore + 1));
        return variant > 0 ? variant : 1;
    }

    static std::string BikeItemFamilyLabel(const BikeItemCatalogEntry& entry) {
        if (IsPitViperDlcBikePartFamily(entry.bikeKey)) {
            return GearSetFamilyAlias("CHEETAH");
        }

        const char* familyAlias = GearSetFamilyAlias(entry.bikeKey);
        return familyAlias ? familyAlias : entry.bikeKey;
    }

    static bool IsSpecialRimFamily(const std::string& bikeKey) {
        return bikeKey == "MINDSCREW"
            || bikeKey == "COMPONENTIAL"
            || bikeKey == "SUMIE"
            || bikeKey == "HOLOGRAPH"
            || bikeKey == "GRINDER"
            || bikeKey == "CLOCKWORK"
            || bikeKey == "SPITTING"
            || bikeKey == "EVILITY";
    }

    static int SpecialRimFamilyOrder(const std::string& bikeKey) {
        if (bikeKey == "MINDSCREW") return 0;
        if (bikeKey == "COMPONENTIAL") return 1;
        if (bikeKey == "SUMIE") return 2;
        if (bikeKey == "HOLOGRAPH") return 3;
        if (bikeKey == "GRINDER") return 4;
        if (bikeKey == "CLOCKWORK") return 5;
        if (bikeKey == "SPITTING") return 6;
        if (bikeKey == "EVILITY") return 7;
        return 100;
    }

    static bool IsRimPart(const std::string& partKey) {
        return partKey == "FRONT_RIM"
            || partKey == "RIM_FRONT"
            || partKey == "REAR_RIM"
            || partKey == "RIM_REAR";
    }

    static bool IsFrontRimPart(const std::string& partKey) {
        return partKey == "FRONT_RIM" || partKey == "RIM_FRONT";
    }

    static bool IsRearRimPart(const std::string& partKey) {
        return partKey == "REAR_RIM" || partKey == "RIM_REAR";
    }

    static bool SpecialRimEntryMatchesTarget(
        const BikeItemCatalogEntry& entry,
        const std::string& targetBikeKey,
        const std::string& targetPartKey) {
        if (!IsRimPart(entry.partKey) || !IsSpecialRimFamily(entry.bikeKey)) {
            return true;
        }
        if (targetBikeKey.empty() || !IsRimPart(targetPartKey)) {
            return false;
        }
        if (IsFrontRimPart(targetPartKey) != IsFrontRimPart(entry.partKey)
            || IsRearRimPart(targetPartKey) != IsRearRimPart(entry.partKey)) {
            return false;
        }

        const std::string genKey(entry.genKey);
        static const char* kBikeKeyTokens[] = {
            "CHEETAH",
            "KINGCOBRA",
            "MINX",
            "TRICKY"
        };
        for (const char* bikeKey : kBikeKeyTokens) {
            if (genKey.find(bikeKey) != std::string::npos && targetBikeKey != bikeKey) {
                return false;
            }
        }

        // The generic *_RIM_BACK_KINGCOBRA_* entries are alternate rear-rim
        // payloads for Kingcobra only. Keeping them off other bikes prevents
        // duplicate back-rim choices while preserving the generic rear entry.
        if (genKey.find("RIM_BACK_KINGCOBRA") != std::string::npos && targetBikeKey != "KINGCOBRA") {
            return false;
        }

        return true;
    }

    static std::string BikePartGroupLabelForEntry(const BikeItemCatalogEntry& entry, const std::string& targetBikeKey) {
        if (IsRimPart(entry.partKey) && IsSpecialRimFamily(entry.bikeKey) && !targetBikeKey.empty()) {
            const char* familyAlias = GearSetFamilyAlias(targetBikeKey);
            return familyAlias ? familyAlias : targetBikeKey;
        }

        return BikeItemFamilyLabel(entry);
    }

    static std::string BikeItemGroupLabel(const BikeItemCatalogEntry& entry) {
        std::ostringstream stream;
        stream << BikeItemFamilyLabel(entry) << ' ' << PrettyPartName(entry.partKey);
        return stream.str();
    }

    static std::string BikeItemCatalogName(const BikeItemCatalogEntry& entry) {
        std::string name;
        if (GearCustomization::GetHiddenObjectName(entry.id, &name) && !name.empty()) {
            return name;
        }

        name = entry.genKey;
        std::replace(name.begin(), name.end(), '_', ' ');
        return name;
    }

    static bool IsBikeItemAliasBikeToken(const std::string& token) {
        return token == "CHEETAH"
            || token == "KINGCOBRA"
            || token == "MINX"
            || token == "TRICKY"
            || token == "FAT"
            || token == "PAPER"
            || token == "RABBIT"
            || token == "GRIZZLY"
            || token == "PITVIPER";
    }

    static bool IsBikeItemAliasPartToken(const std::string& token) {
        return token == "BACK"
            || token == "BACKSWING"
            || token == "BODY"
            || token == "BOY"
            || token == "CHASSIS"
            || token == "ENGINE"
            || token == "EXHAUST"
            || token == "FENDER"
            || token == "FRAME"
            || token == "FRONT"
            || token == "GRIZZLY"
            || token == "HEAD"
            || token == "HEADLIGHT"
            || token == "LIGHT"
            || token == "PEDAL"
            || token == "REAR"
            || token == "RIM"
            || token == "SEAT"
            || token == "STEERING"
            || token == "SUSPENSION"
            || token == "SWING"
            || token == "TANK"
            || token == "TIRE"
            || token == "WHEEL";
    }

    static bool IsBikeItemAliasVariantToken(const std::string& token) {
        return token == "1"
            || token == "2"
            || token == "4"
            || token == "8"
            || token == "16"
            || token == "32"
            || token == "64"
            || token == "128";
    }

    static std::string TitleCaseAliasName(const std::string& alias) {
        std::string result;
        bool capitalizeNext = true;
        for (char ch : alias) {
            if (ch == ' ' || ch == '-' || ch == '_') {
                result.push_back(ch == '_' ? ' ' : ch);
                capitalizeNext = true;
                continue;
            }

            const unsigned char unsignedCh = static_cast<unsigned char>(ch);
            const char lower = static_cast<char>(tolower(unsignedCh));
            result.push_back(capitalizeNext ? static_cast<char>(toupper(static_cast<unsigned char>(lower))) : lower);
            capitalizeNext = false;
        }
        return result;
    }

    static std::string BikeItemAssociatedAliasName(const BikeItemCatalogEntry& entry) {
        switch (entry.id) {
        case 105:
        case 128:
            return "Standard Issue";
        case 106:
        case 129:
            return "Shuriken";
        case 107:
        case 130:
            return "Blade";
        case 108:
        case 131:
            return "Renaissance";
        case 109:
        case 132:
            return "Grind";
        case 110:
        case 133:
            return "Chroma";
        case 111:
        case 134:
            return "Fusion";
        case 795:
        case 796:
            return "Ghost Shell";
        default:
            break;
        }

        std::vector<std::string> aliasTokens;
        std::istringstream stream(entry.genKey);
        std::string token;
        while (std::getline(stream, token, '_')) {
            if (token.empty()
                || IsBikeItemAliasBikeToken(token)
                || IsBikeItemAliasPartToken(token)
                || IsBikeItemAliasVariantToken(token)) {
                continue;
            }

            aliasTokens.push_back(token);
        }

        if (aliasTokens.empty()) {
            return "";
        }

        std::string aliasKey;
        for (size_t i = 0; i < aliasTokens.size(); ++i) {
            if (i > 0) {
                aliasKey += '_';
            }
            aliasKey += aliasTokens[i];
        }

        const char* mappedAlias = GearSetFamilyAlias(aliasKey);
        return TitleCaseAliasName(mappedAlias ? mappedAlias : aliasKey);
    }

    static std::string BikeItemDisplayName(const BikeItemCatalogEntry& entry) {
        const std::string associatedAlias = BikeItemAssociatedAliasName(entry);
        if (!associatedAlias.empty()) {
            return associatedAlias;
        }

        std::ostringstream stream;
        stream << BikeItemCatalogName(entry) << " (" << BikeItemFamilyLabel(entry)
            << " " << PrettyPartName(entry.partKey) << ")";
        return stream.str();
    }

    static std::string BikeItemVariationTooltip(const BikeItemCatalogEntry& entry) {
        std::string tooltip = BikeItemAssociatedAliasName(entry);
        if (tooltip.empty()) {
            tooltip = BikeItemFamilyLabel(entry);
        }

#ifdef DEVELOPMENT_MODE
        std::ostringstream stream;
        stream << tooltip << "\nItem ID: " << entry.id;
        return stream.str();
#else
        return tooltip;
#endif
    }

    static const char* BikePartCompatibilityKey(const std::string& partKey) {
        if (partKey == "FRONT_TIRE" || partKey == "TIRE_FRONT" || partKey == "WHEEL_FRONT") return "FRONT_WHEEL";
        if (partKey == "REAR_TIRE" || partKey == "TIRE_REAR" || partKey == "WHEEL_REAR") return "REAR_WHEEL";
        if (partKey == "FRONT_RIM" || partKey == "RIM_FRONT") return "FRONT_RIM";
        if (partKey == "REAR_RIM" || partKey == "RIM_REAR") return "REAR_RIM";
        if (partKey == "FRONT_FENDER" || partKey == "FENDER_FRONT") return "FRONT_FENDER";
        if (partKey == "REAR_FENDER" || partKey == "FENDER_REAR") return "REAR_FENDER";
        if (partKey == "FRONT_SUSPENSION" || partKey == "SUSPENSION_FRONT" || partKey == "GRIZZLY_SUSPENSION_FRONT") return "FRONT_SUSPENSION";
        if (partKey == "SWING_ARM" || partKey == "BACKSWING" || partKey == "BACK_SWING_ARM" || partKey == "GRIZZLY_SUSPENSION_REAR") return "BACK_SWING_ARM";
        if (partKey == "HEAD_LIGHT" || partKey == "HEADLIGHT" || partKey == "GRIZZLY_HEADLIGHT") return "HEADLIGHT";
        if (partKey == "EXHAUST" || partKey == "EXHAUST_PIPE" || partKey == "GRIZZLY_EXHAUST") return "EXHAUST";
        if (partKey == "ENGINE_FENDER" || partKey == "GRIZZLY_BODY") return "ENGINE_FENDER";
        if (partKey == "FRAME" || partKey == "BOY_FRAME" || partKey == "GRIZZLY_CHASSIS") return "FRAME";
        if (partKey == "STEERING" || partKey == "BOY_STEERING" || partKey == "GRIZZLY_STEERING") return "STEERING";
        if (partKey == "SEAT") return "SEAT";
        if (partKey == "TANK") return "TANK";
        if (partKey == "ENGINE") return "ENGINE";
        return partKey.c_str();
    }

    static bool AreBikePartsCompatible(const std::string& targetPartKey, const std::string& candidatePartKey) {
        return std::strcmp(BikePartCompatibilityKey(targetPartKey), BikePartCompatibilityKey(candidatePartKey)) == 0;
    }

    static int BikePartSortRank(const std::string& partKey) {
        const std::string key = BikePartCompatibilityKey(partKey);
        if (key == "FRONT_RIM") return 0;
        if (key == "REAR_RIM") return 1;
        if (key == "FRONT_WHEEL") return 2;
        if (key == "REAR_WHEEL") return 3;
        if (key == "FRONT_FENDER") return 4;
        if (key == "ENGINE_FENDER") return 5;
        if (key == "REAR_FENDER") return 6;
        if (key == "BACK_SWING_ARM") return 7;
        return 100;
    }

    static void RenderGearEditorColumnHeader() {
        ImGui::TextDisabled("Gear");
        ImGui::SameLine(kGearEditorControlColumnX);
        ImGui::TextDisabled("Type");
        ImGui::SameLine(kGearEditorColorColumnX);
        ImGui::TextDisabled("Color");
        ImGui::SameLine(kGearEditorVisibilityColumnX);
        ImGui::TextDisabled("Visibility");
        ImGui::SameLine(kGearEditorVariantColumnX);
        ImGui::TextDisabled("Variation");
    }

    static void RenderGearEditorSectionHeader(const char* label) {
        ImGui::SetWindowFontScale(1.08f);
        ImGui::TextDisabled("%s", label);
        ImGui::SetWindowFontScale(1.0f);
    }

    static bool RenderVisibilityButton(const char* id, bool hidden) {
        if (hidden) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.42f, 0.16f, 0.16f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.52f, 0.20f, 0.20f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.62f, 0.24f, 0.24f, 1.0f));
        }
        else {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        }

        const std::string label = std::string("X##") + id;
        const bool clicked = ImGui::Button(label.c_str(), ImVec2(kGearEditorVisibilityButtonSize, 0.0f));
        if (hidden) {
            ImGui::PopStyleColor(3);
        }
        else {
            ImGui::PopStyleColor();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Hide this bike subpart");
        }
        return clicked;
    }

    struct BikePartGroupOption {
        const BikeItemCatalogEntry* entry;
        int variant;
        size_t candidateIndex;
    };

    struct BikePartGroup {
        std::string label;
        std::vector<BikePartGroupOption> options;
    };

    static void SortBikePartGroupOptionsByItemId(BikePartGroup& group) {
        std::stable_sort(group.options.begin(), group.options.end(), [](const BikePartGroupOption& left, const BikePartGroupOption& right) {
            return left.entry->id < right.entry->id;
        });
    }

    static bool TryBuildExactPitViperFrontRimGroup(
        const std::vector<uint16_t>& candidateItemIds,
        const std::string& targetBikeKey,
        const std::string& targetPartKey,
        std::vector<BikePartGroup>& outGroups) {
        if (targetBikeKey != "CHEETAH" || !IsFrontRimPart(targetPartKey)) {
            return false;
        }

        static const uint16_t kPitViperFrontRimVariations[] = {
            128,
            129,
            130,
            131,
            132,
            133,
            134,
            795,
            909,
            918,
            787,
            851,
            847,
            804,
            801,
            798
        };

        BikePartGroup group = {};
        group.label = "Pit-Viper";
        for (uint16_t itemId : kPitViperFrontRimVariations) {
            const auto candidateIt = std::find(candidateItemIds.begin(), candidateItemIds.end(), itemId);
            if (candidateIt == candidateItemIds.end()) {
                continue;
            }

            const BikeItemCatalogEntry* entry = FindBikeCatalogEntry(itemId);
            if (!entry) {
                continue;
            }

            group.options.push_back({
                entry,
                BikeItemVariantIndex(*entry),
                static_cast<size_t>(candidateIt - candidateItemIds.begin())
            });
        }

        if (!group.options.empty()) {
            SortBikePartGroupOptionsByItemId(group);
            outGroups.push_back(group);
        }
        return true;
    }

    static std::vector<BikePartGroup> BuildBikePartGroups(
        const std::vector<uint16_t>& candidateItemIds,
        const std::string& targetBikeKey,
        const std::string& targetPartKey,
        uint16_t targetItemId) {
        std::vector<BikePartGroup> groups;
        if (TryBuildExactPitViperFrontRimGroup(candidateItemIds, targetBikeKey, targetPartKey, groups)) {
            return groups;
        }

        for (size_t i = 0; i < candidateItemIds.size(); ++i) {
            const BikeItemCatalogEntry* entry = FindBikeCatalogEntry(candidateItemIds[i]);
            if (!entry) {
                continue;
            }

            if (!SpecialRimEntryMatchesTarget(*entry, targetBikeKey, targetPartKey)) {
                continue;
            }

            const std::string groupLabel = BikePartGroupLabelForEntry(*entry, targetBikeKey);
            auto groupIt = std::find_if(groups.begin(), groups.end(), [&groupLabel](const BikePartGroup& group) {
                return group.label == groupLabel;
            });
            if (groupIt == groups.end()) {
                groups.push_back({ groupLabel, {} });
                groupIt = groups.end() - 1;
            }

            groupIt->options.push_back({ entry, BikeItemVariantIndex(*entry), i });
        }

        for (BikePartGroup& group : groups) {
            SortBikePartGroupOptionsByItemId(group);
        }

        return groups;
    }

    static bool RenderGearSetCombo(
        const char* label,
        GearSetSlot slot,
        int* setId,
        float* color = nullptr,
        bool* outColorChanged = nullptr,
        bool* outColorEditFinished = nullptr) {
        bool changed = false;
        const std::vector<GearSetGroup> groups = BuildGearSetGroups(slot);
        const GearSetGroup* selectedGroup = nullptr;
        const GearSetCatalogEntry* selectedEntry = nullptr;
        int selectedOptionIndex = -1;

        for (const GearSetGroup& group : groups) {
            for (size_t i = 0; i < group.options.size(); ++i) {
                if (*setId == group.options[i].entry->id) {
                    selectedGroup = &group;
                    selectedEntry = group.options[i].entry;
                    selectedOptionIndex = static_cast<int>(i);
                    break;
                }
            }
            if (selectedGroup) {
                break;
            }
        }

        const std::string preview = selectedGroup ? selectedGroup->label : GearSetPreview(*setId);

        const std::string labelText(label);
        const size_t idSeparator = labelText.find("##");
        const std::string visibleLabel = idSeparator == std::string::npos ? labelText : labelText.substr(0, idSeparator);
        const std::string idSuffix = idSeparator == std::string::npos ? labelText : labelText.substr(idSeparator + 2);
        const std::string comboId = "##GearGroup" + idSuffix;

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(visibleLabel.c_str());
        ImGui::SameLine(kGearEditorControlColumnX);

        ImGui::PushItemWidth(kGearEditorControlWidth);
        if (ImGui::BeginCombo(comboId.c_str(), preview.c_str())) {
            for (const GearSetGroup& group : groups) {
                const bool selected = selectedGroup && group.label == selectedGroup->label;
                if (ImGui::Selectable(group.label.c_str(), selected)) {
                    *setId = group.options.empty() ? *setId : group.options[0].entry->id;
                    changed = true;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::PopItemWidth();

        if (color) {
            ImGui::SameLine(kGearEditorColorColumnX);
            if (selectedEntry && selectedEntry->colorPartCount > 0) {
                const std::string colorId = "##GearColor" + idSuffix;
                const bool colorChanged = ImGui::ColorEdit3(
                    colorId.c_str(),
                    color,
                    ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
                if (outColorChanged && colorChanged) {
                    *outColorChanged = true;
                }
                if (outColorEditFinished && ImGui::IsItemDeactivatedAfterEdit()) {
                    *outColorEditFinished = true;
                }
            }
            else {
                ImGui::Dummy(ImVec2(kGearEditorColorSwatchSize, ImGui::GetFrameHeight()));
            }
        }

        if (selectedGroup) {
            ImGui::SameLine(color ? kGearEditorVariantColumnX : kGearEditorColorColumnX);
            for (size_t i = 0; i < selectedGroup->options.size(); ++i) {
                if (i > 0) {
                    ImGui::SameLine();
                }
                const GearSetGroupOption& option = selectedGroup->options[i];
                const bool selected = selectedOptionIndex == static_cast<int>(i);
                if (selected) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                }

                std::string buttonLabel = std::to_string(i + 1) + "##" + std::string(label) + std::to_string(option.entry->id);
                if (ImGui::Button(buttonLabel.c_str(), ImVec2(24.0f, 0.0f))) {
                    *setId = option.entry->id;
                    changed = true;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", GearSetDisplayName(*option.entry).c_str());
                }

                if (selected) {
                    ImGui::PopStyleColor();
                }
            }
        }

        return changed;
    }

    static void DecodeRgb24(uint32_t rgb, float outColor[3]) {
        outColor[0] = static_cast<float>((rgb >> 16) & 0xff) / 255.0f;
        outColor[1] = static_cast<float>((rgb >> 8) & 0xff) / 255.0f;
        outColor[2] = static_cast<float>(rgb & 0xff) / 255.0f;
    }

    static uint32_t EncodeRgb24(const float color[3]) {
        const uint32_t red = static_cast<uint32_t>(ClampFloat(color[0], 0.0f, 1.0f) * 255.0f + 0.5f);
        const uint32_t green = static_cast<uint32_t>(ClampFloat(color[1], 0.0f, 1.0f) * 255.0f + 0.5f);
        const uint32_t blue = static_cast<uint32_t>(ClampFloat(color[2], 0.0f, 1.0f) * 255.0f + 0.5f);
        return (red << 16) | (green << 8) | blue;
    }

    static uint32_t ReadAppearanceColor(const uint16_t appearance[16], int wordIndex) {
        uint32_t color = 0;
        memcpy(&color, appearance + wordIndex, sizeof(color));
        return color;
    }

    static void WriteAppearanceColor(uint16_t appearance[16], int wordIndex, uint32_t color) {
        memcpy(appearance + wordIndex, &color, sizeof(color));
    }

    static std::string GetGearPresetConfigPath() {
        char path[MAX_PATH] = {};
        HMODULE hModule = NULL;
        GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(&GetGearPresetConfigPath),
            &hModule);

        if (hModule && GetModuleFileNameA(hModule, path, MAX_PATH) > 0) {
            std::string fullPath(path);
            const size_t lastSlash = fullPath.find_last_of("\\/");
            if (lastSlash != std::string::npos) {
                return fullPath.substr(0, lastSlash + 1) + "tfpayload_gear_presets.cfg";
            }
        }

        return "tfpayload_gear_presets.cfg";
    }

    static void AddUniqueItemId(std::vector<uint16_t>* itemIds, uint16_t itemId) {
        if (!itemIds || itemId == 0) {
            return;
        }

        if (std::find(itemIds->begin(), itemIds->end(), itemId) == itemIds->end()) {
            itemIds->push_back(itemId);
        }
    }

    static bool AddSupplementalBikeGearSetChildren(uint16_t setId, std::vector<uint16_t>* children) {
        if (!children) {
            return false;
        }

        const uint16_t* supplementalChildren = nullptr;
        size_t supplementalChildCount = 0;

        switch (setId) {
        case 55: { static const uint16_t ids[] = { 61, 62, 92, 99 }; supplementalChildren = ids; supplementalChildCount = _countof(ids); break; }
        case 56: { static const uint16_t ids[] = { 63, 93, 61, 99 }; supplementalChildren = ids; supplementalChildCount = _countof(ids); break; }
        case 57: { static const uint16_t ids[] = { 64, 94, 61, 99 }; supplementalChildren = ids; supplementalChildCount = _countof(ids); break; }
        case 58: { static const uint16_t ids[] = { 65, 95, 61, 99 }; supplementalChildren = ids; supplementalChildCount = _countof(ids); break; }
        case 59: { static const uint16_t ids[] = { 66, 96, 61, 99 }; supplementalChildren = ids; supplementalChildCount = _countof(ids); break; }
        case 60: { static const uint16_t ids[] = { 67, 97, 61, 99 }; supplementalChildren = ids; supplementalChildCount = _countof(ids); break; }
        case 61: { static const uint16_t ids[] = { 68, 98, 61, 99 }; supplementalChildren = ids; supplementalChildCount = _countof(ids); break; }
        case 62: { static const uint16_t ids[] = { 101, 105, 112, 128 }; supplementalChildren = ids; supplementalChildCount = _countof(ids); break; }
        case 66: { static const uint16_t ids[] = { 106, 129, 101, 112 }; supplementalChildren = ids; supplementalChildCount = _countof(ids); break; }
        case 67: { static const uint16_t ids[] = { 107, 130, 101, 112 }; supplementalChildren = ids; supplementalChildCount = _countof(ids); break; }
        case 68: { static const uint16_t ids[] = { 108, 131, 101, 112 }; supplementalChildren = ids; supplementalChildCount = _countof(ids); break; }
        case 69: { static const uint16_t ids[] = { 109, 132, 101, 112 }; supplementalChildren = ids; supplementalChildCount = _countof(ids); break; }
        case 70: { static const uint16_t ids[] = { 110, 133, 101, 112 }; supplementalChildren = ids; supplementalChildCount = _countof(ids); break; }
        case 71: { static const uint16_t ids[] = { 111, 134, 101, 112 }; supplementalChildren = ids; supplementalChildCount = _countof(ids); break; }
        case 75: { static const uint16_t ids[] = { 160, 166, 174, 178 }; supplementalChildren = ids; supplementalChildCount = _countof(ids); break; }
        case 76: { static const uint16_t ids[] = { 167, 179, 160, 174 }; supplementalChildren = ids; supplementalChildCount = _countof(ids); break; }
        case 77: { static const uint16_t ids[] = { 168, 180, 160, 174 }; supplementalChildren = ids; supplementalChildCount = _countof(ids); break; }
        case 78: { static const uint16_t ids[] = { 169, 181, 160, 174 }; supplementalChildren = ids; supplementalChildCount = _countof(ids); break; }
        case 79: { static const uint16_t ids[] = { 170, 182, 160, 174 }; supplementalChildren = ids; supplementalChildCount = _countof(ids); break; }
        case 80: { static const uint16_t ids[] = { 171, 183, 160, 174 }; supplementalChildren = ids; supplementalChildCount = _countof(ids); break; }
        case 81: { static const uint16_t ids[] = { 172, 184, 160, 174 }; supplementalChildren = ids; supplementalChildCount = _countof(ids); break; }
        case 83: { static const uint16_t ids[] = { 193, 194, 195, 196 }; supplementalChildren = ids; supplementalChildCount = _countof(ids); break; }
        case 85: { static const uint16_t ids[] = { 203, 204 }; supplementalChildren = ids; supplementalChildCount = _countof(ids); break; }
        case 90: { static const uint16_t ids[] = { 210, 215, 234, 244 }; supplementalChildren = ids; supplementalChildCount = _countof(ids); break; }
        case 106: { static const uint16_t ids[] = { 210, 215, 292, 296 }; supplementalChildren = ids; supplementalChildCount = _countof(ids); break; }
        case 107: { static const uint16_t ids[] = { 210, 215, 299, 305 }; supplementalChildren = ids; supplementalChildCount = _countof(ids); break; }
        case 108: { static const uint16_t ids[] = { 210, 215, 300, 306 }; supplementalChildren = ids; supplementalChildCount = _countof(ids); break; }
        case 109: { static const uint16_t ids[] = { 210, 215, 301, 307 }; supplementalChildren = ids; supplementalChildCount = _countof(ids); break; }
        case 110: { static const uint16_t ids[] = { 210, 215, 302, 308 }; supplementalChildren = ids; supplementalChildCount = _countof(ids); break; }
        case 111: { static const uint16_t ids[] = { 210, 215, 303, 309 }; supplementalChildren = ids; supplementalChildCount = _countof(ids); break; }
        case 774: { static const uint16_t ids[] = { 280, 113, 298, 277, 278, 135, 276, 275, 279, 281, 282, 283 }; supplementalChildren = ids; supplementalChildCount = _countof(ids); break; }
        case 966: { static const uint16_t ids[] = { 1013, 1014, 1015, 1016, 1017, 1018, 1019, 1020, 1021, 1022, 1023, 1024 }; supplementalChildren = ids; supplementalChildCount = _countof(ids); break; }
        case 967: { static const uint16_t ids[] = { 890, 891, 892, 893, 894, 895, 896, 897, 898, 899, 900 }; supplementalChildren = ids; supplementalChildCount = _countof(ids); break; }
        case 973: { static const uint16_t ids[] = { 866, 867, 868, 869, 870, 871, 872, 873, 874, 875, 122 }; supplementalChildren = ids; supplementalChildCount = _countof(ids); break; }
        case 1008: { static const uint16_t ids[] = { 807, 808, 809, 810, 811, 812, 813, 814, 815, 816, 122 }; supplementalChildren = ids; supplementalChildCount = _countof(ids); break; }
        default:
            return false;
        }

        for (size_t i = 0; i < supplementalChildCount; ++i) {
            AddUniqueItemId(children, supplementalChildren[i]);
        }
        return supplementalChildCount > 0;
    }

    static bool ParseBoolInt(const std::string& value) {
        return std::atoi(value.c_str()) != 0;
    }

}

TweakableAppearanceReload::TweakableAppearanceReload(int id, const std::string& name)
    : TweakableItem(id, name, TweakableType::Custom)
    , m_pitViperTireColor(10071, "Pit-Viper Tire Color")
    , m_appearance{}
    , m_defaultAppearance{}
    , m_riderGearIds{}
    , m_bikeGearIds{}
    , m_riderColors{}
    , m_bikeColors{}
    , m_hasAppearance(false)
    , m_hasCapturedDefaults(false)
    , m_persistCustomAppearance(false)
    , m_dirty(false)
    , m_wasTrackReady(false)
    , m_lastBikePointer(0)
    , m_lastCheckpointCount(0)
    , m_reapplyAttemptsRemaining(0)
    , m_subpartReapplyAttemptsRemaining(0)
    , m_nextReapplyTick(0)
    , m_nextSubpartReapplyTick(0)
    , m_nextAppearanceMismatchCheckTick(0)
    , m_gearPresets{}
    , m_selectedGearPresetSlot(0)
    , m_gearPresetsLoaded(false) {
    for (GearPreset& preset : m_gearPresets) {
        preset.hasData = false;
        memset(preset.appearance, 0, sizeof(preset.appearance));
    }
}

bool TweakableAppearanceReload::LoadLiveAppearance() {
    if (!GearCustomization::GetCurrentAppearanceData(m_appearance)) {
        m_hasAppearance = false;
        return false;
    }

    if (!m_hasCapturedDefaults) {
        memcpy(m_defaultAppearance, m_appearance, sizeof(m_defaultAppearance));
        m_hasCapturedDefaults = true;
    }

    DecodeGearFromAppearance();
    DecodeColorsFromAppearance();
    RefreshBikePartEditors();
    m_hasAppearance = true;
    m_dirty = false;
    m_lastBikePointer = reinterpret_cast<uintptr_t>(Respawn::GetBikePointer());
    m_lastCheckpointCount = Respawn::GetCheckpointCount();
    return true;
}

void TweakableAppearanceReload::DecodeColorsFromAppearance() {
    for (int i = 0; i < 3; ++i) {
        DecodeRgb24(ReadAppearanceColor(m_appearance, 4 + i * 2), m_riderColors[i]);
    }
    for (int i = 0; i < 2; ++i) {
        DecodeRgb24(ReadAppearanceColor(m_appearance, 12 + i * 2), m_bikeColors[i]);
    }
}

void TweakableAppearanceReload::DecodeGearFromAppearance() {
    for (int i = 0; i < 3; ++i) {
        m_riderGearIds[i] = m_appearance[i];
    }
    for (int i = 0; i < 2; ++i) {
        m_bikeGearIds[i] = m_appearance[10 + i];
    }
}

void TweakableAppearanceReload::EncodeColorsIntoAppearance() {
    for (int i = 0; i < 3; ++i) {
        WriteAppearanceColor(m_appearance, 4 + i * 2, EncodeRgb24(m_riderColors[i]));
    }
    for (int i = 0; i < 2; ++i) {
        WriteAppearanceColor(m_appearance, 12 + i * 2, EncodeRgb24(m_bikeColors[i]));
    }
}

void TweakableAppearanceReload::EncodeGearIntoAppearance() {
    for (int i = 0; i < 3; ++i) {
        int clampedGearId = m_riderGearIds[i];
        if (clampedGearId < 0) {
            clampedGearId = 0;
        }
        else if (clampedGearId > 0xffff) {
            clampedGearId = 0xffff;
        }
        m_riderGearIds[i] = clampedGearId;
        m_appearance[i] = static_cast<uint16_t>(clampedGearId);
    }

    for (int i = 0; i < 2; ++i) {
        int clampedGearId = m_bikeGearIds[i];
        if (clampedGearId < 0) {
            clampedGearId = 0;
        }
        else if (clampedGearId > 0xffff) {
            clampedGearId = 0xffff;
        }
        m_bikeGearIds[i] = clampedGearId;
        m_appearance[10 + i] = static_cast<uint16_t>(clampedGearId);
    }
}

void TweakableAppearanceReload::RefreshBikePartEditors() {
    std::map<uint16_t, uint16_t> previousSources;
    std::map<uint16_t, bool> previousHidden;
    struct PreviousColorState {
        uint32_t colors[3];
        bool hasOverrides[3];
    };
    struct PreviousPartState {
        uint16_t sourceItemId;
        PreviousColorState colors;
        bool hidden;
    };
    std::map<uint16_t, PreviousColorState> previousColors;
    std::map<std::string, PreviousPartState> previousPartsByCompatibilityKey;
    for (const BikePartEditorState& state : m_bikePartEditors) {
        previousSources[state.targetItemId] = state.currentSourceItemId;
        previousHidden[state.targetItemId] = state.hidden;
        PreviousColorState colorState = {};
        for (int i = 0; i < 3; ++i) {
            colorState.colors[i] = EncodeRgb24(state.colors[i]);
            colorState.hasOverrides[i] = state.hasColorOverrides[i];
        }
        previousColors[state.targetItemId] = colorState;
        previousPartsByCompatibilityKey[std::string(BikePartCompatibilityKey(state.partKey))] = {
            state.currentSourceItemId,
            colorState,
            state.hidden
        };
    }

    m_bikePartEditors.clear();

    struct ActiveBikeChild {
        uint16_t itemId;
        int colorSlot;
    };
    std::vector<ActiveBikeChild> activeChildren;
    auto addActiveBikeChild = [&activeChildren](uint16_t itemId, int colorSlot) {
        if (itemId == 0) {
            return;
        }

        const auto existing = std::find_if(activeChildren.begin(), activeChildren.end(), [itemId](const ActiveBikeChild& child) {
            return child.itemId == itemId;
        });
        if (existing == activeChildren.end()) {
            activeChildren.push_back({ itemId, colorSlot });
        }
    };
    std::string currentBikeKey;
    bool hasPitViperChild = false;
    for (int i = 0; i < 2; ++i) {
        if (currentBikeKey.empty()) {
            currentBikeKey = BikeKeyFromGearSetId(m_bikeGearIds[i]);
        }

        std::vector<uint16_t> children;
        if (m_bikeGearIds[i] > 0) {
            const uint16_t gearSetId = static_cast<uint16_t>(m_bikeGearIds[i]);
            const bool readChildren = GearCustomization::GetBikeGearSetChildren(gearSetId, &children);
            const bool addedSupplementalChildren = AddSupplementalBikeGearSetChildren(gearSetId, &children);
            if (!readChildren && !addedSupplementalChildren) {
                continue;
            }

            for (uint16_t child : children) {
                addActiveBikeChild(child, i);
                const BikeItemCatalogEntry* childEntry = FindBikeCatalogEntry(child);
                if (childEntry && std::string(childEntry->bikeKey) == "CHEETAH") {
                    hasPitViperChild = true;
                }
            }
        }
    }

    if (hasPitViperChild) {
        const uint16_t pitViperEngineItemId = 147;
        const bool alreadyHasPitViperEngine = std::any_of(activeChildren.begin(), activeChildren.end(), [](const ActiveBikeChild& child) {
            const BikeItemCatalogEntry* childEntry = FindBikeCatalogEntry(child.itemId);
            return childEntry
                && std::string(childEntry->bikeKey) == "CHEETAH"
            && std::string(childEntry->partKey) == "ENGINE";
        });
        if (!alreadyHasPitViperEngine) {
            addActiveBikeChild(pitViperEngineItemId, 1);
        }
    }

    auto addFaultOneZeroDuplicateCandidate = [](BikePartEditorState& state) {
        if (state.bikeKey != "CHEETAH") {
            return;
        }

        static const uint16_t kFaultOneZeroBodyKitChildren[] = {
            280, 113, 298, 277, 278, 135, 276, 275, 279, 281, 282, 283
        };
        for (uint16_t itemId : kFaultOneZeroBodyKitChildren) {
            const BikeItemCatalogEntry* candidate = FindBikeCatalogEntry(itemId);
            if (candidate && AreBikePartsCompatible(state.partKey, candidate->partKey)) {
                AddUniqueItemId(&state.candidateItemIds, itemId);
                return;
            }
        }
    };

    for (const ActiveBikeChild& child : activeChildren) {
        const uint16_t targetItemId = child.itemId;
        const BikeItemCatalogEntry* targetEntry = FindBikeCatalogEntry(targetItemId);
        if (!targetEntry) {
            continue;
        }

        BikePartEditorState state = {};
        state.targetItemId = targetItemId;
        state.currentSourceItemId = targetItemId;
        state.bikeKey = targetEntry->bikeKey;
        state.partKey = targetEntry->partKey;
        if (IsRimPart(state.partKey) && IsSpecialRimFamily(state.bikeKey) && !currentBikeKey.empty()) {
            state.bikeKey = currentBikeKey;
        }
        state.selectedIndex = 0;
        state.hidden = false;
        // Bike appearance slots 12..15 are resolved variant IDs, not RGB colors.
        // Leave per-part color neutral until the real bike color source is mapped.
        for (int colorIndex = 0; colorIndex < 3; ++colorIndex) {
            state.colors[colorIndex][0] = 1.0f;
            state.colors[colorIndex][1] = 1.0f;
            state.colors[colorIndex][2] = 1.0f;
            state.hasColorOverrides[colorIndex] = false;
        }
        GearCustomization::GetHiddenObjectName(targetItemId, &state.nodeName);
        if (state.nodeName.empty()) {
            state.nodeName = state.partKey;
        }

        const std::string compatibilityKey = BikePartCompatibilityKey(state.partKey);
        const auto previousCompatiblePart = previousPartsByCompatibilityKey.find(compatibilityKey);
        const auto previous = previousSources.find(targetItemId);
        if (previous != previousSources.end()) {
            state.currentSourceItemId = previous->second;
        }
        else if (previousCompatiblePart != previousPartsByCompatibilityKey.end()) {
            state.currentSourceItemId = previousCompatiblePart->second.sourceItemId;
        }

        const auto previousHiddenIt = previousHidden.find(targetItemId);
        if (previousHiddenIt != previousHidden.end()) {
            state.hidden = previousHiddenIt->second;
        }
        else if (previousCompatiblePart != previousPartsByCompatibilityKey.end()) {
            state.hidden = previousCompatiblePart->second.hidden;
        }

        const auto previousColor = previousColors.find(targetItemId);
        if (previousColor != previousColors.end()) {
            for (int colorIndex = 0; colorIndex < 3; ++colorIndex) {
                DecodeRgb24(previousColor->second.colors[colorIndex], state.colors[colorIndex]);
                state.hasColorOverrides[colorIndex] = previousColor->second.hasOverrides[colorIndex];
            }
        }
        else if (previousCompatiblePart != previousPartsByCompatibilityKey.end()) {
            for (int colorIndex = 0; colorIndex < 3; ++colorIndex) {
                DecodeRgb24(previousCompatiblePart->second.colors.colors[colorIndex], state.colors[colorIndex]);
                state.hasColorOverrides[colorIndex] = previousCompatiblePart->second.colors.hasOverrides[colorIndex];
            }
        }

        for (size_t i = 0; i < kBikeItemCatalogCount; ++i) {
            const BikeItemCatalogEntry& candidate = kBikeItemCatalog[i];
            if (AreBikePartsCompatible(state.partKey, candidate.partKey)) {
                state.candidateItemIds.push_back(candidate.id);
                if (candidate.id == state.currentSourceItemId) {
                    state.selectedIndex = static_cast<int>(state.candidateItemIds.size()) - 1;
                }
            }
        }
        addFaultOneZeroDuplicateCandidate(state);

        if (!state.candidateItemIds.empty()) {
            m_bikePartEditors.push_back(state);
        }
    }

    std::stable_sort(m_bikePartEditors.begin(), m_bikePartEditors.end(), [](const BikePartEditorState& left, const BikePartEditorState& right) {
        const int leftRank = BikePartSortRank(left.partKey);
        const int rightRank = BikePartSortRank(right.partKey);
        if (leftRank != rightRank) {
            return leftRank < rightRank;
        }
        return PrettyPartName(left.partKey) < PrettyPartName(right.partKey);
    });
}

void TweakableAppearanceReload::ScheduleCustomAppearanceReapply(const char* reason) {
    m_reapplyAttemptsRemaining = 24;
    m_subpartReapplyAttemptsRemaining = 0;
    m_nextReapplyTick = 0;
    m_nextSubpartReapplyTick = 0;

    if (reason) {
        LOG_INFO("[DevMenu] Scheduling custom appearance reapply: " << reason);
    }
}

bool TweakableAppearanceReload::QueueStoredSubpartReapply() {
    bool queuedAny = false;

    for (const BikePartEditorState& state : m_bikePartEditors) {
        if (state.currentSourceItemId == 0) {
            continue;
        }

        uint32_t color = 0x00ffffff;
        bool hasColorOverride = false;
        for (int colorIndex = 0; colorIndex < kBikePartMaxColorParts; ++colorIndex) {
            if (state.hasColorOverrides[colorIndex]) {
                color = EncodeRgb24(state.colors[colorIndex]);
                hasColorOverride = true;
                break;
            }
        }

        if (hasColorOverride) {
            GearCustomization::SetBikeChildColorOverride(state.targetItemId, color);
        }

        if (state.hidden) {
            const uint16_t extraHideItemId = state.currentSourceItemId != state.targetItemId
                ? state.currentSourceItemId
                : 0;
            if (GearCustomization::QueueBikeChildItemOverride(state.targetItemId, 0, color, extraHideItemId)) {
                queuedAny = true;
            }
            continue;
        }

        if (state.currentSourceItemId == state.targetItemId) {
            if (hasColorOverride && GearCustomization::QueueBikeChildItemOverride(0, state.targetItemId, color)) {
                queuedAny = true;
            }
            continue;
        }

        bool payloadPatchQueued = false;
        const bool payloadPatchAvailable = GearCustomization::CopyHiddenObjectVisualPayload(
                state.targetItemId,
                state.currentSourceItemId,
                &payloadPatchQueued);
        if (payloadPatchAvailable && payloadPatchQueued) {
            if (GearCustomization::QueueBikeChildItemOverride(0, state.targetItemId, color)) {
                queuedAny = true;
            }
        }
        else if (payloadPatchAvailable && GearCustomization::QueueBikeChildItemOverride(0, state.currentSourceItemId, color)) {
            LOG_INFO("[DevMenu] Requeued custom subpart source item without payload copy "
                << state.currentSourceItemId << " for target " << state.targetItemId);
            queuedAny = true;
        }
    }

    return queuedAny;
}

std::string TweakableAppearanceReload::GearPresetPartCompatibilityKey(const GearPresetBikePart& part) const {
    if (!part.compatibilityKey.empty()) {
        return part.compatibilityKey;
    }

    if (!part.partKey.empty()) {
        return BikePartCompatibilityKey(part.partKey);
    }

    const BikeItemCatalogEntry* targetEntry = FindBikeCatalogEntry(part.targetItemId);
    if (targetEntry) {
        return BikePartCompatibilityKey(targetEntry->partKey);
    }

    const BikeItemCatalogEntry* sourceEntry = FindBikeCatalogEntry(part.currentSourceItemId);
    if (sourceEntry) {
        return BikePartCompatibilityKey(sourceEntry->partKey);
    }

    return "";
}

void TweakableAppearanceReload::RenderGearPresets() {
    if (!m_gearPresetsLoaded) {
        LoadGearPresetsFromFile();
        m_gearPresetsLoaded = true;
    }

    RenderGearEditorSectionHeader("Presets");
    for (int i = 0; i < 5; ++i) {
        if (i > 0) {
            ImGui::SameLine();
        }

        ImGui::PushID(100900 + i);
        const std::string label = std::string("Slot ") + std::to_string(i + 1);
        int pushedColors = 0;
        if (m_gearPresets[i].hasData) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            ++pushedColors;
        }
        if (i == m_selectedGearPresetSlot) {
            ImGui::PushStyleColor(ImGuiCol_Border, ImGui::GetStyleColorVec4(ImGuiCol_Text));
            ++pushedColors;
        }
        if (ImGui::Button(label.c_str(), ImVec2(62.0f, 0.0f))) {
            m_selectedGearPresetSlot = i;
            if (m_gearPresets[i].hasData && !LoadGearPreset(i)) {
                LOG_WARNING("[DevMenu] Failed to load gear preset slot " << (i + 1));
            }
        }
        if (pushedColors > 0) {
            ImGui::PopStyleColor(pushedColors);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", m_gearPresets[i].hasData ? "Select and load preset" : "Select empty preset slot");
        }
        ImGui::PopID();
    }

    const std::string saveLabel =
        std::string("Save to Slot ") + std::to_string(m_selectedGearPresetSlot + 1) + "##GearPresetSaveSelected";
    if (ImGui::Button(saveLabel.c_str(), ImVec2(160.0f, 0.0f))) {
        SaveGearPreset(m_selectedGearPresetSlot);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Save current gear to selected slot");
    }
}

void TweakableAppearanceReload::SaveGearPreset(int slotIndex) {
    if (slotIndex < 0 || slotIndex >= 5 || !m_hasAppearance) {
        return;
    }

    EncodeGearIntoAppearance();
    EncodeColorsIntoAppearance();

    GearPreset& preset = m_gearPresets[slotIndex];
    preset.hasData = true;
    memcpy(preset.appearance, m_appearance, sizeof(preset.appearance));
    preset.bikeParts.clear();
    for (const BikePartEditorState& state : m_bikePartEditors) {
        GearPresetBikePart part = {};
        part.targetItemId = state.targetItemId;
        part.currentSourceItemId = state.currentSourceItemId;
        part.partKey = state.partKey;
        part.compatibilityKey = BikePartCompatibilityKey(state.partKey);
        part.hidden = state.hidden;
        for (int colorIndex = 0; colorIndex < kBikePartMaxColorParts; ++colorIndex) {
            part.colors[colorIndex] = EncodeRgb24(state.colors[colorIndex]);
            part.hasColorOverrides[colorIndex] = state.hasColorOverrides[colorIndex];
        }
        preset.bikeParts.push_back(part);
    }

    if (!SaveGearPresetsToFile()) {
        LOG_WARNING("[DevMenu] Failed to save gear presets file");
    }
    LOG_INFO("[DevMenu] Saved gear preset slot " << (slotIndex + 1));
}

bool TweakableAppearanceReload::LoadGearPreset(int slotIndex) {
    if (slotIndex < 0 || slotIndex >= 5 || !m_gearPresets[slotIndex].hasData) {
        return false;
    }

    const GearPreset& preset = m_gearPresets[slotIndex];
    memcpy(m_appearance, preset.appearance, sizeof(m_appearance));
    DecodeGearFromAppearance();
    DecodeColorsFromAppearance();
    RefreshBikePartEditors();

    std::vector<bool> usedPresetParts(preset.bikeParts.size(), false);
    for (BikePartEditorState& state : m_bikePartEditors) {
        size_t presetPartIndex = preset.bikeParts.size();
        for (size_t i = 0; i < preset.bikeParts.size(); ++i) {
            if (!usedPresetParts[i] && preset.bikeParts[i].targetItemId == state.targetItemId) {
                presetPartIndex = i;
                break;
            }
        }

        if (presetPartIndex == preset.bikeParts.size()) {
            const std::string stateCompatibilityKey = BikePartCompatibilityKey(state.partKey);
            for (size_t i = 0; i < preset.bikeParts.size(); ++i) {
                if (usedPresetParts[i]) {
                    continue;
                }

                if (GearPresetPartCompatibilityKey(preset.bikeParts[i]) == stateCompatibilityKey) {
                    presetPartIndex = i;
                    break;
                }
            }
        }

        if (presetPartIndex == preset.bikeParts.size()) {
            continue;
        }

        usedPresetParts[presetPartIndex] = true;
        const GearPresetBikePart& presetPart = preset.bikeParts[presetPartIndex];
        state.currentSourceItemId = presetPart.currentSourceItemId;
        state.hidden = presetPart.hidden;
        for (int colorIndex = 0; colorIndex < kBikePartMaxColorParts; ++colorIndex) {
            DecodeRgb24(presetPart.colors[colorIndex], state.colors[colorIndex]);
            state.hasColorOverrides[colorIndex] = presetPart.hasColorOverrides[colorIndex];
        }

        state.selectedIndex = 0;
        for (size_t i = 0; i < state.candidateItemIds.size(); ++i) {
            if (state.candidateItemIds[i] == state.currentSourceItemId) {
                state.selectedIndex = static_cast<int>(i);
                break;
            }
        }
    }

    m_hasAppearance = true;
    m_dirty = true;
    if (!GearCustomization::QueueAppearanceUpdate(m_appearance)) {
        LOG_WARNING("[DevMenu] Failed to queue gear preset update");
        return false;
    }

    m_dirty = false;
    m_persistCustomAppearance = true;
    m_lastBikePointer = reinterpret_cast<uintptr_t>(Respawn::GetBikePointer());
    m_lastCheckpointCount = Respawn::GetCheckpointCount();
    m_subpartReapplyAttemptsRemaining = 12;
    m_nextSubpartReapplyTick = GetTickCount() + 700;
    QueueStoredSubpartReapply();
    LOG_INFO("[DevMenu] Loaded gear preset slot " << (slotIndex + 1));
    return true;
}

bool TweakableAppearanceReload::SaveGearPresetsToFile() const {
    const std::string configPath = GetGearPresetConfigPath();
    std::ofstream file(configPath, std::ios::out | std::ios::trunc);
    if (!file.is_open()) {
        LOG_ERROR("[DevMenu] Failed to open gear preset config for writing: " << configPath);
        return false;
    }

    file << "# TFPayload gear preset slots" << std::endl;
    file << "# Format is internal and may change." << std::endl;
    for (int slot = 0; slot < 5; ++slot) {
        const GearPreset& preset = m_gearPresets[slot];
        file << "slot=" << slot << std::endl;
        file << "valid=" << (preset.hasData ? 1 : 0) << std::endl;
        file << "appearance=";
        for (int i = 0; i < 16; ++i) {
            if (i > 0) {
                file << ' ';
            }
            file << preset.appearance[i];
        }
        file << std::endl;
        file << "parts=" << preset.bikeParts.size() << std::endl;
        for (const GearPresetBikePart& part : preset.bikeParts) {
            file << "part2=" << part.targetItemId << ' '
                << part.currentSourceItemId << ' '
                << (part.hidden ? 1 : 0) << ' '
                << part.partKey << ' '
                << part.compatibilityKey;
            for (int colorIndex = 0; colorIndex < kBikePartMaxColorParts; ++colorIndex) {
                file << ' ' << part.colors[colorIndex]
                    << ' ' << (part.hasColorOverrides[colorIndex] ? 1 : 0);
            }
            file << std::endl;
        }
        file << "end" << std::endl;
    }

    return true;
}

bool TweakableAppearanceReload::LoadGearPresetsFromFile() {
    const std::string configPath = GetGearPresetConfigPath();
    std::ifstream file(configPath);
    if (!file.is_open()) {
        return false;
    }

    int currentSlot = -1;
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }

        const size_t equalsPos = line.find('=');
        if (equalsPos == std::string::npos) {
            continue;
        }

        const std::string key = line.substr(0, equalsPos);
        const std::string value = line.substr(equalsPos + 1);
        if (key == "slot") {
            currentSlot = std::atoi(value.c_str());
            if (currentSlot >= 0 && currentSlot < 5) {
                m_gearPresets[currentSlot].hasData = false;
                memset(m_gearPresets[currentSlot].appearance, 0, sizeof(m_gearPresets[currentSlot].appearance));
                m_gearPresets[currentSlot].bikeParts.clear();
            }
            continue;
        }

        if (currentSlot < 0 || currentSlot >= 5) {
            continue;
        }

        GearPreset& preset = m_gearPresets[currentSlot];
        if (key == "valid") {
            preset.hasData = ParseBoolInt(value);
        }
        else if (key == "appearance") {
            std::istringstream stream(value);
            for (int i = 0; i < 16; ++i) {
                uint32_t word = 0;
                stream >> word;
                preset.appearance[i] = static_cast<uint16_t>(word & 0xffff);
            }
        }
        else if (key == "part" || key == "part2") {
            std::istringstream stream(value);
            GearPresetBikePart part = {};
            int hidden = 0;
            stream >> part.targetItemId >> part.currentSourceItemId >> hidden;
            part.hidden = hidden != 0;
            if (key == "part2") {
                stream >> part.partKey >> part.compatibilityKey;
            }
            for (int colorIndex = 0; colorIndex < kBikePartMaxColorParts; ++colorIndex) {
                int hasOverride = 0;
                stream >> part.colors[colorIndex] >> hasOverride;
                part.hasColorOverrides[colorIndex] = hasOverride != 0;
            }
            if (part.targetItemId != 0) {
                preset.bikeParts.push_back(part);
            }
        }
    }

    LOG_INFO("[DevMenu] Loaded gear presets from " << configPath);
    return true;
}

void TweakableAppearanceReload::UpdateRuntime() {
    if (!m_hasAppearance || !m_persistCustomAppearance) {
        return;
    }

    void* bikePointer = Respawn::GetBikePointer();
    const uintptr_t bikeKey = reinterpret_cast<uintptr_t>(bikePointer);
    const int checkpointCount = Respawn::GetCheckpointCount();
    if (!bikePointer || checkpointCount <= 0) {
        m_wasTrackReady = false;
        return;
    }

    const uint32_t now = GetTickCount();
    if (!m_wasTrackReady) {
        m_wasTrackReady = true;
        m_lastBikePointer = bikeKey;
        m_lastCheckpointCount = checkpointCount;
        ScheduleCustomAppearanceReapply("entered playable track");
    }

    if (bikeKey != m_lastBikePointer || checkpointCount != m_lastCheckpointCount) {
        m_lastBikePointer = bikeKey;
        m_lastCheckpointCount = checkpointCount;
        ScheduleCustomAppearanceReapply("new bike or checkpoint layout");
    }

    if (m_reapplyAttemptsRemaining <= 0 && m_subpartReapplyAttemptsRemaining <= 0) {
        if (m_nextAppearanceMismatchCheckTick == 0
            || static_cast<int32_t>(now - m_nextAppearanceMismatchCheckTick) >= 0) {
            m_nextAppearanceMismatchCheckTick = now + 1000;

            uint16_t liveAppearance[16] = {};
            if (GearCustomization::GetCurrentAppearanceData(liveAppearance)
                && memcmp(liveAppearance, m_appearance, sizeof(m_appearance)) != 0) {
                ScheduleCustomAppearanceReapply("live appearance differs from saved mod-menu appearance");
            }
        }
    }

    if (m_reapplyAttemptsRemaining > 0
        && (m_nextReapplyTick == 0 || static_cast<int32_t>(now - m_nextReapplyTick) >= 0)) {
        --m_reapplyAttemptsRemaining;
        m_nextReapplyTick = now + 500;

        if (GearCustomization::QueueAppearanceUpdate(m_appearance)) {
            LOG_INFO("[DevMenu] Requeued custom gear sets after track load");
            m_reapplyAttemptsRemaining = 0;
            m_subpartReapplyAttemptsRemaining = 12;
            m_nextSubpartReapplyTick = now + 700;
        }
    }

    if (m_subpartReapplyAttemptsRemaining > 0
        && (m_nextSubpartReapplyTick == 0 || static_cast<int32_t>(now - m_nextSubpartReapplyTick) >= 0)) {
        --m_subpartReapplyAttemptsRemaining;
        m_nextSubpartReapplyTick = now + 500;

        if (QueueStoredSubpartReapply()) {
            LOG_INFO("[DevMenu] Requeued custom subparts after gear-set reapply");
            m_subpartReapplyAttemptsRemaining = 0;
        }
    }
}

void TweakableAppearanceReload::Render() {
    if (!m_hasAppearance && !LoadLiveAppearance()) {
        ImGui::TextDisabled("%s unavailable (load into a track first)", m_name.c_str());
        return;
    }

    bool gearChanged = false;
    bool colorsChanged = false;
    bool colorEditFinished = false;

    RenderGearPresets();
    ImGui::Spacing();

    RenderGearEditorSectionHeader("Rider Customization");
    RenderGearEditorColumnHeader();
    gearChanged |= RenderGearSetCombo(
        "Helmet##appearanceGearHelmet",
        GearSetSlot::RiderHelmet,
        &m_riderGearIds[0],
        m_riderColors[0],
        &colorsChanged,
        &colorEditFinished);
    gearChanged |= RenderGearSetCombo(
        "Torso##appearanceGearTop",
        GearSetSlot::RiderTop,
        &m_riderGearIds[1],
        m_riderColors[1],
        &colorsChanged,
        &colorEditFinished);
    gearChanged |= RenderGearSetCombo(
        "Legs##appearanceGearBottom",
        GearSetSlot::RiderBottom,
        &m_riderGearIds[2],
        m_riderColors[2],
        &colorsChanged,
        &colorEditFinished);

    ImGui::Spacing();
    RenderGearEditorSectionHeader("Bike Customization");
    m_pitViperTireColor.Render();
    RenderGearEditorColumnHeader();
    gearChanged |= RenderGearSetCombo(
        "Body kit##appearanceBikeGearBody",
        GearSetSlot::BikeFairings,
        &m_bikeGearIds[1],
        m_bikeColors[1],
        &colorsChanged,
        &colorEditFinished);
    if (gearChanged) {
        EncodeGearIntoAppearance();
        EncodeColorsIntoAppearance();
        RefreshBikePartEditors();
        m_dirty = true;
        Logging::WriteImmediate("[AppearanceApply] queueing direct gear customization update after gear change");
        if (!GearCustomization::QueueAppearanceUpdate(m_appearance)) {
            LOG_WARNING("[DevMenu] Failed to queue direct customization update after gear change");
        }
        else {
            m_dirty = false;
            m_persistCustomAppearance = true;
            m_lastBikePointer = reinterpret_cast<uintptr_t>(Respawn::GetBikePointer());
            m_lastCheckpointCount = Respawn::GetCheckpointCount();
            m_subpartReapplyAttemptsRemaining = 12;
            m_nextSubpartReapplyTick = GetTickCount() + 700;
        }
    }

    if (colorsChanged) {
        EncodeColorsIntoAppearance();
        m_dirty = true;
    }

    if (colorEditFinished) {
        EncodeGearIntoAppearance();
        EncodeColorsIntoAppearance();
        m_dirty = true;
        if (!GearCustomization::QueueAppearanceUpdate(m_appearance)) {
            LOG_WARNING("[DevMenu] Failed to queue direct customization update after edit");
        }
        else {
            m_dirty = false;
            m_persistCustomAppearance = true;
            m_lastBikePointer = reinterpret_cast<uintptr_t>(Respawn::GetBikePointer());
            m_lastCheckpointCount = Respawn::GetCheckpointCount();
        }
    }

    if (!m_bikePartEditors.empty()) {
        ImGui::Spacing();
        const ImGuiTreeNodeFlags partFlags = ImGuiTreeNodeFlags_DefaultOpen;
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        const bool bikePartsOpen = ImGui::TreeNodeEx("Bike Parts##AppearanceBikeParts", partFlags);
        ImGui::PopStyleColor();
        if (bikePartsOpen) {
            RenderGearEditorColumnHeader();
            for (BikePartEditorState& state : m_bikePartEditors) {
            if (state.candidateItemIds.empty()) {
                continue;
            }

            const BikeItemCatalogEntry* selectedEntry = FindBikeCatalogEntry(state.currentSourceItemId);
            if (!selectedEntry) {
                selectedEntry = FindBikeCatalogEntry(state.candidateItemIds[0]);
            }
            if (!selectedEntry) {
                continue;
            }

            auto selectBikePartOption = [&](const BikeItemCatalogEntry& option) -> bool {
                const uint16_t previousTargetItemId = state.targetItemId;
                uint32_t color = 0x00ffffff;
                bool hasColorOverride = false;
                if (option.colorPartCount > 0) {
                    for (int colorIndex = 0; colorIndex < kBikePartMaxColorParts; ++colorIndex) {
                        if (state.hasColorOverrides[colorIndex]) {
                            color = EncodeRgb24(state.colors[colorIndex]);
                            hasColorOverride = true;
                            break;
                        }
                    }
                }
                if (hasColorOverride) {
                    GearCustomization::SetBikeChildColorOverride(previousTargetItemId, color);
                }

                if (option.id == previousTargetItemId) {
                    if (!GearCustomization::RestoreHiddenObjectVisualPayload(previousTargetItemId)) {
                        LOG_WARNING("[DevMenu] Failed to restore bike child visual payload before default rim apply");
                    }
                    state.currentSourceItemId = option.id;
                    m_persistCustomAppearance = true;
                    m_lastBikePointer = reinterpret_cast<uintptr_t>(Respawn::GetBikePointer());
                    m_lastCheckpointCount = Respawn::GetCheckpointCount();
                    if (!GearCustomization::QueueBikeChildItemOverride(0, previousTargetItemId, color)) {
                        LOG_WARNING("[DevMenu] Failed to queue default bike child item refresh");
                        return false;
                    }
                    return true;
                }

                bool payloadPatchQueued = false;
                if (!GearCustomization::CopyHiddenObjectVisualPayload(
                        previousTargetItemId,
                        option.id,
                        &payloadPatchQueued)) {
                    LOG_WARNING("[DevMenu] Failed to queue bike child visual payload copy");
                }
                if (payloadPatchQueued) {
                    state.currentSourceItemId = option.id;
                    m_persistCustomAppearance = true;
                    m_lastBikePointer = reinterpret_cast<uintptr_t>(Respawn::GetBikePointer());
                    m_lastCheckpointCount = Respawn::GetCheckpointCount();
                    const uint16_t extraHideItemId = option.id != previousTargetItemId ? option.id : 0;
                    if (state.hidden
                        ? !GearCustomization::QueueBikeChildItemOverride(previousTargetItemId, 0, color, extraHideItemId)
                        : !GearCustomization::QueueBikeChildItemOverride(0, previousTargetItemId, color)) {
                        LOG_WARNING("[DevMenu] Failed to queue bike child item refresh");
                    }
                }
                else if (previousTargetItemId != option.id) {
                    const bool crossBikeSwap = state.bikeKey != option.bikeKey;
                    if (crossBikeSwap) {
                        state.currentSourceItemId = option.id;
                        m_persistCustomAppearance = true;
                        m_lastBikePointer = reinterpret_cast<uintptr_t>(Respawn::GetBikePointer());
                        m_lastCheckpointCount = Respawn::GetCheckpointCount();
                        if (state.hidden
                            ? !GearCustomization::QueueBikeChildItemOverride(previousTargetItemId, 0, color, option.id)
                            : !GearCustomization::QueueBikeChildItemOverride(0, option.id, color)) {
                            LOG_WARNING("[DevMenu] Failed to queue cross-bike child apply without a visual payload copy");
                            return false;
                        }
                        if (!state.hidden) {
                            LOG_WARNING("[DevMenu] Queued cross-bike child apply without hiding the current child because no visual payload copy was available");
                        }
                        return true;
                    }

                    state.currentSourceItemId = option.id;
                    if (!GearCustomization::ReplaceCurrentBikeGearSetChild(previousTargetItemId, option.id)) {
                        LOG_WARNING("[DevMenu] Failed to queue bike child set replacement");
                    }
                    if (hasColorOverride) {
                        GearCustomization::SetBikeChildColorOverride(option.id, color);
                    }
                    const bool selectedPartIsSharedEngine = std::string(option.partKey) == "ENGINE";
                    if (state.hidden
                        ? !GearCustomization::QueueBikeChildItemOverride(option.id, 0, color, previousTargetItemId)
                        : !GearCustomization::QueueBikeChildItemOverride(
                            selectedPartIsSharedEngine ? 0 : previousTargetItemId,
                            option.id,
                            color)) {
                        LOG_WARNING("[DevMenu] Failed to queue replacement bike child item refresh");
                    }
                    state.targetItemId = option.id;
                    m_persistCustomAppearance = true;
                    m_lastBikePointer = reinterpret_cast<uintptr_t>(Respawn::GetBikePointer());
                    m_lastCheckpointCount = Respawn::GetCheckpointCount();
                }
                else {
                    state.currentSourceItemId = option.id;
                }
                return true;
            };

            const std::string label = PrettyPartName(state.partKey);
            const std::string comboId = "##BikePartGroup" + std::to_string(state.targetItemId);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(label.c_str());
            ImGui::SameLine(kGearEditorControlColumnX);

            ImGui::PushItemWidth(kGearEditorControlWidth);
            const std::vector<BikePartGroup> partGroups = BuildBikePartGroups(
                state.candidateItemIds,
                state.bikeKey,
                state.partKey,
                state.targetItemId);
            const BikePartGroup* selectedGroup = nullptr;
            for (const BikePartGroup& group : partGroups) {
                const auto optionIt = std::find_if(group.options.begin(), group.options.end(), [&state](const BikePartGroupOption& option) {
                    return option.entry->id == state.currentSourceItemId;
                });
                if (optionIt != group.options.end()) {
                    selectedGroup = &group;
                    break;
                }
            }

            const std::string preview = selectedGroup ? selectedGroup->label : BikeItemFamilyLabel(*selectedEntry);
            if (ImGui::BeginCombo(comboId.c_str(), preview.c_str())) {
                for (const BikePartGroup& group : partGroups) {
                    const bool selected = selectedGroup && selectedGroup->label == group.label;
                    if (ImGui::Selectable(group.label.c_str(), selected)) {
                        if (!group.options.empty()) {
                            if (selectBikePartOption(*group.options[0].entry)) {
                                state.selectedIndex = static_cast<int>(group.options[0].candidateIndex);
                            }
                        }
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::PopItemWidth();

            ImGui::SameLine(kGearEditorColorColumnX);
            const int colorPartCount = std::min<int>(selectedEntry->colorPartCount, kBikePartMaxColorParts);
            for (int colorIndex = 0; colorIndex < kBikePartMaxColorParts; ++colorIndex) {
                if (colorIndex > 0) {
                    ImGui::SameLine(0.0f, 2.0f);
                }

                if (colorIndex >= colorPartCount) {
                    ImGui::Dummy(ImVec2(kGearEditorColorSwatchSize, ImGui::GetFrameHeight()));
                    continue;
                }

                const std::string colorId =
                    "##BikePartColor" + std::to_string(state.targetItemId) + "_" + std::to_string(colorIndex);
                if (ImGui::ColorEdit3(
                        colorId.c_str(),
                        state.colors[colorIndex],
                        ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel)) {
                    state.hasColorOverrides[colorIndex] = true;
                    GearCustomization::SetBikeChildColorOverride(state.targetItemId, EncodeRgb24(state.colors[colorIndex]));
                }
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    state.hasColorOverrides[colorIndex] = true;
                    const uint32_t color = EncodeRgb24(state.colors[colorIndex]);
                    GearCustomization::SetBikeChildColorOverride(state.targetItemId, color);
                    m_persistCustomAppearance = true;
                    m_lastBikePointer = reinterpret_cast<uintptr_t>(Respawn::GetBikePointer());
                    m_lastCheckpointCount = Respawn::GetCheckpointCount();
                    if (!GearCustomization::QueueBikeChildItemOverride(0, state.targetItemId, color)) {
                        LOG_WARNING("[DevMenu] Failed to queue bike child color update");
                    }
                }
                if (ImGui::IsItemHovered() && colorPartCount > 1) {
                    ImGui::SetTooltip("Color part %d", colorIndex + 1);
                }
            }

            ImGui::SameLine(kGearEditorVisibilityColumnX);
            const std::string invisibleId = "BikePartHidden" + std::to_string(state.targetItemId);
            if (RenderVisibilityButton(invisibleId.c_str(), state.hidden)) {
                state.hidden = !state.hidden;
                m_persistCustomAppearance = true;
                m_lastBikePointer = reinterpret_cast<uintptr_t>(Respawn::GetBikePointer());
                m_lastCheckpointCount = Respawn::GetCheckpointCount();

                uint32_t color = 0x00ffffff;
                for (int colorIndex = 0; colorIndex < kBikePartMaxColorParts; ++colorIndex) {
                    if (state.hasColorOverrides[colorIndex]) {
                        color = EncodeRgb24(state.colors[colorIndex]);
                        break;
                    }
                }

                bool queued = false;
                if (state.hidden) {
                    const uint16_t extraHideItemId = state.currentSourceItemId != state.targetItemId
                        ? state.currentSourceItemId
                        : 0;
                    queued = GearCustomization::QueueBikeChildItemOverride(state.targetItemId, 0, color, extraHideItemId);
                }
                else if (state.currentSourceItemId == state.targetItemId) {
                    queued = GearCustomization::QueueBikeChildItemOverride(0, state.targetItemId, color);
                }
                else {
                    bool payloadPatchQueued = false;
                    const bool payloadPatchAvailable = GearCustomization::CopyHiddenObjectVisualPayload(
                        state.targetItemId,
                        state.currentSourceItemId,
                        &payloadPatchQueued);
                    if (payloadPatchAvailable && payloadPatchQueued) {
                        queued = GearCustomization::QueueBikeChildItemOverride(0, state.targetItemId, color);
                    }
                    else if (payloadPatchAvailable) {
                        queued = GearCustomization::QueueBikeChildItemOverride(0, state.currentSourceItemId, color);
                    }
                }

                if (!queued) {
                    LOG_WARNING("[DevMenu] Failed to queue bike child visibility update");
                }
            }

            ImGui::SameLine(kGearEditorVariantColumnX);
            if (!selectedGroup && !partGroups.empty()) {
                selectedGroup = &partGroups[0];
            }
            if (selectedGroup) {
                for (size_t i = 0; i < selectedGroup->options.size(); ++i) {
                    if (i > 0) {
                        ImGui::SameLine();
                    }

                    const BikePartGroupOption& option = selectedGroup->options[i];
                    const bool selected = state.currentSourceItemId == option.entry->id;
                    if (selected) {
                        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                    }

                    const std::string buttonLabel =
                        std::to_string(i + 1) + "##BikePart" + std::to_string(state.targetItemId) + "_" + std::to_string(option.entry->id);
                    if (ImGui::Button(buttonLabel.c_str(), ImVec2(24.0f, 0.0f))) {
                        if (selectBikePartOption(*option.entry)) {
                            state.selectedIndex = static_cast<int>(option.candidateIndex);
                        }
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s", BikeItemVariationTooltip(*option.entry).c_str());
                    }

                    if (selected) {
                        ImGui::PopStyleColor();
                    }
                }
            }
        }
            ImGui::TreePop();
        }
    }

}

void TweakableAppearanceReload::Reset() {
    if (!m_hasCapturedDefaults) {
        return;
    }

    memcpy(m_appearance, m_defaultAppearance, sizeof(m_appearance));
    DecodeGearFromAppearance();
    DecodeColorsFromAppearance();
    RefreshBikePartEditors();
    for (BikePartEditorState& state : m_bikePartEditors) {
        state.currentSourceItemId = state.targetItemId;
        state.hidden = false;
    }
    m_hasAppearance = true;
    m_dirty = false;
}

void TweakableAppearanceReload::ResetToDefault() {
    Reset();
    if (m_hasAppearance && !GearCustomization::QueueAppearanceUpdate(m_appearance)) {
        LOG_WARNING("[DevMenu] Failed to queue default customization update");
    }
    else if (m_hasAppearance) {
        m_persistCustomAppearance = false;
        m_reapplyAttemptsRemaining = 0;
        m_subpartReapplyAttemptsRemaining = 0;
    }
}

#ifdef DEVELOPMENT_MODE
void TweakableUIViewExplorer::Render() {
    UIViewExplorer::RenderImGui();
}
#endif

void TweakableEditorInspector::Render() {
    ProcessPendingEditorNudgeRestore();

    const uintptr_t gameManager = ResolveGameManagerForEditorInspector();
    const uintptr_t editorManager = ResolveEditorManagerForInspector();
    const uintptr_t selectionManager = ResolveEditorSelectionManagerForInspector();
    const uintptr_t entityManager = ResolveEntityManagerForEditorInspector();
    const std::vector<SelectedEditorObject> selectedObjects = ReadSelectedEditorObjects();

    ImGui::TextDisabled("Read-only editor selection inspector");
    RenderPointerCopyText("Game manager", gameManager);
    RenderPointerCopyText("Editor manager", editorManager);
    RenderPointerCopyText("Selection manager", selectionManager);
    RenderPointerCopyText("Entity manager", entityManager);

    if (editorManager == 0 || selectionManager == 0) {
        ImGui::Spacing();
        ImGui::TextWrapped("Editor state is unavailable. Open the track editor and select or place an object.");
        return;
    }

    ImGui::Separator();
    ImGui::Text("Selected objects: %d", static_cast<int>(selectedObjects.size()));
    if (selectedObjects.empty()) {
        ImGui::TextDisabled("No objects are currently selected.");
        return;
    }

    const std::string report = BuildEditorInspectorReport(
        gameManager,
        editorManager,
        selectionManager,
        entityManager,
        selectedObjects);

    if (ImGui::Button("Copy Inspector Report", ImVec2(180.0f, 0.0f))) {
        ImGui::SetClipboardText(report.c_str());
        LOG_INFO("[EditorInspector] Copied selected object report to clipboard");
        LOG_INFO("[EditorInspector]\n" << report);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Copy the selected object data and raw fields to clipboard");
    }

    ImGui::SameLine();
    if (ImGui::Button("Log Inspector Report", ImVec2(170.0f, 0.0f))) {
        LOG_INFO("[EditorInspector]\n" << report);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Write the selected object report to the mod log and console");
    }

    if (ImGui::BeginTable("EditorSelectedObjects", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("#");
        ImGui::TableSetupColumn("Selected Object");
        ImGui::TableSetupColumn("EA8 Transform");
        ImGui::TableSetupColumn("Rotation");
        ImGui::TableSetupColumn("Light Range");
        ImGui::TableSetupColumn("Visible");
        ImGui::TableSetupColumn("List Node");
        ImGui::TableHeadersRow();

        for (const SelectedEditorObject& selected : selectedObjects) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%d", selected.index);
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%s", selected.selectedObject ? HexAddress(selected.selectedObject).c_str() : "<null>");
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%s", selected.editorTransform ? HexAddress(selected.editorTransform).c_str() : "<missing>");
            ImGui::TableSetColumnIndex(3);
            if (selected.hasRotation) {
                ImGui::Text("%.2f deg", selected.rotationRadians * 57.2957795f);
            }
            else {
                ImGui::TextDisabled("-");
            }
            ImGui::TableSetColumnIndex(4);
            if (selected.hasLightRange) {
                ImGui::Text("%.4f", selected.lightRangeRaw);
            }
            else {
                ImGui::TextDisabled("-");
            }
            ImGui::TableSetColumnIndex(5);
            if (selected.hasVisible) {
                ImGui::Text("%s", selected.visible ? "true" : "false");
            }
            else {
                ImGui::TextDisabled("-");
            }
            ImGui::TableSetColumnIndex(6);
            ImGui::Text("%s", selected.listNode ? HexAddress(selected.listNode).c_str() : "<null>");
        }

        ImGui::EndTable();
    }

    const SelectedEditorObject& primary = selectedObjects.front();
    ImGui::Spacing();
    if (selectionManager != 0 && IsReadableRange(selectionManager, 0x9c)
        && ImGui::TreeNodeEx("Selection Variation Controller", ImGuiTreeNodeFlags_DefaultOpen)) {
        uintptr_t liveVariationObject = 0;
        uintptr_t variationSource0 = 0;
        uintptr_t variationSource1 = 0;
        uintptr_t variationSource2 = 0;
        float variationScaleFactor = 0.0f;
        SafeReadValue(selectionManager + 0x64, variationScaleFactor);
        SafeReadValue(selectionManager + 0x8c, liveVariationObject);
        SafeReadValue(selectionManager + 0x90, variationSource0);
        SafeReadValue(selectionManager + 0x94, variationSource1);
        SafeReadValue(selectionManager + 0x98, variationSource2);

        RenderPointerCopyText("Live variation object at selectionManager + 0x8C", liveVariationObject);
        RenderPointerCopyText("Variation source 0 at selectionManager + 0x90", variationSource0);
        RenderPointerCopyText("Variation source 1 at selectionManager + 0x94", variationSource1);
        RenderPointerCopyText("Variation source 2 at selectionManager + 0x98", variationSource2);
        ImGui::Text("Selection manager +0x64 bounding radius / gizmo scale: %.4f", variationScaleFactor);

        const int variationIndex = VariationIndexFromMask(primary.sceneHolderVariationMask20);
        if (variationIndex >= 0) {
            ImGui::Text("Current variation index candidate: %d", variationIndex + 1);
        }
        else {
            ImGui::TextDisabled("Current variation index candidate: unknown");
        }

        if (ImGui::Button("Apply current +0x64 gizmo radius", ImVec2(240.0f, 0.0f))) {
            const bool applied = ApplyVariationControllerTransform(selectionManager);
            LOG_INFO("[EditorInspector] Apply variation controller transform controller="
                << HexAddress(selectionManager)
                << " liveObject=" << (liveVariationObject ? HexAddress(liveVariationObject) : "<null>")
                << " scaleFactor=" << variationScaleFactor
                << " applied=" << applied);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Calls the engine helper that applies selectionManager +0x48/+0x54/+0x64 to +0x8C");
        }

        ImGui::SameLine();
        if (ImGui::Button("+1.0 gizmo radius and apply", ImVec2(230.0f, 0.0f))) {
            const float newValue = variationScaleFactor + 1.0f;
            const bool wrote = SafeWriteValue(selectionManager + 0x64, newValue);
            const bool applied = wrote && ApplyVariationControllerTransform(selectionManager);
            LOG_INFO("[EditorInspector] Variation scale factor +1.0 "
                << variationScaleFactor << " -> " << newValue
                << " write=" << wrote
                << " applied=" << applied);
        }

        ImGui::SameLine();
        if (ImGui::Button("-1.0 gizmo radius and apply", ImVec2(230.0f, 0.0f))) {
            const float decreasedValue = variationScaleFactor - 1.0f;
            const float newValue = decreasedValue < 0.01f ? 0.01f : decreasedValue;
            const bool wrote = SafeWriteValue(selectionManager + 0x64, newValue);
            const bool applied = wrote && ApplyVariationControllerTransform(selectionManager);
            LOG_INFO("[EditorInspector] Variation scale factor -1.0 "
                << variationScaleFactor << " -> " << newValue
                << " write=" << wrote
                << " applied=" << applied);
        }

        if (variationIndex >= 0 && ImGui::Button("Rebuild current variation", ImVec2(240.0f, 0.0f))) {
            const bool rebuilt = RebuildObjectVariation(selectionManager, variationIndex);
            LOG_INFO("[EditorInspector] Rebuild current variation controller="
                << HexAddress(selectionManager)
                << " variationIndexZeroBased=" << variationIndex
                << " rebuilt=" << rebuilt);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Calls create_object_variation(selectionManager, current zero-based variation index)");
        }

        ImGui::TreePop();
    }

    ImGui::Spacing();
    if (ImGui::TreeNodeEx("Primary Object Properties", ImGuiTreeNodeFlags_DefaultOpen)) {
        RenderPointerCopyText("Selected object", primary.selectedObject);
        RenderPointerCopyText("Mapped object via entityManager + 0xe74", primary.mappedObject);
        RenderPointerCopyText("Editor transform via entityManager + 0xea8", primary.editorTransform);
        RenderPointerCopyText("Editor scale backing via entityManager + 0xe90", primary.editorScaleBackingObject);
        if (primary.editorScaleBackingObject != 0 && IsReadableRange(primary.editorScaleBackingObject + 0x10, sizeof(float))) {
            float backingScale = 0.0f;
            SafeReadValue(primary.editorScaleBackingObject + 0x10, backingScale);
            ImGui::Text("Editor scale backing +0x10 scalar: %.4f", backingScale);
        }
        const std::vector<EntityMapScanResult> primaryEntityMapResults =
            ScanEntityManagerMapsForSelectedObject(primary);
        ImGui::Text("EntityManager selected-object map matches: %d", static_cast<int>(primaryEntityMapResults.size()));
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Included in Copy/Log Inspector Report; useful for finding backing objects near scale/transform");
        }
        RenderPointerCopyText("Parent key at selectedObject + 0x1c", primary.parentObject);
        RenderPointerCopyText("Mapped parent object", primary.mappedParentObject);
        ImGui::Text(
            "Selected object type/subtype: %u / %u",
            static_cast<uint32_t>(primary.selectedObjectType & 0xff),
            static_cast<uint32_t>(primary.selectedObjectType >> 8));
        ImGui::Text(
            "Selected object +0x0C flags: 0x%08X  physics bit 0x10000=%s",
            primary.selectedObjectFlags0c,
            primary.selectedObjectPhysicsEnabled ? "true" : "false");
        ImGui::Text(
            "Selected object +0x04 movement state: 0x%08X  moving/rotating=%s",
            primary.selectedObjectMovementState04,
            primary.movingOrRotatingCandidate ? "true" : "false");
        if (primary.selectedObject != 0 && IsReadableRange(primary.selectedObject + 0x24, sizeof(float))) {
            float selectedObject24 = 0.0f;
            if (SafeReadValue(primary.selectedObject + 0x24, selectedObject24)) {
                ImGui::Text("Selected object +0x24 float scale-axis candidate: %.4f", selectedObject24);
                if (ImGui::Button("Add +10.0 to selectedObject +0x24", ImVec2(280.0f, 0.0f))) {
                    const float newValue = selectedObject24 + 10.0f;
                    if (SafeWriteValue(primary.selectedObject + 0x24, newValue)) {
                        LOG_INFO("[EditorInspector] Wrote selectedObject+0x24 float "
                            << selectedObject24 << " -> " << newValue
                            << " at " << HexAddress(primary.selectedObject + 0x24));
                    }
                    else {
                        LOG_WARNING("[EditorInspector] Failed to write selectedObject+0x24 at "
                            << HexAddress(primary.selectedObject + 0x24));
                    }
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Experimental runtime write: selected object +0x24 = current + 10.0f");
                }
                ImGui::SameLine();
                if (ImGui::Button("+10.0 and mark first mesh dirty", ImVec2(260.0f, 0.0f))) {
                    const float newValue = selectedObject24 + 10.0f;
                    const bool wroteScaleCandidate = SafeWriteValue(primary.selectedObject + 0x24, newValue);
                    bool wroteDirtyFlag = false;
                    uint32_t meshFlags0c = 0;
                    if (primary.firstMeshSceneObject != 0
                        && SafeReadValue(primary.firstMeshSceneObject + 0x0c, meshFlags0c)) {
                        const uint32_t dirtyFlags = meshFlags0c | 0x80;
                        wroteDirtyFlag = SafeWriteValue(primary.firstMeshSceneObject + 0x0c, dirtyFlags);
                    }

                    LOG_INFO("[EditorInspector] Scale-axis dirty probe selectedObject+0x24 "
                        << selectedObject24 << " -> " << newValue
                        << " write=" << wroteScaleCandidate
                        << " firstMesh=" << (primary.firstMeshSceneObject ? HexAddress(primary.firstMeshSceneObject) : "<missing>")
                        << " dirtyFlagWrite=" << wroteDirtyFlag);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Writes selectedObject+0x24, then sets first mesh +0x0C bit 0x80 like SetObjectScale does before render update");
                }
            }
        }
        ImGui::Text("Selected object child mode/count: %u", primary.selectedObjectChildMode);
        RenderPointerCopyText("Selected object children at +0x18", primary.selectedObjectChildren);
        ImGui::Text("Selected object flags at +0x20: 0x%08X", primary.selectedObjectFlags);
        RenderPointerCopyText("Scene holder at selectedObject + 0x44", primary.sceneHolder);
        RenderPointerCopyText("Resource container at [selectedObject + 0x44]", primary.resourceContainer);
        RenderPointerCopyText("Resource scene root at resourceContainer + 0x68", primary.resourceSceneRoot);

        if (primary.sceneHolder != 0 && IsReadableRange(primary.sceneHolder, 0x20)) {
            uint32_t holder1c = 0;
            float holder1cFloat = 0.0f;
            SafeReadValue(primary.sceneHolder + 0x1c, holder1c);
            memcpy(&holder1cFloat, &holder1c, sizeof(holder1cFloat));
            ImGui::Text(
                "Scene holder +0x08 flags: 0x%08X  horizontal=%s bit2/drivingLine=%s",
                primary.sceneHolderFlags08,
                primary.horizontalAlign ? "true" : "false",
                primary.lockedToDrivingLineCandidate ? "true" : "false");
            ImGui::Text(
                "Flags +0x08 bits: 0=%s 2=%s 3=%s 4=%s 5=%s 6=%s 10=%s 12=%s",
                primary.sceneHolderBit0 ? "1" : "0",
                primary.lockedToDrivingLineCandidate ? "1" : "0",
                primary.sceneHolderBit3 ? "1" : "0",
                primary.horizontalAlign ? "1" : "0",
                primary.sceneHolderBit5 ? "1" : "0",
                primary.sceneHolderBit6 ? "1" : "0",
                primary.fastObjectCandidate ? "1" : "0",
                primary.sceneHolderBit12 ? "1" : "0");
            ImGui::Text("Scene holder +0x20 variation mask: 0x%08X", primary.sceneHolderVariationMask20);
            ImGui::Text("Scene holder +0x1C: 0x%08X / %.4f", holder1c, holder1cFloat);
        }

        ImGui::Spacing();
        ImGui::Text("Named editor properties");
        if (primary.hasRotation) {
            ImGui::Text("Rotation: %.4f rad / %.2f deg", primary.rotationRadians, primary.rotationRadians * 57.2957795f);
        }
        else {
            ImGui::TextDisabled("Rotation: unavailable");
        }
        if (primary.hasLightRange) {
            ImGui::Text("Light range from first mesh +0xA4: %.4f", primary.lightRangeRaw);
        }
        else {
            ImGui::TextDisabled("Light range: unavailable");
        }
        if (primary.hasSceneHolderBuoyancy) {
            ImGui::Text("Buoyancy from scene holder +0x1C: %.4f", primary.sceneHolderBuoyancyRaw);
        }
        if (primary.hasFriction) {
            ImGui::Text("Unknown first mesh +0x90: %.4f", primary.frictionRaw);
        }
        if (primary.hasMeshOffset94) {
            ImGui::Text("Unknown first mesh +0x94: %.4f", primary.meshOffset94Raw);
        }
        if (primary.hasObjectGravity) {
            ImGui::Text("Unknown first mesh +0x98: %.4f", primary.objectGravityRaw);
        }
        if (primary.hasMeshOffset9c) {
            ImGui::Text("Unknown first mesh +0x9C: %.4f", primary.meshOffset9cRaw);
        }
        if (primary.hasLightIntensity) {
            ImGui::Text("Light intensity from first mesh +0xB4: %.4f", primary.lightIntensityRaw);
        }
        if (primary.hasVisible) {
            ImGui::Text("Visible: %s", primary.visible ? "true" : "false");
        }
        else {
            ImGui::TextDisabled("Visible: unavailable");
        }
        if (primary.hasSceneHolderVisible) {
            ImGui::Text("Visible candidate: %s", primary.sceneHolderVisible ? "true" : "false");
            ImGui::Text("Contact response candidate: %s", primary.contactResponseEnabled ? "enabled" : "disabled");
            ImGui::Text("Fast object candidate: %s", primary.fastObjectCandidate ? "true" : "false");
            ImGui::Text(
                "Locked to driving line candidate: %s",
                primary.lockedToDrivingLineCandidate ? "true" : "false");
        }
        if (primary.hasShadowType) {
            ImGui::Text("Shadow type candidate: %s", primary.shadowTypeDynamicCandidate ? "dynamic" : "static");
        }
        else {
            ImGui::TextDisabled("Shadow type: unavailable");
        }
        if (primary.hasSelectedObjectPhysicsEnabled) {
            ImGui::Text(
                "Selected object physics: %s",
                primary.selectedObjectPhysicsEnabled ? "enabled" : "disabled");
        }
        if (primary.hasLightEnabled) {
            ImGui::Text("Light enabled: %s", primary.lightEnabled ? "true" : "false");
        }

        ImGui::Spacing();
        if (ImGui::TreeNodeEx("Material Color Override Test", ImGuiTreeNodeFlags_DefaultOpen)) {
            std::vector<uintptr_t> materialTestMeshes;
            CollectMaterialColorTestSceneObjects(primary, materialTestMeshes);

            ImGui::Text("Collected scene objects for test: %d", static_cast<int>(materialTestMeshes.size()));
            RenderPointerCopyText("Primary first mesh scene object", primary.firstMeshSceneObject);
            if (primary.firstMeshSceneObject != 0) {
                ImGui::TextWrapped("First mesh selector candidates: %s", BuildMaterialSelectorSummary(primary.firstMeshSceneObject).c_str());
            }
            if (!materialTestMeshes.empty()) {
                ImGui::TextWrapped("First collected selector candidates: %s", BuildMaterialSelectorSummary(materialTestMeshes.front()).c_str());
            }
            ImGui::SetNextItemWidth(240.0f);
            ImGui::ColorEdit3(
                "Override color##editorMaterialOverrideColor",
                g_editorMaterialTestColor,
                ImGuiColorEditFlags_NoInputs);
            ImGui::SetNextItemWidth(160.0f);
            ImGui::InputInt("Selector##editorMaterialSelector", &g_editorMaterialTestSelector, 1, 16);
            if (g_editorMaterialTestSelector < 0) {
                g_editorMaterialTestSelector = 0;
            }
            ImGui::SetNextItemWidth(160.0f);
            ImGui::InputInt("Slot##editorMaterialSlot", &g_editorMaterialTestSlot, 1, 1);
            if (g_editorMaterialTestSlot < 0) {
                g_editorMaterialTestSlot = 0;
            }
            if (g_editorMaterialTestSlot > 255) {
                g_editorMaterialTestSlot = 255;
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%s", DescribeMaterialColorSlot(static_cast<uint8_t>(g_editorMaterialTestSlot)));
            ImGui::SetNextItemWidth(160.0f);
            ImGui::InputInt("Source mode##editorMaterialSourceMode", &g_editorMaterialTestSourceMode, 1, 1);
            if (g_editorMaterialTestSourceMode < 0) {
                g_editorMaterialTestSourceMode = 0;
            }
            if (g_editorMaterialTestSourceMode > 255) {
                g_editorMaterialTestSourceMode = 255;
            }
            ImGui::SetNextItemWidth(160.0f);
            ImGui::InputInt("Override mode##editorMaterialOverrideMode", &g_editorMaterialTestOverrideMode, 1, 1);
            if (g_editorMaterialTestOverrideMode < 0) {
                g_editorMaterialTestOverrideMode = 0;
            }
            if (g_editorMaterialTestOverrideMode > 255) {
                g_editorMaterialTestOverrideMode = 255;
            }
            ImGui::SetNextItemWidth(160.0f);
            ImGui::InputInt("Secondary mode##editorMaterialSecondaryMode", &g_editorMaterialTestSecondaryMode, 1, 1);
            if (g_editorMaterialTestSecondaryMode < 0) {
                g_editorMaterialTestSecondaryMode = 0;
            }
            if (g_editorMaterialTestSecondaryMode > 255) {
                g_editorMaterialTestSecondaryMode = 255;
            }
            ImGui::Checkbox("Refresh after apply##editorMaterialRefresh", &g_editorMaterialTestRefreshAfterApply);
            if (ImGui::Checkbox("Sticky armed##editorMaterialSticky", &g_editorMaterialStickyEnabled)) {
                if (g_editorMaterialStickyEnabled) {
                    int attempts = 0;
                    const int appliedCount = ArmStickyEditorMaterialOverride(materialTestMeshes, &attempts);
                    LOG_INFO("[EditorInspector] Sticky material color override armed objects="
                        << materialTestMeshes.size()
                        << " paramOwners=" << g_editorMaterialStickyParamOwnerCount
                        << " attempts=" << attempts
                        << " initialApplied=" << appliedCount
                        << " autoSelectors=" << g_editorMaterialStickyUseAutoSelectors);
                }
                else {
                    g_editorMaterialStickySceneObjects.clear();
                    InterlockedExchange(&g_editorMaterialStickyParamOwnerCount, 0);
                    LOG_INFO("[EditorInspector] Sticky material color override disabled");
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Arms hooks for the current selection; does not run a timed reapply loop");
            }
            ImGui::SameLine();
            ImGui::Checkbox("Sticky auto selectors##editorMaterialStickyAuto", &g_editorMaterialStickyUseAutoSelectors);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Sticky mode tries selector candidates from +0x20/+0x24/+0x38/+0x58");
            }

            if (ImGui::Button("Apply color to first mesh", ImVec2(210.0f, 0.0f))) {
                const bool applied = ApplyMaterialColorOverrideToSceneObject(
                    primary.firstMeshSceneObject,
                    static_cast<uint32_t>(g_editorMaterialTestSelector),
                    static_cast<uint8_t>(g_editorMaterialTestSlot),
                    static_cast<uint8_t>(g_editorMaterialTestSourceMode),
                    static_cast<uint8_t>(g_editorMaterialTestOverrideMode),
                    static_cast<uint8_t>(g_editorMaterialTestSecondaryMode),
                    g_editorMaterialTestColor,
                    g_editorMaterialTestRefreshAfterApply);
                LOG_INFO("[EditorInspector] Material color test firstMesh="
                    << (primary.firstMeshSceneObject ? HexAddress(primary.firstMeshSceneObject) : "<missing>")
                    << " selector=0x" << std::hex << std::uppercase << static_cast<uint32_t>(g_editorMaterialTestSelector)
                    << std::dec
                    << " slot=" << g_editorMaterialTestSlot
                    << " color=(" << g_editorMaterialTestColor[0]
                    << "," << g_editorMaterialTestColor[1]
                    << "," << g_editorMaterialTestColor[2]
                    << ") applied=" << applied);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Calls create_mesh_instances on the first collected mesh scene object");
            }

            ImGui::SameLine();
            if (ImGui::Button("Apply color to collected", ImVec2(210.0f, 0.0f))) {
                const int appliedCount = ApplyMaterialColorOverrideToObjects(
                    materialTestMeshes,
                    static_cast<uint32_t>(g_editorMaterialTestSelector),
                    static_cast<uint8_t>(g_editorMaterialTestSlot),
                    static_cast<uint8_t>(g_editorMaterialTestSourceMode),
                    static_cast<uint8_t>(g_editorMaterialTestOverrideMode),
                    static_cast<uint8_t>(g_editorMaterialTestSecondaryMode),
                    g_editorMaterialTestColor,
                    g_editorMaterialTestRefreshAfterApply);
                LOG_INFO("[EditorInspector] Material color test allMeshes count="
                    << materialTestMeshes.size()
                    << " applied=" << appliedCount
                    << " selector=0x" << std::hex << std::uppercase << static_cast<uint32_t>(g_editorMaterialTestSelector)
                    << std::dec
                    << " slot=" << g_editorMaterialTestSlot);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Calls create_mesh_instances on collected material/mesh scene object candidates");
            }

            if (ImGui::Button("Use first mesh +0x38 selector", ImVec2(220.0f, 0.0f))) {
                uint32_t selector = 0;
                if (primary.firstMeshSceneObject != 0
                    && SafeReadValue(primary.firstMeshSceneObject + 0x38, selector)) {
                    g_editorMaterialTestSelector = static_cast<int>(selector);
                    LOG_INFO("[EditorInspector] Material color selector set from firstMesh+0x38 selector=0x"
                        << std::hex << std::uppercase << selector << std::dec);
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Ghidra material collection path uses sceneObject +0x38 as a material selector");
            }

            ImGui::SameLine();
            if (ImGui::Button("Try auto selectors", ImVec2(180.0f, 0.0f))) {
                int attempts = 0;
                const int appliedCount = ApplyMaterialColorAutoSelectorsToObjects(
                    materialTestMeshes,
                    static_cast<uint32_t>(g_editorMaterialTestSelector),
                    static_cast<uint8_t>(g_editorMaterialTestSlot),
                    static_cast<uint8_t>(g_editorMaterialTestSourceMode),
                    static_cast<uint8_t>(g_editorMaterialTestOverrideMode),
                    static_cast<uint8_t>(g_editorMaterialTestSecondaryMode),
                    g_editorMaterialTestColor,
                    g_editorMaterialTestRefreshAfterApply,
                    &attempts);
                LOG_INFO("[EditorInspector] Material color auto selector test objects="
                    << materialTestMeshes.size()
                    << " attempts=" << attempts
                    << " appliedCalls=" << appliedCount
                    << " slot=" << g_editorMaterialTestSlot
                    << " color=(" << g_editorMaterialTestColor[0]
                    << "," << g_editorMaterialTestColor[1]
                    << "," << g_editorMaterialTestColor[2]
                    << ")");
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Tries non-zero selector candidates from +0x20/+0x24/+0x38/+0x58 on each collected scene object");
            }

            if (ImGui::Button("Set material value ptr", ImVec2(210.0f, 0.0f))) {
                int attempts = 0;
                const int appliedCount = WriteMaterialParameterValuePtrsToObjects(
                    materialTestMeshes,
                    static_cast<uint32_t>(g_editorMaterialTestSelector),
                    static_cast<uint8_t>(g_editorMaterialTestSlot),
                    g_editorMaterialTestColor,
                    g_editorMaterialTestRefreshAfterApply,
                    &attempts);
                LOG_INFO("[EditorInspector] Material value pointer write test objects="
                    << materialTestMeshes.size()
                    << " attempts=" << attempts
                    << " applied=" << appliedCount
                    << " slot=" << g_editorMaterialTestSlot
                    << " color=(" << g_editorMaterialTestColor[0]
                    << "," << g_editorMaterialTestColor[1]
                    << "," << g_editorMaterialTestColor[2]
                    << ")");
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Writes the looked-up diffuse/diffuse2 value pointer once; no override record or sticky hook");
            }

            ImGui::SameLine();
            if (ImGui::Button("Trace material reset", ImVec2(190.0f, 0.0f))) {
                const bool armed = ArmMaterialParameterTrace(
                    materialTestMeshes,
                    static_cast<uint32_t>(g_editorMaterialTestSelector),
                    static_cast<uint8_t>(g_editorMaterialTestSlot));
                LOG_INFO("[EditorInspector] Material reset trace armed=" << armed);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Logs value-pointer changes for the first matching diffuse/diffuse2 parameter; no vtable hook");
            }

            if (ImGui::Button("Trace backing events", ImVec2(190.0f, 0.0f))) {
                EnsureTrackEventMaterialTraceHookInstalled();
                InterlockedExchange(&g_editorMaterialTrackEventTraceCount, 0);
                InterlockedExchange(&g_editorMaterialTrackEventAnyTraceCount, 0);
                g_editorMaterialTrackEventTraceArmed = g_editorMaterialTrackEventHookInstalled;
                LOG_INFO("[EditorInspector] TrackEvent material trace armed="
                    << g_editorMaterialTrackEventTraceArmed);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Logs native event 0x12/0x13 material/color backing map state before and after engine handling");
            }

            if (ImGui::Button("Set mesh color fields", ImVec2(210.0f, 0.0f))) {
                const int appliedCount = SetSerializedMeshColorFieldsOnObjects(
                    materialTestMeshes,
                    g_editorMaterialTestColor,
                    g_editorMaterialTestRefreshAfterApply);
                LOG_INFO("[EditorInspector] Serialized mesh color field test objects="
                    << materialTestMeshes.size()
                    << " applied=" << appliedCount
                    << " color=(" << g_editorMaterialTestColor[0]
                    << "," << g_editorMaterialTestColor[1]
                    << "," << g_editorMaterialTestColor[2]
                    << ")");
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Writes mesh +0x90..+0x9c once; this is the serializer-tracked color path, not a timed override");
            }

            ImGui::SameLine();
            if (ImGui::Button("Start sticky", ImVec2(120.0f, 0.0f))) {
                int attempts = 0;
                const int appliedCount = ArmStickyEditorMaterialOverride(materialTestMeshes, &attempts);
                LOG_INFO("[EditorInspector] Sticky material color override armed objects="
                    << materialTestMeshes.size()
                    << " paramOwners=" << g_editorMaterialStickyParamOwnerCount
                    << " attempts=" << attempts
                    << " initialApplied=" << appliedCount
                    << " autoSelectors="
                    << (g_editorMaterialStickyUseAutoSelectors ? "true" : "false"));
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Arms sticky hooks using the current Sticky auto selectors checkbox state");
            }

            ImGui::SameLine();
            if (ImGui::Button("Stop sticky", ImVec2(120.0f, 0.0f))) {
                g_editorMaterialStickyEnabled = false;
                g_editorMaterialStickySceneObjects.clear();
                InterlockedExchange(&g_editorMaterialStickyParamOwnerCount, 0);
                LOG_INFO("[EditorInspector] Sticky material color override disabled");
            }

            ImGui::SameLine();
            if (ImGui::Button("Reset test color", ImVec2(150.0f, 0.0f))) {
                g_editorMaterialTestColor[0] = 1.0f;
                g_editorMaterialTestColor[1] = 1.0f;
                g_editorMaterialTestColor[2] = 1.0f;
            }

            ImGui::TreePop();
        }

        ImGui::Spacing();
        ImGui::Text("Visual scale");
        if (ImGui::Button("Trace publish scale", ImVec2(190.0f, 0.0f))) {
            EnsureEditorUploadTraceHooksInstalled();
            InterlockedExchange(&g_editorUploadTraceCount, 0);
            g_editorUploadTraceArmed = g_editorUploadTrackListTraceHookInstalled;
            LOG_INFO("[EditorUploadTrace] publish scale trace armed="
                << g_editorUploadTraceArmed
                << " share/publish the track to capture the native object/value list");
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Captures the object/value records the publisher builds for the currently saved track");
        }

        if (g_editorUploadTraceArmed) {
            ImGui::SameLine();
            if (ImGui::Button("Stop publish trace", ImVec2(170.0f, 0.0f))) {
                g_editorUploadTraceArmed = false;
                LOG_INFO("[EditorUploadTrace] publish scale trace manually disarmed");
            }
        }

        ImGui::SameLine();
        if (ImGui::Button("Dump publish snapshot", ImVec2(190.0f, 0.0f))) {
            DumpEditorUploadTraceSnapshot();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Dumps the last captured publisher object/value records without following embedded pointers");
        }
        ImGui::SameLine();
        ImGui::Checkbox("Patch publish scale", &g_editorPublishScalePatchEnabled);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("When enabled, objects scaled by this inspector patch their scale vector into the native publish record");
        }
        ImGui::SameLine();
        ImGui::Checkbox("Native editor uniform scale", &g_editorNativeUniformScaleBackingEnabled);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("For uniform scale, calls the editor's own selected-object scale path used by native save/load");
        }
        ImGui::SameLine();
        ImGui::Checkbox("Sync saved placement", &g_editorSyncSavedPlacementOnScaleEnabled);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Copies the live scene-object position into the EA8/PAR2 saved transform after scale changes");
        }

        if (primary.firstMeshSceneObject != g_editorInspectorAxisScaleObject) {
            g_editorInspectorAxisScaleObject = primary.firstMeshSceneObject;
            g_editorInspectorAutoScale = 1.0f;
            g_editorInspectorLastAppliedScale = 1.0f;
            float liveScale[3] = {};
            if (ReadSceneObjectScaleVector(primary.firstMeshSceneObject, liveScale)) {
                g_editorInspectorAxisScale[0] = ClampEditorAutoScale(liveScale[0]);
                g_editorInspectorAxisScale[1] = ClampEditorAutoScale(liveScale[1]);
                g_editorInspectorAxisScale[2] = ClampEditorAutoScale(liveScale[2]);
            }
            else {
                g_editorInspectorAxisScale[0] = g_editorInspectorAutoScale;
                g_editorInspectorAxisScale[1] = g_editorInspectorAutoScale;
                g_editorInspectorAxisScale[2] = g_editorInspectorAutoScale;
            }
        }

        ImGui::SetNextItemWidth(320.0f);
        const bool scaleChanged = ImGui::SliderFloat(
            "Scale##editorAutoScale",
            &g_editorInspectorAutoScale,
            EDITOR_AUTO_SCALE_MIN,
            EDITOR_AUTO_SCALE_MAX,
            "%.3fx",
            ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_NoRoundToFormat);
        g_editorInspectorAutoScale = ClampEditorAutoScale(g_editorInspectorAutoScale);

        if (scaleChanged && std::fabs(g_editorInspectorAutoScale - g_editorInspectorLastAppliedScale) > 0.0001f) {
            const float previousScale = g_editorInspectorLastAppliedScale > 0.0001f
                ? g_editorInspectorLastAppliedScale
                : 1.0f;
            const float scaleRatio = g_editorInspectorAutoScale / previousScale;
            float scaledAxis[3] = {
                ClampEditorAutoScale(g_editorInspectorAxisScale[0] * scaleRatio),
                ClampEditorAutoScale(g_editorInspectorAxisScale[1] * scaleRatio),
                ClampEditorAutoScale(g_editorInspectorAxisScale[2] * scaleRatio),
            };

            bool applied = TryApplyNativeEditorUniformScaleDelta(g_editorInspectorAutoScale - previousScale);
            if (!applied) {
                applied = SetSceneObjectScaleVectorWithRefresh(
                    primary.firstMeshSceneObject,
                    scaledAxis,
                    0);
            }
            if (applied) {
                CommitEditorVisualScaleChange();
                if (!g_editorNativeUniformScaleBackingEnabled) {
                    TryApplyNativeUniformScaleBacking(primary, scaledAxis);
                }
                SyncEditorSavedPlacementFromBestSceneObject(primary, "auto-scale");
                RememberEditorPublishScaleOverride(primary, scaledAxis);
                g_editorInspectorLastAppliedScale = g_editorInspectorAutoScale;
                g_editorInspectorAxisScale[0] = scaledAxis[0];
                g_editorInspectorAxisScale[1] = scaledAxis[1];
                g_editorInspectorAxisScale[2] = scaledAxis[2];
            }
            LOG_VERBOSE("[EditorInspector] Auto visual scale firstMesh="
                << (primary.firstMeshSceneObject ? HexAddress(primary.firstMeshSceneObject) : "<missing>")
                << " selectedObject=" << (primary.selectedObject ? HexAddress(primary.selectedObject) : "<missing>")
                << " scale=" << g_editorInspectorAutoScale
                << " ratio=" << scaleRatio
                << " axis=(" << scaledAxis[0] << ", " << scaledAxis[1] << ", " << scaledAxis[2] << ")"
                << " applied=" << applied);
        }

        if (primary.firstMeshSceneObject == 0 || primary.selectedObject == 0) {
            ImGui::TextDisabled("Auto scale needs a selected object with a mesh scene object.");
        }
        else {
            if (ImGui::Button("Sync saved placement now", ImVec2(190.0f, 0.0f))) {
                const bool synced = SyncEditorSavedPlacementFromBestSceneObject(primary, "manual");
                LOG_INFO("[EditorInspector] manual saved placement sync selectedObject="
                    << HexAddress(primary.selectedObject)
                    << " firstMesh=" << HexAddress(primary.firstMeshSceneObject)
                    << " synced=" << synced);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Manual test: update the EA8/PAR2 saved transform from the current live mesh position");
            }

            ImGui::SetNextItemWidth(320.0f);
            float axisScale[3] = {
                g_editorInspectorAxisScale[0],
                g_editorInspectorAxisScale[1],
                g_editorInspectorAxisScale[2],
            };
            const bool axisChanged = ImGui::DragFloat3(
                "Axis scale##editorAxisScale",
                axisScale,
                0.01f,
                EDITOR_AUTO_SCALE_MIN,
                EDITOR_AUTO_SCALE_MAX,
                "%.3fx",
                ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_NoRoundToFormat);

            axisScale[0] = ClampEditorAutoScale(axisScale[0]);
            axisScale[1] = ClampEditorAutoScale(axisScale[1]);
            axisScale[2] = ClampEditorAutoScale(axisScale[2]);
            if (axisChanged) {
                const bool previousUniform =
                    std::fabs(g_editorInspectorAxisScale[0] - g_editorInspectorAxisScale[1]) <= 0.0005f
                    && std::fabs(g_editorInspectorAxisScale[0] - g_editorInspectorAxisScale[2]) <= 0.0005f;
                const bool nextUniform =
                    std::fabs(axisScale[0] - axisScale[1]) <= 0.0005f
                    && std::fabs(axisScale[0] - axisScale[2]) <= 0.0005f;
                bool applied = previousUniform && nextUniform
                    ? TryApplyNativeEditorUniformScaleDelta(axisScale[0] - g_editorInspectorAxisScale[0])
                    : false;
                if (!applied) {
                    applied = SetSceneObjectScaleVectorWithRefresh(
                        primary.firstMeshSceneObject,
                        axisScale,
                        0);
                }
                if (applied) {
                    CommitEditorVisualScaleChange();
                    if (!g_editorNativeUniformScaleBackingEnabled) {
                        TryApplyNativeUniformScaleBacking(primary, axisScale);
                    }
                    SyncEditorSavedPlacementFromBestSceneObject(primary, "axis-scale");
                    RememberEditorPublishScaleOverride(primary, axisScale);
                    g_editorInspectorAxisScale[0] = axisScale[0];
                    g_editorInspectorAxisScale[1] = axisScale[1];
                    g_editorInspectorAxisScale[2] = axisScale[2];
                }
                LOG_VERBOSE("[EditorInspector] Axis visual scale firstMesh="
                    << HexAddress(primary.firstMeshSceneObject)
                    << " selectedObject=" << HexAddress(primary.selectedObject)
                    << " scale=(" << axisScale[0] << ", " << axisScale[1] << ", " << axisScale[2] << ")"
                    << " applied=" << applied);
            }

            if (ImGui::Button("Reset axis scale##editorAxisScaleReset", ImVec2(150.0f, 0.0f))) {
                float resetScale[3] = { 1.0f, 1.0f, 1.0f };
                const bool previousUniform =
                    std::fabs(g_editorInspectorAxisScale[0] - g_editorInspectorAxisScale[1]) <= 0.0005f
                    && std::fabs(g_editorInspectorAxisScale[0] - g_editorInspectorAxisScale[2]) <= 0.0005f;
                bool applied = previousUniform
                    ? TryApplyNativeEditorUniformScaleDelta(1.0f - g_editorInspectorAxisScale[0])
                    : false;
                if (!applied) {
                    applied = SetSceneObjectScaleVectorWithRefresh(
                        primary.firstMeshSceneObject,
                        resetScale,
                        0);
                }
                if (applied) {
                    CommitEditorVisualScaleChange();
                    if (!g_editorNativeUniformScaleBackingEnabled) {
                        TryApplyNativeUniformScaleBacking(primary, resetScale);
                    }
                    SyncEditorSavedPlacementFromBestSceneObject(primary, "reset-axis-scale");
                    RememberEditorPublishScaleOverride(primary, resetScale);
                    g_editorInspectorAxisScale[0] = resetScale[0];
                    g_editorInspectorAxisScale[1] = resetScale[1];
                    g_editorInspectorAxisScale[2] = resetScale[2];
                    g_editorInspectorAutoScale = 1.0f;
                    g_editorInspectorLastAppliedScale = 1.0f;
                }
                LOG_INFO("[EditorInspector] Reset axis visual scale firstMesh="
                    << HexAddress(primary.firstMeshSceneObject)
                    << " selectedObject=" << HexAddress(primary.selectedObject)
                    << " applied=" << applied);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Sets the selected visual mesh scale vector to 1,1,1");
            }
        }
        RenderPointerCopyText("First mesh scene object", primary.firstMeshSceneObject);
        ImGui::Text("Mesh scene object count: %u", primary.meshSceneObjectCount);
        ImGui::Text("Mesh reversed type/subtype fallback: %s", primary.meshUsedReversedTypeSubtypeFallback ? "true" : "false");
        RenderPointerCopyText("First visibility scene object", primary.firstVisibilitySceneObject);
        ImGui::Text("Visibility scene object count: %u", primary.visibilitySceneObjectCount);
        ImGui::Text("Visibility reversed type/subtype fallback: %s", primary.visibilityUsedReversedTypeSubtypeFallback ? "true" : "false");
        RenderPointerCopyText("First light scene object", primary.firstLightSceneObject);
        ImGui::Text("Light scene object count: %u", primary.lightSceneObjectCount);
        ImGui::Text("Light reversed type/subtype fallback: %s", primary.lightUsedReversedTypeSubtypeFallback ? "true" : "false");

        ImGui::TreePop();
    }

    if (primary.selectedObject != 0 && ImGui::TreeNode("Raw Selected Object Fields")) {
        if (ImGui::BeginTable("EditorSelectedObjectRawFields", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Offset");
            ImGui::TableSetupColumn("DWord");
            ImGui::TableSetupColumn("Float");
            ImGui::TableSetupColumn("Pointer");
            ImGui::TableHeadersRow();

            for (uintptr_t offset = 0; offset < 0x100; offset += 4) {
                uint32_t dwordValue = 0;
                if (!SafeReadValue(primary.selectedObject + offset, dwordValue)) {
                    continue;
                }

                float floatValue = 0.0f;
                memcpy(&floatValue, &dwordValue, sizeof(floatValue));
                uintptr_t pointerValue = 0;
                if (offset + sizeof(uintptr_t) <= 0x100) {
                    SafeReadValue(primary.selectedObject + offset, pointerValue);
                }

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("+0x%02X", static_cast<unsigned int>(offset));
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("0x%08X", dwordValue);
                ImGui::TableSetColumnIndex(2);
                if (std::isfinite(floatValue) && std::fabs(floatValue) < 1000000.0f) {
                    ImGui::Text("%.4f", floatValue);
                }
                else {
                    ImGui::TextDisabled("-");
                }
                ImGui::TableSetColumnIndex(3);
                if (pointerValue != 0 && IsReadableRange(pointerValue, 0x10)) {
                    ImGui::Text("%s", HexAddress(pointerValue).c_str());
                }
                else {
                    ImGui::TextDisabled("-");
                }
            }

            ImGui::EndTable();
        }
        ImGui::TreePop();
    }
}

void TweakableFmodControls::Render() {
    ImGui::TextUnformatted(Fmod::GetStatusString());
    ImGui::SameLine();
    ImGui::TextDisabled("(last result: %d)", Fmod::GetLastResult());

    const bool eventsMuted = Fmod::AreEventsMuted();
    const char* eventButtonLabel = eventsMuted
        ? "Restore Audio Events##FmodEvents"
        : "Mute Audio Events##FmodEvents";
    if (ImGui::Button(eventButtonLabel, ImVec2(180.0f, 0.0f))) {
        if (!Fmod::ToggleEventsMuted()) {
            LOG_WARNING("[DevMenu] FMOD audio event toggle is pending until FMOD is resolved");
        }
    }

    const bool muteOnStartup = Fmod::IsMuteOnStartupEnabled();
    const char* startupButtonLabel = muteOnStartup
        ? "Disable Startup Mute##FmodStartup"
        : "Enable Startup Mute##FmodStartup";
    ImGui::SameLine();
    if (ImGui::Button(startupButtonLabel, ImVec2(180.0f, 0.0f))) {
        Fmod::ToggleMuteOnStartup();
    }

    ImGui::TextDisabled("%s", Fmod::GetStartupMuteStatusString());
}

void TweakableFileUnlockControls::Render() {
    if (ImGui::Button("Unlock All Items", ImVec2(316.0f, 0.0f))) {
        ImGui::OpenPopup("Confirm Unlock All Items");
    }

    if (ImGui::BeginPopupModal("Confirm Unlock All Items", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("You must restart Trials Fusion to apply this change.");
        ImGui::Spacing();
        ImGui::TextWrapped("This will close the game, patch build\\data_pc\\data_patch.pak, and restart Trials Fusion.");
        ImGui::Spacing();

        if (ImGui::Button("Restart and Unlock", ImVec2(150.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
            FileUnlock::UnlockAllItems();
        }

        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(90.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

// TweakableFolder Implementation
void TweakableFolder::Render() {
    // Use TreeNode for folders
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_None;
    if (m_isOpen) {
        flags |= ImGuiTreeNodeFlags_DefaultOpen;
    }

    std::string label = m_name + "##" + std::to_string(m_id);

    if (ImGui::TreeNodeEx(label.c_str(), flags)) {
        m_isOpen = true;

        // Render all children
        for (auto& child : m_children) {
            child->Render();
        }

        ImGui::TreePop();
    }
    else {
        m_isOpen = false;
    }
}

void TweakableFolder::Reset() {
    for (auto& child : m_children) {
        child->Reset();
    }
}

void TweakableFolder::ResetToDefault() {
    for (auto& child : m_children) {
        child->ResetToDefault();
    }
}

// DevMenu Implementation
DevMenu::DevMenu()
    : m_isVisible(false)
    , m_menuWidth(600.0f)
    , m_menuHeight(800.0f)
    , m_showResetButton(true)
    , m_showSearchBar(true)
    , m_showKeybindingsWindow(false)
{
}

DevMenu::~DevMenu() {
}

void DevMenu::UpdateRuntime() {
#ifdef DEVELOPMENT_MODE
    ProcessPendingEditorNudgeRestore();
    ProcessEditorScaleKeybinds();
    ProcessStickyEditorMaterialOverride();
    ProcessMaterialParameterTrace();
#endif

    if (m_appearanceReload) {
        m_appearanceReload->UpdateRuntime();
    }
}

void DevMenu::Initialize() {
    LOG_VERBOSE("[DevMenu] Initializing...");

    // Initialize all tweakable categories
    InitializeBikeSound();
    InitializeDynamicMusic();
    InitializeProgressionSystem();
    InitializeGarage();
    InitializeContentPack();
    InitializeDLC();
    InitializeEditor();
    InitializeMultiplayer();
    InitializeEvent();
    InitializeFMX();
    InitializeBike();
    InitializeRider();
    InitializeVibra();
    InitializeSoundSystem();
    InitializeReplayCRC();
    InitializeUtils();
    InitializeReplayCamera();
    InitializePhysics();
    InitializeFrameSkipper();
    InitializeGraphic();
    InitializePodium();
    InitializeXPSystem();
    InitializeTrackUpload();
    InitializeGameOption();
    InitializeGameSwf();
    InitializeGameTime();
    InitializeVariableFramerate();
    InitializeDebug();
    InitializeInGameHud();
    InitializeMainHub();
    InitializeMainMenu();
    InitializeFlash();
    InitializeGarbageCollector();
    InitializeSettings();
    InitializeDebugLocalization();
    InitializeMod();
    InitializeKeybindings();

    // Wrap the existing game/editor categories under a single top-level folder
    // while keeping the custom Mod folder separate and first in the list.
    std::shared_ptr<TweakableFolder> modFolder = nullptr;
    std::vector<std::shared_ptr<TweakableFolder>> developerChildren;
    developerChildren.reserve(m_rootFolders.size());

    for (const auto& folder : m_rootFolders) {
        if (folder && folder->GetName() == "Mod") {
            modFolder = folder;
        }
        else {
            developerChildren.push_back(folder);
        }
    }

    if (!developerChildren.empty()) {
        auto redlynxDeveloperMenu = std::make_shared<TweakableFolder>(10001, "Redlynx Developer Menu");
        for (const auto& child : developerChildren) {
            redlynxDeveloperMenu->AddChild(child);
        }

        m_rootFolders.clear();
        if (modFolder) {
            m_rootFolders.push_back(modFolder);
        }
        m_rootFolders.push_back(redlynxDeveloperMenu);
    }

    LOG_VERBOSE("[DevMenu] Initialized with " << m_rootFolders.size() << " root folders");
}

void DevMenu::Render() {
    // Update checkpoint slider range if track changed
    if (g_checkpointIndexSlider) {
        int checkpointCount = Respawn::GetCheckpointCount();
        if (checkpointCount != g_lastCheckpointCount && checkpointCount > 0) {
            g_checkpointIndexSlider->SetRange(0, checkpointCount - 1);
            std::string label = "Select Checkpoint (0-" + std::to_string(checkpointCount - 1) + ")";
            g_checkpointIndexSlider->SetName(label);
            g_lastCheckpointCount = checkpointCount;

            // Clamp current value if needed
            if (g_checkpointIndexSlider->GetValue() >= checkpointCount) {
                g_checkpointIndexSlider->SetValue(checkpointCount - 1);
            }
        }
    }

    // Update prevent finish status label
    if (g_preventFinishLabel) {
        g_preventFinishLabel->SetName(PreventFinish::GetStatusString());
    }

    // Update fault limit toggle button label
    if (g_toggleFaultLimitButton) {
        bool isDisabled = Limits::IsFaultValidationDisabled();
        std::string newLabel = isDisabled
            ? "Fault Limit: DISABLED (Infinite faults)"
            : "Fault Limit: ENABLED (500 faults)";
        g_toggleFaultLimitButton->SetName(newLabel);
    }

    // Update time limit toggle button label
    if (g_toggleTimeLimitButton) {
        bool isDisabled = Limits::IsTimeValidationDisabled();
        std::string newLabel = isDisabled
            ? "Time Limit: DISABLED (Infinite time)"
            : "Time Limit: ENABLED (30 minutes)";
        g_toggleTimeLimitButton->SetName(newLabel);
    }

    // Render Keybindings window independently (even if main menu is hidden)
    if (m_showKeybindingsWindow) {
        ImGui::SetNextWindowSize(ImVec2(820, 400), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(700, 50), ImGuiCond_FirstUseEver);

        if (ImGui::Begin("Keybindings", &m_showKeybindingsWindow)) {
            UpdateGamepadBindingCapture();
            ImGui::Text("Configure hotkeys for mod actions:");
            ImGui::Separator();

            // Build a map of VK codes to count how many actions use each key
            std::unordered_map<int, int> keyUsageCount;
            std::unordered_map<int, int> gamepadUsageCount;
            size_t numKeybinds = m_keybindingItems.size() >= 2 ? m_keybindingItems.size() - 2 : 0;

            for (size_t i = 0; i < numKeybinds; i++) {
                int vkCode = Keybindings::GetKey(m_keybindingActions[i]);
                if (vkCode != 0) { // Ignore unbound keys
                    keyUsageCount[vkCode]++;
                }
                int gamepadButton = Keybindings::GetGamepadButton(m_keybindingActions[i]);
                if (gamepadButton != 0) {
                    gamepadUsageCount[gamepadButton]++;
                }
            }

            // Calculate widths for alignment
            float maxActionWidth = 0.0f;
            float maxKeyWidth = 0.0f;

            for (size_t i = 0; i < numKeybinds; i++) {
                Keybindings::Action action = m_keybindingActions[i];
                std::string actionName = Keybindings::GetActionName(action);
                std::string keyName = Keybindings::GetKeyName(Keybindings::GetKey(action));

                ImVec2 actionSize = ImGui::CalcTextSize(actionName.c_str());
                ImVec2 keySize = ImGui::CalcTextSize(keyName.c_str());

                if (actionSize.x > maxActionWidth) maxActionWidth = actionSize.x;
                if (keySize.x > maxKeyWidth) maxKeyWidth = keySize.x;
            }

            // Add padding for the button
            float buttonPadding = ImGui::GetStyle().FramePadding.x * 2.0f;
            float actionColumnWidth = maxActionWidth + 24.0f;
            float keybindingButtonWidth = maxKeyWidth + buttonPadding + 24.0f;
            if (keybindingButtonWidth < 110.0f) {
                keybindingButtonWidth = 110.0f;
            }
            float gamepadButtonWidth = 150.0f;
            ImGuiTableFlags tableFlags =
                ImGuiTableFlags_BordersV |
                ImGuiTableFlags_BordersOuter |
                ImGuiTableFlags_RowBg |
                ImGuiTableFlags_Resizable |
                ImGuiTableFlags_ScrollY;

            if (ImGui::BeginTable("KeybindingsTable", 6, tableFlags, ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing() * 2.0f))) {
                ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, actionColumnWidth);
                ImGui::TableSetupColumn("Keyboard", ImGuiTableColumnFlags_WidthFixed, keybindingButtonWidth);
                ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 58.0f);
                ImGui::TableSetupColumn("Gamepad", ImGuiTableColumnFlags_WidthFixed, gamepadButtonWidth);
                ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 58.0f);
                ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableHeadersRow();

                // Render keybindings with aligned columns
                std::string lastCategory;
                bool leaderboardScannerHeaderShown = false;
                for (size_t i = 0; i < numKeybinds; i++) {
                    auto& item = m_keybindingItems[i];

                // Get info for this keybinding
                Keybindings::Action action = m_keybindingActions[i];
                std::string category = GetKeybindingCategory(action);
                if (i == 0 || category != lastCategory) {
                    if (category == "Leaderboard Scanner Controls" && leaderboardScannerHeaderShown) {
                        // Keep the scanner rows together under the first header only.
                    } else {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextDisabled("%s", category.c_str());
                        if (category == "Leaderboard Scanner Controls") {
                            leaderboardScannerHeaderShown = true;
                        }
                    }
                    lastCategory = category;
                }
                std::string actionName = Keybindings::GetActionName(action);
                std::string keyName = Keybindings::GetKeyName(Keybindings::GetKey(action));
                int currentKey = Keybindings::GetKey(action);
                int defaultKey = m_keybindingDefaults[i];
                int currentGamepadButton = Keybindings::GetGamepadButton(action);
                int defaultGamepadButton = i < m_gamepadBindingDefaults.size() ? m_gamepadBindingDefaults[i] : 0;
                std::string gamepadButtonName = Keybindings::GetGamepadButtonName(currentGamepadButton);

                // Check if the button is in "waiting for key" mode by checking its actual name
                std::string buttonName = item->GetName();
                bool isWaitingForKey = (buttonName.find("Press any key...") != std::string::npos);
                bool isWaitingForGamepad = g_waitingForGamepadBind && g_waitingGamepadAction == action;

                // Check if this key is bound to multiple actions
                bool isOverbound = (keyUsageCount[currentKey] > 1);
                bool isUnbound = (currentKey == 0);
                bool isGamepadOverbound = (gamepadUsageCount[currentGamepadButton] > 1);
                bool isGamepadUnbound = (currentGamepadButton == 0);

                // Build the button label
                std::string buttonLabel;
                if (isWaitingForKey) {
                    // Show "Press any key..." message when rebinding
                    buttonLabel = "Press any key...##bind" + std::to_string(i);
                } else {
                    buttonLabel = keyName + "##bind" + std::to_string(i);
                }

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%s", actionName.c_str());
                ImGui::TableSetColumnIndex(1);

                // Render the button with fixed width and left-aligned text
                ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f)); // 0.0 = left align, 0.5 = vertical center

                // Show visual indicator when waiting for key press, if key is overbound, or if unbound
                if (isWaitingForKey) {
                    // Use a different color to indicate we're waiting for input
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.6f, 0.3f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.4f, 0.7f, 0.4f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.2f, 0.5f, 0.2f, 1.0f));
                } else if (isOverbound) {
                    // Use red color to indicate this key is bound to multiple actions
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.3f, 0.3f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.7f, 0.1f, 0.1f, 1.0f));
                } else if (isUnbound) {
                    // Use a muted gray to indicate this action is currently unbound
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f, 0.35f, 0.35f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.45f, 0.45f, 0.45f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.25f, 0.25f, 0.25f, 1.0f));
                }

                if (ImGui::Button(buttonLabel.c_str(), ImVec2(keybindingButtonWidth, 0))) {
                    // Trigger the keybinding capture callback
                    if (auto btn = std::dynamic_pointer_cast<TweakableButton>(item)) {
                        btn->TriggerClick();
                    }
                }

                // Add tooltip for overbound keys and unbound keys
                if (isOverbound && !isWaitingForKey) {
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("WARNING: This key is bound to multiple actions!");
                    }
                } else if (isUnbound && !isWaitingForKey) {
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("This action is currently unbound.");
                    }
                }

                if (isWaitingForKey || isOverbound || isUnbound) {
                    ImGui::PopStyleColor(3); // Pop the 3 color overrides
                }

                ImGui::PopStyleVar(); // Restore previous alignment

                ImGui::TableSetColumnIndex(2);
                if (currentKey != defaultKey) {
                    std::string resetKeyboardLabel = "Reset##keyreset" + std::to_string(i);
                    if (ImGui::SmallButton(resetKeyboardLabel.c_str())) {
                        Keybindings::SetKey(action, defaultKey);
                        std::string newKeyName = Keybindings::GetKeyName(defaultKey);
                        item->SetName("Bind " + actionName + ": " + newKeyName);
                        LOG_VERBOSE("[DevMenu] Reset keyboard bind for " << actionName << " to default: " << newKeyName);
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Reset keyboard bind to default");
                    }
                } else {
                    ImGui::TextDisabled("Reset");
                }

                ImGui::TableSetColumnIndex(3);

                std::string gamepadLabel = isWaitingForGamepad
                    ? "Press gamepad...##padbind" + std::to_string(i)
                    : gamepadButtonName + "##padbind" + std::to_string(i);

                ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
                if (isWaitingForGamepad) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.6f, 0.3f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.4f, 0.7f, 0.4f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.2f, 0.5f, 0.2f, 1.0f));
                } else if (isGamepadOverbound) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.3f, 0.3f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.7f, 0.1f, 0.1f, 1.0f));
                } else if (isGamepadUnbound) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f, 0.35f, 0.35f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.45f, 0.45f, 0.45f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.25f, 0.25f, 0.25f, 1.0f));
                }

                if (ImGui::Button(gamepadLabel.c_str(), ImVec2(gamepadButtonWidth, 0))) {
                    g_waitingForGamepadBind = true;
                    g_gamepadCaptureReady = false;
                    g_waitingGamepadAction = action;
                    LOG_VERBOSE("[DevMenu] Waiting for gamepad button to bind " << actionName << "...");
                }

                if (isWaitingForGamepad || isGamepadOverbound || isGamepadUnbound) {
                    ImGui::PopStyleColor(3);
                }
                ImGui::PopStyleVar();

                if (ImGui::IsItemHovered()) {
                    if (isGamepadOverbound && !isWaitingForGamepad) {
                        ImGui::SetTooltip("WARNING: This gamepad button is bound to multiple actions!");
                    } else if (isGamepadUnbound && !isWaitingForGamepad) {
                        ImGui::SetTooltip("This action has no gamepad bind. Press Esc while waiting to clear.");
                    } else {
                        ImGui::SetTooltip("Secondary gamepad bind. Press Esc while waiting to clear.");
                    }
                }

                ImGui::TableSetColumnIndex(4);
                if (currentGamepadButton != defaultGamepadButton) {
                    std::string resetGamepadLabel = "Reset##padreset" + std::to_string(i);
                    if (ImGui::SmallButton(resetGamepadLabel.c_str())) {
                        Keybindings::SetGamepadButton(action, defaultGamepadButton);
                        LOG_VERBOSE("[DevMenu] Reset gamepad bind for " << actionName
                            << " to default: " << Keybindings::GetGamepadButtonName(defaultGamepadButton));
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Reset gamepad bind to default");
                    }
                } else {
                    ImGui::TextDisabled("Reset");
                }

                ImGui::TableSetColumnIndex(5);
                if (currentKey != defaultKey || currentGamepadButton != defaultGamepadButton) {
                    std::string saveLabel = "Save##" + std::to_string(i);
                    if (ImGui::SmallButton(saveLabel.c_str())) {
                        m_keybindingDefaults[i] = currentKey;
                        if (i < m_gamepadBindingDefaults.size()) {
                            m_gamepadBindingDefaults[i] = currentGamepadButton;
                        }
                        Keybindings::SaveToFile();
                        LOG_VERBOSE("[DevMenu] Saved " << actionName << " = " << keyName
                            << ", gamepad = " << gamepadButtonName << " as default");
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Save both binds as default");
                    }
                } else {
                    ImGui::TextDisabled("(default)");
                }
            }
                ImGui::EndTable();
            }

            for (size_t i = numKeybinds; i < m_keybindingItems.size(); i++) {
                m_keybindingItems[i]->Render();
                if (i < m_keybindingItems.size() - 1) {
                    ImGui::SameLine();
                }
            }
        }
        ImGui::End();
    }

    // Early return if main dev menu is not visible
    if (!m_isVisible) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(m_menuWidth, m_menuHeight), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(50, 50), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Developer Menu", &m_isVisible, ImGuiWindowFlags_MenuBar)) {
        ImGui::End();
        return;
    }

    // Menu bar
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Save Config")) {
                SaveConfig("devmenu_config.txt");
            }
            if (ImGui::MenuItem("Load Config")) {
                LoadConfig("devmenu_config.txt");
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Reset All")) {
                ResetAll();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Options")) {
            ImGui::MenuItem("Show Search Bar", nullptr, &m_showSearchBar);
            ImGui::MenuItem("Show Reset Buttons", nullptr, &m_showResetButton);
            bool verboseLoggingEnabled = Logging::IsVerboseEnabled();
            if (ImGui::MenuItem("Verbose Logging", nullptr, &verboseLoggingEnabled)) {
                Logging::SetVerbose(verboseLoggingEnabled);
            }
            ImGui::EndMenu();
        }

        // Keybindings menu
        if (ImGui::BeginMenu("Keybindings")) {
            ImGui::MenuItem("Show Keybindings Window", nullptr, &m_showKeybindingsWindow);
            ImGui::EndMenu();
        }

        ImGui::EndMenuBar();
    }

    // Search bar
    if (m_showSearchBar) {
        char searchBuffer[256] = { 0 };
        strncpy_s(searchBuffer, sizeof(searchBuffer), m_searchFilter.c_str(), _TRUNCATE);

        ImGui::PushItemWidth(-1);
        if (ImGui::InputText("##search", searchBuffer, sizeof(searchBuffer))) {
            m_searchFilter = searchBuffer;
        }
        ImGui::PopItemWidth();

        if (!m_searchFilter.empty()) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Clear")) {
                m_searchFilter.clear();
            }
        }

        ImGui::Separator();
    }

    // Render all root folders
    ImGui::BeginChild("ScrollingRegion", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

    for (auto& folder : m_rootFolders) {
        if (m_searchFilter.empty() || PassesFilter(folder->GetName())) {
            folder->Render();
        }
    }

    ImGui::EndChild();
    ImGui::End();
}

void DevMenu::ResetAll() {
    for (auto& folder : m_rootFolders) {
        folder->ResetToDefault();
    }
}

std::shared_ptr<TweakableFloat> DevMenu::GetFloat(int id) {
    return GetTweakable<TweakableFloat>(id);
}

std::shared_ptr<TweakableInt> DevMenu::GetInt(int id) {
    return GetTweakable<TweakableInt>(id);
}

std::shared_ptr<TweakableBool> DevMenu::GetBool(int id) {
    return GetTweakable<TweakableBool>(id);
}

std::shared_ptr<TweakableFolder> DevMenu::GetFolder(int id) {
    return GetTweakable<TweakableFolder>(id);
}

void DevMenu::SaveConfig(const std::string& filename) {
    LOG_INFO("[DevMenu] Saving config to " << filename);
    std::ofstream file(filename);
    if (!file.is_open()) {
        LOG_ERROR("[DevMenu] Failed to open file for saving: " << filename);
        return;
    }

    for (auto& pair : m_tweakableMap) {
        auto item = pair.second;

        switch (item->GetType()) {
        case TweakableType::Float: {
            auto floatItem = std::static_pointer_cast<TweakableFloat>(item);
            file << item->GetId() << " " << floatItem->GetValue() << "\n";
            break;
        }
        case TweakableType::Int: {
            auto intItem = std::static_pointer_cast<TweakableInt>(item);
            file << item->GetId() << " " << intItem->GetValue() << "\n";
            break;
        }
        case TweakableType::Bool: {
            auto boolItem = std::static_pointer_cast<TweakableBool>(item);
            file << item->GetId() << " " << (boolItem->GetValue() ? 1 : 0) << "\n";
            break;
        }
        default:
            break;
        }
    }

    file.close();
    LOG_INFO("[DevMenu] Config saved successfully");
}

void DevMenu::LoadConfig(const std::string& filename) {
    LOG_INFO("[DevMenu] Loading config from " << filename);
    std::ifstream file(filename);
    if (!file.is_open()) {
        LOG_WARNING("[DevMenu] Config file not found: " << filename);
        return;
    }

    int loadedCount = 0;
    std::string line;
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        int id;
        float value;

        if (iss >> id >> value) {
            auto it = m_tweakableMap.find(id);
            if (it != m_tweakableMap.end()) {
                auto item = it->second;

                switch (item->GetType()) {
                case TweakableType::Float: {
                    auto floatItem = std::static_pointer_cast<TweakableFloat>(item);
                    floatItem->SetValue(value);
                    loadedCount++;
                    break;
                }
                case TweakableType::Int: {
                    auto intItem = std::static_pointer_cast<TweakableInt>(item);
                    intItem->SetValue(static_cast<int>(value));
                    loadedCount++;
                    break;
                }
                case TweakableType::Bool: {
                    auto boolItem = std::static_pointer_cast<TweakableBool>(item);
                    boolItem->SetValue(value != 0.0f);
                    loadedCount++;
                    break;
                }
                default:
                    break;
                }
            }
            else {
                LOG_VERBOSE("[DevMenu] Tweakable ID " << id << " not found in map");
            }
        }
    }

    file.close();
    LOG_INFO("[DevMenu] Config loaded: " << loadedCount << " values restored");
}

void DevMenu::RegisterTweakable(std::shared_ptr<TweakableItem> item) {
    m_tweakableMap[item->GetId()] = item;
}

bool DevMenu::PassesFilter(const std::string& name) {
    if (m_searchFilter.empty()) return true;

    std::string lowerName = name;
    std::string lowerFilter = m_searchFilter;

    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
    std::transform(lowerFilter.begin(), lowerFilter.end(), lowerFilter.begin(), ::tolower);

    return lowerName.find(lowerFilter) != std::string::npos;
}

// Initialization Functions - BikeSound
void DevMenu::InitializeBikeSound() {
    auto bikeSound = std::make_shared<TweakableFolder>(1, "BikeSound");

    // Top-level BikeSound parameters
    auto rpmIdle = CreateSyncedFloat(2, "RpmIdle", 2000.0f, 0.0f, 15000.0f);
    auto rpmFull = CreateSyncedFloat(3, "RpmFull", 10000.0f, 0.0f, 20000.0f);

    RegisterTweakable(rpmIdle);
    RegisterTweakable(rpmFull);
    bikeSound->AddChild(rpmIdle);
    bikeSound->AddChild(rpmFull);

    // Gear1 folder
    auto gear1 = std::make_shared<TweakableFolder>(4, "Gear1");
    auto g1_maxRPM = CreateSyncedFloat(5, "MaxRPM", 0.0f, 0.0f, 20000.0f);
    auto g1_min = CreateSyncedFloat(11, "Min", 0.0f, 0.0f, 100.0f);
    auto g1_max = CreateSyncedFloat(12, "Max", 10.0f, 0.0f, 100.0f);
    auto g1_medianRPM = CreateSyncedFloat(17, "MedianRPM", 5000.0f, 0.0f, 15000.0f);
    auto g1_clutchSeek = CreateSyncedFloat(20, "ClutchSeek", 0.1f, 0.0f, 1.0f);
    auto g1_rpmSeek = CreateSyncedFloat(23, "RPMSeek", 0.1f, 0.0f, 1.0f);
    auto g1_rpmSeekUpInAir = CreateSyncedFloat(26, "RPMSeekUpInAir", 0.2f, 0.0f, 1.0f);
    auto g1_rpmSeekDownInAir = CreateSyncedFloat(29, "RPMSeekDownInAir", 0.1f, 0.0f, 1.0f);
    auto g1_loadSeek = CreateSyncedFloat(32, "LoadSeek", 0.2f, 0.0f, 1.0f);
    auto g1_throttleSeek = CreateSyncedFloat(35, "ThrottleSeek", 0.25f, 0.0f, 1.0f);
    auto g1_externalLoadAmount = CreateSyncedFloat(38, "ExternalLoadAmount", 4.5f, 0.0f, 10.0f);
    auto g1_internalLoadAmount = CreateSyncedFloat(41, "InternalLoadAmount", 4.5f, 0.0f, 10.0f);
    auto g1_throttleLoadAmount = CreateSyncedFloat(44, "ThrottleLoadAmount", 0.2f, 0.0f, 5.0f);
    auto g1_shiftNegativeLoadAmount = CreateSyncedFloat(47, "ShiftNegativeLoadAmount", -0.5f, -5.0f, 5.0f);
    auto g1_throttleRpmResponse = CreateSyncedFloat(50, "ThrottleRpmResponse", 1500.0f, 0.0f, 5000.0f);
    auto g1_extraRPMInAir = CreateSyncedFloat(55, "ExtraRPMInAir", 0.0f, 0.0f, 5000.0f);
    auto g1_tractionSeekUp = CreateSyncedFloat(59, "TractionSeekUp", 0.2f, 0.0f, 1.0f);
    auto g1_tractionSeekDown = CreateSyncedFloat(62, "TractionSeekDown", 0.2f, 0.0f, 1.0f);
    auto g1_tractionFrames = CreateSyncedInt(65, "TractionFrames", 10, 0, 100);

    RegisterTweakable(g1_maxRPM);
    RegisterTweakable(g1_min);
    RegisterTweakable(g1_max);
    RegisterTweakable(g1_medianRPM);
    RegisterTweakable(g1_clutchSeek);
    RegisterTweakable(g1_rpmSeek);
    RegisterTweakable(g1_rpmSeekUpInAir);
    RegisterTweakable(g1_rpmSeekDownInAir);
    RegisterTweakable(g1_loadSeek);
    RegisterTweakable(g1_throttleSeek);
    RegisterTweakable(g1_externalLoadAmount);
    RegisterTweakable(g1_internalLoadAmount);
    RegisterTweakable(g1_throttleLoadAmount);
    RegisterTweakable(g1_shiftNegativeLoadAmount);
    RegisterTweakable(g1_throttleRpmResponse);
    RegisterTweakable(g1_extraRPMInAir);
    RegisterTweakable(g1_tractionSeekUp);
    RegisterTweakable(g1_tractionSeekDown);
    RegisterTweakable(g1_tractionFrames);

    gear1->AddChild(g1_maxRPM);
    gear1->AddChild(g1_min);
    gear1->AddChild(g1_max);
    gear1->AddChild(g1_medianRPM);
    gear1->AddChild(g1_clutchSeek);
    gear1->AddChild(g1_rpmSeek);
    gear1->AddChild(g1_rpmSeekUpInAir);
    gear1->AddChild(g1_rpmSeekDownInAir);
    gear1->AddChild(g1_loadSeek);
    gear1->AddChild(g1_throttleSeek);
    gear1->AddChild(g1_externalLoadAmount);
    gear1->AddChild(g1_internalLoadAmount);
    gear1->AddChild(g1_throttleLoadAmount);
    gear1->AddChild(g1_shiftNegativeLoadAmount);
    gear1->AddChild(g1_throttleRpmResponse);
    gear1->AddChild(g1_extraRPMInAir);
    gear1->AddChild(g1_tractionSeekUp);
    gear1->AddChild(g1_tractionSeekDown);
    gear1->AddChild(g1_tractionFrames);

    RegisterTweakable(gear1);
    bikeSound->AddChild(gear1);

    // Gear2 folder
    auto gear2 = std::make_shared<TweakableFolder>(6, "Gear2");
    auto g2_maxRPM = CreateSyncedFloat(7, "MaxRPM", 0.0f, 0.0f, 20000.0f);
    auto g2_min = CreateSyncedFloat(13, "Min", 10.0f, 0.0f, 100.0f);
    auto g2_max = CreateSyncedFloat(14, "Max", 20.0f, 0.0f, 100.0f);
    auto g2_medianRPM = CreateSyncedFloat(18, "MedianRPM", 5000.0f, 0.0f, 15000.0f);
    auto g2_clutchSeek = CreateSyncedFloat(21, "ClutchSeek", 0.1f, 0.0f, 1.0f);
    auto g2_rpmSeek = CreateSyncedFloat(24, "RPMSeek", 0.1f, 0.0f, 1.0f);
    auto g2_rpmSeekUpInAir = CreateSyncedFloat(27, "RPMSeekUpInAir", 0.2f, 0.0f, 1.0f);
    auto g2_rpmSeekDownInAir = CreateSyncedFloat(30, "RPMSeekDownInAir", 0.1f, 0.0f, 1.0f);
    auto g2_loadSeek = CreateSyncedFloat(33, "LoadSeek", 0.2f, 0.0f, 1.0f);
    auto g2_throttleSeek = CreateSyncedFloat(36, "ThrottleSeek", 0.25f, 0.0f, 1.0f);
    auto g2_externalLoadAmount = CreateSyncedFloat(39, "ExternalLoadAmount", 4.5f, 0.0f, 10.0f);
    auto g2_internalLoadAmount = CreateSyncedFloat(42, "InternalLoadAmount", 4.5f, 0.0f, 10.0f);
    auto g2_throttleLoadAmount = CreateSyncedFloat(45, "ThrottleLoadAmount", 0.2f, 0.0f, 5.0f);
    auto g2_shiftNegativeLoadAmount = CreateSyncedFloat(48, "ShiftNegativeLoadAmount", -0.5f, -5.0f, 5.0f);
    auto g2_throttleRpmResponse = CreateSyncedFloat(51, "ThrottleRpmResponse", 1500.0f, 0.0f, 5000.0f);
    auto g2_extraRPMInAir = CreateSyncedFloat(56, "ExtraRPMInAir", 0.0f, 0.0f, 5000.0f);
    auto g2_tractionSeekUp = CreateSyncedFloat(60, "TractionSeekUp", 0.2f, 0.0f, 1.0f);
    auto g2_tractionSeekDown = CreateSyncedFloat(63, "TractionSeekDown", 0.2f, 0.0f, 1.0f);
    auto g2_tractionFrames = CreateSyncedInt(66, "TractionFrames", 10, 0, 100);

    RegisterTweakable(g2_maxRPM);
    RegisterTweakable(g2_min);
    RegisterTweakable(g2_max);
    RegisterTweakable(g2_medianRPM);
    RegisterTweakable(g2_clutchSeek);
    RegisterTweakable(g2_rpmSeek);
    RegisterTweakable(g2_rpmSeekUpInAir);
    RegisterTweakable(g2_rpmSeekDownInAir);
    RegisterTweakable(g2_loadSeek);
    RegisterTweakable(g2_throttleSeek);
    RegisterTweakable(g2_externalLoadAmount);
    RegisterTweakable(g2_internalLoadAmount);
    RegisterTweakable(g2_throttleLoadAmount);
    RegisterTweakable(g2_shiftNegativeLoadAmount);
    RegisterTweakable(g2_throttleRpmResponse);
    RegisterTweakable(g2_extraRPMInAir);
    RegisterTweakable(g2_tractionSeekUp);
    RegisterTweakable(g2_tractionSeekDown);
    RegisterTweakable(g2_tractionFrames);

    gear2->AddChild(g2_maxRPM);
    gear2->AddChild(g2_min);
    gear2->AddChild(g2_max);
    gear2->AddChild(g2_medianRPM);
    gear2->AddChild(g2_clutchSeek);
    gear2->AddChild(g2_rpmSeek);
    gear2->AddChild(g2_rpmSeekUpInAir);
    gear2->AddChild(g2_rpmSeekDownInAir);
    gear2->AddChild(g2_loadSeek);
    gear2->AddChild(g2_throttleSeek);
    gear2->AddChild(g2_externalLoadAmount);
    gear2->AddChild(g2_internalLoadAmount);
    gear2->AddChild(g2_throttleLoadAmount);
    gear2->AddChild(g2_shiftNegativeLoadAmount);
    gear2->AddChild(g2_throttleRpmResponse);
    gear2->AddChild(g2_extraRPMInAir);
    gear2->AddChild(g2_tractionSeekUp);
    gear2->AddChild(g2_tractionSeekDown);
    gear2->AddChild(g2_tractionFrames);

    RegisterTweakable(gear2);
    bikeSound->AddChild(gear2);

    // Gear3 folder
    auto gear3 = std::make_shared<TweakableFolder>(8, "Gear3");
    auto g3_maxRPM = CreateSyncedFloat(9, "MaxRPM", 0.0f, 0.0f, 20000.0f);
    auto g3_min = CreateSyncedFloat(15, "Min", 20.0f, 0.0f, 100.0f);
    auto g3_max = CreateSyncedFloat(16, "Max", 30.0f, 0.0f, 100.0f);
    auto g3_medianRPM = CreateSyncedFloat(19, "MedianRPM", 5000.0f, 0.0f, 15000.0f);
    auto g3_clutchSeek = CreateSyncedFloat(22, "ClutchSeek", 0.1f, 0.0f, 1.0f);
    auto g3_rpmSeek = CreateSyncedFloat(25, "RPMSeek", 0.1f, 0.0f, 1.0f);
    auto g3_rpmSeekUpInAir = CreateSyncedFloat(28, "RPMSeekUpInAir", 0.2f, 0.0f, 1.0f);
    auto g3_rpmSeekDownInAir = CreateSyncedFloat(31, "RPMSeekDownInAir", 0.1f, 0.0f, 1.0f);
    auto g3_loadSeek = CreateSyncedFloat(34, "LoadSeek", 0.2f, 0.0f, 1.0f);
    auto g3_throttleSeek = CreateSyncedFloat(37, "ThrottleSeek", 0.25f, 0.0f, 1.0f);
    auto g3_externalLoadAmount = CreateSyncedFloat(40, "ExternalLoadAmount", 4.5f, 0.0f, 10.0f);
    auto g3_internalLoadAmount = CreateSyncedFloat(43, "InternalLoadAmount", 4.5f, 0.0f, 10.0f);
    auto g3_throttleLoadAmount = CreateSyncedFloat(46, "ThrottleLoadAmount", 0.2f, 0.0f, 5.0f);
    auto g3_shiftNegativeLoadAmount = CreateSyncedFloat(49, "ShiftNegativeLoadAmount", -0.5f, -5.0f, 5.0f);
    auto g3_throttleRpmResponse = CreateSyncedFloat(52, "ThrottleRpmResponse", 1500.0f, 0.0f, 5000.0f);
    auto g3_extraRPMInAir = CreateSyncedFloat(57, "ExtraRPMInAir", 0.0f, 0.0f, 5000.0f);
    auto g3_tractionSeekUp = CreateSyncedFloat(61, "TractionSeekUp", 0.2f, 0.0f, 1.0f);
    auto g3_tractionSeekDown = CreateSyncedFloat(64, "TractionSeekDown", 0.2f, 0.0f, 1.0f);
    auto g3_tractionFrames = CreateSyncedInt(67, "TractionFrames", 10, 0, 100);

    RegisterTweakable(g3_maxRPM);
    RegisterTweakable(g3_min);
    RegisterTweakable(g3_max);
    RegisterTweakable(g3_medianRPM);
    RegisterTweakable(g3_clutchSeek);
    RegisterTweakable(g3_rpmSeek);
    RegisterTweakable(g3_rpmSeekUpInAir);
    RegisterTweakable(g3_rpmSeekDownInAir);
    RegisterTweakable(g3_loadSeek);
    RegisterTweakable(g3_throttleSeek);
    RegisterTweakable(g3_externalLoadAmount);
    RegisterTweakable(g3_internalLoadAmount);
    RegisterTweakable(g3_throttleLoadAmount);
    RegisterTweakable(g3_shiftNegativeLoadAmount);
    RegisterTweakable(g3_throttleRpmResponse);
    RegisterTweakable(g3_extraRPMInAir);
    RegisterTweakable(g3_tractionSeekUp);
    RegisterTweakable(g3_tractionSeekDown);
    RegisterTweakable(g3_tractionFrames);

    gear3->AddChild(g3_maxRPM);
    gear3->AddChild(g3_min);
    gear3->AddChild(g3_max);
    gear3->AddChild(g3_medianRPM);
    gear3->AddChild(g3_clutchSeek);
    gear3->AddChild(g3_rpmSeek);
    gear3->AddChild(g3_rpmSeekUpInAir);
    gear3->AddChild(g3_rpmSeekDownInAir);
    gear3->AddChild(g3_loadSeek);
    gear3->AddChild(g3_throttleSeek);
    gear3->AddChild(g3_externalLoadAmount);
    gear3->AddChild(g3_internalLoadAmount);
    gear3->AddChild(g3_throttleLoadAmount);
    gear3->AddChild(g3_shiftNegativeLoadAmount);
    gear3->AddChild(g3_throttleRpmResponse);
    gear3->AddChild(g3_extraRPMInAir);
    gear3->AddChild(g3_tractionSeekUp);
    gear3->AddChild(g3_tractionSeekDown);
    gear3->AddChild(g3_tractionFrames);

    RegisterTweakable(gear3);
    bikeSound->AddChild(gear3);

    // Additional BikeSound parameters
    auto gearCount = CreateSyncedInt(10, "GearCount", 2, 1, 6);
    auto shiftDuration = CreateSyncedFloat(53, "ShiftDuration", 0.5f, 0.0f, 2.0f);
    auto cameraDebugRadius = CreateSyncedFloat(54, "CameraDebugRadius", 0.0f, 0.0f, 10.0f);
    auto numberOfFramesInAirRequired = CreateSyncedInt(58, "NumberOfFramesInAirRequired", 8, 0, 60);
    auto gainTire = CreateSyncedFloat(237, "GainTire", 1.0f, 0.0f, 5.0f);
    auto gainEngine = CreateSyncedFloat(238, "GainEngine", 1.0f, 0.0f, 5.0f);
    auto gainWind = CreateSyncedFloat(239, "GainWind", 1.0f, 0.0f, 5.0f);
    auto overrideTireSpeed = CreateSyncedFloat(240, "OverrideTireSpeed", 0.0f, 0.0f, 100.0f);
    auto printTerrainSoundMaterial = CreateSyncedBool(241, "PrintTerrainSoundMaterial", false);
    auto debugFmod = CreateSyncedBool(242, "DebugFmod", true);
    auto skidSoundDelay = CreateSyncedInt(254, "skidSoundDelay", 16, 0, 100);
    auto bodyCollisionSoundDelay = CreateSyncedInt(255, "bodyCollisionSoundDelay", 10, 0, 100);
    auto bikeCollisionSoundDelay = CreateSyncedInt(256, "bikeCollisionSoundDelay", 16, 0, 100);
    auto wheelCollisionSoundDelay = CreateSyncedInt(257, "wheelCollisionSoundDelay", 0, 0, 100);
    auto minimumCollisionSoundForce = CreateSyncedFloat(262, "MinimumCollisionSoundForce", 0.5f, 0.0f, 20.0f);
    auto maximumCollisionSoundForce = CreateSyncedFloat(263, "MaximumCollisionSoundForce", 8.5f, 0.0f, 50.0f);
    auto gainRolling = CreateSyncedFloat(342, "GainRolling", 1.0f, 0.0f, 5.0f);

    RegisterTweakable(gearCount);
    RegisterTweakable(shiftDuration);
    RegisterTweakable(cameraDebugRadius);
    RegisterTweakable(numberOfFramesInAirRequired);
    RegisterTweakable(gainTire);
    RegisterTweakable(gainEngine);
    RegisterTweakable(gainWind);
    RegisterTweakable(overrideTireSpeed);
    RegisterTweakable(printTerrainSoundMaterial);
    RegisterTweakable(debugFmod);
    RegisterTweakable(skidSoundDelay);
    RegisterTweakable(bodyCollisionSoundDelay);
    RegisterTweakable(bikeCollisionSoundDelay);
    RegisterTweakable(wheelCollisionSoundDelay);
    RegisterTweakable(minimumCollisionSoundForce);
    RegisterTweakable(maximumCollisionSoundForce);
    RegisterTweakable(gainRolling);

    bikeSound->AddChild(gearCount);
    bikeSound->AddChild(shiftDuration);
    bikeSound->AddChild(cameraDebugRadius);
    bikeSound->AddChild(numberOfFramesInAirRequired);
    bikeSound->AddChild(gainTire);
    bikeSound->AddChild(gainEngine);
    bikeSound->AddChild(gainWind);
    bikeSound->AddChild(overrideTireSpeed);
    bikeSound->AddChild(printTerrainSoundMaterial);
    bikeSound->AddChild(debugFmod);
    bikeSound->AddChild(skidSoundDelay);
    bikeSound->AddChild(bodyCollisionSoundDelay);
    bikeSound->AddChild(bikeCollisionSoundDelay);
    bikeSound->AddChild(wheelCollisionSoundDelay);
    bikeSound->AddChild(minimumCollisionSoundForce);
    bikeSound->AddChild(maximumCollisionSoundForce);
    bikeSound->AddChild(gainRolling);

    RegisterTweakable(bikeSound);
    m_rootFolders.push_back(bikeSound);
}

void DevMenu::InitializeDynamicMusic() {
    auto dynamicMusic = std::make_shared<TweakableFolder>(68, "DynamicMusic");

    auto progress = CreateSyncedFloat(69, "Progress", 0.0f, 0.0f, 1.0f);
    auto maxProgressAuto = CreateSyncedFloat(70, "MaxProgressAuto", 0.99f, 0.0f, 1.0f);
    auto leftFront = CreateSyncedFloat(71, "LeftFront", 0.92f, 0.0f, 1.0f);
    auto rightFront = CreateSyncedFloat(72, "RightFront", 0.92f, 0.0f, 1.0f);
    auto leftRear = CreateSyncedFloat(73, "LeftRear", 0.68f, 0.0f, 1.0f);
    auto rightRear = CreateSyncedFloat(74, "RightRear", 0.68f, 0.0f, 1.0f);
    auto leftSide = CreateSyncedFloat(75, "LeftSide", 0.5f, 0.0f, 1.0f);
    auto rightSide = CreateSyncedFloat(76, "RightSide", 0.5f, 0.0f, 1.0f);
    auto center = CreateSyncedFloat(77, "Center", 0.0f, 0.0f, 1.0f);
    auto lowFrequencyEmitter = CreateSyncedFloat(78, "LowFrequencyEmitter", 0.8f, 0.0f, 1.0f);

    RegisterTweakable(progress);
    RegisterTweakable(maxProgressAuto);
    RegisterTweakable(leftFront);
    RegisterTweakable(rightFront);
    RegisterTweakable(leftRear);
    RegisterTweakable(rightRear);
    RegisterTweakable(leftSide);
    RegisterTweakable(rightSide);
    RegisterTweakable(center);
    RegisterTweakable(lowFrequencyEmitter);

    dynamicMusic->AddChild(progress);
    dynamicMusic->AddChild(maxProgressAuto);
    dynamicMusic->AddChild(leftFront);
    dynamicMusic->AddChild(rightFront);
    dynamicMusic->AddChild(leftRear);
    dynamicMusic->AddChild(rightRear);
    dynamicMusic->AddChild(leftSide);
    dynamicMusic->AddChild(rightSide);
    dynamicMusic->AddChild(center);
    dynamicMusic->AddChild(lowFrequencyEmitter);

    RegisterTweakable(dynamicMusic);
    m_rootFolders.push_back(dynamicMusic);
}

void DevMenu::InitializeProgressionSystem() {
    auto progressionSystem = std::make_shared<TweakableFolder>(79, "ProgressionSystem");

    auto allBikesUnlocked = CreateSyncedBool(80, "AllBikesUnlocked", true);
    auto fmxTricksUnlocked = CreateSyncedBool(344, "FMXTricksUnlocked", true);
    auto allTracksUnlocked = CreateSyncedBool(491, "AllTracksUnlocked", true);

    RegisterTweakable(allBikesUnlocked);
    RegisterTweakable(fmxTricksUnlocked);
    RegisterTweakable(allTracksUnlocked);

    progressionSystem->AddChild(allBikesUnlocked);
    progressionSystem->AddChild(fmxTricksUnlocked);
    progressionSystem->AddChild(allTracksUnlocked);

    RegisterTweakable(progressionSystem);
    m_rootFolders.push_back(progressionSystem);
}

void DevMenu::InitializeGarage() {
    auto garage = std::make_shared<TweakableFolder>(81, "Garage");

    // Camera folder
    auto camera = std::make_shared<TweakableFolder>(82, "Camera");

    // Bike subfolder
    auto bike = std::make_shared<TweakableFolder>(83, "Bike");
    auto bike_shift = CreateSyncedFloat(84, "Shift", 1.04f, 0.0f, 10.0f);
    auto bike_rotShift = CreateSyncedFloat(85, "RotationShift", 1.97f, 0.0f, 10.0f);
    auto bike_rotX = CreateSyncedFloat(86, "CameraRotX", 3.14159f, -10.0f, 10.0f);
    auto bike_rotY = CreateSyncedFloat(87, "CameraRotY", 0.0f, -10.0f, 10.0f);
    auto bike_zoomDist = CreateSyncedFloat(88, "ZoomOutDistance", -5.0f, -50.0f, 50.0f);
    auto bike_zoomHeight = CreateSyncedFloat(89, "ZoomOutHeight", 1.5f, -10.0f, 10.0f);
    auto bike_zoomShift = CreateSyncedFloat(90, "ZoomOutShift", 1.0f, 0.0f, 10.0f);
    auto bike_minZoom = CreateSyncedFloat(91, "MinZoomCameraDistance", 1.6f, 0.0f, 20.0f);
    auto bike_maxZoom = CreateSyncedFloat(92, "MaxZoomCameraDistance", 6.0f, 0.0f, 20.0f);
    auto bike_camDist = CreateSyncedFloat(93, "CameraDistance", 3.0f, 0.0f, 20.0f);
    auto bike_camRotDist = CreateSyncedFloat(94, "CameraRotationDistance", 4.5f, 0.0f, 20.0f);
    auto bike_camHeight = CreateSyncedFloat(95, "CameraHeight", 0.75f, -10.0f, 10.0f);

    RegisterTweakable(bike_shift);
    RegisterTweakable(bike_rotShift);
    RegisterTweakable(bike_rotX);
    RegisterTweakable(bike_rotY);
    RegisterTweakable(bike_zoomDist);
    RegisterTweakable(bike_zoomHeight);
    RegisterTweakable(bike_zoomShift);
    RegisterTweakable(bike_minZoom);
    RegisterTweakable(bike_maxZoom);
    RegisterTweakable(bike_camDist);
    RegisterTweakable(bike_camRotDist);
    RegisterTweakable(bike_camHeight);

    bike->AddChild(bike_shift);
    bike->AddChild(bike_rotShift);
    bike->AddChild(bike_rotX);
    bike->AddChild(bike_rotY);
    bike->AddChild(bike_zoomDist);
    bike->AddChild(bike_zoomHeight);
    bike->AddChild(bike_zoomShift);
    bike->AddChild(bike_minZoom);
    bike->AddChild(bike_maxZoom);
    bike->AddChild(bike_camDist);
    bike->AddChild(bike_camRotDist);
    bike->AddChild(bike_camHeight);

    RegisterTweakable(bike);
    camera->AddChild(bike);

    // Rider subfolder
    auto rider = std::make_shared<TweakableFolder>(96, "Rider");
    auto rider_shift = CreateSyncedFloat(97, "Shift", 1.45f, 0.0f, 10.0f);
    auto rider_aspect = CreateSyncedFloat(98, "Aspect4x3", 1.2f, 0.0f, 5.0f);
    auto rider_shiftHelmet = CreateSyncedFloat(99, "ShiftHelmet", 0.3f, 0.0f, 5.0f);
    auto rider_shiftTop = CreateSyncedFloat(100, "ShiftTop", 0.41f, 0.0f, 5.0f);
    auto rider_shiftBottom = CreateSyncedFloat(101, "ShiftBottom", 0.3f, 0.0f, 5.0f);
    auto rider_rotShift = CreateSyncedFloat(102, "RotationShift", 2.43f, 0.0f, 10.0f);
    auto rider_rotShiftHelmet = CreateSyncedFloat(103, "RotationShiftHelmet", 0.52f, 0.0f, 5.0f);
    auto rider_rotShiftTop = CreateSyncedFloat(104, "RotationShiftTop", 1.08f, 0.0f, 5.0f);
    auto rider_rotShiftBottom = CreateSyncedFloat(105, "RotationShiftBottom", 1.0f, 0.0f, 5.0f);
    auto rider_camRotX = CreateSyncedFloat(106, "CameraRotX", -1.78f, -10.0f, 10.0f);
    auto rider_camRotY = CreateSyncedFloat(107, "CameraRotY", 0.0f, -10.0f, 10.0f);
    auto rider_zoomDist = CreateSyncedFloat(108, "ZoomOutDistance", -5.0f, -50.0f, 50.0f);
    auto rider_zoomHeight = CreateSyncedFloat(109, "ZoomOutHeight", 1.0f, -10.0f, 10.0f);
    auto rider_zoomShift = CreateSyncedFloat(110, "ZoomOutShift", 2.2f, 0.0f, 10.0f);
    auto rider_camDist = CreateSyncedFloat(111, "CameraDistance", 3.0f, 0.0f, 20.0f);
    auto rider_camRotDist = CreateSyncedFloat(112, "CameraRotationDistance", 4.5f, 0.0f, 20.0f);
    auto rider_camDistHelmet = CreateSyncedFloat(113, "CameraDistanceHelmet", 1.0f, 0.0f, 10.0f);
    auto rider_camDistTop = CreateSyncedFloat(114, "CameraDistanceTop", 1.45f, 0.0f, 10.0f);
    auto rider_camDistBottom = CreateSyncedFloat(115, "CameraDistanceBottom", 1.4f, 0.0f, 10.0f);
    auto rider_camRotDistHelmet = CreateSyncedFloat(116, "CameraRotationDistanceHelmet", 1.8f, 0.0f, 10.0f);
    auto rider_camRotDistTop = CreateSyncedFloat(117, "CameraRotationDistanceTop", 2.5f, 0.0f, 10.0f);
    auto rider_camRotDistBottom = CreateSyncedFloat(118, "CameraRotationDistanceBottom", 2.5f, 0.0f, 10.0f);
    auto rider_minZoomRotDist = CreateSyncedFloat(119, "MinZoomCameraRotationDistance", 3.0f, 0.0f, 10.0f);
    auto rider_maxZoomRotDist = CreateSyncedFloat(120, "MaxZoomCameraRotationDistance", 4.0f, 0.0f, 10.0f);
    auto rider_minZoomRotDistHelmet = CreateSyncedFloat(121, "MinZoomCameraRotationDistanceHelmet", 1.0f, 0.0f, 10.0f);
    auto rider_maxZoomRotDistHelmet = CreateSyncedFloat(122, "MaxZoomCameraRotationDistanceHelmet", 1.8f, 0.0f, 10.0f);
    auto rider_minZoomRotDistTop = CreateSyncedFloat(123, "MinZoomCameraRotationDistanceTop", 1.2f, 0.0f, 10.0f);
    auto rider_maxZoomRotDistTop = CreateSyncedFloat(124, "MaxZoomCameraRotationDistanceTop", 2.2f, 0.0f, 10.0f);
    auto rider_minZoomRotDistBottom = CreateSyncedFloat(125, "MinZoomCameraRotationDistanceBottom", 1.2f, 0.0f, 10.0f);
    auto rider_maxZoomRotDistBottom = CreateSyncedFloat(126, "MaxZoomCameraRotationDistanceBottom", 2.2f, 0.0f, 10.0f);
    auto rider_camHeight = CreateSyncedFloat(127, "CameraHeight", 1.0f, -10.0f, 10.0f);
    auto rider_camHeightHelmet = CreateSyncedFloat(128, "CameraHeightHelmet", 0.02f, -5.0f, 5.0f);
    auto rider_camHeightTop = CreateSyncedFloat(129, "CameraHeightTop", 0.01f, -5.0f, 5.0f);
    auto rider_camHeightBottom = CreateSyncedFloat(130, "CameraHeightBottom", -0.09f, -5.0f, 5.0f);

    RegisterTweakable(rider_shift);
    RegisterTweakable(rider_aspect);
    RegisterTweakable(rider_shiftHelmet);
    RegisterTweakable(rider_shiftTop);
    RegisterTweakable(rider_shiftBottom);
    RegisterTweakable(rider_rotShift);
    RegisterTweakable(rider_rotShiftHelmet);
    RegisterTweakable(rider_rotShiftTop);
    RegisterTweakable(rider_rotShiftBottom);
    RegisterTweakable(rider_camRotX);
    RegisterTweakable(rider_camRotY);
    RegisterTweakable(rider_zoomDist);
    RegisterTweakable(rider_zoomHeight);
    RegisterTweakable(rider_zoomShift);
    RegisterTweakable(rider_camDist);
    RegisterTweakable(rider_camRotDist);
    RegisterTweakable(rider_camDistHelmet);
    RegisterTweakable(rider_camDistTop);
    RegisterTweakable(rider_camDistBottom);
    RegisterTweakable(rider_camRotDistHelmet);
    RegisterTweakable(rider_camRotDistTop);
    RegisterTweakable(rider_camRotDistBottom);
    RegisterTweakable(rider_minZoomRotDist);
    RegisterTweakable(rider_maxZoomRotDist);
    RegisterTweakable(rider_minZoomRotDistHelmet);
    RegisterTweakable(rider_maxZoomRotDistHelmet);
    RegisterTweakable(rider_minZoomRotDistTop);
    RegisterTweakable(rider_maxZoomRotDistTop);
    RegisterTweakable(rider_minZoomRotDistBottom);
    RegisterTweakable(rider_maxZoomRotDistBottom);
    RegisterTweakable(rider_camHeight);
    RegisterTweakable(rider_camHeightHelmet);
    RegisterTweakable(rider_camHeightTop);
    RegisterTweakable(rider_camHeightBottom);

    rider->AddChild(rider_shift);
    rider->AddChild(rider_aspect);
    rider->AddChild(rider_shiftHelmet);
    rider->AddChild(rider_shiftTop);
    rider->AddChild(rider_shiftBottom);
    rider->AddChild(rider_rotShift);
    rider->AddChild(rider_rotShiftHelmet);
    rider->AddChild(rider_rotShiftTop);
    rider->AddChild(rider_rotShiftBottom);
    rider->AddChild(rider_camRotX);
    rider->AddChild(rider_camRotY);
    rider->AddChild(rider_zoomDist);
    rider->AddChild(rider_zoomHeight);
    rider->AddChild(rider_zoomShift);
    rider->AddChild(rider_camDist);
    rider->AddChild(rider_camRotDist);
    rider->AddChild(rider_camDistHelmet);
    rider->AddChild(rider_camDistTop);
    rider->AddChild(rider_camDistBottom);
    rider->AddChild(rider_camRotDistHelmet);
    rider->AddChild(rider_camRotDistTop);
    rider->AddChild(rider_camRotDistBottom);
    rider->AddChild(rider_minZoomRotDist);
    rider->AddChild(rider_maxZoomRotDist);
    rider->AddChild(rider_minZoomRotDistHelmet);
    rider->AddChild(rider_maxZoomRotDistHelmet);
    rider->AddChild(rider_minZoomRotDistTop);
    rider->AddChild(rider_maxZoomRotDistTop);
    rider->AddChild(rider_minZoomRotDistBottom);
    rider->AddChild(rider_maxZoomRotDistBottom);
    rider->AddChild(rider_camHeight);
    rider->AddChild(rider_camHeightHelmet);
    rider->AddChild(rider_camHeightTop);
    rider->AddChild(rider_camHeightBottom);

    RegisterTweakable(rider);
    camera->AddChild(rider);

    // Camera-level parameters
    auto cam_interp = CreateSyncedFloat(480, "Interpolation", 0.06f, 0.0f, 1.0f);
    auto cam_rotSpeed = CreateSyncedFloat(481, "RotationSpeed", 0.05f, 0.0f, 1.0f);
    auto cam_rotSpeedMouse = CreateSyncedFloat(482, "RotationSpeedMouse", 0.25f, 0.0f, 2.0f);

    RegisterTweakable(cam_interp);
    RegisterTweakable(cam_rotSpeed);
    RegisterTweakable(cam_rotSpeedMouse);
    camera->AddChild(cam_interp);
    camera->AddChild(cam_rotSpeed);
    camera->AddChild(cam_rotSpeedMouse);

    RegisterTweakable(camera);
    garage->AddChild(camera);

    // Garage-level parameters
    auto dynamicDof = CreateSyncedBool(470, "DynamicDof", true);
    auto dofNearBlur = CreateSyncedFloat(471, "DofNearBlur", 0.1f, 0.0f, 5.0f);
    auto dofFarBlur = CreateSyncedFloat(472, "DofFarBlur", 1.5f, 0.0f, 10.0f);
    auto dofFarBlurStart = CreateSyncedFloat(473, "DofFarBlurStart", 0.0f, 0.0f, 50.0f);
    auto dofFarBlurEnd = CreateSyncedFloat(474, "DofFarBlurEnd", 12.0f, 0.0f, 50.0f);
    auto lumValue = CreateSyncedFloat(475, "LuminanceValue", 0.5f, 0.0f, 2.0f);
    auto lumBlend = CreateSyncedFloat(476, "LuminanceBlend", 0.5f, 0.0f, 1.0f);
    auto sunAngle = CreateSyncedInt(477, "SunAngle", 10, 0, 360);
    auto sunIntensity = CreateSyncedInt(478, "SunIntensity", 255, 0, 255);
    auto longitude = CreateSyncedInt(479, "Longitude", 50, 0, 360);

    RegisterTweakable(dynamicDof);
    RegisterTweakable(dofNearBlur);
    RegisterTweakable(dofFarBlur);
    RegisterTweakable(dofFarBlurStart);
    RegisterTweakable(dofFarBlurEnd);
    RegisterTweakable(lumValue);
    RegisterTweakable(lumBlend);
    RegisterTweakable(sunAngle);
    RegisterTweakable(sunIntensity);
    RegisterTweakable(longitude);

    garage->AddChild(dynamicDof);
    garage->AddChild(dofNearBlur);
    garage->AddChild(dofFarBlur);
    garage->AddChild(dofFarBlurStart);
    garage->AddChild(dofFarBlurEnd);
    garage->AddChild(lumValue);
    garage->AddChild(lumBlend);
    garage->AddChild(sunAngle);
    garage->AddChild(sunIntensity);
    garage->AddChild(longitude);

    // Lights subfolder
    auto lights = std::make_shared<TweakableFolder>(483, "Lights");
    auto bikeSpotIntensity = CreateSyncedFloat(484, "BikeSpotIntensity", 20.0f, 0.0f, 100.0f);
    RegisterTweakable(bikeSpotIntensity);
    lights->AddChild(bikeSpotIntensity);
    RegisterTweakable(lights);
    garage->AddChild(lights);

    // Animation subfolder
    auto animation = std::make_shared<TweakableFolder>(485, "Animation");
    auto usePodiumAnims = CreateSyncedBool(486, "UsePodiumAnimations", false);
    auto activePodiumAnim = CreateSyncedInt(487, "ActivePodiumAnimation", 1, 0, 10);
    RegisterTweakable(usePodiumAnims);
    RegisterTweakable(activePodiumAnim);
    animation->AddChild(usePodiumAnims);
    animation->AddChild(activePodiumAnim);
    RegisterTweakable(animation);
    garage->AddChild(animation);

    RegisterTweakable(garage);
    m_rootFolders.push_back(garage);
}


void DevMenu::InitializeContentPack() {
    auto contentPack = std::make_shared<TweakableFolder>(131, "ContentPack");

    // Add all ContentPack tweakables
    auto enableAdvertEventsForContentPacks = CreateSyncedBool(132, "Enable Advert events for content packs", true);
    auto owned = CreateSyncedBool(133, "Owned", true);
    auto comingSoon = CreateSyncedBool(134, "Coming Soon", false);

    RegisterTweakable(enableAdvertEventsForContentPacks);
    RegisterTweakable(owned);
    RegisterTweakable(comingSoon);

    contentPack->AddChild(enableAdvertEventsForContentPacks);
    contentPack->AddChild(owned);
    contentPack->AddChild(comingSoon);

    RegisterTweakable(contentPack);
    m_rootFolders.push_back(contentPack);
}

void DevMenu::InitializeDLC() {
    auto dlc = std::make_shared<TweakableFolder>(135, "DLC");

    // Add all DLC tweakables
    auto conflictChecksEnabled = CreateSyncedBool(136, "ConflictChecksEnabled", true);

    RegisterTweakable(conflictChecksEnabled);

    dlc->AddChild(conflictChecksEnabled);

    RegisterTweakable(dlc);
    m_rootFolders.push_back(dlc);
}

void DevMenu::InitializeEditor() {
    auto editor = std::make_shared<TweakableFolder>(137, "Editor");

    // Top-level Editor tweakables
    auto saveLastUndo = CreateSyncedBool(138, "SaveLastUndo", true);
    auto saveTrackEnvironmentSettings = CreateSyncedBool(139, "SaveTrackEnvironmentSettings", false);
    auto printCurrentCameraLocation = CreateSyncedBool(140, "PrintCurrentCameraLocation", true);

    RegisterTweakable(saveLastUndo);
    RegisterTweakable(saveTrackEnvironmentSettings);
    RegisterTweakable(printCurrentCameraLocation);

    editor->AddChild(saveLastUndo);
    editor->AddChild(saveTrackEnvironmentSettings);
    editor->AddChild(printCurrentCameraLocation);

    // Features subfolder
    auto features = std::make_shared<TweakableFolder>(141, "Features");
    auto enableDrivingLineSplitting = CreateSyncedBool(142, "EnableDrivingLineSplitting", true);
    auto enableDrivingLineEvent = CreateSyncedBool(596, "EnableDrivingLineEvent", true);

    RegisterTweakable(enableDrivingLineSplitting);
    RegisterTweakable(enableDrivingLineEvent);

    features->AddChild(enableDrivingLineSplitting);
    features->AddChild(enableDrivingLineEvent);

    RegisterTweakable(features);
    editor->AddChild(features);

    // Additional Editor tweakables
    auto flipDirectionHeightAdjustment = CreateSyncedFloat(149, "FlipDirectionHeightAdjustment", 0.072f, 0.0f, 1.0f);
    auto fakeBikeAmount = CreateSyncedInt(274, "FakeBikeAmount", 7, 0, 100);
    auto simulatedDelayWithGhostsInEditor = CreateSyncedInt(275, "SimulatedDelayWithGhostsInEditor", 400, 0, 5000);
    auto loadHighEndLayer = CreateSyncedBool(353, "LoadHighEndLayer", true);
    auto cloudShadowTiling = CreateSyncedFloat(369, "CloudShadowTiling", 0.0025f, 0.0f, 1.0f);
    auto edgeCollisionFactorMul = CreateSyncedFloat(370, "EdgeCollisionFactorMul", 1.0f, 0.0f, 10.0f);
    auto edgeCollisionNormalPow = CreateSyncedFloat(371, "EdgeCollisionNormalPow", 0.2f, 0.0f, 5.0f);

    RegisterTweakable(flipDirectionHeightAdjustment);
    RegisterTweakable(fakeBikeAmount);
    RegisterTweakable(simulatedDelayWithGhostsInEditor);
    RegisterTweakable(loadHighEndLayer);
    RegisterTweakable(cloudShadowTiling);
    RegisterTweakable(edgeCollisionFactorMul);
    RegisterTweakable(edgeCollisionNormalPow);

    editor->AddChild(flipDirectionHeightAdjustment);
    editor->AddChild(fakeBikeAmount);
    editor->AddChild(simulatedDelayWithGhostsInEditor);
    editor->AddChild(loadHighEndLayer);
    editor->AddChild(cloudShadowTiling);
    editor->AddChild(edgeCollisionFactorMul);
    editor->AddChild(edgeCollisionNormalPow);

    // Overlays subfolder
    auto overlays = std::make_shared<TweakableFolder>(597, "Overlays");
    auto maxCount = CreateSyncedInt(598, "MaxCount", 50, 0, 1000);
    auto maxDistance = CreateSyncedInt(599, "MaxDistance", 50, 0, 1000);
    auto drawOverlayTexts = CreateSyncedBool(600, "DrawOverlayTexts", true);

    RegisterTweakable(maxCount);
    RegisterTweakable(maxDistance);
    RegisterTweakable(drawOverlayTexts);

    overlays->AddChild(maxCount);
    overlays->AddChild(maxDistance);
    overlays->AddChild(drawOverlayTexts);

    RegisterTweakable(overlays);
    editor->AddChild(overlays);

    RegisterTweakable(editor);
    m_rootFolders.push_back(editor);
}

void DevMenu::InitializeMultiplayer() {
    auto multiplayer = std::make_shared<TweakableFolder>(143, "Multiplayer");

    // Top-level Multiplayer tweakables
    auto showMaxPodiumRiders = CreateSyncedBool(144, "ShowMaxPodiumRiders", false);
    auto mpBoosterMaxPower = CreateSyncedFloat(264, "MP_BOOSTER_MAX_POWER", 300.0f, 0.0f, 1000.0f);
    auto enableSwitchFollowedPlayerWhileDriving = CreateSyncedBool(265, "EnableSwitchFollowedPlayerWhileDriving", false);
    auto resetCooldownTicks = CreateSyncedInt(266, "ResetCooldownTicks", 30, 0, 1000);
    auto fakeBikeTrialsDelayTicks = CreateSyncedInt(267, "FakeBikeTrialsDelayTicks", 3, 0, 100);
    auto hiddenRemoteBikeAmount = CreateSyncedInt(276, "HiddenRemoteBikeAmount", 0, 0, 100);
    auto refreshRate = CreateSyncedInt(277, "RefreshRate", 17, 0, 120);
    auto trialsDrivingLineAmount = CreateSyncedInt(278, "TrialsDrivingLineAmount", 1, 0, 100);
    auto followedUpdateMinIntervalTicks = CreateSyncedInt(279, "FollowedUpdateMinIntervalTicks", 120, 0, 1000);
    auto updateIngamePlayerList = CreateSyncedInt(343, "UpdateIngamePlayerList", 61, 0, 1000);
    auto xCrossShowPodiumAfterEachTrack = CreateSyncedBool(350, "XCrossShowPodiumAfterEachTrack", false);
    auto fmxBoostForce = CreateSyncedFloat(433, "FMXBoostForce", 500.0f, 0.0f, 2000.0f);
    auto fmxBoostLengthFactor = CreateSyncedFloat(434, "FMXBoostLengthFactor", 0.00015f, 0.0f, 0.01f);
    auto disconnectOnOutOfSync = CreateSyncedBool(489, "DisconnectOnOutOfSync", true);
    auto ghostFadeOutDistance = CreateSyncedFloat(490, "GhostFadeOutDistance", 25.0f, 0.0f, 200.0f);

    RegisterTweakable(showMaxPodiumRiders);
    RegisterTweakable(mpBoosterMaxPower);
    RegisterTweakable(enableSwitchFollowedPlayerWhileDriving);
    RegisterTweakable(resetCooldownTicks);
    RegisterTweakable(fakeBikeTrialsDelayTicks);
    RegisterTweakable(hiddenRemoteBikeAmount);
    RegisterTweakable(refreshRate);
    RegisterTweakable(trialsDrivingLineAmount);
    RegisterTweakable(followedUpdateMinIntervalTicks);
    RegisterTweakable(updateIngamePlayerList);
    RegisterTweakable(xCrossShowPodiumAfterEachTrack);
    RegisterTweakable(fmxBoostForce);
    RegisterTweakable(fmxBoostLengthFactor);
    RegisterTweakable(disconnectOnOutOfSync);
    RegisterTweakable(ghostFadeOutDistance);

    multiplayer->AddChild(showMaxPodiumRiders);
    multiplayer->AddChild(mpBoosterMaxPower);
    multiplayer->AddChild(enableSwitchFollowedPlayerWhileDriving);
    multiplayer->AddChild(resetCooldownTicks);
    multiplayer->AddChild(fakeBikeTrialsDelayTicks);
    multiplayer->AddChild(hiddenRemoteBikeAmount);
    multiplayer->AddChild(refreshRate);
    multiplayer->AddChild(trialsDrivingLineAmount);
    multiplayer->AddChild(followedUpdateMinIntervalTicks);
    multiplayer->AddChild(updateIngamePlayerList);
    multiplayer->AddChild(xCrossShowPodiumAfterEachTrack);
    multiplayer->AddChild(fmxBoostForce);
    multiplayer->AddChild(fmxBoostLengthFactor);
    multiplayer->AddChild(disconnectOnOutOfSync);
    multiplayer->AddChild(ghostFadeOutDistance);

    // ParameterDefaults subfolder
    auto parameterDefaults = std::make_shared<TweakableFolder>(571, "ParameterDefaults");
    auto heatsPerRace = CreateSyncedInt(572, "HeatsPerRace", 2, 0, 20);
    auto bailoutFinish = CreateSyncedBool(573, "BailoutFinish", false);
    auto fmxTricksEnabled = CreateSyncedBool(574, "FMXTricksEnabled", true);
    auto fmxTricksBoost = CreateSyncedInt(575, "FMXTricksBoost", 0, 0, 100);
    auto noLean = CreateSyncedBool(576, "NoLean", false);
    auto fullThrottle = CreateSyncedBool(577, "FullThrottle", false);
    auto invertControls = CreateSyncedBool(578, "InvertControls", false);
    auto bikeSpeed = CreateSyncedInt(579, "BikeSpeed", 100, 0, 500);
    auto gravity = CreateSyncedInt(580, "Gravity", 100, 0, 500);
    auto invisibleRider = CreateSyncedBool(581, "InvisibleRider", false);
    auto invisibleBike = CreateSyncedBool(582, "InvisibleBike", false);
    auto removeCheckpoints = CreateSyncedBool(583, "RemoveCheckpoints", false);
    auto endingTimeLimit = CreateSyncedInt(584, "EndingTimeLimit", 30, 0, 300);
    auto burningBike = CreateSyncedBool(585, "BurningBike", false);
    auto wheelieMode = CreateSyncedInt(586, "WheelieMode", 0, 0, 10);
    auto lives = CreateSyncedInt(587, "Lives", 500, 0, 10000);
    auto baggieAllowed = CreateSyncedBool(588, "BaggieAllowed", false);
    auto roachAllowed = CreateSyncedBool(589, "RoachAllowed", true);
    auto pitViperAllowed = CreateSyncedBool(590, "PitViperAllowed", true);
    auto foxbatAllowed = CreateSyncedBool(591, "FoxbatAllowed", true);
    auto quadAllowed = CreateSyncedBool(592, "QuadAllowed", true);
    auto bmxAllowed = CreateSyncedBool(593, "BMXAllowed", true);
    auto donkeyAllowed = CreateSyncedBool(594, "DonkeyAllowed", true);
    auto unicornAllowed = CreateSyncedBool(595, "UnicornAllowed", true);

    RegisterTweakable(heatsPerRace);
    RegisterTweakable(bailoutFinish);
    RegisterTweakable(fmxTricksEnabled);
    RegisterTweakable(fmxTricksBoost);
    RegisterTweakable(noLean);
    RegisterTweakable(fullThrottle);
    RegisterTweakable(invertControls);
    RegisterTweakable(bikeSpeed);
    RegisterTweakable(gravity);
    RegisterTweakable(invisibleRider);
    RegisterTweakable(invisibleBike);
    RegisterTweakable(removeCheckpoints);
    RegisterTweakable(endingTimeLimit);
    RegisterTweakable(burningBike);
    RegisterTweakable(wheelieMode);
    RegisterTweakable(lives);
    RegisterTweakable(baggieAllowed);
    RegisterTweakable(roachAllowed);
    RegisterTweakable(pitViperAllowed);
    RegisterTweakable(foxbatAllowed);
    RegisterTweakable(quadAllowed);
    RegisterTweakable(bmxAllowed);
    RegisterTweakable(donkeyAllowed);
    RegisterTweakable(unicornAllowed);

    parameterDefaults->AddChild(heatsPerRace);
    parameterDefaults->AddChild(bailoutFinish);
    parameterDefaults->AddChild(fmxTricksEnabled);
    parameterDefaults->AddChild(fmxTricksBoost);
    parameterDefaults->AddChild(noLean);
    parameterDefaults->AddChild(fullThrottle);
    parameterDefaults->AddChild(invertControls);
    parameterDefaults->AddChild(bikeSpeed);
    parameterDefaults->AddChild(gravity);
    parameterDefaults->AddChild(invisibleRider);
    parameterDefaults->AddChild(invisibleBike);
    parameterDefaults->AddChild(removeCheckpoints);
    parameterDefaults->AddChild(endingTimeLimit);
    parameterDefaults->AddChild(burningBike);
    parameterDefaults->AddChild(wheelieMode);
    parameterDefaults->AddChild(lives);
    parameterDefaults->AddChild(baggieAllowed);
    parameterDefaults->AddChild(roachAllowed);
    parameterDefaults->AddChild(pitViperAllowed);
    parameterDefaults->AddChild(foxbatAllowed);
    parameterDefaults->AddChild(quadAllowed);
    parameterDefaults->AddChild(bmxAllowed);
    parameterDefaults->AddChild(donkeyAllowed);
    parameterDefaults->AddChild(unicornAllowed);

    RegisterTweakable(parameterDefaults);
    multiplayer->AddChild(parameterDefaults);

    // Additional Multiplayer tweakables
    auto estimationMinimumAdvance = CreateSyncedFloat(788, "EstimationMinimumAdvance", 0.5f, 0.0f, 10.0f);
    auto remoteInterpolationCap = CreateSyncedFloat(789, "RemoteInterpolationCap", 120.0f, 0.0f, 500.0f);
    auto delayIncreaseDelay = CreateSyncedFloat(790, "DelayIncreaseDelay", 0.75f, 0.0f, 2.0f);
    auto delayDecreaseDelay = CreateSyncedFloat(791, "DelayDecreaseDelay", 0.998f, 0.0f, 1.0f);

    RegisterTweakable(estimationMinimumAdvance);
    RegisterTweakable(remoteInterpolationCap);
    RegisterTweakable(delayIncreaseDelay);
    RegisterTweakable(delayDecreaseDelay);

    multiplayer->AddChild(estimationMinimumAdvance);
    multiplayer->AddChild(remoteInterpolationCap);
    multiplayer->AddChild(delayIncreaseDelay);
    multiplayer->AddChild(delayDecreaseDelay);

    RegisterTweakable(multiplayer);
    m_rootFolders.push_back(multiplayer);
}

void DevMenu::InitializeEvent() {
    auto event = std::make_shared<TweakableFolder>(145, "Event");

    // Add all Event tweakables
    auto netDelayHigh = CreateSyncedInt(146, "NetDelayHigh", 12, 0, 100);
    auto netDelayMedium = CreateSyncedInt(147, "NetDelayMedium", 6, 0, 100);
    auto netDelayLow = CreateSyncedInt(148, "NetDelayLow", 1, 0, 100);

    RegisterTweakable(netDelayHigh);
    RegisterTweakable(netDelayMedium);
    RegisterTweakable(netDelayLow);

    event->AddChild(netDelayHigh);
    event->AddChild(netDelayMedium);
    event->AddChild(netDelayLow);

    RegisterTweakable(event);
    m_rootFolders.push_back(event);
}

void DevMenu::InitializeFMX() {
    auto fmx = std::make_shared<TweakableFolder>(150, "FMX");
    RegisterTweakable(fmx);
    m_rootFolders.push_back(fmx);
}

void DevMenu::InitializeBike() {
    auto bike = std::make_shared<TweakableFolder>(169, "Bike");

    // Top-level bike parameters
    auto tuneEnabled = CreateSyncedBool(170, "TuneEnabled", false);
    auto newAccelerationEnabled = CreateSyncedBool(171, "newAccelerationEnabled", false);
    auto idNumber = CreateSyncedInt(172, "IdNumber", 3, 0, 10);

    RegisterTweakable(tuneEnabled);
    RegisterTweakable(newAccelerationEnabled);
    RegisterTweakable(idNumber);

    bike->AddChild(tuneEnabled);
    bike->AddChild(newAccelerationEnabled);
    bike->AddChild(idNumber);

    // Engine subfolder
    auto engine = std::make_shared<TweakableFolder>(173, "Engine");
    auto eng_accelMul = CreateSyncedFloat(174, "AccelerationMultiplier", 1.0f, 0.0f, 5.0f);
    auto eng_accelSpeedDiv = CreateSyncedFloat(175, "AccelerationSpeedDivisor", 1.0f, 0.0f, 5.0f);
    auto eng_rpmSeekSpeedMul = CreateSyncedFloat(176, "RpmSeekSpeedMul", 0.0107f, 0.0f, 1.0f);
    auto eng_rpmMin = CreateSyncedFloat(179, "RpmMin", 1980.0f, 0.0f, 10000.0f);
    auto eng_rpmMax = CreateSyncedFloat(186, "RpmMax", 8400.0f, 0.0f, 15000.0f);
    auto eng_rpmMaxAdd = CreateSyncedFloat(187, "RpmMaxAdd", 600.0f, 0.0f, 2000.0f);
    auto eng_rpmMul = CreateSyncedFloat(188, "RpmMul", 0.081f, 0.0f, 1.0f);
    auto eng_rpmAccelCurrent = CreateSyncedFloat(189, "RpmAccelCurrent", 0.991f, 0.0f, 1.0f);
    auto eng_rpmDecelCurrent = CreateSyncedFloat(190, "RpmDecelCurrent", 0.975f, 0.0f, 1.0f);
    auto eng_rpmAccelTarget = CreateSyncedFloat(191, "RpmAccelTarget", 0.025f, 0.0f, 1.0f);
    auto eng_rpmDecelTarget = CreateSyncedFloat(192, "RpmDecelTarget", 0.7f, 0.0f, 1.0f);

    RegisterTweakable(eng_accelMul);
    RegisterTweakable(eng_accelSpeedDiv);
    RegisterTweakable(eng_rpmSeekSpeedMul);
    RegisterTweakable(eng_rpmMin);
    RegisterTweakable(eng_rpmMax);
    RegisterTweakable(eng_rpmMaxAdd);
    RegisterTweakable(eng_rpmMul);
    RegisterTweakable(eng_rpmAccelCurrent);
    RegisterTweakable(eng_rpmDecelCurrent);
    RegisterTweakable(eng_rpmAccelTarget);
    RegisterTweakable(eng_rpmDecelTarget);

    engine->AddChild(eng_accelMul);
    engine->AddChild(eng_accelSpeedDiv);
    engine->AddChild(eng_rpmSeekSpeedMul);
    engine->AddChild(eng_rpmMin);
    engine->AddChild(eng_rpmMax);
    engine->AddChild(eng_rpmMaxAdd);
    engine->AddChild(eng_rpmMul);
    engine->AddChild(eng_rpmAccelCurrent);
    engine->AddChild(eng_rpmDecelCurrent);
    engine->AddChild(eng_rpmAccelTarget);
    engine->AddChild(eng_rpmDecelTarget);

    RegisterTweakable(engine);
    bike->AddChild(engine);

    // Transmission subfolder
    auto transmission = std::make_shared<TweakableFolder>(177, "Transmission");
    auto trans_rpmClutch = CreateSyncedFloat(178, "RpmClutch", 0.97f, 0.0f, 1.0f);
    auto trans_rpmShiftDown = CreateSyncedFloat(180, "RpmShiftDown", 6120.0f, 0.0f, 15000.0f);
    auto trans_rpmGearDiv1 = CreateSyncedFloat(181, "RpmGearDiv1", 1.99f, 0.0f, 10.0f);
    auto trans_rpmGearDiv2 = CreateSyncedFloat(182, "RpmGearDiv2", 2.46f, 0.0f, 10.0f);
    auto trans_rpmGearDiv3 = CreateSyncedFloat(183, "RpmGearDiv3", 2.56f, 0.0f, 10.0f);
    auto trans_rpmGearDiv4 = CreateSyncedFloat(184, "RpmGearDiv4", 2.66f, 0.0f, 10.0f);
    auto trans_rpmGearDiv5 = CreateSyncedFloat(185, "RpmGearDiv5", 3.0f, 0.0f, 10.0f);
    auto trans_shiftLoadReduce = CreateSyncedFloat(193, "ShiftLoadReduce", 0.97f, 0.0f, 1.0f);

    RegisterTweakable(trans_rpmClutch);
    RegisterTweakable(trans_rpmShiftDown);
    RegisterTweakable(trans_rpmGearDiv1);
    RegisterTweakable(trans_rpmGearDiv2);
    RegisterTweakable(trans_rpmGearDiv3);
    RegisterTweakable(trans_rpmGearDiv4);
    RegisterTweakable(trans_rpmGearDiv5);
    RegisterTweakable(trans_shiftLoadReduce);

    transmission->AddChild(trans_rpmClutch);
    transmission->AddChild(trans_rpmShiftDown);
    transmission->AddChild(trans_rpmGearDiv1);
    transmission->AddChild(trans_rpmGearDiv2);
    transmission->AddChild(trans_rpmGearDiv3);
    transmission->AddChild(trans_rpmGearDiv4);
    transmission->AddChild(trans_rpmGearDiv5);
    transmission->AddChild(trans_shiftLoadReduce);

    RegisterTweakable(transmission);
    bike->AddChild(transmission);

    // Properties subfolder
    auto properties = std::make_shared<TweakableFolder>(194, "Properties");
    auto prop_accelPower = CreateSyncedFloat(195, "AccelerationPower", 28.0f, 0.0f, 100.0f);
    auto prop_accelBrake = CreateSyncedFloat(196, "AccelerationBrake", 27.0f, 0.0f, 100.0f);
    auto prop_accelForce = CreateSyncedFloat(197, "AccelerationForce", -0.25f, -10.0f, 10.0f);
    auto prop_engineDamping = CreateSyncedFloat(198, "EngineDamping", 0.0f, 0.0f, 10.0f);
    auto prop_maxVelocity = CreateSyncedFloat(199, "MaximumVelocity", 20.0f, 0.0f, 100.0f);
    auto prop_massFactor = CreateSyncedFloat(200, "MassFactor", 1.0f, 0.0f, 10.0f);
    auto prop_brakePowerFront = CreateSyncedFloat(201, "BrakePowerFront", 1.25f, 0.0f, 10.0f);
    auto prop_brakePowerBack = CreateSyncedFloat(207, "BrakePowerBack", 1.25f, 0.0f, 10.0f);
    auto prop_accelPowerQuadMP = CreateSyncedFloat(260, "AccelerationPowerQuadMP", 28.0f, 0.0f, 100.0f);
    auto prop_accelBrakeQuadMP = CreateSyncedFloat(261, "AccelerationBrakeQuadMP", 27.0f, 0.0f, 100.0f);
    auto prop_unicornWalkMul = CreateSyncedFloat(268, "UnicornWalkMultiplier", 1.0f, 0.0f, 10.0f);
    auto prop_unicornRunMul = CreateSyncedFloat(269, "UnicornRunMultiplier", 0.4f, 0.0f, 10.0f);
    auto prop_unicornGallopMul = CreateSyncedFloat(270, "UnicornGallopMultiplier", 0.14f, 0.0f, 10.0f);
    auto prop_unicornRunSpeed = CreateSyncedFloat(271, "UnicornRunSpeed", 42.0f, 0.0f, 200.0f);
    auto prop_unicornGallopSpeed = CreateSyncedFloat(272, "UnicornGallopSpeed", 125.0f, 0.0f, 300.0f);
    auto prop_unicornFireInitValue = CreateSyncedFloat(273, "UnicornFireInitValue", 0.0f, 0.0f, 100.0f);

    RegisterTweakable(prop_accelPower);
    RegisterTweakable(prop_accelBrake);
    RegisterTweakable(prop_accelForce);
    RegisterTweakable(prop_engineDamping);
    RegisterTweakable(prop_maxVelocity);
    RegisterTweakable(prop_massFactor);
    RegisterTweakable(prop_brakePowerFront);
    RegisterTweakable(prop_brakePowerBack);
    RegisterTweakable(prop_accelPowerQuadMP);
    RegisterTweakable(prop_accelBrakeQuadMP);
    RegisterTweakable(prop_unicornWalkMul);
    RegisterTweakable(prop_unicornRunMul);
    RegisterTweakable(prop_unicornGallopMul);
    RegisterTweakable(prop_unicornRunSpeed);
    RegisterTweakable(prop_unicornGallopSpeed);
    RegisterTweakable(prop_unicornFireInitValue);

    properties->AddChild(prop_accelPower);
    properties->AddChild(prop_accelBrake);
    properties->AddChild(prop_accelForce);
    properties->AddChild(prop_engineDamping);
    properties->AddChild(prop_maxVelocity);
    properties->AddChild(prop_massFactor);
    properties->AddChild(prop_brakePowerFront);
    properties->AddChild(prop_brakePowerBack);
    properties->AddChild(prop_accelPowerQuadMP);
    properties->AddChild(prop_accelBrakeQuadMP);
    properties->AddChild(prop_unicornWalkMul);
    properties->AddChild(prop_unicornRunMul);
    properties->AddChild(prop_unicornGallopMul);
    properties->AddChild(prop_unicornRunSpeed);
    properties->AddChild(prop_unicornGallopSpeed);
    properties->AddChild(prop_unicornFireInitValue);

    RegisterTweakable(properties);
    bike->AddChild(properties);

    // Suspension subfolder
    auto suspension = std::make_shared<TweakableFolder>(202, "Suspension");
    auto susp_frontSpringSoftness = CreateSyncedFloat(203, "FrontSpringSoftness", 4000.0f, 0.0f, 20000.0f);
    auto susp_frontSpringDamping = CreateSyncedFloat(204, "FrontSpringDamping", 0.001f, 0.0f, 1.0f);
    auto susp_frontWheelSpringSoftness = CreateSyncedFloat(205, "FrontWheelSpringSoftness", 0.325f, 0.0f, 5.0f);
    auto susp_frontWheelSpringDamping = CreateSyncedFloat(206, "FrontWheelSpringDamping", 0.2f, 0.0f, 5.0f);
    auto susp_backSpringSoftness = CreateSyncedFloat(208, "BackSpringSoftness", 4000.0f, 0.0f, 20000.0f);
    auto susp_backSpringDamping = CreateSyncedFloat(209, "BackSpringDamping", 0.001f, 0.0f, 1.0f);
    auto susp_backWheelSpringSoftness = CreateSyncedFloat(210, "BackWheelSpringSoftness", 0.2f, 0.0f, 5.0f);
    auto susp_backWheelSpringDamping = CreateSyncedFloat(211, "BackWheelSpringDamping", 0.3f, 0.0f, 5.0f);
    auto susp_backWheelSpring2Softness = CreateSyncedFloat(212, "BackWheelSpring2Softness", 0.0f, 0.0f, 5.0f);
    auto susp_backWheelSpring2Damping = CreateSyncedFloat(213, "BackWheelSpring2Damping", 1.0f, 0.0f, 5.0f);

    RegisterTweakable(susp_frontSpringSoftness);
    RegisterTweakable(susp_frontSpringDamping);
    RegisterTweakable(susp_frontWheelSpringSoftness);
    RegisterTweakable(susp_frontWheelSpringDamping);
    RegisterTweakable(susp_backSpringSoftness);
    RegisterTweakable(susp_backSpringDamping);
    RegisterTweakable(susp_backWheelSpringSoftness);
    RegisterTweakable(susp_backWheelSpringDamping);
    RegisterTweakable(susp_backWheelSpring2Softness);
    RegisterTweakable(susp_backWheelSpring2Damping);

    suspension->AddChild(susp_frontSpringSoftness);
    suspension->AddChild(susp_frontSpringDamping);
    suspension->AddChild(susp_frontWheelSpringSoftness);
    suspension->AddChild(susp_frontWheelSpringDamping);
    suspension->AddChild(susp_backSpringSoftness);
    suspension->AddChild(susp_backSpringDamping);
    suspension->AddChild(susp_backWheelSpringSoftness);
    suspension->AddChild(susp_backWheelSpringDamping);
    suspension->AddChild(susp_backWheelSpring2Softness);
    suspension->AddChild(susp_backWheelSpring2Damping);

    RegisterTweakable(suspension);
    bike->AddChild(suspension);

    RegisterTweakable(bike);
    m_rootFolders.push_back(bike);
}

void DevMenu::InitializeRider() {
    auto rider = std::make_shared<TweakableFolder>(214, "Rider");

    // TuneEnabled
    auto tuneEnabled = CreateSyncedBool(215, "TuneEnabled", false);
    RegisterTweakable(tuneEnabled);
    rider->AddChild(tuneEnabled);

    // Properties subfolder
    auto properties = std::make_shared<TweakableFolder>(216, "Properties");
    auto massFactor = CreateSyncedFloat(217, "MassFactor", 1.0f, 0.0f, 10.0f);

    RegisterTweakable(massFactor);
    properties->AddChild(massFactor);

    RegisterTweakable(properties);
    rider->AddChild(properties);

    RegisterTweakable(rider);
    m_rootFolders.push_back(rider);
}

void DevMenu::InitializeVibra() {
    auto vibra = std::make_shared<TweakableFolder>(243, "Vibra");

    // Add all Vibra tweakables
    auto brakeToLT = CreateSyncedFloat(244, "BrakeToLT", 0.5f, 0.0f, 2.0f);
    auto leftAndRightToLT = CreateSyncedFloat(245, "LeftAndRightToLT", 1.09f, 0.0f, 5.0f);
    auto loadToRT = CreateSyncedFloat(246, "LoadToRT", 0.5f, 0.0f, 2.0f);
    auto leftToRT = CreateSyncedFloat(247, "LeftToRT", 1.35f, 0.0f, 5.0f);
    auto airThreshold = CreateSyncedInt(248, "AirThreshold", 3, 0, 20);
    auto rtStartRPM = CreateSyncedFloat(249, "RTStartRPM", 0.0f, 0.0f, 1.0f);
    auto rtStartLoad = CreateSyncedFloat(250, "RTStartLoad", 0.59f, 0.0f, 2.0f);
    auto rtGasAddition = CreateSyncedFloat(251, "RTGasAddition", 0.0f, 0.0f, 2.0f);
    auto rtBackWheelStart = CreateSyncedFloat(252, "RTBackWheelStart", 0.2f, 0.0f, 2.0f);
    auto rtSlippingClutch = CreateSyncedFloat(253, "RTSlippingClutch", 0.2f, 0.0f, 2.0f);

    RegisterTweakable(brakeToLT);
    RegisterTweakable(leftAndRightToLT);
    RegisterTweakable(loadToRT);
    RegisterTweakable(leftToRT);
    RegisterTweakable(airThreshold);
    RegisterTweakable(rtStartRPM);
    RegisterTweakable(rtStartLoad);
    RegisterTweakable(rtGasAddition);
    RegisterTweakable(rtBackWheelStart);
    RegisterTweakable(rtSlippingClutch);

    vibra->AddChild(brakeToLT);
    vibra->AddChild(leftAndRightToLT);
    vibra->AddChild(loadToRT);
    vibra->AddChild(leftToRT);
    vibra->AddChild(airThreshold);
    vibra->AddChild(rtStartRPM);
    vibra->AddChild(rtStartLoad);
    vibra->AddChild(rtGasAddition);
    vibra->AddChild(rtBackWheelStart);
    vibra->AddChild(rtSlippingClutch);

    RegisterTweakable(vibra);
    m_rootFolders.push_back(vibra);
}

void DevMenu::InitializeSoundSystem() {
    auto soundSystem = std::make_shared<TweakableFolder>(258, "SoundSystem");

    auto freefallYellHeadVelocity = CreateSyncedFloat(259, "freefallYellHeadVelocity", 10.0f, 0.0f, 50.0f);
    auto audioDuckingFadeStep = CreateSyncedFloat(557, "audioDuckingFadeStep", 0.0121f, 0.0f, 1.0f);
    auto audioDuckingFactor = CreateSyncedFloat(558, "audioDuckingFactor", 0.39f, 0.0f, 1.0f);
    auto audioDuckingExtraTicks = CreateSyncedInt(559, "audioDuckingExtraTicks", 10, 0, 100);
    auto forceSynchronousProcessing = CreateSyncedBool(626, "ForceSynchronousProcessing", false);
    auto maxCommands = CreateSyncedInt(627, "MaxCommands", 400, 0, 2000);
    auto printCommandName = CreateSyncedBool(628, "PrintCommandName", true);

    RegisterTweakable(freefallYellHeadVelocity);
    RegisterTweakable(audioDuckingFadeStep);
    RegisterTweakable(audioDuckingFactor);
    RegisterTweakable(audioDuckingExtraTicks);
    RegisterTweakable(forceSynchronousProcessing);
    RegisterTweakable(maxCommands);
    RegisterTweakable(printCommandName);

    soundSystem->AddChild(freefallYellHeadVelocity);
    soundSystem->AddChild(audioDuckingFadeStep);
    soundSystem->AddChild(audioDuckingFactor);
    soundSystem->AddChild(audioDuckingExtraTicks);
    soundSystem->AddChild(forceSynchronousProcessing);
    soundSystem->AddChild(maxCommands);
    soundSystem->AddChild(printCommandName);

    RegisterTweakable(soundSystem);
    m_rootFolders.push_back(soundSystem);
}

void DevMenu::InitializeReplayCRC() {
    auto replayCRC = std::make_shared<TweakableFolder>(280, "Replay CRC check");

    auto active = CreateSyncedBool(281, "active", false);

    RegisterTweakable(active);
    replayCRC->AddChild(active);

    RegisterTweakable(replayCRC);
    m_rootFolders.push_back(replayCRC);
}

void DevMenu::InitializeUtils() {
    auto utils = std::make_shared<TweakableFolder>(282, "Utils");

    auto replayCameraEnabled = CreateSyncedBool(283, "ReplayCameraEnabled", true);
    auto inGameCountersHidden = CreateSyncedBool(605, "InGameCountersHidden", false);

    RegisterTweakable(replayCameraEnabled);
    RegisterTweakable(inGameCountersHidden);

    utils->AddChild(replayCameraEnabled);
    utils->AddChild(inGameCountersHidden);

    RegisterTweakable(utils);
    m_rootFolders.push_back(utils);
}

void DevMenu::InitializeReplayCamera() {
    auto replayCamera = std::make_shared<TweakableFolder>(284, "ReplayCamera");

    // FollowCamera subfolder
    auto followCamera = std::make_shared<TweakableFolder>(285, "FollowCamera");
    auto fc_fovSpeed = CreateSyncedFloat(286, "FOVSpeed", 30.0f, 0.0f, 100.0f);
    auto fc_fovMin = CreateSyncedFloat(287, "FOVMin", 20.0f, 0.0f, 180.0f);
    auto fc_fovMax = CreateSyncedFloat(288, "FOVMax", 90.0f, 0.0f, 180.0f);
    auto fc_panSpeed = CreateSyncedFloat(303, "PanSpeed", 6.0f, 0.0f, 50.0f);
    auto fc_panMin = CreateSyncedFloat(304, "PanMin", -5.0f, -50.0f, 50.0f);
    auto fc_panMax = CreateSyncedFloat(305, "PanMax", 0.0f, -50.0f, 50.0f);
    auto fc_distanceSpeed = CreateSyncedFloat(306, "DistanceSpeed", 10.0f, 0.0f, 100.0f);
    auto fc_distanceMin = CreateSyncedFloat(307, "DistanceMin", 2.0f, 0.0f, 100.0f);
    auto fc_distanceMax = CreateSyncedFloat(308, "DistanceMax", 50.0f, 0.0f, 200.0f);
    auto fc_rollSpeed = CreateSyncedFloat(309, "RollSpeed", 50.0f, 0.0f, 200.0f);
    auto fc_heightSpeed = CreateSyncedFloat(310, "HeightSpeed", 15.0f, 0.0f, 100.0f);
    auto fc_heightMin = CreateSyncedFloat(311, "HeightMin", -10.0f, -50.0f, 50.0f);
    auto fc_heightMax = CreateSyncedFloat(312, "HeightMax", 10.0f, -50.0f, 50.0f);
    auto fc_orbitSpeed = CreateSyncedFloat(313, "OrbitSpeed", 180.0f, 0.0f, 360.0f);
    auto fc_orbitMin = CreateSyncedFloat(314, "OrbitMin", -180.0f, -360.0f, 360.0f);
    auto fc_orbitMax = CreateSyncedFloat(315, "OrbitMax", 180.0f, -360.0f, 360.0f);
    auto fc_sensitivityX = CreateSyncedFloat(316, "SensitivityX", -0.1f, -2.0f, 2.0f);
    auto fc_sensitivityY = CreateSyncedFloat(317, "SensitivityY", -0.1f, -2.0f, 2.0f);
    auto fc_sensitivityZ = CreateSyncedFloat(318, "SensitivityZ", -0.1f, -2.0f, 2.0f);
    auto fc_apertureTime = CreateSyncedFloat(319, "ApertureTime", 0.5f, 0.0f, 5.0f);
    auto fc_filmWidth = CreateSyncedFloat(320, "FilmWidth", 0.1f, 0.0f, 5.0f);
    auto fc_nearBlur = CreateSyncedFloat(321, "NearBlur", 0.5f, 0.0f, 10.0f);
    auto fc_farBlur = CreateSyncedFloat(322, "FarBlur", 50.0f, 0.0f, 200.0f);
    auto fc_positionInterpolation = CreateSyncedFloat(323, "PositionInterpolation", 0.17f, 0.0f, 1.0f);
    auto fc_targetInterpolation = CreateSyncedFloat(324, "TargetInterpolation", 0.05f, 0.0f, 1.0f);

    RegisterTweakable(fc_fovSpeed);
    RegisterTweakable(fc_fovMin);
    RegisterTweakable(fc_fovMax);
    RegisterTweakable(fc_panSpeed);
    RegisterTweakable(fc_panMin);
    RegisterTweakable(fc_panMax);
    RegisterTweakable(fc_distanceSpeed);
    RegisterTweakable(fc_distanceMin);
    RegisterTweakable(fc_distanceMax);
    RegisterTweakable(fc_rollSpeed);
    RegisterTweakable(fc_heightSpeed);
    RegisterTweakable(fc_heightMin);
    RegisterTweakable(fc_heightMax);
    RegisterTweakable(fc_orbitSpeed);
    RegisterTweakable(fc_orbitMin);
    RegisterTweakable(fc_orbitMax);
    RegisterTweakable(fc_sensitivityX);
    RegisterTweakable(fc_sensitivityY);
    RegisterTweakable(fc_sensitivityZ);
    RegisterTweakable(fc_apertureTime);
    RegisterTweakable(fc_filmWidth);
    RegisterTweakable(fc_nearBlur);
    RegisterTweakable(fc_farBlur);
    RegisterTweakable(fc_positionInterpolation);
    RegisterTweakable(fc_targetInterpolation);

    followCamera->AddChild(fc_fovSpeed);
    followCamera->AddChild(fc_fovMin);
    followCamera->AddChild(fc_fovMax);
    followCamera->AddChild(fc_panSpeed);
    followCamera->AddChild(fc_panMin);
    followCamera->AddChild(fc_panMax);
    followCamera->AddChild(fc_distanceSpeed);
    followCamera->AddChild(fc_distanceMin);
    followCamera->AddChild(fc_distanceMax);
    followCamera->AddChild(fc_rollSpeed);
    followCamera->AddChild(fc_heightSpeed);
    followCamera->AddChild(fc_heightMin);
    followCamera->AddChild(fc_heightMax);
    followCamera->AddChild(fc_orbitSpeed);
    followCamera->AddChild(fc_orbitMin);
    followCamera->AddChild(fc_orbitMax);
    followCamera->AddChild(fc_sensitivityX);
    followCamera->AddChild(fc_sensitivityY);
    followCamera->AddChild(fc_sensitivityZ);
    followCamera->AddChild(fc_apertureTime);
    followCamera->AddChild(fc_filmWidth);
    followCamera->AddChild(fc_nearBlur);
    followCamera->AddChild(fc_farBlur);
    followCamera->AddChild(fc_positionInterpolation);
    followCamera->AddChild(fc_targetInterpolation);

    RegisterTweakable(followCamera);
    replayCamera->AddChild(followCamera);

    // SpectatorCamera subfolder
    auto spectatorCamera = std::make_shared<TweakableFolder>(289, "SpectatorCamera");
    auto sc_panSpeed = CreateSyncedFloat(290, "PanSpeed", 10.0f, 0.0f, 50.0f);
    auto sc_panMin = CreateSyncedFloat(291, "PanMin", 0.0f, -50.0f, 50.0f);
    auto sc_panMax = CreateSyncedFloat(292, "PanMax", 0.0f, -50.0f, 50.0f);
    auto sc_distanceSpeed = CreateSyncedFloat(293, "DistanceSpeed", 6.0f, 0.0f, 100.0f);
    auto sc_distanceMin = CreateSyncedFloat(294, "DistanceMin", 5.0f, 0.0f, 100.0f);
    auto sc_distanceMax = CreateSyncedFloat(295, "DistanceMax", 10.0f, 0.0f, 200.0f);
    auto sc_heightSpeed = CreateSyncedFloat(296, "HeightSpeed", 15.0f, 0.0f, 100.0f);
    auto sc_heightMin = CreateSyncedFloat(297, "HeightMin", 0.0f, -50.0f, 50.0f);
    auto sc_heightMax = CreateSyncedFloat(298, "HeightMax", 0.0f, -50.0f, 50.0f);
    auto sc_orbitSpeed = CreateSyncedFloat(299, "OrbitSpeed", 75.0f, 0.0f, 360.0f);
    auto sc_orbitMin = CreateSyncedFloat(300, "OrbitMin", -30.0f, -360.0f, 360.0f);
    auto sc_orbitMax = CreateSyncedFloat(301, "OrbitMax", 5.0f, -360.0f, 360.0f);
    auto sc_resetOnStickRelease = CreateSyncedBool(302, "ResetOnStickRelease", true);

    RegisterTweakable(sc_panSpeed);
    RegisterTweakable(sc_panMin);
    RegisterTweakable(sc_panMax);
    RegisterTweakable(sc_distanceSpeed);
    RegisterTweakable(sc_distanceMin);
    RegisterTweakable(sc_distanceMax);
    RegisterTweakable(sc_heightSpeed);
    RegisterTweakable(sc_heightMin);
    RegisterTweakable(sc_heightMax);
    RegisterTweakable(sc_orbitSpeed);
    RegisterTweakable(sc_orbitMin);
    RegisterTweakable(sc_orbitMax);
    RegisterTweakable(sc_resetOnStickRelease);

    spectatorCamera->AddChild(sc_panSpeed);
    spectatorCamera->AddChild(sc_panMin);
    spectatorCamera->AddChild(sc_panMax);
    spectatorCamera->AddChild(sc_distanceSpeed);
    spectatorCamera->AddChild(sc_distanceMin);
    spectatorCamera->AddChild(sc_distanceMax);
    spectatorCamera->AddChild(sc_heightSpeed);
    spectatorCamera->AddChild(sc_heightMin);
    spectatorCamera->AddChild(sc_heightMax);
    spectatorCamera->AddChild(sc_orbitSpeed);
    spectatorCamera->AddChild(sc_orbitMin);
    spectatorCamera->AddChild(sc_orbitMax);
    spectatorCamera->AddChild(sc_resetOnStickRelease);

    RegisterTweakable(spectatorCamera);
    replayCamera->AddChild(spectatorCamera);

    // FreeCamera subfolder
    auto freeCamera = std::make_shared<TweakableFolder>(325, "FreeCamera");
    auto frc_fovSpeed = CreateSyncedFloat(326, "FOVSpeed", 30.0f, 0.0f, 100.0f);
    auto frc_fovMin = CreateSyncedFloat(327, "FOVMin", 20.0f, 0.0f, 180.0f);
    auto frc_fovMax = CreateSyncedFloat(328, "FOVMax", 90.0f, 0.0f, 180.0f);
    auto frc_moveSpeed = CreateSyncedFloat(329, "MoveSpeed", 20.0f, 0.0f, 200.0f);
    auto frc_moveMin = CreateSyncedFloat(330, "MoveMin", -50.0f, -200.0f, 200.0f);
    auto frc_moveMax = CreateSyncedFloat(331, "MoveMax", 50.0f, -200.0f, 200.0f);
    auto frc_turnSpeed = CreateSyncedFloat(332, "TurnSpeed", 180.0f, 0.0f, 360.0f);
    auto frc_turnMin = CreateSyncedFloat(333, "TurnMin", -180.0f, -360.0f, 360.0f);
    auto frc_turnMax = CreateSyncedFloat(334, "TurnMax", 180.0f, -360.0f, 360.0f);
    auto frc_rollSpeed = CreateSyncedFloat(335, "RollSpeed", 50.0f, 0.0f, 200.0f);
    auto frc_sensitivityX = CreateSyncedFloat(336, "SensitivityX", 0.1f, -2.0f, 2.0f);
    auto frc_sensitivityY = CreateSyncedFloat(337, "SensitivityY", -0.1f, -2.0f, 2.0f);
    auto frc_sensitivityZ = CreateSyncedFloat(338, "SensitivityZ", -0.1f, -2.0f, 2.0f);
    auto frc_positionInterpolation = CreateSyncedFloat(339, "PositionInterpolation", 0.2f, 0.0f, 1.0f);
    auto frc_targetInterpolation = CreateSyncedFloat(340, "TargetInterpolation", 0.2f, 0.0f, 1.0f);

    RegisterTweakable(frc_fovSpeed);
    RegisterTweakable(frc_fovMin);
    RegisterTweakable(frc_fovMax);
    RegisterTweakable(frc_moveSpeed);
    RegisterTweakable(frc_moveMin);
    RegisterTweakable(frc_moveMax);
    RegisterTweakable(frc_turnSpeed);
    RegisterTweakable(frc_turnMin);
    RegisterTweakable(frc_turnMax);
    RegisterTweakable(frc_rollSpeed);
    RegisterTweakable(frc_sensitivityX);
    RegisterTweakable(frc_sensitivityY);
    RegisterTweakable(frc_sensitivityZ);
    RegisterTweakable(frc_positionInterpolation);
    RegisterTweakable(frc_targetInterpolation);

    freeCamera->AddChild(frc_fovSpeed);
    freeCamera->AddChild(frc_fovMin);
    freeCamera->AddChild(frc_fovMax);
    freeCamera->AddChild(frc_moveSpeed);
    freeCamera->AddChild(frc_moveMin);
    freeCamera->AddChild(frc_moveMax);
    freeCamera->AddChild(frc_turnSpeed);
    freeCamera->AddChild(frc_turnMin);
    freeCamera->AddChild(frc_turnMax);
    freeCamera->AddChild(frc_rollSpeed);
    freeCamera->AddChild(frc_sensitivityX);
    freeCamera->AddChild(frc_sensitivityY);
    freeCamera->AddChild(frc_sensitivityZ);
    freeCamera->AddChild(frc_positionInterpolation);
    freeCamera->AddChild(frc_targetInterpolation);

    RegisterTweakable(freeCamera);
    replayCamera->AddChild(freeCamera);

    // ReplayCamera-level parameters
    auto replayCameraYOffset = CreateSyncedFloat(341, "ReplayCameraYOffset", 1.65f, -10.0f, 10.0f);

    RegisterTweakable(replayCameraYOffset);
    replayCamera->AddChild(replayCameraYOffset);

    RegisterTweakable(replayCamera);
    m_rootFolders.push_back(replayCamera);
}

void DevMenu::InitializePhysics() {
    auto physics = std::make_shared<TweakableFolder>(345, "Physics");

    auto breakEffectLoadSmall = CreateSyncedInt(346, "breakEffectLoadSmall", 4, 0, 100);
    auto breakEffectLoadMedium = CreateSyncedInt(347, "breakEffectLoadMedium", 10, 0, 100);
    auto breakEffectCooldownTicks = CreateSyncedInt(348, "breakEffectCooldownTicks", 60, 0, 1000);
    auto superCrossPhysicsActivationDistance = CreateSyncedInt(349, "SuperCrossPhysicsActivationDistance", 20, 0, 200);

    RegisterTweakable(breakEffectLoadSmall);
    RegisterTweakable(breakEffectLoadMedium);
    RegisterTweakable(breakEffectCooldownTicks);
    RegisterTweakable(superCrossPhysicsActivationDistance);

    physics->AddChild(breakEffectLoadSmall);
    physics->AddChild(breakEffectLoadMedium);
    physics->AddChild(breakEffectCooldownTicks);
    physics->AddChild(superCrossPhysicsActivationDistance);

    RegisterTweakable(physics);
    m_rootFolders.push_back(physics);
}

void DevMenu::InitializeFrameSkipper() {
    auto frameSkipper = std::make_shared<TweakableFolder>(351, "FrameSkipper");

    auto skipAdditionalTicksOnReset = CreateSyncedInt(352, "SkipAdditionalTicksOnReset", 10, 0, 100);
    auto pressLBForDelayMS = CreateSyncedInt(488, "PressLBForDelayMS", 0, 0, 10000);
    auto enabled = CreateSyncedBool(520, "enabled", true);
    auto forceSkipperOn = CreateSyncedBool(521, "forceSkipperOn", false);
    auto forceStrictMode = CreateSyncedBool(522, "forceStrictMode", false);
    auto maxSkippedFrames = CreateSyncedInt(523, "maxSkippedFrames", 4, 0, 20);
    auto maxSkippedFramesUGC = CreateSyncedInt(524, "maxSkippedFramesUGC", 4, 0, 20);
    auto maxSkippedFramesInFastForward = CreateSyncedInt(525, "maxSkippedFramesInFastForward", 4, 0, 20);
    auto rateIncreaseDelay = CreateSyncedInt(526, "rateIncreaseDelay", 10, 0, 100);
    auto maxLateFrames = CreateSyncedInt(527, "maxLateFrames", 2, 0, 20);
    auto lateFramesDecayTime = CreateSyncedInt(528, "lateFramesDecayTime", 6, 0, 100);
    auto maxMissedFramesCumulative = CreateSyncedInt(529, "maxMissedFramesCumulative", 20, 0, 200);
    auto waitThreshold = CreateSyncedInt(530, "waitThreshold", -500000, -5000000, 5000000);
    auto framesBetweenSlowdowns = CreateSyncedInt(531, "framesBetweenSlowdowns", 10, 0, 100);
    auto speedUpThreshold = CreateSyncedInt(532, "speedUpThreshold", 50000, -1000000, 1000000);
    auto stopSpeedUpThreshold = CreateSyncedInt(533, "stopSpeedUpThreshold", -30000, -1000000, 1000000);
    auto catchUpThreshold = CreateSyncedInt(534, "catchUpThreshold", 500000, 0, 5000000);
    auto maxUnrenderedFrames = CreateSyncedInt(535, "maxUnrenderedFrames", 5, 0, 20);
    auto catchUpMaxRatio = CreateSyncedFloat(536, "catchUpMaxRatio", 0.15f, 0.0f, 1.0f);
    auto catchUpMinRatio = CreateSyncedFloat(537, "catchUpMinRatio", 0.05f, 0.0f, 1.0f);
    auto catchUpRatioIncreaseRate = CreateSyncedFloat(538, "catchUpRatioIncreaseRate", 0.75f, 0.0f, 5.0f);
    auto fakeFasterWithSkipping = CreateSyncedBool(539, "fakeFasterWithSkipping", true);
    auto fakeRenderDelay = CreateSyncedInt(540, "fakeRenderDelay", 0, 0, 10000);
    auto winMissReportTime = CreateSyncedInt(723, "winMissReportTime", 2, 0, 100);
    auto winAheadReportTime = CreateSyncedInt(724, "winAheadReportTime", 10, 0, 100);
    auto targetFrameRate = CreateSyncedInt(725, "targetFrameRate", 60, 1, 240);
    auto forceVblankWaitInterval = CreateSyncedInt(726, "forceVblankWaitInterval", 30, 0, 100);
    auto vsyncWorks = CreateSyncedBool(727, "vsyncWorks", true);
    auto maxValidFPS = CreateSyncedInt(728, "maxValidFPS", 63, 1, 240);
    auto minValidFPS = CreateSyncedInt(729, "minValidFPS", 57, 1, 240);
    auto fpsCheckInterval = CreateSyncedInt(730, "FPSCheckInterval", 2, 0, 100);
    auto abnormalFramesTolerated = CreateSyncedInt(731, "abnormalFramesTolerated", 4, 0, 100);

    RegisterTweakable(skipAdditionalTicksOnReset);
    RegisterTweakable(pressLBForDelayMS);
    RegisterTweakable(enabled);
    RegisterTweakable(forceSkipperOn);
    RegisterTweakable(forceStrictMode);
    RegisterTweakable(maxSkippedFrames);
    RegisterTweakable(maxSkippedFramesUGC);
    RegisterTweakable(maxSkippedFramesInFastForward);
    RegisterTweakable(rateIncreaseDelay);
    RegisterTweakable(maxLateFrames);
    RegisterTweakable(lateFramesDecayTime);
    RegisterTweakable(maxMissedFramesCumulative);
    RegisterTweakable(waitThreshold);
    RegisterTweakable(framesBetweenSlowdowns);
    RegisterTweakable(speedUpThreshold);
    RegisterTweakable(stopSpeedUpThreshold);
    RegisterTweakable(catchUpThreshold);
    RegisterTweakable(maxUnrenderedFrames);
    RegisterTweakable(catchUpMaxRatio);
    RegisterTweakable(catchUpMinRatio);
    RegisterTweakable(catchUpRatioIncreaseRate);
    RegisterTweakable(fakeFasterWithSkipping);
    RegisterTweakable(fakeRenderDelay);
    RegisterTweakable(winMissReportTime);
    RegisterTweakable(winAheadReportTime);
    RegisterTweakable(targetFrameRate);
    RegisterTweakable(forceVblankWaitInterval);
    RegisterTweakable(vsyncWorks);
    RegisterTweakable(maxValidFPS);
    RegisterTweakable(minValidFPS);
    RegisterTweakable(fpsCheckInterval);
    RegisterTweakable(abnormalFramesTolerated);

    frameSkipper->AddChild(skipAdditionalTicksOnReset);
    frameSkipper->AddChild(pressLBForDelayMS);
    frameSkipper->AddChild(enabled);
    frameSkipper->AddChild(forceSkipperOn);
    frameSkipper->AddChild(forceStrictMode);
    frameSkipper->AddChild(maxSkippedFrames);
    frameSkipper->AddChild(maxSkippedFramesUGC);
    frameSkipper->AddChild(maxSkippedFramesInFastForward);
    frameSkipper->AddChild(rateIncreaseDelay);
    frameSkipper->AddChild(maxLateFrames);
    frameSkipper->AddChild(lateFramesDecayTime);
    frameSkipper->AddChild(maxMissedFramesCumulative);
    frameSkipper->AddChild(waitThreshold);
    frameSkipper->AddChild(framesBetweenSlowdowns);
    frameSkipper->AddChild(speedUpThreshold);
    frameSkipper->AddChild(stopSpeedUpThreshold);
    frameSkipper->AddChild(catchUpThreshold);
    frameSkipper->AddChild(maxUnrenderedFrames);
    frameSkipper->AddChild(catchUpMaxRatio);
    frameSkipper->AddChild(catchUpMinRatio);
    frameSkipper->AddChild(catchUpRatioIncreaseRate);
    frameSkipper->AddChild(fakeFasterWithSkipping);
    frameSkipper->AddChild(fakeRenderDelay);
    frameSkipper->AddChild(winMissReportTime);
    frameSkipper->AddChild(winAheadReportTime);
    frameSkipper->AddChild(targetFrameRate);
    frameSkipper->AddChild(forceVblankWaitInterval);
    frameSkipper->AddChild(vsyncWorks);
    frameSkipper->AddChild(maxValidFPS);
    frameSkipper->AddChild(minValidFPS);
    frameSkipper->AddChild(fpsCheckInterval);
    frameSkipper->AddChild(abnormalFramesTolerated);

    RegisterTweakable(frameSkipper);
    m_rootFolders.push_back(frameSkipper);
}

void DevMenu::InitializeGraphic() {
    auto graphic = std::make_shared<TweakableFolder>(354, "Graphic");

    // Advance subfolder
    auto advance = std::make_shared<TweakableFolder>(355, "Advance");

    // RenderingDebug subfolder
    auto renderingDebug = std::make_shared<TweakableFolder>(356, "RenderingDebug");
    auto rd_enablePostEffects = CreateSyncedBool(357, "EnablePostEffects", true);
    auto rd_underwaterFogExp = CreateSyncedFloat(361, "UnderwaterFogExp", 0.35f, 0.0f, 5.0f);
    auto rd_underwaterSkyFogExp = CreateSyncedFloat(362, "UnderwaterSkyFogExp", 1.0f, 0.0f, 5.0f);
    auto rd_underwaterFogColorTint = CreateSyncedFloat(363, "UnderwaterFogColorTint", 0.5f, 0.0f, 2.0f);
    auto rd_underwaterColorTint = CreateSyncedFloat(364, "UnderwaterColorTint", 0.5f, 0.0f, 2.0f);
    auto rd_underwaterPostTechnqiue = CreateSyncedInt(365, "UnderwaterPostTechnqiue", 6, 0, 20);
    auto rd_drivingLineBikeWheelWidth = CreateSyncedFloat(366, "DrivingLineBikeWheelWidth", 0.12f, 0.0f, 1.0f);
    auto rd_drivingLineQuadWheelWidth = CreateSyncedFloat(367, "DrivingLineQuadWheelWidth", 0.29f, 0.0f, 1.0f);
    auto rd_drivingLinesQuadWheelDistance = CreateSyncedFloat(368, "DrivingLinesQuadWheelDistance", 0.57f, 0.0f, 2.0f);
    auto rd_lodDissolveFarest = CreateSyncedFloat(641, "lodDissolveFarest", 0.95f, 0.0f, 1.0f);
    auto rd_lodDissolveNearest = CreateSyncedFloat(642, "lodDissolveNearest", 0.75f, 0.0f, 1.0f);
    auto rd_lodDissolveConfine = CreateSyncedFloat(643, "lodDissolveConfine", 0.85f, 0.0f, 1.0f);
    auto rd_lodDissolveSpeed = CreateSyncedFloat(644, "lodDissolveSpeed", 0.01f, 0.0f, 1.0f);
    auto rd_enableShadow = CreateSyncedBool(653, "EnableShadow", true);
    auto rd_enableDebugRendering = CreateSyncedBool(655, "EnableDebugRendering", false);
    auto rd_debugRenderingIndex = CreateSyncedInt(665, "DebugRenderingIndex", 0, 0, 15);
    auto rd_debugSpotLightCulling = CreateSyncedBool(666, "DebugSpotLightCulling", false);
    auto rd_underwaterDofDist = CreateSyncedFloat(755, "UnderwaterDofDist", 5.0f, 0.0f, 50.0f);
    auto rd_underwaterDofNear = CreateSyncedFloat(756, "UnderwaterDofNear", 0.3f, 0.0f, 5.0f);
    auto rd_underwaterDofFar = CreateSyncedFloat(757, "UnderwaterDofFar", 0.3f, 0.0f, 5.0f);

    RegisterTweakable(rd_enablePostEffects);
    RegisterTweakable(rd_underwaterFogExp);
    RegisterTweakable(rd_underwaterSkyFogExp);
    RegisterTweakable(rd_underwaterFogColorTint);
    RegisterTweakable(rd_underwaterColorTint);
    RegisterTweakable(rd_underwaterPostTechnqiue);
    RegisterTweakable(rd_drivingLineBikeWheelWidth);
    RegisterTweakable(rd_drivingLineQuadWheelWidth);
    RegisterTweakable(rd_drivingLinesQuadWheelDistance);
    RegisterTweakable(rd_lodDissolveFarest);
    RegisterTweakable(rd_lodDissolveNearest);
    RegisterTweakable(rd_lodDissolveConfine);
    RegisterTweakable(rd_lodDissolveSpeed);
    RegisterTweakable(rd_enableShadow);
    RegisterTweakable(rd_enableDebugRendering);
    RegisterTweakable(rd_debugRenderingIndex);
    RegisterTweakable(rd_debugSpotLightCulling);
    RegisterTweakable(rd_underwaterDofDist);
    RegisterTweakable(rd_underwaterDofNear);
    RegisterTweakable(rd_underwaterDofFar);

    renderingDebug->AddChild(rd_enablePostEffects);
    renderingDebug->AddChild(rd_underwaterFogExp);
    renderingDebug->AddChild(rd_underwaterSkyFogExp);
    renderingDebug->AddChild(rd_underwaterFogColorTint);
    renderingDebug->AddChild(rd_underwaterColorTint);
    renderingDebug->AddChild(rd_underwaterPostTechnqiue);
    renderingDebug->AddChild(rd_drivingLineBikeWheelWidth);
    renderingDebug->AddChild(rd_drivingLineQuadWheelWidth);
    renderingDebug->AddChild(rd_drivingLinesQuadWheelDistance);
    renderingDebug->AddChild(rd_lodDissolveFarest);
    renderingDebug->AddChild(rd_lodDissolveNearest);
    renderingDebug->AddChild(rd_lodDissolveConfine);
    renderingDebug->AddChild(rd_lodDissolveSpeed);
    renderingDebug->AddChild(rd_enableShadow);
    renderingDebug->AddChild(rd_enableDebugRendering);
    renderingDebug->AddChild(rd_debugRenderingIndex);
    renderingDebug->AddChild(rd_debugSpotLightCulling);
    renderingDebug->AddChild(rd_underwaterDofDist);
    renderingDebug->AddChild(rd_underwaterDofNear);
    renderingDebug->AddChild(rd_underwaterDofFar);

    RegisterTweakable(renderingDebug);
    advance->AddChild(renderingDebug);

    // Occlusion subfolder
    auto occlusion = std::make_shared<TweakableFolder>(358, "Occlusion");
    auto occ_predictMainCamera = CreateSyncedBool(359, "PredictMainCamera", true);
    auto occ_predictFrames = CreateSyncedInt(360, "PredictFrames", 6, 0, 20);
    auto occ_predictShadowCamera = CreateSyncedBool(702, "PredictShadowCamera", true);
    auto occ_useExtraCascadeCullPlanes = CreateSyncedBool(703, "UseExtraCascadeCullPlanes", true);
    auto occ_perObject = CreateSyncedBool(713, "PerObject", true);
    auto occ_perNode = CreateSyncedBool(714, "PerNode", true);
    auto occ_perItem = CreateSyncedBool(715, "PerItem", true);
    auto occ_enable = CreateSyncedBool(716, "Enable", true);
    auto occ_alwaysFullCull = CreateSyncedBool(721, "alwaysFullCull", false);
    auto occ_deferredOcclusion = CreateSyncedBool(722, "deferredOcclusion", true);

    RegisterTweakable(occ_predictMainCamera);
    RegisterTweakable(occ_predictFrames);
    RegisterTweakable(occ_predictShadowCamera);
    RegisterTweakable(occ_useExtraCascadeCullPlanes);
    RegisterTweakable(occ_perObject);
    RegisterTweakable(occ_perNode);
    RegisterTweakable(occ_perItem);
    RegisterTweakable(occ_enable);
    RegisterTweakable(occ_alwaysFullCull);
    RegisterTweakable(occ_deferredOcclusion);

    occlusion->AddChild(occ_predictMainCamera);
    occlusion->AddChild(occ_predictFrames);
    occlusion->AddChild(occ_predictShadowCamera);
    occlusion->AddChild(occ_useExtraCascadeCullPlanes);
    occlusion->AddChild(occ_perObject);
    occlusion->AddChild(occ_perNode);
    occlusion->AddChild(occ_perItem);
    occlusion->AddChild(occ_enable);
    occlusion->AddChild(occ_alwaysFullCull);
    occlusion->AddChild(occ_deferredOcclusion);

    RegisterTweakable(occlusion);
    advance->AddChild(occlusion);

    // RGBM_3DLookup subfolder
    auto rgbm3DLookup = std::make_shared<TweakableFolder>(376, "RGBM_3DLookup");
    auto rgbm = CreateSyncedBool(377, "RGBM", false);
    auto lookup3D = CreateSyncedBool(378, "3DLookup", true);

    RegisterTweakable(rgbm);
    RegisterTweakable(lookup3D);

    rgbm3DLookup->AddChild(rgbm);
    rgbm3DLookup->AddChild(lookup3D);

    RegisterTweakable(rgbm3DLookup);
    advance->AddChild(rgbm3DLookup);

    auto rgbm3DLookupInt = CreateSyncedInt(552, "RGBM_3DLookup", 0, 0, 10);
    RegisterTweakable(rgbm3DLookupInt);
    advance->AddChild(rgbm3DLookupInt);

    // MSSAO subfolder
    auto mssao = std::make_shared<TweakableFolder>(553, "MSSAO");
    auto mssao_enable = CreateSyncedBool(554, "MSSAOEnable", true);
    auto mssao_intensity = CreateSyncedFloat(776, "IntensityMSSAO", 2.2f, 0.0f, 10.0f);
    auto mssao_nearRadius = CreateSyncedFloat(777, "t_ao_near_radius", 0.3f, 0.0f, 5.0f);
    auto mssao_farRadius = CreateSyncedFloat(778, "t_ao_far_radius", 20.0f, 0.0f, 100.0f);
    auto mssao_farDistance = CreateSyncedFloat(779, "t_ao_far_distance", 250.0f, 0.0f, 1000.0f);
    auto mssao_gammaDistance = CreateSyncedFloat(780, "t_ao_gamma_distance", 1.0f, 0.0f, 10.0f);
    auto mssao_angleBias = CreateSyncedFloat(781, "t_ao_angle_bias", 14.0f, 0.0f, 90.0f);
    auto mssao_frontFaceStrength = CreateSyncedFloat(782, "t_ao_front_face_strength", 0.4f, 0.0f, 2.0f);
    auto mssao_hbaoRadius = CreateSyncedFloat(783, "t_ao_hbao_radius", 0.5f, 0.0f, 5.0f);
    auto mssao_hbaoMaxPixelRadius = CreateSyncedFloat(784, "t_ao_hbao_max_pixel_radius", 0.25f, 0.0f, 2.0f);

    RegisterTweakable(mssao_enable);
    RegisterTweakable(mssao_intensity);
    RegisterTweakable(mssao_nearRadius);
    RegisterTweakable(mssao_farRadius);
    RegisterTweakable(mssao_farDistance);
    RegisterTweakable(mssao_gammaDistance);
    RegisterTweakable(mssao_angleBias);
    RegisterTweakable(mssao_frontFaceStrength);
    RegisterTweakable(mssao_hbaoRadius);
    RegisterTweakable(mssao_hbaoMaxPixelRadius);

    mssao->AddChild(mssao_enable);
    mssao->AddChild(mssao_intensity);
    mssao->AddChild(mssao_nearRadius);
    mssao->AddChild(mssao_farRadius);
    mssao->AddChild(mssao_farDistance);
    mssao->AddChild(mssao_gammaDistance);
    mssao->AddChild(mssao_angleBias);
    mssao->AddChild(mssao_frontFaceStrength);
    mssao->AddChild(mssao_hbaoRadius);
    mssao->AddChild(mssao_hbaoMaxPixelRadius);

    RegisterTweakable(mssao);
    advance->AddChild(mssao);

    auto parallaxMapping = CreateSyncedBool(555, "ParallaxMapping", true);
    auto decalBlurEnable = CreateSyncedBool(647, "DecalBlurEnable", true);
    auto decalBlurPasses = CreateSyncedInt(648, "DecalBlurPasses", 2, 0, 10);
    auto decalNewBlur = CreateSyncedBool(649, "DecalNewBlur", false);

    RegisterTweakable(parallaxMapping);
    RegisterTweakable(decalBlurEnable);
    RegisterTweakable(decalBlurPasses);
    RegisterTweakable(decalNewBlur);

    advance->AddChild(parallaxMapping);
    advance->AddChild(decalBlurEnable);
    advance->AddChild(decalBlurPasses);
    advance->AddChild(decalNewBlur);

    // Particle/Fog subfolder
    auto particleFog = std::make_shared<TweakableFolder>(650, "Particle/Fog");
    auto pf_renderBillboards = CreateSyncedBool(651, "RenderBillboards", true);
    auto pf_renderParticles = CreateSyncedBool(652, "RenderParticles", true);
    auto pf_particleOverdraw = CreateSyncedBool(659, "ParticleOverdraw", false);
    auto pf_forceFogOff = CreateSyncedBool(660, "forceFogOff", false);
    auto pf_particleFogOff = CreateSyncedBool(661, "particleFogOff", false);

    RegisterTweakable(pf_renderBillboards);
    RegisterTweakable(pf_renderParticles);
    RegisterTweakable(pf_particleOverdraw);
    RegisterTweakable(pf_forceFogOff);
    RegisterTweakable(pf_particleFogOff);

    particleFog->AddChild(pf_renderBillboards);
    particleFog->AddChild(pf_renderParticles);
    particleFog->AddChild(pf_particleOverdraw);
    particleFog->AddChild(pf_forceFogOff);
    particleFog->AddChild(pf_particleFogOff);

    RegisterTweakable(particleFog);
    advance->AddChild(particleFog);

    auto bloom = CreateSyncedBool(654, "Bloom", true);
    auto reloadShaders = CreateSyncedBool(656, "ReloadShaders", false);
    auto wireFrameMode = CreateSyncedBool(657, "WireFrameMode", false);
    auto disableVTJittering = CreateSyncedBool(658, "DisableVTJittering", false);
    auto postMeshesEnabled = CreateSyncedBool(662, "PostMeshesEnabled", true);
    auto lightFlareEnabled = CreateSyncedBool(663, "LightFlareEnabled", true);
    auto renderLightTileWithQuads = CreateSyncedBool(664, "RenderLightTileWithQuads", true);
    auto enableAA = CreateSyncedBool(667, "EnableAA", true);
    auto useDownsample4x4Optimize = CreateSyncedBool(668, "useDownsample4x4Optimize", true);

    RegisterTweakable(bloom);
    RegisterTweakable(reloadShaders);
    RegisterTweakable(wireFrameMode);
    RegisterTweakable(disableVTJittering);
    RegisterTweakable(postMeshesEnabled);
    RegisterTweakable(lightFlareEnabled);
    RegisterTweakable(renderLightTileWithQuads);
    RegisterTweakable(enableAA);
    RegisterTweakable(useDownsample4x4Optimize);

    advance->AddChild(bloom);
    advance->AddChild(reloadShaders);
    advance->AddChild(wireFrameMode);
    advance->AddChild(disableVTJittering);
    advance->AddChild(postMeshesEnabled);
    advance->AddChild(lightFlareEnabled);
    advance->AddChild(renderLightTileWithQuads);
    advance->AddChild(enableAA);
    advance->AddChild(useDownsample4x4Optimize);

    // Shadow subfolder
    auto shadow = std::make_shared<TweakableFolder>(669, "Shadow");
    auto sh_shadowBias = CreateSyncedFloat(670, "shadowBias", 0.004f, 0.0f, 0.1f);
    auto sh_preShadowZScale = CreateSyncedFloat(671, "PreShadowZScale", 0.97f, 0.0f, 2.0f);
    auto sh_preShadowZShift = CreateSyncedFloat(672, "PreShadowZShift", 0.03f, 0.0f, 1.0f);
    auto sh_shadowDebugLevel = CreateSyncedInt(673, "ShadowDebugLevel", 0, 0, 10);
    auto sh_pssmCullingRoundedWay = CreateSyncedInt(674, "PSSMCullingRoundedWay", 2, 0, 5);
    auto sh_pssmUseUniform = CreateSyncedBool(675, "PSSMUseUniform", false);
    auto sh_spotlightBiasHack = CreateSyncedBool(676, "SpotlightBiasHack", true);
    auto sh_pssmCompressShadowRate = CreateSyncedFloat(677, "PSSMCompressShadowRate", 1.0f, 0.0f, 5.0f);
    auto sh_pssmInterleavedRendering = CreateSyncedBool(678, "PSSMInterleavedRendering", true);

    RegisterTweakable(sh_shadowBias);
    RegisterTweakable(sh_preShadowZScale);
    RegisterTweakable(sh_preShadowZShift);
    RegisterTweakable(sh_shadowDebugLevel);
    RegisterTweakable(sh_pssmCullingRoundedWay);
    RegisterTweakable(sh_pssmUseUniform);
    RegisterTweakable(sh_spotlightBiasHack);
    RegisterTweakable(sh_pssmCompressShadowRate);
    RegisterTweakable(sh_pssmInterleavedRendering);

    shadow->AddChild(sh_shadowBias);
    shadow->AddChild(sh_preShadowZScale);
    shadow->AddChild(sh_preShadowZShift);
    shadow->AddChild(sh_shadowDebugLevel);
    shadow->AddChild(sh_pssmCullingRoundedWay);
    shadow->AddChild(sh_pssmUseUniform);
    shadow->AddChild(sh_spotlightBiasHack);
    shadow->AddChild(sh_pssmCompressShadowRate);
    shadow->AddChild(sh_pssmInterleavedRendering);

    // Scrolling subfolder (within Shadow)
    auto scrolling = std::make_shared<TweakableFolder>(679, "Scrolling");
    auto sc_pssmScrollingLogIndex = CreateSyncedInt(680, "PSSMScrollingLogIndex", 0, 0, 10);
    auto sc_pssmAlwaysInvalidCache = CreateSyncedBool(681, "PSSMAlwaysInvalidCache", false);
    auto sc_pssmScrollingCacheDebug = CreateSyncedInt(682, "PSSMScrollingCacheDebug", 0, 0, 10);
    auto sc_pssmLogPredictScrollOffset = CreateSyncedBool(683, "PSSMLogPredictScrollOffset", false);
    auto sc_pssmLogPredictOrthoChange = CreateSyncedBool(684, "PSSMLogPredictOrthoChange", false);
    auto sc_pssmOrthoChangePredictRate = CreateSyncedFloat(685, "PSSMOrthoChangePredictRate", 2.0f, 0.0f, 10.0f);
    auto sc_pssmOutOfRangePredictRate = CreateSyncedFloat(686, "PSSMOutOfRangePredictRate", 1.5f, 0.0f, 10.0f);
    auto sc_changeTerrainDynamicFlag = CreateSyncedBool(772, "ChangeTerrainDynamicFlag", true);
    auto sc_dynamicLODThreshold = CreateSyncedInt(773, "DynamicLODThreshold", 3, 0, 10);
    auto sc_changeTerrainDynamicFlagTess = CreateSyncedBool(786, "ChangeTerrainDynamicFlagTess", true);
    auto sc_dynamicLODThresholdTess = CreateSyncedInt(787, "DynamicLODThresholdTess", 2, 0, 10);

    RegisterTweakable(sc_pssmScrollingLogIndex);
    RegisterTweakable(sc_pssmAlwaysInvalidCache);
    RegisterTweakable(sc_pssmScrollingCacheDebug);
    RegisterTweakable(sc_pssmLogPredictScrollOffset);
    RegisterTweakable(sc_pssmLogPredictOrthoChange);
    RegisterTweakable(sc_pssmOrthoChangePredictRate);
    RegisterTweakable(sc_pssmOutOfRangePredictRate);
    RegisterTweakable(sc_changeTerrainDynamicFlag);
    RegisterTweakable(sc_dynamicLODThreshold);
    RegisterTweakable(sc_changeTerrainDynamicFlagTess);
    RegisterTweakable(sc_dynamicLODThresholdTess);

    scrolling->AddChild(sc_pssmScrollingLogIndex);
    scrolling->AddChild(sc_pssmAlwaysInvalidCache);
    scrolling->AddChild(sc_pssmScrollingCacheDebug);
    scrolling->AddChild(sc_pssmLogPredictScrollOffset);
    scrolling->AddChild(sc_pssmLogPredictOrthoChange);
    scrolling->AddChild(sc_pssmOrthoChangePredictRate);
    scrolling->AddChild(sc_pssmOutOfRangePredictRate);
    scrolling->AddChild(sc_changeTerrainDynamicFlag);
    scrolling->AddChild(sc_dynamicLODThreshold);
    scrolling->AddChild(sc_changeTerrainDynamicFlagTess);
    scrolling->AddChild(sc_dynamicLODThresholdTess);

    RegisterTweakable(scrolling);
    shadow->AddChild(scrolling);

    auto sh_pssmCullingAreaExtendMul = CreateSyncedFloat(687, "PSSMCullingAreaExtendMul", 1.05f, 0.0f, 5.0f);
    auto sh_pssmCullingAreaExtendAdd = CreateSyncedFloat(688, "PSSMCullingAreaExtendAdd", 5.0f, 0.0f, 50.0f);
    auto sh_pssmDepthMinMaxPredict = CreateSyncedFloat(689, "PSSMDepthMinMaxPredict", 6.0f, 0.0f, 20.0f);
    auto sh_useScryControlShadowPCFsteps = CreateSyncedBool(690, "UseScryControlShadowPCFsteps", false);
    auto sh_shadowPCFFilter = CreateSyncedInt(691, "ShadowPCFFilter", 1, 0, 10);
    auto sh_maxShadowViewDistance = CreateSyncedFloat(700, "MaxShadowViewDistance", 400.0f, 0.0f, 2000.0f);
    auto sh_enableTweakingShadowDrawDistance = CreateSyncedBool(701, "enableTweakingShadowDrawDistance", false);
    auto sh_fixedShadowRangeAllTime = CreateSyncedBool(704, "FixedShadowRangeAllTime", false);
    auto sh_limitCascadeChange = CreateSyncedBool(705, "LimitCascadeChange", true);
    auto sh_limitCascadeMinMax = CreateSyncedBool(706, "LimitCascadeMinMax", true);

    RegisterTweakable(sh_pssmCullingAreaExtendMul);
    RegisterTweakable(sh_pssmCullingAreaExtendAdd);
    RegisterTweakable(sh_pssmDepthMinMaxPredict);
    RegisterTweakable(sh_useScryControlShadowPCFsteps);
    RegisterTweakable(sh_shadowPCFFilter);
    RegisterTweakable(sh_maxShadowViewDistance);
    RegisterTweakable(sh_enableTweakingShadowDrawDistance);
    RegisterTweakable(sh_fixedShadowRangeAllTime);
    RegisterTweakable(sh_limitCascadeChange);
    RegisterTweakable(sh_limitCascadeMinMax);

    shadow->AddChild(sh_pssmCullingAreaExtendMul);
    shadow->AddChild(sh_pssmCullingAreaExtendAdd);
    shadow->AddChild(sh_pssmDepthMinMaxPredict);
    shadow->AddChild(sh_useScryControlShadowPCFsteps);
    shadow->AddChild(sh_shadowPCFFilter);
    shadow->AddChild(sh_maxShadowViewDistance);
    shadow->AddChild(sh_enableTweakingShadowDrawDistance);
    shadow->AddChild(sh_fixedShadowRangeAllTime);
    shadow->AddChild(sh_limitCascadeChange);
    shadow->AddChild(sh_limitCascadeMinMax);

    RegisterTweakable(shadow);
    advance->AddChild(shadow);

    // GlobalAmbient subfolder
    auto globalAmbient = std::make_shared<TweakableFolder>(692, "GlobalAmbient");
    auto ga_rebake = CreateSyncedBool(693, "GlobalAmbientRebake", false);
    auto ga_enabled = CreateSyncedBool(738, "Enabled", false);
    auto ga_blend = CreateSyncedFloat(739, "GlobalAmbientBlend", 0.0f, 0.0f, 2.0f);
    auto ga_normalScaleXY = CreateSyncedFloat(740, "NormalScaleXY", 1.6f, 0.0f, 10.0f);
    auto ga_normalScaleZ = CreateSyncedFloat(741, "NormalScaleZ", 1.0f, 0.0f, 10.0f);
    auto ga_maxDeltaHeight = CreateSyncedFloat(742, "MaxDeltaHeight", 200.0f, 0.0f, 1000.0f);
    auto ga_pixelOcclusionFalloffY = CreateSyncedFloat(743, "PixelOcclusionFalloffY", 16.0f, 0.0f, 100.0f);
    auto ga_pixelOcclusionMin = CreateSyncedFloat(744, "PixelOcclusionMin", 0.65f, 0.0f, 2.0f);
    auto ga_zRangeMultiplier = CreateSyncedFloat(745, "ZRangeMultiplier", 128.0f, 0.0f, 500.0f);
    auto ga_zRangeOffset = CreateSyncedFloat(746, "ZRangeOffset", 0.0f, 0.0f, 100.0f);
    auto ga_intensity = CreateSyncedFloat(747, "Intensity", 0.23f, 0.0f, 5.0f);

    RegisterTweakable(ga_rebake);
    RegisterTweakable(ga_enabled);
    RegisterTweakable(ga_blend);
    RegisterTweakable(ga_normalScaleXY);
    RegisterTweakable(ga_normalScaleZ);
    RegisterTweakable(ga_maxDeltaHeight);
    RegisterTweakable(ga_pixelOcclusionFalloffY);
    RegisterTweakable(ga_pixelOcclusionMin);
    RegisterTweakable(ga_zRangeMultiplier);
    RegisterTweakable(ga_zRangeOffset);
    RegisterTweakable(ga_intensity);

    globalAmbient->AddChild(ga_rebake);
    globalAmbient->AddChild(ga_enabled);
    globalAmbient->AddChild(ga_blend);
    globalAmbient->AddChild(ga_normalScaleXY);
    globalAmbient->AddChild(ga_normalScaleZ);
    globalAmbient->AddChild(ga_maxDeltaHeight);
    globalAmbient->AddChild(ga_pixelOcclusionFalloffY);
    globalAmbient->AddChild(ga_pixelOcclusionMin);
    globalAmbient->AddChild(ga_zRangeMultiplier);
    globalAmbient->AddChild(ga_zRangeOffset);
    globalAmbient->AddChild(ga_intensity);

    RegisterTweakable(globalAmbient);
    advance->AddChild(globalAmbient);

    // AverageLuminance subfolder
    auto averageLuminance = std::make_shared<TweakableFolder>(694, "AverageLuminance");
    auto al_debugEnabled = CreateSyncedBool(695, "DebugEnabled", false);
    auto al_averageLuminance = CreateSyncedFloat(696, "averageLuminance", 1.0f, 0.0f, 10.0f);
    auto al_averageLuminanceBlend = CreateSyncedFloat(697, "averageLuminanceBlend", 0.5f, 0.0f, 2.0f);

    RegisterTweakable(al_debugEnabled);
    RegisterTweakable(al_averageLuminance);
    RegisterTweakable(al_averageLuminanceBlend);

    averageLuminance->AddChild(al_debugEnabled);
    averageLuminance->AddChild(al_averageLuminance);
    averageLuminance->AddChild(al_averageLuminanceBlend);

    RegisterTweakable(averageLuminance);
    advance->AddChild(averageLuminance);

    auto windGustStr = CreateSyncedFloat(698, "windGustStr", 1.5f, 0.0f, 10.0f);
    auto debugSkyCubemapMipLevel = CreateSyncedInt(699, "DebugSkyCubemapMipLevel", 0, 0, 10);

    RegisterTweakable(windGustStr);
    RegisterTweakable(debugSkyCubemapMipLevel);

    advance->AddChild(windGustStr);
    advance->AddChild(debugSkyCubemapMipLevel);

    // Bokeh subfolder
    auto bokeh = std::make_shared<TweakableFolder>(707, "Bokeh");
    auto bk_singleVP = CreateSyncedBool(708, "SingleVP", false);
    auto bk_debugView = CreateSyncedBool(709, "DebugView", false);
    auto bk_optimization = CreateSyncedBool(710, "Optimization", true);
    auto bk_brightnessThreshold = CreateSyncedFloat(748, "BrightnessThreshold", 1.2f, 0.0f, 10.0f);
    auto bk_cocThreshold = CreateSyncedFloat(749, "CoCThreshold", 0.7f, 0.0f, 2.0f);
    auto bk_blurSize = CreateSyncedFloat(750, "BlurSize", 32.0f, 0.0f, 100.0f);
    auto bk_blurLimitNear = CreateSyncedFloat(751, "BlurLimitNear", 0.9f, 0.0f, 2.0f);
    auto bk_blurLimitFar = CreateSyncedFloat(752, "BlurLimitFar", 0.5f, 0.0f, 2.0f);
    auto bk_lowResRange = CreateSyncedFloat(753, "LowResRange", 0.75f, 0.0f, 2.0f);
    auto bk_enableParamDebug = CreateSyncedBool(754, "EnableParamDebug", false);

    RegisterTweakable(bk_singleVP);
    RegisterTweakable(bk_debugView);
    RegisterTweakable(bk_optimization);
    RegisterTweakable(bk_brightnessThreshold);
    RegisterTweakable(bk_cocThreshold);
    RegisterTweakable(bk_blurSize);
    RegisterTweakable(bk_blurLimitNear);
    RegisterTweakable(bk_blurLimitFar);
    RegisterTweakable(bk_lowResRange);
    RegisterTweakable(bk_enableParamDebug);

    bokeh->AddChild(bk_singleVP);
    bokeh->AddChild(bk_debugView);
    bokeh->AddChild(bk_optimization);
    bokeh->AddChild(bk_brightnessThreshold);
    bokeh->AddChild(bk_cocThreshold);
    bokeh->AddChild(bk_blurSize);
    bokeh->AddChild(bk_blurLimitNear);
    bokeh->AddChild(bk_blurLimitFar);
    bokeh->AddChild(bk_lowResRange);
    bokeh->AddChild(bk_enableParamDebug);

    RegisterTweakable(bokeh);
    advance->AddChild(bokeh);

    // CMAA subfolder
    auto cmaa = std::make_shared<TweakableFolder>(711, "CMAA");
    auto cmaa_enable = CreateSyncedBool(712, "Enable", true);
    auto cmaa_edgeDetectionThreshold = CreateSyncedFloat(736, "EdgeDetectionThreshold", 0.0769231f, 0.0f, 1.0f);
    auto cmaa_nonDominantEdgeRemovalAmount = CreateSyncedFloat(737, "NonDominantEdgeRemovalAmount", 0.35f, 0.0f, 2.0f);

    RegisterTweakable(cmaa_enable);
    RegisterTweakable(cmaa_edgeDetectionThreshold);
    RegisterTweakable(cmaa_nonDominantEdgeRemovalAmount);

    cmaa->AddChild(cmaa_enable);
    cmaa->AddChild(cmaa_edgeDetectionThreshold);
    cmaa->AddChild(cmaa_nonDominantEdgeRemovalAmount);

    RegisterTweakable(cmaa);
    advance->AddChild(cmaa);

    auto instancingEnable = CreateSyncedBool(717, "InstancingEnable", true);
    auto lowEndLodRange = CreateSyncedFloat(718, "lowEndLodRange", 0.6f, 0.0f, 2.0f);
    auto highEndLodRange = CreateSyncedFloat(719, "highEndLodRange", 1.0f, 0.0f, 2.0f);
    auto cullSpotLightByCone = CreateSyncedBool(720, "CullSpotLightByCone", true);

    RegisterTweakable(instancingEnable);
    RegisterTweakable(lowEndLodRange);
    RegisterTweakable(highEndLodRange);
    RegisterTweakable(cullSpotLightByCone);

    advance->AddChild(instancingEnable);
    advance->AddChild(lowEndLodRange);
    advance->AddChild(highEndLodRange);
    advance->AddChild(cullSpotLightByCone);

    // LocalReflectionParameter subfolder
    auto localReflectionParameter = std::make_shared<TweakableFolder>(758, "LocalReflectionParameter");
    auto lr_minAmbientFresnel = CreateSyncedFloat(759, "MinAmbientFresnel", 0.001f, 0.0f, 1.0f);
    auto lr_initRayStepLength = CreateSyncedFloat(760, "InitRayStepLength", 2.0f, 0.0f, 10.0f);
    auto lr_maxRayJittering = CreateSyncedFloat(761, "MaxRayJittering", 0.1f, 0.0f, 2.0f);
    auto lr_vignetteSizeScale = CreateSyncedFloat(762, "VignetteSizeScale", -5.0f, -20.0f, 20.0f);
    auto lr_vignetteSizeBias = CreateSyncedFloat(763, "VignetteSizeBias", 2.5f, 0.0f, 10.0f);
    auto lr_maxReflectionBrightness = CreateSyncedFloat(764, "MaxReflectionBrightness", 0.8f, 0.0f, 5.0f);
    auto lr_reflectionScale = CreateSyncedFloat(765, "ReflectionScale", 2.0f, 0.0f, 10.0f);
    auto lr_maxRayDistance = CreateSyncedFloat(766, "MaxRayDistance", 5.0f, 0.0f, 50.0f);
    auto lr_maxRayDelta = CreateSyncedFloat(767, "MaxRayDelta", 0.125f, 0.0f, 2.0f);
    auto lr_maxRayStep = CreateSyncedFloat(768, "MaxRayStep", 400.0f, 0.0f, 2000.0f);
    auto lr_farClipDistance = CreateSyncedFloat(769, "FarClipDistance", 0.999f, 0.0f, 2.0f);
    auto lr_reflectionBlurriness = CreateSyncedFloat(770, "ReflectionBlurriness", 300.0f, 0.0f, 1000.0f);
    auto lr_minBlurRadius = CreateSyncedFloat(771, "MinBlurRadius", 2.0f, 0.0f, 10.0f);

    RegisterTweakable(lr_minAmbientFresnel);
    RegisterTweakable(lr_initRayStepLength);
    RegisterTweakable(lr_maxRayJittering);
    RegisterTweakable(lr_vignetteSizeScale);
    RegisterTweakable(lr_vignetteSizeBias);
    RegisterTweakable(lr_maxReflectionBrightness);
    RegisterTweakable(lr_reflectionScale);
    RegisterTweakable(lr_maxRayDistance);
    RegisterTweakable(lr_maxRayDelta);
    RegisterTweakable(lr_maxRayStep);
    RegisterTweakable(lr_farClipDistance);
    RegisterTweakable(lr_reflectionBlurriness);
    RegisterTweakable(lr_minBlurRadius);

    localReflectionParameter->AddChild(lr_minAmbientFresnel);
    localReflectionParameter->AddChild(lr_initRayStepLength);
    localReflectionParameter->AddChild(lr_maxRayJittering);
    localReflectionParameter->AddChild(lr_vignetteSizeScale);
    localReflectionParameter->AddChild(lr_vignetteSizeBias);
    localReflectionParameter->AddChild(lr_maxReflectionBrightness);
    localReflectionParameter->AddChild(lr_reflectionScale);
    localReflectionParameter->AddChild(lr_maxRayDistance);
    localReflectionParameter->AddChild(lr_maxRayDelta);
    localReflectionParameter->AddChild(lr_maxRayStep);
    localReflectionParameter->AddChild(lr_farClipDistance);
    localReflectionParameter->AddChild(lr_reflectionBlurriness);
    localReflectionParameter->AddChild(lr_minBlurRadius);

    RegisterTweakable(localReflectionParameter);
    advance->AddChild(localReflectionParameter);

    auto terrainLODEdgesOnly = CreateSyncedBool(774, "TerrainLODEdgesOnly", true);
    auto terrainLODEdgesAreFat = CreateSyncedBool(775, "TerrainLODEdgesAreFat", true);
    auto tessQuality = CreateSyncedFloat(785, "TessQuality", 0.3f, 0.0f, 2.0f);

    RegisterTweakable(terrainLODEdgesOnly);
    RegisterTweakable(terrainLODEdgesAreFat);
    RegisterTweakable(tessQuality);

    advance->AddChild(terrainLODEdgesOnly);
    advance->AddChild(terrainLODEdgesAreFat);
    advance->AddChild(tessQuality);

    RegisterTweakable(advance);
    graphic->AddChild(advance);

    RegisterTweakable(graphic);
    m_rootFolders.push_back(graphic);
}

void DevMenu::InitializePodium() {
    auto podium = std::make_shared<TweakableFolder>(372, "Podium");

    auto cameraOffsetX = CreateSyncedFloat(373, "cameraOffsetX", -2.6f, -10.0f, 10.0f);
    auto cameraOffsetY = CreateSyncedFloat(374, "cameraOffsetY", 0.0f, -10.0f, 10.0f);
    auto cameraOffsetZ = CreateSyncedFloat(375, "cameraOffsetZ", 0.0f, -10.0f, 10.0f);

    RegisterTweakable(cameraOffsetX);
    RegisterTweakable(cameraOffsetY);
    RegisterTweakable(cameraOffsetZ);

    podium->AddChild(cameraOffsetX);
    podium->AddChild(cameraOffsetY);
    podium->AddChild(cameraOffsetZ);

    RegisterTweakable(podium);
    m_rootFolders.push_back(podium);
}

void DevMenu::InitializeXPSystem() {
    auto xpSystem = std::make_shared<TweakableFolder>(492, "XPSystem");

    auto level1XPLimit = CreateSyncedInt(493, "Level1XPLimit", 100, 0, 100000);
    auto levelXPLimitVal1 = CreateSyncedInt(494, "LevelXPLimitVal1 (Val1 * (Val2 * level + Val3) * (level - 1))", 12, 0, 1000);
    auto levelXPLimitVal2 = CreateSyncedInt(495, "LevelXPLimitVal2", 4, 0, 1000);
    auto levelXPLimitVal3 = CreateSyncedInt(496, "LevelXPLimitVal3", 72, 0, 1000);
    auto xpRewardBeginnerTrackCompleted = CreateSyncedInt(497, "XPRewardBeginnerTrackCompleted", 0, 0, 10000);
    auto xpRewardEasyTrackCompleted = CreateSyncedInt(498, "XPRewardEasyTrackCompleted", 0, 0, 10000);
    auto xpRewardMediumTrackCompleted = CreateSyncedInt(499, "XPRewardMediumTrackCompleted", 0, 0, 10000);
    auto xpRewardHardTrackCompleted = CreateSyncedInt(500, "XPRewardHardTrackCompleted", 0, 0, 10000);
    auto xpRewardExtremeTrackCompleted = CreateSyncedInt(501, "XPRewardExtremeTrackCompleted", 0, 0, 10000);
    auto xpRewardTraining1Passed = CreateSyncedInt(502, "XPRewardTraining1Passed", 100, 0, 10000);
    auto xpRewardTraining2Passed = CreateSyncedInt(503, "XPRewardTraining2Passed", 1000, 0, 10000);
    auto xpRewardTraining3Passed = CreateSyncedInt(504, "XPRewardTraining3Passed", 2000, 0, 10000);
    auto xpRewardTrainingFMXPassed = CreateSyncedInt(505, "XPRewardTrainingFMXPassed", 2000, 0, 10000);
    auto xpRewardTraining5Passed = CreateSyncedInt(506, "XPRewardTraining5Passed", 4500, 0, 10000);
    auto xpRewardBronzeMedal = CreateSyncedInt(507, "XPRewardBronzeMedal", 200, 0, 10000);
    auto xpRewardSilverMedal = CreateSyncedInt(508, "XPRewardSilverMedal", 300, 0, 10000);
    auto xpRewardGoldMedal = CreateSyncedInt(509, "XPRewardGoldMedal", 500, 0, 10000);
    auto xpRewardPlatinumMedal = CreateSyncedInt(510, "XPRewardPlatinumMedal", 1000, 0, 10000);
    auto xpRewardUGCTrackCompleted = CreateSyncedInt(511, "XPRewardUGCTrackCompleted", 50, 0, 10000);
    auto xpRewardMPTournamentPlayed = CreateSyncedInt(512, "XPRewardMPTournamentPlayed", 0, 0, 10000);
    auto xpRewardAllBeginnerTracksCompleted = CreateSyncedInt(513, "XPRewardAllBeginnerTracksCompleted", 0, 0, 10000);
    auto xpRewardAllEasyTracksCompleted = CreateSyncedInt(514, "XPRewardAllEasyTracksCompleted", 0, 0, 10000);
    auto xpRewardAllMediumTracksCompleted = CreateSyncedInt(515, "XPRewardAllMediumTracksCompleted", 0, 0, 10000);
    auto xpRewardAllHardTracksCompleted = CreateSyncedInt(516, "XPRewardAllHardTracksCompleted", 0, 0, 10000);
    auto xpRewardAllExtremeTracksCompleted = CreateSyncedInt(517, "XPRewardAllExtremeTracksCompleted", 0, 0, 10000);

    RegisterTweakable(level1XPLimit);
    RegisterTweakable(levelXPLimitVal1);
    RegisterTweakable(levelXPLimitVal2);
    RegisterTweakable(levelXPLimitVal3);
    RegisterTweakable(xpRewardBeginnerTrackCompleted);
    RegisterTweakable(xpRewardEasyTrackCompleted);
    RegisterTweakable(xpRewardMediumTrackCompleted);
    RegisterTweakable(xpRewardHardTrackCompleted);
    RegisterTweakable(xpRewardExtremeTrackCompleted);
    RegisterTweakable(xpRewardTraining1Passed);
    RegisterTweakable(xpRewardTraining2Passed);
    RegisterTweakable(xpRewardTraining3Passed);
    RegisterTweakable(xpRewardTrainingFMXPassed);
    RegisterTweakable(xpRewardTraining5Passed);
    RegisterTweakable(xpRewardBronzeMedal);
    RegisterTweakable(xpRewardSilverMedal);
    RegisterTweakable(xpRewardGoldMedal);
    RegisterTweakable(xpRewardPlatinumMedal);
    RegisterTweakable(xpRewardUGCTrackCompleted);
    RegisterTweakable(xpRewardMPTournamentPlayed);
    RegisterTweakable(xpRewardAllBeginnerTracksCompleted);
    RegisterTweakable(xpRewardAllEasyTracksCompleted);
    RegisterTweakable(xpRewardAllMediumTracksCompleted);
    RegisterTweakable(xpRewardAllHardTracksCompleted);
    RegisterTweakable(xpRewardAllExtremeTracksCompleted);

    xpSystem->AddChild(level1XPLimit);
    xpSystem->AddChild(levelXPLimitVal1);
    xpSystem->AddChild(levelXPLimitVal2);
    xpSystem->AddChild(levelXPLimitVal3);
    xpSystem->AddChild(xpRewardBeginnerTrackCompleted);
    xpSystem->AddChild(xpRewardEasyTrackCompleted);
    xpSystem->AddChild(xpRewardMediumTrackCompleted);
    xpSystem->AddChild(xpRewardHardTrackCompleted);
    xpSystem->AddChild(xpRewardExtremeTrackCompleted);
    xpSystem->AddChild(xpRewardTraining1Passed);
    xpSystem->AddChild(xpRewardTraining2Passed);
    xpSystem->AddChild(xpRewardTraining3Passed);
    xpSystem->AddChild(xpRewardTrainingFMXPassed);
    xpSystem->AddChild(xpRewardTraining5Passed);
    xpSystem->AddChild(xpRewardBronzeMedal);
    xpSystem->AddChild(xpRewardSilverMedal);
    xpSystem->AddChild(xpRewardGoldMedal);
    xpSystem->AddChild(xpRewardPlatinumMedal);
    xpSystem->AddChild(xpRewardUGCTrackCompleted);
    xpSystem->AddChild(xpRewardMPTournamentPlayed);
    xpSystem->AddChild(xpRewardAllBeginnerTracksCompleted);
    xpSystem->AddChild(xpRewardAllEasyTracksCompleted);
    xpSystem->AddChild(xpRewardAllMediumTracksCompleted);
    xpSystem->AddChild(xpRewardAllHardTracksCompleted);
    xpSystem->AddChild(xpRewardAllExtremeTracksCompleted);

    RegisterTweakable(xpSystem);
    m_rootFolders.push_back(xpSystem);
}

void DevMenu::InitializeTrackUpload() {
    auto trackUpload = std::make_shared<TweakableFolder>(518, "TrackUpload");

    // Add all TrackUpload tweakables
    auto corruptTracks = CreateSyncedBool(519, "corruptTracks", false);

    RegisterTweakable(corruptTracks);

    trackUpload->AddChild(corruptTracks);

    RegisterTweakable(trackUpload);
    m_rootFolders.push_back(trackUpload);
}

void DevMenu::InitializeGameOption() {
    auto gameOption = std::make_shared<TweakableFolder>(541, "GameOption");

    // VideoOption subfolder
    auto videoOption = std::make_shared<TweakableFolder>(542, "VideoOption");
    auto fullScreen = CreateSyncedBool(543, "FullScreen", true);
    auto vsync = CreateSyncedBool(544, "Vsync", true);
    auto fxaa = CreateSyncedInt(545, "FXAA", -1, -1, 2);
    auto showFoliage = CreateSyncedBool(546, "ShowFoliage", true);
    auto shadowQuality = CreateSyncedInt(547, "ShadowQuality", 1, 0, 3);
    auto graphicQuality = CreateSyncedInt(548, "GraphicQuality(low/normal/high)", 1, 0, 2);
    auto geometryQuality = CreateSyncedInt(549, "GeometryQuality(low/high)", 0, 0, 1);
    auto bloomQuality = CreateSyncedInt(550, "BloomQuality(low/high)", 0, 0, 1);
    auto particleQuality = CreateSyncedInt(551, "ParticleQuality(normal/high/ultra)", 0, 0, 2);
    auto bokehDof = CreateSyncedBool(556, "BokehDof", false);

    RegisterTweakable(fullScreen);
    RegisterTweakable(vsync);
    RegisterTweakable(fxaa);
    RegisterTweakable(showFoliage);
    RegisterTweakable(shadowQuality);
    RegisterTweakable(graphicQuality);
    RegisterTweakable(geometryQuality);
    RegisterTweakable(bloomQuality);
    RegisterTweakable(particleQuality);
    RegisterTweakable(bokehDof);

    videoOption->AddChild(fullScreen);
    videoOption->AddChild(vsync);
    videoOption->AddChild(fxaa);
    videoOption->AddChild(showFoliage);
    videoOption->AddChild(shadowQuality);
    videoOption->AddChild(graphicQuality);
    videoOption->AddChild(geometryQuality);
    videoOption->AddChild(bloomQuality);
    videoOption->AddChild(particleQuality);
    videoOption->AddChild(bokehDof);

    RegisterTweakable(videoOption);
    gameOption->AddChild(videoOption);

    RegisterTweakable(gameOption);
    m_rootFolders.push_back(gameOption);
}

void DevMenu::InitializeGameSwf() {
    auto gameSwf = std::make_shared<TweakableFolder>(560, "GameSwf");

    // Top-level GameSwf tweakables
    auto onlineMenuNormalSkip = CreateSyncedInt(561, "OnlineMenuNormalSkip", 1, 0, 10);
    auto onlineMenuUpdateMaxSkip = CreateSyncedInt(562, "OnlineMenuUpdateMaxSkip", 4, 0, 10);

    RegisterTweakable(onlineMenuNormalSkip);
    RegisterTweakable(onlineMenuUpdateMaxSkip);

    gameSwf->AddChild(onlineMenuNormalSkip);
    gameSwf->AddChild(onlineMenuUpdateMaxSkip);

    // Render subfolder
    auto render = std::make_shared<TweakableFolder>(620, "Render");
    auto minTextAutoScale = CreateSyncedFloat(621, "MinTextAutoScale", 0.25f, 0.0f, 2.0f);
    auto textScaleMultiplier = CreateSyncedFloat(622, "TextScaleMultiplier", 1.0f, 0.0f, 5.0f);
    auto zDepth = CreateSyncedFloat(792, "Z-Depth", 0.01f, 0.0f, 1.0f);
    auto resolution = CreateSyncedFloat(793, "Resolution", 1.0f, 0.0f, 4.0f);
    auto textureScale = CreateSyncedFloat(794, "TextureScale", 1.0f, 0.0f, 4.0f);

    RegisterTweakable(minTextAutoScale);
    RegisterTweakable(textScaleMultiplier);
    RegisterTweakable(zDepth);
    RegisterTweakable(resolution);
    RegisterTweakable(textureScale);

    render->AddChild(minTextAutoScale);
    render->AddChild(textScaleMultiplier);
    render->AddChild(zDepth);
    render->AddChild(resolution);
    render->AddChild(textureScale);

    RegisterTweakable(render);
    gameSwf->AddChild(render);

    // Video subfolder
    auto video = std::make_shared<TweakableFolder>(795, "Video");
    auto textureColorR = CreateSyncedFloat(796, "TextureColorR", 1.0f, 0.0f, 2.0f);
    auto textureColorG = CreateSyncedFloat(797, "TextureColorG", 1.0f, 0.0f, 2.0f);
    auto textureColorB = CreateSyncedFloat(798, "TextureColorB", 1.0f, 0.0f, 2.0f);
    auto textureColorA = CreateSyncedFloat(799, "TextureColorA", 1.0f, 0.0f, 2.0f);
    auto textureAddColorR = CreateSyncedFloat(800, "TextureAddColorR", 1.0f, 0.0f, 2.0f);
    auto textureAddColorG = CreateSyncedFloat(801, "TextureAddColorG", 1.0f, 0.0f, 2.0f);
    auto textureAddColorB = CreateSyncedFloat(802, "TextureAddColorB", 1.0f, 0.0f, 2.0f);
    auto textureAddColorA = CreateSyncedFloat(803, "TextureAddColorA", 1.0f, 0.0f, 2.0f);
    auto fillMode = CreateSyncedInt(804, "FillMode", 0, 0, 5);
    auto technique = CreateSyncedInt(805, "Technique", 1, 0, 5);
    auto alphaModX = CreateSyncedFloat(806, "AlphaModX", 1.0f, 0.0f, 2.0f);
    auto alphaModY = CreateSyncedFloat(807, "AlphaModY", 1.0f, 0.0f, 2.0f);

    RegisterTweakable(textureColorR);
    RegisterTweakable(textureColorG);
    RegisterTweakable(textureColorB);
    RegisterTweakable(textureColorA);
    RegisterTweakable(textureAddColorR);
    RegisterTweakable(textureAddColorG);
    RegisterTweakable(textureAddColorB);
    RegisterTweakable(textureAddColorA);
    RegisterTweakable(fillMode);
    RegisterTweakable(technique);
    RegisterTweakable(alphaModX);
    RegisterTweakable(alphaModY);

    video->AddChild(textureColorR);
    video->AddChild(textureColorG);
    video->AddChild(textureColorB);
    video->AddChild(textureColorA);
    video->AddChild(textureAddColorR);
    video->AddChild(textureAddColorG);
    video->AddChild(textureAddColorB);
    video->AddChild(textureAddColorA);
    video->AddChild(fillMode);
    video->AddChild(technique);
    video->AddChild(alphaModX);
    video->AddChild(alphaModY);

    RegisterTweakable(video);
    gameSwf->AddChild(video);

    RegisterTweakable(gameSwf);
    m_rootFolders.push_back(gameSwf);
}

void DevMenu::InitializeGameTime() {
    auto gameTime = std::make_shared<TweakableFolder>(563, "GameTime");

    // Add all GameTime tweakables
    auto logicTimeMaxAhead = CreateSyncedInt(564, "LogicTimeMaxAhead", 4000000, 0, 100000000);
    auto logicTimeMaxBehind = CreateSyncedInt(565, "LogicTimeMaxBehind", 10000000, 0, 100000000);

    RegisterTweakable(logicTimeMaxAhead);
    RegisterTweakable(logicTimeMaxBehind);

    gameTime->AddChild(logicTimeMaxAhead);
    gameTime->AddChild(logicTimeMaxBehind);

    RegisterTweakable(gameTime);
    m_rootFolders.push_back(gameTime);
}

void DevMenu::InitializeVariableFramerate() {
    auto variableFramerate = std::make_shared<TweakableFolder>(566, "VariableFramerateDebug");

    // Add all VariableFramerateDebug tweakables
    auto gpuStall = CreateSyncedInt(567, "GPUStall", 0, 0, 100);
    auto limitingAllowed = CreateSyncedBool(568, "LimitingAllowed", true);
    auto fastForwardRatio = CreateSyncedFloat(569, "FastForwardRatio", 2.0f, 0.0f, 10.0f);
    auto slowMotionRatio = CreateSyncedInt(570, "SlowMotionRatio", 5, 0, 20);
    auto cameraShowInterpolatedAmplitude = CreateSyncedFloat(631, "CameraShowInterpolatedAmplitude", 0.0f, 0.0f, 10.0f);
    auto cameraShowInterpolatedFrequency = CreateSyncedFloat(632, "CameraShowInterpolatedFrequency", 1.0f, 0.0f, 10.0f);
    auto cameraDisableWholeThing = CreateSyncedBool(633, "CameraDisableWholeThing", false);
    auto showInterpolatedObjects = CreateSyncedBool(634, "ShowInterpolatedObjects", false);
    auto doTranslation = CreateSyncedBool(635, "DoTranslation", true);
    auto doRotation = CreateSyncedBool(636, "DoRotation", true);
    auto disableWholeThing = CreateSyncedBool(637, "DisableWholeThing", false);
    auto doNothingInInterpolateMatrices = CreateSyncedBool(638, "DoNothingInInterpolateMatrices", false);
    auto useAsRotation = CreateSyncedBool(639, "UseAsRotation", false);
    auto useAsTranslation = CreateSyncedBool(640, "UseAsTranslation", false);
    auto showInterpolatedBoneObjects = CreateSyncedBool(645, "ShowInterpolatedBoneObjects", false);
    auto disableWholeThingBone = CreateSyncedBool(646, "DisableWholeThingBone", true);
    auto alpha = CreateSyncedFloat(732, "Alpha", 1.0f, 0.0f, 2.0f);
    auto negAlpha = CreateSyncedBool(733, "NegAlpha", false);

    RegisterTweakable(gpuStall);
    RegisterTweakable(limitingAllowed);
    RegisterTweakable(fastForwardRatio);
    RegisterTweakable(slowMotionRatio);
    RegisterTweakable(cameraShowInterpolatedAmplitude);
    RegisterTweakable(cameraShowInterpolatedFrequency);
    RegisterTweakable(cameraDisableWholeThing);
    RegisterTweakable(showInterpolatedObjects);
    RegisterTweakable(doTranslation);
    RegisterTweakable(doRotation);
    RegisterTweakable(disableWholeThing);
    RegisterTweakable(doNothingInInterpolateMatrices);
    RegisterTweakable(useAsRotation);
    RegisterTweakable(useAsTranslation);
    RegisterTweakable(showInterpolatedBoneObjects);
    RegisterTweakable(disableWholeThingBone);
    RegisterTweakable(alpha);
    RegisterTweakable(negAlpha);

    variableFramerate->AddChild(gpuStall);
    variableFramerate->AddChild(limitingAllowed);
    variableFramerate->AddChild(fastForwardRatio);
    variableFramerate->AddChild(slowMotionRatio);
    variableFramerate->AddChild(cameraShowInterpolatedAmplitude);
    variableFramerate->AddChild(cameraShowInterpolatedFrequency);
    variableFramerate->AddChild(cameraDisableWholeThing);
    variableFramerate->AddChild(showInterpolatedObjects);
    variableFramerate->AddChild(doTranslation);
    variableFramerate->AddChild(doRotation);
    variableFramerate->AddChild(disableWholeThing);
    variableFramerate->AddChild(doNothingInInterpolateMatrices);
    variableFramerate->AddChild(useAsRotation);
    variableFramerate->AddChild(useAsTranslation);
    variableFramerate->AddChild(showInterpolatedBoneObjects);
    variableFramerate->AddChild(disableWholeThingBone);
    variableFramerate->AddChild(alpha);
    variableFramerate->AddChild(negAlpha);

    RegisterTweakable(variableFramerate);
    m_rootFolders.push_back(variableFramerate);
}

void DevMenu::InitializeDebug() {
    auto debug = std::make_shared<TweakableFolder>(601, "Debug");

    // Add all Debug tweakables
    auto scoreSaveStateOverride = CreateSyncedInt(602, "ScoreSaveStateOverride", 0, 0, 10);

    RegisterTweakable(scoreSaveStateOverride);

    debug->AddChild(scoreSaveStateOverride);

    RegisterTweakable(debug);
    m_rootFolders.push_back(debug);
}

void DevMenu::InitializeInGameHud() {
    auto inGameHud = std::make_shared<TweakableFolder>(603, "InGameHud");

    // Add all InGameHud tweakables
    auto multiplayerMarkerInterpolation = CreateSyncedFloat(604, "MultiplayerMarkerInterpolation", 0.98f, 0.0f, 1.0f);

    RegisterTweakable(multiplayerMarkerInterpolation);

    inGameHud->AddChild(multiplayerMarkerInterpolation);

    RegisterTweakable(inGameHud);
    m_rootFolders.push_back(inGameHud);
}

void DevMenu::InitializeMainHub() {
    auto mainHub = std::make_shared<TweakableFolder>(606, "MainHub");

    // Add all MainHub tweakables
    auto tournamentsEnabled = CreateSyncedBool(607, "TournamentsEnabled", true);
    auto disableUplayCheck = CreateSyncedBool(613, "Disable Uplay Check", true);

    RegisterTweakable(tournamentsEnabled);
    RegisterTweakable(disableUplayCheck);

    mainHub->AddChild(tournamentsEnabled);
    mainHub->AddChild(disableUplayCheck);

    RegisterTweakable(mainHub);
    m_rootFolders.push_back(mainHub);
}

void DevMenu::InitializeMainMenu() {
    auto mainMenu = std::make_shared<TweakableFolder>(608, "MainMenu");

    // Add all MainMenu tweakables
    auto bannerContentPackId = CreateSyncedInt(609, "BannerContentPackId", 0, 0, 100);
    auto bannerContentPackComingSoon = CreateSyncedBool(610, "BannerContentPackComingSoon", true);

    RegisterTweakable(bannerContentPackId);
    RegisterTweakable(bannerContentPackComingSoon);

    mainMenu->AddChild(bannerContentPackId);
    mainMenu->AddChild(bannerContentPackComingSoon);

    RegisterTweakable(mainMenu);
    m_rootFolders.push_back(mainMenu);
}

void DevMenu::InitializeFlash() {
    auto flash = std::make_shared<TweakableFolder>(611, "Flash");

    // Add all Flash tweakables
    auto errorsEnabled = CreateSyncedBool(612, "ErrorsEnabled", true);

    RegisterTweakable(errorsEnabled);

    flash->AddChild(errorsEnabled);

    RegisterTweakable(flash);
    m_rootFolders.push_back(flash);
}

void DevMenu::InitializeGarbageCollector() {
    auto garbageCollector = std::make_shared<TweakableFolder>(614, "GarbageCollector");

    // Add all GarbageCollector tweakables
    auto printObjectCtorDtor = CreateSyncedBool(615, "PrintObjectCtorDtor", true);
    auto sweepTreshold = CreateSyncedInt(616, "SweepTreshold", 60, 0, 1000);
    auto objCountBegin = CreateSyncedInt(617, "ObjCountBegin", 10000, 0, 100000);
    auto objCountEnd = CreateSyncedInt(618, "ObjCountEnd", 20000, 0, 100000);
    auto sanityCheckInterval = CreateSyncedInt(619, "SanityCheckInterval", 15, 0, 1000);
    auto markMaxMicroseconds = CreateSyncedInt(623, "MarkMaxMicroseconds", 1000000, 0, 10000000);
    auto checkAfterAlive = CreateSyncedBool(624, "CheckAfterAlive", false);
    auto printMarkInfo = CreateSyncedBool(625, "PrintMarkInfo", true);

    RegisterTweakable(printObjectCtorDtor);
    RegisterTweakable(sweepTreshold);
    RegisterTweakable(objCountBegin);
    RegisterTweakable(objCountEnd);
    RegisterTweakable(sanityCheckInterval);
    RegisterTweakable(markMaxMicroseconds);
    RegisterTweakable(checkAfterAlive);
    RegisterTweakable(printMarkInfo);

    garbageCollector->AddChild(printObjectCtorDtor);
    garbageCollector->AddChild(sweepTreshold);
    garbageCollector->AddChild(objCountBegin);
    garbageCollector->AddChild(objCountEnd);
    garbageCollector->AddChild(sanityCheckInterval);
    garbageCollector->AddChild(markMaxMicroseconds);
    garbageCollector->AddChild(checkAfterAlive);
    garbageCollector->AddChild(printMarkInfo);

    RegisterTweakable(garbageCollector);
    m_rootFolders.push_back(garbageCollector);
}

void DevMenu::InitializeSettings() {
    auto settings = std::make_shared<TweakableFolder>(629, "Settings");

    // Add all Settings tweakables
    auto mouseActivationThreshold = CreateSyncedInt(630, "MouseActivationThreshold", 3, 0, 100);

    RegisterTweakable(mouseActivationThreshold);

    settings->AddChild(mouseActivationThreshold);

    RegisterTweakable(settings);
    m_rootFolders.push_back(settings);
}

void DevMenu::InitializeDebugLocalization() {
    auto debugLocalization = std::make_shared<TweakableFolder>(734, "debug");

    // Add all debug tweakables
    auto localization = CreateSyncedBool(735, "localization", false);

    RegisterTweakable(localization);

    debugLocalization->AddChild(localization);

    RegisterTweakable(debugLocalization);
    m_rootFolders.push_back(debugLocalization);
}

// ============================================================================
// Tweakables Dump Functionality (from devTweaks)
// ============================================================================

// Function addresses from Ghidra
typedef void* (__fastcall* InitializeDevMenuDataFunc)(int param_1);
typedef void(__cdecl* BuildTweakablesListFunc)(void* this_ptr, void* outputArray, int categoryId);

// ============================================================================
// Game Memory Addresses - UPLAY VERSION (RVA offsets - Ghidra base 0x700000)
// ============================================================================

// InitializeDevMenuData: Ghidra 0x00d648c0, RVA = 0x00d648c0 - 0x700000 = 0x6648c0
static constexpr uintptr_t INIT_DEV_MENU_DATA_RVA_UPLAY = 0x6648c0;

// BuildTweakablesList: Ghidra 0x00d623e0, RVA = 0x00d623e0 - 0x700000 = 0x6623e0
static constexpr uintptr_t BUILD_TWEAKABLES_LIST_RVA_UPLAY = 0x6623e0;

// DAT_01755230 (g_pDevMenuData): Ghidra 0x01755230, RVA = 0x01755230 - 0x700000 = 0x1055230
static constexpr uintptr_t GLOBAL_DEV_MENU_DATA_RVA_UPLAY = 0x1055230;

// ============================================================================
// Game Memory Addresses - STEAM VERSION (RVA offsets - Ghidra base 0x140000)
// ============================================================================

// InitializeDevMenuData: Ghidra 0x007a3360, RVA = 0x007a3360 - 0x140000 = 0x663360
static constexpr uintptr_t INIT_DEV_MENU_DATA_RVA_STEAM = 0x663360;

// BuildTweakablesList: Ghidra 0x007a0e80 (estimated), RVA = 0x007a0e80 - 0x140000 = 0x660e80
// TODO: This address is estimated based on relative offset - verify in Steam Ghidra
static constexpr uintptr_t BUILD_TWEAKABLES_LIST_RVA_STEAM = 0x660e80;

// DAT_01197230 (g_pDevMenuData): Ghidra 0x01197230, RVA = 0x01197230 - 0x140000 = 0x1057230
static constexpr uintptr_t GLOBAL_DEV_MENU_DATA_RVA_STEAM = 0x1057230;

// ============================================================================
// Helper functions to get correct RVA based on detected version
// ============================================================================

static uintptr_t GetInitDevMenuDataRVA() {
    return BaseAddress::IsSteamVersion() ? INIT_DEV_MENU_DATA_RVA_STEAM : INIT_DEV_MENU_DATA_RVA_UPLAY;
}

static uintptr_t GetBuildTweakablesListRVA() {
    return BaseAddress::IsSteamVersion() ? BUILD_TWEAKABLES_LIST_RVA_STEAM : BUILD_TWEAKABLES_LIST_RVA_UPLAY;
}

static uintptr_t GetGlobalDevMenuDataRVA() {
    return BaseAddress::IsSteamVersion() ? GLOBAL_DEV_MENU_DATA_RVA_STEAM : GLOBAL_DEV_MENU_DATA_RVA_UPLAY;
}

// Helper functions to safely read values (SEH-enabled, no C++ objects)
static bool SafeReadName(char* namePtr, char* buffer, int bufferSize) {
    __try {
        if (namePtr != nullptr && namePtr[0] >= 0x20 && namePtr[0] < 0x7F) {
            int i = 0;
            while (i < bufferSize - 1 && namePtr[i] != '\0' && namePtr[i] >= 0x20 && namePtr[i] < 0x7F) {
                buffer[i] = namePtr[i];
                i++;
            }
            buffer[i] = '\0';
            return true;
        }
        return false;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SafeReadBoolValue(void** valuePtr, int* outValue) {
    __try {
        if (*valuePtr != nullptr) {
            int* intValue = (int*)*valuePtr;
            *outValue = *intValue;
            return true;
        }
        return false;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SafeReadIntValue(void** valuePtr, int* outValue) {
    __try {
        if (*valuePtr != nullptr) {
            int* intValue = (int*)*valuePtr;
            *outValue = *intValue;
            return true;
        }
        return false;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SafeReadFloatValue(void** valuePtr, float* outValue) {
    __try {
        if (*valuePtr != nullptr) {
            float* floatValue = (float*)*valuePtr;
            *outValue = *floatValue;
            return true;
        }
        return false;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Recursive helper function to dump a category and all its children
static void DumpCategoryRecursive(void* devMenuData, uintptr_t buildTweakablesListAddr, int categoryId, int depth) {
    int outputArray[3] = { 0, 2, 0 };
    void* arrayData = malloc(8);
    outputArray[2] = (int)arrayData;

    __asm {
        mov ecx, devMenuData
        lea eax, outputArray
        push categoryId
        push eax
        call buildTweakablesListAddr
    }

    LOG_VERBOSE("Category " << categoryId << " (depth " << depth << "): Found " << outputArray[0] << " items");

    void** tweakablePointers = (void**)outputArray[2];

    for (int i = 0; i < outputArray[0]; i++) {
        void* tweakablePtr = tweakablePointers[i];

        if (tweakablePtr != nullptr) {
            unsigned int* data = (unsigned int*)tweakablePtr;
            int type = data[1];  // +0x04
            int id = data[2];     // +0x08
            char* namePtr = *(char**)(data + 0x14 / 4);

            const char* typeStr = "Unknown";
            if (type == 0) typeStr = "Folder";
            else if (type == 1) typeStr = "Bool";
            else if (type == 2) typeStr = "Int";
            else if (type == 3) typeStr = "Float";

            // Build indentation string
            std::string indent(depth * 2, ' ');

            // Try to read the name safely
            char nameBuffer[256];
            if (SafeReadName(namePtr, nameBuffer, sizeof(nameBuffer))) {
                std::string output = indent + nameBuffer + " (" + typeStr + ", ID=" + std::to_string(id) + ")";

                // Show value for non-folders
                if (type >= 1 && type <= 3) {
                    void** valuePtr = (void**)(data + 0x1bc / 4);

                    if (type == 1) {  // Bool
                        int value;
                        if (SafeReadBoolValue(valuePtr, &value)) {
                            output += " = " + std::string(value ? "true" : "false");
                        }
                        else {
                            output += " = (read failed)";
                        }
                    }
                    else if (type == 2) {  // Int
                        int value;
                        if (SafeReadIntValue(valuePtr, &value)) {
                            output += " = " + std::to_string(value);
                        }
                        else {
                            output += " = (read failed)";
                        }
                    }
                    else if (type == 3) {  // Float
                        float value;
                        if (SafeReadFloatValue(valuePtr, &value)) {
                            char floatBuf[32];
                            snprintf(floatBuf, sizeof(floatBuf), " = %.3f", value);
                            output += floatBuf;
                        }
                        else {
                            output += " = (read failed)";
                        }
                    }
                }

                LOG_INFO(output);

                // Recursively dump if it's a folder
                if (type == 0) {
                    DumpCategoryRecursive(devMenuData, buildTweakablesListAddr, id, depth + 1);
                }
            }
            else {
                std::string failMsg = indent + "(name read failed, ID=" + std::to_string(id) + ")";
                LOG_INFO(failMsg);
            }
        }
    }

    free(arrayData);
}

void DevMenu::DumpTweakablesData() {
    LOG_INFO("===== DUMPING ALL TWEAKABLES (RECURSIVE) =====");

    // Check if Steam version and warn about unverified address
    if (BaseAddress::IsSteamVersion()) {
        LOG_WARNING("[DevMenu] Steam version detected - BuildTweakablesList address is estimated and may not work");
    }

    uintptr_t baseAddress = (uintptr_t)GetModuleHandle(NULL);

    // Get pointer to global dev menu data using version-aware RVA
    void** globalDevMenuDataPtr = (void**)(baseAddress + GetGlobalDevMenuDataRVA());
    void* devMenuData = *globalDevMenuDataPtr;

    LOG_VERBOSE("Base address: 0x" << std::hex << baseAddress);
    LOG_VERBOSE("Global dev menu data ptr: 0x" << std::hex << (uintptr_t)globalDevMenuDataPtr);

    if (devMenuData == nullptr) {
        LOG_INFO("Initializing dev menu data...");
        InitializeDevMenuDataFunc initDevMenuData = (InitializeDevMenuDataFunc)(baseAddress + GetInitDevMenuDataRVA());
        devMenuData = initDevMenuData(0);
        *globalDevMenuDataPtr = devMenuData;
    }

    if (devMenuData == nullptr) {
        LOG_ERROR("Failed to initialize dev menu data!");
        return;
    }

    LOG_INFO("Dev menu data @ 0x" << std::hex << (uintptr_t)devMenuData);

    uintptr_t buildTweakablesListAddr = baseAddress + GetBuildTweakablesListRVA();
    LOG_VERBOSE("BuildTweakablesList @ 0x" << std::hex << buildTweakablesListAddr);
    LOG_INFO("");

    // Start recursive dump from category 0 (top level)
    DumpCategoryRecursive(devMenuData, buildTweakablesListAddr, 0, 0);

    LOG_INFO("");
    LOG_INFO("===== END DUMP =====");
}

// ============================================================================
// MOD Category - Custom Modded Values (not synced to game tweakables)
// ============================================================================

void DevMenu::InitializeMod() {
    auto mod = std::make_shared<TweakableFolder>(10000, "Mod");

    // Fixed width for consistent button sizing
    const float totalButtonsWidth = 316.0f;

    // ============================================================================
    // Gear Customization Subcategory
    // ============================================================================
    auto appearanceFolder = std::make_shared<TweakableFolder>(10070, "Gear Customization");

    auto appearanceReload = std::make_shared<TweakableAppearanceReload>(
        10072,
        "Rider / Bike Tint Refresh"
    );
    m_appearanceReload = appearanceReload;
    RegisterTweakable(appearanceReload);
    appearanceFolder->AddChild(appearanceReload);

    RegisterTweakable(appearanceFolder);

#ifdef DEVELOPMENT_MODE
    // ============================================================================
    // Editor Subcategory
    // ============================================================================
    auto editorFolder = std::make_shared<TweakableFolder>(10180, "Editor");

    auto editorInspector = std::make_shared<TweakableEditorInspector>(
        10181,
        "Selected Object Inspector"
    );
    RegisterTweakable(editorInspector);
    editorFolder->AddChild(editorInspector);

    RegisterTweakable(editorFolder);
#endif

#ifdef DEVELOPMENT_MODE
    // ============================================================================
    // UI / AVM1 Discovery Subcategory
    // ============================================================================
    auto uiExplorerFolder = std::make_shared<TweakableFolder>(10080, "UI View Explorer");

    auto uiExplorer = std::make_shared<TweakableUIViewExplorer>(
        10081,
        "Live Object Probe"
    );
    RegisterTweakable(uiExplorer);
    uiExplorerFolder->AddChild(uiExplorer);

    RegisterTweakable(uiExplorerFolder);
#endif

    // ============================================================================
    // Audio Subcategory
    // ============================================================================
    auto fmodFolder = std::make_shared<TweakableFolder>(10090, "Audio");

    auto fmodControls = std::make_shared<TweakableFmodControls>(
        10091,
        "FMOD Controls"
    );
    RegisterTweakable(fmodControls);
    fmodFolder->AddChild(fmodControls);

    RegisterTweakable(fmodFolder);

    // ============================================================================
    // Checkpoint Subcategory
    // ============================================================================
    auto checkpointFolder = std::make_shared<TweakableFolder>(10040, "Checkpoint");

    // Respawn buttons (aligned in a single row with fixed width)
    const float buttonWidth = 100.0f;  // Fixed width for all three buttons

    // Previous Checkpoint
    auto respawnPrev = std::make_shared<TweakableButton>(
        10004,
        "Previous"
    );
    respawnPrev->SetOnClickCallback([]() {
        Respawn::RespawnAtPreviousCheckpoint();
    });
    respawnPrev->SetRenderInline(false);  // First button, no inline
    respawnPrev->SetFixedWidth(buttonWidth);
    RegisterTweakable(respawnPrev);
    checkpointFolder->AddChild(respawnPrev);

    // Current Checkpoint
    auto respawnCurrent = std::make_shared<TweakableButton>(
        10002,
        "Current"
    );
    respawnCurrent->SetOnClickCallback([]() {
        Respawn::RespawnAtCheckpoint();
    });
    respawnCurrent->SetRenderInline(true);  // Second button, inline
    respawnCurrent->SetFixedWidth(buttonWidth);
    RegisterTweakable(respawnCurrent);
    checkpointFolder->AddChild(respawnCurrent);

    // Next Checkpoint
    auto respawnNext = std::make_shared<TweakableButton>(
        10003,
        "Next"
    );
    respawnNext->SetOnClickCallback([]() {
        Respawn::RespawnAtNextCheckpoint();
    });
    respawnNext->SetRenderInline(true);  // Third button, inline
    respawnNext->SetFixedWidth(buttonWidth);
    RegisterTweakable(respawnNext);
    checkpointFolder->AddChild(respawnNext);

    // Respawn at Checkpoint Index - Slider (no auto-respawn)
    // Total width of three buttons = 100px * 3 = 300px, plus spacing (approx 8px per gap)
    // Using totalButtonsWidth already defined above (316.0f)
    const float sliderWidth = totalButtonsWidth - buttonWidth - 8.0f;  // 208px (316 - 100 - 8)

    g_checkpointIndexSlider = std::make_shared<TweakableInt>(
        10005,
        "Select Checkpoint Index",
        0,      // Default: 0
        0,      // Min: 0
        100     // Max: 100 (will be updated dynamically)
    );
    // Set custom width to account for inline button and hide label
    g_checkpointIndexSlider->SetCustomWidth(sliderWidth);
    g_checkpointIndexSlider->SetHideLabel(true);
    // Go to Selected Checkpoint Button (positioned before slider for inline layout)
    auto goToCheckpointButton = std::make_shared<TweakableButton>(
        10014,
        "GO"
    );
    goToCheckpointButton->SetFixedWidth(buttonWidth);
    goToCheckpointButton->SetRenderInline(false);  // First element on line
    goToCheckpointButton->SetOnClickCallback([]() {
        int selectedIndex = g_checkpointIndexSlider->GetValue();
        int checkpointCount = Respawn::GetCheckpointCount();

        // Update slider range in case track changed
        if (checkpointCount > 0) {
            g_checkpointIndexSlider->SetRange(0, checkpointCount - 1);
            std::string label = "Select Checkpoint (0-" + std::to_string(checkpointCount - 1) + ")";
            g_checkpointIndexSlider->SetName(label);

            // Clamp selected index to valid range
            if (selectedIndex >= checkpointCount) {
                selectedIndex = checkpointCount - 1;
                g_checkpointIndexSlider->SetValue(selectedIndex);
            }
        }

        // Check if user selected the last checkpoint (finish line)
        if (selectedIndex == checkpointCount - 1 && checkpointCount > 0) {
            LOG_INFO("[DevMenu] Last checkpoint selected - using SafeInstantFinish");
            LOG_INFO("[DevMenu] (Respawning at finish line causes softlock)");
            // Route through SafeInstantFinish so prevent-finish fresh checks apply
            PreventFinish::SafeInstantFinish();
        } else {
            LOG_INFO("[DevMenu] Going to checkpoint " << selectedIndex << " (Total checkpoints: " << checkpointCount << ")");
            int currentCp = Respawn::GetCurrentCheckpointIndex();
            if (selectedIndex != currentCp) {
                PreventFinish::NotifyCheckpointSkip();
            }
            Respawn::RespawnAtCheckpointIndex(selectedIndex);
        }
    });
    RegisterTweakable(goToCheckpointButton);
    checkpointFolder->AddChild(goToCheckpointButton);

    // Update the max value based on current track's checkpoint count
    int currentCheckpointCount = Respawn::GetCheckpointCount();
    if (currentCheckpointCount > 0) {
        g_checkpointIndexSlider->SetRange(0, currentCheckpointCount - 1);
        std::string label = "Select Checkpoint (0-" + std::to_string(currentCheckpointCount - 1) + ")";
        g_checkpointIndexSlider->SetName(label);
        g_lastCheckpointCount = currentCheckpointCount;
    }
    // No callback - we'll use a button to apply the respawn
    g_checkpointIndexSlider->SetRenderInline(true);  // Render inline with GO button
    RegisterTweakable(g_checkpointIndexSlider);
    checkpointFolder->AddChild(g_checkpointIndexSlider);

    RegisterTweakable(checkpointFolder);





    // ============================================================================
    // Fault/Time Subcategory
    // ============================================================================
    auto faultTimeFolder = std::make_shared<TweakableFolder>(10050, "Fault/Time");

    // Toggle Fault Limit
    g_toggleFaultLimitButton = std::make_shared<TweakableButton>(
        10006,
        "Toggle Fault Limit [Click to check status]"
    );
    g_toggleFaultLimitButton->SetFixedWidth(totalButtonsWidth);
    g_toggleFaultLimitButton->SetOnClickCallback([]() {
        bool isDisabled = Limits::IsFaultValidationDisabled();
        if (isDisabled) {
            LOG_INFO("[DevMenu] Fault limit is currently DISABLED. Re-enabling now...");
            Limits::EnableFaultValidation();
            Limits::EnableFinishFaultCheck();
            int currentFaults = Respawn::GetFaultCount();
            uint32_t faultLimit = Limits::GetFaultLimit();
            LOG_INFO("[DevMenu] Fault limit ENABLED! (" << faultLimit << " faults, currently at " << currentFaults << ")");
        }
        else {
            LOG_INFO("[DevMenu] Fault limit is currently ENABLED. Disabling now...");
            Limits::DisableFaultLimit();
            Limits::DisableFaultValidation();
            Limits::DisableFinishFaultCheck();
            LOG_INFO("[DevMenu] Fault limit DISABLED!");
        }

        // Note: Button label will be updated automatically in Render() function
        });
    RegisterTweakable(g_toggleFaultLimitButton);
    faultTimeFolder->AddChild(g_toggleFaultLimitButton);

    // Toggle Time Limit
    g_toggleTimeLimitButton = std::make_shared<TweakableButton>(
        10007,
        "Toggle Time Limit [Click to check status]"
    );
    g_toggleTimeLimitButton->SetFixedWidth(totalButtonsWidth);
    g_toggleTimeLimitButton->SetOnClickCallback([]() {
        bool isDisabled = Limits::IsTimeValidationDisabled();
        if (isDisabled) {
            LOG_INFO("[DevMenu] Time limit is currently DISABLED. Re-enabling now...");
            Limits::EnableTimeValidation();
            int currentTimeMs = Respawn::GetRaceTimeMs();
            uint32_t timeLimit = Limits::GetTimeLimit();
            int timeLimitMs = (int)(timeLimit * 1000 / 60);  // Convert ticks to ms
            LOG_INFO("[DevMenu] Time limit ENABLED! (" << timeLimitMs / 60 << " minutes, currently at " << currentTimeMs / 1000 << "s)");
        } else {
            LOG_INFO("[DevMenu] Time limit is currently ENABLED. Disabling now...");
            Limits::DisableTimeLimit();
            Limits::DisableTimeValidation();
            Limits::DisableRaceUpdateTimerFreeze();  // Also disable timer freeze
            Limits::DisableTimeCompletionCheck2();   // And the second time check
            LOG_INFO("[DevMenu] Time limit DISABLED!");
        }

        // Note: Button label will be updated automatically in Render() function
    });
    RegisterTweakable(g_toggleTimeLimitButton);
    faultTimeFolder->AddChild(g_toggleTimeLimitButton);

    // ============================================================================
    // Fault Controls
    // ============================================================================

    // Fault Once
    auto faultOnce = std::make_shared<TweakableButton>(
        10008,
        "Fault Once"
    );
    faultOnce->SetFixedWidth(totalButtonsWidth);
    faultOnce->SetOnClickCallback([]() {
        Respawn::IncrementFaultCounter();
    });
    RegisterTweakable(faultOnce);
    faultTimeFolder->AddChild(faultOnce);

    // Calculate widths for inline sliders
    const float inlineSliderWidth = (totalButtonsWidth - 8.0f) / 2.0f;  // Split width with gap

    // Add/Remove Faults label (header text)
    auto faultLabel = std::make_shared<TweakableButton>(
        10009,
        "Add/Remove Faults"
    );
    faultLabel->SetFixedWidth(inlineSliderWidth);
    faultLabel->SetRenderInline(false);  // Start new line
    faultLabel->SetCustomColors(
        ImVec4(0.0f, 0.0f, 0.0f, 0.0f),  // Transparent background
        ImVec4(0.0f, 0.0f, 0.0f, 0.0f),
        ImVec4(0.0f, 0.0f, 0.0f, 0.0f)
    );
    RegisterTweakable(faultLabel);
    faultTimeFolder->AddChild(faultLabel);

    // Add/Remove Time label (header text)
    auto timeLabel = std::make_shared<TweakableButton>(
        10022,
        "Add/Remove Time"
    );
    timeLabel->SetFixedWidth(inlineSliderWidth);
    timeLabel->SetRenderInline(true);  // Inline with fault label
    timeLabel->SetCustomColors(
        ImVec4(0.0f, 0.0f, 0.0f, 0.0f),  // Transparent background
        ImVec4(0.0f, 0.0f, 0.0f, 0.0f),
        ImVec4(0.0f, 0.0f, 0.0f, 0.0f)
    );
    RegisterTweakable(timeLabel);
    faultTimeFolder->AddChild(timeLabel);

    // Add/Remove Faults slider
    auto faultAdjust = std::make_shared<TweakableInt>(
        10010,
        "",  // No label - using header above
        0,      // Default: 0
        -500,   // Min: -500
        500     // Max: +500
    );
    faultAdjust->SetCustomWidth(inlineSliderWidth);
    faultAdjust->SetHideLabel(true);  // Hide the inline label
    faultAdjust->SetRenderInline(false);  // First on line
    faultAdjust->SetOnChangeCallback([faultAdjust](int value) {
        if (value != 0) {
            Respawn::IncrementFaultCounterBy(value);
            faultAdjust->SetValue(0);
        }
    });
    RegisterTweakable(faultAdjust);
    faultTimeFolder->AddChild(faultAdjust);

    // Add/Remove Time slider
    auto timeAdjust = std::make_shared<TweakableInt>(
        10011,
        "",  // No label - using header above
        0,      // Default: 0
        -1800,  // Min: -30 minutes
        1800    // Max: +30 minutes
    );
    timeAdjust->SetCustomWidth(inlineSliderWidth);
    timeAdjust->SetHideLabel(true);  // Hide the inline label
    timeAdjust->SetRenderInline(true);  // Render inline with fault slider
    timeAdjust->SetOnChangeCallback([timeAdjust](int value) {
        if (value != 0) {
            Respawn::AdjustRaceTimeMs(value * 1000); // Convert to milliseconds
            timeAdjust->SetValue(0);
        }
    });
    RegisterTweakable(timeAdjust);
    faultTimeFolder->AddChild(timeAdjust);

    // Instant Actions Label and Buttons

    // Calculate width for inline buttons (3 buttons fitting in totalButtonsWidth)
    const float instantButtonWidth = (totalButtonsWidth - 16.0f) / 3.0f;  // Account for spacing

    // Instant Time Out (30 minutes) - Trigger instant timeout finish
    auto instantTimeOut = std::make_shared<TweakableButton>(
        10013,
        "Timeout"
    );
    instantTimeOut->SetFixedWidth(instantButtonWidth);
    instantTimeOut->SetRenderInline(false);  // First button on new line
    instantTimeOut->SetOnClickCallback([]() {
        Limits::InstantTimeOut();
    });
    RegisterTweakable(instantTimeOut);
    faultTimeFolder->AddChild(instantTimeOut);

    // Instant Fault Out (500 faults) - Trigger instant fault out finish
    auto instantFaultOut = std::make_shared<TweakableButton>(
        10012,
        "Fault-out"
    );

    instantFaultOut->SetFixedWidth(instantButtonWidth);
    instantFaultOut->SetRenderInline(true);  // Second button, inline
    instantFaultOut->SetOnClickCallback([]() {
        Limits::InstantFaultOut();
    });
    RegisterTweakable(instantFaultOut);
    faultTimeFolder->AddChild(instantFaultOut);

    // Instant Finish (normal) - Trigger normal instant finish using SafeInstantFinish
    auto instantFinish = std::make_shared<TweakableButton>(
        10014,
        "Finish"
    );
    instantFinish->SetFixedWidth(instantButtonWidth);
    instantFinish->SetRenderInline(true);  // Third button, inline
    instantFinish->SetOnClickCallback([]() {
        LOG_VERBOSE("[DevMenu] Instant Finish button pressed - calling SafeInstantFinish...");
        PreventFinish::SafeInstantFinish();
        LOG_VERBOSE("[DevMenu] SafeInstantFinish called!");
    });
    RegisterTweakable(instantFinish);
    faultTimeFolder->AddChild(instantFinish);

    // Prevent Finish Status Label (auto-updates based on gamemode)
    auto preventFinishLabel = std::make_shared<TweakableButton>(
        10015,
        PreventFinish::GetStatusString()
    );

    // Set colors: Normal (Red), Hovered (Bright Red), Active (Dark Red)
    preventFinishLabel->SetCustomColors(
        ImVec4(0.8f, 0.1f, 0.1f, 1.0f), // Normal
        ImVec4(1.0f, 0.2f, 0.2f, 1.0f), // Hovered
        ImVec4(0.6f, 0.0f, 0.0f, 1.0f)  // Active
    );

    preventFinishLabel->SetFixedWidth(totalButtonsWidth);
    // No click callback - this is a status display only
    RegisterTweakable(preventFinishLabel);
    faultTimeFolder->AddChild(preventFinishLabel);
    g_preventFinishLabel = preventFinishLabel;

    RegisterTweakable(faultTimeFolder);


#ifdef DEVELOPMENT_MODE
    // ============================================================================
    // Host-Join Controls
    // ============================================================================

    // Create a subfolder for Host-Join controls
    auto hostJoinFolder = std::make_shared<TweakableFolder>(10030, "Host-Join (Private Match)");

    // Scan for Session ID button
    auto scanSessionIdBtn = std::make_shared<TweakableButton>(
        10036,
        "Scan for Session ID"
    );
    scanSessionIdBtn->SetOnClickCallback([]() {
        LOG_INFO("[DevMenu] Scanning for session ID...");
        HostJoin::RefreshSessionId();
        uint32_t sessionId = HostJoin::GetCurrentSessionId();
        if (sessionId != 0) {
            LOG_INFO("[DevMenu] Found Session ID: 0x" << std::hex << std::uppercase << sessionId);
            LOG_INFO("[DevMenu] Session ID has been saved to F:/session_id.txt");
        } else {
            LOG_INFO("[DevMenu] Scan complete - check log for candidates");
        }
    });
    RegisterTweakable(scanSessionIdBtn);
    hostJoinFolder->AddChild(scanSessionIdBtn);

    // Show Current Session ID button
    auto showSessionIdBtn = std::make_shared<TweakableButton>(
        10031,
        "Show Current Session ID"
    );
    showSessionIdBtn->SetOnClickCallback([]() {
        uint32_t sessionId = HostJoin::GetCurrentSessionId();
        if (sessionId != 0) {
            LOG_INFO("[DevMenu] Current Session ID: 0x" << std::hex << std::uppercase << sessionId);
            LOG_INFO("[DevMenu] Share this ID with other players to let them join!");
        } else {
            LOG_INFO("[DevMenu] Not hosting a session. Enter a private match lobby to capture session ID.");
        }
    });
    RegisterTweakable(showSessionIdBtn);
    hostJoinFolder->AddChild(showSessionIdBtn);

    // Copy Session ID to Clipboard button
    auto copySessionIdBtn = std::make_shared<TweakableButton>(
        10032,
        "Copy Session ID to Clipboard"
    );
    copySessionIdBtn->SetOnClickCallback([]() {
        if (HostJoin::CopySessionIdToClipboard()) {
            LOG_INFO("[DevMenu] Session ID copied to clipboard!");
        } else {
            LOG_ERROR("[DevMenu] No session ID to copy - enter a private match lobby first");
        }
    });
    RegisterTweakable(copySessionIdBtn);
    hostJoinFolder->AddChild(copySessionIdBtn);

    // Paste Session ID from Clipboard button
    auto pasteSessionIdBtn = std::make_shared<TweakableButton>(
        10033,
        "Paste Session ID from Clipboard"
    );
    pasteSessionIdBtn->SetOnClickCallback([]() {
        if (HostJoin::PasteSessionIdFromClipboard()) {
            uint32_t targetId = HostJoin::GetTargetSessionId();
            LOG_INFO("[DevMenu] Session ID pasted: 0x" << std::hex << std::uppercase << targetId);
        } else {
            LOG_ERROR("[DevMenu] Failed to paste session ID from clipboard");
        }
    });
    RegisterTweakable(pasteSessionIdBtn);
    hostJoinFolder->AddChild(pasteSessionIdBtn);

    // Join Session button
    auto joinSessionBtn = std::make_shared<TweakableButton>(
        10034,
        "Join Session (use target ID)"
    );
    joinSessionBtn->SetOnClickCallback([]() {
        uint32_t targetId = HostJoin::GetTargetSessionId();
        if (targetId == 0) {
            LOG_ERROR("[DevMenu] No target session ID set! Paste a session ID first.");
            return;
        }
        LOG_INFO("[DevMenu] Attempting to join session: 0x" << std::hex << std::uppercase << targetId);
        if (HostJoin::JoinSession()) {
            LOG_INFO("[DevMenu] Join request sent!");
        } else {
            LOG_ERROR("[DevMenu] Join request may have failed - check logs");
        }
    });
    RegisterTweakable(joinSessionBtn);
    hostJoinFolder->AddChild(joinSessionBtn);

    // Show Host-Join Status button
    auto showStatusBtn = std::make_shared<TweakableButton>(
        10035,
        "Show Host-Join Status"
    );
    showStatusBtn->SetOnClickCallback([]() {
        LOG_INFO("[DevMenu] === Host-Join Status ===");
        LOG_INFO("[DevMenu] Is Hosting: " << (HostJoin::IsHostingSession() ? "Yes" : "No"));
        LOG_INFO("[DevMenu] Current Session ID: 0x" << std::hex << std::uppercase << HostJoin::GetCurrentSessionId());
        LOG_INFO("[DevMenu] Target Session ID: 0x" << std::hex << std::uppercase << HostJoin::GetTargetSessionId());
        LOG_INFO("[DevMenu] Status: " << HostJoin::GetStatusMessage());
    });
    RegisterTweakable(showStatusBtn);
    hostJoinFolder->AddChild(showStatusBtn);

    RegisterTweakable(hostJoinFolder);
#endif

    // ============================================================================
    // Currency Subcategory
    // ============================================================================
    auto currencyFolder = std::make_shared<TweakableFolder>(10060, "Currency");

    // ============================================================================
    // ROW 1: Get Balance Button
    // ============================================================================

    // Get Money Balance Button
    auto getMoneyBalanceButton = std::make_shared<TweakableButton>(
        10021,
        "Get Money"
    );
    getMoneyBalanceButton->SetFixedWidth(totalButtonsWidth);
    getMoneyBalanceButton->SetRenderInline(false);  // First on line
    getMoneyBalanceButton->SetOnClickCallback([]() {
        int balance = Money::GetBalance();
        if (balance >= 0) {
            LOG_INFO("[DevMenu] Current money balance: " << balance);
        } else {
            LOG_WARNING("[DevMenu] Could not retrieve money balance - are you in-game?");
        }
    });
    RegisterTweakable(getMoneyBalanceButton);
    currencyFolder->AddChild(getMoneyBalanceButton);

    // ============================================================================
    // ROW 2: Amount Slider
    // ============================================================================

    // Money Amount Slider
    auto moneyAmount = std::make_shared<TweakableInt>(
        10022,
        "Money",
        1000,     // Default: 1000
        1,        // Min: 1 (AwardMoneyToPlayer only supports positive)
        100000    // Max: +100000
    );
    moneyAmount->SetCustomWidth(totalButtonsWidth);
    moneyAmount->SetHideLabel(true);
    moneyAmount->SetRenderInline(false);  // First on line
    RegisterTweakable(moneyAmount);
    currencyFolder->AddChild(moneyAmount);

    // ============================================================================
    // ROW 3: Add Button
    // ============================================================================

    // Add Money Button
    auto addMoneyButton = std::make_shared<TweakableButton>(
        10023,
        "Add Money"
    );
    addMoneyButton->SetFixedWidth(totalButtonsWidth);
    addMoneyButton->SetRenderInline(false);  // First on line
    addMoneyButton->SetOnClickCallback([moneyAmount]() {
        int amount = moneyAmount->GetValue();
        LOG_INFO("[DevMenu] Adding " << amount << " money to player balance");

        if (Money::AddToBalance(amount)) {
            LOG_INFO("[DevMenu] Successfully added " << amount << " money!");
        } else {
            LOG_ERROR("[DevMenu] Failed to add money - check if player profile is loaded");
        }
    });
    RegisterTweakable(addMoneyButton);
    currencyFolder->AddChild(addMoneyButton);

    RegisterTweakable(currencyFolder);

    // ============================================================================
    // Files Subcategory
    // ============================================================================
    auto filesFolder = std::make_shared<TweakableFolder>(10190, "Files");

    auto unlockItemsButton = std::make_shared<TweakableFileUnlockControls>(
        10191,
        "Unlock All Items"
    );
    RegisterTweakable(unlockItemsButton);
    filesFolder->AddChild(unlockItemsButton);

    RegisterTweakable(filesFolder);

    mod->AddChild(appearanceFolder);
    mod->AddChild(fmodFolder);
    mod->AddChild(checkpointFolder);
    mod->AddChild(currencyFolder);
    mod->AddChild(faultTimeFolder);
    mod->AddChild(filesFolder);
#ifdef DEVELOPMENT_MODE
    mod->AddChild(editorFolder);
    mod->AddChild(hostJoinFolder);
#endif
#ifdef DEVELOPMENT_MODE
    mod->AddChild(uiExplorerFolder);
#endif

    RegisterTweakable(mod);
    m_rootFolders.push_back(mod);
}

// Keybindings Tab - Menu bar accessible keybinding configuration
// ============================================================================

// Thread data structure for keybinding capture
struct KeybindThreadData {
    TweakableButton* button;
    Keybindings::Action action;
    bool* waitingFlag;
};

static void UpdateGamepadBindingCapture() {
    if (!g_waitingForGamepadBind) {
        return;
    }

    if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) {
        Keybindings::SetGamepadButton(g_waitingGamepadAction, 0);
        LOG_VERBOSE("[DevMenu] Cleared gamepad bind for " << Keybindings::GetActionName(g_waitingGamepadAction));
        g_waitingForGamepadBind = false;
        return;
    }

    int pressedButton = Keybindings::GetPressedGamepadButton();
    if (!g_gamepadCaptureReady) {
        g_gamepadCaptureReady = pressedButton == 0;
        return;
    }

    if (pressedButton != 0) {
        Keybindings::SetGamepadButton(g_waitingGamepadAction, pressedButton);
        LOG_VERBOSE("[DevMenu] " << Keybindings::GetActionName(g_waitingGamepadAction)
            << " gamepad bound to: " << Keybindings::GetGamepadButtonName(pressedButton));
        g_waitingForGamepadBind = false;
    }
}

// Helper function to create a keybinding button
static std::shared_ptr<TweakableButton> CreateKeybindButton(
    int id,
    Keybindings::Action action,
    bool* waitingFlag,
    DevMenu* devMenu)
{
    auto button = std::make_shared<TweakableButton>(
        id,
        "Bind " + Keybindings::GetActionName(action) + ": " +
        Keybindings::GetKeyName(Keybindings::GetKey(action))
    );

    button->SetOnClickCallback([button, action, waitingFlag]() {
        *waitingFlag = true;
        button->SetName("Press any key... (Esc to clear)");
        LOG_VERBOSE("[DevMenu] Waiting for key press to bind " << Keybindings::GetActionName(action) << "...");

        // Create thread data
        KeybindThreadData* data = new KeybindThreadData;
        data->button = button.get();
        data->action = action;
        data->waitingFlag = waitingFlag;

        // Start a thread to capture the key press
        CreateThread(NULL, 0, [](LPVOID param) -> DWORD {
            KeybindThreadData* data = (KeybindThreadData*)param;

            // Wait for any key press
            while (*data->waitingFlag) {
                for (int vk = 0x08; vk <= 0xFE; vk++) {
                    // Skip mouse buttons
                    if (vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON ||
                        vk == VK_XBUTTON1 || vk == VK_XBUTTON2) {
                        continue;
                    }

                    if (GetAsyncKeyState(vk) & 0x8000) {
                        // Wait for key release
                        while (GetAsyncKeyState(vk) & 0x8000) {
                            Sleep(10);
                        }

                        // Esc clears the binding instead of assigning a key
                        int newKey = (vk == VK_ESCAPE) ? 0 : vk;
                        Keybindings::SetKey(data->action, newKey);
                        std::string keyName = Keybindings::GetKeyName(newKey);
                        data->button->SetName("Bind " + Keybindings::GetActionName(data->action) + ": " + keyName);
                        if (newKey == 0) {
                            LOG_VERBOSE("[DevMenu] " << Keybindings::GetActionName(data->action) << " cleared");
                        } else {
                            LOG_VERBOSE("[DevMenu] " << Keybindings::GetActionName(data->action) << " bound to: " << keyName);
                        }
                        *data->waitingFlag = false;
                        delete data;
                        return 0;
                    }
                }
                Sleep(50);
            }
            delete data;
            return 0;
        }, data, 0, NULL);
    });

    return button;
}

void DevMenu::InitializeKeybindings() {
    // Static flags for each keybinding (to track if waiting for key press)
    static bool waitingForInstantFinish = false;
    static bool waitingForToggleDevMenu = false;
    static bool waitingForToggleKeybindingsMenu = false;
    static bool waitingForClearConsole = false;
    static bool waitingForToggleVerbose = false;
    static bool waitingForShowHelp = false;
    static bool waitingForDumpTweakables = false;
    static bool waitingForCycleHUD = false;
    static bool waitingForRespawnAtCheckpoint = false;
    static bool waitingForRespawnPrevCheckpoint = false;
    static bool waitingForRespawnNextCheckpoint = false;
    static bool waitingForRespawnForward5 = false;
    static bool waitingForCaptureSaveState = false;
    static bool waitingForRestoreSaveState = false;
    static bool waitingForDebugSaveState = false;
    static bool waitingForIncrementFault = false;
    static bool waitingForDebugFaultCounter = false;
    static bool waitingForAdd100Faults = false;
    static bool waitingForSubtract100Faults = false;
    static bool waitingForResetFaults = false;
    static bool waitingForDebugTimeCounter = false;
    static bool waitingForAdd10Seconds = false;
    static bool waitingForSubtract10Seconds = false;
    static bool waitingForAdd1Minute = false;
    static bool waitingForResetTime = false;
    static bool waitingForRestoreDefaultLimits = false;
    static bool waitingForDebugLimits = false;
    static bool waitingForToggleLimitValidation = false;
    static bool waitingForTogglePause = false;
    static bool waitingForScanLeaderboardByID = false;
    static bool waitingForScanCurrentLeaderboard = false;
    static bool waitingForStartAutoScroll = false;
    static bool waitingForKillswitch = false;
    static bool waitingForCycleSearch = false;
    static bool waitingForDecreaseScrollDelay = false;
    static bool waitingForIncreaseScrollDelay = false;
    static bool waitingForTestFetchTrackID = false;
    static bool waitingForTogglePatch = false;
    static bool waitingForSaveMultiplayerLogs = false;
    static bool waitingForCaptureSessionState = false;
    static bool waitingForTogglePhysicsLogging = false;
    static bool waitingForDumpPhysicsLog = false;
    static bool waitingForModifyXPosition = false;
    static bool waitingForFullCountdownSequence = false;
    static bool waitingForShowSingleCountdown = false;
    static bool waitingForToggleLoadScreen = false;
    static bool waitingForToggleOverlay = false;
    static bool waitingForSwapNextBike = false;
    static bool waitingForSwapPrevBike = false;
    static bool waitingForDebugBikeInfo = false;
#ifdef DEVELOPMENT_MODE
    static bool waitingForEditorScaleDecrease = false;
    static bool waitingForEditorScaleIncrease = false;
#endif
    static bool waitingForToggleConsole = false;

    // Clear the action and default vectors in case of re-initialization
    m_keybindingActions.clear();
    m_keybindingDefaults.clear();
    m_gamepadBindingDefaults.clear();

    // === General Controls ===

    // Toggle Dev Menu
    auto toggleDevMenuBtn = CreateKeybindButton(10101, Keybindings::Action::ToggleDevMenu, &waitingForToggleDevMenu, this);
    RegisterTweakable(toggleDevMenuBtn);
    m_keybindingItems.push_back(toggleDevMenuBtn);
    m_keybindingActions.push_back(Keybindings::Action::ToggleDevMenu);
    m_keybindingDefaults.push_back(VK_F2);

    // Toggle Keybindings Menu
    auto toggleKeybindingsMenuBtn = CreateKeybindButton(10100, Keybindings::Action::ToggleKeybindingsMenu, &waitingForToggleKeybindingsMenu, this);
    RegisterTweakable(toggleKeybindingsMenuBtn);
    m_keybindingItems.push_back(toggleKeybindingsMenuBtn);
    m_keybindingActions.push_back(Keybindings::Action::ToggleKeybindingsMenu);
    m_keybindingDefaults.push_back('K');

    // Toggle Overlay
    auto toggleOverlayBtn = CreateKeybindButton(10144, Keybindings::Action::ToggleOverlay, &waitingForToggleOverlay, this);
    RegisterTweakable(toggleOverlayBtn);
    m_keybindingItems.push_back(toggleOverlayBtn);
    m_keybindingActions.push_back(Keybindings::Action::ToggleOverlay);
    m_keybindingDefaults.push_back(VK_F4);

#ifdef DEVELOPMENT_MODE
    // Toggle ImGui Console
    auto toggleConsoleBtn = CreateKeybindButton(10148, Keybindings::Action::ToggleConsole, &waitingForToggleConsole, this);
    RegisterTweakable(toggleConsoleBtn);
    m_keybindingItems.push_back(toggleConsoleBtn);
    m_keybindingActions.push_back(Keybindings::Action::ToggleConsole);
    m_keybindingDefaults.push_back(VK_F11);
#endif

#ifndef RELEASE_AUTOLOAD_MODE
    // Clear Console
    auto clearConsoleBtn = CreateKeybindButton(10102, Keybindings::Action::ClearConsole, &waitingForClearConsole, this);
    RegisterTweakable(clearConsoleBtn);
    m_keybindingItems.push_back(clearConsoleBtn);
    m_keybindingActions.push_back(Keybindings::Action::ClearConsole);
    m_keybindingDefaults.push_back('C');
#endif

    // Toggle Verbose Logging
    auto toggleVerboseBtn = CreateKeybindButton(10103, Keybindings::Action::ToggleVerboseLogging, &waitingForToggleVerbose, this);
    RegisterTweakable(toggleVerboseBtn);
    m_keybindingItems.push_back(toggleVerboseBtn);
    m_keybindingActions.push_back(Keybindings::Action::ToggleVerboseLogging);
    m_keybindingDefaults.push_back('L');

#ifndef RELEASE_AUTOLOAD_MODE
    // Show Help Text
    auto showHelpBtn = CreateKeybindButton(10104, Keybindings::Action::ShowHelpText, &waitingForShowHelp, this);
    RegisterTweakable(showHelpBtn);
    m_keybindingItems.push_back(showHelpBtn);
    m_keybindingActions.push_back(Keybindings::Action::ShowHelpText);
    m_keybindingDefaults.push_back(VK_OEM_MINUS);

    // Dump Tweakables
    auto dumpTweakablesBtn = CreateKeybindButton(10105, Keybindings::Action::DumpTweakables, &waitingForDumpTweakables, this);
    RegisterTweakable(dumpTweakablesBtn);
    m_keybindingItems.push_back(dumpTweakablesBtn);
    m_keybindingActions.push_back(Keybindings::Action::DumpTweakables);
    m_keybindingDefaults.push_back('D');
#endif

    // === Respawn Controls ===

    // Respawn At Checkpoint
    auto respawnAtCheckpointBtn = CreateKeybindButton(10108, Keybindings::Action::RespawnAtCheckpoint, &waitingForRespawnAtCheckpoint, this);
    RegisterTweakable(respawnAtCheckpointBtn);
    m_keybindingItems.push_back(respawnAtCheckpointBtn);
    m_keybindingActions.push_back(Keybindings::Action::RespawnAtCheckpoint);
    m_keybindingDefaults.push_back('W');

    // Respawn Prev Checkpoint
    auto respawnPrevCheckpointBtn = CreateKeybindButton(10109, Keybindings::Action::RespawnPrevCheckpoint, &waitingForRespawnPrevCheckpoint, this);
    RegisterTweakable(respawnPrevCheckpointBtn);
    m_keybindingItems.push_back(respawnPrevCheckpointBtn);
    m_keybindingActions.push_back(Keybindings::Action::RespawnPrevCheckpoint);
    m_keybindingDefaults.push_back('Q');

    // Respawn Next Checkpoint
    auto respawnNextCheckpointBtn = CreateKeybindButton(10110, Keybindings::Action::RespawnNextCheckpoint, &waitingForRespawnNextCheckpoint, this);
    RegisterTweakable(respawnNextCheckpointBtn);
    m_keybindingItems.push_back(respawnNextCheckpointBtn);
    m_keybindingActions.push_back(Keybindings::Action::RespawnNextCheckpoint);
    m_keybindingDefaults.push_back('E');

    // Respawn Forward 5
    auto respawnForward5Btn = CreateKeybindButton(10111, Keybindings::Action::RespawnForward5, &waitingForRespawnForward5, this);
    RegisterTweakable(respawnForward5Btn);
    m_keybindingItems.push_back(respawnForward5Btn);
    m_keybindingActions.push_back(Keybindings::Action::RespawnForward5);
    m_keybindingDefaults.push_back(0);

    // Instant Finish
    auto instantFinishBtn = CreateKeybindButton(10106, Keybindings::Action::InstantFinish, &waitingForInstantFinish, this);
    RegisterTweakable(instantFinishBtn);
    m_keybindingItems.push_back(instantFinishBtn);
    m_keybindingActions.push_back(Keybindings::Action::InstantFinish);
    m_keybindingDefaults.push_back('F');

    // === Save States ===

    // Capture Save State
    auto captureSaveStateBtn = CreateKeybindButton(10184, Keybindings::Action::CaptureSaveState, &waitingForCaptureSaveState, this);
    RegisterTweakable(captureSaveStateBtn);
    m_keybindingItems.push_back(captureSaveStateBtn);
    m_keybindingActions.push_back(Keybindings::Action::CaptureSaveState);
    m_keybindingDefaults.push_back(0);

    // Restore Save State
    auto restoreSaveStateBtn = CreateKeybindButton(10185, Keybindings::Action::RestoreSaveState, &waitingForRestoreSaveState, this);
    RegisterTweakable(restoreSaveStateBtn);
    m_keybindingItems.push_back(restoreSaveStateBtn);
    m_keybindingActions.push_back(Keybindings::Action::RestoreSaveState);
    m_keybindingDefaults.push_back(0);

#ifdef DEVELOPMENT_MODE
    // Debug Save State
    auto debugSaveStateBtn = CreateKeybindButton(10186, Keybindings::Action::DebugSaveState, &waitingForDebugSaveState, this);
    RegisterTweakable(debugSaveStateBtn);
    m_keybindingItems.push_back(debugSaveStateBtn);
    m_keybindingActions.push_back(Keybindings::Action::DebugSaveState);
    m_keybindingDefaults.push_back(0);
#endif

    // === Fault / Time / Limit Controls ===
    // Toggle Limit Validation
    auto toggleLimitValidationBtn = CreateKeybindButton(10125, Keybindings::Action::ToggleLimitValidation, &waitingForToggleLimitValidation, this);
    RegisterTweakable(toggleLimitValidationBtn);
    m_keybindingItems.push_back(toggleLimitValidationBtn);
    m_keybindingActions.push_back(Keybindings::Action::ToggleLimitValidation);
    m_keybindingDefaults.push_back(VK_F3);

    // Reset Faults
    auto resetFaultsBtn = CreateKeybindButton(10116, Keybindings::Action::ResetFaults, &waitingForResetFaults, this);
    RegisterTweakable(resetFaultsBtn);
    m_keybindingItems.push_back(resetFaultsBtn);
    m_keybindingActions.push_back(Keybindings::Action::ResetFaults);
    m_keybindingDefaults.push_back('1');

    // Reset Time
    auto resetTimeBtn = CreateKeybindButton(10121, Keybindings::Action::ResetTime, &waitingForResetTime, this);
    RegisterTweakable(resetTimeBtn);
    m_keybindingItems.push_back(resetTimeBtn);
    m_keybindingActions.push_back(Keybindings::Action::ResetTime);
    m_keybindingDefaults.push_back('2');

    // === Time Controls ===

#ifdef DEVELOPMENT_MODE
    // Debug Time Counter
    auto debugTimeCounterBtn = CreateKeybindButton(10117, Keybindings::Action::DebugTimeCounter, &waitingForDebugTimeCounter, this);
    RegisterTweakable(debugTimeCounterBtn);
    m_keybindingItems.push_back(debugTimeCounterBtn);
    m_keybindingActions.push_back(Keybindings::Action::DebugTimeCounter);
    m_keybindingDefaults.push_back(VK_OEM_4);

    // Debug Fault Counter
    auto debugFaultCounterBtn = CreateKeybindButton(10113, Keybindings::Action::DebugFaultCounter, &waitingForDebugFaultCounter, this);
    RegisterTweakable(debugFaultCounterBtn);
    m_keybindingItems.push_back(debugFaultCounterBtn);
    m_keybindingActions.push_back(Keybindings::Action::DebugFaultCounter);
    m_keybindingDefaults.push_back(VK_OEM_6);
#endif

    // Increment Fault
    auto incrementFaultBtn = CreateKeybindButton(10112, Keybindings::Action::IncrementFault, &waitingForIncrementFault, this);
    RegisterTweakable(incrementFaultBtn);
    m_keybindingItems.push_back(incrementFaultBtn);
    m_keybindingActions.push_back(Keybindings::Action::IncrementFault);
    m_keybindingDefaults.push_back(0);

    // Add 100 Faults
    auto add100FaultsBtn = CreateKeybindButton(10114, Keybindings::Action::Add100Faults, &waitingForAdd100Faults, this);
    RegisterTweakable(add100FaultsBtn);
    m_keybindingItems.push_back(add100FaultsBtn);
    m_keybindingActions.push_back(Keybindings::Action::Add100Faults);
    m_keybindingDefaults.push_back(0);

    // Subtract 100 Faults
    auto subtract100FaultsBtn = CreateKeybindButton(10115, Keybindings::Action::Subtract100Faults, &waitingForSubtract100Faults, this);
    RegisterTweakable(subtract100FaultsBtn);
    m_keybindingItems.push_back(subtract100FaultsBtn);
    m_keybindingActions.push_back(Keybindings::Action::Subtract100Faults);
    m_keybindingDefaults.push_back(0);

    // Add 10 Seconds
    auto add10SecondsBtn = CreateKeybindButton(10118, Keybindings::Action::Add60Seconds, &waitingForAdd10Seconds, this);
    RegisterTweakable(add10SecondsBtn);
    m_keybindingItems.push_back(add10SecondsBtn);
    m_keybindingActions.push_back(Keybindings::Action::Add60Seconds);
    m_keybindingDefaults.push_back(0);

    // Subtract 10 Seconds
    auto subtract10SecondsBtn = CreateKeybindButton(10119, Keybindings::Action::Subtract60Seconds, &waitingForSubtract10Seconds, this);
    RegisterTweakable(subtract10SecondsBtn);
    m_keybindingItems.push_back(subtract10SecondsBtn);
    m_keybindingActions.push_back(Keybindings::Action::Subtract60Seconds);
    m_keybindingDefaults.push_back(0);

    // Add 1 Minute
    auto add1MinuteBtn = CreateKeybindButton(10120, Keybindings::Action::Add10Minute, &waitingForAdd1Minute, this);
    RegisterTweakable(add1MinuteBtn);
    m_keybindingItems.push_back(add1MinuteBtn);
    m_keybindingActions.push_back(Keybindings::Action::Add10Minute);
    m_keybindingDefaults.push_back(0);
    // === Pause Controls ===

    // Toggle Pause
    auto togglePauseBtn = CreateKeybindButton(10126, Keybindings::Action::TogglePause, &waitingForTogglePause, this);
    RegisterTweakable(togglePauseBtn);
    m_keybindingItems.push_back(togglePauseBtn);
    m_keybindingActions.push_back(Keybindings::Action::TogglePause);
    m_keybindingDefaults.push_back('P');

    // === Replay Controls ===

    // Cycle HUD Visibility
    auto CycleHUDBtn = CreateKeybindButton(10107, Keybindings::Action::CycleHUD, &waitingForCycleHUD, this);
    RegisterTweakable(CycleHUDBtn);
    m_keybindingItems.push_back(CycleHUDBtn);
    m_keybindingActions.push_back(Keybindings::Action::CycleHUD);
    m_keybindingDefaults.push_back('V');

    // === Bike Swap Controls ===

    // Swap Next Bike
    auto swapNextBikeBtn = CreateKeybindButton(10145, Keybindings::Action::SwapNextBike, &waitingForSwapNextBike, this);
    RegisterTweakable(swapNextBikeBtn);
    m_keybindingItems.push_back(swapNextBikeBtn);
    m_keybindingActions.push_back(Keybindings::Action::SwapNextBike);
    m_keybindingDefaults.push_back(VK_OEM_PERIOD);

    // Swap Previous Bike
    auto swapPrevBikeBtn = CreateKeybindButton(10146, Keybindings::Action::SwapPrevBike, &waitingForSwapPrevBike, this);
    RegisterTweakable(swapPrevBikeBtn);
    m_keybindingItems.push_back(swapPrevBikeBtn);
    m_keybindingActions.push_back(Keybindings::Action::SwapPrevBike);
    m_keybindingDefaults.push_back(VK_OEM_COMMA);

#ifdef DEVELOPMENT_MODE
    // Debug Bike Info
    auto debugBikeInfoBtn = CreateKeybindButton(10147, Keybindings::Action::DebugBikeInfo, &waitingForDebugBikeInfo, this);
    RegisterTweakable(debugBikeInfoBtn);
    m_keybindingItems.push_back(debugBikeInfoBtn);
    m_keybindingActions.push_back(Keybindings::Action::DebugBikeInfo);
    m_keybindingDefaults.push_back(0);
#endif

#ifdef DEVELOPMENT_MODE
    // === Editor Controls ===

    auto editorScaleDecreaseBtn = CreateKeybindButton(
        10182,
        Keybindings::Action::EditorScaleDecrease,
        &waitingForEditorScaleDecrease,
        this);
    RegisterTweakable(editorScaleDecreaseBtn);
    m_keybindingItems.push_back(editorScaleDecreaseBtn);
    m_keybindingActions.push_back(Keybindings::Action::EditorScaleDecrease);
    m_keybindingDefaults.push_back(0);

    auto editorScaleIncreaseBtn = CreateKeybindButton(
        10183,
        Keybindings::Action::EditorScaleIncrease,
        &waitingForEditorScaleIncrease,
        this);
    RegisterTweakable(editorScaleIncreaseBtn);
    m_keybindingItems.push_back(editorScaleIncreaseBtn);
    m_keybindingActions.push_back(Keybindings::Action::EditorScaleIncrease);
    m_keybindingDefaults.push_back(0);
#endif

    // === Track Central Auto-Scroll Controls ===

    // Start Auto Scroll
    auto startAutoScrollBtn = CreateKeybindButton(10129, Keybindings::Action::StartAutoScroll, &waitingForStartAutoScroll, this);
    RegisterTweakable(startAutoScrollBtn);
    m_keybindingItems.push_back(startAutoScrollBtn);
    m_keybindingActions.push_back(Keybindings::Action::StartAutoScroll);
    m_keybindingDefaults.push_back(VK_F5);

    // Killswitch
    auto killswitchBtn = CreateKeybindButton(10130, Keybindings::Action::Killswitch, &waitingForKillswitch, this);
    RegisterTweakable(killswitchBtn);
    m_keybindingItems.push_back(killswitchBtn);
    m_keybindingActions.push_back(Keybindings::Action::Killswitch);
    m_keybindingDefaults.push_back(VK_F6);

    // Cycle Search
    auto cycleSearchBtn = CreateKeybindButton(10131, Keybindings::Action::CycleSearch, &waitingForCycleSearch, this);
    RegisterTweakable(cycleSearchBtn);
    m_keybindingItems.push_back(cycleSearchBtn);
    m_keybindingActions.push_back(Keybindings::Action::CycleSearch);
    m_keybindingDefaults.push_back(VK_F7);

    // Decrease Scroll Delay
    auto decreaseScrollDelayBtn = CreateKeybindButton(10132, Keybindings::Action::DecreaseScrollDelay, &waitingForDecreaseScrollDelay, this);
    RegisterTweakable(decreaseScrollDelayBtn);
    m_keybindingItems.push_back(decreaseScrollDelayBtn);
    m_keybindingActions.push_back(Keybindings::Action::DecreaseScrollDelay);
    m_keybindingDefaults.push_back(VK_INSERT);

    // Increase Scroll Delay
    auto increaseScrollDelayBtn = CreateKeybindButton(10133, Keybindings::Action::IncreaseScrollDelay, &waitingForIncreaseScrollDelay, this);
    RegisterTweakable(increaseScrollDelayBtn);
    m_keybindingItems.push_back(increaseScrollDelayBtn);
    m_keybindingActions.push_back(Keybindings::Action::IncreaseScrollDelay);
    m_keybindingDefaults.push_back(VK_DELETE);

    // === Multiplayer Monitoring Controls ===
    // Save Multiplayer Logs
    auto saveMultiplayerLogsBtn = CreateKeybindButton(10136, Keybindings::Action::SaveMultiplayerLogs, &waitingForSaveMultiplayerLogs, this);
    RegisterTweakable(saveMultiplayerLogsBtn);
    m_keybindingItems.push_back(saveMultiplayerLogsBtn);
    m_keybindingActions.push_back(Keybindings::Action::SaveMultiplayerLogs);
    m_keybindingDefaults.push_back(0);

    // Capture Session State
    auto captureSessionStateBtn = CreateKeybindButton(10137, Keybindings::Action::CaptureSessionState, &waitingForCaptureSessionState, this);
    RegisterTweakable(captureSessionStateBtn);
    m_keybindingItems.push_back(captureSessionStateBtn);
    m_keybindingActions.push_back(Keybindings::Action::CaptureSessionState);
    m_keybindingDefaults.push_back(0);

    // === Leaderboard Scanner Controls ===

    // Scan Leaderboard By ID
    auto scanLeaderboardByIDBtn = CreateKeybindButton(10127, Keybindings::Action::ScanLeaderboardByID, &waitingForScanLeaderboardByID, this);
    RegisterTweakable(scanLeaderboardByIDBtn);
    m_keybindingItems.push_back(scanLeaderboardByIDBtn);
    m_keybindingActions.push_back(Keybindings::Action::ScanLeaderboardByID);
    m_keybindingDefaults.push_back(0);

    // Scan Current Leaderboard
    auto scanCurrentLeaderboardBtn = CreateKeybindButton(10128, Keybindings::Action::ScanCurrentLeaderboard, &waitingForScanCurrentLeaderboard, this);
    RegisterTweakable(scanCurrentLeaderboardBtn);
    m_keybindingItems.push_back(scanCurrentLeaderboardBtn);
    m_keybindingActions.push_back(Keybindings::Action::ScanCurrentLeaderboard);
    m_keybindingDefaults.push_back(0);

    // Test Fetch Track ID
    auto testFetchTrackIDBtn = CreateKeybindButton(10134, Keybindings::Action::TestFetchTrackID, &waitingForTestFetchTrackID, this);
    RegisterTweakable(testFetchTrackIDBtn);
    m_keybindingItems.push_back(testFetchTrackIDBtn);
    m_keybindingActions.push_back(Keybindings::Action::TestFetchTrackID);
    m_keybindingDefaults.push_back(0);

    // === ActionScript Controls ===
    // Full Countdown Sequence
    auto fullCountdownSequenceBtn = CreateKeybindButton(10141, Keybindings::Action::FullCountdownSequence, &waitingForFullCountdownSequence, this);
    RegisterTweakable(fullCountdownSequenceBtn);
    m_keybindingItems.push_back(fullCountdownSequenceBtn);
    m_keybindingActions.push_back(Keybindings::Action::FullCountdownSequence);
    m_keybindingDefaults.push_back(0);

    // Show Single Countdown
    auto showSingleCountdownBtn = CreateKeybindButton(10142, Keybindings::Action::ShowSingleCountdown, &waitingForShowSingleCountdown, this);
    RegisterTweakable(showSingleCountdownBtn);
    m_keybindingItems.push_back(showSingleCountdownBtn);
    m_keybindingActions.push_back(Keybindings::Action::ShowSingleCountdown);
    m_keybindingDefaults.push_back(0);

    // Toggle Load Screen
    auto ToggleLoadScreenBtn = CreateKeybindButton(10143, Keybindings::Action::ToggleLoadScreen, &waitingForToggleLoadScreen, this);
    RegisterTweakable(ToggleLoadScreenBtn);
    m_keybindingItems.push_back(ToggleLoadScreenBtn);
    m_keybindingActions.push_back(Keybindings::Action::ToggleLoadScreen);
    m_keybindingDefaults.push_back(0);

    m_gamepadBindingDefaults.assign(m_keybindingActions.size(), 0);
    for (size_t i = 0; i < m_keybindingActions.size(); ++i) {
        if (m_keybindingActions[i] == Keybindings::Action::RespawnPrevCheckpoint) {
            m_gamepadBindingDefaults[i] = XINPUT_GAMEPAD_LEFT_SHOULDER;
        } else if (m_keybindingActions[i] == Keybindings::Action::RespawnNextCheckpoint) {
            m_gamepadBindingDefaults[i] = XINPUT_GAMEPAD_RIGHT_SHOULDER;
        } else if (m_keybindingActions[i] == Keybindings::Action::EditorScaleDecrease) {
            m_gamepadBindingDefaults[i] = XINPUT_GAMEPAD_DPAD_LEFT;
        } else if (m_keybindingActions[i] == Keybindings::Action::EditorScaleIncrease) {
            m_gamepadBindingDefaults[i] = XINPUT_GAMEPAD_DPAD_RIGHT;
        }
    }

    // Save as Default button - explicitly saves current keybindings to config file
    auto saveKeybindings = std::make_shared<TweakableButton>(
        10198,
        "Save as Default"
    );
    saveKeybindings->SetOnClickCallback([]() {
        if (Keybindings::SaveToFile()) {
            LOG_INFO("[DevMenu] Keybindings saved to config file");
        } else {
            LOG_ERROR("[DevMenu] Failed to save keybindings to config file");
        }
    });
    RegisterTweakable(saveKeybindings);
    m_keybindingItems.push_back(saveKeybindings);

    // Reset all to defaults button
    auto resetKeybindings = std::make_shared<TweakableButton>(
        10199,
        "Reset All to Defaults"
    );
    resetKeybindings->SetOnClickCallback([this]() {
        // Reset each keybinding to its default using the stored mappings
        for (size_t i = 0; i < m_keybindingActions.size(); i++) {
            Keybindings::Action action = m_keybindingActions[i];
            int defaultKey = m_keybindingDefaults[i];
            int defaultGamepadButton = i < m_gamepadBindingDefaults.size() ? m_gamepadBindingDefaults[i] : 0;

            // Reset the keybinding
            Keybindings::SetKey(action, defaultKey);
            Keybindings::SetGamepadButton(action, defaultGamepadButton);

            // Update the button label
            std::string keyName = Keybindings::GetKeyName(defaultKey);
            m_keybindingItems[i]->SetName("Bind " + Keybindings::GetActionName(action) + ": " + keyName);
        }

        LOG_VERBOSE("[DevMenu] Reset all keybindings to defaults");
    });
    RegisterTweakable(resetKeybindings);
    m_keybindingItems.push_back(resetKeybindings);
}
