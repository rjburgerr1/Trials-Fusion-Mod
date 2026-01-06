#include "pch.h"
#include "camera.h"
#include "logging.h"
#include "keybindings.h"
#include <iostream>
#include <Windows.h>

namespace Camera {
    // RVA offsets from Ghidra analysis (base address 0x700000)
    static constexpr uintptr_t GLOBAL_STRUCT_RVA = 0x104b308;  // DAT_0174b308
    static constexpr uintptr_t SET_CAMERA_MODE_RVA = 0x44e890;  // SetCameraMode (0x00b4e890 - 0x00700000)
    static constexpr uintptr_t CYCLE_CAMERA_MODE_RVA = 0x44e8e0;  // CycleHUD (0x00b4e8e0 - 0x00700000)
    static constexpr uintptr_t CAMERA_ACTIVE_FLAG_RVA = 0xfd4f1c;  // DAT_016d4f1c - 0x00700000
    
    // Camera object offsets
    // There are TWO mode systems:
    //   - 0x60 with callback at 0x10: Used by SetCameraMode (initialization)
    //   - 0x64 with callback at 0x28: Used by CycleHUD (gameplay cycling)
    static constexpr uintptr_t CAMERA_MODE_INIT_OFFSET = 0x60;   // SetCameraMode reads/writes this
    static constexpr uintptr_t CAMERA_MODE_CYCLE_OFFSET = 0x64;  // CycleHUD reads/writes this (100 decimal)
    static constexpr uintptr_t CAMERA_CALLBACK_INIT_OFFSET = 0x10;  // Callback for SetCameraMode
    static constexpr uintptr_t CAMERA_CALLBACK_CYCLE_OFFSET = 0x28; // Callback for CycleHUD
    
    // Function pointer types
    typedef void(__thiscall* SetCameraModeFunc)(void* thisPtr, int mode);
    typedef void(__fastcall* CycleHUDFunc)(void* thisPtr);

    // State tracking
    static bool g_initialized = false;
    static uintptr_t g_baseAddress = 0;
    static void** g_globalStructPtr = nullptr;
    static char* g_cameraActiveFlag = nullptr;
    static SetCameraModeFunc g_setCameraModeFunc = nullptr;
    static CycleHUDFunc g_CycleHUDFunc = nullptr;

    // ============================================================================
    // SEH-safe wrapper functions (no C++ objects allowed in these)
    // ============================================================================

