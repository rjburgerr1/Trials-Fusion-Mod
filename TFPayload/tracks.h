// tracks.h
#pragma once
#include <string>
#include <cstdint>
#include <functional>
#include <vector>

namespace Tracks {
    struct TrackInfo {
        // Track identification
        uint16_t trackId = 0;
        std::string trackName;
        std::string description;

        // Creator information
        uint64_t creatorUID = 0;
        std::string creatorName;
        std::string creatorImage;  // Not used - was misidentified

        // Statistics
        uint32_t likeCount = 0;
        uint32_t dislikeCount = 0;
        uint32_t downloadCount = 0;

        // Note: Play count, completion count, success rate, and avg time
        // are not available in this structure

        // Upload date info (year/month/day can be extracted separately)
        uint32_t uploadedTimestamp = 0;

        bool isValid = false;
    };

    // Auto-scroll configuration
    struct AutoScrollConfig {
        bool enabled = false;
        uint32_t delayMs = 200;         // Delay between scrolls (default 200ms)
        uint32_t maxScrolls = 0;        // 0 = infinite
        bool useRightArrow = true;      // true = Right Arrow, false = Down Arrow
    };

    // Auto-search configuration
    struct AutoSearchConfig {
        std::string searchTerm = "";
        uint32_t delayBetweenSteps = 500;   // Delay between each step (ms)
        uint32_t delayAfterSearch = 2000;   // Delay after search before scrolling (ms)
        bool autoScrollAfterSearch = true;  // Start auto-scrolling after search
        bool fromEmptySearch = false;       // Flag indicating previous search was empty
        AutoScrollConfig scrollConfig;      // Scroll config to use after search
    };

    // Pagination info structure
    struct PaginationInfo {
        int totalResults = 0;
        int maxPages = 0;
        int pageSize = 50;  // Approximate page size
        bool isValid = false;
    };

    // Callback for when track info is captured
    using TrackUpdateCallback = std::function<void(const TrackInfo&)>;

    // Initialize track metadata capture
    // Hooks at Ghidra: 0x00a5088f / trials_fusion.exe+0x35088F
    bool Initialize();

    // Cleanup hooks
    void Shutdown();

    // Set callback for track updates
    void SetUpdateCallback(TrackUpdateCallback callback);

    // Enable/disable logging to tracks_debug.log
    void SetLoggingEnabled(bool enabled);

    // Manually dump track structure (for debugging)
    void DumpTrackStructure(void* trackPtr, size_t size = 0x200);

    // Auto-scroll controls
    void StartAutoScroll(const AutoScrollConfig& config = AutoScrollConfig());
    void StopAutoScroll();
    bool IsAutoScrolling();
    AutoScrollConfig GetAutoScrollConfig();
    void SetAutoScrollDelay(uint32_t delayMs);

    // Auto-search controls
    void StartAutoSearch(const AutoSearchConfig& config);
    void StopAutoSearch();
    bool IsAutoSearching();
    void SwitchSearchTerm(const AutoSearchConfig& config);  // New search from results page

    // Search term list management
    void SetSearchTerms(const std::vector<std::string>& terms);
    void CycleToNextSearch();  // Automatically navigate and search next term
    void SetAutoCycleEnabled(bool enabled);  // Enable/disable auto-cycling after each search
    bool IsAutoCycleEnabled();  // Check if auto-cycling is enabled

    // Killswitch controls (emergency stop)
    void ActivateKillSwitch();    // Stop ALL operations immediately (scroll + search)
    void DeactivateKillSwitch();  // Reset killswitch, allow operations to resume
    bool IsKillSwitchActivated(); // Check if killswitch is active

    // File loading
    bool LoadSearchTerms(const std::string& filepath);  // Load search terms from file

    // Pagination info
    PaginationInfo GetPaginationInfo();  // Get current pagination info
    void LogPaginationInfo();            // Log pagination to console

    // Search statistics
    void PrintSearchStatsSummary();      // Print summary of all searches performed
    void ClearSearchStats();             // Clear search statistics history
}