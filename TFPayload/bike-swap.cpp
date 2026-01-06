// bike-swap.cpp
// Implementation of runtime bike swapping functionality
// Based on Ghidra reverse engineering of Trials Fusion bike selection system

#include "pch.h"
#include "bike-swap.h"
#include "respawn.h"
#include "logging.h"
#include "keybindings.h"
#include <Windows.h>
#include <unordered_map>

namespace BikeSwap {
    // ============================================================================
    // Game Memory Addresses (RVA offsets - subtract 0x700000 from Ghidra addresses)
    // ============================================================================

    // Global structure pointer (same as respawn.cpp)
    static constexpr uintptr_t GLOBAL_STRUCT_RVA = 0x104b308;

    // Bike-related offsets within bike/rider object
    static constexpr uintptr_t BIKE_ID_OFFSET = 0x680;           // Bike type ID (byte)
    static constexpr uintptr_t BIKE_GFX_HASH_OFFSET = 0x67c;     // Graphics hash for bike model
    static constexpr uintptr_t BIKE_IS_REAL_OFFSET = 0x13c;      // Flag: 1 = real bike, 0 = ghost/replay
    static constexpr uintptr_t BIKE_APPEARANCE_OFFSET = 0x9ec;   // Appearance/customization data (32 bytes)
    
    // Function RVAs (Ghidra address - 0x700000)
    static constexpr uintptr_t LOAD_BIKE_SETTINGS_RVA = 0x208490;           // LoadBikeSettings
    static constexpr uintptr_t BIKE_LOOKUP_RVA = 0x19a30;                   // Bike data lookup by ID
    static constexpr uintptr_t INITIALIZE_BIKE_ATTACHMENTS_RVA = 0x20ce90;  // InitializeBikeAttachments
    static constexpr uintptr_t CHANGE_BIKE_WITH_MESH_RVA = 0x229c00;        // ChangeBikeWithMeshReload (FUN_00929c00)
    static constexpr uintptr_t LOAD_BIKE_MESH_RVA = 0x2144e0;               // FUN_009144e0 - loads bike mesh
    static constexpr uintptr_t CLEANUP_SCENE_GEOMETRY_RVA = 0x21e610;       // CleanupSceneGeometry
    static constexpr uintptr_t GET_APPEARANCE_DATA_RVA = 0x3055a0;          // FUN_00a055a0 - gets default appearance for bike
    static constexpr uintptr_t INIT_BIKE_APPEARANCE_SLOTS_RVA = 0x229980;   // InitializeBikeAppearanceSlots
    static constexpr uintptr_t SERIALIZE_BIKE_SCENE_RVA = 0x205750;         // SerializeBikeSceneObjects
    static constexpr uintptr_t FINALIZE_RIDER_SETUP_RVA = 0x20a7c0;         // FinalizeRiderSetup
    static constexpr uintptr_t RESET_BIKE_STATE_RVA = 0x2059d0;             // ResetBikeState
    static constexpr uintptr_t RELOAD_BIKE_WITH_NEW_ID_RVA = 0x9fd40;       // FUN_0079fd40 - reloads bike using ID at param+0x684
    static constexpr uintptr_t ALLOCATE_MEMORY_RVA = 0xc340;                // AllocateMemory
    static constexpr uintptr_t CONSTRUCT_GAME_MESSAGE_RVA = 0xc03f0;        // ConstructGameMessage
    static constexpr uintptr_t SEND_MESSAGE_RVA = 0x67a040;                 // SendMessage
    
    // Bike database offset within global struct
    static constexpr uintptr_t BIKE_DATABASE_OFFSET = 0x118;    // *(globalStruct+0x118) = bike database

    // ============================================================================
    // Function Pointer Types
    // ============================================================================

    // LoadBikeSettings(void* bikePtr) - __fastcall, bike pointer in ECX
    typedef void(__fastcall* LoadBikeSettingsFunc)(void* bikePtr);

    // Bike lookup: returns bike data pointer for given bike ID
    // FUN_00719a30(void* bikeDatabase, byte bikeId)
    typedef void*(__thiscall* BikeLookupFunc)(void* bikeDatabase, uint8_t bikeId);

    // InitializeBikeAttachments(void* bikePtr)
    typedef void(__fastcall* InitializeBikeAttachmentsFunc)(void* bikePtr);

    // ChangeBikeWithMeshReload(void* bikePtr, byte bikeId, void* appearanceData)
    // __thiscall: bikePtr in ECX, bikeId and appearanceData on stack
    typedef void(__thiscall* ChangeBikeWithMeshReloadFunc)(void* bikePtr, uint8_t bikeId, void* appearanceData);

    // LoadBikeMesh(void* bikePtr) - __fastcall
    typedef void(__fastcall* LoadBikeMeshFunc)(void* bikePtr);

    // LoadBikeMeshAndVisuals - the actual mesh loading function
    // FUN_009144e0 at RVA 0x2144e0
    typedef void(__fastcall* LoadBikeMeshAndVisualsFunc)(void* bikePtr);

    // CleanupSceneGeometry(void* bikePtr, char fullCleanup) - __thiscall
    typedef void(__thiscall* CleanupSceneGeometryFunc)(void* bikePtr, char fullCleanup);

