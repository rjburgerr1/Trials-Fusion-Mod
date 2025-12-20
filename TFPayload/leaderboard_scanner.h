#pragma once
#include <Windows.h>
#include <string>
#include <vector>
#include <functional>

namespace LeaderboardScanner {

    struct LeaderboardEntry {
        int rank;
        std::string playerName;
        int faults;
        int timeMs;
        int medal;
    };

    // Callback type for each leaderboard entry
    using EntryCallback = std::function<void(const LeaderboardEntry& entry)>;

    // Scanner state
    struct ScannerState {
        void* capturedContext = nullptr;
        bool isScanning = false;
        int currentPage = 0;
        int totalEntries = 0;
        std::vector<LeaderboardEntry> allEntries;
        std::string currentTrackId;
    };

    // Initialize the scanner hooks
    bool Initialize(DWORD_PTR baseAddress);

    // Shutdown and cleanup
    void Shutdown();

    // Start scanning the current leaderboard
    void StartScan();

    // Stop scanning
    void StopScan();

    // Save current scan to file
    void SaveToFile();

    // Scan a specific track by ID (not yet implemented - placeholder)
    bool ScanTrackById(const std::string& trackId);

    // Set callback for processing each entry
    void SetEntryCallback(EntryCallback callback);

    // Set output file path
    void SetOutputPath(const std::string& path);

    // Get current scanner state
    const ScannerState& GetState();

    // Check if F1 was pressed and trigger scan
    void CheckHotkey();

    // Get all scanned entries
    const std::vector<LeaderboardEntry>& GetAllEntries();

} // namespace LeaderboardScanner
