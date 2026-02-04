#include "Overlay.h"
#include "logging.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <Psapi.h>
#include <sstream>
#include <dxgi1_2.h>  // For IDXGISwapChain1, IDXGIFactory2, etc.

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

// Present1 hook (DXGI 1.2+)
typedef HRESULT(__stdcall* Present1Fn)(IDXGISwapChain1* pSwapChain, UINT SyncInterval, UINT PresentFlags, const DXGI_PRESENT_PARAMETERS* pPresentParameters);
Present1Fn oPresent1 = nullptr;

// ResizeBuffers hook
typedef HRESULT(__stdcall* ResizeBuffersFn)(IDXGISwapChain* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags);
ResizeBuffersFn oResizeBuffers = nullptr;

// Release hook to track swapchain lifetime
typedef ULONG(__stdcall* ReleaseFn)(IUnknown* This);
ReleaseFn oRelease = nullptr;

// WndProc hook
extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
typedef LRESULT(CALLBACK* WndProcFn)(HWND, UINT, WPARAM, LPARAM);
WndProcFn oWndProc = nullptr;

// Callback registry for external renderers (like TFPayload)
typedef void (*RenderCallback)();
static std::vector<RenderCallback> g_RenderCallbacks;

// Hook chaining detection
static bool g_PresentWasAlreadyHooked = false;
static void* g_OtherModPresentHook = nullptr;

// Minimal overlay visibility (always on, no toggle)
static bool g_ShowOverlay = true;

// For hooking via CreateDXGIFactory approach
typedef HRESULT(WINAPI* D3D11CreateDeviceAndSwapChainFn)(
    IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL*, UINT, UINT,
    const DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**, ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);
D3D11CreateDeviceAndSwapChainFn oD3D11CreateDeviceAndSwapChain = nullptr;

// Hook IDXGIFactory::CreateSwapChain (used by xinput mod)
typedef HRESULT(WINAPI* CreateDXGIFactoryFn)(REFIID riid, void** ppFactory);
CreateDXGIFactoryFn oCreateDXGIFactory = nullptr;

typedef HRESULT(WINAPI* CreateDXGIFactory1Fn)(REFIID riid, void** ppFactory);
CreateDXGIFactory1Fn oCreateDXGIFactory1 = nullptr;

// Store original CreateSwapChain from the factory's vtable
typedef HRESULT(__stdcall* CreateSwapChainFn)(IDXGIFactory* This, IUnknown* pDevice, DXGI_SWAP_CHAIN_DESC* pDesc, IDXGISwapChain** ppSwapChain);
CreateSwapChainFn oCreateSwapChain = nullptr;

// CreateSwapChainForHwnd (DXGI 1.2)
typedef HRESULT(__stdcall* CreateSwapChainForHwndFn)(IDXGIFactory2* This, IUnknown* pDevice, HWND hWnd, const DXGI_SWAP_CHAIN_DESC1* pDesc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc, IDXGIOutput* pRestrictToOutput, IDXGISwapChain1** ppSwapChain);
CreateSwapChainForHwndFn oCreateSwapChainForHwnd = nullptr;

static bool g_HookedPresent = false;
static IDXGISwapChain* g_pGameSwapChain = nullptr;

// Delayed hook thread
static HANDLE g_hDelayedHookThread = nullptr;

LRESULT CALLBACK hkWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (g_ImGuiInit) {
        ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam);

        ImGuiIO& io = ImGui::GetIO();

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
                return 0;
            }
        }

        if (io.WantCaptureKeyboard) {
            switch (uMsg) {
            case WM_KEYDOWN:
            case WM_KEYUP:
            case WM_SYSKEYDOWN:
            case WM_SYSKEYUP:
            case WM_CHAR:
                return 0;
            }
        }
    }

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

    LOG_VERBOSE("[Overlay] InitImGuiForSwapChain...");

    HRESULT hr = swap->GetDevice(__uuidof(ID3D11Device), (void**)&g_Device);
    if (FAILED(hr) || !g_Device) {
        LOG_ERROR("[Overlay] Failed to get D3D11 Device");
        return;
    }

    g_Device->GetImmediateContext(&g_Context);

    DXGI_SWAP_CHAIN_DESC desc{};
    swap->GetDesc(&desc);
    g_Hwnd = desc.OutputWindow;

    LOG_VERBOSE("[Overlay] Game HWND: 0x" << std::hex << (uintptr_t)g_Hwnd);

    CreateRenderTarget(swap);

    oWndProc = (WndProcFn)SetWindowLongPtr(g_Hwnd, GWLP_WNDPROC, (LONG_PTR)hkWndProc);
    LOG_VERBOSE("[Overlay] WndProc hooked");

    IMGUI_CHECKVERSION();
    g_ImGuiContext = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

    ImGui::StyleColorsDark();

    ImGui_ImplWin32_Init(g_Hwnd);
    ImGui_ImplDX11_Init(g_Device, g_Context);

    g_ImGuiInit = true;
    LOG_INFO("[Overlay] ImGui initialized!");
    
    if (g_PresentWasAlreadyHooked) {
        LOG_VERBOSE("[Overlay] Chained with mod at: 0x" << std::hex << (uintptr_t)g_OtherModPresentHook);
    }
}