    // GetAppearanceData(void* appearanceDb, ushort* outData, byte bikeId, byte param3) - __thiscall
    // Returns pointer to outData after populating it
    typedef uint16_t*(__thiscall* GetAppearanceDataFunc)(void* appearanceDb, uint16_t* outData, uint8_t bikeId, uint8_t param3);

    // InitializeBikeAppearanceSlots(void* bikePtr, char param1) - __thiscall
    typedef void(__thiscall* InitBikeAppearanceSlotsFunc)(void* bikePtr, char param1);

    // SerializeBikeSceneObjects(int bikePtr) - __fastcall
    typedef void(__fastcall* SerializeBikeSceneFunc)(int bikePtr);

    // FinalizeRiderSetup(void* bikePtr) - __fastcall
    typedef void(__fastcall* FinalizeRiderSetupFunc)(void* bikePtr);

    // ResetBikeState(int bikePtr) - __fastcall (takes int, not void*)
    typedef void(__fastcall* ResetBikeStateFunc)(int bikePtr);

    // ReloadBikeWithNewId(void* playerSettings) - __fastcall
    // FUN_0079fd40 - reads bike ID from playerSettings+0x684, reloads bike mesh
    typedef void(__fastcall* ReloadBikeWithNewIdFunc)(void* playerSettings);

    // ============================================================================
    // Appearance Data Structure (32 bytes at bike+0x9ec)
    // ============================================================================
    #pragma pack(push, 1)
    struct BikeAppearanceData {
        uint16_t field_0;       // +0x00
        uint16_t field_2;       // +0x02
        uint16_t field_4;       // +0x04
        uint32_t field_6;       // +0x06 (was 0x9f4)
        uint32_t field_A;       // +0x0A (was 0x9f8)
        uint32_t field_E;       // +0x0E (was 0x9fc)
        uint16_t field_12;      // +0x12 (was 0xa00)
        uint16_t field_14;      // +0x14 (was 0xa02)
        uint32_t field_16;      // +0x16 (was 0xa04)
        uint32_t field_1A;      // +0x1A (was 0xa08)
        uint16_t padding;       // +0x1E padding to 32 bytes
    };
    #pragma pack(pop)

    // ============================================================================
    // Global State
    // ============================================================================

    static bool g_initialized = false;
    static uintptr_t g_baseAddress = 0;
    static void** g_globalStructPtr = nullptr;
    
    static LoadBikeSettingsFunc g_loadBikeSettings = nullptr;
    static BikeLookupFunc g_bikeLookup = nullptr;
    static InitializeBikeAttachmentsFunc g_initBikeAttachments = nullptr;
    static ChangeBikeWithMeshReloadFunc g_changeBikeWithMesh = nullptr;
    static LoadBikeMeshFunc g_loadBikeMesh = nullptr;
    static LoadBikeMeshAndVisualsFunc g_loadBikeMeshAndVisuals = nullptr;
    static CleanupSceneGeometryFunc g_cleanupSceneGeometry = nullptr;
    static GetAppearanceDataFunc g_getAppearanceData = nullptr;
    static InitBikeAppearanceSlotsFunc g_initBikeAppearanceSlots = nullptr;
    static SerializeBikeSceneFunc g_serializeBikeScene = nullptr;
    static FinalizeRiderSetupFunc g_finalizeRiderSetup = nullptr;
    static ResetBikeStateFunc g_resetBikeState = nullptr;
    static ReloadBikeWithNewIdFunc g_reloadBikeWithNewId = nullptr;

    // Bike name lookup table
    static std::unordered_map<uint8_t, std::string> g_bikeNames = {
        {1, "Squid (125cc)"},
        {2, "Donkey (250cc)"},
        {3, "Pit Viper (450cc)"},
        {4, "Roach (FMX)"},
        {5, "Banshee (Quad)"},
        {6, "Turtle (Unicycle)"},
        {7, "Mantis (Helium)"},
        {8, "Rabbit (Raptor)"},
    };

    // State machine for pending bike swap
    enum class SwapState {
        None,
        WaitingForRespawn,      // Waiting for initial respawn to complete
        ExecutingSwap,          // Executing the mesh swap
        WaitingForStabilize     // Waiting for final stabilization
    };

    // Pending bike swap state - used to defer bike swap to a safe execution point
    static bool g_pendingBikeSwap = false;
    static uint8_t g_pendingBikeId = 0;
    static DWORD g_swapRequestTime = 0;
    static const DWORD SWAP_COOLDOWN_MS = 500;  // Minimum time between swap attempts
    static SwapState g_swapState = SwapState::None;
    
    // Track restart function pointer for safer bike swapping
    typedef void*(__cdecl* AllocateMemoryFunc)(int size);
    typedef void*(__thiscall* ConstructGameMessageFunc)(void* obj, int type, int p1, int p2);
    typedef void(__thiscall* SendMessageFunc)(void* queue, int msg, int p1, int p2);
    
    static constexpr uintptr_t ALLOCATE_MEMORY_RVA_LOCAL = 0xc340;
    static constexpr uintptr_t CONSTRUCT_GAME_MESSAGE_RVA_LOCAL = 0xc03f0;
    static constexpr uintptr_t SEND_MESSAGE_RVA_LOCAL = 0x67a040;

    // List of known valid bike IDs for cycling
    static const uint8_t g_validBikeIds[] = {1, 2, 3, 4, 5, 6, 7, 8};
    static const int g_validBikeCount = sizeof(g_validBikeIds) / sizeof(g_validBikeIds[0]);

