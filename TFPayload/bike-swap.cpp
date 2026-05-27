#include "pch.h"
#include "bike-swap.h"
#include "logging.h"
#include "keybindings.h"
#include "base-address.h"
#include "respawn.h"
#include "gamemode.h"
#include "fmod.h"
#include "gear-customization.h"
#include "save-states.h"
#include <Windows.h>
#include <TlHelp32.h>
#include <cstring>
#include <mutex>
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
    // Verified from uplay-to-steam.csv:
    // Uplay GetBikeAppearanceData 0x00a055a0 -> Steam 0x00444830
    // Steam RVA = 0x00444830 - 0x00140000 = 0x00304830
    static constexpr uintptr_t GET_BIKE_APPEARANCE_DATA_RVA_STEAM = 0x304830;
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
    static constexpr int MAX_RUNTIME_SAFE_BIKE_ID = 6;

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
    static volatile LONG g_pendingAppearanceTintRefresh = 0;
    static uint16_t g_pendingAppearanceTintData[16] = {};
    static volatile LONG g_pendingAppearanceReload = 0;
    static uint16_t g_pendingAppearanceData[16] = {};
    static volatile LONG g_pendingVisualOnlyReload = 0;
    static uint16_t g_pendingVisualOnlyData[16] = {};
    static std::mutex g_pendingAppearanceMutex;
    static bool g_hookInstalled = false;
    static volatile LONG g_swapInProgress = 0;       // Guard against overlapping swaps
    enum class SwapStage {
        None,
        RespawnBeforeReload,
        ReloadBike
    };

    static volatile int g_stageDelayFrames = -1;       // Countdown frames before the next swap stage
    static volatile LONG g_activeSwapBikeId = -1;      // Target bike while staged swap is running
    static bool g_activeAppearanceReload = false;
    static bool g_activeVisualOnlyReload = false;
    static uint16_t g_activeAppearanceData[16] = {};
    static bool g_fmodPausedForSwap = false;
    static SwapStage g_swapStage = SwapStage::None;
    static constexpr int RESPAWN_DELAY_FRAMES = 5;     // Wait N frames before respawning
    static constexpr int RELOAD_DELAY_FRAMES = 15;     // Wait N frames after respawn before bike reload

    // Original function pointer for HandleGameFrameUpdate
    static HandleGameFrameUpdateFunc g_OriginalHandleGameFrameUpdate = nullptr;

    static void LogRelativeCallTargets(const char* label, uintptr_t functionAddress, size_t byteCount) {
        if (!label || functionAddress == 0 || byteCount < 5
            || IsBadReadPtr(reinterpret_cast<void*>(functionAddress), byteCount)) {
            return;
        }

        LOG_INFO("[BikeSwap] Steam probe " << label << " @ 0x" << std::hex << functionAddress);
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(functionAddress);
        for (size_t i = 0; i + 5 <= byteCount; ++i) {
            if (bytes[i] != 0xe8) {
                continue;
            }

            const int32_t relative = *reinterpret_cast<const int32_t*>(bytes + i + 1);
            const uintptr_t target = functionAddress + i + 5 + relative;
            LOG_INFO("[BikeSwap]   call +0x" << std::hex << i
                << " -> abs=0x" << target
                << " rva=0x" << (target - g_baseAddress));
        }
        LOG_INFO(std::dec);
    }

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
        g_activeAppearanceReload = false;
        g_activeVisualOnlyReload = false;
        InterlockedExchange(&g_swapInProgress, 0);
        if (g_fmodPausedForSwap) {
            Fmod::SetRuntimeUpdatesPaused(false);
            g_fmodPausedForSwap = false;
        }
    }

    static void PauseFmodForSwap() {
        if (!g_fmodPausedForSwap) {
            g_fmodPausedForSwap = true;
            Fmod::SetRuntimeUpdatesPaused(true);
        }
    }

    static bool HasPendingReloadWork() {
        return InterlockedCompareExchange(&g_swapInProgress, 0, 0) != 0
            || g_swapStage != SwapStage::None
            || InterlockedCompareExchange(&g_pendingAppearanceReload, 0, 0) != 0
            || InterlockedCompareExchange(&g_pendingVisualOnlyReload, 0, 0) != 0;
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

    static void* GetBikeVisualCatalog() {
        void* gameManager = GetGameManagerStruct();
        if (!gameManager) return nullptr;

        uintptr_t addr = reinterpret_cast<uintptr_t>(gameManager) + 0x114;
        if (IsBadReadPtr((void*)addr, sizeof(void*))) return nullptr;

        void* catalog = *reinterpret_cast<void**>(addr);
        if (!catalog || IsBadReadPtr(catalog, 0x40)) return nullptr;
        return catalog;
    }


    static void* GetCurrentBikeEntity() {
        // Use the GetBikePointer from Respawn module since it does the same thing
        return Respawn::GetBikePointer();
    }

    static bool WriteAppearanceDataToBike(void* bikeEntity, const uint16_t appearanceData[16]) {
        if (!bikeEntity || !appearanceData) {
            return false;
        }

        uintptr_t appearanceDataAddr = reinterpret_cast<uintptr_t>(bikeEntity) + 0x9ec;
        if (IsBadWritePtr(reinterpret_cast<void*>(appearanceDataAddr), sizeof(uint16_t) * 16)) {
            return false;
        }

        __try {
            memcpy(reinterpret_cast<void*>(appearanceDataAddr), appearanceData, sizeof(uint16_t) * 16);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    // ============================================================================
    // SEH-safe Wrapper Functions
    // These use a two-layer approach: inner function does SEH, outer does logging
    // ============================================================================

    static bool CallReloadBikeFromSettings_Inner(void* editorManager, DWORD* exceptionCode);

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

    static bool CallChangeBikeWithMeshReloadAtomic(void* bikeEntity, uint8_t bikeId, void* appearanceData) {
        std::vector<HANDLE> suspendedThreads = SuspendOtherThreads();
        DWORD exceptionCode = 0;
        bool result = CallChangeBikeWithMeshReload_Inner(bikeEntity, bikeId, appearanceData, &exceptionCode);
        ResumeThreads(suspendedThreads);
        if (!result) {
            LOG_ERROR("[BikeSwap] Exception in atomic ChangeBikeWithMeshReload: 0x" << std::hex << exceptionCode);
        }
        return result;
    }

    static bool CallChangeBikeWithMeshReloadAndRespawnAtomic(void* bikeEntity, uint8_t bikeId, void* appearanceData) {
        std::vector<HANDLE> suspendedThreads = SuspendOtherThreads();
        DWORD exceptionCode = 0;
        bool result = CallChangeBikeWithMeshReload_Inner(bikeEntity, bikeId, appearanceData, &exceptionCode);
        if (result) {
            LOG_INFO("[BikeSwap] Atomic bike reload completed; stabilizing with checkpoint respawn");
            result = Respawn::RespawnAtCheckpoint();
        }
        ResumeThreads(suspendedThreads);

        if (exceptionCode != 0) {
            LOG_ERROR("[BikeSwap] Exception in atomic ChangeBikeWithMeshReload: 0x" << std::hex << exceptionCode);
        }
        else if (!result) {
            LOG_ERROR("[BikeSwap] Atomic post-reload respawn failed");
        }
        return result;
    }

    static bool CallReloadBikeFromSettingsAtomic(void* editorManager) {
        std::vector<HANDLE> suspendedThreads = SuspendOtherThreads();
        DWORD exceptionCode = 0;
        bool result = CallReloadBikeFromSettings_Inner(editorManager, &exceptionCode);
        if (result) {
            LOG_INFO("[BikeSwap] Settings reload completed; stabilizing with checkpoint respawn");
            result = Respawn::RespawnAtCheckpoint();
        }
        ResumeThreads(suspendedThreads);

        if (exceptionCode != 0) {
            LOG_ERROR("[BikeSwap] Exception in atomic ReloadBikeFromSettings: 0x" << std::hex << exceptionCode);
        }
        else if (!result) {
            LOG_ERROR("[BikeSwap] Atomic settings-reload respawn failed");
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

        // Run the staged swap after normal frame work. The order is:
        // selection write -> checkpoint respawn -> bike reload.
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
                    if (!g_activeAppearanceReload && !g_activeVisualOnlyReload) {
                        void* editorManager = GetEditorManager();
                        if (!editorManager) {
                            LOG_ERROR("[BikeSwap] Could not get editor manager during settings bike reload");
                            ClearActiveSwap();
                            return;
                        }

                        if (IsBadReadPtr(editorManager, 0x800)) {
                            LOG_ERROR("[BikeSwap] Editor manager became unreadable before settings reload");
                            ClearActiveSwap();
                            return;
                        }

                        Fmod::InvalidateCachedPointers(5000);
                        LOG_INFO("[BikeSwap] Applying settings-driven bike reload for bike "
                            << std::dec << targetBike);
                        if (!CallReloadBikeFromSettingsAtomic(editorManager)) {
                            Fmod::InvalidateCachedPointers(5000);
                            ClearActiveSwap();
                            return;
                        }

                        Fmod::InvalidateCachedPointers(5000);
                        LOG_INFO("[BikeSwap] Settings bike reload completed; current bike ID is "
                            << std::dec << GetCurrentBikeId());

                        ClearActiveSwap();
                        LOG_INFO("[BikeSwap] Bike swap + settings reload complete");
                        return;
                    }

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
                    if (g_activeAppearanceReload || g_activeVisualOnlyReload) {
                        memcpy(appearanceData, g_activeAppearanceData, sizeof(appearanceData));
                    }
                    else if (!BuildBikeAppearanceData(static_cast<uint8_t>(targetBike), appearanceData)) {
                        ClearActiveSwap();
                        return;
                    }

                    if (!GearCustomization::ApplyPendingHiddenObjectPayloadPatches()) {
                        LOG_WARNING("[BikeSwap] Deferred gear customization payload patch failed; aborting reload");
                        ClearActiveSwap();
                        return;
                    }

                    Fmod::InvalidateCachedPointers(5000);
                    LOG_INFO("[BikeSwap] Applying direct "
                        << (g_activeVisualOnlyReload ? "visual-only" : (g_activeAppearanceReload ? "appearance" : "bike"))
                        << " reload for bike " << std::dec << targetBike);
                    bool reloadSucceeded = false;
                    if (g_activeVisualOnlyReload) {
                        reloadSucceeded = WriteAppearanceDataToBike(bikeEntity, appearanceData)
                            && CallCleanupSceneGeometry(bikeEntity, 1)
                            && CallLoadBikeMeshAndVisuals(bikeEntity)
                            && CallInitBikeAppearanceSlots(bikeEntity, 0)
                            && CallSerializeBikeSceneObjects(bikeEntity);
                        if (!reloadSucceeded) {
                            Fmod::InvalidateCachedPointers(5000);
                            GearCustomization::RestoreActiveHiddenObjectPayloadPatches();
                            ClearActiveSwap();
                            return;
                        }
                    }
                    else {
                        reloadSucceeded = CallChangeBikeWithMeshReloadAndRespawnAtomic(bikeEntity, static_cast<uint8_t>(targetBike), appearanceData);
                        if (!reloadSucceeded) {
                            Fmod::InvalidateCachedPointers(5000);
                            GearCustomization::RestoreActiveHiddenObjectPayloadPatches();
                            ClearActiveSwap();
                            return;
                        }
                    }

                    Fmod::InvalidateCachedPointers(5000);
                    LOG_INFO("[BikeSwap] Direct bike reload completed; current bike ID is "
                        << std::dec << GetCurrentBikeId());

                    const bool completedVisualOnlyReload = g_activeVisualOnlyReload;
                    const bool completedAppearanceReload = g_activeAppearanceReload;
                    GearCustomization::RestoreActiveHiddenObjectPayloadPatches();

                    if (completedVisualOnlyReload) {
                        ClearActiveSwap();
                        LOG_INFO("[BikeSwap] Visual-only reload complete");
                        return;
                    }

                    ClearActiveSwap();
                    LOG_INFO("[BikeSwap] "
                        << (completedAppearanceReload ? "Appearance reload" : "Bike swap")
                        << " + atomic respawn complete");
                    return;
                }
            }
        }

        if (g_swapStage == SwapStage::None) {
            SaveStates::ProcessPendingMainThread();
        }

        GearCustomization::ProcessPendingMainThread();

        if (InterlockedExchange(&g_pendingAppearanceTintRefresh, 0) != 0) {
            uint16_t appearanceData[16] = {};
            {
                std::lock_guard<std::mutex> lock(g_pendingAppearanceMutex);
                memcpy(appearanceData, g_pendingAppearanceTintData, sizeof(appearanceData));
            }

            void* bikeEntity = GetCurrentBikeEntity();
            if (!bikeEntity || IsBadReadPtr(bikeEntity, 0xb20)) {
                LOG_ERROR("[BikeSwap] Tint refresh skipped because the current bike is unavailable");
                return;
            }

            if (!WriteAppearanceDataToBike(bikeEntity, appearanceData)) {
                LOG_ERROR("[BikeSwap] Tint refresh could not write live appearance data");
                return;
            }

            LOG_INFO("[BikeSwap] Reapplying live appearance tint slots without full reload");
            if (!CallInitBikeAppearanceSlots(bikeEntity, 0)) {
                return;
            }

            LOG_INFO("[BikeSwap] Tint-slot refresh complete");
            return;
        }

        GearCustomization::ProcessPendingMainThread();

        if (InterlockedExchange(&g_pendingAppearanceReload, 0) != 0) {
            if (InterlockedCompareExchange(&g_swapInProgress, 1, 0) != 0) {
                LOG_WARNING("[BikeSwap] Reload already in progress, ignoring appearance reload");
                return;
            }
            PauseFmodForSwap();

            if (!IsGameStateSafeForSwap()) {
                LOG_WARNING("[BikeSwap] Pending appearance reload skipped because game state is no longer safe");
                ClearActiveSwap();
                return;
            }

            int currentBikeId = GetCurrentBikeId();
            if (currentBikeId < 0 || currentBikeId >= GetTotalBikeCount()) {
                LOG_ERROR("[BikeSwap] Pending appearance reload has invalid current bike ID");
                ClearActiveSwap();
                return;
            }

            {
                std::lock_guard<std::mutex> lock(g_pendingAppearanceMutex);
                memcpy(g_activeAppearanceData, g_pendingAppearanceData, sizeof(g_activeAppearanceData));
            }
            g_activeAppearanceReload = true;
            InterlockedExchange(&g_activeSwapBikeId, currentBikeId);
            g_swapStage = SwapStage::RespawnBeforeReload;
            g_stageDelayFrames = RESPAWN_DELAY_FRAMES;
            LOG_INFO("[BikeSwap] Same-bike appearance reload scheduled for bike " << currentBikeId
                << " with pre-reload respawn");
            return;
        }

        if (InterlockedExchange(&g_pendingVisualOnlyReload, 0) != 0) {
            if (InterlockedCompareExchange(&g_swapInProgress, 1, 0) != 0) {
                LOG_WARNING("[BikeSwap] Reload already in progress, ignoring visual-only reload");
                return;
            }
            PauseFmodForSwap();

            if (!IsGameStateSafeForSwap()) {
                LOG_WARNING("[BikeSwap] Pending visual-only reload skipped because game state is no longer safe");
                ClearActiveSwap();
                return;
            }

            int currentBikeId = GetCurrentBikeId();
            if (currentBikeId < 0 || currentBikeId >= GetTotalBikeCount()) {
                LOG_ERROR("[BikeSwap] Pending visual-only reload has invalid current bike ID");
                ClearActiveSwap();
                return;
            }

            {
                std::lock_guard<std::mutex> lock(g_pendingAppearanceMutex);
                memcpy(g_activeAppearanceData, g_pendingVisualOnlyData, sizeof(g_activeAppearanceData));
            }
            g_activeVisualOnlyReload = true;
            InterlockedExchange(&g_activeSwapBikeId, currentBikeId);
            g_swapStage = SwapStage::ReloadBike;
            g_stageDelayFrames = 1;
            LOG_INFO("[BikeSwap] Same-bike visual-only reload scheduled for bike " << currentBikeId
                << " without pre-reload respawn");
            return;
        }

        // Check if we have a pending bike swap. Like GearCustomization, the
        // public API only publishes an atomic request; the actual engine reload
        // is owned by this main-thread hook.
        LONG targetBike = InterlockedExchange(&g_pendingBikeId, -1);
        if (targetBike >= 0) {
            if (InterlockedCompareExchange(&g_swapInProgress, 1, 0) != 0) {
                LOG_WARNING("[BikeSwap] Swap already in progress when pending request was consumed");
                return;
            }
            PauseFmodForSwap();

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

        // Check if a reload or staged swap is already in progress.
        if (HasPendingReloadWork() || InterlockedCompareExchange(&g_pendingBikeId, -1, -1) >= 0) {
            LOG_WARNING("[BikeSwap] Swap already in progress, ignoring request");
            return false;
        }

        if (InterlockedCompareExchange(&g_pendingBikeId, bikeId, -1) != -1) {
            LOG_WARNING("[BikeSwap] Swap already pending, ignoring request");
            return false;
        }
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
        LOG_INFO("[BikeSwap] Appearance RVAs: frame=0x" << std::hex << GetHandleGameFrameUpdateRVA()
            << " initTints=0x" << GetInitBikeAppearanceSlotsRVA()
            << " loadVisuals=0x" << GetLoadBikeMeshAndVisualsRVA()
            << " cleanup=0x" << GetCleanupSceneGeometryRVA()
            << " serialize=0x" << GetSerializeBikeSceneObjectsRVA()
            << std::dec);
        if (BaseAddress::IsSteamVersion()) {
            LOG_INFO("[BikeSwap] Steam mapping evidence: tintChild 0x221fc0 <- Uplay 0x2226d0, hideChild 0x2cbca0 <- Uplay 0x2cc660");
            LOG_WARNING("[BikeSwap] Steam catalog accessor RVAs are duplicate-wrapper matches; treat gearSlot/hiddenObject reads as lower-confidence than the live child helpers");
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

        GearCustomization::Initialize(baseAddress);

        LOG_INFO("[BikeSwap] Initialized successfully (hook: " << (g_hookInstalled ? "active" : "inactive") << ")");

        return true;
    }

    void Shutdown() {
        if (!g_initialized) {
            return;
        }

        GearCustomization::Shutdown();

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
        g_pendingAppearanceTintRefresh = 0;
        g_pendingAppearanceReload = 0;
        g_pendingVisualOnlyReload = 0;
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

        // Runtime swapping writes the selected-bike byte directly. The extracted
        // bike catalog backs IDs 1..6, and ID 0 is observed in live state. IDs
        // above 6 can enter special/non-garage data and crash during game update.
        return MAX_RUNTIME_SAFE_BIKE_ID + 1;
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
            "Pit Viper",    // 0 - observed live baseline
            "Squid",        // 1
            "Roach",        // 2
            "Turtle",       // 3
            "Jackal",       // 4
            "Mantis",       // 5
            "Donkey"        // 6
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

    bool GetCurrentAppearanceData(uint16_t outAppearance[16]) {
        if (!outAppearance) {
            return false;
        }

        void* bikeEntity = GetCurrentBikeEntity();
        if (!bikeEntity) {
            return false;
        }

        uintptr_t appearanceDataAddr = reinterpret_cast<uintptr_t>(bikeEntity) + 0x9ec;
        if (IsBadReadPtr(reinterpret_cast<void*>(appearanceDataAddr), sizeof(uint16_t) * 16)) {
            return false;
        }

        __try {
            memcpy(outAppearance, reinterpret_cast<void*>(appearanceDataAddr), sizeof(uint16_t) * 16);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    bool WriteCurrentAppearanceData(const uint16_t appearanceData[16]) {
        void* bikeEntity = GetCurrentBikeEntity();
        if (!bikeEntity) {
            return false;
        }

        return WriteAppearanceDataToBike(bikeEntity, appearanceData);
    }

    bool QueueCurrentAppearanceTintRefresh(const uint16_t appearanceData[16]) {
        if (!appearanceData) {
            return false;
        }

        if (!g_initialized || !g_hookInstalled) {
            LOG_ERROR("[BikeSwap] Tint refresh unavailable because BikeSwap is not ready");
            return false;
        }

        if (!IsGameStateSafeForSwap()) {
            LOG_WARNING("[BikeSwap] Tint refresh unavailable in current game state");
            return false;
        }

        if (InterlockedCompareExchange(&g_swapInProgress, 0, 0) != 0
            || InterlockedCompareExchange(&g_pendingAppearanceReload, 0, 0) != 0) {
            LOG_WARNING("[BikeSwap] Reload already in progress, ignoring tint refresh request");
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(g_pendingAppearanceMutex);
            memcpy(g_pendingAppearanceTintData, appearanceData, sizeof(g_pendingAppearanceTintData));
        }
        const LONG alreadyPending = InterlockedExchange(&g_pendingAppearanceTintRefresh, 1);
        if (alreadyPending != 0) {
            LOG_VERBOSE("[BikeSwap] Replaced pending tint-slot refresh");
        }
        else {
            LOG_INFO("[BikeSwap] Queued tint-slot refresh");
        }
        return true;
    }

    bool QueueCurrentAppearanceReload(const uint16_t appearanceData[16]) {
        Logging::WriteImmediate("[AppearanceQueue] QueueCurrentAppearanceReload entered");
        if (!appearanceData) {
            Logging::WriteImmediate("[AppearanceQueue] rejected null appearance data");
            return false;
        }

        if (!g_initialized || !g_hookInstalled) {
            Logging::WriteImmediate("[AppearanceQueue] rejected because BikeSwap is not ready");
            LOG_ERROR("[BikeSwap] Appearance reload unavailable because BikeSwap is not ready");
            return false;
        }

        if (!IsGameStateSafeForSwap()) {
            Logging::WriteImmediate("[AppearanceQueue] rejected because game state is unsafe");
            LOG_WARNING("[BikeSwap] Appearance reload unavailable in current game state");
            return false;
        }

        if (InterlockedCompareExchange(&g_swapInProgress, 0, 0) != 0
            || InterlockedCompareExchange(&g_pendingAppearanceReload, 0, 0) != 0) {
            Logging::WriteImmediate("[AppearanceQueue] rejected because reload is already in progress");
            LOG_WARNING("[BikeSwap] Reload already in progress, ignoring appearance request");
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(g_pendingAppearanceMutex);
            memcpy(g_pendingAppearanceData, appearanceData, sizeof(g_pendingAppearanceData));
        }
        InterlockedExchange(&g_pendingAppearanceReload, 1);
        Logging::WriteImmediate("[AppearanceQueue] pending same-bike appearance reload set");
        LOG_INFO("[BikeSwap] Queued same-bike appearance reload");
        return true;
    }

    bool QueueCurrentVisualOnlyReload(const uint16_t appearanceData[16]) {
        if (!appearanceData) {
            return false;
        }

        // Visual-only reload is safe for tint/appearance refreshes, but live hidden-
        // object payload swaps can change the backing mesh payload as well.  The
        // narrow cleanup/load path has proven unsafe when restoring some real mesh
        // variants (for example CHEETAH_BACKSWING_1_1 after 1_2/1_4), so promote
        // those cases to the broader same-bike reload path.
        if (GearCustomization::HasPendingReloadMutation()) {
            LOG_INFO("[BikeSwap] Promoting visual-only reload to full appearance reload after hidden-object payload patch");
            return QueueCurrentAppearanceReload(appearanceData);
        }

        if (!g_initialized || !g_hookInstalled) {
            LOG_ERROR("[BikeSwap] Visual-only reload unavailable because BikeSwap is not ready");
            return false;
        }

        if (!IsGameStateSafeForSwap()) {
            LOG_WARNING("[BikeSwap] Visual-only reload unavailable in current game state");
            return false;
        }

        if (InterlockedCompareExchange(&g_swapInProgress, 0, 0) != 0
            || InterlockedCompareExchange(&g_pendingAppearanceReload, 0, 0) != 0
            || InterlockedCompareExchange(&g_pendingVisualOnlyReload, 0, 0) != 0) {
            LOG_WARNING("[BikeSwap] Reload already in progress, ignoring visual-only request");
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(g_pendingAppearanceMutex);
            memcpy(g_pendingVisualOnlyData, appearanceData, sizeof(g_pendingVisualOnlyData));
        }
        InterlockedExchange(&g_pendingVisualOnlyReload, 1);
        LOG_INFO("[BikeSwap] Queued same-bike visual-only reload");
        return true;
    }

    bool GetSwapStatus(float* secondsRemaining, float* progress01, std::string* statusText) {
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

        const bool stagedSwapActive = InterlockedCompareExchange(&g_swapInProgress, 0, 0) != 0
            || g_swapStage != SwapStage::None;
        float stageSeconds = 0.0f;
        float stageProgress = 1.0f;
        if (stagedSwapActive && g_stageDelayFrames > 0) {
            int totalFrames = RESPAWN_DELAY_FRAMES;
            if (g_swapStage == SwapStage::ReloadBike) {
                totalFrames = RELOAD_DELAY_FRAMES;
            }
            stageSeconds = static_cast<float>(g_stageDelayFrames) / 60.0f;
            stageProgress = 1.0f - (static_cast<float>(g_stageDelayFrames) / static_cast<float>(totalFrames));
        }

        float remaining = 0.0f;
        float progress = 1.0f;
        std::string text;

        if (stagedSwapActive) {
            remaining = stageSeconds;
            progress = stageProgress;
            if (g_swapStage == SwapStage::ReloadBike) {
                text = "Bike swap applying";
            }
            else {
                text = "Bike swap respawning";
            }
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
        LOG_INFO("[BikeSwap] Swap in progress: "
            << (InterlockedCompareExchange(&g_swapInProgress, 0, 0) != 0 ? "yes" : "no"));
        LOG_INFO("[BikeSwap] Swap stage: " << static_cast<int>(g_swapStage));
        LOG_INFO("[BikeSwap] Stage delay frames: " << g_stageDelayFrames);
        LOG_INFO("[BikeSwap] Active swap bike ID: " << g_activeSwapBikeId);
        LOG_INFO("[BikeSwap] Active appearance reload: " << (g_activeAppearanceReload ? "yes" : "no"));
        LOG_INFO("[BikeSwap] === END DEBUG ===");
    }
}
