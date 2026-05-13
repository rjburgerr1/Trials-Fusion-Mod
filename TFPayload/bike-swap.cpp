#include "pch.h"
#include "bike-swap.h"
#include "logging.h"
#include "keybindings.h"
#include "base-address.h"
#include "respawn.h"
#include "gamemode.h"
#include <Windows.h>
#include <TlHelp32.h>
#include <cstring>
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
    static constexpr uintptr_t GET_BIKE_APPEARANCE_DATA_RVA_UPLAY = 0x3055a0;
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
    static constexpr uintptr_t GET_BIKE_APPEARANCE_DATA_RVA_STEAM = 0x304e70; // Estimated from nearby bike-function delta
    static constexpr uintptr_t GET_BIKE_DATA_BY_INDEX_RVA_STEAM = 0x19940;
    static constexpr uintptr_t GET_FIRST_ENTITY_FROM_LIST_RVA_STEAM = 0x25eb20;
    static constexpr uintptr_t HANDLE_GAME_FRAME_UPDATE_RVA_STEAM = 0x3b6140;

    // ============================================================================
    // Structure offsets (same for both versions)
    // ============================================================================

    // GameManager offsets
    static constexpr uintptr_t ENTITY_MANAGER_OFFSET = 0xdc;     // Entity list / game state manager
    static constexpr uintptr_t EDITOR_MANAGER_OFFSET = 0x104;    // Editor/track/session manager (ReloadBikeFromSettings 'this')
    static constexpr uintptr_t BIKE_DATA_MANAGER_OFFSET = 0x118;
    static constexpr uintptr_t BIKE_APPEARANCE_MANAGER_OFFSET = 0x120;

    // Bike list structure offsets
    static constexpr uintptr_t BIKE_LIST_STRUCT_OFFSET = 0x2f0;
    static constexpr uintptr_t BIKE_LIST_FIRST_PTR_OFFSET = 0x14;
    static constexpr uintptr_t BIKE_LIST_COUNT_OFFSET = 0x34;

    // Bike entity offsets
    static constexpr uintptr_t BIKE_ID_OFFSET = 0x680;

    // Editor manager offsets
    static constexpr uintptr_t SELECTED_BIKE_ID_OFFSET = 0x684;  // Byte: selected bike index (on editor manager)

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

    static uintptr_t GetBikeAppearanceDataRVA() {
        return BaseAddress::IsSteamVersion() ? GET_BIKE_APPEARANCE_DATA_RVA_STEAM : GET_BIKE_APPEARANCE_DATA_RVA_UPLAY;
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

    // ushort* __thiscall GetBikeAppearanceData(void* this, ushort* out, byte bikeId, byte variant)
    typedef uint16_t* (__thiscall* GetBikeAppearanceDataFunc)(void* thisPtr, uint16_t* outAppearance, uint8_t bikeId, uint8_t variant);

    // int __thiscall GetBikeDataByIndex(void* bikeDataManager, byte index)
    typedef int(__thiscall* GetBikeDataByIndexFunc)(void* bikeDataManager, uint8_t index);

    // int __fastcall GetFirstEntityFromList(int gameManager)
    typedef int(__fastcall* GetFirstEntityFromListFunc)(int gameManager);

    // void __fastcall ReloadBikeFromSettings(void* editorManager)
    typedef void(__fastcall* ReloadBikeFromSettingsFunc)(void* editorManager);

    // HandleGameFrameUpdate is __thiscall with 2 stack params:
    //   MOV ECX, [0x0174d8f4]   ; this in ECX
    //   PUSH ptr                ; param2 (pointer to local)
    //   PUSH int                ; param3 (bool/int)
    //   CALL HandleGameFrameUpdate
    //   RET 0x8                 ; callee cleans 8 bytes
    // For MinHook we model __thiscall as __fastcall with an unused EDX param.
    // The stack params follow after ECX(this) and EDX(unused).
    typedef void(__fastcall* HandleGameFrameUpdateFunc)(void* thisPtr, void* edx_unused, void* param2, int param3);

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
    static GetBikeAppearanceDataFunc g_getBikeAppearanceData = nullptr;
    static GetBikeDataByIndexFunc g_getBikeDataByIndex = nullptr;
    static GetFirstEntityFromListFunc g_getFirstEntityFromList = nullptr;
    static ReloadBikeFromSettingsFunc g_reloadBikeFromSettings = nullptr;

    // Hook-based swap state (must be declared before Initialize uses them)
    static volatile LONG g_pendingBikeId = -1;      // -1 = no pending swap (atomic via InterlockedExchange)
    static bool g_hookInstalled = false;
    static volatile bool g_swapInProgress = false;   // Guard against overlapping swaps
    static DWORD g_lastSwapTick = 0;                 // Cooldown timer
    static constexpr DWORD SWAP_COOLDOWN_MS = 8000;  // Minimum ms between swaps (give engine time to settle)
    enum class SwapStage {
        None,
        RespawnBeforeReload,
        ReloadBike
    };

    static volatile int g_stageDelayFrames = -1;       // Countdown frames before the next swap stage
    static volatile LONG g_activeSwapBikeId = -1;      // Target bike while staged swap is running
    static volatile LONG g_lastCompletedBikeId = -1;   // Last bike applied by a completed direct reload
    static volatile int g_settleValidationFrames = 0;  // Frames left before another swap is allowed
    static SwapStage g_swapStage = SwapStage::None;
    static constexpr int RESPAWN_DELAY_FRAMES = 5;     // Wait N frames before respawning
    static constexpr int RELOAD_DELAY_FRAMES = 15;     // Wait N frames after respawn before bike reload
    static constexpr int POST_SWAP_SETTLE_FRAMES = 90; // Require stable frames after completed reload

    // Original function pointer for HandleGameFrameUpdate
    static HandleGameFrameUpdateFunc g_OriginalHandleGameFrameUpdate = nullptr;

    // Forward declarations for hook and queue functions
    static void __fastcall Hook_HandleGameFrameUpdate(void* thisPtr, void* edx_unused, void* param2, int param3);
    static bool QueueBikeSwapForMainThread(int bikeId);
    static void* GetCurrentBikeEntity();
    int GetCurrentBikeId();

    static bool IsGameStateSafeForSwap() {
        if (!GameMode::IsGameModeActive() || !GameMode::IsPlaying()) {
            return false;
        }

        if (GameMode::IsInMultiplayerMode() || GameMode::IsWatchingReplay() || GameMode::IsRaceFinished()) {
            return false;
        }

        return Respawn::GetCheckpointCount() > 0;
    }

    static void ClearActiveSwap() {
        g_swapStage = SwapStage::None;
        g_stageDelayFrames = -1;
        InterlockedExchange(&g_activeSwapBikeId, -1);
        g_swapInProgress = false;
    }

    static bool IsPostSwapSettleComplete() {
        if (g_settleValidationFrames <= 0) {
            return true;
        }

        void* bikeEntity = GetCurrentBikeEntity();
        if (!bikeEntity || IsBadReadPtr(bikeEntity, 0xb20)) {
            return false;
        }

        int currentBikeId = GetCurrentBikeId();
        if (currentBikeId != g_lastCompletedBikeId) {
            return false;
        }

        g_settleValidationFrames--;
        if (g_settleValidationFrames == 0) {
            LOG_INFO("[BikeSwap] Post-swap settle validation complete");
        }

        return g_settleValidationFrames <= 0;
    }

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
    // Pointer Resolution Helpers
    // ============================================================================

    // Get the game manager value (the struct pointed to by g_pGameManager)
    static void* GetGameManagerStruct() {
        if (!g_globalStructPtr || IsBadReadPtr(g_globalStructPtr, sizeof(void*))) {
            return nullptr;
        }

        void* globalStruct = *g_globalStructPtr;
        if (!globalStruct || IsBadReadPtr(globalStruct, 0x200)) {
            return nullptr;
        }

        return globalStruct;
    }

    // Get *(g_pGameManager + 0xdc) - entity list / game state manager
    static void* GetEntityManager() {
        void* gameManager = GetGameManagerStruct();
        if (!gameManager) return nullptr;

        uintptr_t addr = reinterpret_cast<uintptr_t>(gameManager) + ENTITY_MANAGER_OFFSET;
        if (IsBadReadPtr((void*)addr, sizeof(void*))) return nullptr;

        void* entityMgr = *reinterpret_cast<void**>(addr);
        if (!entityMgr || IsBadReadPtr(entityMgr, 0x1000)) return nullptr;

        return entityMgr;
    }

    // Get *(g_pGameManager + 0x104) - editor/track/session manager
    // This is the correct 'this' pointer for ReloadBikeFromSettings
    static void* GetEditorManager() {
        void* gameManager = GetGameManagerStruct();
        if (!gameManager) return nullptr;

        uintptr_t addr = reinterpret_cast<uintptr_t>(gameManager) + EDITOR_MANAGER_OFFSET;
        if (IsBadReadPtr((void*)addr, sizeof(void*))) return nullptr;

        void* editorMgr = *reinterpret_cast<void**>(addr);
        if (!editorMgr || IsBadReadPtr(editorMgr, 0x800)) return nullptr;

        return editorMgr;
    }

    static void* GetBikeDataManager() {
        void* gameManager = GetGameManagerStruct();
        if (!gameManager) return nullptr;

        uintptr_t addr = reinterpret_cast<uintptr_t>(gameManager) + BIKE_DATA_MANAGER_OFFSET;
        if (IsBadReadPtr((void*)addr, sizeof(void*))) return nullptr;

        void* bikeDataMgr = *reinterpret_cast<void**>(addr);
        if (!bikeDataMgr || IsBadReadPtr(bikeDataMgr, 0x100)) return nullptr;

        return bikeDataMgr;
    }

    static void* GetBikeAppearanceManager() {
        void* gameManager = GetGameManagerStruct();
        if (!gameManager) return nullptr;

        uintptr_t addr = reinterpret_cast<uintptr_t>(gameManager) + BIKE_APPEARANCE_MANAGER_OFFSET;
        if (IsBadReadPtr((void*)addr, sizeof(void*))) return nullptr;

        void* appearanceManagerHolder = *reinterpret_cast<void**>(addr);
        if (!appearanceManagerHolder || IsBadReadPtr(appearanceManagerHolder, 0x14)) return nullptr;

        uintptr_t appearanceManagerAddr = reinterpret_cast<uintptr_t>(appearanceManagerHolder) + 0x10;
        if (IsBadReadPtr((void*)appearanceManagerAddr, sizeof(void*))) return nullptr;

        void* appearanceManager = *reinterpret_cast<void**>(appearanceManagerAddr);
        if (!appearanceManager || IsBadReadPtr(appearanceManager, 0x1200)) return nullptr;

        return appearanceManager;
    }

    static void* GetCurrentBikeEntity() {
        // Use the GetBikePointer from Respawn module since it does the same thing
        return Respawn::GetBikePointer();
    }

    // ============================================================================
    // SEH-safe Wrapper Functions
    // These use a two-layer approach: inner function does SEH, outer does logging
    // ============================================================================

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

    static bool CallGetBikeAppearanceData_Inner(void* appearanceManager, uint16_t* outAppearance, uint8_t bikeId, DWORD* exceptionCode) {
        *exceptionCode = 0;
        __try {
            g_getBikeAppearanceData(appearanceManager, outAppearance, bikeId, 0);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            *exceptionCode = GetExceptionCode();
            return false;
        }
    }

    static bool BuildBikeAppearanceData(uint8_t bikeId, uint16_t outAppearance[16]) {
        memset(outAppearance, 0, sizeof(uint16_t) * 16);

        void* appearanceManager = GetBikeAppearanceManager();
        if (!appearanceManager) {
            LOG_ERROR("[BikeSwap] Could not get bike appearance manager");
            return false;
        }

        if (!g_getBikeAppearanceData) {
            LOG_ERROR("[BikeSwap] GetBikeAppearanceData pointer is null");
            return false;
        }

        DWORD exceptionCode = 0;
        bool result = CallGetBikeAppearanceData_Inner(appearanceManager, outAppearance, bikeId, &exceptionCode);
        if (!result) {
            LOG_ERROR("[BikeSwap] Exception in GetBikeAppearanceData: 0x" << std::hex << exceptionCode);
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

    // Inner SEH wrapper for ReloadBikeFromSettings (no C++ objects allowed)
    static bool CallReloadBikeFromSettings_Inner(void* editorManager, DWORD* exceptionCode) {
        *exceptionCode = 0;
        __try {
            g_reloadBikeFromSettings(editorManager);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            *exceptionCode = GetExceptionCode();
            return false;
        }
    }

    // ============================================================================
    // Hook-based Bike Swap (executes on game main thread)
    // ============================================================================

    // Game frame update hook - runs on the game's main thread
    // Must match real signature: __thiscall(this, ptr, int) with RET 0x8
    // Modeled as __fastcall(ECX=this, EDX=unused, stack_param1, stack_param2)
    static void __fastcall Hook_HandleGameFrameUpdate(void* thisPtr, void* edx_unused, void* param2, int param3) {
        // Call original function FIRST so the game frame is in a consistent state
        if (g_OriginalHandleGameFrameUpdate) {
            g_OriginalHandleGameFrameUpdate(thisPtr, edx_unused, param2, param3);
        }

        if (g_swapStage == SwapStage::None && g_settleValidationFrames > 0) {
            IsPostSwapSettleComplete();
        }

        // Run the staged swap after normal frame work. The order is:
        // selection write -> respawn to settle physics -> direct bike reload -> immediate respawn.
        if (g_swapStage != SwapStage::None) {
            if (g_stageDelayFrames > 0) {
                g_stageDelayFrames--;
            }
            else if (g_stageDelayFrames == 0) {
                g_stageDelayFrames = -1;

                LONG targetBike = g_activeSwapBikeId;
                if (targetBike < 0) {
                    LOG_WARNING("[BikeSwap] Active swap has no target bike; aborting");
                    ClearActiveSwap();
                    return;
                }

                if (!IsGameStateSafeForSwap()) {
                    LOG_WARNING("[BikeSwap] Staged swap skipped because game state changed");
                    ClearActiveSwap();
                    return;
                }

                if (g_swapStage == SwapStage::RespawnBeforeReload) {
                    LOG_INFO("[BikeSwap] Pre-reload respawn executing...");
                    if (!Respawn::RespawnAtCheckpoint()) {
                        LOG_ERROR("[BikeSwap] Pre-reload respawn failed; aborting swap");
                        ClearActiveSwap();
                        return;
                    }

                    g_swapStage = SwapStage::ReloadBike;
                    g_stageDelayFrames = RELOAD_DELAY_FRAMES;
                    LOG_INFO("[BikeSwap] Pre-reload respawn complete; bike reload scheduled");
                    return;
                }

                if (g_swapStage == SwapStage::ReloadBike) {
                    void* bikeEntity = GetCurrentBikeEntity();
                    if (!bikeEntity) {
                        LOG_ERROR("[BikeSwap] Could not get bike entity during direct bike reload");
                        ClearActiveSwap();
                        return;
                    }

                    if (IsBadReadPtr(bikeEntity, 0xb20)) {
                        LOG_ERROR("[BikeSwap] Bike entity became unreadable before direct reload");
                        ClearActiveSwap();
                        return;
                    }

                    uint16_t appearanceData[16] = {};
                    if (!BuildBikeAppearanceData(static_cast<uint8_t>(targetBike), appearanceData)) {
                        ClearActiveSwap();
                        return;
                    }

                    LOG_INFO("[BikeSwap] Applying direct bike reload for bike " << std::dec << targetBike);
                    if (!CallChangeBikeWithMeshReload(bikeEntity, static_cast<uint8_t>(targetBike), appearanceData)) {
                        ClearActiveSwap();
                        return;
                    }

                    LOG_INFO("[BikeSwap] Direct bike reload completed; current bike ID is "
                        << std::dec << GetCurrentBikeId());

                    LOG_INFO("[BikeSwap] Immediate post-reload respawn executing...");
                    if (!Respawn::RespawnAtCheckpoint()) {
                        LOG_ERROR("[BikeSwap] Immediate post-reload respawn failed");
                        ClearActiveSwap();
                        return;
                    }

                    InterlockedExchange(&g_lastCompletedBikeId, targetBike);
                    g_settleValidationFrames = POST_SWAP_SETTLE_FRAMES;
                    ClearActiveSwap();
                    LOG_INFO("[BikeSwap] Bike swap + respawn complete");
                    return;
                }
            }
        }

        // Check if we have a pending bike swap
        LONG targetBike = InterlockedExchange(&g_pendingBikeId, -1);
        if (targetBike >= 0) {
            g_swapInProgress = true;

            if (!IsGameStateSafeForSwap()) {
                LOG_WARNING("[BikeSwap] Pending swap skipped because game state is no longer safe");
                ClearActiveSwap();
                return;
            }

            void* editorManager = GetEditorManager();
            if (!editorManager) {
                LOG_ERROR("[BikeSwap] Invalid editor manager during main-thread swap");
                ClearActiveSwap();
            }
            else {
                uintptr_t bikeSelAddr = reinterpret_cast<uintptr_t>(editorManager) + SELECTED_BIKE_ID_OFFSET;
                if (IsBadWritePtr((void*)bikeSelAddr, sizeof(uint8_t))) {
                    LOG_ERROR("[BikeSwap] Cannot write editor manager + 0x684");
                    ClearActiveSwap();
                }
                else {
                    LOG_INFO("[BikeSwap] Main thread: swapping to bike " << (int)targetBike
                        << " (" << GetBikeName((int)targetBike) << ")");

                    // Write the selected bike ID to editorManager + 0x684
                    *reinterpret_cast<uint8_t*>(bikeSelAddr) = static_cast<uint8_t>(targetBike);

                    InterlockedExchange(&g_activeSwapBikeId, targetBike);
                    g_swapStage = SwapStage::RespawnBeforeReload;
                    g_stageDelayFrames = RESPAWN_DELAY_FRAMES;
                    LOG_INFO("[BikeSwap] Selected bike updated; staged swap scheduled");
                }
            }
        }
    }

    // Queue a bike swap to be executed on the next game frame (thread-safe)
    static bool QueueBikeSwapForMainThread(int bikeId) {
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

        if (!IsGameStateSafeForSwap()) {
            LOG_WARNING("[BikeSwap] Swap unavailable in current game state");
            return false;
        }

        if (!IsPostSwapSettleComplete()) {
            LOG_WARNING("[BikeSwap] Previous swap is still settling, ignoring request");
            return false;
        }

        // Check cooldown
        DWORD now = GetTickCount();
        if (now - g_lastSwapTick < SWAP_COOLDOWN_MS) {
            LOG_WARNING("[BikeSwap] Swap cooldown active, ignoring request");
            return false;
        }

        // Check if a swap is already in progress (including pending respawn)
        if (g_swapInProgress) {
            LOG_WARNING("[BikeSwap] Swap already in progress, ignoring request");
            return false;
        }

        g_lastSwapTick = now;
        InterlockedExchange(&g_pendingBikeId, bikeId);
        LOG_INFO("[BikeSwap] Queued swap to bike " << bikeId << " (" << GetBikeName(bikeId) << ") - will execute on next frame");
        return true;
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
        g_getBikeAppearanceData = reinterpret_cast<GetBikeAppearanceDataFunc>(
            baseAddress + GetBikeAppearanceDataRVA());
        g_getBikeDataByIndex = reinterpret_cast<GetBikeDataByIndexFunc>(
            baseAddress + GetBikeDataByIndexRVA());
        g_getFirstEntityFromList = reinterpret_cast<GetFirstEntityFromListFunc>(
            baseAddress + GetFirstEntityFromListRVA());
        g_reloadBikeFromSettings = reinterpret_cast<ReloadBikeFromSettingsFunc>(
            baseAddress + GetReloadBikeFromSettingsRVA());

        if (IsBadReadPtr(g_globalStructPtr, sizeof(void*))) {
            LOG_ERROR("[BikeSwap] Invalid global struct pointer");
            return false;
        }

        g_initialized = true;

        // Install the frame update hook so bike swaps run on the game's main thread
        if (!g_hookInstalled && GetReloadBikeFromSettingsRVA() != 0) {
            uintptr_t frameUpdateAddr = baseAddress + GetHandleGameFrameUpdateRVA();
            MH_STATUS hookStatus = MH_CreateHook(
                reinterpret_cast<LPVOID>(frameUpdateAddr),
                reinterpret_cast<LPVOID>(&Hook_HandleGameFrameUpdate),
                reinterpret_cast<LPVOID*>(&g_OriginalHandleGameFrameUpdate));

            if (hookStatus == MH_OK) {
                hookStatus = MH_EnableHook(reinterpret_cast<LPVOID>(frameUpdateAddr));
                if (hookStatus == MH_OK) {
                    g_hookInstalled = true;
                    LOG_INFO("[BikeSwap] Frame update hook installed - bike swaps will run on main thread");
                }
                else {
                    LOG_ERROR("[BikeSwap] Failed to enable frame hook: " << MH_StatusToString(hookStatus));
                }
            }
            else {
                LOG_ERROR("[BikeSwap] Failed to create frame hook: " << MH_StatusToString(hookStatus));
            }
        }
        else if (GetReloadBikeFromSettingsRVA() == 0) {
            LOG_WARNING("[BikeSwap] ReloadBikeFromSettings RVA is 0 - hook-based swap unavailable");
        }

        LOG_INFO("[BikeSwap] Initialized successfully (hook: " << (g_hookInstalled ? "active" : "inactive") << ")");

        return true;
    }

    void Shutdown() {
        if (!g_initialized) {
            return;
        }

        // Disable the frame update hook
        if (g_hookInstalled) {
            uintptr_t frameUpdateAddr = g_baseAddress + GetHandleGameFrameUpdateRVA();
            MH_DisableHook(reinterpret_cast<LPVOID>(frameUpdateAddr));
            MH_RemoveHook(reinterpret_cast<LPVOID>(frameUpdateAddr));
            g_hookInstalled = false;
            g_OriginalHandleGameFrameUpdate = nullptr;
            LOG_VERBOSE("[BikeSwap] Frame update hook removed");
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
        g_getBikeAppearanceData = nullptr;
        g_getBikeDataByIndex = nullptr;
        g_getFirstEntityFromList = nullptr;
        g_reloadBikeFromSettings = nullptr;
        g_pendingBikeId = -1;
        g_lastCompletedBikeId = -1;
        g_settleValidationFrames = 0;
        ClearActiveSwap();

        LOG_VERBOSE("[BikeSwap] Shutdown complete");
    }

    bool IsSwapAvailable() {
        if (!g_initialized) {
            return false;
        }

        if (!IsGameStateSafeForSwap()) {
            return false;
        }

        void* bikeEntity = GetCurrentBikeEntity();
        if (!bikeEntity) {
            return false;
        }

        int bikeId = GetCurrentBikeId();
        return bikeId >= 0 && bikeId < GetTotalBikeCount();
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

        if (!IsGameStateSafeForSwap()) {
            LOG_WARNING("[BikeSwap] Swap unavailable in current game state");
            return false;
        }

        int currentBikeId = GetCurrentBikeId();
        if (currentBikeId == bikeId) {
            LOG_VERBOSE("[BikeSwap] Already on bike " << bikeId);
            return true;
        }

        // Prefer the hook-based approach (main thread safe)
        if (g_hookInstalled) {
            return QueueBikeSwapForMainThread(bikeId);
        }

        LOG_WARNING("[BikeSwap] Frame hook not installed - refusing unsafe direct bike reload");
        return false;
    }

    bool SwapToNextBike() {
        if (!IsSwapAvailable()) {
            LOG_WARNING("[BikeSwap] Swap unavailable in current game state");
            return false;
        }

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
        if (!IsSwapAvailable()) {
            LOG_WARNING("[BikeSwap] Swap unavailable in current game state");
            return false;
        }

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

    bool GetCooldownStatus(float* secondsRemaining, float* progress01, std::string* statusText) {
        if (secondsRemaining) {
            *secondsRemaining = 0.0f;
        }
        if (progress01) {
            *progress01 = 1.0f;
        }
        if (statusText) {
            statusText->clear();
        }

        if (!g_initialized) {
            return false;
        }

        const DWORD now = GetTickCount();
        float cooldownSeconds = 0.0f;
        float cooldownProgress = 1.0f;

        if (g_lastSwapTick != 0) {
            DWORD elapsedMs = now - g_lastSwapTick;
            if (elapsedMs < SWAP_COOLDOWN_MS) {
                DWORD remainingMs = SWAP_COOLDOWN_MS - elapsedMs;
                cooldownSeconds = static_cast<float>(remainingMs) / 1000.0f;
                cooldownProgress = static_cast<float>(elapsedMs) / static_cast<float>(SWAP_COOLDOWN_MS);
            }
        }

        // Approximate frame-based settle delay as seconds for display only.
        const float settleSeconds = g_settleValidationFrames > 0
            ? static_cast<float>(g_settleValidationFrames) / 60.0f
            : 0.0f;
        const float settleProgress = g_settleValidationFrames > 0
            ? 1.0f - (static_cast<float>(g_settleValidationFrames) / static_cast<float>(POST_SWAP_SETTLE_FRAMES))
            : 1.0f;

        const bool stagedSwapActive = g_swapInProgress || g_swapStage != SwapStage::None;
        float stageSeconds = 0.0f;
        float stageProgress = 1.0f;
        if (stagedSwapActive && g_stageDelayFrames > 0) {
            int totalFrames = g_swapStage == SwapStage::ReloadBike ? RELOAD_DELAY_FRAMES : RESPAWN_DELAY_FRAMES;
            stageSeconds = static_cast<float>(g_stageDelayFrames) / 60.0f;
            stageProgress = 1.0f - (static_cast<float>(g_stageDelayFrames) / static_cast<float>(totalFrames));
        }

        float remaining = cooldownSeconds;
        float progress = cooldownProgress;
        std::string text = "Bike swap cooldown";

        if (stagedSwapActive) {
            remaining = stageSeconds;
            progress = stageProgress;
            text = g_swapStage == SwapStage::ReloadBike ? "Bike swap applying" : "Bike swap respawning";
        }
        else if (settleSeconds > remaining) {
            remaining = settleSeconds;
            progress = settleProgress;
            text = "Bike swap settling";
        }

        if (remaining <= 0.0f && !stagedSwapActive) {
            return false;
        }

        if (secondsRemaining) {
            *secondsRemaining = remaining;
        }
        if (progress01) {
            if (progress < 0.0f) progress = 0.0f;
            if (progress > 1.0f) progress = 1.0f;
            *progress01 = progress;
        }
        if (statusText) {
            *statusText = text;
        }

        return true;
    }

    void CheckHotkey() {
        if (!g_initialized) {
            return;
        }

        // Check for bike swap hotkeys - queue swaps for main thread execution
        if (Keybindings::IsActionPressed(Keybindings::Action::SwapNextBike)) {
            SwapToNextBike();
        }

        if (Keybindings::IsActionPressed(Keybindings::Action::SwapPrevBike)) {
            SwapToPreviousBike();
        }

        if (Keybindings::IsActionPressed(Keybindings::Action::DebugBikeInfo)) {
            DebugDumpBikeInfo();
        }
    }

    // Simple bike swap - kept as a compatibility wrapper for the safer queued path.
    bool SwapToBikeSimple(int bikeId) {
        return SwapToBike(bikeId);
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
        LOG_INFO("[BikeSwap] This function is disabled - use hook-based swap instead");

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

        // Show editor manager info
        void* editorMgr = GetEditorManager();
        if (editorMgr) {
            LOG_INFO("[BikeSwap] Editor manager ptr: 0x" << std::hex << reinterpret_cast<uintptr_t>(editorMgr));
            uintptr_t selBikeAddr = reinterpret_cast<uintptr_t>(editorMgr) + SELECTED_BIKE_ID_OFFSET;
            if (!IsBadReadPtr((void*)selBikeAddr, sizeof(uint8_t))) {
                LOG_INFO("[BikeSwap] Selected bike at editorMgr+0x684: " << std::dec
                    << (int)*reinterpret_cast<uint8_t*>(selBikeAddr));
            }
        }
        else {
            LOG_WARNING("[BikeSwap] Editor manager is null");
        }

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

        LOG_INFO("[BikeSwap] Hook installed: " << (g_hookInstalled ? "yes" : "no"));
        LOG_INFO("[BikeSwap] Swap in progress: " << (g_swapInProgress ? "yes" : "no"));
        LOG_INFO("[BikeSwap] Swap stage: " << static_cast<int>(g_swapStage));
        LOG_INFO("[BikeSwap] Stage delay frames: " << g_stageDelayFrames);
        LOG_INFO("[BikeSwap] Active swap bike ID: " << g_activeSwapBikeId);
        LOG_INFO("[BikeSwap] Last completed bike ID: " << g_lastCompletedBikeId);
        LOG_INFO("[BikeSwap] Settle validation frames: " << g_settleValidationFrames);

        LOG_INFO("[BikeSwap] === END DEBUG ===");
    }
}
