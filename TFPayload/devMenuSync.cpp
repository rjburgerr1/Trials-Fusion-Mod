// Implementation of DevMenu <-> Game Memory synchronization
#include "pch.h"
#include "devMenuSync.h"
#include "devMenu.h"
#include "logging.h"
#include "base-address.h"

namespace DevMenuSync {
    static uintptr_t g_BaseAddress = 0;
    std::unordered_map<int, TweakableMemoryInfo> g_tweakableMemoryMap;

    // InitializeDevMenuData: Ghidra 0x00d648c0, RVA = 0x00d648c0 - 0x00700000 = 0x006648c0
    static constexpr uintptr_t INIT_DEV_MENU_DATA_RVA_UPLAY = 0x006648c0;

    // BuildTweakablesList: Ghidra 0x00d623e0, RVA = 0x00d623e0 - 0x00700000 = 0x006623e0
    static constexpr uintptr_t BUILD_TWEAKABLES_LIST_RVA_UPLAY = 0x006623e0;

    // g_DevMenuData: Ghidra 0x017552 30, RVA = 0x01755230 - 0x00700000 = 0x01055230
    static constexpr uintptr_t GLOBAL_DEV_MENU_DATA_RVA_UPLAY = 0x01055230;

    // InitializeDevMenuData: Ghidra 0x008e3360, RVA = 0x008e3360 - 0x140000 = 0x7a3360
    // MAPPED via CSV: Uplay 0x00d648c0 -> Steam 0x007a3360
    static constexpr uintptr_t INIT_DEV_MENU_DATA_RVA_STEAM = 0x007a3360;

    // BuildTweakablesList: FUN_007a0e80 (confirmed via getChildTweakablesForId -> FUN_00689c20 -> FUN_007a0e80)
    // Steam Ghidra: 0x007a0e80, RVA = 0x007a0e80 - 0x140000 = 0x00660e80
    // Verified by ActionScript callback registration and function signature match
    static constexpr uintptr_t BUILD_TWEAKABLES_LIST_RVA_STEAM = 0x00660e80;

    // g_DevMenuData: Multiple locations found
    // DAT_01197230 (from CSV mapping): RVA = 0x01057230 - gives valid devMenuData ptr
    // DAT_011939e8 (used by ActionScript callbacks): RVA = 0x010539e8 - gives NULL
    // Using CSV mapping since it has valid data
    static constexpr uintptr_t GLOBAL_DEV_MENU_DATA_RVA_STEAM = 0x01057230;

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

    typedef void* (__fastcall* InitializeDevMenuDataFunc)(int param_1);

    // ============================================================================
    // Low-level unsafe call wrappers (C-style functions for __try/__except)
    // ============================================================================

    // Safe wrapper for calling BuildTweakablesList
    static int SafeCallBuildTweakablesList(void* devMenuData, int* outputArray, int categoryId, uintptr_t funcAddr) {
        __try {
            __asm {
                mov ecx, devMenuData
                push categoryId
                push outputArray      // Push the pointer directly, not address-of
                call funcAddr
            }
            return 1; // Success
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return 0; // Failure
        }
    }

    // Safe wrapper for calling InitializeDevMenuData
    static void* SafeCallInitializeDevMenuData(uintptr_t funcAddr) {
        void* result = nullptr;
        __try {
            InitializeDevMenuDataFunc initFunc = (InitializeDevMenuDataFunc)funcAddr;
            result = initFunc(0);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            result = nullptr;
        }
        return result;
    }

    // Safe wrapper for reading a pointer
    static int SafeReadPointer(void** ptrAddr, void** outValue) {
        __try {
            *outValue = *ptrAddr;
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return 0;
        }
    }

    // Safe wrapper for reading int from memory
    static int SafeReadInt(void* address, int* outValue) {
        __try {
            *outValue = *(int*)address;
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return 0;
        }
    }

    // Safe wrapper for reading float from memory
    static int SafeReadFloat(void* address, float* outValue) {
        __try {
            *outValue = *(float*)address;
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return 0;
        }
    }

    // Safe wrapper for writing a pointer
    static int SafeWritePointer(void** ptrAddr, void* value) {
        __try {
            *ptrAddr = value;
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return 0;
        }
    }