HRESULT __stdcall hkPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags)
{
    static int frameCount = 0;
    frameCount++;
    
    // Log every 60 frames to verify hook is being called
    if (frameCount % 60 == 0) {
        LOG_VERBOSE("[Overlay] hkPresent called (frame " << frameCount << ")");
    }
    
    // F2 key toggle for overlay
    static bool lastF2State = false;
    bool f2Down = (GetAsyncKeyState(VK_F2) & 0x8000) != 0;
    if (f2Down && !lastF2State) {
        g_ShowOverlay = !g_ShowOverlay;
        LOG_INFO(g_ShowOverlay ? "[Overlay] Overlay SHOWN" : "[Overlay] Overlay HIDDEN");
    }
    lastF2State = f2Down;
    
    // Log only on first call
    if (frameCount == 1) {
        LOG_VERBOSE("[Overlay] hkPresent hooked successfully!");
        LOG_VERBOSE("[Overlay] SwapChain: 0x" << std::hex << (uintptr_t)pSwapChain);
    }
    
    if (!g_ImGuiInit) {
        InitImGuiForSwapChain(pSwapChain);
    }

    if (g_ImGuiInit && g_MainRenderTargetView) {
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        RenderOverlay();

        for (auto callback : g_RenderCallbacks) {
            if (callback) {
                try {
                    callback();
                }
                catch (...) {
                    LOG_ERROR("[Overlay] Exception in render callback!");
                }
            }
        }

        ImGui::Render();

        g_Context->OMSetRenderTargets(1, &g_MainRenderTargetView, nullptr);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    }

    return oPresent(pSwapChain, SyncInterval, Flags);
}

HRESULT __stdcall hkPresent1(IDXGISwapChain1* pSwapChain, UINT SyncInterval, UINT PresentFlags, const DXGI_PRESENT_PARAMETERS* pPresentParameters)
{
    static int frameCount = 0;
    frameCount++;
    
    // Log every 60 frames to verify hook is being called
    if (frameCount % 60 == 0) {
        LOG_VERBOSE("[Overlay] hkPresent1 called (frame " << frameCount << ") - DXGI 1.2 path");
    }
    
    // F2 key toggle for overlay
    static bool lastF2State = false;
    bool f2Down = (GetAsyncKeyState(VK_F2) & 0x8000) != 0;
    if (f2Down && !lastF2State) {
        g_ShowOverlay = !g_ShowOverlay;
        LOG_INFO(g_ShowOverlay ? "[Overlay] Overlay SHOWN" : "[Overlay] Overlay HIDDEN");
    }
    lastF2State = f2Down;
    
    // Log only on first call
    if (frameCount == 1) {
        LOG_VERBOSE("[Overlay] hkPresent1 hooked successfully!");
        LOG_VERBOSE("[Overlay] SwapChain1: 0x" << std::hex << (uintptr_t)pSwapChain);
    }
    
    // Cast to IDXGISwapChain for compatibility
    IDXGISwapChain* pSwapChainBase = (IDXGISwapChain*)pSwapChain;
    
    if (!g_ImGuiInit) {
        InitImGuiForSwapChain(pSwapChainBase);
    }

    if (g_ImGuiInit && g_MainRenderTargetView) {
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        RenderOverlay();

        for (auto callback : g_RenderCallbacks) {
            if (callback) {
                try {
                    callback();
                }
                catch (...) {
                    LOG_ERROR("[Overlay] Exception in render callback!");
                }
            }
        }

        ImGui::Render();

        g_Context->OMSetRenderTargets(1, &g_MainRenderTargetView, nullptr);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    }

    return oPresent1(pSwapChain, SyncInterval, PresentFlags, pPresentParameters);
}

HRESULT __stdcall hkResizeBuffers(IDXGISwapChain* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags)
{
    LOG_VERBOSE("[Overlay] hkResizeBuffers called!");
    CleanupRenderTarget();
    HRESULT hr = oResizeBuffers(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);
    CreateRenderTarget(pSwapChain);
    return hr;
}

