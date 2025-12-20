#include <Windows.h>
#include <iostream>
#include <winuser.h>
#include <Shlwapi.h>
#include "ProxyDbgcore.h"
#include <TlHelp32.h>
#include <wchar.h>

const wchar_t* const DEINIT_TRIGGER_FILE = L"F:\\VSProjects\\BasicProxy\\BasicProxy\\DEINIT_TRIGGER.txt";
static bool isLoaded = false;
HMODULE hPayload = nullptr;

void AllocateConsole()
{
    AllocConsole();
    FILE* fDummy;
    freopen_s(&fDummy, "CONOUT$", "w", stdout);
    freopen_s(&fDummy, "CONOUT$", "w", stderr);
    freopen_s(&fDummy, "CONIN$", "r", stdin);
}

DWORD GetProcessID(LPCTSTR ProcessName)
{
    // Create a snapshot of all running processes
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

// Console positioning global
bool g_consolePositioned = false;

// Console positioning on monitor
void PositionConsoleOnMonitor()
{
    if (g_consolePositioned) {
        return;  // Already positioned, don't keep trying
    }

    // Retry a few times since console might not exist immediately
    for (int attempt = 0; attempt < 5; attempt++) {
        HWND consoleWindow = GetConsoleWindow();
        if (consoleWindow == NULL) {
            if (attempt == 0) {
                std::cout << "INFO: Console window not found yet, retrying..." << std::endl;
            }
            Sleep(200);
            continue;
        }

        // Enumerate monitors fresh each time
        int monitorCount = 0;
        HMONITOR monitors[10] = { NULL };

        EnumDisplayMonitors(NULL, NULL, [](HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData) -> BOOL {
            HMONITOR* pMonitors = (HMONITOR*)dwData;
            static int count = 0;
            if (count < 10) {
                pMonitors[count++] = hMonitor;
            }
            return TRUE;
            }, (LPARAM)monitors);


        // Get third monitor info (index 2)
        MONITORINFO mi = { sizeof(MONITORINFO) };
        if (!GetMonitorInfo(monitors[1], &mi)) {
            std::cout << "ERROR: Could not get monitor info for monitor!" << std::endl;
            Sleep(200);
            continue;
        }

        RECT workArea = mi.rcWork;
        int monitorWidth = workArea.right - workArea.left;
        int monitorHeight = workArea.bottom - workArea.top;

        // Position to right half of screen
        int x = workArea.left + (monitorWidth / 2);
        int y = workArea.top;
        int width = monitorWidth / 2;
        int height = monitorHeight;

        std::cout << "Positioning to: x=" << x << ", y=" << y << ", width=" << width << ", height=" << height << std::endl;

        // Set window position and size
        if (SetWindowPos(consoleWindow, HWND_TOP, x, y, width, height, SWP_SHOWWINDOW)) {
            std::cout << "[Monitor] Positioned on left monitor - Right half (attempt " << (attempt + 1) << ")" << std::endl;
            g_consolePositioned = true;
            return;
        }
        else {
            std::cout << "WARNING: SetWindowPos failed on attempt " << (attempt + 1) << ", retrying..." << std::endl;
        }
    }
    g_consolePositioned = true;
}


void UnloadPayload() {

    if (isLoaded && hPayload != nullptr) {

        std::wcout << "[Proxy DLL] Attempting safe shutdown of payload..." << std::endl;

        // Define the function signature (must match the payload's export)
        typedef void(__cdecl* ShutdownFunc)();

        // Dynamically link to the exported function
        ShutdownFunc shutdown = (ShutdownFunc)GetProcAddress(hPayload, "ShutdownPayload");

        if (shutdown != nullptr) {
            // Execute the payload's internal cleanup FIRST.
            shutdown();
            std::wcout << "[Proxy DLL] Payload cleanup function executed." << std::endl;
        }

        FreeLibrary(hPayload);
        hPayload = nullptr;
        isLoaded = false;

        std::cout << "Payload Dll DOWN safely." << std::endl;
    }
    isLoaded = false;
}

void LoadPayload() {
    if (!isLoaded) {
        // Get the directory where ProxyDLL is located
        wchar_t proxyPath[MAX_PATH];
        GetModuleFileNameW(GetModuleHandleA("dbgcore.dll"), proxyPath, MAX_PATH);
        
        // Remove the filename to get just the directory
        wchar_t* lastSlash = wcsrchr(proxyPath, L'\\');
        if (lastSlash) *lastSlash = L'\0';
        
        // Build path to TFPayload.dll (assuming it's in the same directory)
        wchar_t payloadPath[MAX_PATH];
        swprintf_s(payloadPath, MAX_PATH, L"%s\\TFPayload.dll", proxyPath);
        
        hPayload = LoadLibraryW(payloadPath);
        if (hPayload != nullptr) {
            std::wcout << L"[Proxy DLL] Payload loaded from: " << payloadPath << std::endl;
            std::cout << "Payload Dll UP" << std::endl;
            isLoaded = true;

            // Call the payload's startup/init function if it exists
            typedef void(__cdecl* InitFunc)();
            InitFunc init = (InitFunc)GetProcAddress(hPayload, "PayloadInit");
            if (init != nullptr) {
                std::cout << "[Proxy DLL] Calling PayloadInit..." << std::endl;
                init();
            }
        }
        else {
            DWORD error = GetLastError();
            std::wcerr << L"ERROR: Failed to load TFPayload.dll from " << payloadPath << L" (Error: " << error << L")" << std::endl;
        }
    }
}

void CheckForDeinitTrigger() {
    wchar_t fullPath[MAX_PATH];
    DWORD dwLength;

    dwLength = GetFullPathNameW(
        DEINIT_TRIGGER_FILE, // The path to be resolved (e.g., "..\\DEINIT_TRIGGER.txt")
        MAX_PATH,            // Size of the output buffer
        fullPath,            // Output buffer for the absolute path
        NULL                 // Optional pointer to file part
    );

    // Check if path resolution succeeded
    if (dwLength == 0 || dwLength > MAX_PATH) {
        wcscpy_s(fullPath, MAX_PATH, DEINIT_TRIGGER_FILE);
    }

    DWORD attributes = GetFileAttributesW(DEINIT_TRIGGER_FILE);

    // Check if the file exists
    if (attributes != INVALID_FILE_ATTRIBUTES) {
        std::wcout << "[Proxy DLL] Trigger file found. Unloading payload..." << std::endl;

        // 1. Unload the current payload instance
        UnloadPayload();
        Sleep(200);
        if (DeleteFileW(DEINIT_TRIGGER_FILE)) {
            std::wcout << "[Proxy DLL] Successfully deleted " << DEINIT_TRIGGER_FILE << ". Ready for rebuild." << std::endl;
            // Add a delay to ensure the file system finishes writing the new DLL
            Sleep(14000);
            LoadPayload(); // This calls LoadLibraryA("TFPayload.dll")
        }
        else {
            DWORD error = GetLastError();
            std::wcout << "[Proxy DLL ERROR] Failed to delete " << DEINIT_TRIGGER_FILE << ". Error: " << error << std::endl;
        }
    }
}

DWORD WINAPI PayloadManagerThread()
{
    // Check if the target process (UbisoftGameLauncher.exe) is running.
    if (GetProcessID(L"UbisoftGameLauncher.exe") == 0)
    {
        return 0;
    }

    // Create a console window for debug output.
    AllocateConsole();

    // Position console on specific monitor
    PositionConsoleOnMonitor();

    // Start an infinite loop to monitor for hotkeys AND the trigger file.
    while (true) {

        CheckForDeinitTrigger();

        if (GetAsyncKeyState(VK_F1) & 0x1) {
            if (isLoaded) {
                // F1 Pressed: UNLOAD
                UnloadPayload();
            }
            else {
                // F1 Pressed: LOAD
                LoadPayload();
            }
        }
        Sleep(150);
    }
    return 0;
}


BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
        StartProxy();
        // Create a new thread to manage the payload's lifecycle, hotkeys, and the file trigger.
        CreateThread(0, 0, (LPTHREAD_START_ROUTINE)PayloadManagerThread, 0, 0, 0);
        break;

    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}