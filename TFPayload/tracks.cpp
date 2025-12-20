#include "pch.h"
#include "tracks.h"
#include <MinHook.h>
#include <iostream>
#include <fstream>
#include <Windows.h>
#include <thread>
#include <atomic>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <map>
#include <vector>
#include <unordered_set>
#include <mutex>
#include <condition_variable>
#include <deque>

namespace Tracks {
    static TrackUpdateCallback g_updateCallback = nullptr;
    static bool g_loggingEnabled = false;
    static std::ofstream g_logFile;
    static std::ofstream g_csvFile;
    static std::ofstream g_maxPagesLogFile;
    static bool g_csvLoggingEnabled = false;

    // Store captured ESI value (points to track structure)
    static void* g_capturedESI = nullptr;

    // Trampoline function pointer (points to relocated original code)
    static void* g_trampolineFunc = nullptr;

    // Hook location for cleanup
    static void* g_hookLocation = nullptr;

    // Auto-scroll state
    static std::atomic<bool> g_autoScrollEnabled{ false };
    static AutoScrollConfig g_autoScrollConfig;
    static std::atomic<uint32_t> g_scrollCount{ 0 };
    static std::string g_currentSearchTerm;  // Track current search term separately

    // Track deduplication for auto-stop detection
    static std::unordered_set<uint64_t> g_seenTrackIds;
    static std::chrono::steady_clock::time_point g_lastNewTrackTime;
    static std::chrono::steady_clock::time_point g_lastAnyTrackTime;  // Track ANY scan activity
    static std::mutex g_trackIdMutex;
    static const int AUTOSTOP_TIMEOUT_MS = 2000; // 2 seconds without new tracks
    static std::atomic<uint32_t> g_totalTracksScannedThisSearch{ 0 };
    static std::atomic<uint32_t> g_uniqueTracksThisSearch{ 0 };
    static const int MAX_TRACKS_WARNING_THRESHOLD = 1000; // Warn if we hit max pages

    // Per-search statistics tracking
    struct SearchStats {
        std::string searchTerm;
        uint32_t totalTracksScanned;
        uint32_t uniqueTracks;
        bool hitMaxPages;
        bool noTracksFound;  // NEW: Track if search yielded zero results
    };
    static std::vector<SearchStats> g_searchStatsHistory;

    // Track detection for no-results scenario
    static std::atomic<bool> g_firstPageScanned{ false };  // Tracks if we've seen ANY track on first page
    static std::chrono::steady_clock::time_point g_searchExecutedTime;  // When we pressed Enter on search
    static const int NO_TRACKS_DETECTION_MS = 1000;  // Time to wait to detect no tracks

    // Search state
    static std::vector<std::string> g_searchTerms;
    static std::atomic<int> g_currentSearchIndex{ -1 };
    static std::atomic<bool> g_autoCycleEnabled{ true };
    static std::atomic<bool> g_killSwitchActivated{ false };

    // Worker thread command queue
    enum class WorkerCommand {
        None,
        InitialSearch,
        SwitchSearch,
        AutoScroll,
        Stop
    };

    struct WorkerTask {
        WorkerCommand command;
        AutoSearchConfig searchConfig;
        AutoScrollConfig scrollConfig;
    };

    static std::mutex g_queueMutex;
    static std::condition_variable g_queueCV;
    static std::deque<WorkerTask> g_taskQueue;
    static std::atomic<bool> g_workerThreadRunning{ false };
    static std::thread g_workerThread;

    static void LogMessage(const std::string& message) {
        if (g_loggingEnabled) {
            // Output to console
            std::cout << message << std::endl;

            // Output to file
            if (g_logFile.is_open()) {
                g_logFile << message << std::endl;
                g_logFile.flush();
            }
        }
    }

    // Load search terms from a file
    static bool LoadSearchTermsFromFile(const std::string& filepath) {
        std::ifstream file(filepath);
        if (!file.is_open()) {
            char buffer[256];
            sprintf_s(buffer, "[Track] Failed to open search terms file: %s", filepath.c_str());
            LogMessage(buffer);
            return false;
        }

        std::vector<std::string> terms;
        std::string line;
        while (std::getline(file, line)) {
            // Trim whitespace
            size_t start = line.find_first_not_of(" \t\r\n");
            size_t end = line.find_last_not_of(" \t\r\n");

            if (start != std::string::npos && end != std::string::npos) {
                std::string term = line.substr(start, end - start + 1);

                // Skip empty lines and comments (lines starting with #)
                if (!term.empty() && term[0] != '#') {
                    terms.push_back(term);
                }
            }
        }
        file.close();

        if (terms.empty()) {
            LogMessage("[Track] Warning: No search terms found in file");
            return false;
        }

        g_searchTerms = terms;
        g_currentSearchIndex = -1;

        char buffer[512];
        sprintf_s(buffer, "[Track] Loaded %zu search terms from %s:", terms.size(), filepath.c_str());
        LogMessage(buffer);

        return true;
    }

    // Helper function to escape CSV fields
    static std::string EscapeCSV(const std::string& field) {
        if (field.find(',') != std::string::npos ||
            field.find('"') != std::string::npos ||
            field.find('\n') != std::string::npos ||
            field.find('\r') != std::string::npos) {
            std::string escaped = "\"";
            for (char c : field) {
                if (c == '"') {
                    escaped += "\"\"";
                }
                else {
                    escaped += c;
                }
            }
            escaped += "\"";
            return escaped;
        }
        return field;
    }

