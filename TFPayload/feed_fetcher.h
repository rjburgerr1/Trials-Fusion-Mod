#pragma once
#include <Windows.h>
#include <string>
#include <vector>
#include <functional>

namespace FeedFetcher {

    // Track metadata structure
    struct TrackMetadata {
        int trackId;
        std::string trackName;
        std::string authorName;
        std::string description;
        int trackType;
        int difficulty;
        int rating;
        int downloads;
    };

    // Callback type for each fetched track
    using TrackCallback = std::function<void(const TrackMetadata& track)>;

    // Fetcher state
    struct FetcherState {
        bool isFetching = false;
        int startIndex = 0;
        int endIndex = 999;
        int currentIndex = 0;
        int tracksPerBatch = 52;
        std::vector<TrackMetadata> fetchedTracks;
    };

    // Configuration
    struct FetchConfig {
        int startIndex = 0;      // Start fetching from this index
        int endIndex = 999;      // Stop at this index (inclusive)
        int batchSize = 52;      // Tracks per request
        bool patchLimit = true;  // Patch the 999 limit check
        std::string outputPath = "feed_data.csv";
    };

    // Initialize the feed fetcher hooks
    bool Initialize(DWORD_PTR baseAddress);

    // Shutdown and cleanup
    void Shutdown();

    // Start fetching with specified configuration
    bool StartFetch(const FetchConfig& config);

    // Stop fetching
    void StopFetch();

    // Configure fetch parameters
    void Configure(const FetchConfig& config);

    // Set callback for processing each track
    void SetTrackCallback(TrackCallback callback);

    // Get current fetcher state
    const FetcherState& GetState();

    // Check if F2 was pressed and trigger fetch
    void CheckHotkey();

    // Save fetched tracks to file
    void SaveToFile(const std::string& path = "");

    // Get all fetched tracks
    const std::vector<TrackMetadata>& GetFetchedTracks();

    // Called by tracks.cpp to check if fetch should be redirected
    bool ShouldRedirectFetch(uint32_t& startIndex, uint32_t& count);

    // Called by tracks.cpp AFTER network response is processed
    void OnTracksReceived(void* cacheObjPtr);

}
