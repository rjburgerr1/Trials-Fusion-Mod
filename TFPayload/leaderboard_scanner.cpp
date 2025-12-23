#include "pch.h"
#include "leaderboard_scanner.h"
#include "leaderboard_direct.h"
#include "logging.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <MinHook.h>

namespace LeaderboardScanner {

    // Static state
    static ScannerState s_state;
    static EntryCallback s_entryCallback = nullptr;
    static DWORD_PTR s_baseAddress = 0;
    static int s_totalScanned = 0;
    static std::string s_outputPath = "F:/leaderboard_scans.txt";
    
    // Multi-track scanning state
    static std::vector<std::string> s_trackQueue;
    static int s_currentTrackIndex = -1;
    static bool s_autoScanNextTrack = false;

    // Function pointers
    using ProcessLeaderboardDataFn = void(__fastcall*)(void* context);
    using GetLeaderboardEntryFn = void* (__thiscall*)(void* service, int index);
    using SetLeaderboardListRangeFn = void(__thiscall*)(void* context, int startIndex, int count);
    using RefreshLeaderboardHandlerFn = void(__thiscall*)(void* context);

    static ProcessLeaderboardDataFn o_ProcessLeaderboardData = nullptr;
    static GetLeaderboardEntryFn o_GetLeaderboardEntry = nullptr;
    static SetLeaderboardListRangeFn o_SetLeaderboardListRange = nullptr;
    static RefreshLeaderboardHandlerFn o_RefreshLeaderboardHandler = nullptr;

    // Global leaderboard service pointer
    // Ghidra: DAT_0174b308: RVA = 0x104b308
    static void* GetLeaderboardService() {
        if (s_baseAddress == 0) return nullptr;

        void** globalPtr = (void**)(s_baseAddress + 0x104b308);
        if (*globalPtr == nullptr) return nullptr;

        void** servicePtr = (void**)((char*)*globalPtr + 0x1c8);
        return *servicePtr;
    }

    std::string ReadEmbeddedString(void* ptr, int maxLen = 50) {
        if (!ptr) return "";

        // Simple validation without SEH
        if ((DWORD_PTR)ptr < 0x10000 || (DWORD_PTR)ptr > 0x7FFFFFFF) {
            return "";
        }

        char* str = (char*)ptr;
        std::string result;
        result.reserve(maxLen);

        for (int i = 0; i < maxLen; i++) {
            char c = str[i];
            if (c == 0) break;
            if (c >= 32 && c <= 126) {
                result += c;
            }
            else {
                break;
            }
        }
        return result;
    }

    std::string ReadGameString(void* stringObjPtr) {
        if (!stringObjPtr) return "";

        try {
            // Game string structure: [int* ptrToStringData]
            int** stringPtr = (int**)stringObjPtr;
            if (*stringPtr == nullptr) return "";

            // Refcounted string: [int refCount][int length][char data...]
            int* stringData = *stringPtr;
            int refCount = stringData[0];
            int length = stringData[1];

            if (length <= 0 || length > 1000) return "";

            char* str = (char*)&stringData[2];
            return std::string(str, length);
        }
        catch (...) {
            return "";
        }
    }

