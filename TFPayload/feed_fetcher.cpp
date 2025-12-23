#include "pch.h"
#include "feed_fetcher.h"
#include <MinHook.h>
#include <fstream>
#include <sstream>
#include <iostream>
#include <iomanip>

namespace FeedFetcher {
    // GetTrackByIndexFromCache: RVA: 0x3565d0
    // EnqueueTrackFetchTask: RVA: 0x560060
    constexpr DWORD_PTR RVA_GET_TRACK_BY_INDEX = 0x3565d0;
    constexpr DWORD_PTR RVA_ENQUEUE_TRACK_FETCH = 0x560060;
    constexpr size_t TRACK_ENTRY_SIZE = 0x138;  // Size of each track entry in cache

    // STATE
    static FetcherState g_state;
    static FetchConfig g_config;
    static TrackCallback g_trackCallback = nullptr;
    static DWORD_PTR g_baseAddress = 0;
    
    // Track where fetched tracks came from so we can copy them
    static uint32_t g_lastFetchedStartIndex = 0;
    static uint32_t g_lastFetchedCount = 0;
    
    // Original function pointers
    typedef int(__thiscall* GetTrackByIndexFromCache_t)(void* thisPtr, int param_1, int param_2, char* param_3);
    static GetTrackByIndexFromCache_t g_originalGetTrackByIndex = nullptr;

    // HELPERS
    
    void LogToFile(const std::string& message) {
        std::ofstream logFile("feed_fetcher_log.txt", std::ios::app);
        if (logFile.is_open()) {
            logFile << message << std::endl;
            logFile.close();
        }
        // Also print to console
        std::cout << message << std::endl;
    }

    // HOOKS

    // Hook for GetTrackByIndexFromCache
    int __fastcall Hook_GetTrackByIndexFromCache(void* thisPtr, void* edx, int param_1, int param_2, char* param_3) {
        if (g_state.isFetching) {
            std::stringstream ss;
            ss << "[GetTrackByIndexFromCache] UI requesting index=" << param_2;
            LogToFile(ss.str());
        }
        
        // Call original function
        int result = g_originalGetTrackByIndex(thisPtr, param_1, param_2, param_3);
        
        if (g_state.isFetching && result != 0) {
            std::stringstream ss;
            ss << "[GetTrackByIndexFromCache] Returned valid data for index=" << param_2;
            LogToFile(ss.str());
        }
        
        return result;
    }