    // ============================================================================
    // SEH-Safe Helper Functions (no C++ objects that need unwinding)
    // ============================================================================

    // Safe wrapper for bike lookup - returns nullptr on exception
    static void* SafeBikeLookup(void* bikeDatabase, uint8_t bikeId) {
        __try {
            return g_bikeLookup(bikeDatabase, bikeId);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return nullptr;
        }
    }

    // Safe wrapper for LoadBikeSettings - returns false on exception
    static bool SafeLoadBikeSettings(void* bikePtr) {
        __try {
            g_loadBikeSettings(bikePtr);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    // Safe wrapper for writing bike ID - returns false on exception
    static bool SafeWriteBikeId(void* bikePtr, uint8_t bikeId) {
        __try {
            uintptr_t bikeIdAddr = reinterpret_cast<uintptr_t>(bikePtr) + BIKE_ID_OFFSET;
            *reinterpret_cast<int*>(bikeIdAddr) = static_cast<int>(bikeId);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    // Safe wrapper for InitializeBikeAttachments - returns false on exception
    static bool SafeInitBikeAttachments(void* bikePtr) {
        __try {
            g_initBikeAttachments(bikePtr);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    // Safe wrapper for ChangeBikeWithMeshReload - returns false on exception
    static bool SafeChangeBikeWithMesh(void* bikePtr, uint8_t bikeId, void* appearanceData) {
        __try {
            g_changeBikeWithMesh(bikePtr, bikeId, appearanceData);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    // Safe wrapper for LoadBikeMesh - returns false on exception
    static bool SafeLoadBikeMesh(void* bikePtr) {
        __try {
            g_loadBikeMesh(bikePtr);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    // Safe wrapper for LoadBikeMeshAndVisuals - returns false on exception
    static bool SafeLoadBikeMeshAndVisuals(void* bikePtr) {
        __try {
            g_loadBikeMeshAndVisuals(bikePtr);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    // Safe wrapper for CleanupSceneGeometry - returns false on exception
    static bool SafeCleanupSceneGeometry(void* bikePtr, char fullCleanup) {
        __try {
            g_cleanupSceneGeometry(bikePtr, fullCleanup);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    // Read appearance data from bike - returns false on exception
    static bool SafeReadAppearanceData(void* bikePtr, BikeAppearanceData* outData) {
        __try {
            uintptr_t appearanceAddr = reinterpret_cast<uintptr_t>(bikePtr) + BIKE_APPEARANCE_OFFSET;
            memcpy(outData, reinterpret_cast<void*>(appearanceAddr), sizeof(BikeAppearanceData));
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    // Get the appearance database pointer from global struct
    // Path: globalStruct + 0x120 -> dereference -> +0x10 -> dereference
    static void* GetAppearanceDatabase() {
        if (!g_globalStructPtr || IsBadReadPtr(g_globalStructPtr, sizeof(void*))) {
            return nullptr;
        }

        void* globalStruct = *g_globalStructPtr;
        if (!globalStruct || IsBadReadPtr(globalStruct, 0x200)) {
            return nullptr;
        }

        // globalStruct + 0x120
        uintptr_t step1Addr = reinterpret_cast<uintptr_t>(globalStruct) + 0x120;
        if (IsBadReadPtr((void*)step1Addr, sizeof(void*))) {
            return nullptr;
        }

        void* step1Ptr = *reinterpret_cast<void**>(step1Addr);
        if (!step1Ptr || IsBadReadPtr(step1Ptr, 0x20)) {
            return nullptr;
        }

        // step1Ptr + 0x10
        uintptr_t step2Addr = reinterpret_cast<uintptr_t>(step1Ptr) + 0x10;
        if (IsBadReadPtr((void*)step2Addr, sizeof(void*))) {
            return nullptr;
        }

        void* appearanceDb = *reinterpret_cast<void**>(step2Addr);
        
        // Validate the appearance database pointer
        if (appearanceDb && IsBadReadPtr(appearanceDb, 0x100)) {
            return nullptr;
        }
        
        return appearanceDb;
    }

    // Safe wrapper for GetAppearanceData - returns false on exception
    static bool SafeGetAppearanceData(void* appearanceDb, uint16_t* outData, uint8_t bikeId) {
        __try {
            g_getAppearanceData(appearanceDb, outData, bikeId, 0);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    // Safe wrapper for InitializeBikeAppearanceSlots - returns false on exception
    static bool SafeInitBikeAppearanceSlots(void* bikePtr, char param1) {
        __try {
            g_initBikeAppearanceSlots(bikePtr, param1);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    // Safe wrapper for SerializeBikeSceneObjects - returns false on exception
    static bool SafeSerializeBikeScene(void* bikePtr) {
        __try {
            g_serializeBikeScene(reinterpret_cast<int>(bikePtr));
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    // Safe wrapper for FinalizeRiderSetup - returns false on exception
    static bool SafeFinalizeRiderSetup(void* bikePtr) {
        __try {
            g_finalizeRiderSetup(bikePtr);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    // Safe wrapper for ResetBikeState - returns false on exception
    static bool SafeResetBikeState(void* bikePtr) {
        __try {
            g_resetBikeState(reinterpret_cast<int>(bikePtr));
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    // Safe wrapper for ReloadBikeWithNewId - returns false on exception
    static bool SafeReloadBikeWithNewId(void* playerSettings) {
        __try {
            g_reloadBikeWithNewId(playerSettings);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    // Send a track restart message (message type 0x10)
    // This triggers the game's built-in restart sequence which safely reloads everything
    static bool SendTrackRestartMessage() {
        if (!g_baseAddress) return false;

        void* globalStruct = *g_globalStructPtr;
        if (!globalStruct || IsBadReadPtr(globalStruct, 0x104)) {
            return false;
        }

        // Get message queue
        void* messageQueue = *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(globalStruct) + 0x100);
        if (!messageQueue) {
            return false;
        }

        // Get function pointers
        AllocateMemoryFunc allocMem = reinterpret_cast<AllocateMemoryFunc>(g_baseAddress + ALLOCATE_MEMORY_RVA_LOCAL);
        ConstructGameMessageFunc constructMsg = reinterpret_cast<ConstructGameMessageFunc>(g_baseAddress + CONSTRUCT_GAME_MESSAGE_RVA_LOCAL);
        SendMessageFunc sendMsg = reinterpret_cast<SendMessageFunc>(g_baseAddress + SEND_MESSAGE_RVA_LOCAL);

        __try {
            // Allocate message object (0x90 bytes as per SendRestartMessage)
            void* msgObj = allocMem(0x90);
            if (!msgObj) return false;

            // Construct game message with type 0x10 (restart)
            void* msg = constructMsg(msgObj, 0x10, 0, 0);
            if (!msg) return false;

            // Send the message
            sendMsg(messageQueue, reinterpret_cast<int>(msg), 0, 0);
            
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    // ============================================================================
    // Helper Functions
    // ============================================================================

    static void* GetGameManager() {
        if (!g_globalStructPtr || IsBadReadPtr(g_globalStructPtr, sizeof(void*))) {
            return nullptr;
        }

        void* globalStruct = *g_globalStructPtr;
        if (!globalStruct || IsBadReadPtr(globalStruct, 0x200)) {
            return nullptr;
        }

        uintptr_t managerAddr = reinterpret_cast<uintptr_t>(globalStruct) + 0xdc;
        if (IsBadReadPtr((void*)managerAddr, sizeof(void*))) {
            return nullptr;
        }

        void* manager = *reinterpret_cast<void**>(managerAddr);
        if (!manager || IsBadReadPtr(manager, 0x1000)) {
            return nullptr;
        }

        return manager;
    }

    static void* GetBikePointer() {
        // Reuse the respawn system's bike pointer getter for consistency
        return Respawn::GetBikePointer();
    }

    static void* GetBikeDatabase() {
        if (!g_globalStructPtr || IsBadReadPtr(g_globalStructPtr, sizeof(void*))) {
            return nullptr;
        }

        void* globalStruct = *g_globalStructPtr;
        if (!globalStruct || IsBadReadPtr(globalStruct, 0x200)) {
            return nullptr;
        }

        uintptr_t dbAddr = reinterpret_cast<uintptr_t>(globalStruct) + BIKE_DATABASE_OFFSET;
        if (IsBadReadPtr((void*)dbAddr, sizeof(void*))) {
            return nullptr;
        }

        void* bikeDatabase = *reinterpret_cast<void**>(dbAddr);
        return bikeDatabase;
    }

    // ============================================================================
    // Initialization & Shutdown
    // ============================================================================

    bool Initialize(uintptr_t baseAddress) {
        if (g_initialized) {
            LOG_VERBOSE("[BikeSwap] Already initialized");
            return true;
        }

        g_baseAddress = baseAddress;

        // Get global struct pointer
        g_globalStructPtr = reinterpret_cast<void**>(baseAddress + GLOBAL_STRUCT_RVA);
        if (IsBadReadPtr(g_globalStructPtr, sizeof(void*))) {
            LOG_ERROR("[BikeSwap] Failed to get global struct pointer");
            return false;
        }

        // Get function pointers
        g_loadBikeSettings = reinterpret_cast<LoadBikeSettingsFunc>(baseAddress + LOAD_BIKE_SETTINGS_RVA);
        g_bikeLookup = reinterpret_cast<BikeLookupFunc>(baseAddress + BIKE_LOOKUP_RVA);
        g_initBikeAttachments = reinterpret_cast<InitializeBikeAttachmentsFunc>(baseAddress + INITIALIZE_BIKE_ATTACHMENTS_RVA);
        g_changeBikeWithMesh = reinterpret_cast<ChangeBikeWithMeshReloadFunc>(baseAddress + CHANGE_BIKE_WITH_MESH_RVA);
        g_loadBikeMesh = reinterpret_cast<LoadBikeMeshFunc>(baseAddress + LOAD_BIKE_MESH_RVA);
        g_loadBikeMeshAndVisuals = reinterpret_cast<LoadBikeMeshAndVisualsFunc>(baseAddress + LOAD_BIKE_MESH_RVA);  // Same RVA
        g_cleanupSceneGeometry = reinterpret_cast<CleanupSceneGeometryFunc>(baseAddress + CLEANUP_SCENE_GEOMETRY_RVA);
        g_getAppearanceData = reinterpret_cast<GetAppearanceDataFunc>(baseAddress + GET_APPEARANCE_DATA_RVA);
        g_initBikeAppearanceSlots = reinterpret_cast<InitBikeAppearanceSlotsFunc>(baseAddress + INIT_BIKE_APPEARANCE_SLOTS_RVA);
        g_serializeBikeScene = reinterpret_cast<SerializeBikeSceneFunc>(baseAddress + SERIALIZE_BIKE_SCENE_RVA);
        g_finalizeRiderSetup = reinterpret_cast<FinalizeRiderSetupFunc>(baseAddress + FINALIZE_RIDER_SETUP_RVA);
        g_resetBikeState = reinterpret_cast<ResetBikeStateFunc>(baseAddress + RESET_BIKE_STATE_RVA);
        g_reloadBikeWithNewId = reinterpret_cast<ReloadBikeWithNewIdFunc>(baseAddress + RELOAD_BIKE_WITH_NEW_ID_RVA);

        // Validate critical function pointers
        if (IsBadCodePtr((FARPROC)g_loadBikeSettings)) {
            LOG_ERROR("[BikeSwap] Invalid LoadBikeSettings function pointer");
            return false;
        }

        if (IsBadCodePtr((FARPROC)g_bikeLookup)) {
            LOG_ERROR("[BikeSwap] Invalid BikeLookup function pointer");
            return false;
        }

        if (IsBadCodePtr((FARPROC)g_changeBikeWithMesh)) {
            LOG_WARNING("[BikeSwap] Invalid ChangeBikeWithMeshReload function pointer - mesh swap may not work");
        }

        if (IsBadCodePtr((FARPROC)g_getAppearanceData)) {
            LOG_WARNING("[BikeSwap] Invalid GetAppearanceData function pointer - using fallback");
        }

        g_initialized = true;
        LOG_VERBOSE("[BikeSwap] Initialized successfully");
        LOG_VERBOSE("[BikeSwap] LoadBikeSettings @ 0x" << std::hex << (uintptr_t)g_loadBikeSettings);
        LOG_VERBOSE("[BikeSwap] ChangeBikeWithMesh @ 0x" << std::hex << (uintptr_t)g_changeBikeWithMesh);
        LOG_VERBOSE("[BikeSwap] GetAppearanceData @ 0x" << std::hex << (uintptr_t)g_getAppearanceData);

        return true;
    }

    void Shutdown() {
        g_initialized = false;
        g_baseAddress = 0;
        g_globalStructPtr = nullptr;
        g_loadBikeSettings = nullptr;
        g_bikeLookup = nullptr;
        g_initBikeAttachments = nullptr;
        g_changeBikeWithMesh = nullptr;
        g_loadBikeMesh = nullptr;
        g_loadBikeMeshAndVisuals = nullptr;
        g_cleanupSceneGeometry = nullptr;
        g_getAppearanceData = nullptr;
        g_initBikeAppearanceSlots = nullptr;
        g_serializeBikeScene = nullptr;
        g_finalizeRiderSetup = nullptr;
        g_resetBikeState = nullptr;
        g_reloadBikeWithNewId = nullptr;
        
        // Reset pending swap state
        g_pendingBikeSwap = false;
        g_pendingBikeId = 0;
        g_swapRequestTime = 0;
        g_swapState = SwapState::None;
        
        LOG_VERBOSE("[BikeSwap] Shutdown complete");
    }

    bool IsInitialized() {
        return g_initialized;
    }

    // ============================================================================
    // Bike Information Functions
    // ============================================================================

    std::vector<BikeInfo> GetAvailableBikes() {
        std::vector<BikeInfo> bikes;

        void* bikeDatabase = GetBikeDatabase();

        for (int i = 0; i < g_validBikeCount; i++) {
            uint8_t id = g_validBikeIds[i];
            BikeInfo info;
            info.id = id;
            
            auto nameIt = g_bikeNames.find(id);
            if (nameIt != g_bikeNames.end()) {
                info.name = nameIt->second;
            } else {
                info.name = "Unknown Bike " + std::to_string(id);
            }
            
            info.internalName = "Bike_" + std::to_string(id);
            
            // Check if bike data exists in the database
            if (bikeDatabase && g_bikeLookup) {
                void* bikeData = SafeBikeLookup(bikeDatabase, id);
                info.available = (bikeData != nullptr);
            } else {
                info.available = false;
            }
            
            bikes.push_back(info);
        }

        return bikes;
    }

    uint8_t GetCurrentBikeId() {
        void* bikePtr = GetBikePointer();
        if (!bikePtr) {
            return 0;
        }

        uintptr_t bikeIdAddr = reinterpret_cast<uintptr_t>(bikePtr) + BIKE_ID_OFFSET;
        if (IsBadReadPtr((void*)bikeIdAddr, sizeof(int))) {
            return 0;
        }

        // Bike ID is stored as an int at +0x680, but only the low byte matters
        int bikeIdFull = *reinterpret_cast<int*>(bikeIdAddr);
        return static_cast<uint8_t>(bikeIdFull);
    }

    std::string GetCurrentBikeName() {
        uint8_t id = GetCurrentBikeId();
        return GetBikeNameFromId(id);
    }

    std::string GetBikeNameFromId(uint8_t bikeId) {
        auto it = g_bikeNames.find(bikeId);
        if (it != g_bikeNames.end()) {
            return it->second;
        }
        return "Unknown (" + std::to_string(bikeId) + ")";
    }

    // ============================================================================
    // Bike Swap Functions
    // ============================================================================

    bool SetBike(BikeType bike) {
        return SetBikeById(static_cast<uint8_t>(bike));
    }

    // Internal function to execute the actual bike swap
    // This should only be called when the game is in a safe state
    static bool ExecuteBikeSwapInternal(uint8_t bikeId) {
        void* bikePtr = GetBikePointer();
        if (!bikePtr) {
            LOG_ERROR("[BikeSwap] No active bike found during swap execution");
            return false;
        }

        // Get appearance data
        void* appearanceDb = GetAppearanceDatabase();
        uint16_t appearanceData[16];  // 32 bytes
        memset(appearanceData, 0, sizeof(appearanceData));
        
        if (appearanceDb && g_getAppearanceData && !IsBadCodePtr((FARPROC)g_getAppearanceData)) {
            SafeGetAppearanceData(appearanceDb, appearanceData, bikeId);
        }
        
        // Call ChangeBikeWithMeshReload
        bool meshChanged = false;
        if (g_changeBikeWithMesh && !IsBadCodePtr((FARPROC)g_changeBikeWithMesh)) {
            LOG_VERBOSE("[BikeSwap] Executing ChangeBikeWithMeshReload...");
            meshChanged = SafeChangeBikeWithMesh(bikePtr, bikeId, appearanceData);
        }
        
        return meshChanged;
    }

    // Process any pending bike swap - called from CheckHotkey
    // Uses a state machine to safely sequence the swap operations
    void ProcessPendingBikeSwap() {
        if (!g_pendingBikeSwap) {
            g_swapState = SwapState::None;
            return;
        }

        DWORD currentTime = GetTickCount();
        DWORD elapsed = currentTime - g_swapRequestTime;

        switch (g_swapState) {
            case SwapState::WaitingForRespawn: {
                // Wait 250ms for the initial respawn to fully complete
                // This gives time for CleanupSceneGeometry and LoadSceneObjectsFromStream
                if (elapsed < 250) {
                    return;
                }

                // Extra safety check - make sure bike pointer is still valid
                void* bikePtr = GetBikePointer();
                if (!bikePtr) {
                    LOG_ERROR("[BikeSwap] Bike pointer became invalid, canceling swap");
                    g_pendingBikeSwap = false;
                    g_swapState = SwapState::None;
                    return;
                }

                // Now execute the actual mesh swap
                std::string bikeName = GetBikeNameFromId(g_pendingBikeId);
                LOG_VERBOSE("[BikeSwap] Executing mesh swap to " << bikeName);

                if (ExecuteBikeSwapInternal(g_pendingBikeId)) {
                    LOG_INFO("[BikeSwap] Mesh swap successful!");
                    
                    // Move to stabilization phase
                    g_swapState = SwapState::ExecutingSwap;
                    g_swapRequestTime = currentTime;  // Reset timer for next phase
                    
                    // Trigger another respawn to reset game state after mesh change
                    LOG_VERBOSE("[BikeSwap] Triggering stabilization respawn...");
                    Respawn::RespawnAtCheckpoint();
                } else {
                    LOG_WARNING("[BikeSwap] Mesh swap may have failed");
                    g_pendingBikeSwap = false;
                    g_swapState = SwapState::None;
                }
                break;
            }

            case SwapState::ExecutingSwap:
            case SwapState::WaitingForStabilize: {
                // Wait another 200ms after the stabilization respawn
                if (elapsed < 200) {
                    return;
                }

                // Swap complete!
                std::string bikeName = GetBikeNameFromId(g_pendingBikeId);
                LOG_INFO("[BikeSwap] Bike swap complete! Now riding " << bikeName);
                
                g_pendingBikeSwap = false;
                g_pendingBikeId = 0;
                g_swapState = SwapState::None;
                break;
            }

            case SwapState::None:
            default:
                // Shouldn't happen, but reset state just in case
                g_pendingBikeSwap = false;
                break;
        }
    }

    bool SetBikeById(uint8_t bikeId) {
        if (!g_initialized) {
            LOG_ERROR("[BikeSwap] Not initialized");
            return false;
        }

        // Check cooldown to prevent rapid swapping
        DWORD currentTime = GetTickCount();
        if (g_swapRequestTime != 0 && (currentTime - g_swapRequestTime) < SWAP_COOLDOWN_MS) {
            LOG_VERBOSE("[BikeSwap] Swap on cooldown, please wait");
            return false;
        }

        void* bikePtr = GetBikePointer();
        if (!bikePtr) {
            LOG_ERROR("[BikeSwap] No active bike found - are you in a track?");
            return false;
        }

        // Verify the bike ID is valid by checking the database
        void* bikeDatabase = GetBikeDatabase();
        if (!bikeDatabase) {
            LOG_ERROR("[BikeSwap] Could not get bike database");
            return false;
        }

        void* bikeData = SafeBikeLookup(bikeDatabase, bikeId);
        if (!bikeData) {
            LOG_ERROR("[BikeSwap] Bike ID " << (int)bikeId << " not found in database");
            return false;
        }

        uint8_t currentId = GetCurrentBikeId();
        if (currentId == bikeId) {
            LOG_VERBOSE("[BikeSwap] Already using bike ID " << (int)bikeId);
            return true;
        }

        // Get names for logging
        std::string fromName = GetBikeNameFromId(currentId);
        std::string toName = GetBikeNameFromId(bikeId);
        
        LOG_INFO("[BikeSwap] Requesting swap from " << fromName << " to " << toName);

        // SAFER APPROACH: Defer the bike swap to execute after a respawn
        // This ensures the game is in a transitional state where mesh changes are safer
        
        // Step 1: Write bike ID to player settings (for future restarts)
        void* globalStruct = *g_globalStructPtr;
        if (globalStruct && !IsBadReadPtr(globalStruct, 0x108)) {
            uintptr_t playerSettingsAddr = reinterpret_cast<uintptr_t>(globalStruct) + 0x104;
            if (!IsBadReadPtr((void*)playerSettingsAddr, sizeof(void*))) {
                void* playerSettings = *reinterpret_cast<void**>(playerSettingsAddr);
                if (playerSettings && !IsBadWritePtr(reinterpret_cast<uint8_t*>(playerSettings) + 0x684, 1)) {
                    *reinterpret_cast<uint8_t*>(reinterpret_cast<uintptr_t>(playerSettings) + 0x684) = bikeId;
                    LOG_VERBOSE("[BikeSwap] Bike ID written to player settings");
                }
            }
        }
        
        // Step 2: Set up pending swap state
        g_pendingBikeSwap = true;
        g_pendingBikeId = bikeId;
        g_swapRequestTime = currentTime;
        g_swapState = SwapState::WaitingForRespawn;  // Start state machine
        
        // Step 3: Trigger respawn - the actual swap will happen after respawn completes
        LOG_VERBOSE("[BikeSwap] Triggering initial respawn, swap will execute in ~250ms...");
        Respawn::RespawnAtCheckpoint();
        
        return true;
    }

    bool CycleNextBike() {
        uint8_t currentId = GetCurrentBikeId();
        if (currentId == 0) {
            LOG_ERROR("[BikeSwap] Could not get current bike ID");
            return false;
        }

        // Find current bike in the list
        int currentIndex = -1;
        for (int i = 0; i < g_validBikeCount; i++) {
            if (g_validBikeIds[i] == currentId) {
                currentIndex = i;
                break;
            }
        }

        // Get next bike ID (wrap around)
        int nextIndex = (currentIndex + 1) % g_validBikeCount;
        uint8_t nextId = g_validBikeIds[nextIndex];

        return SetBikeById(nextId);
    }

    bool CyclePreviousBike() {
        uint8_t currentId = GetCurrentBikeId();
        if (currentId == 0) {
            LOG_ERROR("[BikeSwap] Could not get current bike ID");
            return false;
        }

        // Find current bike in the list
        int currentIndex = -1;
        for (int i = 0; i < g_validBikeCount; i++) {
            if (g_validBikeIds[i] == currentId) {
                currentIndex = i;
                break;
            }
        }

        // Get previous bike ID (wrap around)
        int prevIndex = (currentIndex - 1 + g_validBikeCount) % g_validBikeCount;
        uint8_t prevId = g_validBikeIds[prevIndex];

        return SetBikeById(prevId);
    }

    bool ReloadBikeSettings() {
        if (!g_initialized || !g_loadBikeSettings) {
            LOG_ERROR("[BikeSwap] Not initialized or LoadBikeSettings unavailable");
            return false;
        }

        void* bikePtr = GetBikePointer();
        if (!bikePtr) {
            LOG_ERROR("[BikeSwap] No active bike found");
            return false;
        }

        LOG_VERBOSE("[BikeSwap] Reloading bike settings...");

        if (SafeLoadBikeSettings(bikePtr)) {
            LOG_INFO("[BikeSwap] Bike settings reloaded successfully!");
            return true;
        } else {
            LOG_ERROR("[BikeSwap] Exception in LoadBikeSettings");
            return false;
        }
    }

    // ============================================================================
    // Advanced Functions
    // ============================================================================

    void* GetBikeDataPointer(uint8_t bikeId) {
        if (!g_initialized) {
            return nullptr;
        }

        void* bikeDatabase = GetBikeDatabase();
        if (!bikeDatabase) {
            return nullptr;
        }

        return SafeBikeLookup(bikeDatabase, bikeId);
    }

    bool ForceFullBikeReload() {
        if (!g_initialized) {
            LOG_ERROR("[BikeSwap] Not initialized");
            return false;
        }

        void* bikePtr = GetBikePointer();
        if (!bikePtr) {
            LOG_ERROR("[BikeSwap] No active bike found");
            return false;
        }

        uint8_t currentId = GetCurrentBikeId();
        LOG_INFO("[BikeSwap] Performing full bike reload for " << GetBikeNameFromId(currentId) << "...");

        // Use respawn to trigger a full reload - this is the safest approach
        // The respawn transition gives the game a safe point to reload everything
        LOG_VERBOSE("[BikeSwap] Triggering respawn for full bike reload...");
        
        if (Respawn::RespawnAtCheckpoint()) {
            LOG_INFO("[BikeSwap] Full bike reload complete via respawn!");
            return true;
        }
        
        // Fallback if respawn fails: just reload settings
        if (!ReloadBikeSettings()) {
            return false;
        }

        // Try to reinitialize attachments if available
        if (g_initBikeAttachments && !IsBadCodePtr((FARPROC)g_initBikeAttachments)) {
            if (SafeInitBikeAttachments(bikePtr)) {
                LOG_VERBOSE("[BikeSwap] Bike attachments reinitialized");
            } else {
                LOG_WARNING("[BikeSwap] Exception in InitializeBikeAttachments (non-fatal)");
            }
        }

        LOG_INFO("[BikeSwap] Full bike reload complete!");
        return true;
    }

    void DebugDumpBikeState() {
        LOG_INFO("");
        LOG_INFO("=== BIKE SWAP DEBUG INFO ===");
        LOG_INFO("Initialized: " << (g_initialized ? "Yes" : "No"));
        LOG_INFO("ChangeBikeWithMesh available: " << ((g_changeBikeWithMesh && !IsBadCodePtr((FARPROC)g_changeBikeWithMesh)) ? "Yes" : "No"));
        
        void* bikePtr = GetBikePointer();
        if (bikePtr) {
            LOG_INFO("Bike Pointer: 0x" << std::hex << reinterpret_cast<uintptr_t>(bikePtr));
            
            uint8_t bikeId = GetCurrentBikeId();
            LOG_INFO("Current Bike ID: " << std::dec << (int)bikeId);
            LOG_INFO("Current Bike Name: " << GetBikeNameFromId(bikeId));

            // Read some additional fields for debugging
            uintptr_t bikePtrAddr = reinterpret_cast<uintptr_t>(bikePtr);
            
            if (!IsBadReadPtr((void*)(bikePtrAddr + BIKE_GFX_HASH_OFFSET), sizeof(uint32_t))) {
                uint32_t gfxHash = *reinterpret_cast<uint32_t*>(bikePtrAddr + BIKE_GFX_HASH_OFFSET);
                LOG_INFO("Bike GFX Hash: 0x" << std::hex << gfxHash);
            }
            
            if (!IsBadReadPtr((void*)(bikePtrAddr + BIKE_IS_REAL_OFFSET), sizeof(uint8_t))) {
                uint8_t isReal = *reinterpret_cast<uint8_t*>(bikePtrAddr + BIKE_IS_REAL_OFFSET);
                LOG_INFO("Is Real Bike: " << (isReal ? "Yes" : "No (Ghost/Replay)"));
            }

            // Dump appearance data
            BikeAppearanceData appearanceData;
            if (SafeReadAppearanceData(bikePtr, &appearanceData)) {
                LOG_INFO("Appearance Data @ 0x" << std::hex << (bikePtrAddr + BIKE_APPEARANCE_OFFSET));
                LOG_INFO("  field_0: " << std::dec << appearanceData.field_0);
                LOG_INFO("  field_2: " << appearanceData.field_2);
                LOG_INFO("  field_4: " << appearanceData.field_4);
            }
        } else {
            LOG_INFO("Bike Pointer: NULL (not in track?)");
        }

        void* bikeDb = GetBikeDatabase();
        if (bikeDb) {
            LOG_INFO("Bike Database: 0x" << std::hex << reinterpret_cast<uintptr_t>(bikeDb));
        } else {
            LOG_INFO("Bike Database: NULL");
        }

        LOG_INFO("");
        LOG_INFO("Available Bikes:");
        std::vector<BikeInfo> bikes = GetAvailableBikes();
        for (size_t i = 0; i < bikes.size(); i++) {
            const BikeInfo& bike = bikes[i];
            LOG_INFO("  ID " << std::dec << (int)bike.id << ": " << bike.name 
                     << (bike.available ? " [Available]" : " [Not Found]"));
        }
        
        LOG_INFO("============================");
        LOG_INFO("");
    }

    // ============================================================================
    // Hotkey Handler
    // ============================================================================

    static bool g_bracketLeftPressed = false;
    static bool g_bracketRightPressed = false;
    static bool g_backslashPressed = false;

    void CheckHotkey() {
        if (!g_initialized) {
            return;
        }

        // Process any pending bike swap (deferred execution)
        ProcessPendingBikeSwap();

        // [ key - Cycle to previous bike
        bool bracketLeftDown = (GetAsyncKeyState(VK_OEM_4) & 0x8000) != 0;
        if (bracketLeftDown && !g_bracketLeftPressed) {
            g_bracketLeftPressed = true;
            LOG_VERBOSE("[BikeSwap] '[' pressed - Cycling to previous bike");
            CyclePreviousBike();
        } else if (!bracketLeftDown) {
            g_bracketLeftPressed = false;
        }

        // ] key - Cycle to next bike
        bool bracketRightDown = (GetAsyncKeyState(VK_OEM_6) & 0x8000) != 0;
        if (bracketRightDown && !g_bracketRightPressed) {
            g_bracketRightPressed = true;
            LOG_VERBOSE("[BikeSwap] ']' pressed - Cycling to next bike");
            CycleNextBike();
        } else if (!bracketRightDown) {
            g_bracketRightPressed = false;
        }

        // \ key - Debug dump bike state
        bool backslashDown = (GetAsyncKeyState(VK_OEM_5) & 0x8000) != 0;
        if (backslashDown && !g_backslashPressed) {
            g_backslashPressed = true;
            DebugDumpBikeState();
        } else if (!backslashDown) {
            g_backslashPressed = false;
        }
    }

} // namespace BikeSwap
