#include "pch.h"
#include "bike-swap.h"
#include "logging.h"
#include "keybindings.h"
#include "base-address.h"
#include "respawn.h"
#include <Windows.h>
#include <TlHelp32.h>
#include <vector>
#include <MinHook.h>

namespace BikeSwap {
    // RVA for ReloadBikeFromSettings
    static constexpr uintptr_t RELOAD_BIKE_FROM_SETTINGS_RVA_UPLAY = 0x9fd40;
    static constexpr uintptr_t RELOAD_BIKE_FROM_SETTINGS_RVA_STEAM = 0x9f810;

    // ============================================================================
    // Game Memory Addresses - UPLAY VERSION (RVA offsets from Ghidra base 0x700000)
    // ============================================================================

    // Global game manager pointer - same as respawn.cpp
    static constexpr uintptr_t GLOBAL_STRUCT_RVA_UPLAY = 0x104b308;

    // Bike-related function addresses - Uplay
    static constexpr uintptr_t CHANGE_BIKE_WITH_MESH_RELOAD_RVA_UPLAY = 0x229c00;
    static constexpr uintptr_t LOAD_BIKE_SETTINGS_RVA_UPLAY = 0x208490;
    static constexpr uintptr_t LOAD_BIKE_MESH_AND_VISUALS_RVA_UPLAY = 0x2144e0;
    static constexpr uintptr_t CLEANUP_SCENE_GEOMETRY_RVA_UPLAY = 0x21e610;
    static constexpr uintptr_t FINALIZE_RIDER_SETUP_RVA_UPLAY = 0x20a7c0;
    static constexpr uintptr_t RESET_BIKE_STATE_RVA_UPLAY = 0x2059d0;
    static constexpr uintptr_t INIT_BIKE_APPEARANCE_SLOTS_RVA_UPLAY = 0x229980;
    static constexpr uintptr_t SERIALIZE_BIKE_SCENE_OBJECTS_RVA_UPLAY = 0x205750;
    static constexpr uintptr_t GET_BIKE_DATA_BY_INDEX_RVA_UPLAY = 0x19a30;
    static constexpr uintptr_t GET_FIRST_ENTITY_FROM_LIST_RVA_UPLAY = 0x25f000;
    static constexpr uintptr_t HANDLE_GAME_FRAME_UPDATE_RVA_UPLAY = 0x3b6980;

    // ============================================================================
    // Game Memory Addresses - STEAM VERSION (RVA offsets from Ghidra base 0x140000)
    // ============================================================================

    // Global game manager pointer - same as respawn.cpp
    static constexpr uintptr_t GLOBAL_STRUCT_RVA_STEAM = 0x104d308;

    // Bike-related function addresses - Steam
    static constexpr uintptr_t CHANGE_BIKE_WITH_MESH_RELOAD_RVA_STEAM = 0x2294d0;
    static constexpr uintptr_t LOAD_BIKE_SETTINGS_RVA_STEAM = 0x207d40;
    static constexpr uintptr_t LOAD_BIKE_MESH_AND_VISUALS_RVA_STEAM = 0x213dd0;
    static constexpr uintptr_t CLEANUP_SCENE_GEOMETRY_RVA_STEAM = 0x21df00;
    static constexpr uintptr_t FINALIZE_RIDER_SETUP_RVA_STEAM = 0x20a070;
    static constexpr uintptr_t RESET_BIKE_STATE_RVA_STEAM = 0x205310;
    static constexpr uintptr_t INIT_BIKE_APPEARANCE_SLOTS_RVA_STEAM = 0x229250;
    static constexpr uintptr_t SERIALIZE_BIKE_SCENE_OBJECTS_RVA_STEAM = 0x205090;
    static constexpr uintptr_t GET_BIKE_DATA_BY_INDEX_RVA_STEAM = 0x19940;
    static constexpr uintptr_t GET_FIRST_ENTITY_FROM_LIST_RVA_STEAM = 0x25eb20;
    static constexpr uintptr_t HANDLE_GAME_FRAME_UPDATE_RVA_STEAM = 0x3b6140;

    // ============================================================================
    // Structure offsets (same for both versions)
    // ============================================================================

    // GameManager offsets
    static constexpr uintptr_t GAME_MANAGER_OFFSET = 0xdc;
    static constexpr uintptr_t BIKE_LIST_STRUCT_OFFSET = 0x2f0;
    static constexpr uintptr_t BIKE_DATA_MANAGER_OFFSET = 0x118;

    // Bike list structure offsets
    static constexpr uintptr_t BIKE_LIST_FIRST_PTR_OFFSET = 0x14;
    static constexpr uintptr_t BIKE_LIST_COUNT_OFFSET = 0x34;

    // Bike entity offsets
    static constexpr uintptr_t BIKE_ID_OFFSET = 0x680;

    // ============================================================================
    // Helper functions to get correct RVA based on detected version
    // ============================================================================

    static uintptr_t GetGlobalStructRVA() {
        return BaseAddress::IsSteamVersion() ? GLOBAL_STRUCT_RVA_STEAM : GLOBAL_STRUCT_RVA_UPLAY;
    }

    static uintptr_t GetChangeBikeWithMeshReloadRVA() {
        return BaseAddress::IsSteamVersion() ? CHANGE_BIKE_WITH_MESH_RELOAD_RVA_STEAM : CHANGE_BIKE_WITH_MESH_RELOAD_RVA_UPLAY;
    }

