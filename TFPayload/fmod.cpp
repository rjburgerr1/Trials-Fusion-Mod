#include "pch.h"
#include "fmod.h"
#include "logging.h"
#include "base-address.h"
#include <mutex>
#include <string>

namespace Fmod {
    typedef int (WINAPI* FMOD_EventSystem_GetCategoryFunc)(void* eventSystem, const char* name, void** category);
    typedef int (WINAPI* FMOD_EventSystem_GetVersionFunc)(void* eventSystem, unsigned int* version);
    typedef int (WINAPI* FMOD_EventCategory_SetVolumeFunc)(void* category, float volume);
    typedef int (WINAPI* FMOD_EventCategory_GetVolumeFunc)(void* category, float* volume);

    static constexpr uintptr_t SOUND_SYSTEM_FMOD_VTABLE_RVA_UPLAY = 0x00de7d70;
    static constexpr uintptr_t SOUND_SYSTEM_FMOD_VTABLE_RVA_STEAM = 0x00de8d38;

    static HMODULE g_fmodEventModule = nullptr;
    static uintptr_t g_baseAddress = 0;
    static void* g_soundSystem = nullptr;
    static void* g_eventSystem = nullptr;
    static void* g_masterCategory = nullptr;

    static bool g_initialized = false;
    static bool g_eventsMuted = false;
    static bool g_muteOnStartup = true;
    static bool g_restoreVolumeCaptured = false;
    static bool g_lastAppliedStateValid = false;
    static bool g_lastAppliedMuted = false;
    static float g_restoreVolume = 1.0f;
    static int g_lastResult = 0;
    static DWORD g_lastResolveAttemptMs = 0;
    static DWORD g_lastSoundSystemScanMs = 0;
    static DWORD g_lastEventSystemScanMs = 0;
    static DWORD g_lastSilentApplyMs = 0;
    static DWORD g_suppressApplyUntilMs = 0;
    static std::mutex g_stateMutex;

    static FMOD_EventSystem_GetCategoryFunc g_FMOD_EventSystem_GetCategory = nullptr;
    static FMOD_EventSystem_GetVersionFunc g_FMOD_EventSystem_GetVersion = nullptr;
    static FMOD_EventCategory_SetVolumeFunc g_FMOD_EventCategory_SetVolume = nullptr;
    static FMOD_EventCategory_GetVolumeFunc g_FMOD_EventCategory_GetVolume = nullptr;

    static FARPROC ResolveProc(HMODULE module, const char* undecorated, const char* decorated) {
        FARPROC proc = GetProcAddress(module, undecorated);
        if (!proc && decorated) {
            proc = GetProcAddress(module, decorated);
        }
        return proc;
    }

    static void ClearRuntimePointers() {
        g_soundSystem = nullptr;
        g_eventSystem = nullptr;
        g_masterCategory = nullptr;
        g_restoreVolumeCaptured = false;
        g_lastAppliedStateValid = false;
        g_lastSoundSystemScanMs = 0;
        g_lastEventSystemScanMs = 0;
        g_lastSilentApplyMs = 0;
    }

    static bool IsApplySuppressed() {
        if (g_suppressApplyUntilMs == 0) {
            return false;
        }

        if (static_cast<LONG>(g_suppressApplyUntilMs - GetTickCount()) > 0) {
            return true;
        }

        g_suppressApplyUntilMs = 0;
        return false;
    }

    static std::string GetConfigPath() {
        char path[MAX_PATH] = {};
        if (GetModuleFileNameA(nullptr, path, MAX_PATH) == 0) {
            return "tfpayload_fmod.ini";
        }

        std::string fullPath(path);
        size_t slash = fullPath.find_last_of("\\/");
        if (slash == std::string::npos) {
            return "tfpayload_fmod.ini";
        }

        return fullPath.substr(0, slash + 1) + "tfpayload_fmod.ini";
    }

    static void LoadConfig() {
        std::string path = GetConfigPath();
        g_muteOnStartup = GetPrivateProfileIntA("FMOD", "MuteOnStartup", 1, path.c_str()) != 0;
    }

    static void SaveConfig() {
        std::string path = GetConfigPath();
        WritePrivateProfileStringA("FMOD", "MuteOnStartup", g_muteOnStartup ? "1" : "0", path.c_str());
    }

