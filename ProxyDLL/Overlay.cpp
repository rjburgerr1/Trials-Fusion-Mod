#include "Overlay.h"
#include "Keybinds.h"
#include <iostream>
#include <vector>

// ImGui
#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_win32.h"
#include "imgui/backends/imgui_impl_dx11.h"

// MinHook
#include <MinHook.h>

// Globals
static bool g_ImGuiInit = false;
static ID3D11Device* g_Device = nullptr;
static ID3D11DeviceContext* g_Context = nullptr;
static ID3D11RenderTargetView* g_MainRenderTargetView = nullptr;
static HWND g_Hwnd = nullptr;
static ImGuiContext* g_ImGuiContext = nullptr;

// Present hook
typedef HRESULT(__stdcall* PresentFn)(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);
PresentFn oPresent = nullptr;

// ResizeBuffers hook
typedef HRESULT(__stdcall* ResizeBuffersFn)(IDXGISwapChain* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags);
ResizeBuffersFn oResizeBuffers = nullptr;

// WndProc hook
extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
typedef LRESULT(CALLBACK* WndProcFn)(HWND, UINT, WPARAM, LPARAM);
WndProcFn oWndProc = nullptr;

// Callback registry for external renderers (like TFPayload)
typedef void (*RenderCallback)();
static std::vector<RenderCallback> g_RenderCallbacks;

LRESULT CALLBACK hkWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (g_ImGuiInit) {
        // Let ImGui process the message first
        ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam);

        // Check if ImGui wants to capture mouse input
        ImGuiIO& io = ImGui::GetIO();

        // Block mouse messages from reaching the game if ImGui wants the mouse
        if (io.WantCaptureMouse) {
            switch (uMsg) {
            case WM_LBUTTONDOWN:
            case WM_LBUTTONUP:
            case WM_LBUTTONDBLCLK:
            case WM_RBUTTONDOWN:
            case WM_RBUTTONUP:
            case WM_RBUTTONDBLCLK:
            case WM_MBUTTONDOWN:
            case WM_MBUTTONUP:
            case WM_MBUTTONDBLCLK:
            case WM_MOUSEWHEEL:
            case WM_MOUSEHWHEEL:
            case WM_MOUSEMOVE:
                // Return 0 to indicate we handled the message and don't pass it to the game
                return 0;
            }
        }

        // Block keyboard messages from reaching the game if ImGui wants the keyboard
        if (io.WantCaptureKeyboard) {
            switch (uMsg) {
            case WM_KEYDOWN:
            case WM_KEYUP:
            case WM_SYSKEYDOWN:
            case WM_SYSKEYUP:
            case WM_CHAR:
                // Return 0 to indicate we handled the message
                return 0;
            }
        }
    }

    // Pass all other messages to the game
    return CallWindowProc(oWndProc, hWnd, uMsg, wParam, lParam);
}

void CleanupRenderTarget()
{
    if (g_MainRenderTargetView) {
        g_MainRenderTargetView->Release();
        g_MainRenderTargetView = nullptr;
    }
}

void CreateRenderTarget(IDXGISwapChain* pSwapChain)
{
    ID3D11Texture2D* pBackBuffer = nullptr;
    pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer);
    if (pBackBuffer) {
        g_Device->CreateRenderTargetView(pBackBuffer, nullptr, &g_MainRenderTargetView);
        pBackBuffer->Release();
    }
}

