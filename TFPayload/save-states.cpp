#include "pch.h"
#include "save-states.h"
#include "base-address.h"
#include "keybindings.h"
#include "logging.h"
#include "prevent-finish.h"
#include "respawn.h"
#include <Windows.h>
#include <cmath>
#include <cstring>
#include <iomanip>

namespace SaveStates {
    // UpdateBikeAndCameraFromTrack / ApplyTrackPlaybackFrameToBikeAndCamera.
    static constexpr uintptr_t UPDATE_BIKE_AND_CAMERA_FROM_TRACK_RVA_UPLAY = 0x218690;
    static constexpr uintptr_t UPDATE_BIKE_AND_CAMERA_FROM_TRACK_RVA_STEAM = 0x217f80;
    static constexpr uintptr_t FIND_SCENE_OBJECT_BY_HASH_RVA_UPLAY = 0x20a180;
    static constexpr uintptr_t FIND_SCENE_OBJECT_BY_HASH_RVA_STEAM = 0x209a30;
    static constexpr uintptr_t SET_SCENE_OBJECT_POSITION_RVA_UPLAY = 0x65cc90;
    static constexpr uintptr_t SET_SCENE_OBJECT_POSITION_RVA_STEAM = 0x65b890;
    static constexpr uintptr_t SET_SCENE_OBJECT_VELOCITY_RVA_UPLAY = 0x65cd60;
    static constexpr uintptr_t SET_SCENE_OBJECT_VELOCITY_RVA_STEAM = 0x65b960;
    static constexpr uintptr_t SET_QUATERNION_ROTATION_RVA_UPLAY = 0x6a3ec0;
    static constexpr uintptr_t SET_QUATERNION_ROTATION_RVA_STEAM = 0x6a1f10;
    static constexpr uintptr_t UPDATE_RIDER_POSITION_AND_ANIMATION_RVA_UPLAY = 0x228120;
    static constexpr uintptr_t UPDATE_RIDER_POSITION_AND_ANIMATION_RVA_STEAM = 0x2279f0;
    static constexpr uintptr_t UPDATE_CAMERA_TRANSFORMS_AND_PHYSICS_RVA_UPLAY = 0x2267a0;
    static constexpr uintptr_t UPDATE_CAMERA_TRANSFORMS_AND_PHYSICS_RVA_STEAM = 0x226070;
    static constexpr uintptr_t GET_QUATERNION_OR_ANGULAR_VELOCITY_RVA_UPLAY = 0x20bdc0;
    static constexpr uintptr_t GET_QUATERNION_OR_ANGULAR_VELOCITY_RVA_STEAM = 0x20b670;
    static constexpr uintptr_t GET_RIGID_BODY_VELOCITY_RVA_UPLAY = 0x659390;
    static constexpr uintptr_t GET_RIGID_BODY_VELOCITY_RVA_STEAM = 0x658000;
    static constexpr uintptr_t GET_RAGDOLL_BONE_POSITION_RVA_UPLAY = 0x659430;
    static constexpr uintptr_t GET_RAGDOLL_BONE_POSITION_RVA_STEAM = 0x6580a0;
    static constexpr uintptr_t COLLECT_SCENE_OBJECTS_BY_TYPE_RVA_UPLAY = 0x6a3000;
    static constexpr uintptr_t COLLECT_SCENE_OBJECTS_BY_TYPE_RVA_STEAM = 0x6a19f0;
    static constexpr uintptr_t GLOBAL_STRUCT_RVA_UPLAY = 0x104b308;
    static constexpr uintptr_t GLOBAL_STRUCT_RVA_STEAM = 0x104d308;
    static constexpr uintptr_t TRIGGER_CAMERA_WARP_RVA_UPLAY = 0x236630;
    static constexpr uintptr_t TRIGGER_CAMERA_WARP_RVA_STEAM = 0x235d60;
    static constexpr uintptr_t EXECUTE_TASK_WITH_LOCKING_RVA_UPLAY = 0x56e50;
    static constexpr uintptr_t EXECUTE_TASK_WITH_LOCKING_RVA_STEAM = 0x14b50;
    static constexpr uintptr_t EXECUTE_ASYNC_TASK_RVA_UPLAY = 0x59830;
    // CSV duplicate match: ExecuteAsyncTask votes select Steam 0x002f4060
    // (RVA 0x1b4060). The alternate 0x00921360 maps better to DecryptTaskData.
    static constexpr uintptr_t EXECUTE_ASYNC_TASK_RVA_STEAM = 0x1b4060;

    static constexpr size_t BIKE_POSE_CACHE_SIZE = 0x34;
    static constexpr uintptr_t BIKE_POSE_CACHE_OFFSET = 0xd0;
    static constexpr size_t SCENE_OBJECT_LIVE_TRANSFORM_SIZE = 0x3c;
    static constexpr size_t SCENE_OBJECT_LIVE_STATE_SIZE = 0x1c;
    static constexpr size_t SCENE_OBJECT_ANGULAR_STATE_SIZE = 0x0c;
    static constexpr size_t SCENE_OBJECT_FALLBACK_STATE_SIZE = 0x18;
    static constexpr size_t PORTAL_DYNAMIC_OBJECT_CAPACITY = 4092;
    static constexpr size_t MAX_SCENE_OBJECT_SNAPSHOTS = 256;
    static constexpr size_t KNOWN_SCENE_OBJECT_HASH_COUNT = 6;
    static constexpr uint32_t SCENE_OBJECT_HASH_TRACK_ROOT = 0x73d39136;
    static constexpr uint32_t SCENE_OBJECT_HASH_PRIMARY_BODY = 0x1329aad5;
    static constexpr uint32_t SCENE_OBJECT_HASH_AUX_BODY = 0x5bd7085d;
    static constexpr uint32_t SCENE_OBJECT_HASH_REAR_DYNAMIC = 0xf84642f0;
    static constexpr uint32_t SCENE_OBJECT_HASH_FRONT_DYNAMIC = 0x8d32199b;
    static constexpr uint32_t SCENE_OBJECT_HASH_RIDER_DYNAMIC = 0x6fe00d62;

    typedef void(__thiscall* UpdateBikeAndCameraFromTrackFunc)(
        void* bike,
        void* checkpointOrTrackFrame,
        char preserveDynamicObjects,
        int trackEvalMode);
    typedef void* (__thiscall* FindSceneObjectByHashFunc)(void* bike, uint32_t* hash);
    typedef void(__thiscall* SetSceneObjectVec3Func)(void* sceneObject, float* vec3);
    typedef void(__thiscall* SetQuaternionRotationFunc)(void* sceneObject, float* quat);
    typedef void(__thiscall* SceneObjectSetVec3Func)(void* sceneObject, const float* vec3);
    typedef void(__thiscall* UpdateRiderPositionAndAnimationFunc)(void* bike, uint32_t* trackFrame, int preserveDynamicObjects);
    typedef void(__thiscall* UpdateCameraTransformsAndPhysicsFunc)(
        void* bike,
        float* targetPosition,
        float* rotationDelta,
        float* targetVector,
        int mode,
        char keepVelocity,
        char rotateVelocity,
        int positionArray,
        int velocityArray);
    typedef float* (__thiscall* GetQuaternionOrAngularVelocityFunc)(void* bike, float* outQuat);
    typedef float* (__thiscall* GetSceneObjectVec3Func)(void* sceneObject, float* outVec3);
    typedef void(__thiscall* CollectSceneObjectsByTypeFunc)(
        void* root,
        int outObjects,
        uint32_t type,
        uint32_t subtype,
        int param4,
        uint32_t param5);
    typedef void(__thiscall* TriggerCameraWarpFunc)(void* cameraState, int frame);
    typedef int(__fastcall* ExecuteTaskWithLockingFunc)(int encryptedDataPtr);
    typedef void(__fastcall* ExecuteAsyncTaskFunc)(void* encryptedDataPtr);

    struct SceneObjectCollection {
        void* objects[PORTAL_DYNAMIC_OBJECT_CAPACITY] = {};
        uint32_t count = 0;
    };

    struct SceneObjectSnapshot {
        bool valid = false;
        uint32_t hash = 0;
        void* objectPtr = nullptr;
        bool hasVectorState = false;
        float state20Live150[3] = {};
        float state2cLive160[3] = {};
        bool hasLiveTransform = false;
        uint8_t liveTransform[SCENE_OBJECT_LIVE_TRANSFORM_SIZE] = {};
        bool hasLiveState = false;
        uint8_t liveState[SCENE_OBJECT_LIVE_STATE_SIZE] = {};
        bool hasAngularState = false;
        uint8_t angularState[SCENE_OBJECT_ANGULAR_STATE_SIZE] = {};
        bool hasFallbackState = false;
        uint8_t fallbackStateBlock[SCENE_OBJECT_FALLBACK_STATE_SIZE] = {};
        bool hasTransformPosition = false;
        float transformPosition[3] = {};
        bool hasTransformQuaternion = false;
        float transformQuaternion[4] = {};
    };