    static bool ResolveFmodEventExports() {
        if (!g_fmodEventModule) {
            g_fmodEventModule = GetModuleHandleA("fmod_event.dll");
        }
        if (!g_fmodEventModule) {
            g_fmodEventModule = GetModuleHandleA("fmod_event_net.dll");
        }
        if (!g_fmodEventModule) {
            return false;
        }

        g_FMOD_EventSystem_GetCategory = reinterpret_cast<FMOD_EventSystem_GetCategoryFunc>(
            ResolveProc(g_fmodEventModule, "FMOD_EventSystem_GetCategory", "_FMOD_EventSystem_GetCategory@12"));
        g_FMOD_EventSystem_GetVersion = reinterpret_cast<FMOD_EventSystem_GetVersionFunc>(
            ResolveProc(g_fmodEventModule, "FMOD_EventSystem_GetVersion", "_FMOD_EventSystem_GetVersion@8"));
        g_FMOD_EventCategory_SetVolume = reinterpret_cast<FMOD_EventCategory_SetVolumeFunc>(
            ResolveProc(g_fmodEventModule, "FMOD_EventCategory_SetVolume", "_FMOD_EventCategory_SetVolume@8"));
        g_FMOD_EventCategory_GetVolume = reinterpret_cast<FMOD_EventCategory_GetVolumeFunc>(
            ResolveProc(g_fmodEventModule, "FMOD_EventCategory_GetVolume", "_FMOD_EventCategory_GetVolume@8"));

        return g_FMOD_EventSystem_GetCategory
            && g_FMOD_EventSystem_GetVersion
            && g_FMOD_EventCategory_SetVolume;
    }

