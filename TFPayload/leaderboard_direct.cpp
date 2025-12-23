#include "pch.h"
#include "leaderboard_direct.h"
#include "logging.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <MinHook.h>

namespace LeaderboardDirect {
    
    // STATIC STATE
    static FetcherState s_state;
    static EntryCallback s_entryCallback = nullptr;
    static CompletionCallback s_completionCallback = nullptr;
    static DWORD_PTR s_baseAddress = 0;

    // Original bytes at patch location
    static BYTE s_originalBytes[2] = {0};
    static bool s_originalBytesSaved = false;
    
    static const DWORD PATCH_RVA = 0x34397e;
    static const BYTE ORIGINAL_OPCODE = 0x75;  // JNZ
    static const BYTE PATCHED_OPCODE = 0xEB;   // JMP (unconditional)

    // GLOBAL GAME POINTERS

    // DAT_0174b308 -> RVA 0x104b308 (Ghidra base is 0x700000)
    static void** GetGlobalStatePtr() {
        if (s_baseAddress == 0) return nullptr;
        return (void**)(s_baseAddress + 0x104b308);
    }

    // Get the existing leaderboard service from global state
    // Located at: *(DAT_0174b308 + 0x1c8)
    static void* GetLeaderboardService() {
        void** globalPtr = GetGlobalStatePtr();
        if (!globalPtr || !*globalPtr) return nullptr;
        
        void** servicePtr = (void**)((char*)*globalPtr + 0x1c8);
        return *servicePtr;
    }

    // ============================================================
    // GAME FUNCTION POINTERS
    // ============================================================
    
    // Flag to indicate we triggered a fetch (so scanner can log results)
    static bool s_weTriggeredFetch = false;
    static int s_fetchTrackId = 0;

    // Function pointer for reading leaderboard entries
    using GetLeaderboardEntryFn = void* (__thiscall*)(void* service, int index);
    static GetLeaderboardEntryFn o_GetLeaderboardEntry = nullptr;

    // Called by leaderboard_scanner when ProcessLeaderboardData is called
    // Returns true if we triggered the fetch and want to capture results
    bool OnLeaderboardDataReceived(void* context) {
        if (!s_weTriggeredFetch || !context) {
            return false;
        }
        
        LOG_INFO("[LB - Direct] ========================================");
        LOG_INFO("[LB - Direct] LEADERBOARD DATA RECEIVED!");
        LOG_INFO("[LB - Direct] Track ID requested: " << s_fetchTrackId);
        LOG_INFO("[LB - Direct] ========================================");
        
        // Get leaderboard service to read entries
        void* service = GetLeaderboardService();
        if (service && o_GetLeaderboardEntry) {
            // Try to get total entries from context+0x150
            int totalEntries = *(int*)((char*)context + 0x150);
            LOG_VERBOSE("[LB - Direct] Total entries available: " << totalEntries);
            
            // Clear previous entries
            s_state.fetchedEntries.clear();
            
            // Read up to 20 entries
            int entriesToRead = (totalEntries < 20) ? totalEntries : 20;
            if (entriesToRead <= 0) entriesToRead = 10; // Default if total is weird
            
            for (int i = 0; i < entriesToRead; i++) {
                void* entry = o_GetLeaderboardEntry(service, i);
                if (entry) {
                    // Entry structure:
                    // +0x00: rank (int)
                    // +0x34: faults (int)
                    // +0x38: time in ms (int)
                    // +0x43: player name (char*)
                    // +0x88: medal (int)
                    int rank = *(int*)((char*)entry + 0x00);
                    int faults = *(int*)((char*)entry + 0x34);
                    int timeMs = *(int*)((char*)entry + 0x38);
                    char* playerName = (char*)entry + 0x43;
                    int medal = *(int*)((char*)entry + 0x88);
                    
                    // Format time
                    int minutes = timeMs / 60000;
                    int seconds = (timeMs % 60000) / 1000;
                    int ms = timeMs % 1000;
                    
                    LOG_VERBOSE("[LB - Direct] #" << rank << ": " << playerName 
                        << " - " << minutes << ":" << std::setfill('0') << std::setw(2) << seconds 
                        << "." << std::setw(3) << ms << std::setfill(' ')
                        << " (" << faults << " faults)");
                    
                    // Store in our state
                    LeaderboardEntry e;
                    e.rank = rank;
                    e.playerName = playerName;
                    e.faults = faults;
                    e.timeMs = timeMs;
                    e.medal = medal;
                    s_state.fetchedEntries.push_back(e);
                }
            }
            
            LOG_INFO("[LB - Direct] ========================================");
            LOG_INFO("[LB - Direct] Captured " << s_state.fetchedEntries.size() << " entries");
            LOG_INFO("[LB - Direct] ========================================");
        } else {
            LOG_WARNING("[LB - Direct] Could not read entries (service=0x" << std::hex << (uintptr_t)service << ", GetEntry=0x" << (uintptr_t)o_GetLeaderboardEntry << std::dec << ")");
        }
        
        s_weTriggeredFetch = false;
        s_state.isFetching = false;
        
        // Call completion callback if set
        if (s_completionCallback) {
            s_completionCallback(true, (int)s_state.fetchedEntries.size());
        }
        
        return true;
    }