    bool Initialize(uintptr_t baseAddress) {
        LOG_VERBOSE("[DevMenuSync] Initializing sync system...");

        if (baseAddress == 0) {
            LOG_ERROR("[DevMenuSync] Invalid base address!");
            return false;
        }

        g_BaseAddress = baseAddress;

        // Log version detection
        if (BaseAddress::IsSteamVersion()) {
            LOG_VERBOSE("[DevMenuSync] Steam version detected");
            LOG_VERBOSE("[DevMenuSync]   BuildTweakablesList RVA: 0x" << std::hex << BUILD_TWEAKABLES_LIST_RVA_STEAM);
            LOG_VERBOSE("[DevMenuSync]   InitDevMenuData RVA: 0x" << std::hex << INIT_DEV_MENU_DATA_RVA_STEAM);
            LOG_VERBOSE("[DevMenuSync]   GlobalDevMenuData RVA: 0x" << std::hex << GLOBAL_DEV_MENU_DATA_RVA_STEAM);
        }
        else {
            LOG_VERBOSE("[DevMenuSync] Uplay version detected");
            LOG_VERBOSE("[DevMenuSync]   BuildTweakablesList RVA: 0x" << std::hex << BUILD_TWEAKABLES_LIST_RVA_UPLAY);
            LOG_VERBOSE("[DevMenuSync]   InitDevMenuData RVA: 0x" << std::hex << INIT_DEV_MENU_DATA_RVA_UPLAY);
            LOG_VERBOSE("[DevMenuSync]   GlobalDevMenuData RVA: 0x" << std::hex << GLOBAL_DEV_MENU_DATA_RVA_UPLAY);
        }

        g_BaseAddress = baseAddress;

        if (!ScanGameMemory(baseAddress)) {
            LOG_ERROR("[DevMenuSync] Failed to scan game memory!");
            return false;
        }

        LOG_VERBOSE("[DevMenuSync] Sync system initialized! Found " << g_tweakableMemoryMap.size() << " tweakables.");
        return true;
    }

    void Shutdown() {
        g_tweakableMemoryMap.clear();
    }

    TweakableMemoryInfo* GetMemoryInfo(int tweakableId) {
        auto it = g_tweakableMemoryMap.find(tweakableId);
        if (it != g_tweakableMemoryMap.end()) {
            return &it->second;
        }
        return nullptr;
    }

    void DebugPrintTweakable(int id) {
        LOG_INFO("[DevMenuSync] === DEBUG for ID " << id << " ===");
        LOG_INFO("[DevMenuSync] Map size: " << g_tweakableMemoryMap.size());

        auto* info = GetMemoryInfo(id);
        if (!info) {
            LOG_WARNING("[DevMenuSync] ID " << id << " NOT FOUND in map!");
            return;
        }

        LOG_INFO("[DevMenuSync] ID " << id << " found:");
        LOG_INFO("[DevMenuSync]   valuePtr: 0x" << std::hex << (uintptr_t)info->valuePtr);
        LOG_INFO("[DevMenuSync]   type: " << std::dec << info->type);
        LOG_INFO("[DevMenuSync]   isValid: " << (info->isValid ? "true" : "false"));
    }

    // Recursive function to scan a category and all its children
    void ScanCategoryRecursive(void* devMenuData, uintptr_t buildTweakablesListAddr, int categoryId) {
        LOG_INFO("[DevMenuSync] >>> ScanCategoryRecursive ENTERED: categoryId=" << categoryId);

        int outputArray[3] = { 0, 2, 0 };
        void* arrayData = malloc(8);
        outputArray[2] = (int)arrayData;

        LOG_INFO("[DevMenuSync] About to call BuildTweakablesList...");
        LOG_INFO("[DevMenuSync]   devMenuData: 0x" << std::hex << (uintptr_t)devMenuData);
        LOG_INFO("[DevMenuSync]   buildTweakablesListAddr: 0x" << std::hex << buildTweakablesListAddr);
        LOG_INFO("[DevMenuSync]   categoryId: " << std::dec << categoryId);

        // Call BuildTweakablesList safely
        int success = SafeCallBuildTweakablesList(devMenuData, outputArray, categoryId, buildTweakablesListAddr);

        LOG_INFO("[DevMenuSync] BuildTweakablesList call completed, success=" << success);

        if (!success) {
            LOG_ERROR("[DevMenuSync] !!! CRASH/FAILURE in BuildTweakablesList! CategoryID: " << categoryId);
            LOG_ERROR("[DevMenuSync] devMenuData: 0x" << std::hex << (uintptr_t)devMenuData);
            LOG_ERROR("[DevMenuSync] buildTweakablesListAddr: 0x" << buildTweakablesListAddr);
            free(arrayData);
            return;
        }

        LOG_VERBOSE("[DevMenuSync] BuildTweakablesList returned: count=" << outputArray[0] << ", outputArray[1]=" << outputArray[1] << ", arrayPtr=0x" << std::hex << outputArray[2]);

        void** tweakablePointers = (void**)outputArray[2];

        for (int i = 0; i < outputArray[0]; i++) {
            void* tweakablePtr = tweakablePointers[i];

            if (tweakablePtr != nullptr) {
                unsigned int* data = (unsigned int*)tweakablePtr;
                int type = data[1];  // +0x04
                int id = data[2];     // +0x08

                LOG_VERBOSE("[DevMenuSync]   Found tweakable: id=" << std::dec << id << ", type=" << type << ", ptr=0x" << std::hex << (uintptr_t)tweakablePtr);

                // For non-folder types, store the memory location
                if (type >= 1 && type <= 3) {
                    void** valuePtr = (void**)(data + 0x1bc / 4);

                    TweakableMemoryInfo info;
                    info.valuePtr = *valuePtr;
                    info.type = type;
                    info.isValid = (*valuePtr != nullptr);

                    LOG_VERBOSE("[DevMenuSync]     valuePtr at offset 0x1bc: 0x" << std::hex << (uintptr_t)info.valuePtr << ", isValid=" << (info.isValid ? "true" : "false"));

                    g_tweakableMemoryMap[id] = info;
                }

                // If it's a folder, recursively scan it
                if (type == 0) {
                    ScanCategoryRecursive(devMenuData, buildTweakablesListAddr, id);
                }
            }
        }

        free(arrayData);
    }