    static bool SafeReadPointer(void* address, void** outValue) {
        __try {
            *outValue = *reinterpret_cast<void**>(address);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    static int SafeCallGetVersion(void* eventSystem, unsigned int* version) {
        if (!g_FMOD_EventSystem_GetVersion) {
            return -1;
        }

        __try {
            return g_FMOD_EventSystem_GetVersion(eventSystem, version);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return -1;
        }
    }

    static int SafeCallGetCategory(void* eventSystem, const char* name, void** category) {
        __try {
            return g_FMOD_EventSystem_GetCategory(eventSystem, name, category);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return -1;
        }
    }

    static int SafeCallSetVolume(void* category, float volume) {
        __try {
            return g_FMOD_EventCategory_SetVolume(category, volume);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return -1;
        }
    }

    static int SafeCallGetVolume(void* category, float* volume) {
        if (!g_FMOD_EventCategory_GetVolume) {
            return -1;
        }

        __try {
            return g_FMOD_EventCategory_GetVolume(category, volume);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return -1;
        }
    }

    static bool LooksLikeEventSystem(void* candidate) {
        if (!candidate || !ResolveFmodEventExports()) {
            return false;
        }

        unsigned int version = 0;
        int result = SafeCallGetVersion(candidate, &version);
        if (result != 0 || version == 0) {
            return false;
        }

        return (version & 0xffff0000) == 0x00040000;
    }

    static bool ResolveMasterCategory() {
        if (IsApplySuppressed()) {
            return false;
        }

        if (!g_eventSystem || !ResolveFmodEventExports()) {
            return false;
        }

        if (g_masterCategory) {
            return true;
        }

        static const char* categoryNames[] = { "master", "Master", "" };
        for (int i = 0; i < static_cast<int>(sizeof(categoryNames) / sizeof(categoryNames[0])); ++i) {
            void* category = nullptr;
            int result = SafeCallGetCategory(g_eventSystem, categoryNames[i], &category);
            g_lastResult = result;
            if (result == 0 && category) {
                g_masterCategory = category;
                g_lastAppliedStateValid = false;
                LOG_VERBOSE("[FMOD] Resolved event category '" << categoryNames[i] << "' at 0x"
                    << std::hex << (uintptr_t)g_masterCategory << std::dec);
                return true;
            }
        }

        return false;
    }

    static bool ApplyEventVolume(bool logChange) {
        if (IsApplySuppressed()) {
            return false;
        }

        if (!ResolveMasterCategory()) {
            return false;
        }

        if (!g_restoreVolumeCaptured) {
            float currentVolume = 1.0f;
            if (SafeCallGetVolume(g_masterCategory, &currentVolume) == 0 && currentVolume > 0.0f) {
                g_restoreVolume = currentVolume;
            }
            g_restoreVolumeCaptured = true;
        }

        const float volume = g_eventsMuted ? 0.0f : g_restoreVolume;
        int result = SafeCallSetVolume(g_masterCategory, volume);
        g_lastResult = result;
        if (result != 0) {
            LOG_WARNING("[FMOD] Failed to set event category volume, result=" << result);
            ClearRuntimePointers();
            g_suppressApplyUntilMs = GetTickCount() + 1000;
            return false;
        }

        bool changed = !g_lastAppliedStateValid || g_lastAppliedMuted != g_eventsMuted;
        g_lastAppliedStateValid = true;
        g_lastAppliedMuted = g_eventsMuted;

        if (logChange && changed) {
            LOG_INFO("[FMOD] Event volume " << (g_eventsMuted ? "muted" : "restored")
                << " (volume=" << volume << ")");
        }

        return true;
    }

    static void CaptureEventSystem(void* eventSystem, const char* source) {
        if (!eventSystem || g_eventSystem == eventSystem) {
            return;
        }

        g_eventSystem = eventSystem;
        g_masterCategory = nullptr;
        g_restoreVolumeCaptured = false;
        g_lastAppliedStateValid = false;
        LOG_VERBOSE("[FMOD] Captured EventSystem from " << source << " at 0x"
            << std::hex << (uintptr_t)eventSystem << std::dec);

        if (!IsApplySuppressed()) {
            ApplyEventVolume(true);
        }
    }

    static bool ScanSoundSystemForEventSystem() {
        if (!g_soundSystem || g_eventSystem || !ResolveFmodEventExports()) {
            return false;
        }

        for (int offset = 0; offset < 0x400; offset += 4) {
            void* candidate = nullptr;
            if (!SafeReadPointer(static_cast<unsigned char*>(g_soundSystem) + offset, &candidate)) {
                continue;
            }

            if (LooksLikeEventSystem(candidate)) {
                LOG_VERBOSE("[FMOD] Found EventSystem candidate at SoundSystemFMOD+0x"
                    << std::hex << offset << " ptr=0x" << (uintptr_t)candidate << std::dec);
                CaptureEventSystem(candidate, "SoundSystemFMOD field scan");
                return true;
            }
        }

        return false;
    }

    static bool IsReadableWritablePage(DWORD protect) {
        if ((protect & PAGE_GUARD) != 0 || (protect & PAGE_NOACCESS) != 0) {
            return false;
        }

        protect &= 0xff;
        return protect == PAGE_READWRITE
            || protect == PAGE_WRITECOPY
            || protect == PAGE_EXECUTE_READWRITE
            || protect == PAGE_EXECUTE_WRITECOPY;
    }

    static bool ScanMemoryForSoundSystem() {
        if (g_soundSystem || g_baseAddress == 0) {
            return false;
        }

        uintptr_t vtableRva = BaseAddress::IsSteamVersion()
            ? SOUND_SYSTEM_FMOD_VTABLE_RVA_STEAM
            : SOUND_SYSTEM_FMOD_VTABLE_RVA_UPLAY;
        uintptr_t vtable = g_baseAddress + vtableRva;

        SYSTEM_INFO systemInfo = {};
        GetSystemInfo(&systemInfo);

        unsigned char* address = static_cast<unsigned char*>(systemInfo.lpMinimumApplicationAddress);
        unsigned char* maxAddress = static_cast<unsigned char*>(systemInfo.lpMaximumApplicationAddress);

        while (address < maxAddress) {
            MEMORY_BASIC_INFORMATION mbi = {};
            if (VirtualQuery(address, &mbi, sizeof(mbi)) != sizeof(mbi)) {
                address += 0x1000;
                continue;
            }

            unsigned char* regionBase = static_cast<unsigned char*>(mbi.BaseAddress);
            unsigned char* regionEnd = regionBase + mbi.RegionSize;

            if (mbi.State == MEM_COMMIT && IsReadableWritablePage(mbi.Protect)) {
                for (unsigned char* p = regionBase; p + sizeof(uintptr_t) <= regionEnd; p += sizeof(uintptr_t)) {
                    void* valuePtr = nullptr;
                    if (!SafeReadPointer(p, &valuePtr)) {
                        p = regionEnd;
                        break;
                    }

                    if (reinterpret_cast<uintptr_t>(valuePtr) == vtable) {
                        g_soundSystem = p;
                        LOG_VERBOSE("[FMOD] Captured SoundSystemFMOD from vtable scan at 0x"
                            << std::hex << (uintptr_t)g_soundSystem << std::dec);
                        ScanSoundSystemForEventSystem();
                        return true;
                    }
                }
            }

            address = regionEnd;
        }

        return false;
    }

    bool Initialize(uintptr_t baseAddress) {
        std::lock_guard<std::mutex> lock(g_stateMutex);

        g_baseAddress = baseAddress;
        g_initialized = true;
        g_fmodEventModule = nullptr;
        ClearRuntimePointers();
        g_restoreVolume = 1.0f;
        g_lastResult = 0;
        g_lastResolveAttemptMs = 0;
        g_suppressApplyUntilMs = 0;

        LoadConfig();
        g_eventsMuted = g_muteOnStartup;

        if (!ResolveFmodEventExports()) {
            LOG_VERBOSE("[FMOD] fmod_event.dll not loaded yet; will retry from Update()");
            return true;
        }

        LOG_VERBOSE("[FMOD] Initialized; mute on startup is "
            << (g_muteOnStartup ? "enabled" : "disabled"));
        return true;
    }

    void Shutdown() {
        std::lock_guard<std::mutex> lock(g_stateMutex);

        if (g_eventsMuted) {
            g_eventsMuted = false;
            ApplyEventVolume(true);
        }

        g_initialized = false;
        ClearRuntimePointers();
    }

    void Update() {
        std::lock_guard<std::mutex> lock(g_stateMutex);

        if (!g_initialized) {
            return;
        }

        if (IsApplySuppressed()) {
            return;
        }

        DWORD now = GetTickCount();

        if (!g_fmodEventModule && (g_lastResolveAttemptMs == 0 || now - g_lastResolveAttemptMs >= 1000)) {
            g_lastResolveAttemptMs = now;
            ResolveFmodEventExports();
        }

        if (!g_soundSystem && (g_lastSoundSystemScanMs == 0 || now - g_lastSoundSystemScanMs >= 3000)) {
            g_lastSoundSystemScanMs = now;
            ScanMemoryForSoundSystem();
        }

        if (g_soundSystem && !g_eventSystem && (g_lastEventSystemScanMs == 0 || now - g_lastEventSystemScanMs >= 1000)) {
            g_lastEventSystemScanMs = now;
            ScanSoundSystemForEventSystem();
        }

        // Do not periodically re-assert through cached FMOD category pointers.
        // Bike reloads can invalidate those objects while the key monitor thread is running.
    }

    void InvalidateCachedPointers(DWORD suppressApplyMs) {
        std::lock_guard<std::mutex> lock(g_stateMutex);

        ClearRuntimePointers();
        if (suppressApplyMs != 0) {
            g_suppressApplyUntilMs = GetTickCount() + suppressApplyMs;
        }
        LOG_VERBOSE("[FMOD] Invalidated cached FMOD pointers"
            << (suppressApplyMs != 0 ? " with temporary apply suppression" : ""));
    }

    bool SetEventsMuted(bool muted) {
        std::lock_guard<std::mutex> lock(g_stateMutex);

        g_eventsMuted = muted;
        return ApplyEventVolume(true);
    }

    bool ToggleEventsMuted() {
        std::lock_guard<std::mutex> lock(g_stateMutex);

        g_eventsMuted = !g_eventsMuted;
        return ApplyEventVolume(true);
    }

    bool AreEventsMuted() {
        std::lock_guard<std::mutex> lock(g_stateMutex);

        return g_eventsMuted;
    }

    bool IsMuteOnStartupEnabled() {
        std::lock_guard<std::mutex> lock(g_stateMutex);

        return g_muteOnStartup;
    }

    void SetMuteOnStartupEnabled(bool enabled) {
        std::lock_guard<std::mutex> lock(g_stateMutex);

        g_muteOnStartup = enabled;
        SaveConfig();
    }

    bool ToggleMuteOnStartup() {
        std::lock_guard<std::mutex> lock(g_stateMutex);

        g_muteOnStartup = !g_muteOnStartup;
        SaveConfig();
        return g_muteOnStartup;
    }

    const char* GetStatusString() {
        std::lock_guard<std::mutex> lock(g_stateMutex);

        if (g_eventsMuted) {
            return g_masterCategory ? "Audio Events: OFF" : "Audio Events: OFF (pending)";
        }
        return g_masterCategory ? "Audio Events: ON" : "Audio Events: ON (pending)";
    }

    const char* GetStartupMuteStatusString() {
        std::lock_guard<std::mutex> lock(g_stateMutex);

        return g_muteOnStartup ? "Mute Audio on Startup: ON" : "Mute Audio on Startup: OFF";
    }

    int GetLastResult() {
        std::lock_guard<std::mutex> lock(g_stateMutex);

        return g_lastResult;
    }
}