void InitImGuiForSwapChain(IDXGISwapChain* swap)
{
    if (g_ImGuiInit) return;

    std::cout << "[ProxyDLL/Overlay] Initializing ImGui..." << std::endl;

    HRESULT hr = swap->GetDevice(__uuidof(ID3D11Device), (void**)&g_Device);
    if (FAILED(hr) || !g_Device) {
        std::cout << "[ProxyDLL/Overlay] Failed to get D3D11 Device" << std::endl;
        return;
    }

    g_Device->GetImmediateContext(&g_Context);

    // Get swap chain description
    DXGI_SWAP_CHAIN_DESC desc{};
    swap->GetDesc(&desc);
    g_Hwnd = desc.OutputWindow;

    std::cout << "[ProxyDLL/Overlay] Game HWND: 0x" << std::hex << (uintptr_t)g_Hwnd << std::dec << std::endl;

    // Create render target
    CreateRenderTarget(swap);

    // Hook WndProc
    oWndProc = (WndProcFn)SetWindowLongPtr(g_Hwnd, GWLP_WNDPROC, (LONG_PTR)hkWndProc);
    std::cout << "[ProxyDLL/Overlay] WndProc hooked" << std::endl;

    IMGUI_CHECKVERSION();
    g_ImGuiContext = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

    ImGui::StyleColorsDark();

    ImGui_ImplWin32_Init(g_Hwnd);
    ImGui_ImplDX11_Init(g_Device, g_Context);

    g_ImGuiInit = true;
    std::cout << "[ProxyDLL/Overlay] ImGui initialized successfully!" << std::endl;
    std::cout << "[ProxyDLL/Overlay] ImGui context: 0x" << std::hex << (uintptr_t)g_ImGuiContext << std::dec << std::endl;
}

HRESULT __stdcall hkPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags)
{
    // Initialize ImGui on first call
    if (!g_ImGuiInit) {
        InitImGuiForSwapChain(pSwapChain);
    }

    // Render ImGui only if initialized and we have a render target
    if (g_ImGuiInit && g_MainRenderTargetView) {
        // Start new ImGui frame
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // Render built-in overlay
        RenderOverlay();

        // Call all registered render callbacks (from TFPayload, etc.)
        for (auto callback : g_RenderCallbacks) {
            if (callback) {
                try {
                    callback();
                }
                catch (...) {
                    std::cout << "[ProxyDLL/Overlay] Exception in render callback!" << std::endl;
                }
            }
        }

        // Finish ImGui frame
        ImGui::Render();

        // Set render target and render
        g_Context->OMSetRenderTargets(1, &g_MainRenderTargetView, nullptr);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    }

    // Call original Present
    return oPresent(pSwapChain, SyncInterval, Flags);
}

HRESULT __stdcall hkResizeBuffers(IDXGISwapChain* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags)
{
    CleanupRenderTarget();
    HRESULT hr = oResizeBuffers(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);
    CreateRenderTarget(pSwapChain);
    return hr;
}

bool InitializeD3D11Hook()
{
    std::cout << "[ProxyDLL/Overlay] Initializing D3D11 hook..." << std::endl;

    // Create temporary window
    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, DefWindowProc, 0L, 0L, GetModuleHandle(NULL), NULL, NULL, NULL, NULL, L"TempD3D11Window", NULL };
    RegisterClassEx(&wc);
    HWND hWnd = CreateWindow(wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 100, 100, 300, 300, NULL, NULL, wc.hInstance, NULL);

    if (!hWnd) {
        std::cout << "[ProxyDLL/Overlay] Failed to create temp window" << std::endl;
        return false;
    }

    // Create swap chain
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 1;
    sd.BufferDesc.Width = 800;
    sd.BufferDesc.Height = 600;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    ID3D11Device* pDevice = nullptr;
    ID3D11DeviceContext* pContext = nullptr;
    IDXGISwapChain* pSwapChain = nullptr;
    D3D_FEATURE_LEVEL featureLevel;

    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        0,
        featureLevels,
        3,
        D3D11_SDK_VERSION,
        &sd,
        &pSwapChain,
        &pDevice,
        &featureLevel,
        &pContext
    );

    if (FAILED(hr) || !pSwapChain) {
        std::cout << "[ProxyDLL/Overlay] HARDWARE failed, trying WARP..." << std::endl;
        hr = D3D11CreateDeviceAndSwapChain(
            nullptr,
            D3D_DRIVER_TYPE_WARP,
            nullptr,
            0,
            featureLevels,
            3,
            D3D11_SDK_VERSION,
            &sd,
            &pSwapChain,
            &pDevice,
            &featureLevel,
            &pContext
        );
    }

    if (FAILED(hr) || !pSwapChain) {
        std::cout << "[ProxyDLL/Overlay] Device creation failed (HRESULT: 0x" << std::hex << hr << std::dec << ")" << std::endl;
        DestroyWindow(hWnd);
        UnregisterClass(wc.lpszClassName, wc.hInstance);
        return false;
    }

    std::cout << "[ProxyDLL/Overlay] Temp swap chain created" << std::endl;

    // Get vtable addresses
    void** pSwapChainVTable = *(void***)pSwapChain;
    void* pPresentAddr = pSwapChainVTable[8];
    void* pResizeBuffersAddr = pSwapChainVTable[13];

    std::cout << "[ProxyDLL/Overlay] Present: 0x" << std::hex << (uintptr_t)pPresentAddr << std::dec << std::endl;
    std::cout << "[ProxyDLL/Overlay] ResizeBuffers: 0x" << std::hex << (uintptr_t)pResizeBuffersAddr << std::dec << std::endl;

    // Clean up temp resources
    pSwapChain->Release();
    pContext->Release();
    pDevice->Release();
    DestroyWindow(hWnd);
    UnregisterClass(wc.lpszClassName, wc.hInstance);

    // Initialize MinHook
    MH_STATUS status = MH_Initialize();
    if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) {
        std::cout << "[ProxyDLL/Overlay] MinHook init failed: " << status << std::endl;
        return false;
    }

    // Hook Present
    status = MH_CreateHook(pPresentAddr, &hkPresent, (void**)&oPresent);
    if (status != MH_OK) {
        std::cout << "[ProxyDLL/Overlay] Failed to create Present hook: " << status << std::endl;
        return false;
    }

    status = MH_EnableHook(pPresentAddr);
    if (status != MH_OK) {
        std::cout << "[ProxyDLL/Overlay] Failed to enable Present hook: " << status << std::endl;
        return false;
    }

    std::cout << "[ProxyDLL/Overlay] Present hooked!" << std::endl;

    // Hook ResizeBuffers
    status = MH_CreateHook(pResizeBuffersAddr, &hkResizeBuffers, (void**)&oResizeBuffers);
    if (status == MH_OK) {
        MH_EnableHook(pResizeBuffersAddr);
        std::cout << "[ProxyDLL/Overlay] ResizeBuffers hooked!" << std::endl;
    }

    std::cout << "[ProxyDLL/Overlay] D3D11 hook complete!" << std::endl;
    return true;
}

