#pragma once
#include <Windows.h>
#include <cstdint>
#include <string>

namespace HostJoin {
    // ============================================================================
    // HOST-JOIN SYSTEM
    // ============================================================================
    // This system allows joining a private match lobby by session ID without an invite.
    // 
    // WORKFLOW:
    // 1. Player1 enters a private match lobby (as host)
    // 2. The system logs the session ID to F:/session_id.txt and console
    // 3. Player1 shares the session ID with Player2
    // 4. Player2 enters the session ID in the dev menu
    // 5. Player2 clicks "Join Session" to join Player1's lobby
    // ============================================================================
    
    // Initialize the host-join system (call once at startup)
    bool Initialize(DWORD_PTR baseAddress);
    
    // Shutdown and cleanup
    void Shutdown();
    
    // Set the manager pointer (called from multiplayer.cpp hooks)
    void SetManagerPointer(void* manager, int joinMode);
    
    // Get the current/last captured session ID (host's session)
    uint32_t GetCurrentSessionId();
    
    // Check if we're currently hosting a session
    bool IsHostingSession();
    
    // Set the target session ID to join (enter this in dev menu)
    void SetTargetSessionId(uint32_t sessionId);
    
    // Get the current target session ID
    uint32_t GetTargetSessionId();
    
    // Attempt to join the target session ID
    // Returns true if the join request was sent successfully
    bool JoinSession();
    
    // Attempt to join a session by ID directly
    bool JoinSessionById(uint32_t sessionId);
    
    // Force refresh/recapture the current session ID
    void RefreshSessionId();
    
    // Copy session ID to clipboard (for easy sharing)
    bool CopySessionIdToClipboard();
    
    // Parse session ID from clipboard (for easy pasting)
    bool PasteSessionIdFromClipboard();
    
    // Get status message for UI display
    std::string GetStatusMessage();
    
    // Render ImGui controls for dev menu integration
    void RenderDevMenuControls();
}