ULONG __stdcall hkRelease(IUnknown* This)
{
    static int releaseCount = 0;
    releaseCount++;
    
    LOG_VERBOSE("[Overlay] SwapChain Release called (call #" << releaseCount << ")");
    LOG_VERBOSE("[Overlay] Releasing swapchain: 0x" << std::hex << (uintptr_t)This);
    
    ULONG refCount = oRelease(This);
    
    LOG_VERBOSE("[Overlay] New ref count: " << refCount);
    
    if (refCount == 0) {
        LOG_VERBOSE("[Overlay] *** SWAPCHAIN DESTROYED ***");
    }
    
    return refCount;
}

bool IsAddressInModule(void* address, const char* moduleName)
{
    HMODULE hModule = GetModuleHandleA(moduleName);
    if (!hModule) return false;
    
    MODULEINFO modInfo;
    if (!GetModuleInformation(GetCurrentProcess(), hModule, &modInfo, sizeof(modInfo))) {
        return false;
    }
    
    uintptr_t addr = (uintptr_t)address;
    uintptr_t modBase = (uintptr_t)modInfo.lpBaseOfDll;
    uintptr_t modEnd = modBase + modInfo.SizeOfImage;
    
    return (addr >= modBase && addr < modEnd);
}

std::string GetModuleNameFromAddress(void* address)
{
    HMODULE hModule = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          (LPCSTR)address, &hModule)) {
        char modulePath[MAX_PATH];
        if (GetModuleFileNameA(hModule, modulePath, MAX_PATH)) {
            std::string path(modulePath);
            size_t lastSlash = path.find_last_of("\\/");
            if (lastSlash != std::string::npos) {
                return path.substr(lastSlash + 1);
            }
            return path;
        }
    }
    return "unknown";
}

// Direct vtable patching
void PatchVTableEntry(void** vtable, int index, void* newFunc, void** oldFunc)
{
    DWORD oldProtect;
    VirtualProtect(&vtable[index], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect);
    *oldFunc = vtable[index];
    vtable[index] = newFunc;
    VirtualProtect(&vtable[index], sizeof(void*), oldProtect, &oldProtect);
}

// Thread that waits and then hooks Present on the stored swapchain
DWORD WINAPI DelayedPresentHookThread(LPVOID lpParam)
{
    IDXGISwapChain* pSwapChain = (IDXGISwapChain*)lpParam;
    
    LOG_VERBOSE("[Overlay] DelayedPresentHookThread: Waiting 1000ms for other mods to hook first...");
    Sleep(1000);
    
    if (g_HookedPresent) {
        LOG_VERBOSE("[Overlay] DelayedPresentHookThread: Already hooked, exiting");
        return 0;
    }
    
    LOG_VERBOSE("[Overlay] DelayedPresentHookThread: Now hooking Present...");
    
    void** pSwapChainVTable = *(void***)pSwapChain;
    LOG_VERBOSE("[Overlay] VTable: 0x" << std::hex << (uintptr_t)pSwapChainVTable);
    
    void* pPresentAddr = pSwapChainVTable[8];
    void* pResizeBuffersAddr = pSwapChainVTable[13];
    
    LOG_VERBOSE("[Overlay] Present vtable[8]: 0x" << std::hex << (uintptr_t)pPresentAddr);
    LOG_VERBOSE("[Overlay] ResizeBuffers vtable[13]: 0x" << std::hex << (uintptr_t)pResizeBuffersAddr);
    
    bool presentInDXGI = IsAddressInModule(pPresentAddr, "dxgi.dll");
    std::string presentModule = GetModuleNameFromAddress(pPresentAddr);
    
    LOG_VERBOSE("[Overlay] Present currently points to: " << presentModule);
    
    if (!presentInDXGI) {
        g_PresentWasAlreadyHooked = true;
        g_OtherModPresentHook = pPresentAddr;
        LOG_VERBOSE("[Overlay] *** Present ALREADY HOOKED by another mod - will chain ***");
    } else {
        LOG_VERBOSE("[Overlay] Present points to dxgi.dll (not hooked by other mod)");
    }
    
    // Patch vtable
    LOG_VERBOSE("[Overlay] Patching vtable...");
    
    PatchVTableEntry(pSwapChainVTable, 8, &hkPresent, (void**)&oPresent);
    LOG_VERBOSE("[Overlay] oPresent (original/chained): 0x" << std::hex << (uintptr_t)oPresent);
    LOG_VERBOSE("[Overlay] Present vtable PATCHED");
    
    PatchVTableEntry(pSwapChainVTable, 13, &hkResizeBuffers, (void**)&oResizeBuffers);
    LOG_VERBOSE("[Overlay] oResizeBuffers: 0x" << std::hex << (uintptr_t)oResizeBuffers);
    LOG_VERBOSE("[Overlay] ResizeBuffers vtable PATCHED");
    
    g_HookedPresent = true;
    LOG_VERBOSE("[Overlay] DelayedPresentHookThread: VTable hooks installed!");
    
    return 0;
}

