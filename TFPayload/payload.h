#pragma once
#include <Windows.h>
#include <cstdio>

// Defines the function signature for the Proxy DLL to see.
// extern "C" ensures C++ name mangling doesn't happen, which is crucial 
// for GetProcAddress to find the symbol "ShutdownPayload".
extern "C" __declspec(dllexport) void ShutdownPayload();

// Hook conflict debugging helper
namespace HookLogger {
    inline void LogHook(const char* moduleName, const char* functionName, void* targetAddress, void* hookAddress) {
        FILE* f = nullptr;
        fopen_s(&f, "tfpayload_hooks.log", "a");
        if (f) {
            fprintf(f, "[%s] %s\n", moduleName, functionName);
            fprintf(f, "  Target:  0x%p\n", targetAddress);
            fprintf(f, "  Hook:    0x%p\n\n", hookAddress);
            fclose(f);
        }
    }
}