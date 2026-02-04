#include "ProxyDbgcore.h"
#include "logging.h"
#include <iostream>

#define EXPORT __declspec(dllexport)

FARPROC pMiniDumpReadDumpStream = nullptr;
FARPROC pMiniDumpWriteDump = nullptr;

extern "C" {

    EXPORT void MiniDumpReadDumpStream() { __asm { jmp pMiniDumpReadDumpStream } }
    EXPORT void MiniDumpWriteDump() { __asm { jmp pMiniDumpWriteDump } }

}

void StartProxy()
{
    char path[MAX_PATH]{};
    GetSystemDirectoryA(path, MAX_PATH);
    strcat_s(path, MAX_PATH, "\\dbgcore.dll");

    HMODULE real = LoadLibraryA(path);
    if (!real) {
        LOG_ERROR("[Proxy] Failed to load real dbgcore.dll");
        return;
    }

    pMiniDumpReadDumpStream = GetProcAddress(real, "MiniDumpReadDumpStream");
    pMiniDumpWriteDump = GetProcAddress(real, "MiniDumpWriteDump");

    LOG_INFO("[Proxy] Loaded real dbgcore.dll");
}