// Hook D3D11CreateDeviceAndSwapChain to intercept the game's swapchain creation
HRESULT WINAPI hkD3D11CreateDeviceAndSwapChain(
    IDXGIAdapter* pAdapter,
    D3D_DRIVER_TYPE DriverType,
    HMODULE Software,
    UINT Flags,
    const D3D_FEATURE_LEVEL* pFeatureLevels,
    UINT FeatureLevels,
    UINT SDKVersion,
    const DXGI_SWAP_CHAIN_DESC* pSwapChainDesc,
    IDXGISwapChain** ppSwapChain,
    ID3D11Device** ppDevice,
    D3D_FEATURE_LEVEL* pFeatureLevel,
    ID3D11DeviceContext** ppImmediateContext)
{
    static int callCount = 0;
    int thisCallNum = ++callCount;
    
    LOG_VERBOSE("[Overlay] hkD3D11CreateDeviceAndSwapChain called! (Call #" << thisCallNum << ")");
    
    if (pSwapChainDesc) {
        LOG_VERBOSE("[Overlay] Requested width: " << pSwapChainDesc->BufferDesc.Width);
        LOG_VERBOSE("[Overlay] Requested height: " << pSwapChainDesc->BufferDesc.Height);
        LOG_VERBOSE("[Overlay] OutputWindow: 0x" << std::hex << (uintptr_t)pSwapChainDesc->OutputWindow);
    }
    
    // Call original
    HRESULT hr = oD3D11CreateDeviceAndSwapChain(
        pAdapter, DriverType, Software, Flags, pFeatureLevels, FeatureLevels,
        SDKVersion, pSwapChainDesc, ppSwapChain, ppDevice, pFeatureLevel, ppImmediateContext);
    
    LOG_VERBOSE("[Overlay] Call #" << thisCallNum << " returned: 0x" << std::hex << hr);
    
    if (SUCCEEDED(hr) && ppSwapChain && *ppSwapChain) {
        LOG_VERBOSE("[Overlay] SwapChain created at: 0x" << std::hex << (uintptr_t)*ppSwapChain);
        
        // Hook EVERY swapchain immediately via vtable patch
        // Don't use a delayed thread - hook all swapchains as they're created
        void** pSwapChainVTable = *(void***)*ppSwapChain;
        void* pPresentAddr = pSwapChainVTable[8];
        
        std::string presentModule = GetModuleNameFromAddress(pPresentAddr);
        LOG_VERBOSE("[Overlay] This swapchain's Present points to: " << presentModule);
        
        // Only patch if not already patched
        if (pPresentAddr != (void*)&hkPresent) {
            LOG_VERBOSE("[Overlay] Patching this swapchain's vtable...");
            
            // Check if another mod already hooked it
            bool alreadyHooked = !IsAddressInModule(pPresentAddr, "dxgi.dll");
            if (alreadyHooked) {
                LOG_VERBOSE("[Overlay] *** Another mod hooked this swapchain - will chain ***");
            }
            
            // Patch vtable entries for THIS specific swapchain
            PresentFn thisOPresent = nullptr;
            Present1Fn thisOPresent1 = nullptr;
            ResizeBuffersFn thisOResizeBuffers = nullptr;
            ReleaseFn thisORelease = nullptr;
            
            PatchVTableEntry(pSwapChainVTable, 8, &hkPresent, (void**)&thisOPresent);
            LOG_VERBOSE("[Overlay] Patched vtable[8] (Present)");
            
            PatchVTableEntry(pSwapChainVTable, 22, &hkPresent1, (void**)&thisOPresent1); // Present1 at index 22
            LOG_VERBOSE("[Overlay] Patched vtable[22] (Present1)");
            
            PatchVTableEntry(pSwapChainVTable, 13, &hkResizeBuffers, (void**)&thisOResizeBuffers);
            LOG_VERBOSE("[Overlay] Patched vtable[13] (ResizeBuffers)");
            
            PatchVTableEntry(pSwapChainVTable, 2, &hkRelease, (void**)&thisORelease); // Release is at index 2
            LOG_VERBOSE("[Overlay] Patched vtable[2] (Release)");
            
            // Store the first valid Present pointer we find
            if (!oPresent) {
                oPresent = thisOPresent;
                LOG_VERBOSE("[Overlay] Stored oPresent: 0x" << std::hex << (uintptr_t)oPresent);
            }
            if (!oPresent1) {
                oPresent1 = thisOPresent1;
                LOG_VERBOSE("[Overlay] Stored oPresent1: 0x" << std::hex << (uintptr_t)oPresent1);
            }
            if (!oResizeBuffers) {
                oResizeBuffers = thisOResizeBuffers;
                LOG_VERBOSE("[Overlay] Stored oResizeBuffers: 0x" << std::hex << (uintptr_t)oResizeBuffers);
            }
            if (!oRelease) {
                oRelease = thisORelease;
                LOG_VERBOSE("[Overlay] Stored oRelease: 0x" << std::hex << (uintptr_t)oRelease);
            }
            
            LOG_VERBOSE("[Overlay] Swapchain vtable PATCHED");
            g_HookedPresent = true;
            
            // CRITICAL DEBUG: Verify the patch actually took
            void** pVerifyVTable = *(void***)*ppSwapChain;
            void* pVerifyPresent = pVerifyVTable[8];
            
            if (pVerifyPresent == (void*)&hkPresent) {
                LOG_VERBOSE("[Overlay] VERIFICATION: Vtable patch confirmed!");
            } else {
                LOG_ERROR("[Overlay] ERROR: Vtable patch FAILED to apply!");
                LOG_ERROR("[Overlay] Present now points to: 0x" << std::hex << (uintptr_t)pVerifyPresent);
            }
        } else {
            LOG_VERBOSE("[Overlay] Swapchain already points to our hook, skipping");
        }
    } else if (SUCCEEDED(hr)) {
        LOG_VERBOSE("[Overlay] Call succeeded but NO swapchain was created!");
    } else {
        LOG_ERROR("[Overlay] Call #" << thisCallNum << " FAILED with HRESULT: 0x" << std::hex << hr);
    }
    
    return hr;
}

