// rendering.cpp - CONTEXT SHARING VERSION
#include "pch.h"
#include "rendering.h"
#include "devMenu.h"
#include "logging.h"
#include "prevent-finish.h"
#include "gamemode.h"
#include "imgui/imgui.h"
#include <iostream>
#include <algorithm>

struct ImGuiContext; // Forward declare

// Function pointer types for ProxyDLL exports
typedef void (*RegisterRenderCallbackFn)(void(*)());
typedef void (*UnregisterRenderCallbackFn)(void(*)());
typedef ImGuiContext* (*GetImGuiContextFn)();

static RegisterRenderCallbackFn g_RegisterCallback = nullptr;
static UnregisterRenderCallbackFn g_UnregisterCallback = nullptr;
static GetImGuiContextFn g_GetImGuiContext = nullptr;
static bool g_IsRegistered = false;

// Safe callback with protection
void TFPayloadRenderCallback()
{
    // =========================================================================
    // NOTE: BikeSwap::ProcessPendingSwap() has been moved to the game update
    // hook instead of the render callback to avoid crashes when changing
    // bike meshes during rendering.
    // =========================================================================
    
    // =========================================================================
    // IMGUI RENDERING
    // =========================================================================
    
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

    try {
        // =========================================================================
        // PREVENT FINISH ON-SCREEN INDICATOR
        // =========================================================================
        
        // Display "Finish: BLOCKED" in red at the top middle of screen when enabled
        // Only show while actively playing a track — suppress in menus, replays, etc.
        if (PreventFinish::IsEnabled() && GameMode::IsPlaying() && PreventFinish::GetStatusString()) {
            const char* statusText = PreventFinish::GetStatusString();
            
            // Check if finishing is blocked
            if (strstr(statusText, "BLOCKED") != nullptr) {
                // Get viewport/display size
                ImGuiIO& io = ImGui::GetIO();
                float screenWidth = io.DisplaySize.x;
                float screenHeight = io.DisplaySize.y;
                
                // Text to display
                const char* displayText = "Finish: BLOCKED";
                
                // Calculate text size to center it
                ImVec2 textSize = ImGui::CalcTextSize(displayText);
                const char* buttonText = "Reset to Allow Finish";
                ImVec2 buttonSize = ImVec2(150, 0); // Auto height
                
                // Calculate total width needed (text + padding + button + padding)
                float totalWidth = textSize.x + 20 + buttonSize.x + 20;
                float totalHeight = (textSize.y > 20.0f ? textSize.y : 20.0f) + 20; // Padding
                
                float windowX = (screenWidth - totalWidth) * 0.5f;
                float windowY = 20.0f; // 20 pixels from top
                
                // Create a window with semi-transparent dark background
                ImGui::SetNextWindowPos(ImVec2(windowX, windowY));
                ImGui::SetNextWindowSize(ImVec2(totalWidth, totalHeight));
                
                // Push background color - semi-transparent dark gray
                ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.1f, 0.1f, 0.1f, 0.85f));
                ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0f, 0.0f, 0.0f, 0.8f)); // Red border
                ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 5.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 10));
                
                ImGui::Begin("PreventFinishOverlay", nullptr,
                    ImGuiWindowFlags_NoTitleBar |
                    ImGuiWindowFlags_NoResize |
                    ImGuiWindowFlags_NoMove |
                    ImGuiWindowFlags_NoScrollbar |
                    ImGuiWindowFlags_NoSavedSettings);
                
                // Draw the text in red
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.0f, 0.0f, 1.0f)); // Red
                ImGui::Text("%s", displayText);
                ImGui::PopStyleColor();
                
                ImGui::SameLine();
                ImGui::Spacing();
                ImGui::SameLine();
                
                // Add reset button
                if (ImGui::Button(buttonText, buttonSize)) {
                    // Call the reset function from PreventFinish
                    // This will reset all bike/rider values to defaults and re-enable finishing
                    PreventFinish::ResetToAllowFinish();
                }
                
                ImGui::End();
                
                ImGui::PopStyleVar(3);
                ImGui::PopStyleColor(2);
            }
        }
        
        // =========================================================================
        // DEVMENU RENDERING
        // =========================================================================
        
        // Try to render DevMenu (or just keybindings window)
        if (g_DevMenu && (g_DevMenu->IsVisible() || g_DevMenu->IsKeybindingsWindowVisible())) {
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
        
        // =========================================================================
        // IMGUI CONSOLE RENDERING
        // =========================================================================
        
        // Render the ImGui console if visible
        try {
            Logging::RenderConsole();
        }
        catch (const std::exception& e) {
            LOG_ERROR("[Render] Console exception: " << e.what());
        }
        catch (...) {
            LOG_ERROR("[Render] Console unknown exception!");
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
