#include <Windows.h>
#include <iostream>
#include <winuser.h>
#include "ProxyDbgcore.h"
#include "HookDX11.h"
#include <TlHelp32.h>

// Debug Console
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

    // Initialize a PROCESSENTRY32 structure with the size of the structure
    PROCESSENTRY32 pt;
    pt.dwSize = sizeof(PROCESSENTRY32);

    // Iterate through all running processes
    if (Process32First(hsnap, &pt))
    {
        do
        {
            // Compare the executable file name of the current process with the given process name (case-insensitive)
            if (!lstrcmpi(pt.szExeFile, ProcessName))
            {
                // If the names match, close the snapshot handle and return the process ID of the current process
                CloseHandle(hsnap);
                return pt.th32ProcessID;
            }
        } while (Process32Next(hsnap, &pt));
    }

    // If no process with the given name is found, close the snapshot handle and return 0
    CloseHandle(hsnap);
    return 0;
}

// Function to get the base address of a module in a process
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
    // Check if the target process (UbisoftGameLauncher.exe) is running.
    if (GetProcessID(L"UbisoftGameLauncher.exe") == 0)
    {
        // If not found, exit the thread immediately.
        return 0;
    }

    // Create a console window for debug output.
    AllocateConsole();

    // Start an infinite loop to monitor for hotkeys.
    while (true) {
        // Check if the F3 key was pressed (0x1 ensures it's a single press).
        if (GetAsyncKeyState(VK_F3) & 0x1) {
            // Check current state: is the payload already loaded?
            if (isLoaded) {
                // UNLOAD (Free) the payload DLL.
                isLoaded = false;
                FreeLibrary(hPayload);
                hPayload = nullptr;

                // Output status to the console.
                std::cout << "Payload Dll DOWN" << std::endl;
            }
            // If the payload is NOT loaded.
            else {
                // LOAD the payload DLL from file.
                isLoaded = true;
                hPayload = LoadLibraryA("TFPayload.dll");
                // Output status to the console.
                std::cout << "Payload Dll UP" << std::endl;
            }
        }
    }
}


BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    // Standard entry point for a Windows Dynamic Link Library (DLL).
    switch (reason)
    {
        // Executed when the DLL is mapped into a process's address space.
    case DLL_PROCESS_ATTACH:
        // Initialize the proxy (likely for function forwarding/spoofing).
        StartProxy();

        // DX11 init, finds present function, applies MinHook to redirect rendering calls to custom hkPresent function in HookDX11.cpp (draws overlay)
        StartDX11Hook();

        // Create a new thread to manage the payload's lifecycle and hotkeys.
        CreateThread(0, 0, (LPTHREAD_START_ROUTINE)PayloadManagerThread, 0, 0, 0);
        break;

        // Executed when the DLL is unmapped from the process's address space.
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}