// Hook for IDXGIFactory::CreateSwapChain (xinput mod probably uses this)
HRESULT __stdcall hkCreateSwapChain(IDXGIFactory* This, IUnknown* pDevice, DXGI_SWAP_CHAIN_DESC* pDesc, IDXGISwapChain** ppSwapChain)
{
    static int callCount = 0;
    int thisCallNum = ++callCount;
    
    LOG_VERBOSE("[Overlay] IDXGIFactory::CreateSwapChain called! (Call #" << thisCallNum << ")");
    
    if (pDesc) {
        LOG_VERBOSE("[Overlay] Width: " << pDesc->BufferDesc.Width);
        LOG_VERBOSE("[Overlay] Height: " << pDesc->BufferDesc.Height);
        LOG_VERBOSE("[Overlay] OutputWindow: 0x" << std::hex << (uintptr_t)pDesc->OutputWindow);
    }
    
    // Call original
    HRESULT hr = oCreateSwapChain(This, pDevice, pDesc, ppSwapChain);
    
    LOG_VERBOSE("[Overlay] CreateSwapChain returned: 0x" << std::hex << hr);
    
    if (SUCCEEDED(hr) && ppSwapChain && *ppSwapChain) {
        LOG_VERBOSE("[Overlay] SwapChain created at: 0x" << std::hex << (uintptr_t)*ppSwapChain);
        
        // Hook this swapchain's Present immediately
        void** pSwapChainVTable = *(void***)*ppSwapChain;
        void* pPresentAddr = pSwapChainVTable[8];
        
        std::string presentModule = GetModuleNameFromAddress(pPresentAddr);
        LOG_VERBOSE("[Overlay] This swapchain's Present points to: " << presentModule);
        
        // Only patch if not already patched
        if (pPresentAddr != (void*)&hkPresent) {
            LOG_VERBOSE("[Overlay] Patching this swapchain's vtable...");
            
            // Check if another mod already hooked it
            bool alreadyHooked = !IsAddressInModule(pPresentAddr, "dxgi.dll");
            if (alreadyHooked) {
                LOG_VERBOSE("[Overlay] *** Another mod hooked this swapchain - will chain ***");
            }
            
            // Patch vtable entries for THIS specific swapchain
            PresentFn thisOPresent = nullptr;
            Present1Fn thisOPresent1 = nullptr;
            ResizeBuffersFn thisOResizeBuffers = nullptr;
            ReleaseFn thisORelease = nullptr;
            
            PatchVTableEntry(pSwapChainVTable, 8, &hkPresent, (void**)&thisOPresent);
            PatchVTableEntry(pSwapChainVTable, 22, &hkPresent1, (void**)&thisOPresent1);
            PatchVTableEntry(pSwapChainVTable, 13, &hkResizeBuffers, (void**)&thisOResizeBuffers);
            PatchVTableEntry(pSwapChainVTable, 2, &hkRelease, (void**)&thisORelease);
            
            // Store the first valid Present pointer we find
            if (!oPresent) {
                oPresent = thisOPresent;
                LOG_VERBOSE("[Overlay] Stored oPresent: 0x" << std::hex << (uintptr_t)oPresent);
            }
            if (!oPresent1) {
                oPresent1 = thisOPresent1;
                LOG_VERBOSE("[Overlay] Stored oPresent1: 0x" << std::hex << (uintptr_t)oPresent1);
            }
            if (!oResizeBuffers) {
                oResizeBuffers = thisOResizeBuffers;
                LOG_VERBOSE("[Overlay] Stored oResizeBuffers: 0x" << std::hex << (uintptr_t)oResizeBuffers);
            }
            
            LOG_VERBOSE("[Overlay] Swapchain vtable PATCHED (from IDXGIFactory)");
            g_HookedPresent = true;
            
            // CRITICAL DEBUG: Verify the patch actually took
            void** pVerifyVTable = *(void***)*ppSwapChain;
            void* pVerifyPresent = pVerifyVTable[8];
            
            if (pVerifyPresent == (void*)&hkPresent) {
                LOG_VERBOSE("[Overlay] VERIFICATION: Vtable patch confirmed!");
            } else {
                LOG_ERROR("[Overlay] ERROR: Vtable patch FAILED to apply!");
                LOG_ERROR("[Overlay] Present now points to: 0x" << std::hex << (uintptr_t)pVerifyPresent);
            }
        } else {
            LOG_VERBOSE("[Overlay] Swapchain already points to our hook, skipping");
        }
    }
    
    return hr;
}