    bool ScanGameMemory(uintptr_t baseAddress) {
        LOG_VERBOSE("[DevMenuSync] ScanGameMemory called with baseAddress: 0x" << std::hex << baseAddress);

        // CRITICAL: Log version detection first
        bool isSteam = BaseAddress::IsSteamVersion();
        LOG_INFO("[DevMenuSync] ========================================");
        LOG_INFO("[DevMenuSync] Version Detection: " << (isSteam ? "STEAM" : "UPLAY"));
        LOG_INFO("[DevMenuSync] ========================================");
        LOG_INFO("[DevMenuSync] Using RVAs:");
        LOG_INFO("[DevMenuSync]   GlobalDevMenuData: 0x" << std::hex << GetGlobalDevMenuDataRVA());
        LOG_INFO("[DevMenuSync]   InitDevMenuData: 0x" << std::hex << GetInitDevMenuDataRVA());
        LOG_INFO("[DevMenuSync]   BuildTweakablesList: 0x" << std::hex << GetBuildTweakablesListRVA());
        LOG_INFO("[DevMenuSync] ========================================");

        // Get pointer to global dev menu data using version-aware helper
        void** globalDevMenuDataPtr = (void**)(baseAddress + GetGlobalDevMenuDataRVA());

        LOG_VERBOSE("[DevMenuSync] Global dev menu data pointer at: 0x" << std::hex << (uintptr_t)globalDevMenuDataPtr);
        LOG_VERBOSE("[DevMenuSync] Calculated from baseAddress 0x" << std::hex << baseAddress << " + RVA 0x" << GetGlobalDevMenuDataRVA());

        void* devMenuData = nullptr;
        if (!SafeReadPointer(globalDevMenuDataPtr, &devMenuData)) {
            LOG_ERROR("[DevMenuSync] Failed to read global dev menu data pointer!");
            return false;
        }

        LOG_VERBOSE("[DevMenuSync] Read devMenuData ptr: 0x" << std::hex << (uintptr_t)devMenuData);

        if (devMenuData == nullptr) {
            LOG_VERBOSE("[DevMenuSync] devMenuData is NULL, calling InitializeDevMenuData...");
            uintptr_t initDevMenuDataAddr = baseAddress + GetInitDevMenuDataRVA();

            LOG_VERBOSE("[DevMenuSync] Calling InitializeDevMenuData at: 0x" << std::hex << initDevMenuDataAddr);

            devMenuData = SafeCallInitializeDevMenuData(initDevMenuDataAddr);

            if (devMenuData == nullptr) {
                LOG_ERROR("[DevMenuSync] CRASH or NULL return from InitializeDevMenuData!");
                return false;
            }

            LOG_VERBOSE("[DevMenuSync] InitializeDevMenuData returned: 0x" << std::hex << (uintptr_t)devMenuData);

            // Write back to global (with safety check)
            if (!SafeWritePointer(globalDevMenuDataPtr, devMenuData)) {
                LOG_ERROR("[DevMenuSync] Failed to write devMenuData back to global!");
            }
        }

        if (devMenuData == nullptr) {
            LOG_ERROR("[DevMenuSync] Failed to initialize dev menu data!");
            return false;
        }

        LOG_VERBOSE("[DevMenuSync] Dev menu data @ 0x" << std::hex << (uintptr_t)devMenuData);

        // Debug: dump first few values of devMenuData structure
        unsigned int* devMenuDataInts = (unsigned int*)devMenuData;
        LOG_VERBOSE("[DevMenuSync] devMenuData[0] (offset 0x00): 0x" << std::hex << devMenuDataInts[0]);
        LOG_VERBOSE("[DevMenuSync] devMenuData[1] (offset 0x04): 0x" << std::hex << devMenuDataInts[1]);
        LOG_VERBOSE("[DevMenuSync] devMenuData[2] (offset 0x08): 0x" << std::hex << devMenuDataInts[2]);
        LOG_VERBOSE("[DevMenuSync] devMenuData[3] (offset 0x0c): 0x" << std::hex << devMenuDataInts[3]);

        // Check what BuildTweakablesList would check for categoryId=0
        // It checks: *(*(devMenuData + 4 + categoryId*4) + 4) == 0
        void* ptrAtOffset4 = (void*)devMenuDataInts[1];  // devMenuData + 4
        LOG_VERBOSE("[DevMenuSync] Pointer at devMenuData+4: 0x" << std::hex << (uintptr_t)ptrAtOffset4);
        if (ptrAtOffset4 != nullptr) {
            unsigned int* innerData = (unsigned int*)ptrAtOffset4;
            LOG_VERBOSE("[DevMenuSync]   innerData[0] (offset 0x00): 0x" << std::hex << innerData[0]);
            LOG_VERBOSE("[DevMenuSync]   innerData[1] (offset 0x04): 0x" << std::hex << innerData[1]);
            LOG_VERBOSE("[DevMenuSync]   BuildTweakablesList check: *(ptr+4) == 0? " << (innerData[1] == 0 ? "YES (will process)" : "NO (will skip!)"));
        }

        uintptr_t buildTweakablesListAddr = baseAddress + GetBuildTweakablesListRVA();
        LOG_VERBOSE("[DevMenuSync] BuildTweakablesList function @ 0x" << std::hex << buildTweakablesListAddr);
        LOG_VERBOSE("[DevMenuSync] Using BuildTweakablesList RVA: 0x" << std::hex << GetBuildTweakablesListRVA());

        // Clear existing map
        g_tweakableMemoryMap.clear();

        LOG_INFO("[DevMenuSync] ===== STARTING RECURSIVE SCAN =====");
        LOG_INFO("[DevMenuSync] About to call ScanCategoryRecursive with:");
        LOG_INFO("[DevMenuSync]   devMenuData: 0x" << std::hex << (uintptr_t)devMenuData);
        LOG_INFO("[DevMenuSync]   buildTweakablesListAddr: 0x" << std::hex << buildTweakablesListAddr);
        LOG_INFO("[DevMenuSync]   categoryId: 0");
        LOG_INFO("[DevMenuSync] =====================================");

        // Start recursive scan from category 0 (top level)
        ScanCategoryRecursive(devMenuData, buildTweakablesListAddr, 0);

        LOG_INFO("[DevMenuSync] ===== SCAN COMPLETE =====");
        LOG_INFO("[DevMenuSync] Found " << std::dec << g_tweakableMemoryMap.size() << " tweakables in game memory.");
        LOG_INFO("[DevMenuSync] ==========================");
        return true;
    }