    static uintptr_t GetLoadBikeSettingsRVA() {
        return BaseAddress::IsSteamVersion() ? LOAD_BIKE_SETTINGS_RVA_STEAM : LOAD_BIKE_SETTINGS_RVA_UPLAY;
    }

    static uintptr_t GetLoadBikeMeshAndVisualsRVA() {
        return BaseAddress::IsSteamVersion() ? LOAD_BIKE_MESH_AND_VISUALS_RVA_STEAM : LOAD_BIKE_MESH_AND_VISUALS_RVA_UPLAY;
    }

    static uintptr_t GetCleanupSceneGeometryRVA() {
        return BaseAddress::IsSteamVersion() ? CLEANUP_SCENE_GEOMETRY_RVA_STEAM : CLEANUP_SCENE_GEOMETRY_RVA_UPLAY;
    }

    static uintptr_t GetFinalizeRiderSetupRVA() {
        return BaseAddress::IsSteamVersion() ? FINALIZE_RIDER_SETUP_RVA_STEAM : FINALIZE_RIDER_SETUP_RVA_UPLAY;
    }

    static uintptr_t GetResetBikeStateRVA() {
        return BaseAddress::IsSteamVersion() ? RESET_BIKE_STATE_RVA_STEAM : RESET_BIKE_STATE_RVA_UPLAY;
    }

    static uintptr_t GetInitBikeAppearanceSlotsRVA() {
        return BaseAddress::IsSteamVersion() ? INIT_BIKE_APPEARANCE_SLOTS_RVA_STEAM : INIT_BIKE_APPEARANCE_SLOTS_RVA_UPLAY;
    }

    static uintptr_t GetSerializeBikeSceneObjectsRVA() {
        return BaseAddress::IsSteamVersion() ? SERIALIZE_BIKE_SCENE_OBJECTS_RVA_STEAM : SERIALIZE_BIKE_SCENE_OBJECTS_RVA_UPLAY;
    }

    static uintptr_t GetBikeDataByIndexRVA() {
        return BaseAddress::IsSteamVersion() ? GET_BIKE_DATA_BY_INDEX_RVA_STEAM : GET_BIKE_DATA_BY_INDEX_RVA_UPLAY;
    }

    static uintptr_t GetFirstEntityFromListRVA() {
        return BaseAddress::IsSteamVersion() ? GET_FIRST_ENTITY_FROM_LIST_RVA_STEAM : GET_FIRST_ENTITY_FROM_LIST_RVA_UPLAY;
    }

    static uintptr_t GetHandleGameFrameUpdateRVA() {
        return BaseAddress::IsSteamVersion() ? HANDLE_GAME_FRAME_UPDATE_RVA_STEAM : HANDLE_GAME_FRAME_UPDATE_RVA_UPLAY;
    }

    static uintptr_t GetReloadBikeFromSettingsRVA() {
        return BaseAddress::IsSteamVersion() ? RELOAD_BIKE_FROM_SETTINGS_RVA_STEAM : RELOAD_BIKE_FROM_SETTINGS_RVA_UPLAY;
    }

    // ============================================================================
    // Function Pointer Types
    // ============================================================================

    // void __thiscall ChangeBikeWithMeshReload(void* this, byte bikeId, undefined2* bikeAppearanceData)
    // Note: This uses __stdcall convention for the stack params (callee cleans up with RET 0x8)
    // We use an assembly wrapper since __thiscall function pointers can be tricky
    typedef void* ChangeBikeWithMeshReloadFunc;  // We'll call via asm

    // void __fastcall LoadBikeSettings(void* bikeEntity)
    typedef void(__fastcall* LoadBikeSettingsFunc)(void* bikeEntity);

    // void __fastcall LoadBikeMeshAndVisuals(void* bikeEntity)  
    typedef void(__fastcall* LoadBikeMeshAndVisualsFunc)(void* bikeEntity);

    // void __thiscall CleanupSceneGeometry(void* this, char param1)
    typedef void(__thiscall* CleanupSceneGeometryFunc)(void* thisPtr, char param1);

    // void __fastcall FinalizeRiderSetup(void* bikeEntity)
    typedef void(__fastcall* FinalizeRiderSetupFunc)(void* bikeEntity);

    // void __fastcall ResetBikeState(int bikeEntity)
    typedef void(__fastcall* ResetBikeStateFunc)(int bikeEntity);

    // void __thiscall InitializeBikeAppearanceSlots(void* this, char param1)
    typedef void(__thiscall* InitBikeAppearanceSlotsFunc)(void* thisPtr, char param1);

    // void __fastcall SerializeBikeSceneObjects(int bikeEntity)
    typedef void(__fastcall* SerializeBikeSceneObjectsFunc)(int bikeEntity);

    // int __thiscall GetBikeDataByIndex(void* bikeDataManager, byte index)
    typedef int(__thiscall* GetBikeDataByIndexFunc)(void* bikeDataManager, uint8_t index);

    // int __fastcall GetFirstEntityFromList(int gameManager)
    typedef int(__fastcall* GetFirstEntityFromListFunc)(int gameManager);

    // ============================================================================
    // Global State
    // ============================================================================

    static bool g_initialized = false;
    static uintptr_t g_baseAddress = 0;
    static void** g_globalStructPtr = nullptr;