// Hook for CreateDXGIFactory to intercept factory creation and hook CreateSwapChain
HRESULT WINAPI hkCreateDXGIFactory(REFIID riid, void** ppFactory)
{
    LOG_VERBOSE("[Overlay] CreateDXGIFactory called!");
    
    HRESULT hr = oCreateDXGIFactory(riid, ppFactory);
    
    if (SUCCEEDED(hr) && ppFactory && *ppFactory) {
        LOG_VERBOSE("[Overlay] Factory created successfully");
        LOG_VERBOSE("[Overlay] Factory at: 0x" << std::hex << (uintptr_t)*ppFactory);
        
        // Hook the CreateSwapChain method on this factory
        void** pFactoryVTable = *(void***)*ppFactory;
        void* pCreateSwapChainAddr = pFactoryVTable[10]; // CreateSwapChain is at index 10
        
        std::string module = GetModuleNameFromAddress(pCreateSwapChainAddr);
        LOG_VERBOSE("[Overlay] Factory's CreateSwapChain points to: " << module);
        
        // Only hook if not already hooked
        if (!oCreateSwapChain && pCreateSwapChainAddr != (void*)&hkCreateSwapChain) {
            LOG_VERBOSE("[Overlay] Hooking IDXGIFactory::CreateSwapChain...");
            PatchVTableEntry(pFactoryVTable, 10, &hkCreateSwapChain, (void**)&oCreateSwapChain);
            LOG_VERBOSE("[Overlay] IDXGIFactory::CreateSwapChain HOOKED");
        } else {
            LOG_VERBOSE("[Overlay] CreateSwapChain already hooked, skipping");
        }
    }
    
    return hr;
}

