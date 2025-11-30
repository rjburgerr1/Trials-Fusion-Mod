#include "HookDX11.h"
#include "Overlay.h"
#include "Keybinds.h"
#include <Windows.h>
#include <iostream>
#include <d3d11.h>
#include <dxgi.h>
#include "MinHook.h"


typedef HRESULT(__stdcall* PresentFn)(IDXGISwapChain*, UINT, UINT);
PresentFn oPresent = nullptr;

HRESULT __stdcall hkPresent(IDXGISwapChain* swap, UINT si, UINT flags)
{

    // Update keybinds
    UpdateKeybinds();

    // ImGui and overlay rendering
    InitImGuiForSwapChain(swap);
    RenderOverlay();

    return oPresent(swap, si, flags);
}

DWORD WINAPI HookThread(LPVOID)
{
    std::cout << "[Hook] Setting up Present hook...\n";

    // Dummy window
    WNDCLASS wc{};
    wc.lpfnWndProc = DefWindowProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = L"RJDummyWnd";
    RegisterClass(&wc);

    HWND wnd = CreateWindow(wc.lpszClassName, L"dummy", WS_OVERLAPPEDWINDOW,
        0, 0, 100, 100, nullptr, nullptr, wc.hInstance, nullptr);

    DXGI_SWAP_CHAIN_DESC scd{};
    scd.BufferCount = 1;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = wnd;
    scd.SampleDesc.Count = 1;
    scd.Windowed = TRUE;

    IDXGISwapChain* sc = nullptr;
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    D3D_FEATURE_LEVEL fl;

    if (SUCCEEDED(D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        nullptr, 0, D3D11_SDK_VERSION,
        &scd, &sc, &dev, &fl, &ctx)))
    {
        void** vmt = *(void***)sc;
        void* present = vmt[8];

        MH_Initialize();
        MH_CreateHook(present, &hkPresent, reinterpret_cast<void**>(&oPresent));
        MH_EnableHook(present);

        std::cout << "[Hook] Present hook installed.\n";
        std::cout << "[Hook] Music toggle will be active (Press M to toggle)\n";
    }
    else {
        std::cout << "[Hook] FAILED to create dummy DX11 swapchain.\n";
    }

    if (sc) sc->Release();
    if (ctx) ctx->Release();
    if (dev) dev->Release();
    DestroyWindow(wnd);

    return 0;
}

void StartDX11Hook()
{
    CreateThread(nullptr, 0, HookThread, nullptr, 0, nullptr);
}