    struct SaveStateSlot {
        bool valid = false;
        int checkpointIndex = -1;
        void* checkpointPtr = nullptr;
        void* bikePtr = nullptr;
        int raceTimeMs = 0;
        int faults = 0;
        int trackFrame = 0;
        int trackFrameAux = 0;
        uint8_t bikeTrackState4f8[0x0c] = {};
        uint8_t bikeTrackState9a4[0x08] = {};
        bool hasBikeTrackState = false;
        uint8_t bikePoseCache[BIKE_POSE_CACHE_SIZE] = {};
        bool hasBikePoseCache = false;
        bool hasPortalTargetPosition = false;
        float portalTargetPosition[3] = {};
        bool hasPortalTargetQuaternion = false;
        float portalTargetQuaternion[4] = {};
        SceneObjectSnapshot sceneObjects[MAX_SCENE_OBJECT_SNAPSHOTS] = {};
        size_t sceneObjectCount = 0;
    };

    static bool g_initialized = false;
    static uintptr_t g_baseAddress = 0;
    static UpdateBikeAndCameraFromTrackFunc g_updateBikeAndCameraFromTrack = nullptr;
    static FindSceneObjectByHashFunc g_findSceneObjectByHash = nullptr;
    static SetSceneObjectVec3Func g_setSceneObjectPosition = nullptr;
    static SetSceneObjectVec3Func g_setSceneObjectVelocity = nullptr;
    static SetQuaternionRotationFunc g_setQuaternionRotation = nullptr;
    static UpdateRiderPositionAndAnimationFunc g_updateRiderPositionAndAnimation = nullptr;
    static UpdateCameraTransformsAndPhysicsFunc g_updateCameraTransformsAndPhysics = nullptr;
    static GetQuaternionOrAngularVelocityFunc g_getQuaternionOrAngularVelocity = nullptr;
    static GetSceneObjectVec3Func g_getRigidBodyVelocity = nullptr;
    static GetSceneObjectVec3Func g_getRagdollBonePosition = nullptr;
    static CollectSceneObjectsByTypeFunc g_collectSceneObjectsByType = nullptr;
    static TriggerCameraWarpFunc g_triggerCameraWarp = nullptr;
    static void** g_globalStructPtr = nullptr;
    static ExecuteTaskWithLockingFunc g_executeTaskWithLocking = nullptr;
    static ExecuteAsyncTaskFunc g_executeAsyncTask = nullptr;
    static SaveStateSlot g_slot;
    static volatile LONG g_pendingCapture = 0;
    static volatile LONG g_pendingRestore = 0;
    static volatile LONG g_pendingDebugDump = 0;
    static bool g_isSteamVersion = false;

    static uintptr_t GetUpdateBikeAndCameraFromTrackRVA() {
        return BaseAddress::IsSteamVersion()
            ? UPDATE_BIKE_AND_CAMERA_FROM_TRACK_RVA_STEAM
            : UPDATE_BIKE_AND_CAMERA_FROM_TRACK_RVA_UPLAY;
    }

    static uintptr_t GetFindSceneObjectByHashRVA() {
        return BaseAddress::IsSteamVersion()
            ? FIND_SCENE_OBJECT_BY_HASH_RVA_STEAM
            : FIND_SCENE_OBJECT_BY_HASH_RVA_UPLAY;
    }

    static uintptr_t GetSetSceneObjectPositionRVA() {
        return BaseAddress::IsSteamVersion()
            ? SET_SCENE_OBJECT_POSITION_RVA_STEAM
            : SET_SCENE_OBJECT_POSITION_RVA_UPLAY;
    }

    static uintptr_t GetSetSceneObjectVelocityRVA() {
        return BaseAddress::IsSteamVersion()
            ? SET_SCENE_OBJECT_VELOCITY_RVA_STEAM
            : SET_SCENE_OBJECT_VELOCITY_RVA_UPLAY;
    }

    static uintptr_t GetSetQuaternionRotationRVA() {
        return BaseAddress::IsSteamVersion()
            ? SET_QUATERNION_ROTATION_RVA_STEAM
            : SET_QUATERNION_ROTATION_RVA_UPLAY;
    }

    static uintptr_t GetUpdateRiderPositionAndAnimationRVA() {
        return BaseAddress::IsSteamVersion()
            ? UPDATE_RIDER_POSITION_AND_ANIMATION_RVA_STEAM
            : UPDATE_RIDER_POSITION_AND_ANIMATION_RVA_UPLAY;
    }

    static uintptr_t GetUpdateCameraTransformsAndPhysicsRVA() {
        return BaseAddress::IsSteamVersion()
            ? UPDATE_CAMERA_TRANSFORMS_AND_PHYSICS_RVA_STEAM
            : UPDATE_CAMERA_TRANSFORMS_AND_PHYSICS_RVA_UPLAY;
    }

    static uintptr_t GetGetQuaternionOrAngularVelocityRVA() {
        return BaseAddress::IsSteamVersion()
            ? GET_QUATERNION_OR_ANGULAR_VELOCITY_RVA_STEAM
            : GET_QUATERNION_OR_ANGULAR_VELOCITY_RVA_UPLAY;
    }

    static uintptr_t GetGetRigidBodyVelocityRVA() {
        return BaseAddress::IsSteamVersion()
            ? GET_RIGID_BODY_VELOCITY_RVA_STEAM
            : GET_RIGID_BODY_VELOCITY_RVA_UPLAY;
    }

    static uintptr_t GetGetRagdollBonePositionRVA() {
        return BaseAddress::IsSteamVersion()
            ? GET_RAGDOLL_BONE_POSITION_RVA_STEAM
            : GET_RAGDOLL_BONE_POSITION_RVA_UPLAY;
    }

    static uintptr_t GetCollectSceneObjectsByTypeRVA() {
        return BaseAddress::IsSteamVersion()
            ? COLLECT_SCENE_OBJECTS_BY_TYPE_RVA_STEAM
            : COLLECT_SCENE_OBJECTS_BY_TYPE_RVA_UPLAY;
    }

    static uintptr_t GetGlobalStructRVA() {
        return BaseAddress::IsSteamVersion()
            ? GLOBAL_STRUCT_RVA_STEAM
            : GLOBAL_STRUCT_RVA_UPLAY;
    }

    static uintptr_t GetTriggerCameraWarpRVA() {
        return BaseAddress::IsSteamVersion()
            ? TRIGGER_CAMERA_WARP_RVA_STEAM
            : TRIGGER_CAMERA_WARP_RVA_UPLAY;
    }

    static uintptr_t GetExecuteTaskWithLockingRVA() {
        return BaseAddress::IsSteamVersion()
            ? EXECUTE_TASK_WITH_LOCKING_RVA_STEAM
            : EXECUTE_TASK_WITH_LOCKING_RVA_UPLAY;
    }

    static uintptr_t GetExecuteAsyncTaskRVA() {
        return BaseAddress::IsSteamVersion()
            ? EXECUTE_ASYNC_TASK_RVA_STEAM
            : EXECUTE_ASYNC_TASK_RVA_UPLAY;
    }

    static bool IsReadable(const void* ptr, size_t size) {
        return ptr != nullptr && !IsBadReadPtr(ptr, size);
    }

    static bool IsWritable(void* ptr, size_t size) {
        return ptr != nullptr && !IsBadWritePtr(ptr, size);
    }

    static void* GetGameManager() {
        if (!g_globalStructPtr || !IsReadable(g_globalStructPtr, sizeof(void*))) {
            return nullptr;
        }

        void* globalStruct = *g_globalStructPtr;
        if (!IsReadable(globalStruct, 0x100)) {
            return nullptr;
        }

        uintptr_t managerAddr = reinterpret_cast<uintptr_t>(globalStruct) + 0xdc;
        if (!IsReadable(reinterpret_cast<void*>(managerAddr), sizeof(void*))) {
            return nullptr;
        }

        void* manager = *reinterpret_cast<void**>(managerAddr);
        if (!IsReadable(manager, 0x1000)) {
            return nullptr;
        }

        return manager;
    }

    static bool CopyBikePoseCache(void* bike, uint8_t outCache[BIKE_POSE_CACHE_SIZE]) {
        uint8_t* cachePtr = reinterpret_cast<uint8_t*>(bike) + BIKE_POSE_CACHE_OFFSET;
        if (!IsReadable(cachePtr, BIKE_POSE_CACHE_SIZE)) {
            return false;
        }

        std::memcpy(outCache, cachePtr, BIKE_POSE_CACHE_SIZE);
        return true;
    }

