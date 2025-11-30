// dllmain.cpp : Defines the entry point for the DLL application.
#include "pch.h"
#include <TlHelp32.h>
#include <iostream>;
bool isRunning = true;

#define KeyPress(...) (GetAsyncKeyState(__VA_ARGS__) & 0x1)
#define Log(...) std::cout << __VA_ARGS__ << std::endl

struct DumpStruct {
    char data[0x200];
};

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

DWORD WINAPI TFPayload() {
    const wchar_t* processName = L"trials_fusion.exe";
    const wchar_t* moduleName = L"trials_fusion.exe";

    DWORD processID = GetProcessID(processName);
    DWORD_PTR baseAddress = GetModuleBaseAddress(processID, moduleName);

    //auto func = (return type(calling convention*)(parameters...))(baseAddress + RVA);
    // __thiscall
    // __cdecl

    DumpStruct dummyObj{};

    auto disableMusic = (void(__cdecl*)())(baseAddress + 0x526D10);
    //auto disableReverb = (void(__cdecl*)())(baseAddress + 0xBD1FC0);
    auto disableReverb = (void(__fastcall*)(DumpStruct obj))(baseAddress + 0xBD1FC0);

    while (isRunning) {
        if (KeyPress(VK_END))
        {
            isRunning = false;
            Log("EndPayloadRun");

            
        }
        if (KeyPress(VK_F7)) {
            //disableMusic();
            //disableReverb(dummyObj);
            Log("Disable/Enable Music");
            
        }
    }
    return 0;
}


BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        CreateThread(0, 0, (LPTHREAD_START_ROUTINE)TFPayload, 0, 0, 0);
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}

