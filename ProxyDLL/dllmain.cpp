#include <Windows.h>
#include <winuser.h>
#include "ProxyDbgcore.h"
#include "Overlay.h"
#include "SkipIntro.h"
#include "logging.h"
#include <TlHelp32.h>
#include <stdio.h>

// Debug Console
void AllocateConsole()
{
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

    LOG_VERBOSE("Entering loop...");
    
    while (true) {
        // F1 to toggle TFPayload.dll
        if (GetAsyncKeyState(VK_F1) & 0x1) {
            if (isLoaded) {
                isLoaded = false;
                FreeLibrary(hPayload);
                hPayload = nullptr;
                std::cout << std::endl;
                std::cout << "Payload DOWN" << std::endl;
                std::cout << std::endl;
            }
            else {
                isLoaded = true;
                hPayload = LoadLibraryA("TFPayload.dll");
                std::cout << std::endl;
                std::cout << "Payload UP" << std::endl;
                std::cout << std::endl;
            }
        }

        // Note: Verbose logging toggle (=) is now handled by TFPayload's keybindings system
        
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
        Logging::Shutdown();
        break;
    }
    return TRUE;
}