    // Helper to extract track ID from leaderboard context
    // The game stores track ID as a string in an object at context+0x164
    // The actual string data is at offset +0xc within that object
    std::string GetTrackIDFromContext(void* context) {
        if (!context) return "";

        try {
            // Get the string object pointer at context+0x164
            void** stringObjPtr = (void**)((char*)context + 0x164);
            if (*stringObjPtr == nullptr) return "";

            // The string data starts at offset +0xc in the object
            char* str = (char*)(*stringObjPtr) + 0xc;
            
            // Validate it's a reasonable string
            if ((DWORD_PTR)str < 0x10000 || (DWORD_PTR)str > 0x7FFFFFFF) {
                return "";
            }

            // Read up to 32 characters
            std::string result;
            for (int i = 0; i < 32; i++) {
                char c = str[i];
                if (c == 0) break;
                // Allow digits, letters, and common separators
                if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || 
                    (c >= 'a' && c <= 'z') || c == '-' || c == '_') {
                    result += c;
                }
            }

            return result;
        }
        catch (...) {
            return "";
        }
    }

    // Helper to format time
    std::string FormatTime(int timeMs) {
        int minutes = timeMs / 60000;
        int seconds = (timeMs % 60000) / 1000;
        int milliseconds = timeMs % 1000;

        char buffer[32];
        sprintf_s(buffer, "%02d:%02d.%03d", minutes, seconds, milliseconds);
        return std::string(buffer);
    }

    // Helper to get medal name
    std::string GetMedalName(int medal) {
        switch (medal) {
        case 0: return "Bronze";
        case 1: return "Silver";
        case 2: return "Gold";
        case 3: return "Platinum";
        default: return "None";
        }
    }

    // Hook for ProcessLeaderboardData - just capture context
    void __fastcall Hook_ProcessLeaderboardData(void* context) {
        if (context) {
            s_state.capturedContext = context;
            
            // Check if LeaderboardDirect triggered this fetch
            if (LeaderboardDirect::OnLeaderboardDataReceived(context)) {
                // call original
                o_ProcessLeaderboardData(context);
                return;
            }
            
            // Try to read track ID from context
            try {
                std::string trackId = GetTrackIDFromContext(context);
                if (!trackId.empty() && trackId != s_state.currentTrackId) {
                    s_state.currentTrackId = trackId;
                    LOG_VERBOSE("");
                    LOG_VERBOSE("[Scanner] Track ID: " << trackId);
                    
                    // If we're in auto-scan mode, start scanning automatically
                    if (s_autoScanNextTrack && !s_trackQueue.empty() && s_currentTrackIndex >= 0) {
                        LOG_VERBOSE("[Scanner] Auto-scanning track " << (s_currentTrackIndex + 1) << "/" << s_trackQueue.size());
                        LOG_VERBOSE("[Scanner] ========================================");
                        // Wait for data to fully load, then start scan
                        Sleep(400);
                        StartScan();
                    } else {
                        LOG_VERBOSE("[Scanner] Press F3 to scan this leaderboard");
                        LOG_VERBOSE("");
                    }
                }
            }
            catch (...) {
                // Ignore errors reading track ID
            }
        }

        o_ProcessLeaderboardData(context);
    }

    bool Initialize(DWORD_PTR baseAddress) {
        s_baseAddress = baseAddress;

        LOG_VERBOSE("[Scanner] Initializing with base address: 0x" << std::hex << baseAddress << std::dec);

        if (MH_Initialize() != MH_OK && MH_Initialize() != MH_ERROR_ALREADY_INITIALIZED) {
            LOG_ERROR("[Scanner] Failed to initialize MinHook");
            return false;
        }

        // Hook ProcessLeaderboardData
        // Ghidra: 0x00a44250 -> RVA: 0x344250
        void* targetProcessData = (void*)(baseAddress + 0x344250);

        if (MH_CreateHook(targetProcessData, &Hook_ProcessLeaderboardData,
            reinterpret_cast<LPVOID*>(&o_ProcessLeaderboardData)) != MH_OK) {
            LOG_ERROR("[Scanner] Failed to hook ProcessLeaderboardData");
            return false;
        }

        if (MH_EnableHook(targetProcessData) != MH_OK) {
            LOG_ERROR("[Scanner] Failed to enable ProcessLeaderboardData hook");
            return false;
        }

        // Store function pointers
        // GetLeaderboardEntry: RVA 0x343ab0
        // SetLeaderboardListRange: RVA 0x345530
        // RefreshLeaderboardHandler: RVA 0x345300
        o_GetLeaderboardEntry = (GetLeaderboardEntryFn)(baseAddress + 0x343ab0);
        o_SetLeaderboardListRange = (SetLeaderboardListRangeFn)(baseAddress + 0x345530);
        o_RefreshLeaderboardHandler = (RefreshLeaderboardHandlerFn)(baseAddress + 0x345300);

        LOG_VERBOSE("[Scanner] Leaderboard scanner initialized!");
        LOG_VERBOSE("[Scanner] Output file: " << s_outputPath);

        return true;
    }

    void Shutdown() {
        if (s_baseAddress == 0) return;

        void* targetProcessData = (void*)(s_baseAddress + 0x344250);
        MH_DisableHook(targetProcessData);
        MH_RemoveHook(targetProcessData);

        s_state = ScannerState();
        s_entryCallback = nullptr;
        s_baseAddress = 0;

        LOG_VERBOSE("[Scanner] Leaderboard scanner shut down");
    }

    void StartScan() {
        if (!s_state.capturedContext) {
            LOG_ERROR("[Scanner] No leaderboard context! Navigate to a leaderboard first.");
            return;
        }

        void* context = s_state.capturedContext;

        // Get total entries from context+0x150
        int totalEntries = *(int*)((char*)context + 0x150);

        if (totalEntries == 0 || totalEntries > 100000) {
            LOG_ERROR("[Scanner] Invalid total entries: " << totalEntries);
            return;
        }

        s_state.isScanning = true;
        s_state.currentPage = 0;
        s_state.totalEntries = totalEntries;
        s_state.allEntries.clear();
        s_totalScanned = 0;

        LOG_INFO("[Scanner] STARTING LEADERBOARD SCAN");
        if (!s_state.currentTrackId.empty()) {
            LOG_INFO("[Scanner] Track ID: " << s_state.currentTrackId);
        }
        LOG_INFO("[Scanner] Total entries: " << totalEntries);
        LOG_INFO("[Scanner] Pages required: " << ((totalEntries + 9) / 10));
        LOG_INFO("[Scanner] ======================================");
        LOG_INFO("");

        // Request first page
        o_SetLeaderboardListRange(context, 0, 10);
    }

    void StopScan() {
        if (s_state.isScanning) {
            LOG_INFO("[Scanner] Scan stopped by user");
            s_state.isScanning = false;
        }
    }

    void SaveToFile() {
        if (s_state.allEntries.empty()) {
            LOG_WARNING("[Scanner] No entries to save!");
            return;
        }

        try {
            std::ofstream file(s_outputPath, std::ios::app);
            if (!file.is_open()) {
                LOG_ERROR("[Scanner] Could not open output file: " << s_outputPath);
                return;
            }

            // Write header with timestamp
            auto now = std::time(nullptr);
            std::tm timeinfo;
            localtime_s(&timeinfo, &now);
            char timestamp[100];
            std::strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &timeinfo);

            file << "\n========================================\n";
            file << "Scan Date: " << timestamp << "\n";
            if (!s_state.currentTrackId.empty()) {
                file << "Track ID: " << s_state.currentTrackId << "\n";
            }
            file << "Total Entries: " << s_state.allEntries.size() << "\n";
            file << "========================================\n\n";

            // Write entries
            for (const auto& entry : s_state.allEntries) {
                std::string timeStr = FormatTime(entry.timeMs);
                std::string medalStr = GetMedalName(entry.medal);

                file << std::setw(4) << entry.rank << " | "
                     << std::setw(20) << std::left << entry.playerName << " | "
                     << "Faults: " << std::setw(3) << entry.faults << " | "
                     << "Time: " << timeStr << " | "
                     << "Medal: " << medalStr << "\n";
            }

            file << "\n";
            file.close();

            LOG_INFO("[Scanner] Scan saved to file: " << s_outputPath);
        }
        catch (...) {
            LOG_ERROR("[Scanner] Failed to write to file");
        }
    }

    bool ScanTrackById(const std::string& trackId) {
        if (!s_state.capturedContext) {
            LOG_ERROR("[Scanner] No leaderboard context! Open any leaderboard first.");
            return false;
        }

        if (trackId.empty()) {
            LOG_ERROR("[Scanner] Track ID cannot be empty");
            return false;
        }

        // Validate track ID is numeric
        for (char c : trackId) {
            if (c < '0' || c > '9') {
                LOG_ERROR("[Scanner] Track ID must be numeric, got: " << trackId);
                return false;
            }
        }

        LOG_INFO("");
        LOG_INFO("[Scanner] REQUESTING TRACK ID: " << trackId);
        LOG_INFO("");

        void* context = s_state.capturedContext;

        try {
            // Get the string object pointer at context+0x164
            void** stringObjPtr = (void**)((char*)context + 0x164);
            if (*stringObjPtr == nullptr) {
                LOG_ERROR("[Scanner] String object is null");
                return false;
            }

            // The string data starts at offset +0xc in the object
            char* str = (char*)(*stringObjPtr) + 0xc;
            
            // Validate pointer
            if ((DWORD_PTR)str < 0x10000 || (DWORD_PTR)str > 0x7FFFFFFF) {
                LOG_ERROR("[Scanner] Invalid string pointer");
                return false;
            }

            // Write the new track ID (max 32 chars to be safe)
            memset(str, 0, 32);  // Clear existing
            strncpy_s(str, 32, trackId.c_str(), trackId.length());

            LOG_VERBOSE("[Scanner] Track ID written to context");

            // Call RefreshLeaderboard to reload with new track ID
            o_RefreshLeaderboardHandler(context);

            LOG_INFO("[Scanner] Refresh called - waiting for leaderboard to load...");

            return true;
        }
        catch (...) {
            LOG_ERROR("[Scanner] Exception while setting track ID");
            return false;
        }
    }

    // Process the current page by calling GetLeaderboardEntry
    void ProcessCurrentPage() {
        if (!s_state.isScanning || !s_state.capturedContext) return;

        void* context = s_state.capturedContext;

        // Get current page info from context
        int startIndex = *(int*)((char*)context + 0x148);
        int count = *(int*)((char*)context + 0x14c);

        void* service = GetLeaderboardService();
        if (!service) {
            LOG_ERROR("[Scanner] Could not get leaderboard service");
            s_state.isScanning = false;
            return;
        }

        LOG_VERBOSE("[Scanner] Page " << s_state.currentPage << " - Processing entries " << startIndex << " to " << (startIndex + count - 1));

        // Fetch entries using GetLeaderboardEntry
        for (int i = startIndex; i < startIndex + count && i < s_state.totalEntries; i++) {
            void* entryPtr = o_GetLeaderboardEntry(service, i);

            if (entryPtr) {
                try {
                    LeaderboardEntry entry;
                    entry.rank = *(int*)((uintptr_t)entryPtr + 0x00);
                    entry.faults = *(int*)((uintptr_t)entryPtr + 0x34);
                    entry.timeMs = *(int*)((uintptr_t)entryPtr + 0x38);
                    entry.medal = *(int*)((uintptr_t)entryPtr + 0x88);
                    entry.playerName = ReadEmbeddedString((void*)((uintptr_t)entryPtr + 0x43), 30);

                    if (entry.playerName.length() < 3 || entry.playerName.find("Index") != std::string::npos) {
                        std::string alt1 = ReadEmbeddedString((void*)((uintptr_t)entryPtr + 0x4C), 30);
                        if (alt1.length() > entry.playerName.length() && alt1.find("Index") == std::string::npos) {
                            entry.playerName = alt1;
                        }
                    }

                    if (entry.playerName.length() < 3 || entry.playerName.find("Index") != std::string::npos) {
                        std::string alt2 = ReadEmbeddedString((void*)((uintptr_t)entryPtr + 0xE3), 30);
                        if (alt2.length() > 3 && alt2.find("Index") == std::string::npos) {
                            entry.playerName = alt2;
                        }
                    }

                    // Format output
                    std::string timeStr = FormatTime(entry.timeMs);
                    std::string medalStr = GetMedalName(entry.medal);

                    LOG_VERBOSE("#" << std::setw(4) << entry.rank
                        << " | " << std::setw(20) << std::left << entry.playerName
                        << " | Faults: " << std::setw(3) << entry.faults
                        << " | Time: " << timeStr
                        << " | Medal: " << medalStr);

                    s_state.allEntries.push_back(entry);
                    s_totalScanned++;

                    if (s_entryCallback) {
                        s_entryCallback(entry);
                    }
                }
                catch (...) {
                    LOG_VERBOSE("[Scanner] Error reading entry " << i);
                }
            }
        }

        // Request next page
        s_state.currentPage++;
        int nextStart = s_state.currentPage * 10;

        if (nextStart < s_state.totalEntries) {
            o_SetLeaderboardListRange(context, nextStart, 10);
        }
        else {
            LOG_INFO("");
            LOG_INFO("[Scanner] SCAN COMPLETE");
            LOG_INFO("[Scanner] ======================================");
            LOG_INFO("[Scanner] Total entries scanned: " << s_totalScanned << " / " << s_state.totalEntries);
            LOG_INFO("");
            s_state.isScanning = false;

            // Auto-save to file
            SaveToFile();
            
            // If we're in multi-track mode, load the next track
            if (s_autoScanNextTrack && !s_trackQueue.empty()) {
                s_currentTrackIndex++;
                if (s_currentTrackIndex < (int)s_trackQueue.size()) {
                    LOG_INFO("");
                    LOG_INFO("[Scanner] Loading next track...");
                    LOG_INFO("[Scanner] Track " << (s_currentTrackIndex + 1) << "/" << s_trackQueue.size());
                    LOG_INFO("");
                    Sleep(1000);  // Wait a moment between scans
                    ScanTrackById(s_trackQueue[s_currentTrackIndex]);
                } else {
                    // All tracks scanned!
                    LOG_INFO("");
                    LOG_INFO("[Scanner] ALL TRACKS SCANNED!");
                    LOG_INFO("[Scanner] Total tracks: " << s_trackQueue.size());
                    LOG_INFO("[Scanner] Results saved to: " << s_outputPath);
                    LOG_INFO("");
                    s_trackQueue.clear();
                    s_currentTrackIndex = -1;
                    s_autoScanNextTrack = false;
                }
            }
        }
    }

    void CheckHotkey() {
        // If scanning, process pages as they load
        if (s_state.isScanning && s_state.capturedContext) {
            static int frameDelay = 0;
            frameDelay++;

            if (frameDelay > 10) { // Wait ~200ms
                ProcessCurrentPage();
                frameDelay = 0;
            }
        }

        // Check for F3 key press (scan current leaderboard)
        static bool f3WasPressed = false;
        bool f3IsPressed = (GetAsyncKeyState(VK_F3) & 0x8000) != 0;

        if (f3IsPressed && !f3WasPressed) {
            if (!s_state.isScanning) {
                StartScan();
            }
            else {
                StopScan();
            }
        }

        f3WasPressed = f3IsPressed;

        // Check for F2 key press (load track by ID - test with a hardcoded ID)
        static bool f2WasPressed = false;
        bool f2IsPressed = (GetAsyncKeyState(VK_F2) & 0x8000) != 0;

        if (f2IsPressed && !f2WasPressed && !s_state.isScanning) {
            std::string testTrackId = "220120";
            LOG_INFO("[Scanner] F2 pressed - Loading test track ID: " << testTrackId);
            ScanTrackById(testTrackId);
        }

        f2WasPressed = f2IsPressed;
        
        // Check for F4 key press (scan multiple tracks)
        static bool f4WasPressed = false;
        bool f4IsPressed = (GetAsyncKeyState(VK_F4) & 0x8000) != 0;

    }

    void SetEntryCallback(EntryCallback callback) {
        s_entryCallback = callback;
    }

    void SetOutputPath(const std::string& path) {
        s_outputPath = path;
        LOG_INFO("[Scanner] Output path set to: " << path);
    }

    const ScannerState& GetState() {
        return s_state;
    }

    const std::vector<LeaderboardEntry>& GetAllEntries() {
        return s_state.allEntries;
    }

}