    // Function pointers
    static ChangeBikeWithMeshReloadFunc g_changeBikeWithMeshReload = nullptr;
    static LoadBikeSettingsFunc g_loadBikeSettings = nullptr;
    static LoadBikeMeshAndVisualsFunc g_loadBikeMeshAndVisuals = nullptr;
    static CleanupSceneGeometryFunc g_cleanupSceneGeometry = nullptr;
    static FinalizeRiderSetupFunc g_finalizeRiderSetup = nullptr;
    static ResetBikeStateFunc g_resetBikeState = nullptr;
    static InitBikeAppearanceSlotsFunc g_initBikeAppearanceSlots = nullptr;
    static SerializeBikeSceneObjectsFunc g_serializeBikeSceneObjects = nullptr;
    static GetBikeDataByIndexFunc g_getBikeDataByIndex = nullptr;
    static GetFirstEntityFromListFunc g_getFirstEntityFromList = nullptr;

    // ============================================================================
    // Thread Suspension Helpers (for thread-safe bike swapping)
    // ============================================================================

    static std::vector<HANDLE> SuspendOtherThreads() {
        std::vector<HANDLE> suspendedThreads;
        DWORD currentThreadId = GetCurrentThreadId();
        DWORD processId = GetCurrentProcessId();

        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snapshot == INVALID_HANDLE_VALUE) {
            LOG_ERROR("[BikeSwap] Failed to create thread snapshot");
            return suspendedThreads;
        }

        THREADENTRY32 te32;
        te32.dwSize = sizeof(THREADENTRY32);

        if (Thread32First(snapshot, &te32)) {
            do {
                if (te32.th32OwnerProcessID == processId && te32.th32ThreadID != currentThreadId) {
                    HANDLE hThread = OpenThread(THREAD_SUSPEND_RESUME, FALSE, te32.th32ThreadID);
                    if (hThread != NULL) {
                        if (SuspendThread(hThread) != (DWORD)-1) {
                            suspendedThreads.push_back(hThread);
                        }
                        else {
                            CloseHandle(hThread);
                        }
                    }
                }
            } while (Thread32Next(snapshot, &te32));
        }

