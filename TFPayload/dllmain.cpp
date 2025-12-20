#include "pch.h"
#include <TlHelp32.h>
#include <iostream>
#include <algorithm>
#include <vector>
#include <fstream>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#include "tracks.h"
#include "payload.h"
#include "leaderboard_scanner.h"
#include "feed_fetcher.h"
#include "leaderboard_direct.h"
#include "pause.h"
#include <MinHook.h>

// FORWARD DECLARATIONS
DWORD WINAPI KeyMonitorThread(LPVOID lpParam);

// GLOBAL STATE
bool isRunning = false;
HANDLE g_hKeyMonitorThread = NULL;
int g_monitorCount = 0;
HMONITOR g_monitors[3] = { NULL };
bool g_consolePositioned = false;

// MACROS
#define KeyPress(...) (GetAsyncKeyState(__VA_ARGS__) & 0x1)
#define Log(...) std::cout << __VA_ARGS__ << std::endl

struct DumpStruct {
    char data[0x200];
};

// UTILITY FUNCTIONS
DWORD GetProcessID(LPCTSTR ProcessName)
{
    HANDLE hsnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32 pt;
    pt.dwSize = sizeof(PROCESSENTRY32);

    if (Process32First(hsnap, &pt)) {
        do {
            if (!lstrcmpi(pt.szExeFile, ProcessName)) {
                CloseHandle(hsnap);
                return pt.th32ProcessID;
            }
        } while (Process32Next(hsnap, &pt));
    }

    CloseHandle(hsnap);
    return 0;
}

DWORD_PTR GetModuleBaseAddress(DWORD processID, const wchar_t* moduleName)
{
    DWORD_PTR baseAddress = 0;
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, processID);

    if (hSnapshot != INVALID_HANDLE_VALUE) {
        MODULEENTRY32 moduleEntry;
        moduleEntry.dwSize = sizeof(MODULEENTRY32);

        if (Module32First(hSnapshot, &moduleEntry)) {
            do {
                if (!_wcsicmp(moduleEntry.szModule, moduleName)) {
                    baseAddress = (DWORD_PTR)moduleEntry.modBaseAddr;
                    break;
                }
            } while (Module32Next(hSnapshot, &moduleEntry));
        }

        CloseHandle(hSnapshot);
    }

    return baseAddress;
}

// MONITOR POSITIONING
BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData)
{
    if (g_monitorCount < 3) {
        g_monitors[g_monitorCount++] = hMonitor;
    }
    return TRUE;
}


// CALLBACKS
void OnTrackUpdate(const Tracks::TrackInfo& trackInfo)
{
    Log("\n=== TRACK INFO ===");
    Log("Track: " << trackInfo.trackName);
    Log("Creator: " << trackInfo.creatorName);
    Log("Likes: " << trackInfo.likeCount << " | Dislikes: " << trackInfo.dislikeCount);
    Log("Downloads: " << trackInfo.downloadCount);
    if (!trackInfo.description.empty()) {
        Log("Description: " << trackInfo.description);
    }
}

void OnFeedTrack(const FeedFetcher::TrackMetadata& track)
{
    Log("[FEED] Track ID: " << track.trackId
        << " | Name: " << track.trackName
        << " | Author: " << track.authorName);
}

// SHUTDOWN
extern "C" __declspec(dllexport) void ShutdownPayload()
{
    Log("\n[TFPayload] Shutting down for rebuild (Automated Trigger)...");

    isRunning = false;

    if (g_hKeyMonitorThread != NULL) {
        Log("[TFPayload] Waiting for monitor thread to exit...");
        WaitForSingleObject(g_hKeyMonitorThread, 2000);
        CloseHandle(g_hKeyMonitorThread);
        g_hKeyMonitorThread = NULL;
    }

    Tracks::Shutdown();
    LeaderboardScanner::Shutdown();
    FeedFetcher::Shutdown();
    LeaderboardDirect::Shutdown();
    Pause::Shutdown();

    Log("[Main] All resources cleaned up.");
}