void RenderOverlay()
{
    if (!g_ImGuiInit || !g_ShowOverlay)
        return;

    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoNavInputs |
        ImGuiWindowFlags_NoFocusOnAppearing;

    ImGui::Begin("RJHUD", nullptr, flags);
    ImGui::Text("Hook and RJ's Trials Mod v0.1");
    ImGui::Separator();
    ImGui::Text("Overlay: F4 toggle");
    ImGui::Text("Callbacks registered: %d", (int)g_RenderCallbacks.size());
    ImGui::End();
}

// Export function for TFPayload to register its render callback
extern "C" __declspec(dllexport) void RegisterRenderCallback(RenderCallback callback)
{
    if (!callback) {
        std::cout << "[ProxyDLL/Overlay] WARNING: Attempted to register null callback!" << std::endl;
        return;
    }

    std::cout << "[ProxyDLL/Overlay] Registering render callback: 0x" << std::hex << (uintptr_t)callback << std::dec << std::endl;
    g_RenderCallbacks.push_back(callback);
    std::cout << "[ProxyDLL/Overlay] Total callbacks registered: " << g_RenderCallbacks.size() << std::endl;
}

// Export function for TFPayload to unregister its render callback
extern "C" __declspec(dllexport) void UnregisterRenderCallback(RenderCallback callback)
{
    std::cout << "[ProxyDLL/Overlay] Unregistering render callback: 0x" << std::hex << (uintptr_t)callback << std::dec << std::endl;
    auto it = std::find(g_RenderCallbacks.begin(), g_RenderCallbacks.end(), callback);
    if (it != g_RenderCallbacks.end()) {
        g_RenderCallbacks.erase(it);
        std::cout << "[ProxyDLL/Overlay] Callback unregistered. Remaining callbacks: " << g_RenderCallbacks.size() << std::endl;
    }
    else {
        std::cout << "[ProxyDLL/Overlay] WARNING: Callback not found in registry!" << std::endl;
    }
}

// Export function to get ImGui context (so TFPayload can use it)
extern "C" __declspec(dllexport) ImGuiContext* GetImGuiContext()
{
    return g_ImGuiContext;
}