    static void DumpHex(const void* data, size_t size, const std::string& label) {
        if (!g_loggingEnabled) return;

        char buffer[256];
        sprintf_s(buffer, "[Track] %s (%zu bytes):", label.c_str(), size);
        LogMessage(buffer);

        const uint8_t* bytes = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < size; i += 16) {
            char line[256];
            int pos = sprintf_s(line, "  %04zX: ", i);

            for (size_t j = 0; j < 16 && (i + j) < size; j++) {
                pos += sprintf_s(line + pos, sizeof(line) - pos, "%02X ", bytes[i + j]);
            }

            for (size_t j = size - i; j < 16; j++) {
                pos += sprintf_s(line + pos, sizeof(line) - pos, "   ");
            }

            pos += sprintf_s(line + pos, sizeof(line) - pos, " |");
            for (size_t j = 0; j < 16 && (i + j) < size; j++) {
                uint8_t b = bytes[i + j];
                pos += sprintf_s(line + pos, sizeof(line) - pos, "%c",
                    (b >= 32 && b < 127) ? b : '.');
            }
            sprintf_s(line + pos, sizeof(line) - pos, "|");

            LogMessage(line);
        }
    }

    static bool IsValidPointer(const void* ptr) {
        if (!ptr) return false;

        // Check for obvious invalid pointers
        uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
        if (addr < 0x10000 || addr > 0x7FFFFFFFFFFF) {
            return false;  // Too low or too high
        }

        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQuery(ptr, &mbi, sizeof(mbi)) == 0) {
            return false;
        }

        if (mbi.State != MEM_COMMIT) {
            return false;
        }

        if (!(mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE))) {
            return false;
        }

        return true;
    }

    static bool SafeReadString(const void* ptr, std::string& outStr, size_t maxLen = 256) {
        if (!IsValidPointer(ptr)) return false;

        const char* charPtr = static_cast<const char*>(ptr);
        size_t len = 0;

        while (len < maxLen) {
            if (!IsValidPointer(charPtr + len)) break;
            if (charPtr[len] == '\0') break;
            len++;
        }

        if (len > 0 && len < maxLen) {
            outStr.assign(charPtr, len);
            return true;
        }
        return false;
    }

    template<typename T>
    static bool SafeRead(const void* ptr, T& outValue) {
        if (!IsValidPointer(ptr) || !IsValidPointer(static_cast<const uint8_t*>(ptr) + sizeof(T) - 1))
            return false;
        outValue = *static_cast<const T*>(ptr);
        return true;
    }

    // Verified track structure offsets
    struct TrackOffsets {
        static const int TRACK_ID = 0x00;
        static const int TRACK_NAME_PTR = 0x4C;
        static const int DESCRIPTION_PTR = 0x88;
        static const int CREATOR_NAME_PTR = 0x98;
        static const int CREATOR_UID = 0xA0;
        static const int UPLOAD_YEAR = 0xB0;
        static const int UPLOAD_MONTH = 0xB2;
        static const int UPLOAD_DAY = 0xB6;
        static const int LIKE_COUNT = 0xC0;
        static const int DISLIKE_COUNT = 0xC4;
        static const int DOWNLOAD_COUNT = 0xC8;
    };

    // Forward declarations
    static void SimulateKeyPress(WORD vkCode);

    static void ProcessTrackData(void* trackPtr) {
        try {
            if (!trackPtr || !IsValidPointer(trackPtr)) {
                LogMessage("[Track] Invalid track pointer - hook called but pointer is invalid");
                return;
            }

            char buffer[512];
            TrackInfo info = {};
            info.isValid = true;
            const uint8_t* base = static_cast<const uint8_t*>(trackPtr);

            LogMessage("\n[Track] ========== Track Data Captured ==========");
            sprintf_s(buffer, "[Track] Track Structure (ESI): 0x%p", trackPtr);
            LogMessage(buffer);

            // Read track ID first - if it's 0, this might be invalid/empty slot
            uint32_t trackId = 0;
            if (!SafeRead(base + TrackOffsets::TRACK_ID, trackId) || trackId == 0) {
                LogMessage("[Track] Invalid or empty track slot (ID=0), skipping");

                // If we're seeing empty tracks, we might be at the end
                // Stop auto-scroll immediately
                if (g_autoScrollEnabled) {
                    LogMessage("[Track] WARNING: Empty track detected during auto-scroll - STOPPING");
                    g_autoScrollEnabled = false;  // Signal worker thread to stop scrolling
                }
                return;
            }

            sprintf_s(buffer, "[Track] Track ID: %u", trackId);
            LogMessage(buffer);
            info.trackId = static_cast<uint16_t>(trackId);
            
            // Mark that we've scanned a VALID track (for no-tracks detection)
            // This must come AFTER we verify trackId != 0
            if (!g_firstPageScanned) {
                LogMessage("[Track] *** FIRST VALID TRACK DETECTED - Setting g_firstPageScanned flag ***");
                g_firstPageScanned = true;
            }

            // Read creator UID
            uint64_t creatorUID = 0;
            if (SafeRead(base + TrackOffsets::CREATOR_UID, creatorUID)) {
                sprintf_s(buffer, "[Track] Creator UID: %llu", creatorUID);
                LogMessage(buffer);
                info.creatorUID = creatorUID;
            }

            // Read creator name
            const void* const* creatorNamePtrPtr = reinterpret_cast<const void* const*>(base + TrackOffsets::CREATOR_NAME_PTR);
            if (IsValidPointer(creatorNamePtrPtr) && IsValidPointer(*creatorNamePtrPtr)) {
                std::string creatorName;
                if (SafeReadString(*creatorNamePtrPtr, creatorName, 256)) {
                    sprintf_s(buffer, "[Track] Creator Name: %s", creatorName.c_str());
                    LogMessage(buffer);
                    info.creatorName = creatorName;
                }
            }

            // Read track name
            std::string trackName;
            const void* const* trackNamePtrPtr = reinterpret_cast<const void* const*>(base + TrackOffsets::TRACK_NAME_PTR);
            if (IsValidPointer(trackNamePtrPtr) && IsValidPointer(*trackNamePtrPtr)) {
                std::string name;
                if (SafeReadString(*trackNamePtrPtr, name, 100) && name.length() >= 3) {
                    if (name.find("::") == std::string::npos && name.find("Job") == std::string::npos) {
                        trackName = name;
                    }
                }
            }

            if (!trackName.empty()) {
                sprintf_s(buffer, "[Track] Track Name: %s", trackName.c_str());
                LogMessage(buffer);
                info.trackName = trackName;
            }

            // Read track description
            const void* const* descPtrPtr = reinterpret_cast<const void* const*>(base + TrackOffsets::DESCRIPTION_PTR);
            if (IsValidPointer(descPtrPtr) && IsValidPointer(*descPtrPtr)) {
                std::string description;
                if (SafeReadString(*descPtrPtr, description, 512) && description.length() >= 5) {
                    sprintf_s(buffer, "[Track] Description: %s", description.c_str());
                    LogMessage(buffer);
                    info.description = description;
                }
            }

            // Read statistics
            uint32_t likeCount = 0;
            if (SafeRead(base + TrackOffsets::LIKE_COUNT, likeCount)) {
                sprintf_s(buffer, "[Track] Like Count: %u", likeCount);
                LogMessage(buffer);
                info.likeCount = likeCount;
            }

            uint32_t dislikeCount = 0;
            if (SafeRead(base + TrackOffsets::DISLIKE_COUNT, dislikeCount)) {
                sprintf_s(buffer, "[Track] Dislike Count: %u", dislikeCount);
                LogMessage(buffer);
                info.dislikeCount = dislikeCount;
            }

            uint32_t downloadCount = 0;
            if (SafeRead(base + TrackOffsets::DOWNLOAD_COUNT, downloadCount)) {
                sprintf_s(buffer, "[Track] Download Count: %u", downloadCount);
                LogMessage(buffer);
                info.downloadCount = downloadCount;
            }

            // Read upload date
            uint16_t uploadYear = 0;
            uint8_t uploadMonth = 0;
            uint8_t uploadDay = 0;
            if (SafeRead(base + TrackOffsets::UPLOAD_YEAR, uploadYear)) {
                SafeRead(base + TrackOffsets::UPLOAD_MONTH, uploadMonth);
                SafeRead(base + TrackOffsets::UPLOAD_DAY, uploadDay);
                sprintf_s(buffer, "[Track] Upload Date: %u-%02u-%02u", uploadYear, uploadMonth, uploadDay);
                LogMessage(buffer);
            }

            LogMessage("[Track] ========== End Track Data ==========\n");

            // Write to CSV if enabled
            if (g_csvLoggingEnabled && g_csvFile.is_open()) {
                g_csvFile << info.trackId << ","
                    << EscapeCSV(info.trackName) << ","
                    << EscapeCSV(info.creatorName) << ","
                    << info.creatorUID << ","
                    << info.likeCount << ","
                    << info.dislikeCount << ","
                    << info.downloadCount << ","
                    << uploadYear << "-" << std::setfill('0') << std::setw(2) << (int)uploadMonth
                    << "-" << std::setw(2) << (int)uploadDay << ","
                    << EscapeCSV(info.description) << "\n";
                g_csvFile.flush();
            }

            // Check for duplicate track (for auto-stop detection)
            // IMPORTANT: This runs in the game's main thread (from the hook)
            if (info.trackId != 0 && g_autoScrollEnabled) {
                // Increment total tracks counter (atomic, thread-safe)
                g_totalTracksScannedThisSearch++;

                bool isNewTrack = false;
                {
                    std::lock_guard<std::mutex> lock(g_trackIdMutex);
                    auto result = g_seenTrackIds.insert(info.trackId);
                    isNewTrack = result.second;

                    // Update last scan time for ANY track (including duplicates)
                    g_lastAnyTrackTime = std::chrono::steady_clock::now();

                    if (isNewTrack) {
                        // New track found!
                        g_uniqueTracksThisSearch++;
                        g_lastNewTrackTime = std::chrono::steady_clock::now();
                    }
                }

                if (isNewTrack) {
                    sprintf_s(buffer, "[Track] NEW track #%u (unique: %u/%u total)",
                        info.trackId, g_uniqueTracksThisSearch.load(), g_totalTracksScannedThisSearch.load());
                    LogMessage(buffer);
                }
                else {
                    sprintf_s(buffer, "[Track] DUPLICATE track #%u (unique: %u/%u total)",
                        info.trackId, g_uniqueTracksThisSearch.load(), g_totalTracksScannedThisSearch.load());
                    LogMessage(buffer);
                }
            }

            if (g_updateCallback) {
                g_updateCallback(info);
            }
        }
        catch (const std::exception& ex) {
            char buffer[256];
            sprintf_s(buffer, "[Track] Exception: %s", ex.what());
            LogMessage(buffer);
        }
        catch (...) {
            LogMessage("[Track] Unknown exception in ProcessTrackData");
        }
    }

    // Simple naked hook that just calls our handler then jumps to trampoline
    __declspec(naked) void HookFunction() {
        __asm {
            // Preserve all registers
            pushad

            // Call our processing function with ESI
            push esi
            call ProcessTrackData
            add esp, 4

            // Restore all registers
            popad

            // Jump to trampoline (which has the original instructions)
            jmp dword ptr[g_trampolineFunc]
        }
    }

    // Simulate a key press using SendInput
    static void SimulateKeyPress(WORD vkCode) {
        INPUT inputs[2] = {};

        // Key down
        inputs[0].type = INPUT_KEYBOARD;
        inputs[0].ki.wVk = vkCode;
        inputs[0].ki.dwFlags = 0;

        // Key up
        inputs[1].type = INPUT_KEYBOARD;
        inputs[1].ki.wVk = vkCode;
        inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;

        SendInput(2, inputs, sizeof(INPUT));
    }

    // Simulate typing text
    static void SimulateTextInput(const std::string& text) {
        for (char c : text) {
            INPUT inputs[2] = {};

            // Convert char to virtual key code and handle shift for uppercase
            bool needShift = false;
            WORD vkCode = 0;

            if (c >= 'a' && c <= 'z') {
                vkCode = VkKeyScan(c) & 0xFF;
            }
            else if (c >= 'A' && c <= 'Z') {
                vkCode = VkKeyScan(c) & 0xFF;
                needShift = (VkKeyScan(c) & 0x100) != 0;
            }
            else if (c >= '0' && c <= '9') {
                vkCode = c;
            }
            else if (c == ' ') {
                vkCode = VK_SPACE;
            }
            else {
                // For special characters, use VkKeyScan
                SHORT vkResult = VkKeyScan(c);
                vkCode = vkResult & 0xFF;
                needShift = (vkResult & 0x100) != 0;
            }

            if (vkCode == 0 || vkCode == 0xFF) {
                continue; // Skip unsupported characters
            }

            // Press shift if needed
            if (needShift) {
                SimulateKeyPress(VK_SHIFT);
                Sleep(10);
            }

            // Press the key
            inputs[0].type = INPUT_KEYBOARD;
            inputs[0].ki.wVk = vkCode;
            inputs[0].ki.dwFlags = 0;

            inputs[1].type = INPUT_KEYBOARD;
            inputs[1].ki.wVk = vkCode;
            inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;

            SendInput(2, inputs, sizeof(INPUT));

            // Release shift if needed
            if (needShift) {
                Sleep(10);
            }

            Sleep(50); // Small delay between characters
        }
    }

    // Execute initial search workflow
    static void ExecuteInitialSearch(const AutoSearchConfig& config) {
        if (g_killSwitchActivated) {
            LogMessage("[Worker] Killswitch activated - aborting initial search");
            return;
        }

        char buffer[512];
        sprintf_s(buffer, "[Worker] Starting initial search: %s", config.searchTerm.c_str());
        LogMessage(buffer);

        // Update current search term for tracking
        g_currentSearchTerm = config.searchTerm;

        try {
            // Press Enter to enable search bar
            if (g_killSwitchActivated) return;
            LogMessage("[Worker] Pressing Enter to enable search bar...");
            SimulateKeyPress(VK_RETURN);
            std::this_thread::sleep_for(std::chrono::milliseconds(config.delayBetweenSteps));

            // Type the search term
            if (g_killSwitchActivated) return;
            sprintf_s(buffer, "[Worker] Typing search term: %s", config.searchTerm.c_str());
            LogMessage(buffer);
            SimulateTextInput(config.searchTerm);
            std::this_thread::sleep_for(std::chrono::milliseconds(config.delayBetweenSteps));

            // Press Enter to exit search bar
            if (g_killSwitchActivated) return;
            LogMessage("[Worker] Pressing Enter to exit search bar...");
            SimulateKeyPress(VK_RETURN);
            std::this_thread::sleep_for(std::chrono::milliseconds(config.delayBetweenSteps));

            // Press Right Arrow to navigate to search button
            if (g_killSwitchActivated) return;
            LogMessage("[Worker] Pressing Right Arrow...");
            SimulateKeyPress(VK_RIGHT);
            std::this_thread::sleep_for(std::chrono::milliseconds(config.delayBetweenSteps));

            // Reset first page scan detection BEFORE executing search
            g_firstPageScanned = false;
            g_totalTracksScannedThisSearch = 0;

            // Press Enter to execute search
            if (g_killSwitchActivated) return;
            LogMessage("[Worker] Pressing Enter to execute search...");
            SimulateKeyPress(VK_RETURN);
            g_searchExecutedTime = std::chrono::steady_clock::now();  // Mark when we executed
            std::this_thread::sleep_for(std::chrono::milliseconds(config.delayAfterSearch));

            LogMessage("[Worker] Initial search complete");
        }
        catch (const std::exception& ex) {
            sprintf_s(buffer, "[Worker] Exception in ExecuteInitialSearch: %s", ex.what());
            LogMessage(buffer);
        }
    }

    // Execute search switch workflow
    static void ExecuteSwitchSearch(const AutoSearchConfig& config) {
        if (g_killSwitchActivated) {
            LogMessage("[Worker] Killswitch activated - aborting search switch");
            return;
        }

        char buffer[512];
        sprintf_s(buffer, "[Worker] Switching to search: %s", config.searchTerm.c_str());
        LogMessage(buffer);

        // Update current search term for tracking
        g_currentSearchTerm = config.searchTerm;

        try {
            // Safety delay
            if (g_killSwitchActivated) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(200));

            // Flush input queue
            MSG msg;
            while (PeekMessage(&msg, NULL, WM_KEYFIRST, WM_KEYLAST, PM_REMOVE)) {}

            // Press Left Arrow to navigate to search bar
            if (g_killSwitchActivated) return;
            LogMessage("[Worker] Pressing Left Arrow...");
            SimulateKeyPress(VK_LEFT);
            std::this_thread::sleep_for(std::chrono::milliseconds(config.delayBetweenSteps));

            // Press Enter to enter search bar
            if (g_killSwitchActivated) return;
            LogMessage("[Worker] Pressing Enter to enter search bar...");
            SimulateKeyPress(VK_RETURN);
            std::this_thread::sleep_for(std::chrono::milliseconds(config.delayBetweenSteps));

            // Backspace to clear old text
            if (g_killSwitchActivated) return;
            LogMessage("[Worker] Clearing old search term...");
            for (int i = 0; i < 20; i++) {
                if (g_killSwitchActivated) return;
                SimulateKeyPress(VK_BACK);
                Sleep(50);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(config.delayBetweenSteps));

            // Type new search term
            if (g_killSwitchActivated) return;
            sprintf_s(buffer, "[Worker] Typing new search term: %s", config.searchTerm.c_str());
            LogMessage(buffer);
            SimulateTextInput(config.searchTerm);
            std::this_thread::sleep_for(std::chrono::milliseconds(config.delayBetweenSteps));

            // Press Enter to exit search bar
            if (g_killSwitchActivated) return;
            LogMessage("[Worker] Pressing Enter to exit search bar...");
            SimulateKeyPress(VK_RETURN);
            std::this_thread::sleep_for(std::chrono::milliseconds(config.delayBetweenSteps));

            // Press Right Arrow
            if (g_killSwitchActivated) return;
            LogMessage("[Worker] Pressing Right Arrow...");
            SimulateKeyPress(VK_RIGHT);
            std::this_thread::sleep_for(std::chrono::milliseconds(config.delayBetweenSteps));

            // Reset first page scan detection BEFORE executing search
            g_firstPageScanned = false;
            g_totalTracksScannedThisSearch = 0;

            // Press Enter to execute search
            if (g_killSwitchActivated) return;
            LogMessage("[Worker] Pressing Enter to execute search...");
            SimulateKeyPress(VK_RETURN);
            g_searchExecutedTime = std::chrono::steady_clock::now();
            std::this_thread::sleep_for(std::chrono::milliseconds(config.delayAfterSearch));

            LogMessage("[Worker] Search switch complete");
            std::this_thread::sleep_for(std::chrono::milliseconds(800));

        }
        catch (const std::exception& ex) {
            sprintf_s(buffer, "[Worker] Exception in ExecuteSwitchSearch: %s", ex.what());
            LogMessage(buffer);
        }
    }

    // Execute auto-scroll
    static void ExecuteAutoScroll(const AutoScrollConfig& config) {
        char buffer[256];
        sprintf_s(buffer, "[Worker] Starting auto-scroll (delay: %ums, max: %u)",
            config.delayMs, config.maxScrolls);
        LogMessage(buffer);

        g_autoScrollConfig = config;
        g_autoScrollEnabled = true;
        g_scrollCount = 0;

        // Initial safety delay - but check for no tracks during this time
        sprintf_s(buffer, "[Worker] Waiting up to %dms for first page to load and scan...", NO_TRACKS_DETECTION_MS);
        LogMessage(buffer);
        sprintf_s(buffer, "[Worker] g_firstPageScanned initial state: %s", g_firstPageScanned.load() ? "true" : "false");
        LogMessage(buffer);
        
        auto waitStart = std::chrono::steady_clock::now();
        bool tracksFound = false;
        
        while (std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - waitStart).count() < NO_TRACKS_DETECTION_MS) {
            
            // Check if we've scanned any tracks
            if (g_firstPageScanned) {
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - waitStart).count();
                sprintf_s(buffer, "[Worker] First page scanned successfully after %lldms, tracks found!", elapsed);
                LogMessage(buffer);
                tracksFound = true;
                break;
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
            if (g_killSwitchActivated) return;
        }
        
        if (!tracksFound) {
            LogMessage("[Worker] Timeout expired - g_firstPageScanned still false");
        }
        
        sprintf_s(buffer, "[Worker] Final g_firstPageScanned state: %s", g_firstPageScanned.load() ? "true" : "false");
        LogMessage(buffer);
        sprintf_s(buffer, "[Worker] Total tracks scanned so far: %u", g_totalTracksScannedThisSearch.load());
        LogMessage(buffer);

        // If no tracks were scanned, we have a no-results scenario
        if (!g_firstPageScanned) {
            LogMessage("[Worker] *** NO TRACKS FOUND for this search ***");
            std::cout << "\n[Auto-Search] No tracks found for: \"" << g_currentSearchTerm << "\"" << std::endl;
            
            // Record stats
            SearchStats stats;
            stats.searchTerm = g_currentSearchTerm;
            stats.totalTracksScanned = 0;
            stats.uniqueTracks = 0;
            stats.hitMaxPages = false;
            stats.noTracksFound = true;
            g_searchStatsHistory.push_back(stats);
            
            g_autoScrollEnabled = false;
            
            // Queue next search with NO-TRACKS workflow
            if (g_autoCycleEnabled && !g_searchTerms.empty() && g_currentSearchIndex >= 0) {
                int nextIndex = (g_currentSearchIndex + 1) % g_searchTerms.size();
                
                if (nextIndex == 0 && g_currentSearchIndex > 0) {
                    std::cout << "\n[Auto-Cycle] All searches completed!\n" << std::endl;
                    LogMessage("[Worker] All searches in cycle completed");
                    return;
                }
                
                g_currentSearchIndex = nextIndex;
                std::cout << "[Auto-Cycle] Will continue to: \"" << g_searchTerms[nextIndex] << "\"\n" << std::endl;
                
                // NO-TRACKS PATH: Left arrow -> Enter -> Clear -> Type new -> etc
                LogMessage("[Worker] Executing NO-TRACKS workflow");
                
                try {
                    // Left arrow to go to search bar
                    LogMessage("[Worker] Pressing Left Arrow to navigate to search bar...");
                    SimulateKeyPress(VK_LEFT);
                    std::this_thread::sleep_for(std::chrono::milliseconds(400));
                    
                    // Press Enter to enter search bar
                    LogMessage("[Worker] Pressing Enter to enter search bar...");
                    SimulateKeyPress(VK_RETURN);
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                    
                    // Clear old search
                    LogMessage("[Worker] Clearing old search term...");
                    for (int i = 0; i < 20; i++) {
                        if (g_killSwitchActivated) return;
                        SimulateKeyPress(VK_BACK);
                        Sleep(50);
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(400));
                    
                    // Type new search term
                    std::string newTerm = g_searchTerms[nextIndex];
                    g_currentSearchTerm = newTerm;
                    sprintf_s(buffer, "[Worker] Typing new search term: %s", newTerm.c_str());
                    LogMessage(buffer);
                    SimulateTextInput(newTerm);
                    std::this_thread::sleep_for(std::chrono::milliseconds(400));
                    
                    // Press Enter to exit search bar
                    LogMessage("[Worker] Pressing Enter to exit search bar...");
                    SimulateKeyPress(VK_RETURN);
                    std::this_thread::sleep_for(std::chrono::milliseconds(400));
                    
                    // Press Right Arrow to navigate to search button
                    LogMessage("[Worker] Pressing Right Arrow...");
                    SimulateKeyPress(VK_RIGHT);
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                    
                    // Reset detection flags
                    g_firstPageScanned = false;
                    g_totalTracksScannedThisSearch = 0;
                    
                    // Press Enter to execute search
                    LogMessage("[Worker] Pressing Enter to execute new search...");
                    SimulateKeyPress(VK_RETURN);
                    g_searchExecutedTime = std::chrono::steady_clock::now();
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                    
                    // Now queue the auto-scroll task for the new search
                    WorkerTask scrollTask;
                    scrollTask.command = WorkerCommand::AutoScroll;
                    scrollTask.scrollConfig.delayMs = 200;
                    scrollTask.scrollConfig.maxScrolls = 0;
                    scrollTask.scrollConfig.useRightArrow = true;
                    
                    {
                        std::lock_guard<std::mutex> lock(g_queueMutex);
                        g_taskQueue.push_back(scrollTask);
                    }
                    g_queueCV.notify_one();
                    
                } catch (const std::exception& ex) {
                    sprintf_s(buffer, "[Worker] Exception in NO-TRACKS workflow: %s", ex.what());
                    LogMessage(buffer);
                }
            }
            
            return;
        }

        // Reset tracking
        {
            std::lock_guard<std::mutex> lock(g_trackIdMutex);
            g_seenTrackIds.clear();
            g_lastNewTrackTime = std::chrono::steady_clock::now();
            g_lastAnyTrackTime = std::chrono::steady_clock::now();
        }
        g_totalTracksScannedThisSearch = 0;
        g_uniqueTracksThisSearch = 0;

        while (g_autoScrollEnabled && g_workerThreadRunning && !g_killSwitchActivated) {
            // Check max scroll count
            if (config.maxScrolls > 0 && g_scrollCount >= config.maxScrolls) {
                LogMessage("[Worker] Max scrolls reached, stopping");
                break;
            }

            // Check timeout for infinite mode
            if (config.maxScrolls == 0) {
                auto now = std::chrono::steady_clock::now();
                long long elapsed = 0;

                {
                    std::lock_guard<std::mutex> lock(g_trackIdMutex);
                    // Use lastAnyTrackTime instead of lastNewTrackTime
                    // This prevents timeout during active scanning of duplicates
                    elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - g_lastAnyTrackTime).count();
                }

                if (elapsed > AUTOSTOP_TIMEOUT_MS) {
                    uint32_t totalScanned = g_totalTracksScannedThisSearch.load();
                    uint32_t uniqueScanned = g_uniqueTracksThisSearch.load();
                    bool hitMaxPages = (totalScanned >= MAX_TRACKS_WARNING_THRESHOLD);

                    // Record stats
                    SearchStats stats;
                    stats.searchTerm = g_currentSearchTerm;
                    stats.totalTracksScanned = totalScanned;
                    stats.uniqueTracks = uniqueScanned;
                    stats.hitMaxPages = hitMaxPages;
                    stats.noTracksFound = false;
                    g_searchStatsHistory.push_back(stats);

                    // Log to max_pages file if we hit the threshold
                    if (hitMaxPages) {
                        if (!g_maxPagesLogFile.is_open()) {
                            g_maxPagesLogFile.open("max_pages_substrings.txt", std::ios::out | std::ios::app);
                        }
                        if (g_maxPagesLogFile.is_open()) {
                            auto now = std::chrono::system_clock::now();
                            auto now_c = std::chrono::system_clock::to_time_t(now);
                            char timestamp[64];
                            struct tm timeinfo;
                            localtime_s(&timeinfo, &now_c);
                            strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &timeinfo);

                            g_maxPagesLogFile << "[" << timestamp << "] Search term: \"" << g_currentSearchTerm
                                << "\" - Scanned " << totalScanned << " total tracks ("
                                << uniqueScanned << " unique) - Likely hit 143 max pages\n";
                            g_maxPagesLogFile.flush();

                            sprintf_s(buffer, "[Track] Logged max pages warning for: %s", g_currentSearchTerm.c_str());
                            LogMessage(buffer);
                        }
                    }

                    sprintf_s(buffer, "[Worker] Timeout reached. Scanned %u total (%u unique)",
                        totalScanned, uniqueScanned);
                    LogMessage(buffer);

                    std::cout << "\n==================================" << std::endl;
                    std::cout << "  AUTO-SCROLL COMPLETE" << std::endl;
                    std::cout << "==================================" << std::endl;
                    std::cout << "  Total Tracks: " << totalScanned << std::endl;
                    std::cout << "  Unique: " << uniqueScanned << std::endl;
                    std::cout << "  Duplicates: " << (totalScanned - uniqueScanned) << std::endl;
                    std::cout << "  Scrolls: " << g_scrollCount.load() << std::endl;

                    if (hitMaxPages) {
                        std::cout << "\n  *** WARNING: Hit max page limit ***" << std::endl;
                    }

                    // Determine if we should auto-cycle (but don't queue yet!)
                    bool shouldAutoCycle = false;
                    if (g_autoCycleEnabled && !g_searchTerms.empty() && g_currentSearchIndex >= 0) {
                        int nextIndex = (g_currentSearchIndex + 1) % g_searchTerms.size();

                        if (nextIndex == 0 && g_currentSearchIndex > 0) {
                            // Completed full cycle
                            std::cout << "\n[Auto-Cycle] All searches completed!\n" << std::endl;
                            LogMessage("[Worker] All searches in cycle completed");
                            shouldAutoCycle = false;
                        }
                        else {
                            // Should continue to next search
                            shouldAutoCycle = true;
                            std::cout << "[Auto-Cycle] Will continue to: \"" << g_searchTerms[nextIndex] << "\"\n" << std::endl;
                            LogMessage("[Worker] Will auto-cycle to next search after cleanup");

                            // Update index for next search
                            g_currentSearchIndex = nextIndex;
                        }
                    }

                    // ALWAYS press ESCAPE to exit results page cleanly
                    LogMessage("[Worker] Pressing ESCAPE to exit results");
                    SimulateKeyPress(VK_ESCAPE);

                    // Give game time to process ESCAPE and stabilize
                    LogMessage("[Worker] Waiting for game to stabilize...");
                    std::this_thread::sleep_for(std::chrono::milliseconds(400));

                    g_autoScrollEnabled = false;
                    LogMessage("[Worker] Auto-scroll stopped");

                    // AFTER scroll completes and game stabilized, queue next task
                    if (shouldAutoCycle && !g_searchTerms.empty() && !g_killSwitchActivated) {
                        int nextIndex = g_currentSearchIndex.load();
                        if (nextIndex >= 0 && nextIndex < (int)g_searchTerms.size()) {
                            LogMessage("[Worker] Auto-scroll complete, queueing next search");

                            WorkerTask nextTask;
                            nextTask.command = WorkerCommand::SwitchSearch;
                            nextTask.searchConfig.searchTerm = g_searchTerms[nextIndex];
                            nextTask.searchConfig.delayBetweenSteps = 450;
                            nextTask.searchConfig.delayAfterSearch = 400;
                            nextTask.searchConfig.autoScrollAfterSearch = true;
                            nextTask.searchConfig.scrollConfig.delayMs = 200;
                            nextTask.searchConfig.scrollConfig.maxScrolls = 0;
                            nextTask.searchConfig.scrollConfig.useRightArrow = true;

                            {
                                std::lock_guard<std::mutex> lock(g_queueMutex);
                                g_taskQueue.push_back(nextTask);
                            }
                            g_queueCV.notify_one();
                        }
                    }
                    return;
                }
            }

            // Simulate scroll
            WORD keyToPress = config.useRightArrow ? VK_RIGHT : VK_DOWN;
            SimulateKeyPress(keyToPress);

            g_scrollCount++;

            // Wait with interruptible delay
            auto waitStart = std::chrono::steady_clock::now();
            while (g_autoScrollEnabled && g_workerThreadRunning && !g_killSwitchActivated) {
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - waitStart).count();

                if (elapsed >= config.delayMs) {
                    break;
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }

        if (g_killSwitchActivated) {
            LogMessage("[Worker] Killswitch activated - auto-scroll aborted");
        }

        g_autoScrollEnabled = false;
        LogMessage("[Worker] Auto-scroll stopped");
    }

    // Worker thread function
    static void WorkerThreadFunc() {
        LogMessage("[Worker] Started");

        while (g_workerThreadRunning) {
            WorkerTask task;
            {
                std::unique_lock<std::mutex> lock(g_queueMutex);
                g_queueCV.wait_for(lock, std::chrono::milliseconds(100), [] {
                    return !g_taskQueue.empty() || !g_workerThreadRunning;
                    });

                if (!g_workerThreadRunning) {
                    break;
                }

                if (g_taskQueue.empty()) {
                    continue;
                }

                task = g_taskQueue.front();
                g_taskQueue.pop_front();
            }

            // Execute the task
            switch (task.command) {
            case WorkerCommand::InitialSearch:
                ExecuteInitialSearch(task.searchConfig);
                if (task.searchConfig.autoScrollAfterSearch) {
                    ExecuteAutoScroll(task.searchConfig.scrollConfig);
                }
                break;

            case WorkerCommand::SwitchSearch:
                ExecuteSwitchSearch(task.searchConfig);
                if (task.searchConfig.autoScrollAfterSearch) {
                    ExecuteAutoScroll(task.searchConfig.scrollConfig);
                }
                break;

            case WorkerCommand::AutoScroll:
                ExecuteAutoScroll(task.scrollConfig);
                break;

            case WorkerCommand::Stop:
                g_autoScrollEnabled = false;
                break;

            default:
                break;
            }
        }

        LogMessage("[Worker] Thread exiting");
    }

    bool Initialize() {
        g_logFile.open("tracks_debug.log", std::ios::out | std::ios::trunc);
        LogMessage("[Track] Track Hook Initializing ");

        // Open CSV file in append mode (or create with header if doesn't exist)
        bool fileExists = false;
        {
            std::ifstream testFile("F:/tracks_data.csv");
            fileExists = testFile.good();
        }

        g_csvFile.open("F:/tracks_data.csv", std::ios::out | std::ios::app);
        if (g_csvFile.is_open()) {
            // Only write header if file is new
            if (!fileExists) {
                g_csvFile << "TrackID,TrackName,CreatorName,CreatorUID,Likes,Dislikes,Downloads,UploadDate,Description\n";
                g_csvFile.flush();
                LogMessage("[Track] CSV logging enabled: F:/tracks_data.csv (new file created with header)");
            }
            else {
                LogMessage("[Track] CSV logging enabled: F:/tracks_data.csv (appending to existing file)");
            }
            g_csvLoggingEnabled = true;
        }
        else {
            LogMessage("[Track] Warning: Could not open CSV file for writing");
        }

        HMODULE baseModule = GetModuleHandle(NULL);
        if (!baseModule) {
            LogMessage("[Track] Failed to get module handle");
            return false;
        }

        DWORD_PTR baseAddress = reinterpret_cast<DWORD_PTR>(baseModule);
        void* targetAddress = reinterpret_cast<void*>(baseAddress + 0x35088F);
        g_hookLocation = targetAddress;

        char buffer[256];

        // Install hook using MinHook
        MH_STATUS status = MH_CreateHook(
            targetAddress,
            &HookFunction,
            &g_trampolineFunc
        );

        if (status != MH_OK) {
            sprintf_s(buffer, "[Track] Failed to create hook: %s", MH_StatusToString(status));
            LogMessage(buffer);
            return false;
        }

        status = MH_EnableHook(targetAddress);
        if (status != MH_OK) {
            sprintf_s(buffer, "[Track] Failed to enable hook: %s", MH_StatusToString(status));
            LogMessage(buffer);
            return false;
        }


        // Start the worker thread
        g_workerThreadRunning = true;
        g_workerThread = std::thread(WorkerThreadFunc);

        LogMessage("[Track] Hook installed successfully!");
        return true;
    }

    void Shutdown() {
        LogMessage("[Track] Shutting Down");

        // Stop worker thread
        if (g_workerThreadRunning) {
            g_workerThreadRunning = false;
            g_queueCV.notify_all();

            if (g_workerThread.joinable()) {
                g_workerThread.join();
            }
            LogMessage("[Worker] Stopped");
        }

        if (g_hookLocation) {
            MH_DisableHook(g_hookLocation);
            MH_RemoveHook(g_hookLocation);
            g_hookLocation = nullptr;
        }

        g_updateCallback = nullptr;
        g_capturedESI = nullptr;

        if (g_csvFile.is_open()) {
            g_csvFile.close();
            LogMessage("[Track] CSV file closed");
        }

        if (g_maxPagesLogFile.is_open()) {
            g_maxPagesLogFile.close();
            LogMessage("[Track] Max pages log file closed");
        }

        if (g_logFile.is_open()) {
            LogMessage("[Track] Shutdown complete");
            g_logFile.close();
        }
    }

    void SetUpdateCallback(TrackUpdateCallback callback) {
        g_updateCallback = callback;
    }

    void SetLoggingEnabled(bool enabled) {
        g_loggingEnabled = enabled;
        char buffer[128];
        sprintf_s(buffer, "[Track] Logging %s", enabled ? "enabled" : "disabled");
        LogMessage(buffer);
    }

    void DumpTrackStructure(void* trackPtr, size_t size) {
        if (trackPtr && IsValidPointer(trackPtr)) {
            DumpHex(trackPtr, size, "Manual Track Structure Dump");
        }
    }

    void StartAutoScroll(const AutoScrollConfig& config) {
        WorkerTask task;
        task.command = WorkerCommand::AutoScroll;
        task.scrollConfig = config;

        {
            std::lock_guard<std::mutex> lock(g_queueMutex);
            g_taskQueue.push_back(task);
        }
        g_queueCV.notify_one();
    }

    void StopAutoScroll() {
        g_autoScrollEnabled = false;
    }

    bool IsAutoScrolling() {
        return g_autoScrollEnabled;
    }

    AutoScrollConfig GetAutoScrollConfig() {
        return g_autoScrollConfig;
    }

    void SetAutoScrollDelay(uint32_t delayMs) {
        g_autoScrollConfig.delayMs = delayMs;
        char buffer[128];
        sprintf_s(buffer, "[Track] Scroll delay set to %ums", delayMs);
        LogMessage(buffer);
    }

    void StartAutoSearch(const AutoSearchConfig& config) {
        StopAutoScroll();

        WorkerTask task;
        task.command = WorkerCommand::InitialSearch;
        task.searchConfig = config;

        {
            std::lock_guard<std::mutex> lock(g_queueMutex);
            g_taskQueue.push_back(task);
        }
        g_queueCV.notify_one();
    }

    void StopAutoSearch() {
        g_autoScrollEnabled = false;
    }

    bool IsAutoSearching() {
        return g_autoScrollEnabled || !g_taskQueue.empty();
    }

    void SwitchSearchTerm(const AutoSearchConfig& config) {
        WorkerTask task;
        task.command = WorkerCommand::SwitchSearch;
        task.searchConfig = config;

        {
            std::lock_guard<std::mutex> lock(g_queueMutex);
            g_taskQueue.push_back(task);
        }
        g_queueCV.notify_one();
    }

    void SetSearchTerms(const std::vector<std::string>& terms) {
        g_searchTerms = terms;
        g_currentSearchIndex = -1;
        char buffer[256];
        sprintf_s(buffer, "[Track] Search terms configured: %zu terms", terms.size());
        LogMessage(buffer);
    }

    void CycleToNextSearch() {
        if (g_searchTerms.empty()) {
            LogMessage("[Track] Error: No search terms configured!");
            return;
        }

        // Stop current operation
        if (g_autoScrollEnabled) {
            LogMessage("[Track] Stopping current scroll...");
            StopAutoScroll();
            std::this_thread::sleep_for(std::chrono::milliseconds(1300));
        }

        // Move to next search term
        int currentIndex = g_currentSearchIndex.load();
        int nextIndex = (currentIndex + 1) % g_searchTerms.size();
        g_currentSearchIndex = nextIndex;

        std::string searchTerm = g_searchTerms[nextIndex];

        char buffer[512];
        sprintf_s(buffer, "[Track] Cycling to search %d/%zu: %s",
            nextIndex + 1, g_searchTerms.size(), searchTerm.c_str());
        LogMessage(buffer);

        // Configure search
        AutoSearchConfig config;
        config.searchTerm = searchTerm;
        config.delayBetweenSteps = 650;
        config.delayAfterSearch = 500;
        config.autoScrollAfterSearch = true;
        config.scrollConfig.delayMs = 210;
        config.scrollConfig.maxScrolls = 0;
        config.scrollConfig.useRightArrow = true;

        // Update current search term for stats tracking
        g_currentSearchTerm = searchTerm;

        // Queue appropriate command
        if (currentIndex == -1) {
            StartAutoSearch(config);
        }
        else {
            SwitchSearchTerm(config);
        }
    }

    void PrintSearchStatsSummary() {
        if (g_searchStatsHistory.empty()) {
            std::cout << "\n[Search Stats] No searches performed yet.\n" << std::endl;
            return;
        }

        uint32_t totalTracksAllSearches = 0;
        uint32_t totalUniqueAllSearches = 0;
        uint32_t searchesHitMaxPages = 0;

        std::cout << "\n========================================" << std::endl;
        std::cout << "  SEARCH STATISTICS SUMMARY" << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << "  Total Searches: " << g_searchStatsHistory.size() << std::endl;
        std::cout << "========================================\n" << std::endl;

        for (size_t i = 0; i < g_searchStatsHistory.size(); i++) {
            const auto& stats = g_searchStatsHistory[i];
            totalTracksAllSearches += stats.totalTracksScanned;
            totalUniqueAllSearches += stats.uniqueTracks;
            if (stats.hitMaxPages) searchesHitMaxPages++;

            std::cout << "Search #" << (i + 1) << ": \"" << stats.searchTerm << "\"" << std::endl;
            
            if (stats.noTracksFound) {
                std::cout << "  *** NO TRACKS FOUND ***" << std::endl;
            } else {
                std::cout << "  Total: " << stats.totalTracksScanned << " tracks" << std::endl;
                std::cout << "  Unique: " << stats.uniqueTracks << " tracks" << std::endl;

                if (stats.hitMaxPages) {
                    std::cout << "  *** HIT MAX PAGE LIMIT ***" << std::endl;
                }
            }
            std::cout << std::endl;
        }

        std::cout << "========================================" << std::endl;
        std::cout << "  TOTALS" << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << "  Total Tracks: " << totalTracksAllSearches << std::endl;
        std::cout << "  Unique: " << totalUniqueAllSearches << std::endl;
        std::cout << "  Hit Max: " << searchesHitMaxPages << "/" << g_searchStatsHistory.size() << std::endl;
        std::cout << "========================================\n" << std::endl;
    }

    void ClearSearchStats() {
        g_searchStatsHistory.clear();
        LogMessage("[Track] Stats cleared");
    }

    void SetAutoCycleEnabled(bool enabled) {
        g_autoCycleEnabled = enabled;
        char buffer[128];
        sprintf_s(buffer, "[Track] Auto-cycle %s", enabled ? "enabled" : "disabled");
        LogMessage(buffer);
    }

    bool IsAutoCycleEnabled() {
        return g_autoCycleEnabled;
    }

    void ActivateKillSwitch() {
        g_killSwitchActivated = true;
        g_autoScrollEnabled = false;

        // Clear task queue
        {
            std::lock_guard<std::mutex> lock(g_queueMutex);
            g_taskQueue.clear();
        }

        LogMessage("[Track] *** KILLSWITCH ACTIVATED *** All operations stopped!");
        std::cout << "\n*** KILLSWITCH ACTIVATED ***" << std::endl;
        std::cout << "All auto-scroll and auto-search operations have been stopped." << std::endl;
        std::cout << "Press F6 again to reset and re-enable." << std::endl;
    }

    void DeactivateKillSwitch() {
        g_killSwitchActivated = false;
        LogMessage("[Track] Killswitch deactivated - operations can resume");
        std::cout << "[Track] Killswitch deactivated - ready for new operations" << std::endl;
    }

    bool IsKillSwitchActivated() {
        return g_killSwitchActivated;
    }

    bool LoadSearchTerms(const std::string& filepath) {
        bool success = LoadSearchTermsFromFile(filepath);
        if (success) {
            std::cout << "\n[Track] Search terms loaded successfully!" << std::endl;
        }
        else {
            std::cout << "\n[Track] Failed to load search terms from: " << filepath << std::endl;
        }
        return success;
    }

    PaginationInfo GetPaginationInfo() {
        PaginationInfo info;
        return info;
    }

    void LogPaginationInfo() {
    }
}