#pragma once
#include <Windows.h>
#include <string>
#include <vector>
#include <functional>

namespace LeaderboardDirect {

    // Leaderboard entry structure
    struct LeaderboardEntry {
        int rank;
        std::string playerName;
        int faults;
        int timeMs;
        int medal;
    };

    // Callback types
    using EntryCallback = std::function<void(const LeaderboardEntry& entry)>;
    using CompletionCallback = std::function<void(bool success, int totalEntries)>;

    // Fetch request configuration
    struct FetchRequest {
        std::string trackId;          // Track ID to fetch leaderboard for
        int startIndex = 0;           // Starting rank index
        int count = 10;               // Number of entries to fetch
        int leaderboardType = 4;      // 0=overall, 4=track-specific, etc.
    };

    // Fetcher state
    struct FetcherState {
        bool isInitialized = false;
        bool isPatchApplied = false;
        bool isFetching = false;
        FetchRequest currentRequest;
        std::vector<LeaderboardEntry> fetchedEntries;
        int totalAvailable = 0;
        std::string lastError;
    };

    // ============================================================
    // INITIALIZATION
    // ============================================================
    
    // Initialize the direct fetcher (call once at DLL load)
    bool Initialize(DWORD_PTR baseAddress);
    
    // Shutdown and cleanup
    void Shutdown();

    // ============================================================
    // PATCH MANAGEMENT
    // ============================================================
    
    // Apply the patch to bypass track manager check
    bool ApplyPatch();
    
    // Remove the patch (restore original bytes)
    bool RemovePatch();
    
    // Check if patch is currently applied
    bool IsPatchApplied();

    // ============================================================
    // CORE FUNCTIONALITY
    // ============================================================
    
    // Fetch leaderboard data for a specific track ID
    // Requires patch to be applied OR being in a valid track context
    bool FetchLeaderboard(const FetchRequest& request);
    
    // Check if a fetch is in progress
    bool IsFetching();

    // ============================================================
    // CALLBACKS
    // ============================================================
    
    // Set callback for each entry received
    void SetEntryCallback(EntryCallback callback);
    
    // Set callback for when fetch completes
    void SetCompletionCallback(CompletionCallback callback);

    // ============================================================
    // STATE ACCESS
    // ============================================================
    
    // Get current state
    const FetcherState& GetState();
    
    // Get fetched entries
    const std::vector<LeaderboardEntry>& GetEntries();
    
    // Get last error message
    const std::string& GetLastError();

    // ============================================================
    // INTER-MODULE COMMUNICATION
    // ============================================================
    
    // Called by leaderboard_scanner when ProcessLeaderboardData fires
    // Returns true if we triggered the fetch and captured results
    bool OnLeaderboardDataReceived(void* context);

    // ============================================================
    // UTILITIES
    // ============================================================
    
    // Format time in milliseconds to MM:SS.mmm
    std::string FormatTime(int timeMs);

    // ============================================================
    // UPDATE / HOTKEYS
    // ============================================================
    
    // Check hotkeys (F10 = test fetch, F11 = toggle patch)
    void CheckHotkey();

} // namespace LeaderboardDirect