// PAYLOAD INITIALIZATION (called when hot-loaded)
extern "C" __declspec(dllexport) void PayloadInit()
{
    Log("\n[TFPayload] PayloadInit called - Starting payload operations...");

    if (isRunning) {
        Log("[TFPayload] Already running, skipping init");
        return;
    }

    isRunning = true;

    // Initialize MinHook
    MH_STATUS mhStatus = MH_Initialize();
    if (mhStatus != MH_OK) {
        Log("[TFPayload ERROR] MinHook initialization failed: " << mhStatus);
        return;
    }
    Log("[TFPayload] MinHook initialized successfully");

    const wchar_t* processName = L"trials_fusion.exe";
    const wchar_t* moduleName = L"trials_fusion.exe";
    
    DWORD_PTR baseAddress = GetModuleBaseAddress(GetProcessID(processName), moduleName);
    if (baseAddress == 0) {
        Log("[TFPayload ERROR] Could not retrieve base address!");
        return;
    }

    Log("[TFPayload] Game Base Address: " << baseAddress);

    // Initialize Hooks
    Tracks::SetLoggingEnabled(true);
    Tracks::SetUpdateCallback(OnTrackUpdate);
    if (Tracks::Initialize()) {
        Log("[TFPayload] Track metadata hook initialized successfully!");
        if (!Tracks::LoadSearchTerms("F:/search_terms.txt")) {
            Log("[TFPayload] Could not load F:/search_terms.txt, using default search terms");
        }
    } else {
        Log("[TFPayload ERROR] Failed to initialize track hooks!");
    }

    // Initialize leaderboard scanner
    LeaderboardScanner::Initialize(baseAddress);
    Log("[TFPayload] Leaderboard scanner initialized");

    // Initialize feed fetcher
    FeedFetcher::SetTrackCallback(OnFeedTrack);
    FeedFetcher::Initialize(baseAddress);
    Log("[TFPayload] Feed fetcher initialized");

    // Initialize leaderboard direct (patch-based, UI-independent)
    LeaderboardDirect::Initialize(baseAddress);
    Log("[TFPayload] Leaderboard direct initialized");

    // Initialize pause system
    Pause::Initialize(baseAddress);
    Log("[TFPayload] Pause system initialized");

    DWORD threadId;
    g_hKeyMonitorThread = CreateThread(0, 0, (LPTHREAD_START_ROUTINE)KeyMonitorThread, (LPVOID)baseAddress, 0, &threadId);

    if (g_hKeyMonitorThread == NULL) {
        Log("[TFPayload ERROR] Failed to create key monitor thread!");
        return;
    }

    Log("[TFPayload] Key monitor thread created successfully!");
    Log("[TFPayload] === INITIALIZATION COMPLETE ===");
}

// HOTKEY STATE TRACKING
struct HotkeyState {
    bool f5 = false;
    bool f6 = false;
    bool f7 = false;
    bool f8 = false;
    bool f9 = false;
    bool f10 = false;
    bool f11 = false;
    bool f12 = false;
    bool insert = false;
    bool del = false;
    bool home = false;
};

// HELP TEXT
void PrintHelpText()
{
    Log("  TRIALS FUSION MOD - HOTKEY CONTROLS");
    Log("========================================");
    Log("SYSTEM:");
    Log("  END - (1st) Shutdown and Exit");
    Log("  F1  - (2nd) Reload TFPayload.dll");
    Log("");
    Log("LEADERBOARD SCANNER:");
    Log("  F2  - Load single track by ID (221120)");
    Log("  F3  - Scan current open leaderboard");
    Log("");
    Log("TRACK CENTRAL AUTO-SCROLL:");
    Log("  F5  - Start auto-scroll");
    Log("  F6  - KILLSWITCH (Emergency stop ALL operations)");
    Log("  F12 - Cycle through searches: Ninja -> Mountain -> Speed");
    Log("  INSERT - Decrease scroll delay (-200ms)");
    Log("  DELETE - Increase scroll delay (+200ms)");
    Log("  NOTE: All tracks auto-saved to F:/tracks_data.csv");
    Log("");
    Log("FEED FETCHER:");
    Log("  F8  - Stop feed fetching");
    Log("  F9  - Save feed data to CSV");
    Log("");
    Log("LEADERBOARD DIRECT (Patch-based, No UI required!):");
    Log("  F10 - Test fetch track ID 221120");
    Log("  F11 - Toggle patch (bypass track check)");
    Log("");
    Log("PAUSE Time/Physics:");
    Log("  0   - Toggle pause/resume");
    Log("");
    Log("Results: F:/tracks_data.csv, F:/leaderboard_scans.txt & feed_data.csv");
    Log("========================================\n");
}

// HOTKEY HANDLERS
void HandleF5(HotkeyState& state)
{
    bool f5IsPressed = (GetAsyncKeyState(VK_F5) & 0x8000) != 0;
    if (f5IsPressed && !state.f5) {
        if (!Tracks::IsAutoScrolling()) {
            Log("\n[F5] ===== TRACK AUTO-SCROLL STARTED =====");
            Tracks::AutoScrollConfig config;
            config.delayMs = 2000;
            config.maxScrolls = 0;
            config.useRightArrow = true;
            Tracks::StartAutoScroll(config);
            Log("[F5] Auto-scrolling with 2s delay (Right Arrow)");
            Log("[F5] Press F6 to stop");
        }
        else {
            Log("[F5] Auto-scroll already running! Press F6 to stop first.");
        }
    }
    state.f5 = f5IsPressed;
}

