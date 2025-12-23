#pragma once
#include <d3d11.h>
#include <dxgi.h>

struct ImGuiContext; // Forward declare

void InitImGuiForSwapChain(IDXGISwapChain* swap);
void RenderOverlay();
bool InitializeD3D11Hook();

// Export functions for TFPayload
typedef void (*RenderCallback)();
extern "C" __declspec(dllexport) void RegisterRenderCallback(RenderCallback callback);
extern "C" __declspec(dllexport) void UnregisterRenderCallback(RenderCallback callback);
extern "C" __declspec(dllexport) ImGuiContext* GetImGuiContext();
