// devMenuSync.cpp
// Implementation of DevMenu <-> Game Memory synchronization
#include "pch.h"
#include "devMenuSync.h"
#include "devMenu.h"
#include "logging.h"

namespace DevMenuSync {
    std::unordered_map<int, TweakableMemoryInfo> g_tweakableMemoryMap;

    // Game addresses from devTweaks.cpp
    const uintptr_t INIT_DEV_MENU_DATA_ADDR = 0x00cef440;
    const uintptr_t BUILD_TWEAKABLES_LIST_ADDR = 0x00d623e0;
    const uintptr_t GLOBAL_DEV_MENU_DATA = 0x01755230;

    typedef void* (__fastcall* InitializeDevMenuDataFunc)(int param_1);

    bool Initialize() {
        LOG_VERBOSE("[DevMenuSync] Initializing sync system...");
        
        if (!ScanGameMemory()) {
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

    // Recursive function to scan a category and all its children
    void ScanCategoryRecursive(void* devMenuData, uintptr_t buildTweakablesListAddr, int categoryId) {
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

        void** tweakablePointers = (void**)outputArray[2];

        for (int i = 0; i < outputArray[0]; i++) {
            void* tweakablePtr = tweakablePointers[i];

            if (tweakablePtr != nullptr) {
                unsigned int* data = (unsigned int*)tweakablePtr;
                int type = data[1];  // +0x04
                int id = data[2];     // +0x08

                // For non-folder types, store the memory location
                if (type >= 1 && type <= 3) {
                    void** valuePtr = (void**)(data + 0x1bc/4);
                    
                    TweakableMemoryInfo info;
                    info.valuePtr = *valuePtr;
                    info.type = type;
                    info.isValid = (*valuePtr != nullptr);

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

    bool ScanGameMemory() {
        LOG_VERBOSE("[DevMenuSync] Scanning game memory for tweakables...");

        uintptr_t baseAddress = (uintptr_t)GetModuleHandle(NULL);

        // Get pointer to global dev menu data
        void** globalDevMenuDataPtr = (void**)(baseAddress + GLOBAL_DEV_MENU_DATA - 0x700000);
        void* devMenuData = *globalDevMenuDataPtr;

        if (devMenuData == nullptr) {
            LOG_VERBOSE("[DevMenuSync] Initializing dev menu data...");
            InitializeDevMenuDataFunc initDevMenuData = (InitializeDevMenuDataFunc)(baseAddress + INIT_DEV_MENU_DATA_ADDR - 0x700000);
            devMenuData = initDevMenuData(0);
            *globalDevMenuDataPtr = devMenuData;
        }

        if (devMenuData == nullptr) {
            LOG_ERROR("[DevMenuSync] Failed to initialize dev menu data!");
            return false;
        }

        LOG_VERBOSE("[DevMenuSync] Dev menu data @ 0x" << std::hex << (uintptr_t)devMenuData);

        uintptr_t buildTweakablesListAddr = baseAddress + BUILD_TWEAKABLES_LIST_ADDR - 0x700000;

        // Clear existing map
        g_tweakableMemoryMap.clear();

        // Start recursive scan from category 0 (top level)
        ScanCategoryRecursive(devMenuData, buildTweakablesListAddr, 0);

        LOG_VERBOSE("[DevMenuSync] Scan complete! Found " << std::dec << g_tweakableMemoryMap.size() << " tweakables in game memory.");
        return true;
    }

    // Helper function to safely read from game memory
    template<typename T>
    bool SafeReadMemory(void* address, T& outValue) {
        __try {
            outValue = *(T*)address;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
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
                    if (SafeReadMemory(memInfo.valuePtr, gameValue)) {
                        tweakable->SetValue(gameValue != 0);
                    } else {
                        memInfo.isValid = false;
                    }
                }
            }
            else if (memInfo.type == 2) {  // Int
                auto tweakable = g_DevMenu->GetInt(id);
                if (tweakable) {
                    int gameValue = 0;
                    if (SafeReadMemory(memInfo.valuePtr, gameValue)) {
                        tweakable->SetValue(gameValue);
                    } else {
                        memInfo.isValid = false;
                    }
                }
            }
            else if (memInfo.type == 3) {  // Float
                auto tweakable = g_DevMenu->GetFloat(id);
                if (tweakable) {
                    float gameValue = 0.0f;
                    if (SafeReadMemory(memInfo.valuePtr, gameValue)) {
                        tweakable->SetValue(gameValue);
                    } else {
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