void HandleF6(HotkeyState& state)
{
    bool f6IsPressed = (GetAsyncKeyState(VK_F6) & 0x8000) != 0;
    if (f6IsPressed && !state.f6) {
        if (Tracks::IsKillSwitchActivated()) {
            Tracks::DeactivateKillSwitch();
            Log("[F6] Killswitch DEACTIVATED - ready for new operations");
        }
        else {
            Tracks::ActivateKillSwitch();
            Log("[F6] *** KILLSWITCH ACTIVATED ***");
        }
    }
    state.f6 = f6IsPressed;
}


void HandleF8(HotkeyState& state)
{
    bool f8IsPressed = (GetAsyncKeyState(VK_F8) & 0x8000) != 0;
    if (f8IsPressed && !state.f8) {
        Log("[F8] Stopping feed fetch...");
        FeedFetcher::StopFetch();
    }
    state.f8 = f8IsPressed;
}

void HandleF9(HotkeyState& state)
{
    bool f9IsPressed = (GetAsyncKeyState(VK_F9) & 0x8000) != 0;
    if (f9IsPressed && !state.f9) {
        Log("[F9] Saving feed data...");
        FeedFetcher::SaveToFile();
        auto state_data = FeedFetcher::GetState();
        Log("Saved " << state_data.fetchedTracks.size() << " tracks to CSV");
    }
    state.f9 = f9IsPressed;
}


void HandleF12(HotkeyState& state)
{
    bool f12IsPressed = (GetAsyncKeyState(VK_F12) & 0x8000) != 0;
    if (f12IsPressed && !state.f12) {
        if (!Tracks::IsAutoSearching()) {
            Log("\n[F12] ===== CYCLING TO NEXT SEARCH =====");
            Tracks::CycleToNextSearch();
        }
        else {
            Log("[F12] Search/switch operation already in progress! Please wait...");
        }
    }
    state.f12 = f12IsPressed;
}

void HandleInsert(HotkeyState& state)
{
    bool insertIsPressed = (GetAsyncKeyState(VK_INSERT) & 0x8000) != 0;
    if (insertIsPressed && !state.insert) {
        auto config = Tracks::GetAutoScrollConfig();
        uint32_t newDelay = (config.delayMs > 200) ? config.delayMs - 200 : 200;
        Tracks::SetAutoScrollDelay(newDelay);
        Log("[INSERT] Scroll delay decreased to " << newDelay << "ms");
    }
    state.insert = insertIsPressed;
}

void HandleDelete(HotkeyState& state)
{
    bool deleteIsPressed = (GetAsyncKeyState(VK_DELETE) & 0x8000) != 0;
    if (deleteIsPressed && !state.del) {
        auto config = Tracks::GetAutoScrollConfig();
        uint32_t newDelay = config.delayMs + 200;
        Tracks::SetAutoScrollDelay(newDelay);
        Log("[DELETE] Scroll delay increased to " << newDelay << "ms");
    }
    state.del = deleteIsPressed;
}



// MAIN KEY MONITOR THREAD
DWORD WINAPI KeyMonitorThread(LPVOID lpParam)
{
    DWORD_PTR baseAddress = (DWORD_PTR)lpParam;

    if (baseAddress == 0) {
        Log("Error: Could not retrieve base address for module.");
        return 1;
    }

    DumpStruct dummyObj{};
    auto disableMusic = (void(__cdecl*)())(baseAddress + 0x526D10);
    auto disableReverb = (void(__fastcall*)(DumpStruct obj))(baseAddress + 0xBD1FC0);

    PrintHelpText();

    HotkeyState hotkeyState;

    while (isRunning) {
        if (KeyPress(VK_END)) {
            Log("Ending");
            ShutdownPayload();
        }

        HandleF5(hotkeyState);
        HandleF6(hotkeyState);
        HandleF8(hotkeyState);
        HandleF9(hotkeyState);
        HandleF12(hotkeyState);
        HandleInsert(hotkeyState);
        HandleDelete(hotkeyState);
        LeaderboardScanner::CheckHotkey();
        LeaderboardDirect::CheckHotkey();
        Pause::CheckHotkey();

        Sleep(100);
    }
    return 0;
}

// DLL ENTRY POINT
BOOL APIENTRY DllMain(HMODULE hModule,
    DWORD ul_reason_for_call,
    LPVOID lpReserved)
{
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        break;

    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
        break;

    case DLL_PROCESS_DETACH:
        ShutdownPayload();
        MH_Uninitialize();
        break;
    }

    return TRUE;
}