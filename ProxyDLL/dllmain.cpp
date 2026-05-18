#include <Windows.h>
#include <winuser.h>
#include "ProxyDbgcore.h"
#include "Overlay.h"
#include "SkipIntro.h"
#include "logging.h"
#include <TlHelp32.h>
#include <stdio.h>
#include <cstring>
#include <string>

extern "C" IMAGE_DOS_HEADER __ImageBase;

// Debug Console
void AllocateConsole()
{
#ifdef RELEASE_AUTOLOAD_MODE
    // In RELEASE_AUTOLOAD_MODE, don't allocate console
    return;
#endif
    // Check if console already exists
    HWND existingConsole = GetConsoleWindow();
    if (existingConsole) {
        LOG_VERBOSE("[Console] Console already exists, reusing it");
        
        // Reopen stdout/stderr/stdin to the existing console
        FILE* fDummy;
        freopen_s(&fDummy, "CONOUT$", "w", stdout);
        freopen_s(&fDummy, "CONOUT$", "w", stderr);
        freopen_s(&fDummy, "CONIN$", "r", stdin);
        
        // Make sure it's visible
        ShowWindow(existingConsole, SW_SHOW);
        return;
    }
    
    LOG_VERBOSE("[Console] Creating new console...");
    
    if (!AllocConsole()) {
        LOG_ERROR("[Console] AllocConsole FAILED!");
        return;
    }
    
    FILE* fDummy;
    freopen_s(&fDummy, "CONOUT$", "w", stdout);
    freopen_s(&fDummy, "CONOUT$", "w", stderr);
    freopen_s(&fDummy, "CONIN$", "r", stdin);

    HWND consoleWindow = GetConsoleWindow();
    if (consoleWindow)
    {
        LOG_INFO("[Console] Console created successfully");
        
        POINT pt = { -1, 0 };
        HMONITOR hMonitor = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);

        MONITORINFO mi;
        mi.cbSize = sizeof(MONITORINFO);
        GetMonitorInfo(hMonitor, &mi);

        int monitorLeft = mi.rcWork.left;
        int monitorTop = mi.rcWork.top;
        int monitorWidth = mi.rcWork.right - mi.rcWork.left;
        int monitorHeight = mi.rcWork.bottom - mi.rcWork.top;

        int consoleWidth = monitorWidth / 2;
        int consoleHeight = monitorHeight;
        int consoleX = monitorLeft + (monitorWidth / 2);
        int consoleY = monitorTop;

        SetWindowPos(consoleWindow, HWND_TOP, consoleX, consoleY, consoleWidth, consoleHeight, SWP_SHOWWINDOW);
        
        // Set console title to identify it
        SetConsoleTitleA("RJ's Trials Fusion Mod - Debug Console");
    } else {
        LOG_ERROR("[Console] Failed to get console window handle");
    }
}

DWORD GetProcessID(LPCTSTR ProcessName)
{
    HANDLE hsnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

    PROCESSENTRY32 pt;
    pt.dwSize = sizeof(PROCESSENTRY32);

    if (Process32First(hsnap, &pt))
    {
        do
        {
            if (!lstrcmpi(pt.szExeFile, ProcessName))
            {
                CloseHandle(hsnap);
                return pt.th32ProcessID;
            }
        } while (Process32Next(hsnap, &pt));
    }

    CloseHandle(hsnap);
    return 0;
}

DWORD_PTR GetModuleBaseAddress(DWORD processID, const wchar_t* moduleName) {
    DWORD_PTR baseAddress = 0;
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, processID);

    if (hSnapshot != INVALID_HANDLE_VALUE) {
        MODULEENTRY32 moduleEntry;
        moduleEntry.dwSize = sizeof(MODULEENTRY32);

        if (Module32First(hSnapshot, &moduleEntry)) {
            do {
                if (!_wcsicmp(moduleEntry.szModule, moduleName)) {
                    baseAddress = (DWORD_PTR)moduleEntry.modBaseAddr;
                    break;
                }
            } while (Module32Next(hSnapshot, &moduleEntry));
        }
        CloseHandle(hSnapshot);
    }
    return baseAddress;
}

