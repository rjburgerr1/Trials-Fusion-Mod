#include "Overlay.h"
#include "Keybinds.h"
#include <iostream>

// ImGui
#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"

// Globals
static bool g_ImGuiInit = false;
static ID3D11Device* g_Device = nullptr;
static ID3D11DeviceContext* g_Context = nullptr;

void InitImGuiForSwapChain(IDXGISwapChain* swap)
{
    if (g_ImGuiInit) return;

    HRESULT hr = swap->GetDevice(__uuidof(ID3D11Device), (void**)&g_Device);
    if (FAILED(hr) || !g_Device)
        return;

    g_Device->GetImmediateContext(&g_Context);

    // Get HWND
    DXGI_SWAP_CHAIN_DESC desc{};
    swap->GetDesc(&desc);
    HWND hwnd = desc.OutputWindow;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_Device, g_Context);

    g_ImGuiInit = true;
    std::cout << "[ImGui] Initialized.\n";
}

void RenderOverlay()
{
    if (!g_ImGuiInit || !g_ShowOverlay)
        return;

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

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
    ImGui::End();

    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}
