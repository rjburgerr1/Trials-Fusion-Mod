// rendering.cpp - CONTEXT SHARING VERSION
#include "pch.h"
#include "rendering.h"
#include "devMenu.h"
#include "logging.h"
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
            LOG_ERROR("[Render] No ImGui context!");
        }
        return;
    }

    g_TestFrameCount++;
    try {
        // Debug: Check DevMenu state changes
        if (g_DevMenu) {
            bool currentVisible = g_DevMenu->IsVisible();
            if (currentVisible != g_LastDevMenuVisible) {
                LOG_VERBOSE("[Render] DevMenu visibility changed: " 
                          << (currentVisible ? "VISIBLE" : "HIDDEN"));
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
                        LOG_VERBOSE("[Render] Button clicked, toggling menu...");
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
                LOG_ERROR("[Render] DevMenu exception: " << e.what());
                g_DevMenu->Hide();
            }
            catch (...) {
                LOG_ERROR("[Render] DevMenu unknown exception!");
                g_DevMenu->Hide();
            }
        }
    }
    catch (const std::exception& e) {
        LOG_ERROR("[Render] Callback exception: " << e.what());
    }
    catch (...) {
        LOG_ERROR("[Render] Callback unknown exception!");
    }
}

namespace Rendering {

bool Initialize()
{
    LOG_INFO("[TFPayload/Rendering] Connecting to ProxyDLL's D3D11 hook...");

    HMODULE hProxyDLL = GetModuleHandleA("dbgcore.dll");
    if (!hProxyDLL) {
        LOG_ERROR("[TFPayload/Rendering] Could not find dbgcore.dll!");
        return false;
    }

    LOG_VERBOSE("[TFPayload/Rendering] Found dbgcore.dll");

    // Get all exported functions
    g_RegisterCallback = (RegisterRenderCallbackFn)GetProcAddress(hProxyDLL, "RegisterRenderCallback");
    g_UnregisterCallback = (UnregisterRenderCallbackFn)GetProcAddress(hProxyDLL, "UnregisterRenderCallback");
    g_GetImGuiContext = (GetImGuiContextFn)GetProcAddress(hProxyDLL, "GetImGuiContext");

    if (!g_RegisterCallback || !g_UnregisterCallback) {
        LOG_ERROR("[TFPayload/Rendering] Could not find callback exports!");
        return false;
    }

    if (!g_GetImGuiContext) {
        LOG_WARNING("[TFPayload/Rendering] Could not find GetImGuiContext export!");
        LOG_WARNING("[TFPayload/Rendering] Context sharing may not work!");
    } else {
        LOG_VERBOSE("[TFPayload/Rendering] Found GetImGuiContext export");
    }

    try {
        g_RegisterCallback(TFPayloadRenderCallback);
        g_IsRegistered = true;
        LOG_INFO("[TFPayload/Rendering] Registered render callback!");
        LOG_VERBOSE("[TFPayload/Rendering] Status window should be visible in-game");
    }
    catch (...) {
        LOG_ERROR("[TFPayload/Rendering] Exception while registering callback!");
        return false;
    }

    return true;
}

void Shutdown()
{
    LOG_VERBOSE("[TFPayload/Rendering] Shutting down...");

    if (g_IsRegistered && g_UnregisterCallback) {
        try {
            g_UnregisterCallback(TFPayloadRenderCallback);
            LOG_VERBOSE("[TFPayload/Rendering] Unregistered render callback");
        }
        catch (...) {
            LOG_ERROR("[TFPayload/Rendering] Exception while unregistering");
        }
        g_IsRegistered = false;
    }

    g_RegisterCallback = nullptr;
    g_UnregisterCallback = nullptr;
    g_GetImGuiContext = nullptr;

    LOG_VERBOSE("[TFPayload/Rendering] Shutdown complete");
}

void RenderFrame()
{
    // Not used
}

} // namespace Rendering