    // Hook for FetchLeaderboardDataFromServer to debug what gets sent
    using FetchLeaderboardDataFromServerFn = void(__thiscall*)(void* thisPtr, int param1, int param2, int** filterString, int param4, int configPtr);
    static FetchLeaderboardDataFromServerFn o_FetchLeaderboardDataFromServer = nullptr;

    static void __fastcall Hook_FetchLeaderboardDataFromServer(void* thisPtr, void* edx, int param1, int param2, int** filterString, int param4, int configPtr) {
        LOG_VERBOSE("[LB - Direct] === FetchLeaderboardDataFromServer CALLED ===");
        LOG_VERBOSE("[LB - Direct] param1 (requestType): " << param1);
        LOG_VERBOSE("[LB - Direct] param2 (startIndex): " << param2);
        LOG_VERBOSE("[LB - Direct] filterString ptr: 0x" << std::hex << (uintptr_t)filterString << std::dec);
        if (filterString && *filterString) {
            int* strData = *filterString;
            // Structure is: [refcount:4][length:4][capacity?:4][string data...]
            int refCount = strData[0];
            int len = strData[1];
            int capacity = strData[2];
            char* str = (char*)&strData[3];  // String starts at offset 12
            
            // Read string with null terminator search
            std::string fullStr;
            for (int i = 0; i < 64 && str[i] != '\0'; i++) {
                fullStr += str[i];
            }
            LOG_VERBOSE("[LB - Direct] filterString value: \"" << fullStr << "\"");
        }
        LOG_VERBOSE("[LB - Direct] param4 (trackId): " << param4);
        LOG_VERBOSE("[LB - Direct] configPtr: 0x" << std::hex << configPtr << std::dec);
        if (configPtr) {
            LOG_VERBOSE("[LB - Direct] config+0x18: 0x" << std::hex << *(int*)(configPtr + 0x18) << std::dec);
            LOG_VERBOSE("[LB - Direct] config+0x1c: 0x" << std::hex << *(int*)(configPtr + 0x1c) << std::dec);
            LOG_VERBOSE("[LB - Direct] config+0x30: 0x" << std::hex << *(int*)(configPtr + 0x30) << std::dec);
            LOG_VERBOSE("[LB - Direct] config+0x34 (lbType): " << *(int*)(configPtr + 0x34));
        }
        
        // Call original function
        o_FetchLeaderboardDataFromServer(thisPtr, param1, param2, filterString, param4, configPtr);
    }

    // RequestLeaderboardData: RVA 0x343950
    // void __thiscall RequestLeaderboardData(void* this, int leaderboardType, int startIndex)
    using RequestLeaderboardDataFn = bool(__thiscall*)(void* thisPtr, int leaderboardType, int startIndex);
    static RequestLeaderboardDataFn o_RequestLeaderboardData = nullptr;