// Hook for CreateDXGIFactory1
HRESULT WINAPI hkCreateDXGIFactory1(REFIID riid, void** ppFactory)
{
    LOG_VERBOSE("[Overlay] CreateDXGIFactory1 called!");
    
    HRESULT hr = oCreateDXGIFactory1(riid, ppFactory);
    
    if (SUCCEEDED(hr) && ppFactory && *ppFactory) {
        LOG_VERBOSE("[Overlay] Factory1 created successfully");
        LOG_VERBOSE("[Overlay] Factory1 at: 0x" << std::hex << (uintptr_t)*ppFactory);
        
        // Hook the CreateSwapChain method on this factory
        void** pFactoryVTable = *(void***)*ppFactory;
        void* pCreateSwapChainAddr = pFactoryVTable[10]; // CreateSwapChain is at index 10
        
        std::string module = GetModuleNameFromAddress(pCreateSwapChainAddr);
        LOG_VERBOSE("[Overlay] Factory1's CreateSwapChain points to: " << module);
        
        // Only hook if not already hooked
        if (!oCreateSwapChain && pCreateSwapChainAddr != (void*)&hkCreateSwapChain) {
            LOG_VERBOSE("[Overlay] Hooking IDXGIFactory1::CreateSwapChain...");
            PatchVTableEntry(pFactoryVTable, 10, &hkCreateSwapChain, (void**)&oCreateSwapChain);
            LOG_VERBOSE("[Overlay] IDXGIFactory1::CreateSwapChain HOOKED");
        } else {
            LOG_VERBOSE("[Overlay] CreateSwapChain already hooked, skipping");
        }
    }
    
    return hr;
}

bool InitializeD3D11Hook()
{
    LOG_VERBOSE("[Overlay] === InitializeD3D11Hook START ===");
    
    // NO DELAY HERE - hook D3D11CreateDeviceAndSwapChain as early as possible
    // The delay is now in the thread that patches Present
    
    LOG_VERBOSE("[Overlay] Checking xinput1_3.dll...");
    
    HMODULE hXInput = GetModuleHandleA("xinput1_3.dll");
    if (hXInput) {
        LOG_VERBOSE("[Overlay] xinput1_3.dll FOUND");
        MODULEINFO modInfo;
        if (GetModuleInformation(GetCurrentProcess(), hXInput, &modInfo, sizeof(modInfo))) {
            LOG_VERBOSE("[Overlay] xinput base: 0x" << std::hex << (uintptr_t)modInfo.lpBaseOfDll);
            LOG_VERBOSE("[Overlay] xinput size: 0x" << std::hex << modInfo.SizeOfImage);
        }
    } else {
        LOG_VERBOSE("[Overlay] xinput1_3.dll NOT found");
    }

    LOG_VERBOSE("[Overlay] MH_Initialize...");
    MH_STATUS status = MH_Initialize();
    if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) {
        LOG_ERROR("[Overlay] MH_Initialize FAILED: " << status);
        return false;
    }
    LOG_VERBOSE("[Overlay] MH_Initialize OK");
    
    // Hook CreateDXGIFactory to catch factory-based swapchain creation (used by xinput)
    LOG_VERBOSE("[Overlay] Hooking CreateDXGIFactory...");
    
    HMODULE hDXGI = GetModuleHandleA("dxgi.dll");
    if (!hDXGI) {
        LOG_VERBOSE("[Overlay] dxgi.dll not loaded, loading it...");
        hDXGI = LoadLibraryA("dxgi.dll");
    }
    
    if (hDXGI) {
        void* pCreateDXGIFactory = GetProcAddress(hDXGI, "CreateDXGIFactory");
        if (pCreateDXGIFactory) {
            LOG_VERBOSE("[Overlay] CreateDXGIFactory: 0x" << std::hex << (uintptr_t)pCreateDXGIFactory);
            
            status = MH_CreateHook(pCreateDXGIFactory, &hkCreateDXGIFactory, (void**)&oCreateDXGIFactory);
            if (status == MH_OK) {
                status = MH_EnableHook(pCreateDXGIFactory);
                if (status == MH_OK) {
                    LOG_VERBOSE("[Overlay] CreateDXGIFactory HOOKED");
                } else {
                    LOG_ERROR("[Overlay] MH_EnableHook(CreateDXGIFactory) FAILED: " << status);
                }
            } else {
                LOG_ERROR("[Overlay] MH_CreateHook(CreateDXGIFactory) FAILED: " << status);
            }
        } else {
            LOG_ERROR("[Overlay] Failed to get CreateDXGIFactory address");
        }
        
        // Also hook CreateDXGIFactory1
        void* pCreateDXGIFactory1 = GetProcAddress(hDXGI, "CreateDXGIFactory1");
        if (pCreateDXGIFactory1) {
            LOG_VERBOSE("[Overlay] CreateDXGIFactory1: 0x" << std::hex << (uintptr_t)pCreateDXGIFactory1);
            
            status = MH_CreateHook(pCreateDXGIFactory1, &hkCreateDXGIFactory1, (void**)&oCreateDXGIFactory1);
            if (status == MH_OK) {
                status = MH_EnableHook(pCreateDXGIFactory1);
                if (status == MH_OK) {
                    LOG_VERBOSE("[Overlay] CreateDXGIFactory1 HOOKED");
                } else {
                    LOG_ERROR("[Overlay] MH_EnableHook(CreateDXGIFactory1) FAILED: " << status);
                }
            } else {
                LOG_ERROR("[Overlay] MH_CreateHook(CreateDXGIFactory1) FAILED: " << status);
            }
        } else {
            LOG_ERROR("[Overlay] Failed to get CreateDXGIFactory1 address");
        }
        
        // Also hook CreateDXGIFactory2 for DXGI 1.3+
        void* pCreateDXGIFactory2 = GetProcAddress(hDXGI, "CreateDXGIFactory2");
        if (pCreateDXGIFactory2) {
            LOG_VERBOSE("[Overlay] CreateDXGIFactory2 found, but not hooking (not implemented yet)");
        }
    } else {
        LOG_ERROR("[Overlay] Failed to load dxgi.dll");
    }
    
    LOG_VERBOSE("[Overlay] Getting D3D11CreateDeviceAndSwapChain address...");
    
    HMODULE hD3D11 = GetModuleHandleA("d3d11.dll");
    if (!hD3D11) {
        LOG_VERBOSE("[Overlay] d3d11.dll not loaded yet, loading it...");
        hD3D11 = LoadLibraryA("d3d11.dll");
    }
    
    if (!hD3D11) {
        LOG_ERROR("[Overlay] Failed to get d3d11.dll");
        return false;
    }
    
    LOG_VERBOSE("[Overlay] d3d11.dll: 0x" << std::hex << (uintptr_t)hD3D11);
    
    void* pCreateFunc = GetProcAddress(hD3D11, "D3D11CreateDeviceAndSwapChain");
    if (!pCreateFunc) {
        LOG_ERROR("[Overlay] Failed to get D3D11CreateDeviceAndSwapChain");
        return false;
    }
    
    LOG_VERBOSE("[Overlay] D3D11CreateDeviceAndSwapChain: 0x" << std::hex << (uintptr_t)pCreateFunc);
    
    std::string createModule = GetModuleNameFromAddress(pCreateFunc);
    LOG_VERBOSE("[Overlay] D3D11CreateDeviceAndSwapChain in module: " << createModule);
    
    LOG_VERBOSE("[Overlay] Hooking D3D11CreateDeviceAndSwapChain...");
    
    status = MH_CreateHook(pCreateFunc, &hkD3D11CreateDeviceAndSwapChain, (void**)&oD3D11CreateDeviceAndSwapChain);
    if (status != MH_OK) {
        LOG_ERROR("[Overlay] MH_CreateHook FAILED: " << status);
        return false;
    }
    
    status = MH_EnableHook(pCreateFunc);
    if (status != MH_OK) {
        LOG_ERROR("[Overlay] MH_EnableHook FAILED: " << status);
        return false;
    }
    
    LOG_VERBOSE("[Overlay] D3D11CreateDeviceAndSwapChain HOOKED");
    LOG_VERBOSE("[Overlay] Will hook Present immediately when swapchains are created");
    LOG_VERBOSE("[Overlay] === InitializeD3D11Hook COMPLETE ===");
    
    return true;
}

