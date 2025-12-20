#pragma once

// Defines the function signature for the Proxy DLL to see.
// extern "C" ensures C++ name mangling doesn't happen, which is crucial 
// for GetProcAddress to find the symbol "ShutdownPayload".
extern "C" __declspec(dllexport) void ShutdownPayload();