    // Wrapper function to call RequestLeaderboardData with SEH
    // This is in a separate function to avoid mixing SEH with C++ objects
    static bool CallRequestLeaderboardData(void* service, int type, int startIndex) {
        bool result = false;
        __try {
            result = o_RequestLeaderboardData(service, type, startIndex);
        }
        __except(EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
        return result;
    }

    // INITIALIZATION
    // ============================================================
    bool Initialize(DWORD_PTR baseAddress) {
        s_baseAddress = baseAddress;
        
        LOG_VERBOSE("[LB - Direct] Initializing LeaderboardDirect Hook...");

        // Set up function pointers
        o_RequestLeaderboardData = (RequestLeaderboardDataFn)(baseAddress + 0x343950);
        o_GetLeaderboardEntry = (GetLeaderboardEntryFn)(baseAddress + 0x343ab0);

        // Hook FetchLeaderboardDataFromServer to debug what gets sent
        // FetchLeaderboardDataFromServer: RVA 0x3436f0
        void* targetFetch = (void*)(baseAddress + 0x3436f0);
        if (MH_CreateHook(targetFetch, &Hook_FetchLeaderboardDataFromServer,
            reinterpret_cast<LPVOID*>(&o_FetchLeaderboardDataFromServer)) != MH_OK) {
            LOG_WARNING("[LB - Direct] Failed to hook FetchLeaderboardDataFromServer");
        } else {
            if (MH_EnableHook(targetFetch) != MH_OK) {
                LOG_WARNING("[LB - Direct] Failed to enable FetchLeaderboardDataFromServer hook");
            } else {
                LOG_VERBOSE("[LB - Direct] Hooked FetchLeaderboardDataFromServer for debugging");
            }
        }

        // Save original bytes at patch location
        BYTE* patchAddr = (BYTE*)(baseAddress + PATCH_RVA);
        
        // Make memory readable
        DWORD oldProtect;
        if (!VirtualProtect(patchAddr, 2, PAGE_EXECUTE_READ, &oldProtect)) {
            LOG_WARNING("[LB - Direct] Could not read patch location");
        } else {
            s_originalBytes[0] = patchAddr[0];
            s_originalBytes[1] = patchAddr[1];
            s_originalBytesSaved = true;
            VirtualProtect(patchAddr, 2, oldProtect, &oldProtect);
            
            if (s_originalBytes[0] == ORIGINAL_OPCODE) {
                LOG_VERBOSE("[LB - Direct] Verified: Found JNZ instruction at expected location");
            } else {
                LOG_WARNING("[LB - Direct] Expected JNZ (0x75), found 0x" << std::hex << (int)s_originalBytes[0] << std::dec);
            }
        }

        s_state.isInitialized = true;
        LOG_VERBOSE("[LB - Direct] LeaderboardDirect initialized successfully!");
        
        return true;
    }

    void Shutdown() {
        if (!s_state.isInitialized) return;

        // Remove patch if applied
        if (s_state.isPatchApplied) {
            RemovePatch();
        }

        // Remove hook
        void* targetFetch = (void*)(s_baseAddress + 0x3436f0);
        MH_DisableHook(targetFetch);
        MH_RemoveHook(targetFetch);

        s_state = FetcherState();
        s_entryCallback = nullptr;
        s_completionCallback = nullptr;
        s_baseAddress = 0;

        LOG_VERBOSE("[LB - Direct] LeaderboardDirect shut down");
    }

    // PATCH MANAGEMENT
    bool ApplyPatch() {
        if (!s_state.isInitialized) {
            s_state.lastError = "Not initialized";
            return false;
        }

        if (s_state.isPatchApplied) {
            LOG_VERBOSE("[LB - Direct] Patch already applied");
            return true;
        }

        if (!s_originalBytesSaved) {
            s_state.lastError = "Original bytes not saved";
            return false;
        }

        BYTE* patchAddr = (BYTE*)(s_baseAddress + PATCH_RVA);
        
        // Make memory writable
        DWORD oldProtect;
        if (!VirtualProtect(patchAddr, 1, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            s_state.lastError = "VirtualProtect failed";
            LOG_ERROR("[LB - Direct] " << s_state.lastError);
            return false;
        }

        // Apply patch: change JNZ (75) to JMP (EB)
        patchAddr[0] = PATCHED_OPCODE;

        // Restore protection
        VirtualProtect(patchAddr, 1, oldProtect, &oldProtect);

        // Flush instruction cache
        FlushInstructionCache(GetCurrentProcess(), patchAddr, 1);

        s_state.isPatchApplied = true;
        LOG_INFO("[LB - Direct] *** PATCH APPLIED ***");
        LOG_INFO("[LB - Direct] Track manager check bypassed - can fetch any leaderboard!");
        
        return true;
    }

    bool RemovePatch() {
        if (!s_state.isInitialized) {
            return false;
        }

        if (!s_state.isPatchApplied) {
            LOG_VERBOSE("[LB - Direct] Patch not applied");
            return true;
        }

        if (!s_originalBytesSaved) {
            s_state.lastError = "Original bytes not saved";
            return false;
        }

        BYTE* patchAddr = (BYTE*)(s_baseAddress + PATCH_RVA);
        
        // Make memory writable
        DWORD oldProtect;
        if (!VirtualProtect(patchAddr, 1, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            s_state.lastError = "VirtualProtect failed";
            LOG_ERROR("[LB - Direct] " << s_state.lastError);
            return false;
        }

        // Restore original byte
        patchAddr[0] = s_originalBytes[0];

        // Restore protection
        VirtualProtect(patchAddr, 1, oldProtect, &oldProtect);

        // Flush instruction cache
        FlushInstructionCache(GetCurrentProcess(), patchAddr, 1);

        s_state.isPatchApplied = false;
        LOG_INFO("[LB - Direct] *** PATCH REMOVED ***");
        LOG_INFO("[LB - Direct] Track manager check restored");
        
        return true;
    }

    bool IsPatchApplied() {
        return s_state.isPatchApplied;
    }

    // CORE FUNCTIONALITY
    bool FetchLeaderboard(const FetchRequest& request) {
        if (!s_state.isInitialized) {
            s_state.lastError = "Not initialized";
            LOG_ERROR("[LB - Direct] " << s_state.lastError);
            return false;
        }

        // Get leaderboard service
        void* service = GetLeaderboardService();
        if (!service) {
            s_state.lastError = "No leaderboard service - open any leaderboard first to initialize";
            LOG_ERROR("[LB - Direct] " << s_state.lastError);
            return false;
        }

        LOG_INFO("[LB - Direct] ========================================");
        LOG_INFO("[LB - Direct] FETCHING LEADERBOARD");
        LOG_INFO("[LB - Direct] Track ID: " << request.trackId);
        LOG_VERBOSE("[LB - Direct] Start Index: " << request.startIndex);
        LOG_VERBOSE("[LB - Direct] Leaderboard Type: " << request.leaderboardType);
        LOG_INFO("[LB - Direct] Patch Applied: " << (s_state.isPatchApplied ? "YES" : "NO"));
        LOG_INFO("[LB - Direct] ========================================");

        // Store current request
        s_state.currentRequest = request;
        s_state.fetchedEntries.clear();
        s_state.isFetching = true;
        s_weTriggeredFetch = true;

        // Convert track ID string to int
        int trackIdInt = 0;
        for (size_t i = 0; i < request.trackId.length(); i++) {
            char c = request.trackId[i];
            if (c < '0' || c > '9') {
                s_state.lastError = "Invalid track ID (not a number)";
                LOG_ERROR("[LB - Direct] " << s_state.lastError);
                s_state.isFetching = false;
                return false;
            }
            trackIdInt = trackIdInt * 10 + (c - '0');
        }

        // Set track ID at service+0xc (this gets read by case 4 and 5 in the switch)
        *(int*)((char*)service + 0xc) = trackIdInt;
        LOG_VERBOSE("[LB - Direct] Set track ID at service+0xc: " << trackIdInt);

        // From debug hook, we learned:
        // - filterString format is "sp_new_ugc:TRACKID"
        // - filterString structure: [refcount:4][length:4][capacity:4][string data...]
        // - param4 is the track ID (we set this at dataValue[5])
        // - config+0x18 and config+0x1c should be 1
        
        int** dataPtr = (int**)((char*)service + 0x10);
        if (dataPtr && *dataPtr) {
            int* dataValue = *dataPtr;  // This is piVar2 from RE
            
            LOG_VERBOSE("[LB - Direct] dataPtr address: 0x" << std::hex << (uintptr_t)dataPtr << std::dec);
            LOG_VERBOSE("[LB - Direct] dataValue (*dataPtr): 0x" << std::hex << (uintptr_t)dataValue << std::dec);
            
            // Build the filter string "sp_new_ugc:TRACKID"
            std::string filterStr = "sp_new_ugc:" + request.trackId;
            size_t len = filterStr.length();
            
            // Allocate new string structure: [refcount:4][length:4][capacity:4][string data + null]
            int* newString = (int*)malloc(12 + len + 1);
            newString[0] = 1;           // refcount
            newString[1] = (int)len;    // length
            newString[2] = (int)len;    // capacity
            memcpy((char*)&newString[3], filterStr.c_str(), len + 1);  // string data + null
            
            // Set the filter string pointer at dataValue[3] (offset 0xC)
            int** filterStringLoc = (int**)(dataValue + 3);
            LOG_VERBOSE("[LB - Direct] filterStringLoc (dataValue+3): 0x" << std::hex << (uintptr_t)filterStringLoc << std::dec);
            
            // Store pointer (leaking old string for now)
            *filterStringLoc = newString;
            LOG_VERBOSE("[LB - Direct] Set filter string: \"" << filterStr << "\"");
            
            // Set param4 (dataValue[5]) to our track ID
            dataValue[5] = trackIdInt;
            LOG_VERBOSE("[LB - Direct] Set dataValue[5] (param4) to track ID: " << trackIdInt);
            
        } else {
            LOG_WARNING("[LB - Direct] dataPtr is null, cannot set track ID");
        }

        // Set flags before calling - this tells our hook to capture the results
        s_weTriggeredFetch = true;
        s_fetchTrackId = trackIdInt;
        s_state.fetchedEntries.clear();

        // Call RequestLeaderboardData
        LOG_VERBOSE("[LB - Direct] Calling RequestLeaderboardData...");
        LOG_VERBOSE("[LB - Direct]   this = 0x" << std::hex << (uintptr_t)service << std::dec);
        LOG_VERBOSE("[LB - Direct]   type = " << request.leaderboardType);
        LOG_VERBOSE("[LB - Direct]   startIndex = " << request.startIndex);

        bool result = CallRequestLeaderboardData(service, request.leaderboardType, request.startIndex);
        
        LOG_VERBOSE("[LB - Direct] RequestLeaderboardData returned: " << (result ? "true" : "false"));

        if (!result) {
            if (!s_state.isPatchApplied) {
                s_state.lastError = "RequestLeaderboardData failed - try applying patch (F11)";
            } else {
                s_state.lastError = "RequestLeaderboardData failed even with patch";
            }
            LOG_ERROR("[LB - Direct] " << s_state.lastError);
            s_state.isFetching = false;
            return false;
        }

        LOG_INFO("[LB - Direct] Request sent! Watch leaderboard_scanner for results...");
        s_state.isFetching = false;
        
        return true;
    }

    bool IsFetching() {
        return s_state.isFetching;
    }

    // ============================================================
    // CALLBACKS
    // ============================================================
    
    void SetEntryCallback(EntryCallback callback) {
        s_entryCallback = callback;
    }

    void SetCompletionCallback(CompletionCallback callback) {
        s_completionCallback = callback;
    }

    // ============================================================
    // STATE ACCESS
    // ============================================================
    
    const FetcherState& GetState() {
        return s_state;
    }

    const std::vector<LeaderboardEntry>& GetEntries() {
        return s_state.fetchedEntries;
    }

    const std::string& GetLastError() {
        return s_state.lastError;
    }

    // ============================================================
    // UTILITIES
    // ============================================================
    
    std::string FormatTime(int timeMs) {
        if (timeMs < 0) return "??:??.???";
        
        int minutes = timeMs / 60000;
        int seconds = (timeMs % 60000) / 1000;
        int milliseconds = timeMs % 1000;

        char buffer[32];
        sprintf_s(buffer, "%02d:%02d.%03d", minutes, seconds, milliseconds);
        return std::string(buffer);
    }

    // ============================================================
    // HOTKEYS
    // ============================================================
    
    void CheckHotkey() {
        // F10 = Test fetch
        static bool f10WasPressed = false;
        bool f10IsPressed = (GetAsyncKeyState(VK_F10) & 0x8000) != 0;

        if (f10IsPressed && !f10WasPressed) {
            LOG_INFO("\n[LB - Direct] F10 pressed - Testing leaderboard fetch...");
            
            FetchRequest request;
            request.trackId = "221120";
            request.startIndex = 0;
            request.count = 10;
            request.leaderboardType = 2;  // Type 5: uses service+0xc as track ID, param becomes 1
            
            FetchLeaderboard(request);
        }
        f10WasPressed = f10IsPressed;

        // F11 = Toggle patch
        static bool f11WasPressed = false;
        bool f11IsPressed = (GetAsyncKeyState(VK_F11) & 0x8000) != 0;

        if (f11IsPressed && !f11WasPressed) {
            LOG_INFO("\n[LB - Direct] F11 pressed - Toggling patch...");
            
            if (s_state.isPatchApplied) {
                RemovePatch();
            } else {
                ApplyPatch();
            }
        }
        f11WasPressed = f11IsPressed;
    }

} // namespace LeaderboardDirect