    static bool CallSetCameraModeInternal(void* cameraPtr, int mode) {
        __try {
            g_setCameraModeFunc(cameraPtr, mode);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    static bool CallCycleHUDInternal(void* cameraPtr) {
        __try {
            g_CycleHUDFunc(cameraPtr);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    static bool CallModeCallbackInternal(void* funcPtr, int mode) {
        __try {
            typedef void(__cdecl* ModeCallbackFunc)(int mode);
            ModeCallbackFunc callback = reinterpret_cast<ModeCallbackFunc>(funcPtr);
            callback(mode);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    // Helper functions
    static void* GetCameraObject() {
        
        if (!g_globalStructPtr) {
            LOG_ERROR("[Camera] g_globalStructPtr is null");
            return nullptr;
        }
        
        if (IsBadReadPtr(g_globalStructPtr, sizeof(void*))) {
            LOG_ERROR("[Camera] g_globalStructPtr is unreadable at 0x" << std::hex << reinterpret_cast<uintptr_t>(g_globalStructPtr));
            return nullptr;
        }

        // Step 1: *DAT_0174b308
        void* step1 = *g_globalStructPtr;
        if (!step1) {
            LOG_ERROR("[Camera] Step1 (*g_globalStructPtr) is null");
            return nullptr;
        }
        
        if (IsBadReadPtr(step1, 0x178)) {
            LOG_ERROR("[Camera] Step1 is unreadable at 0x" << std::hex << reinterpret_cast<uintptr_t>(step1));
            return nullptr;
        }

        // Step 2: *(step1 + 0x174)
        uintptr_t step2Addr = reinterpret_cast<uintptr_t>(step1) + 0x174;
        void* step2 = *reinterpret_cast<void**>(step2Addr);
        if (!step2) {
            LOG_ERROR("[Camera] Step2 (*(step1 + 0x174)) is null. step1=0x" << std::hex << reinterpret_cast<uintptr_t>(step1));
            return nullptr;
        }
        
        if (IsBadReadPtr(step2, 0x400)) {
            LOG_ERROR("[Camera] Step2 is unreadable at 0x" << std::hex << reinterpret_cast<uintptr_t>(step2));
            return nullptr;
        }

        // Step 3: step2 + 0x328
        uintptr_t cameraAddr = reinterpret_cast<uintptr_t>(step2) + 0x328;
        void* cameraObj = reinterpret_cast<void*>(cameraAddr);
        
        if (IsBadReadPtr(cameraObj, 0x100)) {
            LOG_ERROR("[Camera] cameraObj is unreadable at 0x" << std::hex << cameraAddr);
            return nullptr;
        }
        
        return cameraObj;
    }

    static int GetCameraValueAtOffset(uintptr_t offset) {
        void* cameraObj = GetCameraObject();
        if (!cameraObj) {
            return -1;
        }

        uintptr_t addr = reinterpret_cast<uintptr_t>(cameraObj) + offset;
        if (IsBadReadPtr((void*)addr, sizeof(int))) {
            return -1;
        }

        return *reinterpret_cast<int*>(addr);
    }

    static bool SetCameraValueAtOffset(uintptr_t offset, int value) {
        void* cameraObj = GetCameraObject();
        if (!cameraObj) {
            return false;
        }

        uintptr_t addr = reinterpret_cast<uintptr_t>(cameraObj) + offset;
        if (IsBadWritePtr((void*)addr, sizeof(int))) {
            return false;
        }

        *reinterpret_cast<int*>(addr) = value;
        return true;
    }

    static void* GetCameraPointerAtOffset(uintptr_t offset) {
        void* cameraObj = GetCameraObject();
        if (!cameraObj) {
            return nullptr;
        }

        uintptr_t addr = reinterpret_cast<uintptr_t>(cameraObj) + offset;
        if (IsBadReadPtr((void*)addr, sizeof(void*))) {
            return nullptr;
        }

        return *reinterpret_cast<void**>(addr);
    }

    // ============================================================================
    // Public API
    // ============================================================================

    bool Initialize(uintptr_t baseAddress) {
        if (g_initialized) {
            LOG_WARNING("[Camera] Already initialized");
            return true;
        }

        g_baseAddress = baseAddress;

        g_cameraActiveFlag = reinterpret_cast<char*>(baseAddress + CAMERA_ACTIVE_FLAG_RVA);
        if (IsBadReadPtr(g_cameraActiveFlag, sizeof(char))) {
            LOG_ERROR("[Camera] Invalid camera active flag pointer");
            return false;
        }

        g_globalStructPtr = reinterpret_cast<void**>(baseAddress + GLOBAL_STRUCT_RVA);
        if (IsBadReadPtr(g_globalStructPtr, sizeof(void*))) {
            LOG_ERROR("[Camera] Invalid global structure pointer");
            return false;
        }

        g_setCameraModeFunc = reinterpret_cast<SetCameraModeFunc>(baseAddress + SET_CAMERA_MODE_RVA);
        if (IsBadReadPtr(g_setCameraModeFunc, 1)) {
            LOG_ERROR("[Camera] Invalid SetCameraMode function pointer");
            return false;
        }

        g_CycleHUDFunc = reinterpret_cast<CycleHUDFunc>(baseAddress + CYCLE_CAMERA_MODE_RVA);
        if (IsBadReadPtr(g_CycleHUDFunc, 1)) {
            LOG_ERROR("[Camera] Invalid CycleHUD function pointer");
            return false;
        }

        g_initialized = true;
        
        void* initCallbackPtr = GetCameraPointerAtOffset(CAMERA_CALLBACK_INIT_OFFSET);
        void* cycleCallbackPtr = GetCameraPointerAtOffset(CAMERA_CALLBACK_CYCLE_OFFSET);
        int initMode = GetCameraValueAtOffset(CAMERA_MODE_INIT_OFFSET);
        int cycleMode = GetCameraValueAtOffset(CAMERA_MODE_CYCLE_OFFSET);
        
        LOG_VERBOSE("[Camera] System initialized");
        LOG_VERBOSE("[Camera]   Init mode (0x60): " << initMode << ", callback (0x10): 0x" << std::hex << reinterpret_cast<uintptr_t>(initCallbackPtr));
        LOG_VERBOSE("[Camera]   Cycle mode (0x64): " << std::dec << cycleMode << ", callback (0x28): 0x" << std::hex << reinterpret_cast<uintptr_t>(cycleCallbackPtr));
        
        if (cycleCallbackPtr == nullptr) {
            LOG_WARNING("[Camera] Cycle callback is NULL - mode cycling may not work until gameplay starts!");
        }
        
        return true;
    }

    void Shutdown() {
        if (!g_initialized) {
            return;
        }

        LOG_VERBOSE("[Camera] Shutting down");
        g_initialized = false;
        g_baseAddress = 0;
        g_globalStructPtr = nullptr;
        g_cameraActiveFlag = nullptr;
        g_setCameraModeFunc = nullptr;
        g_CycleHUDFunc = nullptr;
    }

    bool SetMode(int mode) {
        if (!g_initialized) {
            LOG_ERROR("[Camera] Not initialized");
            return false;
        }

        if (!g_cameraActiveFlag || IsBadReadPtr(g_cameraActiveFlag, sizeof(char))) {
            LOG_ERROR("[Camera] Camera active flag is invalid");
            return false;
        }

        if (*g_cameraActiveFlag == 0) {
            LOG_WARNING("[Camera] Camera system is not active (not in gameplay?)");
            return false;
        }

        if (mode < 0 || mode > 2) {
            LOG_ERROR("[Camera] Invalid camera mode: " << mode << " (valid: 0-2)");
            return false;
        }

        void* cameraObj = GetCameraObject();
        if (!cameraObj) {
            LOG_ERROR("[Camera] Failed to get camera object");
            return false;
        }

        // Check the CYCLE callback (0x28) - this is what the in-game keybind uses
        void* cycleCallbackPtr = GetCameraPointerAtOffset(CAMERA_CALLBACK_CYCLE_OFFSET);
        int beforeCycleMode = GetCameraValueAtOffset(CAMERA_MODE_CYCLE_OFFSET);
        int beforeInitMode = GetCameraValueAtOffset(CAMERA_MODE_INIT_OFFSET);

        LOG_VERBOSE("[Camera] Attempting to set mode to " << mode);
        LOG_VERBOSE("[Camera]   Before: cycleMode(0x64)=" << beforeCycleMode << ", initMode(0x60)=" << beforeInitMode);
        LOG_VERBOSE("[Camera]   Cycle callback (0x28): 0x" << std::hex << reinterpret_cast<uintptr_t>(cycleCallbackPtr));

        if (cycleCallbackPtr == nullptr) {
            LOG_ERROR("[Camera] Cannot switch mode - cycle callback pointer (0x28) is NULL!");
            LOG_ERROR("[Camera] This means the camera mode switch handler is not registered.");
            return false;
        }

        // Get the callback function pointer
        uintptr_t callbackObjAddr = reinterpret_cast<uintptr_t>(cycleCallbackPtr);
        if (IsBadReadPtr((void*)callbackObjAddr, sizeof(void*))) {
            LOG_ERROR("[Camera] Cannot read callback object");
            return false;
        }

        void* vtablePtr = *reinterpret_cast<void**>(callbackObjAddr);
        if (!vtablePtr || IsBadReadPtr(vtablePtr, 8)) {
            LOG_ERROR("[Camera] Invalid callback vtable");
            return false;
        }

        // Get the function at vtable+4 (the actual mode switch function)
        uintptr_t vtableAddr = reinterpret_cast<uintptr_t>(vtablePtr);
        void* switchFunc = *reinterpret_cast<void**>(vtableAddr + 4);
        if (!switchFunc || IsBadReadPtr(switchFunc, 1)) {
            LOG_ERROR("[Camera] Invalid switch function pointer");
            return false;
        }

        LOG_VERBOSE("[Camera]   Switch function: 0x" << std::hex << reinterpret_cast<uintptr_t>(switchFunc));

        // Set the mode value at 0x64
        if (!SetCameraValueAtOffset(CAMERA_MODE_CYCLE_OFFSET, mode)) {
            LOG_ERROR("[Camera] Failed to set mode value at 0x64");
            return false;
        }

        // Call the callback function with the mode
        if (!CallModeCallbackInternal(switchFunc, mode)) {
            LOG_ERROR("[Camera] Exception calling mode switch callback");
            return false;
        }

        int afterCycleMode = GetCameraValueAtOffset(CAMERA_MODE_CYCLE_OFFSET);
        int afterInitMode = GetCameraValueAtOffset(CAMERA_MODE_INIT_OFFSET);
        
        const char* modeNames[] = { "Follow Camera", "Automatic Camera", "Free Camera" };
        LOG_VERBOSE("[Camera]   After: cycleMode(0x64)=" << std::dec << afterCycleMode << " (" << modeNames[afterCycleMode] << "), initMode(0x60)=" << afterInitMode);
        
        return true;
    }

    bool CycleMode() {
        if (!g_initialized) {
            LOG_ERROR("[Camera] Not initialized");
            return false;
        }

        if (!g_cameraActiveFlag || IsBadReadPtr(g_cameraActiveFlag, sizeof(char))) {
            LOG_ERROR("[Camera] Camera active flag is invalid");
            return false;
        }

        if (*g_cameraActiveFlag == 0) {
            LOG_WARNING("[Camera] Camera system is not active (DAT_016d4f1c == 0)");
            return false;
        }

        // Replicate exact game logic:
        //   MOV ECX, dword ptr [0x0174b308]    ; ECX = *DAT_0174b308
        //   MOV ECX, dword ptr [ECX + 0x174]   ; ECX = *(ECX + 0x174)
        //   ADD ECX, 0x328                      ; ECX += 0x328
        //   CALL CycleHUD

        if (!g_globalStructPtr || IsBadReadPtr(g_globalStructPtr, sizeof(void*))) {
            LOG_ERROR("[Camera] g_globalStructPtr invalid");
            return false;
        }

        void* step1 = *g_globalStructPtr;
        LOG_VERBOSE("[Camera] step1 = *g_globalStructPtr = 0x" << std::hex << reinterpret_cast<uintptr_t>(step1));
        
        if (!step1 || IsBadReadPtr(step1, 0x178)) {
            LOG_ERROR("[Camera] step1 is null or unreadable");
            return false;
        }

        uintptr_t step2Addr = reinterpret_cast<uintptr_t>(step1) + 0x174;
        void* step2 = *reinterpret_cast<void**>(step2Addr);
        LOG_VERBOSE("[Camera] step2 = *(step1 + 0x174) = 0x" << std::hex << reinterpret_cast<uintptr_t>(step2));
        
        if (!step2 || IsBadReadPtr(step2, 0x400)) {
            LOG_ERROR("[Camera] step2 is null or unreadable");
            return false;
        }

        uintptr_t cameraObj = reinterpret_cast<uintptr_t>(step2) + 0x328;
        LOG_VERBOSE("[Camera] cameraObj = step2 + 0x328 = 0x" << std::hex << cameraObj);

        // Check callback at 0x28 before calling
        if (IsBadReadPtr(reinterpret_cast<void*>(cameraObj + 0x28), sizeof(void*))) {
            LOG_ERROR("[Camera] Cannot read callback pointer at cameraObj + 0x28");
            return false;
        }
        
        void* callbackPtr = *reinterpret_cast<void**>(cameraObj + 0x28);
        LOG_VERBOSE("[Camera] callback (0x28) = 0x" << std::hex << reinterpret_cast<uintptr_t>(callbackPtr));
        
        if (callbackPtr == nullptr) {
            LOG_ERROR("[Camera] Callback pointer is NULL - camera mode switching not available");
            return false;
        }

        int beforeMode = -1;
        if (!IsBadReadPtr(reinterpret_cast<void*>(cameraObj + 0x64), sizeof(int))) {
            beforeMode = *reinterpret_cast<int*>(cameraObj + 0x64);
        }
        LOG_VERBOSE("[Camera] Before mode (0x64) = " << std::dec << beforeMode);

        // Call CycleHUD with cameraObj in ECX (fastcall)
        LOG_VERBOSE("[Camera] Calling CycleHUD...");
        if (!CallCycleHUDInternal(reinterpret_cast<void*>(cameraObj))) {
            LOG_ERROR("[Camera] Exception calling CycleHUD");
            return false;
        }

        int afterMode = -1;
        if (!IsBadReadPtr(reinterpret_cast<void*>(cameraObj + 0x64), sizeof(int))) {
            afterMode = *reinterpret_cast<int*>(cameraObj + 0x64);
        }
        
        const char* modeNames[] = { "Follow Camera", "Automatic Camera", "Free Camera" };
        const char* modeName = (afterMode >= 0 && afterMode <= 2) ? modeNames[afterMode] : "Unknown";
        LOG_VERBOSE("[Camera] After mode (0x64) = " << std::dec << afterMode << " (" << modeName << ")");

        return true;
    }

    void CheckHotkey() {
        if (!g_initialized) {
            return;
        }

        if (!g_cameraActiveFlag || IsBadReadPtr(g_cameraActiveFlag, sizeof(char)) || *g_cameraActiveFlag == 0) {
            return;
        }

        // Use keybinding system to cycle camera mode
        if (Keybindings::IsActionPressed(Keybindings::Action::CycleHUD)) {
            CycleMode();
        }
    }
}