    static bool RestoreBikePoseCache(void* bike, const uint8_t cache[BIKE_POSE_CACHE_SIZE]) {
        uint8_t* cachePtr = reinterpret_cast<uint8_t*>(bike) + BIKE_POSE_CACHE_OFFSET;
        if (!IsWritable(cachePtr, BIKE_POSE_CACHE_SIZE)) {
            return false;
        }

        std::memcpy(cachePtr, cache, BIKE_POSE_CACHE_SIZE);
        return true;
    }

    static bool CopyBikeTrackState(void* bike, SaveStateSlot& slot) {
        uint8_t* base = reinterpret_cast<uint8_t*>(bike);
        if (!IsReadable(base + 0x4f8, sizeof(slot.bikeTrackState4f8))
            || !IsReadable(base + 0x9a4, sizeof(slot.bikeTrackState9a4))) {
            return false;
        }

        std::memcpy(slot.bikeTrackState4f8, base + 0x4f8, sizeof(slot.bikeTrackState4f8));
        std::memcpy(slot.bikeTrackState9a4, base + 0x9a4, sizeof(slot.bikeTrackState9a4));
        return true;
    }

    static bool RestoreBikeTrackState(void* bike, const SaveStateSlot& slot) {
        uint8_t* base = reinterpret_cast<uint8_t*>(bike);
        if (!slot.hasBikeTrackState
            || !IsWritable(base + 0x4f8, sizeof(slot.bikeTrackState4f8))
            || !IsWritable(base + 0x9a4, sizeof(slot.bikeTrackState9a4))) {
            return false;
        }

        std::memcpy(base + 0x4f8, slot.bikeTrackState4f8, sizeof(slot.bikeTrackState4f8));
        std::memcpy(base + 0x9a4, slot.bikeTrackState9a4, sizeof(slot.bikeTrackState9a4));
        return true;
    }

    static void CopyVec3(float dst[3], const float src[3]) {
        dst[0] = src[0];
        dst[1] = src[1];
        dst[2] = src[2];
    }

    static void CopyVec4(float dst[4], const float src[4]) {
        dst[0] = src[0];
        dst[1] = src[1];
        dst[2] = src[2];
        dst[3] = src[3];
    }

    static void NormalizeQuat(float quat[4]) {
        float lengthSq =
            quat[0] * quat[0] +
            quat[1] * quat[1] +
            quat[2] * quat[2] +
            quat[3] * quat[3];
        if (lengthSq <= 0.000001f) {
            quat[0] = 0.0f;
            quat[1] = 0.0f;
            quat[2] = 0.0f;
            quat[3] = 1.0f;
            return;
        }

        float invLength = 1.0f / sqrtf(lengthSq);
        quat[0] *= invLength;
        quat[1] *= invLength;
        quat[2] *= invLength;
        quat[3] *= invLength;
    }

    static void InvertQuat(const float quat[4], float out[4]) {
        out[0] = -quat[0];
        out[1] = -quat[1];
        out[2] = -quat[2];
        out[3] = quat[3];
    }

