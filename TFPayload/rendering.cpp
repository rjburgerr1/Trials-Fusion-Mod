// rendering.cpp - CONTEXT SHARING VERSION
#include "pch.h"
#include "rendering.h"
#include "devMenu.h"
#include "imgui/imgui.h"
#include <iostream>

struct ImGuiContext; // Forward declare

// Function pointer types for ProxyDLL exports
typedef void (*RegisterRenderCallbackFn)(void(*)());
typedef void (*UnregisterRenderCallbackFn)(void(*)());
typedef ImGuiContext* (*GetImGuiContextFn)();

static RegisterRenderCallbackFn g_RegisterCallback = nullptr;
static UnregisterRenderCallbackFn g_UnregisterCallback = nullptr;
static GetImGuiContextFn g_GetImGuiContext = nullptr;
static bool g_IsRegistered = false;
static int g_TestFrameCount = 0;
static bool g_ShowTestWindow = true;
static bool g_LastDevMenuVisible = false;

// Safe callback with protection
void TFPayloadRenderCallback()
{
    // Get and set ImGui context from ProxyDLL
    if (g_GetImGuiContext) {
        ImGuiContext* ctx = g_GetImGuiContext();
        if (ctx) {
            ImGui::SetCurrentContext(ctx);
        }
    }

    // Safety check: Make sure ImGui context exists
    if (!ImGui::GetCurrentContext()) {
        static int errorCount = 0;
        if (errorCount++ < 5) { // Only log first 5 errors
            std::cout << "[Render] No ImGui context!" << std::endl;
        }
        return;
    }

    g_TestFrameCount++;

    try {
        // Debug: Check DevMenu state changes
        if (g_DevMenu) {
            bool currentVisible = g_DevMenu->IsVisible();
            if (currentVisible != g_LastDevMenuVisible) {
                std::cout << "[Render] DevMenu visibility changed: " 
                          << (currentVisible ? "VISIBLE" : "HIDDEN") << std::endl;
                g_LastDevMenuVisible = currentVisible;
            }
        }

        // Simple test window that's always visible
        if (g_ShowTestWindow) {
            ImGui::SetNextWindowPos(ImVec2(400, 100), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(400, 200), ImGuiCond_FirstUseEver);
            
            if (ImGui::Begin("TFPayload Status", &g_ShowTestWindow)) {
                ImGui::Text("TFPayload is rendering!");
                ImGui::Text("Frame count: %d", g_TestFrameCount);
                ImGui::Text("ImGui context: 0x%p", ImGui::GetCurrentContext());
                ImGui::Separator();
                
                if (g_DevMenu) {
                    bool isVisible = g_DevMenu->IsVisible();
                    ImGui::Text("DevMenu exists: YES");
                    ImGui::Text("DevMenu visible: %s", isVisible ? "YES" : "NO");
                    
                    if (ImGui::Button(isVisible ? "Hide Dev Menu" : "Show Dev Menu")) {
                        std::cout << "[Render] Button clicked, toggling menu..." << std::endl;
                        g_DevMenu->Toggle();
                    }
                    
                    ImGui::Separator();
                    ImGui::TextColored(ImVec4(1, 1, 0, 1), "Press HOME to toggle");
                } else {
                    ImGui::TextColored(ImVec4(1, 0, 0, 1), "DevMenu: NOT INITIALIZED");
                }
            }
            ImGui::End();
        }
        
        // Try to render DevMenu
        if (g_DevMenu && g_DevMenu->IsVisible()) {
            try {
                g_DevMenu->Render();
            }
            catch (const std::exception& e) {
                std::cout << "[Render] DevMenu exception: " << e.what() << std::endl;
                g_DevMenu->Hide();
            }
            catch (...) {
                std::cout << "[Render] DevMenu unknown exception!" << std::endl;
                g_DevMenu->Hide();
            }
        }
    }
    catch (const std::exception& e) {
        std::cout << "[Render] Callback exception: " << e.what() << std::endl;
    }
    catch (...) {
        std::cout << "[Render] Callback unknown exception!" << std::endl;
    }
}

namespace Rendering {

bool Initialize()
{
    std::cout << "[TFPayload/Rendering] Connecting to ProxyDLL's D3D11 hook..." << std::endl;

    HMODULE hProxyDLL = GetModuleHandleA("dbgcore.dll");
    if (!hProxyDLL) {
        std::cout << "[TFPayload/Rendering] ERROR: Could not find dbgcore.dll!" << std::endl;
        return false;
    }

    std::cout << "[TFPayload/Rendering] Found dbgcore.dll" << std::endl;

    // Get all exported functions
    g_RegisterCallback = (RegisterRenderCallbackFn)GetProcAddress(hProxyDLL, "RegisterRenderCallback");
    g_UnregisterCallback = (UnregisterRenderCallbackFn)GetProcAddress(hProxyDLL, "UnregisterRenderCallback");
    g_GetImGuiContext = (GetImGuiContextFn)GetProcAddress(hProxyDLL, "GetImGuiContext");

    if (!g_RegisterCallback || !g_UnregisterCallback) {
        std::cout << "[TFPayload/Rendering] ERROR: Could not find callback exports!" << std::endl;
        return false;
    }

    if (!g_GetImGuiContext) {
        std::cout << "[TFPayload/Rendering] WARNING: Could not find GetImGuiContext export!" << std::endl;
        std::cout << "[TFPayload/Rendering] Context sharing may not work!" << std::endl;
    } else {
        std::cout << "[TFPayload/Rendering] Found GetImGuiContext export" << std::endl;
    }

    try {
        g_RegisterCallback(TFPayloadRenderCallback);
        g_IsRegistered = true;
        std::cout << "[TFPayload/Rendering] Registered render callback!" << std::endl;
        std::cout << "[TFPayload/Rendering] Status window should be visible in-game" << std::endl;
    }
    catch (...) {
        std::cout << "[TFPayload/Rendering] Exception while registering callback!" << std::endl;
        return false;
    }

    return true;
}

void Shutdown()
{
    std::cout << "[TFPayload/Rendering] Shutting down..." << std::endl;

    if (g_IsRegistered && g_UnregisterCallback) {
        try {
            g_UnregisterCallback(TFPayloadRenderCallback);
            std::cout << "[TFPayload/Rendering] Unregistered render callback" << std::endl;
        }
        catch (...) {
            std::cout << "[TFPayload/Rendering] Exception while unregistering" << std::endl;
        }
        g_IsRegistered = false;
    }

    g_RegisterCallback = nullptr;
    g_UnregisterCallback = nullptr;
    g_GetImGuiContext = nullptr;

    std::cout << "[TFPayload/Rendering] Shutdown complete" << std::endl;
}

void RenderFrame()
{
    // Not used
}

} // namespace Rendering