static bool isLoaded = false;
HMODULE hPayload = nullptr;
static constexpr const char* DEV_UNLOAD_TRIGGER = "tfpayload_dev_unload.trigger";
static constexpr const char* DEV_LOAD_TRIGGER = "tfpayload_dev_load.trigger";
#ifdef DEVELOPMENT_MODE
static bool pendingDevelopmentLoad = false;
#endif

void LoadTFPayload()
{
    if (hPayload == nullptr)
    {
        SetEnvironmentVariableA("TFPAYLOAD_PROXY_LOAD", "1");
        
        hPayload = LoadLibraryA("TFPayload.dll");
        
        if (hPayload != nullptr)
        {
            auto manualInit = (void(*)())GetProcAddress(hPayload, "ManualInitialize");
            if (manualInit)
            {
                manualInit();
                isLoaded = true;
                LOG_INFO("TFPayload loaded and initialized manually");
            }
        }
        else
        {
            LOG_ERROR("Failed to load TFPayload.dll: " << GetLastError());
        }
        
        SetEnvironmentVariableA("TFPAYLOAD_PROXY_LOAD", nullptr);
    }
}

#ifdef DEVELOPMENT_MODE
// Development mode - manual unload with F1

static std::string GetProxyDirectory()
{
    char modulePath[MAX_PATH] = {};
    DWORD length = GetModuleFileNameA(reinterpret_cast<HMODULE>(&__ImageBase), modulePath, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return "";
    }

    char* lastSlash = strrchr(modulePath, '\\');
    if (lastSlash == nullptr) {
        return "";
    }

    *(lastSlash + 1) = '\0';
    return modulePath;
}

static bool ConsumeTriggerFile(const char* fileName)
{
    std::string path = GetProxyDirectory() + fileName;
    DWORD attributes = GetFileAttributesA(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        return false;
    }

    if (!DeleteFileA(path.c_str())) {
        LOG_ERROR("Failed to delete dev trigger " << path << ": " << GetLastError());
    }

    return true;
}

void UnloadTFPayload()
{
    if (hPayload != nullptr)
    {
        auto manualShutdown = (void(*)())GetProcAddress(hPayload, "ManualShutdown");
        if (manualShutdown)
        {
            manualShutdown();
        }
        
        FreeLibrary(hPayload);
        hPayload = nullptr;
        isLoaded = false;
        LOG_INFO("TFPayload unloaded");
    }
}

void CheckDevelopmentReloadTriggers()
{
    if (ConsumeTriggerFile(DEV_UNLOAD_TRIGGER)) {
        LOG_INFO("Development reload: unload requested by build trigger");
        pendingDevelopmentLoad = false;
        if (isLoaded) {
            UnloadTFPayload();
        }
    }

    if (ConsumeTriggerFile(DEV_LOAD_TRIGGER)) {
        pendingDevelopmentLoad = true;
        LOG_INFO("Development reload: load requested by build trigger; waiting for game render readiness");
    }

    if (pendingDevelopmentLoad && !isLoaded && IsGameRenderReady()) {
        pendingDevelopmentLoad = false;
        LOG_INFO("Development reload: game render ready; loading payload");
        LoadTFPayload();
    }
    else if (pendingDevelopmentLoad && isLoaded) {
        pendingDevelopmentLoad = false;
    }
}

void CheckDevelopmentStartupAutoload(bool& autoLoadTriggered)
{
    if (!autoLoadTriggered && !isLoaded && IsGameRenderReady()) {
        autoLoadTriggered = true;
        if (ConsumeTriggerFile(DEV_LOAD_TRIGGER)) {
            LOG_INFO("Development mode: game render ready; auto-triggering payload load from build trigger.");
            LoadTFPayload();
            LOG_INFO(isLoaded ? "Development mode: TFPayload loaded automatically." : "Development mode: TFPayload auto-load failed.");
        }
    }
}