        CloseHandle(snapshot);
        LOG_INFO("[BikeSwap] Suspended " << suspendedThreads.size() << " threads");
        return suspendedThreads;
    }

    static void ResumeThreads(std::vector<HANDLE>& threads) {
        for (HANDLE hThread : threads) {
            ResumeThread(hThread);
            CloseHandle(hThread);
        }
        LOG_INFO("[BikeSwap] Resumed " << threads.size() << " threads");
        threads.clear();
    }

    // ============================================================================
    // SEH-safe Wrapper Functions
    // These use a two-layer approach: inner function does SEH, outer does logging
    // ============================================================================

    static void* GetGameManager() {
        if (!g_globalStructPtr || IsBadReadPtr(g_globalStructPtr, sizeof(void*))) {
            return nullptr;
        }

        void* globalStruct = *g_globalStructPtr;
        if (!globalStruct || IsBadReadPtr(globalStruct, 0x100)) {
            return nullptr;
        }

        uintptr_t managerAddr = reinterpret_cast<uintptr_t>(globalStruct) + GAME_MANAGER_OFFSET;
        if (IsBadReadPtr((void*)managerAddr, sizeof(void*))) {
            return nullptr;
        }

        void* manager = *reinterpret_cast<void**>(managerAddr);
        if (!manager || IsBadReadPtr(manager, 0x1000)) {
            return nullptr;
        }

        return manager;
    }

    static void* GetBikeDataManager() {
        void* manager = GetGameManager();
        if (!manager) {
            return nullptr;
        }

        uintptr_t bikeDataMgrAddr = reinterpret_cast<uintptr_t>(manager) + BIKE_DATA_MANAGER_OFFSET;
        if (IsBadReadPtr((void*)bikeDataMgrAddr, sizeof(void*))) {
            return nullptr;
        }

        void* bikeDataMgr = *reinterpret_cast<void**>(bikeDataMgrAddr);
        if (!bikeDataMgr || IsBadReadPtr(bikeDataMgr, 0x100)) {
            return nullptr;
        }

        return bikeDataMgr;
    }

    static void* GetCurrentBikeEntity() {
        // Use the GetBikePointer from Respawn module since it does the same thing
        return Respawn::GetBikePointer();
    }

    // Assembly wrapper for thiscall function
    // ChangeBikeWithMeshReload: void __thiscall(void* this, byte bikeId, void* appearanceData)
    // thiscall: this in ECX, params pushed right-to-left
    static void CallChangeBikeWithMeshReload_Asm(void* bikeEntity, uint8_t bikeId, void* appearanceData) {
        __asm {
            push appearanceData    // param2: appearance data pointer
            movzx eax, bikeId      // param1: bike ID (extend byte to dword)
            push eax
            mov ecx, bikeEntity    // this pointer in ECX
            call g_changeBikeWithMeshReload
        }
    }

    // Inner SEH wrapper - no C++ objects allowed
    static bool CallChangeBikeWithMeshReload_Inner(void* bikeEntity, uint8_t bikeId, void* appearanceData, DWORD* exceptionCode) {
        *exceptionCode = 0;
        __try {
            CallChangeBikeWithMeshReload_Asm(bikeEntity, bikeId, appearanceData);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            *exceptionCode = GetExceptionCode();
            return false;
        }
    }

    // Outer wrapper with logging
    static bool CallChangeBikeWithMeshReload(void* bikeEntity, uint8_t bikeId, void* appearanceData) {
        DWORD exceptionCode = 0;
        bool result = CallChangeBikeWithMeshReload_Inner(bikeEntity, bikeId, appearanceData, &exceptionCode);
        if (!result) {
            LOG_ERROR("[BikeSwap] Exception in ChangeBikeWithMeshReload: 0x" << std::hex << exceptionCode);
        }
        return result;
    }

    // Inner SEH wrapper
    static bool CallLoadBikeSettings_Inner(void* bikeEntity, DWORD* exceptionCode) {
        *exceptionCode = 0;
        __try {
            g_loadBikeSettings(bikeEntity);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            *exceptionCode = GetExceptionCode();
            return false;
        }
    }

    static bool CallLoadBikeSettings(void* bikeEntity) {
        DWORD exceptionCode = 0;
        bool result = CallLoadBikeSettings_Inner(bikeEntity, &exceptionCode);
        if (!result) {
            LOG_ERROR("[BikeSwap] Exception in LoadBikeSettings: 0x" << std::hex << exceptionCode);
        }
        return result;
    }

    // Inner SEH wrapper
    static bool CallLoadBikeMeshAndVisuals_Inner(void* bikeEntity, DWORD* exceptionCode) {
        *exceptionCode = 0;
        __try {
            g_loadBikeMeshAndVisuals(bikeEntity);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            *exceptionCode = GetExceptionCode();
            return false;
        }
    }

    static bool CallLoadBikeMeshAndVisuals(void* bikeEntity) {
        DWORD exceptionCode = 0;
        bool result = CallLoadBikeMeshAndVisuals_Inner(bikeEntity, &exceptionCode);
        if (!result) {
            LOG_ERROR("[BikeSwap] Exception in LoadBikeMeshAndVisuals: 0x" << std::hex << exceptionCode);
        }
        return result;
    }

    // Inner SEH wrapper
    static bool CallCleanupSceneGeometry_Inner(void* bikeEntity, char param1, DWORD* exceptionCode) {
        *exceptionCode = 0;
        __try {
            g_cleanupSceneGeometry(bikeEntity, param1);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            *exceptionCode = GetExceptionCode();
            return false;
        }
    }

    static bool CallCleanupSceneGeometry(void* bikeEntity, char param1) {
        DWORD exceptionCode = 0;
        bool result = CallCleanupSceneGeometry_Inner(bikeEntity, param1, &exceptionCode);
        if (!result) {
            LOG_ERROR("[BikeSwap] Exception in CleanupSceneGeometry: 0x" << std::hex << exceptionCode);
        }
        return result;
    }

    // Inner SEH wrapper
    static bool CallFinalizeRiderSetup_Inner(void* bikeEntity, DWORD* exceptionCode) {
        *exceptionCode = 0;
        __try {
            g_finalizeRiderSetup(bikeEntity);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            *exceptionCode = GetExceptionCode();
            return false;
        }
    }

    static bool CallFinalizeRiderSetup(void* bikeEntity) {
        DWORD exceptionCode = 0;
        bool result = CallFinalizeRiderSetup_Inner(bikeEntity, &exceptionCode);
        if (!result) {
            LOG_ERROR("[BikeSwap] Exception in FinalizeRiderSetup: 0x" << std::hex << exceptionCode);
        }
        return result;
    }

    // Inner SEH wrapper
    static bool CallResetBikeState_Inner(void* bikeEntity, DWORD* exceptionCode) {
        *exceptionCode = 0;
        __try {
            g_resetBikeState(reinterpret_cast<int>(bikeEntity));
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            *exceptionCode = GetExceptionCode();
            return false;
        }
    }

    static bool CallResetBikeState(void* bikeEntity) {
        DWORD exceptionCode = 0;
        bool result = CallResetBikeState_Inner(bikeEntity, &exceptionCode);
        if (!result) {
            LOG_ERROR("[BikeSwap] Exception in ResetBikeState: 0x" << std::hex << exceptionCode);
        }
        return result;
    }

    // Inner SEH wrapper
    static bool CallInitBikeAppearanceSlots_Inner(void* bikeEntity, char param1, DWORD* exceptionCode) {
        *exceptionCode = 0;
        __try {
            g_initBikeAppearanceSlots(bikeEntity, param1);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            *exceptionCode = GetExceptionCode();
            return false;
        }
    }

    static bool CallInitBikeAppearanceSlots(void* bikeEntity, char param1) {
        DWORD exceptionCode = 0;
        bool result = CallInitBikeAppearanceSlots_Inner(bikeEntity, param1, &exceptionCode);
        if (!result) {
            LOG_ERROR("[BikeSwap] Exception in InitializeBikeAppearanceSlots: 0x" << std::hex << exceptionCode);
        }
        return result;
    }

    // Inner SEH wrapper
    static bool CallSerializeBikeSceneObjects_Inner(void* bikeEntity, DWORD* exceptionCode) {
        *exceptionCode = 0;
        __try {
            g_serializeBikeSceneObjects(reinterpret_cast<int>(bikeEntity));
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            *exceptionCode = GetExceptionCode();
            return false;
        }
    }

    static bool CallSerializeBikeSceneObjects(void* bikeEntity) {
        DWORD exceptionCode = 0;
        bool result = CallSerializeBikeSceneObjects_Inner(bikeEntity, &exceptionCode);
        if (!result) {
            LOG_ERROR("[BikeSwap] Exception in SerializeBikeSceneObjects: 0x" << std::hex << exceptionCode);
        }
        return result;
    }

    // Inner SEH wrapper
    static int CallGetBikeDataByIndex_Inner(void* bikeDataManager, uint8_t index, DWORD* exceptionCode) {
        *exceptionCode = 0;
        __try {
            return g_getBikeDataByIndex(bikeDataManager, index);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            *exceptionCode = GetExceptionCode();
            return 0;
        }
    }

    static int CallGetBikeDataByIndex(void* bikeDataManager, uint8_t index) {
        DWORD exceptionCode = 0;
        int result = CallGetBikeDataByIndex_Inner(bikeDataManager, index, &exceptionCode);
        if (exceptionCode != 0) {
            LOG_ERROR("[BikeSwap] Exception in GetBikeDataByIndex: 0x" << std::hex << exceptionCode);
        }
        return result;
    }

    // ============================================================================
    // Public API Implementation
    // ============================================================================

    bool Initialize(uintptr_t baseAddress) {
        if (g_initialized) {
            LOG_WARNING("[BikeSwap] Already initialized");
            return true;
        }

        if (baseAddress == 0) {
            LOG_ERROR("[BikeSwap] Invalid base address");
            return false;
        }

        if (BaseAddress::IsSteamVersion()) {
            LOG_INFO("[BikeSwap] Steam version detected - using Steam addresses");
        }
        else {
            LOG_INFO("[BikeSwap] Uplay version detected - using Uplay addresses");
        }

        g_baseAddress = baseAddress;
        g_globalStructPtr = reinterpret_cast<void**>(baseAddress + GetGlobalStructRVA());

        // Initialize function pointers
        g_changeBikeWithMeshReload = reinterpret_cast<ChangeBikeWithMeshReloadFunc>(
            baseAddress + GetChangeBikeWithMeshReloadRVA());
        g_loadBikeSettings = reinterpret_cast<LoadBikeSettingsFunc>(
            baseAddress + GetLoadBikeSettingsRVA());
        g_loadBikeMeshAndVisuals = reinterpret_cast<LoadBikeMeshAndVisualsFunc>(
            baseAddress + GetLoadBikeMeshAndVisualsRVA());
        g_cleanupSceneGeometry = reinterpret_cast<CleanupSceneGeometryFunc>(
            baseAddress + GetCleanupSceneGeometryRVA());
        g_finalizeRiderSetup = reinterpret_cast<FinalizeRiderSetupFunc>(
            baseAddress + GetFinalizeRiderSetupRVA());
        g_resetBikeState = reinterpret_cast<ResetBikeStateFunc>(
            baseAddress + GetResetBikeStateRVA());
        g_initBikeAppearanceSlots = reinterpret_cast<InitBikeAppearanceSlotsFunc>(
            baseAddress + GetInitBikeAppearanceSlotsRVA());
        g_serializeBikeSceneObjects = reinterpret_cast<SerializeBikeSceneObjectsFunc>(
            baseAddress + GetSerializeBikeSceneObjectsRVA());
        g_getBikeDataByIndex = reinterpret_cast<GetBikeDataByIndexFunc>(
            baseAddress + GetBikeDataByIndexRVA());
        g_getFirstEntityFromList = reinterpret_cast<GetFirstEntityFromListFunc>(
            baseAddress + GetFirstEntityFromListRVA());

        if (IsBadReadPtr(g_globalStructPtr, sizeof(void*))) {
            LOG_ERROR("[BikeSwap] Invalid global struct pointer");
            return false;
        }

        g_initialized = true;
        LOG_INFO("[BikeSwap] Initialized successfully");

        return true;
    }

    void Shutdown() {
        if (!g_initialized) {
            return;
        }

        g_initialized = false;
        g_globalStructPtr = nullptr;
        g_changeBikeWithMeshReload = nullptr;
        g_loadBikeSettings = nullptr;
        g_loadBikeMeshAndVisuals = nullptr;
        g_cleanupSceneGeometry = nullptr;
        g_finalizeRiderSetup = nullptr;
        g_resetBikeState = nullptr;
        g_initBikeAppearanceSlots = nullptr;
        g_serializeBikeSceneObjects = nullptr;
        g_getBikeDataByIndex = nullptr;
        g_getFirstEntityFromList = nullptr;

        LOG_VERBOSE("[BikeSwap] Shutdown complete");
    }

    bool IsSwapAvailable() {
        if (!g_initialized) {
            return false;
        }

        void* bikeEntity = GetCurrentBikeEntity();
        return bikeEntity != nullptr;
    }

    int GetCurrentBikeId() {
        if (!g_initialized) {
            return -1;
        }

        void* bikeEntity = GetCurrentBikeEntity();
        if (!bikeEntity) {
            return -1;
        }

        // Read bike ID from bike entity at offset 0x680
        uintptr_t bikeIdAddr = reinterpret_cast<uintptr_t>(bikeEntity) + BIKE_ID_OFFSET;
        if (IsBadReadPtr((void*)bikeIdAddr, sizeof(int))) {
            return -1;
        }

        int bikeId = *reinterpret_cast<int*>(bikeIdAddr);
        return bikeId;
    }

    int GetTotalBikeCount() {
        if (!g_initialized) {
            return 0;
        }

        // The game has a fixed set of bikes (typically 8-10)
        // For safety, we'll return a reasonable max
        // Bikes: Pit Viper (0), Squid (1), Roach (2), Turtle (3), Jackal (4), 
        //        Mantis (5), Donkey (6), Rabbit (7), Rhino (8)
        return 9;
    }

    bool SwapToBike(int bikeId) {
        if (!g_initialized) {
            LOG_ERROR("[BikeSwap] Not initialized");
            return false;
        }

        if (bikeId < 0 || bikeId >= GetTotalBikeCount()) {
            LOG_ERROR("[BikeSwap] Invalid bike ID: " << bikeId);
            return false;
        }

        void* bikeEntity = GetCurrentBikeEntity();
        if (!bikeEntity) {
            LOG_ERROR("[BikeSwap] Could not get current bike entity - not in a race?");
            return false;
        }

        int currentBikeId = GetCurrentBikeId();
        if (currentBikeId == bikeId) {
            LOG_VERBOSE("[BikeSwap] Already on bike " << bikeId);
            return true;
        }

        LOG_INFO("[BikeSwap] Swapping from bike " << currentBikeId << " to bike " << bikeId);
        LOG_INFO("[BikeSwap] Bike entity: 0x" << std::hex << reinterpret_cast<uintptr_t>(bikeEntity));
        LOG_INFO("[BikeSwap] ChangeBikeWithMeshReload func: 0x" << std::hex << reinterpret_cast<uintptr_t>(g_changeBikeWithMeshReload));

        // Get the current appearance data from the bike entity
        // The appearance data is at bike+0x9ec (size 0x20 bytes based on ChangeBikeWithMeshReload)
        uintptr_t appearanceDataAddr = reinterpret_cast<uintptr_t>(bikeEntity) + 0x9ec;
        if (IsBadReadPtr((void*)appearanceDataAddr, 0x20)) {
            LOG_ERROR("[BikeSwap] Cannot read appearance data");
            return false;
        }

        // IMPORTANT: Copy appearance data to a local buffer!
        // ChangeBikeWithMeshReload copies FROM the param TO the bike entity.
        // If we pass the same address, it corrupts memory. We need a separate copy.
        uint8_t appearanceDataCopy[0x20];
        memcpy(appearanceDataCopy, reinterpret_cast<void*>(appearanceDataAddr), 0x20);

        LOG_INFO("[BikeSwap] Appearance data copied to local buffer at 0x" << std::hex << reinterpret_cast<uintptr_t>(appearanceDataCopy));
        LOG_INFO("[BikeSwap] About to call ChangeBikeWithMeshReload...");
        LOG_INFO("[BikeSwap]   bikeEntity = 0x" << std::hex << reinterpret_cast<uintptr_t>(bikeEntity));
        LOG_INFO("[BikeSwap]   bikeId = " << std::dec << (int)bikeId);
        LOG_INFO("[BikeSwap]   appearanceDataCopy = 0x" << std::hex << reinterpret_cast<uintptr_t>(appearanceDataCopy));

        // Call ChangeBikeWithMeshReload which handles the full bike swap sequence
        // This function internally calls:
        // 1. Copies appearance data from param to bike+0x9ec
        // 2. ResetBikeState
        // 3. Sets bike ID at +0x680
        // 4. CleanupSceneGeometry
        // 5. LoadBikeSettings
        // 6. LoadBikeMeshAndVisuals
        // 7. InitializeBikeAppearanceSlots
        // 8. SerializeBikeSceneObjects
        // 9. FinalizeRiderSetup
        bool success = CallChangeBikeWithMeshReload(bikeEntity, static_cast<uint8_t>(bikeId),
            appearanceDataCopy);

        if (success) {
            LOG_INFO("[BikeSwap] Successfully swapped to bike " << bikeId);
        }
        else {
            LOG_ERROR("[BikeSwap] Failed to swap to bike " << bikeId);
        }

        return success;
    }

    bool SwapToNextBike() {
        int currentId = GetCurrentBikeId();
        if (currentId < 0) {
            LOG_ERROR("[BikeSwap] Could not get current bike ID");
            return false;
        }

        int totalBikes = GetTotalBikeCount();
        int nextId = (currentId + 1) % totalBikes;

        return SwapToBike(nextId);
    }

    bool SwapToPreviousBike() {
        int currentId = GetCurrentBikeId();
        if (currentId < 0) {
            LOG_ERROR("[BikeSwap] Could not get current bike ID");
            return false;
        }

        int totalBikes = GetTotalBikeCount();
        int prevId = (currentId - 1 + totalBikes) % totalBikes;

        return SwapToBike(prevId);
    }

    std::string GetBikeName(int bikeId) {
        // Bike names based on Trials Fusion
        static const char* bikeNames[] = {
            "Pit Viper",    // 0 - Beginner
            "Squid",        // 1 - Easy
            "Roach",        // 2 - Medium  
            "Turtle",       // 3 - Medium
            "Jackal",       // 4 - Hard
            "Mantis",       // 5 - Hard
            "Donkey",       // 6 - Extreme
            "Rabbit",       // 7 - Unicorn/Special
            "Rhino"         // 8 - Helium/Special
        };

        if (bikeId < 0 || bikeId >= static_cast<int>(sizeof(bikeNames) / sizeof(bikeNames[0]))) {
            return "Unknown";
        }

        return bikeNames[bikeId];
    }

    std::string GetCurrentBikeName() {
        int currentId = GetCurrentBikeId();
        return GetBikeName(currentId);
    }

    void CheckHotkey() {
        if (!g_initialized) {
            return;
        }

        // Check for bike swap hotkeys
        // Set bike ID at raceManager+0x684 then call ReloadBikeFromSettings
        if (Keybindings::IsActionPressed(Keybindings::Action::SwapNextBike)) {
            int currentId = GetCurrentBikeId();
            int nextId = (currentId + 1) % GetTotalBikeCount();

            // Get the race manager from g_pGameManager + 0xdc
            if (g_globalStructPtr && *g_globalStructPtr) {
                uintptr_t gameManager = reinterpret_cast<uintptr_t>(*g_globalStructPtr);
                uintptr_t raceManager = *reinterpret_cast<uintptr_t*>(gameManager + 0xdc);

                if (raceManager) {
                    LOG_INFO("[BikeSwap] Swapping to bike " << nextId);

                    // Write the bike selection
                    *reinterpret_cast<uint8_t*>(raceManager + 0x684) = static_cast<uint8_t>(nextId);

                    // Call ReloadBikeFromSettings to load the new bike mesh
                    uintptr_t reloadRVA = GetReloadBikeFromSettingsRVA();

                    // Steam version not yet mapped - skip reload call
                    if (reloadRVA == 0) {
                        LOG_WARNING("[BikeSwap] Steam ReloadBikeFromSettings not mapped - hotkey bike swap disabled");
                        LOG_INFO("[BikeSwap] Bike ID updated at +0x684, but reload skipped - respawn may not work correctly");
                    }
                    else {
                        LOG_INFO("[BikeSwap] Calling ReloadBikeFromSettings...");
                        uintptr_t reloadFunc = g_baseAddress + reloadRVA;

                        __asm {
                            mov ecx, raceManager
                            call reloadFunc
                        }

                        // Now respawn to reset physics/camera
                        LOG_INFO("[BikeSwap] Triggering respawn...");
                        Respawn::RespawnAtCheckpoint();

                        LOG_INFO("[BikeSwap] Bike swap complete!");
                    }
                }
            }
        }

        if (Keybindings::IsActionPressed(Keybindings::Action::SwapPrevBike)) {
            int currentId = GetCurrentBikeId();
            int totalBikes = GetTotalBikeCount();
            int prevId = (currentId - 1 + totalBikes) % totalBikes;

            if (g_globalStructPtr && *g_globalStructPtr) {
                uintptr_t gameManager = reinterpret_cast<uintptr_t>(*g_globalStructPtr);
                uintptr_t raceManager = *reinterpret_cast<uintptr_t*>(gameManager + 0xdc);

                if (raceManager) {
                    LOG_INFO("[BikeSwap] Swapping to bike " << prevId);

                    // Write the bike selection
                    *reinterpret_cast<uint8_t*>(raceManager + 0x684) = static_cast<uint8_t>(prevId);

                    // Call ReloadBikeFromSettings to load the new bike mesh
                    uintptr_t reloadRVA = GetReloadBikeFromSettingsRVA();

                    // Steam version not yet mapped - skip reload call
                    if (reloadRVA == 0) {
                        LOG_WARNING("[BikeSwap] Steam ReloadBikeFromSettings not mapped - hotkey bike swap disabled");
                        LOG_INFO("[BikeSwap] Bike ID updated at +0x684, but reload skipped - respawn may not work correctly");
                    }
                    else {
                        LOG_INFO("[BikeSwap] Calling ReloadBikeFromSettings...");
                        uintptr_t reloadFunc = g_baseAddress + reloadRVA;

                        __asm {
                            mov ecx, raceManager
                            call reloadFunc
                        }

                        // Now respawn to reset physics/camera
                        LOG_INFO("[BikeSwap] Triggering respawn...");
                        Respawn::RespawnAtCheckpoint();

                        LOG_INFO("[BikeSwap] Bike swap complete!");
                    }
                }
            }
        }

        if (Keybindings::IsActionPressed(Keybindings::Action::DebugBikeInfo)) {
            DebugDumpBikeInfo();
        }
    }

    // ============================================================================
    // Hook-based Bike Swap (executes on game main thread)
    // ============================================================================

    static int g_pendingBikeId = -1;  // -1 means no pending swap
    static bool g_hookInstalled = false;

    // Original function pointer for HandleGameFrameUpdate
    typedef void(__fastcall* HandleGameFrameUpdateFunc)(void* param1);
    static HandleGameFrameUpdateFunc g_OriginalHandleGameFrameUpdate = nullptr;

    // Game frame update hook - this runs on the game's main thread
    // NOTE: This hook is currently not used - keeping for future reference
    void __fastcall Hook_HandleGameFrameUpdate(void* param1) {
        // Check if we have a pending bike swap
        if (g_pendingBikeId >= 0) {
            int targetBike = g_pendingBikeId;
            g_pendingBikeId = -1;  // Clear immediately to prevent re-entry

            void* bikeEntity = GetCurrentBikeEntity();
            if (bikeEntity && g_changeBikeWithMeshReload) {
                LOG_INFO("[BikeSwap] Executing bike swap on main thread to bike " << targetBike);

                // Get appearance data
                uintptr_t bikeAddr = reinterpret_cast<uintptr_t>(bikeEntity);
                uint8_t appearanceData[0x20];
                memcpy(appearanceData, reinterpret_cast<void*>(bikeAddr + 0x9ec), 0x20);

                // Call ChangeBikeWithMeshReload on the main thread
                // Note: SEH removed due to C++ object unwinding conflict
                CallChangeBikeWithMeshReload(bikeEntity, static_cast<uint8_t>(targetBike), appearanceData);
                LOG_INFO("[BikeSwap] Bike swap call completed");
            }
        }

        // Call original function
        if (g_OriginalHandleGameFrameUpdate) {
            g_OriginalHandleGameFrameUpdate(param1);
        }
    }

    // Queue a bike swap to be executed on the next frame
    bool QueueBikeSwapForMainThread(int bikeId) {
        if (!g_initialized) {
            LOG_ERROR("[BikeSwap] Not initialized");
            return false;
        }

        if (!g_hookInstalled) {
            LOG_ERROR("[BikeSwap] Frame update hook not installed");
            return false;
        }

        if (bikeId < 0 || bikeId >= GetTotalBikeCount()) {
            LOG_ERROR("[BikeSwap] Invalid bike ID: " << bikeId);
            return false;
        }

        g_pendingBikeId = bikeId;
        LOG_INFO("[BikeSwap] Queued swap to bike " << bikeId << " (" << GetBikeName(bikeId) << ") - will execute on next frame");
        return true;
    }

    // Simple bike swap - just change ID and respawn
    // This is safer because respawn handles the reload on the main thread
    bool SwapToBikeSimple(int bikeId) {
        if (!g_initialized) {
            LOG_ERROR("[BikeSwap] Not initialized");
            return false;
        }

        if (bikeId < 0 || bikeId >= GetTotalBikeCount()) {
            LOG_ERROR("[BikeSwap] Invalid bike ID: " << bikeId);
            return false;
        }

        void* bikeEntity = GetCurrentBikeEntity();
        if (!bikeEntity) {
            LOG_ERROR("[BikeSwap] Could not get current bike entity");
            return false;
        }

        LOG_INFO("[BikeSwap] Simple swap: Setting bike ID to " << bikeId << " and respawning");

        // Just set the bike ID at offset 0x684 (the "selected bike" field used by ReloadBikeFromSettings)
        // This is different from 0x680 which is the current bike ID
        *reinterpret_cast<uint8_t*>(reinterpret_cast<uintptr_t>(bikeEntity) + 0x684) = static_cast<uint8_t>(bikeId);

        LOG_INFO("[BikeSwap] Bike ID set at +0x684, now triggering respawn...");

        // Trigger a respawn - this should cause the game to reload the bike on its main thread
        bool respawnResult = Respawn::RespawnAtCheckpoint();

        if (respawnResult) {
            LOG_INFO("[BikeSwap] Respawn triggered successfully");
        }
        else {
            LOG_ERROR("[BikeSwap] Respawn failed");
        }

        return respawnResult;
    }

    // Manual step-by-step bike swap for debugging
    // NOTE: This crashes due to threading issues - kept for reference only
    bool SwapToBikeManual(int bikeId) {
        if (!g_initialized) {
            LOG_ERROR("[BikeSwap] Not initialized");
            return false;
        }

        if (bikeId < 0 || bikeId >= GetTotalBikeCount()) {
            LOG_ERROR("[BikeSwap] Invalid bike ID: " << bikeId);
            return false;
        }

        void* bikeEntity = GetCurrentBikeEntity();
        if (!bikeEntity) {
            LOG_ERROR("[BikeSwap] Could not get current bike entity");
            return false;
        }

        LOG_INFO("[BikeSwap] === MANUAL SWAP (reference only - crashes due to threading) ===");
        LOG_INFO("[BikeSwap] Bike entity: 0x" << std::hex << reinterpret_cast<uintptr_t>(bikeEntity));
        LOG_INFO("[BikeSwap] Target bike ID: " << std::dec << bikeId);
        LOG_INFO("[BikeSwap] This function is disabled - use raceManager+0x684 approach instead");

        return false;
    }

    void DebugDumpBikeInfo() {
        LOG_INFO("[BikeSwap] === DEBUG BIKE INFO ===");

        if (!g_initialized) {
            LOG_ERROR("[BikeSwap] Not initialized");
            return;
        }

        void* bikeEntity = GetCurrentBikeEntity();
        if (!bikeEntity) {
            LOG_ERROR("[BikeSwap] No bike entity found");
            return;
        }

        LOG_INFO("[BikeSwap] Bike entity ptr: 0x" << std::hex << reinterpret_cast<uintptr_t>(bikeEntity));

        int currentId = GetCurrentBikeId();
        LOG_INFO("[BikeSwap] Current bike ID: " << std::dec << currentId);
        LOG_INFO("[BikeSwap] Current bike name: " << GetCurrentBikeName());

        // Dump appearance data
        uintptr_t appearanceDataAddr = reinterpret_cast<uintptr_t>(bikeEntity) + 0x9ec;
        if (!IsBadReadPtr((void*)appearanceDataAddr, 0x20)) {
            LOG_INFO("[BikeSwap] Appearance data at bike+0x9ec:");
            uint8_t* data = reinterpret_cast<uint8_t*>(appearanceDataAddr);
            for (int i = 0; i < 0x20; i += 4) {
                LOG_INFO("[BikeSwap]   +0x" << std::hex << i << ": "
                    << std::hex << (int)data[i] << " "
                    << std::hex << (int)data[i + 1] << " "
                    << std::hex << (int)data[i + 2] << " "
                    << std::hex << (int)data[i + 3]);
            }
        }

        LOG_INFO("[BikeSwap] Total bikes: " << std::dec << GetTotalBikeCount());
        LOG_INFO("[BikeSwap] Available bikes:");
        for (int i = 0; i < GetTotalBikeCount(); i++) {
            LOG_INFO("[BikeSwap]   " << i << ": " << GetBikeName(i));
        }

        LOG_INFO("[BikeSwap] === END DEBUG ===");
    }
}