    // PUBLIC INTERFACE
    bool Initialize(DWORD_PTR baseAddress) {
        g_baseAddress = baseAddress;
        
        LogToFile("Feed Fetcher Initializing");
        
        // Calculate absolute addresses
        DWORD_PTR getTrackByIndexAddr = baseAddress + RVA_GET_TRACK_BY_INDEX;
        DWORD_PTR enqueueTrackFetchAddr = baseAddress + RVA_ENQUEUE_TRACK_FETCH;
        
        // Use internally tracked values
        uint32_t receivedStartIndex = g_lastFetchedStartIndex;
        uint32_t receivedCount = g_lastFetchedCount;
        
        std::stringstream ss;
        ss << "GetTrackByIndexFromCache at: 0x" << std::hex << getTrackByIndexAddr;
        LogToFile(ss.str());
        ss.str("");
        ss << "EnqueueTrackFetchTask at: 0x" << std::hex << enqueueTrackFetchAddr;
        LogToFile(ss.str());
        
        // Hook GetTrackByIndexFromCache for logging
        if (MH_CreateHook(
            reinterpret_cast<LPVOID>(getTrackByIndexAddr),
            &Hook_GetTrackByIndexFromCache,
            reinterpret_cast<LPVOID*>(&g_originalGetTrackByIndex)) != MH_OK) {
            LogToFile("ERROR: Failed to create hook for GetTrackByIndexFromCache");
            return false;
        }
        
        // Enable hooks
        if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK) {
            LogToFile("ERROR: Failed to enable hooks");
            return false;
        }
        // Use LOG_VERBOSE instead of LogToFile for initialization message
        std::cout << "Feed Fetcher initialized successfully" << std::endl;
        return true;
    }

    void Shutdown() {
        LogToFile("Feed Fetcher shutting down");
        
        if (g_state.isFetching) {
            StopFetch();
        }
        
        if (g_originalGetTrackByIndex) {
            MH_DisableHook(reinterpret_cast<LPVOID>(g_baseAddress + RVA_GET_TRACK_BY_INDEX));
        }
    }

    bool StartFetch(const FetchConfig& config) {
        if (g_state.isFetching) {
            LogToFile("ERROR: Already fetching");
            return false;
        }
        
        g_config = config;
        g_state.isFetching = true;
        g_state.startIndex = config.startIndex;
        g_state.endIndex = config.endIndex;
        g_state.currentIndex = config.startIndex;
        g_state.tracksPerBatch = config.batchSize;
        g_state.fetchedTracks.clear();
        
        std::stringstream ss;
        ss << "Starting fetch: indices " << config.startIndex << "-" << config.endIndex 
           << ", batch size=" << config.batchSize;
        LogToFile(ss.str());
        
        std::cout << "\n=== FEED FETCHER STARTED ===" << std::endl;
        std::cout << "Fetching tracks " << config.startIndex << "-" << config.endIndex << std::endl;
        std::cout << "Navigate to Track Central to trigger fetching..." << std::endl;
        std::cout << "Press F8 to stop fetching" << std::endl;
        
        return true;
    }

    void StopFetch() {
        if (!g_state.isFetching) {
            return;
        }
        
        g_state.isFetching = false;
        
        std::stringstream ss;
        ss << "Stopped fetching at index " << g_state.currentIndex 
           << ", total tracks fetched: " << g_state.fetchedTracks.size();
        LogToFile(ss.str());
        
        std::cout << "\n=== FEED FETCHER STOPPED ===" << std::endl;
        std::cout << "Fetched " << g_state.fetchedTracks.size() << " tracks" << std::endl;
        
        // Auto-save
        if (!g_state.fetchedTracks.empty()) {
            SaveToFile(g_config.outputPath);
        }
    }

    void Configure(const FetchConfig& config) {
        g_config = config;
    }

    void SetTrackCallback(TrackCallback callback) {
        g_trackCallback = callback;
    }

    const FetcherState& GetState() {
        return g_state;
    }

    void CheckHotkey() {
        // No longer used - hotkeys in dllmain now
    }

    void SaveToFile(const std::string& path) {
        std::string filepath = path.empty() ? g_config.outputPath : path;
        
        std::ofstream file(filepath);
        if (!file.is_open()) {
            LogToFile("ERROR: Could not open output file: " + filepath);
            return;
        }
        
        // CSV header
        file << "TrackID,TrackName,Author,Description,Type,Difficulty,Rating,Downloads\n";
        
        // Write tracks
        for (const auto& track : g_state.fetchedTracks) {
            file << track.trackId << ","
                 << track.trackName << ","
                 << track.authorName << ","
                 << track.description << ","
                 << track.trackType << ","
                 << track.difficulty << ","
                 << track.rating << ","
                 << track.downloads << "\n";
        }
        
        file.close();
        
        std::stringstream ss;
        ss << "Saved " << g_state.fetchedTracks.size() << " tracks to " << filepath;
        LogToFile(ss.str());
        std::cout << ss.str() << std::endl;
    }

    const std::vector<TrackMetadata>& GetFetchedTracks() {
        return g_state.fetchedTracks;
    }

    // Called by tracks.cpp hook to check if we want to redirect the fetch request
    bool ShouldRedirectFetch(uint32_t& startIndex, uint32_t& count) {
        if (!g_state.isFetching) {
            return false;
        }
        
        std::stringstream ss;
        ss << "[FeedFetcher] *** Network fetch intercepted: startIndex=" << startIndex << ", count=" << count;
        LogToFile(ss.str());
        
        // Calculate where we should be in our custom range
        int targetIndex = g_state.currentIndex;
        
        // Check if we've finished our range
        if (targetIndex > g_config.endIndex) {
            LogToFile("[FeedFetcher] Reached end of fetch range, stopping");
            StopFetch();
            return false;
        }
        
        // Calculate how many tracks remain in our target range
        int remaining = g_config.endIndex - targetIndex + 1;
        if (remaining <= 0) {
            LogToFile("[FeedFetcher] No tracks remaining, stopping");
            StopFetch();
            return false;
        }
        
        // Determine fetch count (up to batch size or remaining)
        uint32_t fetchCount = (std::min)((uint32_t)remaining, (uint32_t)g_config.batchSize);
        
        ss.str("");
        ss << "[FeedFetcher] *** Redirecting network fetch: " << startIndex << " -> " << targetIndex << " (count=" << fetchCount << ")";
        LogToFile(ss.str());
        
        // Store what we fetched so we can copy it later
        g_lastFetchedStartIndex = targetIndex;
        g_lastFetchedCount = fetchCount;
        
        // Update the parameters
        startIndex = targetIndex;
        count = fetchCount;
        
        // Advance our position for next fetch
        g_state.currentIndex += fetchCount;
        
        return true;
    }

    // Called by tracks.cpp AFTER network response is processed
    // cachePtr is the "thisPtr" from ConvertTrackTagsAndProcessPacket  
    // Cache structure: thisPtr + 0x60890 is track array base
    // Uses internal tracking (g_lastFetchedStartIndex/Count) to know what to copy
    void OnTracksReceived(void* cacheObjPtr) {
        if (!g_state.isFetching) {
            return;  // Not in fetch mode, nothing to do
        }
        
        // Use internally tracked values from the fetch we just did
        uint32_t receivedStartIndex = g_lastFetchedStartIndex;
        uint32_t receivedCount = g_lastFetchedCount;
        
        if (receivedCount == 0) {
            LogToFile("[OnTracksReceived] No tracks were fetched, nothing to copy");
            return;
        }
        
        std::stringstream ss;
        ss << "\n[OnTracksReceived] Processing fetch of " << receivedCount << " tracks starting at index " << receivedStartIndex;
        LogToFile(ss.str());

        // Track array is at: cacheObjPtr + 0x60890
        // Each track is 0x138 bytes
        uint8_t* cacheBase = reinterpret_cast<uint8_t*>(cacheObjPtr) + 0x60890;
        
        ss.str("");
        ss << "[OnTracksReceived] Cache base at: 0x" << std::hex << reinterpret_cast<uintptr_t>(cacheBase);
        LogToFile(ss.str());
        
        // Calculate range to copy
        int rangeSize = g_config.endIndex - g_config.startIndex + 1;
        int copyCount = (std::min)((int)receivedCount, rangeSize);
        
        ss.str("");
        ss << "[OnTracksReceived] Will copy " << std::dec << copyCount << " tracks from cache indices " 
           << receivedStartIndex << "-" << (receivedStartIndex + copyCount - 1)
           << " to cache indices 0-" << (copyCount - 1);
        LogToFile(ss.str());
        
        // Copy tracks from high indices to low indices
        for (int i = 0; i < copyCount; i++) {
            uint8_t* srcTrack = cacheBase + ((receivedStartIndex + i) * TRACK_ENTRY_SIZE);
            uint8_t* dstTrack = cacheBase + (i * TRACK_ENTRY_SIZE);
            
            // Copy the entire track structure
            memcpy(dstTrack, srcTrack, TRACK_ENTRY_SIZE);
            
            if (i < 3 || i >= copyCount - 3) {  // Log first 3 and last 3
                ss.str("");
                ss << "[OnTracksReceived] Copied track from cache index " << (receivedStartIndex + i) << " to index " << i;
                LogToFile(ss.str());
            }
        }
        
        LogToFile("[OnTracksReceived] *** Cache copy complete! UI should now see tracks at indices 0-9 ***\n");
    }

}