#endif

DWORD WINAPI PayloadManagerThread()
{
    LOG_VERBOSE("=== THREAD START ===");
    
    LOG_VERBOSE("Checking UbisoftGameLauncher...");
    if (GetProcessID(L"UbisoftGameLauncher.exe") == 0)
    {
        LOG_VERBOSE("Launcher NOT found - exit");
        return 0;
    }
    LOG_VERBOSE("Launcher found");

    LOG_VERBOSE("AllocateConsole...");
    AllocateConsole();
    LOG_VERBOSE("Console done");

    // TODO: SkipIntro needs Steam addresses - currently only has Uplay addresses
    // LOG_VERBOSE("SkipIntro::Initialize...");
    // if (SkipIntro::Initialize()) {
    //     LOG_INFO("SkipIntro OK");
    // } else {
    //     LOG_ERROR("SkipIntro FAIL");
    // }
    LOG_INFO("SkipIntro DISABLED (needs Steam address translation)");

    LOG_VERBOSE("InitializeD3D11Hook...");
    if (InitializeD3D11Hook()) {
        LOG_INFO("D3D11 hook OK");
    }
    else {
        LOG_ERROR("D3D11 hook FAIL");
    }

#ifdef DEVELOPMENT_MODE
    LOG_INFO("Development mode: Press F1 to load/unload TFPayload");
#elif defined(RELEASE_AUTOLOAD_MODE)
    LOG_INFO("Release mode: Will auto-trigger normal payload load from manager loop.");
#endif
    
    LOG_VERBOSE("Entering loop...");
    bool autoLoadTriggered = false;
    
    while (true) {
#ifdef DEVELOPMENT_MODE
        CheckDevelopmentReloadTriggers();
        CheckDevelopmentStartupAutoload(autoLoadTriggered);

        // F1 to toggle TFPayload.dll (development mode only)
        if (GetAsyncKeyState(VK_F1) & 0x1) {
            if (isLoaded) {
                UnloadTFPayload();
                std::cout << std::endl;
                std::cout << "Payload DOWN" << std::endl;
                std::cout << std::endl;
            }
            else {
                LoadTFPayload();
                std::cout << std::endl;
                std::cout << "Payload UP" << std::endl;
                std::cout << std::endl;
            }
        }
#elif defined(RELEASE_AUTOLOAD_MODE)
        if (!autoLoadTriggered && !isLoaded && IsGameRenderReady()) {
            autoLoadTriggered = true;
            LOG_INFO("Release mode: game render ready; auto-triggering normal payload load.");
            LoadTFPayload();
            LOG_INFO(isLoaded ? "Release mode: TFPayload loaded automatically." : "Release mode: TFPayload auto-load failed.");
        }
#endif
        // Note: Verbose logging toggle (=) is now handled by TFPayload's keybindings system
        // In RELEASE_AUTOLOAD_MODE, F1 does nothing
        
        Sleep(10);
    }
}


BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
        // Initialize logging FIRST
        Logging::Initialize();
        LOG_VERBOSE("=== DLL_PROCESS_ATTACH ===");
        
        StartProxy();
        LOG_VERBOSE("StartProxy done");
        
        CreateThread(0, 0, (LPTHREAD_START_ROUTINE)PayloadManagerThread, 0, 0, 0);
        LOG_VERBOSE("Thread created");
        break;

    case DLL_PROCESS_DETACH:
        LOG_VERBOSE("=== DLL_PROCESS_DETACH ===");
#ifdef DEVELOPMENT_MODE
        // Only allow cleanup in development mode
        if (hPayload != nullptr)
        {
            UnloadTFPayload();
        }
#endif
        // In release mode, we intentionally don't unload to prevent cheating
        Logging::Shutdown();
        break;
    }
    return TRUE;
}