    static void MultiplyQuat(const float a[4], const float b[4], float out[4]) {
        out[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
        out[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
        out[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
        out[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
        NormalizeQuat(out);
    }

    static bool Matrix3x3ToQuat(const uint8_t matrixBlock[SCENE_OBJECT_LIVE_TRANSFORM_SIZE], float outQuat[4]) {
        if (!matrixBlock || !outQuat) {
            return false;
        }

        const float* m = reinterpret_cast<const float*>(matrixBlock);
        float m00 = m[0];
        float m01 = m[1];
        float m02 = m[2];
        float m10 = m[4];
        float m11 = m[5];
        float m12 = m[6];
        float m20 = m[8];
        float m21 = m[9];
        float m22 = m[10];
        float trace = m00 + m11 + m22;

        if (trace > 0.0f) {
            float s = sqrtf(trace + 1.0f) * 2.0f;
            outQuat[3] = 0.25f * s;
            outQuat[0] = (m21 - m12) / s;
            outQuat[1] = (m02 - m20) / s;
            outQuat[2] = (m10 - m01) / s;
        }
        else if (m00 > m11 && m00 > m22) {
            float s = sqrtf(1.0f + m00 - m11 - m22) * 2.0f;
            outQuat[3] = (m21 - m12) / s;
            outQuat[0] = 0.25f * s;
            outQuat[1] = (m01 + m10) / s;
            outQuat[2] = (m02 + m20) / s;
        }
        else if (m11 > m22) {
            float s = sqrtf(1.0f + m11 - m00 - m22) * 2.0f;
            outQuat[3] = (m02 - m20) / s;
            outQuat[0] = (m01 + m10) / s;
            outQuat[1] = 0.25f * s;
            outQuat[2] = (m12 + m21) / s;
        }
        else {
            float s = sqrtf(1.0f + m22 - m00 - m11) * 2.0f;
            outQuat[3] = (m10 - m01) / s;
            outQuat[0] = (m02 + m20) / s;
            outQuat[1] = (m12 + m21) / s;
            outQuat[2] = 0.25f * s;
        }

        NormalizeQuat(outQuat);
        return true;
    }

    static bool ReadVec3(uintptr_t address, float out[3]) {
        if (!out || !IsReadable(reinterpret_cast<void*>(address), sizeof(float) * 3)) {
            return false;
        }

        std::memcpy(out, reinterpret_cast<void*>(address), sizeof(float) * 3);
        return true;
    }

    static bool WriteVec3(uintptr_t address, const float value[3]) {
        if (!value || !IsWritable(reinterpret_cast<void*>(address), sizeof(float) * 3)) {
            return false;
        }

        std::memcpy(reinterpret_cast<void*>(address), value, sizeof(float) * 3);
        return true;
    }

    static bool ReadVec4(uintptr_t address, float out[4]) {
        if (!out || !IsReadable(reinterpret_cast<void*>(address), sizeof(float) * 4)) {
            return false;
        }

        std::memcpy(out, reinterpret_cast<void*>(address), sizeof(float) * 4);
        return true;
    }

    static bool WriteVec4(uintptr_t address, const float value[4]) {
        if (!value || !IsWritable(reinterpret_cast<void*>(address), sizeof(float) * 4)) {
            return false;
        }

        std::memcpy(reinterpret_cast<void*>(address), value, sizeof(float) * 4);
        return true;
    }

    static bool ReadBlock(uintptr_t address, void* out, size_t size) {
        if (!out || !IsReadable(reinterpret_cast<void*>(address), size)) {
            return false;
        }

        std::memcpy(out, reinterpret_cast<void*>(address), size);
        return true;
    }

    static bool WriteBlock(uintptr_t address, const void* value, size_t size) {
        if (!value || !IsWritable(reinterpret_cast<void*>(address), size)) {
            return false;
        }

        std::memcpy(reinterpret_cast<void*>(address), value, size);
        return true;
    }

    static bool CallSetSceneObjectVec3Internal(SetSceneObjectVec3Func func, void* sceneObject, float* value) {
        __try {
            func(sceneObject, value);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    static bool CallSetQuaternionRotationInternal(void* sceneObject, float* value) {
        __try {
            g_setQuaternionRotation(sceneObject, value);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    static int CallExecuteTaskWithLockingInternal(uintptr_t encryptedDataPtr, bool* success) {
        __try {
            int result = g_executeTaskWithLocking(static_cast<int>(encryptedDataPtr));
            *success = true;
            return result;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            *success = false;
            return 0;
        }
    }

    static void CallExecuteAsyncTaskWithValueAsm(void* structPtr, int value) {
        __asm {
            push value
            mov ecx, structPtr
            call g_executeAsyncTask
        }
    }

    static bool CallExecuteAsyncTaskWithValue(void* structPtr, int value) {
        __try {
            CallExecuteAsyncTaskWithValueAsm(structPtr, value);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    static bool CallUpdateRiderPositionAndAnimationInternal(void* bike, uint32_t* trackFrame, int preserveDynamicObjects) {
        __try {
            g_updateRiderPositionAndAnimation(bike, trackFrame, preserveDynamicObjects);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    static bool CallUpdateCameraTransformsAndPhysicsInternal(
        void* bike,
        float* targetPosition,
        float* rotationDelta,
        float* targetVector,
        int mode,
        char keepVelocity,
        char rotateVelocity,
        int positionArray,
        int velocityArray) {
        __try {
            g_updateCameraTransformsAndPhysics(
                bike,
                targetPosition,
                rotationDelta,
                targetVector,
                mode,
                keepVelocity,
                rotateVelocity,
                positionArray,
                velocityArray);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    static bool CallGetQuaternionOrAngularVelocityInternal(void* bike, float outQuat[4]) {
        __try {
            float* result = g_getQuaternionOrAngularVelocity(bike, outQuat);
            if (result != outQuat) {
                CopyVec4(outQuat, result);
            }
            NormalizeQuat(outQuat);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    static bool CallGetSceneObjectVec3Internal(GetSceneObjectVec3Func getter, void* sceneObject, float outVec3[3]) {
        if (!getter || !sceneObject || !outVec3) {
            return false;
        }

        __try {
            float* result = getter(sceneObject, outVec3);
            if (result != outVec3) {
                CopyVec3(outVec3, result);
            }
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    static bool CallCollectSceneObjectsByTypeInternal(void* root, SceneObjectCollection& collection) {
        collection = SceneObjectCollection{};
        __try {
            g_collectSceneObjectsByType(root, reinterpret_cast<int>(&collection), 3, 1, 0, 0);
            if (collection.count > PORTAL_DYNAMIC_OBJECT_CAPACITY) {
                collection.count = static_cast<uint32_t>(PORTAL_DYNAMIC_OBJECT_CAPACITY);
            }
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            collection = SceneObjectCollection{};
            return false;
        }
    }

    static bool CallTriggerCameraWarpInternal(void* cameraState, int frame) {
        __try {
            g_triggerCameraWarp(cameraState, frame);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    static bool ApplyPortalCameraWarpSideEffect() {
        if (!g_triggerCameraWarp || !g_executeTaskWithLocking) {
            return false;
        }

        void* manager = GetGameManager();
        if (!manager) {
            return false;
        }

        uint8_t* managerBytes = reinterpret_cast<uint8_t*>(manager);
        if (!IsReadable(managerBytes + 0xe8, sizeof(void*))
            || !IsReadable(managerBytes + 0xfc, sizeof(void*))) {
            return false;
        }

        void* cameraState = *reinterpret_cast<void**>(managerBytes + 0xe8);
        void* cameraFlags = *reinterpret_cast<void**>(managerBytes + 0xfc);
        if (!IsReadable(cameraState, 0xa0)
            || !IsWritable(reinterpret_cast<uint8_t*>(cameraFlags) + 0x6c, sizeof(uint8_t))) {
            return false;
        }

        bool success = false;
        int frame = CallExecuteTaskWithLockingInternal(reinterpret_cast<uintptr_t>(managerBytes + 0x14), &success);
        if (!success) {
            return false;
        }

        if (!CallTriggerCameraWarpInternal(cameraState, frame + 1)) {
            return false;
        }

        *reinterpret_cast<uint8_t*>(reinterpret_cast<uint8_t*>(cameraFlags) + 0x6c) = 1;
        return true;
    }

    static bool RestoreBikeFrameTasks(void* bike, int trackFrame, int trackFrameAux) {
        uint8_t* base = reinterpret_cast<uint8_t*>(bike);
        bool ok = true;
        ok = CallExecuteAsyncTaskWithValue(base + 0x140, trackFrame) && ok;
        ok = CallExecuteAsyncTaskWithValue(base + 0x9bc, trackFrame) && ok;
        ok = CallExecuteAsyncTaskWithValue(base + 0x150, trackFrameAux) && ok;
        ok = CallExecuteAsyncTaskWithValue(base + 0x9ac, trackFrameAux) && ok;
        return ok;
    }

    static bool CaptureBikeFrameTasks(void* bike, SaveStateSlot& slot) {
        if (!g_executeTaskWithLocking) {
            return false;
        }

        bool success = false;
        uint8_t* base = reinterpret_cast<uint8_t*>(bike);
        slot.trackFrame = CallExecuteTaskWithLockingInternal(reinterpret_cast<uintptr_t>(base + 0x140), &success);
        if (!success) {
            return false;
        }

        slot.trackFrameAux = CallExecuteTaskWithLockingInternal(reinterpret_cast<uintptr_t>(base + 0x150), &success);
        if (!success) {
            slot.trackFrameAux = slot.trackFrame;
        }
        return true;
    }

    static bool CallSceneObjectVtableVec3(void* sceneObject, uintptr_t methodOffset, const float value[3]) {
        if (!sceneObject || !value || !IsReadable(sceneObject, sizeof(uintptr_t))) {
            return false;
        }

        uintptr_t vtable = *reinterpret_cast<uintptr_t*>(sceneObject);
        if (!IsReadable(reinterpret_cast<void*>(vtable + methodOffset), sizeof(uintptr_t))) {
            return false;
        }

        uintptr_t method = *reinterpret_cast<uintptr_t*>(vtable + methodOffset);
        if (!method) {
            return false;
        }

        __try {
            SceneObjectSetVec3Func func = reinterpret_cast<SceneObjectSetVec3Func>(method);
            func(sceneObject, value);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    static void* CallFindSceneObjectByHashInternal(void* bike, uint32_t* hash) {
        __try {
            return g_findSceneObjectByHash(bike, hash);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return nullptr;
        }
    }

    static void* FindSceneObjectByHashSafe(void* bike, uint32_t hash) {
        if (!g_findSceneObjectByHash || !bike) {
            return nullptr;
        }

        uint32_t hashCopy = hash;
        return CallFindSceneObjectByHashInternal(bike, &hashCopy);
    }

    static bool GetSceneObjectHash(void* sceneObject, uint32_t& outHash) {
        if (!sceneObject || !IsReadable(reinterpret_cast<uint8_t*>(sceneObject) + 0x28, sizeof(uintptr_t))) {
            return false;
        }

        uintptr_t transform = *reinterpret_cast<uintptr_t*>(reinterpret_cast<uint8_t*>(sceneObject) + 0x28);
        if (!IsReadable(reinterpret_cast<void*>(transform + 0x08), sizeof(uint32_t))) {
            return false;
        }

        outHash = *reinterpret_cast<uint32_t*>(transform + 0x08);
        return true;
    }

    static const SceneObjectSnapshot* FindSnapshotByHash(const SaveStateSlot& slot, uint32_t hash) {
        for (size_t i = 0; i < slot.sceneObjectCount; ++i) {
            if (slot.sceneObjects[i].valid && slot.sceneObjects[i].hash == hash) {
                return &slot.sceneObjects[i];
            }
        }

        return nullptr;
    }

    static bool CaptureSceneObjectSnapshotFromObject(void* sceneObject, uint32_t hash, SceneObjectSnapshot& snapshot) {
        snapshot = SceneObjectSnapshot{};
        snapshot.hash = hash;

        if (!sceneObject || !IsReadable(sceneObject, 0xb4)) {
            return false;
        }

        snapshot.objectPtr = sceneObject;

        uintptr_t fallbackState = 0;
        if (IsReadable(reinterpret_cast<uint8_t*>(sceneObject) + 0xa0, sizeof(uintptr_t))) {
            fallbackState = *reinterpret_cast<uintptr_t*>(reinterpret_cast<uint8_t*>(sceneObject) + 0xa0);
        }

        uintptr_t liveBody = 0;
        if (IsReadable(reinterpret_cast<uint8_t*>(sceneObject) + 0xa8, sizeof(uintptr_t))) {
            liveBody = *reinterpret_cast<uintptr_t*>(reinterpret_cast<uint8_t*>(sceneObject) + 0xa8);
        }

        bool liveAvailable = false;
        if (liveBody != 0
            && IsReadable(reinterpret_cast<uint8_t*>(sceneObject) + 0xb0, sizeof(uint8_t))
            && *reinterpret_cast<uint8_t*>(reinterpret_cast<uint8_t*>(sceneObject) + 0xb0) == 0
            && IsReadable(reinterpret_cast<void*>(liveBody + 0x16c), 1)) {
            liveAvailable = true;
        }

        bool capturedState =
            CallGetSceneObjectVec3Internal(g_getRigidBodyVelocity, sceneObject, snapshot.state20Live150) &&
            CallGetSceneObjectVec3Internal(g_getRagdollBonePosition, sceneObject, snapshot.state2cLive160);
        if (liveAvailable) {
            capturedState =
                capturedState ||
                ReadVec3(liveBody + 0x150, snapshot.state20Live150) &&
                ReadVec3(liveBody + 0x160, snapshot.state2cLive160);
            snapshot.hasLiveTransform = ReadBlock(
                liveBody + 0x10,
                snapshot.liveTransform,
                sizeof(snapshot.liveTransform));
            snapshot.hasLiveState = ReadBlock(
                liveBody + 0x150,
                snapshot.liveState,
                sizeof(snapshot.liveState));
            snapshot.hasAngularState = ReadBlock(
                liveBody + 0x1d0,
                snapshot.angularState,
                sizeof(snapshot.angularState));
        }

        if (!capturedState && fallbackState != 0) {
            capturedState =
                ReadVec3(fallbackState + 0x20, snapshot.state20Live150) &&
                ReadVec3(fallbackState + 0x2c, snapshot.state2cLive160);
        }
        snapshot.hasVectorState = capturedState;
        if (fallbackState != 0) {
            snapshot.hasFallbackState = ReadBlock(
                fallbackState + 0x20,
                snapshot.fallbackStateBlock,
                sizeof(snapshot.fallbackStateBlock));
        }

        uintptr_t transform = 0;
        if (IsReadable(reinterpret_cast<uint8_t*>(sceneObject) + 0x28, sizeof(uintptr_t))) {
            transform = *reinterpret_cast<uintptr_t*>(reinterpret_cast<uint8_t*>(sceneObject) + 0x28);
        }

        if (transform != 0) {
            snapshot.hasTransformPosition = ReadVec3(transform + 0x14, snapshot.transformPosition);
            snapshot.hasTransformQuaternion = ReadVec4(transform + 0x20, snapshot.transformQuaternion);
        }

        snapshot.valid = capturedState
            || snapshot.hasLiveTransform
            || snapshot.hasLiveState
            || snapshot.hasAngularState
            || snapshot.hasFallbackState
            || snapshot.hasTransformPosition
            || snapshot.hasTransformQuaternion;
        return snapshot.valid;
    }

    static bool CaptureSceneObjectSnapshot(void* bike, uint32_t hash, SceneObjectSnapshot& snapshot) {
        void* sceneObject = FindSceneObjectByHashSafe(bike, hash);
        return CaptureSceneObjectSnapshotFromObject(sceneObject, hash, snapshot);
    }

    static bool HasSnapshotHash(const SceneObjectSnapshot snapshots[MAX_SCENE_OBJECT_SNAPSHOTS], size_t count, uint32_t hash) {
        for (size_t i = 0; i < count; ++i) {
            if (snapshots[i].valid && snapshots[i].hash == hash) {
                return true;
            }
        }

        return false;
    }

    static bool GetPortalDynamicRoot(void* bike, void*& outRoot);

    static size_t CaptureSceneObjectSnapshots(void* bike, SceneObjectSnapshot outSnapshots[MAX_SCENE_OBJECT_SNAPSHOTS]) {
        static const uint32_t hashes[KNOWN_SCENE_OBJECT_HASH_COUNT] = {
            SCENE_OBJECT_HASH_TRACK_ROOT,
            SCENE_OBJECT_HASH_PRIMARY_BODY,
            SCENE_OBJECT_HASH_AUX_BODY,
            SCENE_OBJECT_HASH_REAR_DYNAMIC,
            SCENE_OBJECT_HASH_FRONT_DYNAMIC,
            SCENE_OBJECT_HASH_RIDER_DYNAMIC,
        };

        size_t count = 0;
        if (g_collectSceneObjectsByType && bike) {
            void* root = nullptr;
            if (GetPortalDynamicRoot(bike, root)) {
                SceneObjectCollection collection;
                if (CallCollectSceneObjectsByTypeInternal(root, collection)) {
                    uint32_t limit = collection.count;
                    if (limit > MAX_SCENE_OBJECT_SNAPSHOTS) {
                        limit = static_cast<uint32_t>(MAX_SCENE_OBJECT_SNAPSHOTS);
                    }

                    for (uint32_t i = 0; i < limit; ++i) {
                        uint32_t hash = 0;
                        if (!GetSceneObjectHash(collection.objects[i], hash) || HasSnapshotHash(outSnapshots, count, hash)) {
                            continue;
                        }

                        SceneObjectSnapshot snapshot;
                        if (CaptureSceneObjectSnapshotFromObject(collection.objects[i], hash, snapshot)) {
                            outSnapshots[count++] = snapshot;
                        }
                    }
                }
            }
        }

        for (size_t i = 0; i < KNOWN_SCENE_OBJECT_HASH_COUNT && count < MAX_SCENE_OBJECT_SNAPSHOTS; ++i) {
            if (HasSnapshotHash(outSnapshots, count, hashes[i])) {
                continue;
            }

            SceneObjectSnapshot snapshot;
            if (CaptureSceneObjectSnapshot(bike, hashes[i], snapshot)) {
                outSnapshots[count++] = snapshot;
            }
        }
        return count;
    }

    static bool CopyPortalTargetPosition(const SaveStateSlot& slot, float outPosition[3]) {
        for (size_t i = 0; i < slot.sceneObjectCount; ++i) {
            const SceneObjectSnapshot& snapshot = slot.sceneObjects[i];
            if (snapshot.hash != SCENE_OBJECT_HASH_PRIMARY_BODY) {
                continue;
            }

            if (snapshot.hasLiveTransform) {
                std::memcpy(outPosition, snapshot.liveTransform + 0x30, sizeof(float) * 3);
                return true;
            }

            if (snapshot.hasTransformPosition) {
                CopyVec3(outPosition, snapshot.transformPosition);
                return true;
            }
        }

        for (size_t i = 0; i < slot.sceneObjectCount; ++i) {
            const SceneObjectSnapshot& snapshot = slot.sceneObjects[i];
            if (snapshot.hash != SCENE_OBJECT_HASH_TRACK_ROOT) {
                continue;
            }

            if (snapshot.hasLiveTransform) {
                std::memcpy(outPosition, snapshot.liveTransform + 0x30, sizeof(float) * 3);
                return true;
            }

            if (snapshot.hasTransformPosition) {
                CopyVec3(outPosition, snapshot.transformPosition);
                return true;
            }
        }

        float sum[3] = {};
        size_t count = 0;

        for (size_t i = 0; i < slot.sceneObjectCount; ++i) {
            const SceneObjectSnapshot& snapshot = slot.sceneObjects[i];
            if (!snapshot.hasLiveTransform) {
                continue;
            }

            const float* position = reinterpret_cast<const float*>(snapshot.liveTransform + 0x30);
            sum[0] += position[0];
            sum[1] += position[1];
            sum[2] += position[2];
            ++count;
        }

        if (count != 0) {
            outPosition[0] = sum[0] / static_cast<float>(count);
            outPosition[1] = sum[1] / static_cast<float>(count);
            outPosition[2] = sum[2] / static_cast<float>(count);
            return true;
        }

        return false;
    }

    static bool CopyPrimaryBodyLiveQuaternion(const SaveStateSlot& slot, float outQuat[4]) {
        for (size_t i = 0; i < slot.sceneObjectCount; ++i) {
            const SceneObjectSnapshot& snapshot = slot.sceneObjects[i];
            if (snapshot.hash != SCENE_OBJECT_HASH_PRIMARY_BODY || !snapshot.hasLiveTransform) {
                continue;
            }

            return Matrix3x3ToQuat(snapshot.liveTransform, outQuat);
        }

        return false;
    }

    static bool GetPortalDynamicRoot(void* bike, void*& outRoot) {
        if (!bike || !IsReadable(reinterpret_cast<uint8_t*>(bike) + 0x678, sizeof(uintptr_t))) {
            return false;
        }

        outRoot = *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(bike) + 0x678);
        return outRoot != nullptr && IsReadable(outRoot, sizeof(uintptr_t));
    }

    static size_t BuildPortalVelocityArrays(
        void* bike,
        const SaveStateSlot& slot,
        float positionState[PORTAL_DYNAMIC_OBJECT_CAPACITY][3],
        float velocityState[PORTAL_DYNAMIC_OBJECT_CAPACITY][3],
        uint32_t* outCollectedCount) {
        if (outCollectedCount) {
            *outCollectedCount = 0;
        }

        if (!g_collectSceneObjectsByType || !bike || !positionState || !velocityState) {
            return 0;
        }

        void* root = nullptr;
        if (!GetPortalDynamicRoot(bike, root)) {
            return 0;
        }

        SceneObjectCollection collection;
        if (!CallCollectSceneObjectsByTypeInternal(root, collection) || collection.count == 0) {
            return 0;
        }
        if (outCollectedCount) {
            *outCollectedCount = collection.count;
        }

        size_t matched = 0;
        for (uint32_t i = 0; i < collection.count; ++i) {
            positionState[i][0] = 0.0f;
            positionState[i][1] = 0.0f;
            positionState[i][2] = 0.0f;
            velocityState[i][0] = 0.0f;
            velocityState[i][1] = 0.0f;
            velocityState[i][2] = 0.0f;

            uint32_t hash = 0;
            if (!GetSceneObjectHash(collection.objects[i], hash)) {
                continue;
            }

            const SceneObjectSnapshot* snapshot = FindSnapshotByHash(slot, hash);
            if (!snapshot || !snapshot->hasVectorState) {
                continue;
            }

            // Match the engine's preserve-dynamic-object pairing:
            // state2c/live160 is passed to SetSceneObjectPosition,
            // state20/live150 is passed to SetSceneObjectVelocity.
            CopyVec3(positionState[i], snapshot->state2cLive160);
            CopyVec3(velocityState[i], snapshot->state20Live150);
            ++matched;
        }

        return matched;
    }

    static bool RestoreSceneObjectSnapshot(void* bike, const SceneObjectSnapshot& snapshot) {
        if (!snapshot.valid) {
            return false;
        }

        void* sceneObject = FindSceneObjectByHashSafe(bike, snapshot.hash);
        if (!sceneObject || !IsReadable(sceneObject, 0xb4)) {
            return false;
        }

        bool wroteAny = false;

        float state20Live150[3] = {};
        float state2cLive160[3] = {};
        CopyVec3(state20Live150, snapshot.state20Live150);
        CopyVec3(state2cLive160, snapshot.state2cLive160);

        if (snapshot.hasTransformPosition) {
            wroteAny = CallSceneObjectVtableVec3(sceneObject, 0x0c, snapshot.transformPosition) || wroteAny;
        }

        // Match UpdateBikeAndCameraFromTrack's preserve-dynamic-object pairing:
        // captured state2c/live160 is passed to SetSceneObjectPosition, and
        // captured state20/live150 is passed to SetSceneObjectVelocity.
        if (snapshot.hasVectorState && g_setSceneObjectPosition) {
            wroteAny = CallSetSceneObjectVec3Internal(g_setSceneObjectPosition, sceneObject, state2cLive160) || wroteAny;
        }

        if (snapshot.hasVectorState && g_setSceneObjectVelocity) {
            wroteAny = CallSetSceneObjectVec3Internal(g_setSceneObjectVelocity, sceneObject, state20Live150) || wroteAny;
        }

        if (g_setQuaternionRotation && snapshot.hasTransformQuaternion) {
            float quat[4] = {};
            CopyVec4(quat, snapshot.transformQuaternion);
            wroteAny = CallSetQuaternionRotationInternal(sceneObject, quat) || wroteAny;
        }

        uintptr_t fallbackState = 0;
        if (IsReadable(reinterpret_cast<uint8_t*>(sceneObject) + 0xa0, sizeof(uintptr_t))) {
            fallbackState = *reinterpret_cast<uintptr_t*>(reinterpret_cast<uint8_t*>(sceneObject) + 0xa0);
        }
        if (fallbackState != 0) {
            if (snapshot.hasFallbackState) {
                wroteAny = WriteBlock(
                    fallbackState + 0x20,
                    snapshot.fallbackStateBlock,
                    sizeof(snapshot.fallbackStateBlock)) || wroteAny;
            }
            if (snapshot.hasVectorState) {
                wroteAny = WriteVec3(fallbackState + 0x20, snapshot.state20Live150) || wroteAny;
                wroteAny = WriteVec3(fallbackState + 0x2c, snapshot.state2cLive160) || wroteAny;
            }
        }

        uintptr_t liveBody = 0;
        if (IsReadable(reinterpret_cast<uint8_t*>(sceneObject) + 0xa8, sizeof(uintptr_t))) {
            liveBody = *reinterpret_cast<uintptr_t*>(reinterpret_cast<uint8_t*>(sceneObject) + 0xa8);
        }
        if (liveBody != 0
            && IsReadable(reinterpret_cast<uint8_t*>(sceneObject) + 0xb0, sizeof(uint8_t))
            && *reinterpret_cast<uint8_t*>(reinterpret_cast<uint8_t*>(sceneObject) + 0xb0) == 0) {
            if (snapshot.hasLiveTransform) {
                wroteAny = WriteBlock(
                    liveBody + 0x10,
                    snapshot.liveTransform,
                    sizeof(snapshot.liveTransform)) || wroteAny;
            }
            if (snapshot.hasLiveState) {
                wroteAny = WriteBlock(
                    liveBody + 0x150,
                    snapshot.liveState,
                    sizeof(snapshot.liveState)) || wroteAny;
            }
            if (snapshot.hasAngularState) {
                wroteAny = WriteBlock(
                    liveBody + 0x1d0,
                    snapshot.angularState,
                    sizeof(snapshot.angularState)) || wroteAny;
            }
            if (snapshot.hasVectorState) {
                wroteAny = WriteVec3(liveBody + 0x150, snapshot.state20Live150) || wroteAny;
                wroteAny = WriteVec3(liveBody + 0x160, snapshot.state2cLive160) || wroteAny;
            }
        }

        uintptr_t transform = 0;
        if (IsReadable(reinterpret_cast<uint8_t*>(sceneObject) + 0x28, sizeof(uintptr_t))) {
            transform = *reinterpret_cast<uintptr_t*>(reinterpret_cast<uint8_t*>(sceneObject) + 0x28);
        }
        if (transform != 0) {
            if (snapshot.hasTransformPosition) {
                wroteAny = WriteVec3(transform + 0x14, snapshot.transformPosition) || wroteAny;
            }
            if (snapshot.hasTransformQuaternion) {
                wroteAny = WriteVec4(transform + 0x20, snapshot.transformQuaternion) || wroteAny;
            }
        }

        return wroteAny;
    }

    static size_t RestoreSceneObjectSnapshots(void* bike, const SaveStateSlot& slot) {
        size_t restored = 0;
        for (size_t i = 0; i < slot.sceneObjectCount; ++i) {
            if (RestoreSceneObjectSnapshot(bike, slot.sceneObjects[i])) {
                ++restored;
            }
        }
        return restored;
    }

    static bool ApplyCapturedSceneObjectVelocity(void* bike, const SceneObjectSnapshot& snapshot) {
        if (!snapshot.valid || !snapshot.hasVectorState) {
            return false;
        }

        void* sceneObject = FindSceneObjectByHashSafe(bike, snapshot.hash);
        if (!sceneObject || !IsReadable(sceneObject, 0xb4)) {
            return false;
        }

        bool wroteAny = false;
        float velocity[3] = {};
        CopyVec3(velocity, snapshot.state20Live150);
        if (g_setSceneObjectVelocity) {
            wroteAny = CallSetSceneObjectVec3Internal(g_setSceneObjectVelocity, sceneObject, velocity) || wroteAny;
        }

        uintptr_t fallbackState = 0;
        if (IsReadable(reinterpret_cast<uint8_t*>(sceneObject) + 0xa0, sizeof(uintptr_t))) {
            fallbackState = *reinterpret_cast<uintptr_t*>(reinterpret_cast<uint8_t*>(sceneObject) + 0xa0);
        }
        if (fallbackState != 0) {
            wroteAny = WriteVec3(fallbackState + 0x20, snapshot.state20Live150) || wroteAny;
        }

        uintptr_t liveBody = 0;
        if (IsReadable(reinterpret_cast<uint8_t*>(sceneObject) + 0xa8, sizeof(uintptr_t))) {
            liveBody = *reinterpret_cast<uintptr_t*>(reinterpret_cast<uint8_t*>(sceneObject) + 0xa8);
        }
        if (liveBody != 0
            && IsReadable(reinterpret_cast<uint8_t*>(sceneObject) + 0xb0, sizeof(uint8_t))
            && *reinterpret_cast<uint8_t*>(reinterpret_cast<uint8_t*>(sceneObject) + 0xb0) == 0) {
            wroteAny = WriteVec3(liveBody + 0x150, snapshot.state20Live150) || wroteAny;
        }

        return wroteAny;
    }

    static size_t ApplyCapturedSceneObjectVelocities(void* bike, const SaveStateSlot& slot) {
        size_t restored = 0;
        for (size_t i = 0; i < slot.sceneObjectCount; ++i) {
            if (ApplyCapturedSceneObjectVelocity(bike, slot.sceneObjects[i])) {
                ++restored;
            }
        }
        return restored;
    }

    static bool CallUpdateBikeAndCameraFromTrackInternal(
        void* bike,
        void* checkpoint,
        char preserveDynamicObjects,
        int trackEvalMode) {
        __try {
            g_updateBikeAndCameraFromTrack(bike, checkpoint, preserveDynamicObjects, trackEvalMode);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    static bool CallUpdateBikeAndCameraFromTrack(
        void* bike,
        void* checkpoint,
        char preserveDynamicObjects,
        int trackEvalMode) {
        if (!g_updateBikeAndCameraFromTrack) {
            LOG_ERROR("[SaveStates] UpdateBikeAndCameraFromTrack is not initialized");
            return false;
        }

        bool result = CallUpdateBikeAndCameraFromTrackInternal(
            bike,
            checkpoint,
            preserveDynamicObjects,
            trackEvalMode);
        if (!result) {
            LOG_ERROR("[SaveStates] Exception in UpdateBikeAndCameraFromTrack");
        }
        return result;
    }

    bool Initialize(uintptr_t baseAddress) {
        if (g_initialized) {
            return true;
        }

        g_baseAddress = baseAddress;
        g_isSteamVersion = BaseAddress::IsSteamVersion();
        g_updateBikeAndCameraFromTrack =
            reinterpret_cast<UpdateBikeAndCameraFromTrackFunc>(
                g_baseAddress + GetUpdateBikeAndCameraFromTrackRVA());
        g_findSceneObjectByHash =
            reinterpret_cast<FindSceneObjectByHashFunc>(
                g_baseAddress + GetFindSceneObjectByHashRVA());
        g_setSceneObjectPosition =
            reinterpret_cast<SetSceneObjectVec3Func>(
                g_baseAddress + GetSetSceneObjectPositionRVA());
        g_setSceneObjectVelocity =
            reinterpret_cast<SetSceneObjectVec3Func>(
                g_baseAddress + GetSetSceneObjectVelocityRVA());
        g_setQuaternionRotation =
            reinterpret_cast<SetQuaternionRotationFunc>(
                g_baseAddress + GetSetQuaternionRotationRVA());
        g_updateRiderPositionAndAnimation =
            reinterpret_cast<UpdateRiderPositionAndAnimationFunc>(
                g_baseAddress + GetUpdateRiderPositionAndAnimationRVA());
        g_updateCameraTransformsAndPhysics =
            reinterpret_cast<UpdateCameraTransformsAndPhysicsFunc>(
                g_baseAddress + GetUpdateCameraTransformsAndPhysicsRVA());
        g_getQuaternionOrAngularVelocity =
            reinterpret_cast<GetQuaternionOrAngularVelocityFunc>(
                g_baseAddress + GetGetQuaternionOrAngularVelocityRVA());
        g_getRigidBodyVelocity =
            reinterpret_cast<GetSceneObjectVec3Func>(
                g_baseAddress + GetGetRigidBodyVelocityRVA());
        g_getRagdollBonePosition =
            reinterpret_cast<GetSceneObjectVec3Func>(
                g_baseAddress + GetGetRagdollBonePositionRVA());
        g_collectSceneObjectsByType =
            reinterpret_cast<CollectSceneObjectsByTypeFunc>(
                g_baseAddress + GetCollectSceneObjectsByTypeRVA());
        g_triggerCameraWarp =
            reinterpret_cast<TriggerCameraWarpFunc>(
                g_baseAddress + GetTriggerCameraWarpRVA());
        g_globalStructPtr = reinterpret_cast<void**>(g_baseAddress + GetGlobalStructRVA());
        g_executeTaskWithLocking =
            reinterpret_cast<ExecuteTaskWithLockingFunc>(
                g_baseAddress + GetExecuteTaskWithLockingRVA());
        g_executeAsyncTask =
            reinterpret_cast<ExecuteAsyncTaskFunc>(
                g_baseAddress + GetExecuteAsyncTaskRVA());

        if (!g_updateBikeAndCameraFromTrack
            || !g_findSceneObjectByHash
            || !g_setSceneObjectPosition
            || !g_setSceneObjectVelocity
            || !g_setQuaternionRotation
            || !g_updateRiderPositionAndAnimation
            || !g_updateCameraTransformsAndPhysics
            || !g_getQuaternionOrAngularVelocity
            || !g_getRigidBodyVelocity
            || !g_getRagdollBonePosition
            || !g_collectSceneObjectsByType
            || !g_triggerCameraWarp
            || !g_globalStructPtr
            || !g_executeTaskWithLocking
            || !g_executeAsyncTask) {
            LOG_ERROR("[SaveStates] Failed to initialize native function pointers");
            return false;
        }

        g_initialized = true;
        LOG_INFO("[SaveStates] Initialized"
            << " version=" << (g_isSteamVersion ? "Steam" : "Uplay")
            << " base=0x" << std::hex << g_baseAddress
            << " updateBikeAndCamera=0x" << reinterpret_cast<uintptr_t>(g_updateBikeAndCameraFromTrack)
            << " findSceneObject=0x" << reinterpret_cast<uintptr_t>(g_findSceneObjectByHash)
            << " setPosition=0x" << reinterpret_cast<uintptr_t>(g_setSceneObjectPosition)
            << " setVelocity=0x" << reinterpret_cast<uintptr_t>(g_setSceneObjectVelocity)
            << " setQuaternion=0x" << reinterpret_cast<uintptr_t>(g_setQuaternionRotation)
            << " updateRider=0x" << reinterpret_cast<uintptr_t>(g_updateRiderPositionAndAnimation)
            << " updateCameraPhysics=0x" << reinterpret_cast<uintptr_t>(g_updateCameraTransformsAndPhysics)
            << " getQuat=0x" << reinterpret_cast<uintptr_t>(g_getQuaternionOrAngularVelocity)
            << " getVelocity=0x" << reinterpret_cast<uintptr_t>(g_getRigidBodyVelocity)
            << " getBonePosition=0x" << reinterpret_cast<uintptr_t>(g_getRagdollBonePosition)
            << " collectObjects=0x" << reinterpret_cast<uintptr_t>(g_collectSceneObjectsByType)
            << " cameraWarp=0x" << reinterpret_cast<uintptr_t>(g_triggerCameraWarp)
            << std::dec);
        return true;
    }

    void Shutdown() {
        g_initialized = false;
        g_baseAddress = 0;
        g_updateBikeAndCameraFromTrack = nullptr;
        g_findSceneObjectByHash = nullptr;
        g_setSceneObjectPosition = nullptr;
        g_setSceneObjectVelocity = nullptr;
        g_setQuaternionRotation = nullptr;
        g_updateRiderPositionAndAnimation = nullptr;
        g_updateCameraTransformsAndPhysics = nullptr;
        g_getQuaternionOrAngularVelocity = nullptr;
        g_getRigidBodyVelocity = nullptr;
        g_getRagdollBonePosition = nullptr;
        g_collectSceneObjectsByType = nullptr;
        g_triggerCameraWarp = nullptr;
        g_globalStructPtr = nullptr;
        g_executeTaskWithLocking = nullptr;
        g_executeAsyncTask = nullptr;
        g_slot = SaveStateSlot{};
        InterlockedExchange(&g_pendingCapture, 0);
        InterlockedExchange(&g_pendingRestore, 0);
        InterlockedExchange(&g_pendingDebugDump, 0);
        g_isSteamVersion = false;
        LOG_VERBOSE("[SaveStates] Shutdown");
    }

    bool CaptureSlot() {
        if (!g_initialized) {
            LOG_ERROR("[SaveStates] Not initialized");
            return false;
        }

        void* bike = Respawn::GetBikePointer();
        int checkpointIndex = Respawn::GetCurrentCheckpointIndex();
        void* checkpoint = checkpointIndex >= 0 ? Respawn::GetCheckpointPointer(checkpointIndex) : nullptr;

        if (!bike || checkpointIndex < 0 || !checkpoint) {
            LOG_ERROR("[SaveStates] Capture failed"
                << " bike=0x" << std::hex << reinterpret_cast<uintptr_t>(bike)
                << " checkpointIndex=" << std::dec << checkpointIndex
                << " checkpoint=0x" << std::hex << reinterpret_cast<uintptr_t>(checkpoint)
                << std::dec);
            g_slot.valid = false;
            return false;
        }

        SaveStateSlot nextSlot;
        nextSlot.bikePtr = bike;
        nextSlot.checkpointIndex = checkpointIndex;
        nextSlot.checkpointPtr = checkpoint;
        nextSlot.raceTimeMs = Respawn::GetRaceTimeMs();
        nextSlot.faults = Respawn::GetFaultCount();
        CaptureBikeFrameTasks(bike, nextSlot);
        nextSlot.hasBikeTrackState = CopyBikeTrackState(bike, nextSlot);
        nextSlot.hasBikePoseCache = CopyBikePoseCache(bike, nextSlot.bikePoseCache);
        nextSlot.sceneObjectCount = CaptureSceneObjectSnapshots(bike, nextSlot.sceneObjects);
        nextSlot.hasPortalTargetPosition = CopyPortalTargetPosition(nextSlot, nextSlot.portalTargetPosition);
        nextSlot.hasPortalTargetQuaternion = CopyPrimaryBodyLiveQuaternion(nextSlot, nextSlot.portalTargetQuaternion);
        if (!nextSlot.hasPortalTargetQuaternion) {
            nextSlot.hasPortalTargetQuaternion =
                g_getQuaternionOrAngularVelocity &&
                CallGetQuaternionOrAngularVelocityInternal(bike, nextSlot.portalTargetQuaternion);
        }
        nextSlot.valid = true;

        g_slot = nextSlot;

        LOG_INFO("[SaveStates] Captured slot"
            << " checkpointIndex=" << g_slot.checkpointIndex
            << " checkpoint=0x" << std::hex << reinterpret_cast<uintptr_t>(g_slot.checkpointPtr)
            << " bike=0x" << reinterpret_cast<uintptr_t>(g_slot.bikePtr)
            << std::dec
            << " time=" << g_slot.raceTimeMs
            << " faults=" << g_slot.faults
            << " trackFrame=" << g_slot.trackFrame
            << " trackFrameAux=" << g_slot.trackFrameAux
            << " trackState=" << (g_slot.hasBikeTrackState ? "yes" : "no")
            << " poseCache=" << (g_slot.hasBikePoseCache ? "yes" : "no")
            << " portalPos=" << (g_slot.hasPortalTargetPosition ? "yes" : "no")
            << " portalQuat=" << (g_slot.hasPortalTargetQuaternion ? "yes" : "no")
            << " sceneObjects=" << g_slot.sceneObjectCount);
        return true;
    }

    static bool ApplyCapturedSlotWithPortalTransform() {
        if (!g_initialized) {
            LOG_ERROR("[SaveStates] Not initialized");
            return false;
        }

        if (!g_slot.valid) {
            LOG_WARNING("[SaveStates] No captured slot to restore");
            return false;
        }

        void* bike = Respawn::GetBikePointer();

        if (!bike) {
            LOG_ERROR("[SaveStates] Restore failed"
                << " bike=0x" << std::hex << reinterpret_cast<uintptr_t>(bike)
                << " checkpointIndex=" << std::dec << g_slot.checkpointIndex
                << std::dec);
            return false;
        }

        LOG_INFO("[SaveStates] Applying captured slot"
            << " checkpointIndex=" << g_slot.checkpointIndex
            << " bike=0x" << reinterpret_cast<uintptr_t>(bike)
            << std::dec
            << " time=" << g_slot.raceTimeMs
            << " faults=" << g_slot.faults
            << " trackFrame=" << g_slot.trackFrame
            << " mode=track-frame");

        if (!RestoreBikeFrameTasks(bike, g_slot.trackFrame, g_slot.trackFrameAux)) {
            LOG_WARNING("[SaveStates] Failed to restore encrypted frame-task values");
        }
        RestoreBikeTrackState(bike, g_slot);

        void* checkpoint = Respawn::GetCheckpointPointer(g_slot.checkpointIndex);
        bool usedTrackFrameRestore = false;
        if (!checkpoint) {
            LOG_WARNING("[SaveStates] Failed to resolve checkpoint pointer for native restore");
        }
        else {
            usedTrackFrameRestore = CallUpdateBikeAndCameraFromTrack(bike, checkpoint, 1, 1);
        }

        size_t restoredSceneObjects = RestoreSceneObjectSnapshots(bike, g_slot);
        if (g_slot.sceneObjectCount != 0 && restoredSceneObjects == 0) {
            LOG_WARNING("[SaveStates] Failed to restore scene object snapshots");
        }

        if (g_slot.hasBikePoseCache && !RestoreBikePoseCache(bike, g_slot.bikePoseCache)) {
            LOG_WARNING("[SaveStates] Failed to restore bike pose cache");
        }

        if (!Respawn::SetFaultCounterValue(g_slot.faults)) {
            LOG_WARNING("[SaveStates] Failed to restore fault counter");
        }

        if (!Respawn::SetRaceTimeMs(g_slot.raceTimeMs)) {
            LOG_WARNING("[SaveStates] Failed to restore race time");
        }

        size_t restoredVelocities = ApplyCapturedSceneObjectVelocities(bike, g_slot);
        if (g_slot.sceneObjectCount != 0 && restoredVelocities == 0) {
            LOG_WARNING("[SaveStates] Failed to apply captured scene object velocities");
        }

        LOG_INFO("[SaveStates] Restore complete"
            << " trackFrameRestore=" << (usedTrackFrameRestore ? "yes" : "no")
            << " sceneObjects=" << restoredSceneObjects << "/" << g_slot.sceneObjectCount
            << " velocities=" << restoredVelocities << "/" << g_slot.sceneObjectCount);
        PreventFinish::NotifySaveStateRestore();
        return true;
    }

    bool RestoreSlotNativeOnly() {
        if (!g_initialized) {
            LOG_ERROR("[SaveStates] Not initialized");
            return false;
        }

        if (!g_slot.valid) {
            LOG_WARNING("[SaveStates] No captured slot to restore");
            return false;
        }

        LOG_INFO("[SaveStates] Starting native track-frame restore"
            << " checkpointIndex=" << g_slot.checkpointIndex
            << " time=" << g_slot.raceTimeMs
            << " faults=" << g_slot.faults
            << " trackFrame=" << g_slot.trackFrame);

        return ApplyCapturedSlotWithPortalTransform();
    }

    void DebugDumpSlot() {
        LOG_INFO("[SaveStates] Slot"
            << " valid=" << (g_slot.valid ? "yes" : "no")
            << " checkpointIndex=" << g_slot.checkpointIndex
            << " checkpoint=0x" << std::hex << reinterpret_cast<uintptr_t>(g_slot.checkpointPtr)
            << " bike=0x" << reinterpret_cast<uintptr_t>(g_slot.bikePtr)
            << " updateBikeAndCamera=0x" << reinterpret_cast<uintptr_t>(g_updateBikeAndCameraFromTrack)
            << std::dec
            << " time=" << g_slot.raceTimeMs
            << " faults=" << g_slot.faults
            << " trackFrame=" << g_slot.trackFrame
            << " trackFrameAux=" << g_slot.trackFrameAux
            << " trackState=" << (g_slot.hasBikeTrackState ? "yes" : "no")
            << " poseCache=" << (g_slot.hasBikePoseCache ? "yes" : "no")
            << " portalPos=" << (g_slot.hasPortalTargetPosition ? "yes" : "no")
            << " portalQuat=" << (g_slot.hasPortalTargetQuaternion ? "yes" : "no")
            << " sceneObjects=" << g_slot.sceneObjectCount);
        for (size_t i = 0; i < g_slot.sceneObjectCount; ++i) {
            const SceneObjectSnapshot& snapshot = g_slot.sceneObjects[i];
            LOG_INFO("[SaveStates]   object[" << i << "]"
                << " hash=0x" << std::hex << snapshot.hash
                << " ptr=0x" << reinterpret_cast<uintptr_t>(snapshot.objectPtr)
                << std::dec
                << " state150=(" << snapshot.state20Live150[0] << ", " << snapshot.state20Live150[1] << ", " << snapshot.state20Live150[2] << ")"
                << " state160=(" << snapshot.state2cLive160[0] << ", " << snapshot.state2cLive160[1] << ", " << snapshot.state2cLive160[2] << ")"
                << " liveTransform=" << (snapshot.hasLiveTransform ? "yes" : "no")
                << " liveState=" << (snapshot.hasLiveState ? "yes" : "no")
                << " angularState=" << (snapshot.hasAngularState ? "yes" : "no")
                << " fallbackState=" << (snapshot.hasFallbackState ? "yes" : "no")
                << " transformPos=" << (snapshot.hasTransformPosition ? "yes" : "no")
                << " transformQuat=" << (snapshot.hasTransformQuaternion ? "yes" : "no"));
        }
    }

    void CheckHotkey() {
        if (!g_initialized) {
            return;
        }

        if (Keybindings::IsActionPressed(Keybindings::Action::CaptureSaveState)) {
            InterlockedExchange(&g_pendingCapture, 1);
            LOG_INFO("[SaveStates] Capture queued for main thread");
        }

        if (Keybindings::IsActionPressed(Keybindings::Action::RestoreSaveState)) {
            InterlockedExchange(&g_pendingRestore, 1);
            LOG_INFO("[SaveStates] Restore queued for main thread");
        }

        if (Keybindings::IsActionPressed(Keybindings::Action::DebugSaveState)) {
            InterlockedExchange(&g_pendingDebugDump, 1);
            LOG_INFO("[SaveStates] Debug dump queued for main thread");
        }
    }

    void ProcessPendingMainThread() {
        if (!g_initialized) {
            return;
        }

        if (InterlockedExchange(&g_pendingCapture, 0) != 0) {
            CaptureSlot();
        }

        if (InterlockedExchange(&g_pendingRestore, 0) != 0) {
            RestoreSlotNativeOnly();
        }

        if (InterlockedExchange(&g_pendingDebugDump, 0) != 0) {
            DebugDumpSlot();
        }
    }
}