void RenderOverlay()
{
    if (!g_ImGuiInit || !g_ShowOverlay)
        return;

    // Always show the overlay, even if no callbacks registered
    // (TFPayload might not be loaded yet)
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
    ImGui::Text("RJ's Trials Mod v0.1");
    ImGui::Separator();
    ImGui::Text("Press HOME to toggle devmenu");
    ImGui::Text("Press F2 to toggle this overlay");
    if (g_PresentWasAlreadyHooked) {
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.5f, 1.0f), "Chained with other mod");
    }
    ImGui::End();
}

extern "C" __declspec(dllexport) void RegisterRenderCallback(RenderCallback callback)
{
    if (!callback) {
        LOG_ERROR("[Overlay] WARNING: null callback!");
        return;
    }

    LOG_VERBOSE("[Overlay] RegisterRenderCallback: 0x" << std::hex << (uintptr_t)callback);
    g_RenderCallbacks.push_back(callback);
}

extern "C" __declspec(dllexport) void UnregisterRenderCallback(RenderCallback callback)
{
    LOG_VERBOSE("[Overlay] UnregisterRenderCallback: 0x" << std::hex << (uintptr_t)callback);
    auto it = std::find(g_RenderCallbacks.begin(), g_RenderCallbacks.end(), callback);
    if (it != g_RenderCallbacks.end()) {
        g_RenderCallbacks.erase(it);
    }
}

extern "C" __declspec(dllexport) ImGuiContext* GetImGuiContext()
{
    return g_ImGuiContext;
}