    void SyncFromGame() {
        if (!g_DevMenu) return;

        // Iterate through all tweakables in the ImGui menu and update them from game memory
        for (auto& pair : g_tweakableMemoryMap) {
            int id = pair.first;
            auto& memInfo = pair.second;

            if (!memInfo.isValid || !memInfo.valuePtr) continue;

            if (memInfo.type == 1) {  // Bool
                auto tweakable = g_DevMenu->GetBool(id);
                if (tweakable) {
                    int gameValue = 0;
                    if (SafeReadInt(memInfo.valuePtr, &gameValue)) {
                        tweakable->SetValue(gameValue != 0);
                    }
                    else {
                        memInfo.isValid = false;
                    }
                }
            }
            else if (memInfo.type == 2) {  // Int
                auto tweakable = g_DevMenu->GetInt(id);
                if (tweakable) {
                    int gameValue = 0;
                    if (SafeReadInt(memInfo.valuePtr, &gameValue)) {
                        tweakable->SetValue(gameValue);
                    }
                    else {
                        memInfo.isValid = false;
                    }
                }
            }
            else if (memInfo.type == 3) {  // Float
                auto tweakable = g_DevMenu->GetFloat(id);
                if (tweakable) {
                    float gameValue = 0.0f;
                    if (SafeReadFloat(memInfo.valuePtr, &gameValue)) {
                        tweakable->SetValue(gameValue);
                    }
                    else {
                        memInfo.isValid = false;
                    }
                }
            }
        }
    }

    void SyncToGame() {
        // This is handled by the onChange callbacks set up during initialization
        // See SetupSyncCallbacks() function
    }
}