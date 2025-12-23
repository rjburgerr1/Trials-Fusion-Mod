// dllmain.cpp (TFPayload)
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
#include "save-states.h"
#include "save-physics.h"
#include "save-bike.h"
#include "devMenu.h"
#include "devMenuSync.h"
#include "rendering.h"
#include "actionscript.h"
#include "logging.h"
#include <MinHook.h>

// FORWARD DECLARATIONS
DWORD WINAPI KeyMonitorThread(LPVOID lpParam);

// GLOBAL STATE
bool isRunning = false;
bool isShuttingDown = false; // Prevent re-entry during shutdown
HANDLE g_hKeyMonitorThread = NULL;
int g_monitorCount = 0;
HMONITOR g_monitors[3] = { NULL };
bool g_consolePositioned = false;
HMODULE g_hModule = NULL; // Store our own module handle for self-unloading

// MACROS
#define KeyPress(...) (GetAsyncKeyState(__VA_ARGS__) & 0x1)

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
    LOG_INFO("\n=== TRACK INFO ===");
    LOG_INFO("Track: " << trackInfo.trackName);
    LOG_INFO("Creator: " << trackInfo.creatorName);
    LOG_INFO("Likes: " << trackInfo.likeCount << " | Dislikes: " << trackInfo.dislikeCount);
    LOG_INFO("Downloads: " << trackInfo.downloadCount);
    if (!trackInfo.description.empty()) {
        LOG_INFO("Description: " << trackInfo.description);
    }
}

void OnFeedTrack(const FeedFetcher::TrackMetadata& track)
{
    LOG_INFO("[FEED] Track ID: " << track.trackId
        << " | Name: " << track.trackName
        << " | Author: " << track.authorName);
}


// SHUTDOWN
extern "C" __declspec(dllexport) void ShutdownPayload()
{
    // Prevent re-entry
    if (isShuttingDown) {
        return;
    }
    isShuttingDown = true;

    LOG_INFO("\n[TFPayload] Shutting down for rebuild (Automated Trigger)...");

    isRunning = false;

    if (g_hKeyMonitorThread != NULL) {
        LOG_VERBOSE("[TFPayload] Waiting for monitor thread to exit...");
        WaitForSingleObject(g_hKeyMonitorThread, 2000);
        CloseHandle(g_hKeyMonitorThread);
        g_hKeyMonitorThread = NULL;
    }

    // Shutdown rendering BEFORE dev menu
    Rendering::Shutdown();

    // Shutdown DevMenuSync
    DevMenuSync::Shutdown();

    // Now shutdown dev menu
    if (g_DevMenu) {
        delete g_DevMenu;
        g_DevMenu = nullptr;
    }

    Tracks::Shutdown();
    LeaderboardScanner::Shutdown();
    FeedFetcher::Shutdown();
    LeaderboardDirect::Shutdown();
    Pause::Shutdown();
    SaveStates::Shutdown();
    SavePhysics::Shutdown();
    SaveBike::Shutdown();

    LOG_INFO("[Main] All resources cleaned up.");
}

// PAYLOAD INITIALIZATION (called when hot-loaded)
extern "C" __declspec(dllexport) void PayloadInit()
{
    LOG_INFO("\n[TFPayload] PayloadInit called - Starting payload operations...");

    if (isRunning) {
        LOG_INFO("[TFPayload] Already running, skipping init");
        return;
    }

    isRunning = true;

    // Initialize MinHook
    MH_STATUS mhStatus = MH_Initialize();
    if (mhStatus != MH_OK && mhStatus != MH_ERROR_ALREADY_INITIALIZED) {
        LOG_ERROR("[TFPayload] MinHook initialization failed: " << mhStatus);
        return;
    }
    LOG_VERBOSE("[TFPayload] MinHook initialized successfully");

    const wchar_t* processName = L"trials_fusion.exe";
    const wchar_t* moduleName = L"trials_fusion.exe";
    
    DWORD_PTR baseAddress = GetModuleBaseAddress(GetProcessID(processName), moduleName);
    if (baseAddress == 0) {
        LOG_ERROR("[TFPayload] Could not retrieve base address!");
        return;
    }

    LOG_VERBOSE("[TFPayload] Game Base Address: " << baseAddress);

    // Initialize Hooks
    Tracks::SetLoggingEnabled(true);
    Tracks::SetUpdateCallback(OnTrackUpdate);
    if (Tracks::Initialize()) {
        LOG_VERBOSE("[TFPayload] Track metadata hook initialized successfully!");
        if (!Tracks::LoadSearchTerms("F:/search_terms.txt")) {
            LOG_VERBOSE("[TFPayload] Could not load F:/search_terms.txt, using default search terms");
        }
    } else {
        LOG_ERROR("[TFPayload] Failed to initialize track hooks!");
    }

    // Initialize leaderboard scanner
    LeaderboardScanner::Initialize(baseAddress);
    LOG_VERBOSE("[TFPayload] Leaderboard scanner initialized");

    // Initialize feed fetcher
    FeedFetcher::SetTrackCallback(OnFeedTrack);
    FeedFetcher::Initialize(baseAddress);
    LOG_VERBOSE("[TFPayload] Feed fetcher initialized");

    // Initialize leaderboard direct (patch-based, UI-independent)
    LeaderboardDirect::Initialize(baseAddress);
    LOG_VERBOSE("[TFPayload] Leaderboard direct initialized");

    // Initialize pause system
    Pause::Initialize(baseAddress);
    LOG_VERBOSE("[TFPayload] Pause system initialized");

    // Initialize save states system
    SaveStates::Initialize(baseAddress);
    LOG_VERBOSE("[TFPayload] Save states system initialized");

    // Initialize save physics system
    SavePhysics::Initialize(baseAddress);
    LOG_VERBOSE("[TFPayload] Save physics system initialized");

    // Initialize save bike system
    SaveBike::Initialize(baseAddress);
    LOG_VERBOSE("[TFPayload] Save bike system initialized");

    // Initialize ActionScript messaging system
    if (ActionScript::Initialize()) {
        LOG_VERBOSE("[TFPayload] ActionScript messaging system initialized");
    } else {
        LOG_ERROR("[TFPayload] Failed to initialize ActionScript messaging!");
    }

    // Wait a moment to ensure ProxyDLL has hooked D3D11
    LOG_VERBOSE("[TFPayload] Waiting for ProxyDLL to initialize D3D11...");
    Sleep(500);

    // Initialize rendering system (connects to ProxyDLL's hook)
    if (!Rendering::Initialize()) {
        LOG_ERROR("[TFPayload] Failed to initialize rendering system!");
        LOG_WARNING("[TFPayload] Dev menu will not be available");
    } else {
        LOG_VERBOSE("[TFPayload] Rendering system connected to ProxyDLL");
        
        // NOW initialize ImGui dev menu AFTER rendering is ready
        g_DevMenu = new DevMenu();
        g_DevMenu->Initialize();
        LOG_VERBOSE("[TFPayload] ImGui Dev Menu initialized");
        
        // Initialize DevMenuSync (connects ImGui to game memory)
        if (!DevMenuSync::Initialize()) {
            LOG_ERROR("[TFPayload] Failed to initialize DevMenu sync!");
            LOG_WARNING("[TFPayload] Changes in menu will not affect game!");
        } else {
            LOG_VERBOSE("[TFPayload] DevMenuSync initialized");
            
            // Do initial sync from game to UI
            DevMenuSync::SyncFromGame();
            LOG_VERBOSE("[TFPayload] Initial sync complete");
        }
        
        LOG_VERBOSE("[TFPayload] Dev Menu ready! (Press HOME to toggle)");
    }

    DWORD threadId;
    g_hKeyMonitorThread = CreateThread(0, 0, (LPTHREAD_START_ROUTINE)KeyMonitorThread, (LPVOID)baseAddress, 0, &threadId);

    if (g_hKeyMonitorThread == NULL) {
        LOG_ERROR("[TFPayload] Failed to create key monitor thread!");
        return;
    }

    LOG_VERBOSE("[TFPayload] Key monitor thread created successfully!");
    // Initialize logging system
    Logging::Initialize();
    
    LOG_INFO("[TFPayload] === INITIALIZATION COMPLETE ===");
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
    bool b = false;
    bool d = false;
    bool m = false;
    bool n = false;
    bool u = false;
    bool t = false;
    bool l = false;
    bool c = false;
    bool k = false;
    bool v = false;
    bool equals = false;
    bool hyphen = false;
    bool clearConsole = false;
};

// HELP TEXT
void PrintHelpText()
{
    LOG_INFO("  TRIALS FUSION MOD - HOTKEY CONTROLS");
    LOG_INFO("========================================");
    LOG_INFO("SYSTEM:");
    LOG_INFO("  C   - Clear debug console");
    LOG_INFO("  -   - Show this help text");
    LOG_INFO("  =   - Toggle verbose logging (ON/OFF)");
    LOG_INFO("  END - Shutdown and unload TFPayload.dll (automatic)");
    LOG_INFO("  F1  - Reload TFPayload.dll (load/unload toggle)");
    LOG_INFO("");
    LOG_INFO("LEADERBOARD SCANNER:");
    LOG_INFO("  F2  - Scan current leaderboard view (friends/overall/myscore) for single track by ID (default: 221120)");
    LOG_INFO("  F3  - Scan current open leaderboard");
    LOG_INFO("");
    LOG_INFO("TRACK CENTRAL AUTO-SCROLL:");
    LOG_INFO("  F5  - Start auto-scroll");
    LOG_INFO("  F6  - KILLSWITCH (Emergency stop ALL operations)");
    LOG_INFO("  F12 - Cycle through searches: Ninja -> Mountain -> Speed");
    LOG_INFO("  INSERT - Decrease scroll delay (-200ms)");
    LOG_INFO("  DELETE - Increase scroll delay (+200ms)");
    LOG_INFO("  NOTE: All tracks auto-saved to F:/tracks_data.csv");
    LOG_INFO("");
    LOG_INFO("FEED FETCHER:");
    LOG_INFO("  F8  - Stop feed fetching");
    LOG_INFO("  F9  - Save feed data to CSV");
    LOG_INFO("");
    LOG_INFO("LEADERBOARD DIRECT (Patch-based, No UI required!):");
    LOG_INFO("  F10 - Test fetch track ID 221120");
    LOG_INFO("  F11 - Toggle patch (bypass track check)");
    LOG_INFO("");
    LOG_INFO("PAUSE Time/Physics:");
    LOG_INFO("  0   - Toggle pause/resume");
    LOG_INFO("");
    LOG_INFO("SAVE STATES (TAS-style):");
    LOG_INFO("  F7       - Quick save (slot 0)");
    LOG_INFO("  F8       - Quick load (slot 0)");
    LOG_INFO("  SHIFT+1-9 - Save to slot 1-9");
    LOG_INFO("  1-9      - Load from slot 1-9");
    LOG_INFO("");
    LOG_INFO("SAVE PHYSICS (Camera state - using game functions):");
    LOG_INFO("  1  - Quick save physics (slot 0)");
    LOG_INFO("  2  - Quick load physics (slot 0)");
    LOG_INFO("");
    LOG_INFO("SAVE BIKE (Actual bike entity):");
    LOG_INFO("  3  - Quick save bike (slot 0)");
    LOG_INFO("  4  - Quick load bike (slot 0)");
    LOG_INFO("");
    LOG_INFO("DEV MENU / TWEAKABLES:");
    LOG_INFO("  HOME - Toggle Custom ImGui Dev Menu (Renders in-game!)");
    LOG_INFO("  B    - Dump tweakables data (see what's available)");
    LOG_INFO("");
    LOG_INFO("ACTIONSCRIPT COMMANDS:");
    LOG_INFO("  T        - Full countdown sequence (3, 2, 1, GO, Ready!) with auto-timing");
    LOG_INFO("  SHIFT+T  - Show single countdown value (39)");
    LOG_INFO("");
    LOG_INFO("ACTIONSCRIPT MESSAGING:");
    LOG_INFO("  L        - Test ActionScript commands (loading screen, clear messages)");
    LOG_INFO("");
    LOG_INFO("Results: F:/tracks_data.csv, F:/leaderboard_scans.txt & feed_data.csv");
    LOG_INFO("========================================\n");
}

// HOTKEY HANDLERS
void HandleF5(HotkeyState& state)
{
    bool f5IsPressed = (GetAsyncKeyState(VK_F5) & 0x8000) != 0;
    if (f5IsPressed && !state.f5) {
        if (!Tracks::IsAutoScrolling()) {
            LOG_INFO("\n[F5] ===== TRACK AUTO-SCROLL STARTED =====");
            Tracks::AutoScrollConfig config;
            config.delayMs = 2000;
            config.maxScrolls = 0;
            config.useRightArrow = true;
            Tracks::StartAutoScroll(config);
            LOG_INFO("[F5] Auto-scrolling with 2s delay (Right Arrow)");
            LOG_INFO("[F5] Press F6 to stop");
        }
        else {
            LOG_INFO("[F5] Auto-scroll already running! Press F6 to stop first.");
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
            LOG_INFO("[F6] Killswitch DEACTIVATED - ready for new operations");
        }
        else {
            Tracks::ActivateKillSwitch();
            LOG_INFO("[F6] *** KILLSWITCH ACTIVATED ***");
        }
    }
    state.f6 = f6IsPressed;
}


void HandleF8(HotkeyState& state)
{
    bool f8IsPressed = (GetAsyncKeyState(VK_F8) & 0x8000) != 0;
    if (f8IsPressed && !state.f8) {
        LOG_VERBOSE("[F8] Stopping feed fetch...");
        FeedFetcher::StopFetch();
    }
    state.f8 = f8IsPressed;
}

void HandleF9(HotkeyState& state)
{
    bool f9IsPressed = (GetAsyncKeyState(VK_F9) & 0x8000) != 0;
    if (f9IsPressed && !state.f9) {
        LOG_VERBOSE("[F9] Saving feed data...");
        FeedFetcher::SaveToFile();
        auto state_data = FeedFetcher::GetState();
        LOG_VERBOSE("Saved " << state_data.fetchedTracks.size() << " tracks to CSV");
    }
    state.f9 = f9IsPressed;
}


void HandleF12(HotkeyState& state)
{
    bool f12IsPressed = (GetAsyncKeyState(VK_F12) & 0x8000) != 0;
    if (f12IsPressed && !state.f12) {
        if (!Tracks::IsAutoSearching()) {
            LOG_VERBOSE("\n[F12] ===== CYCLING TO NEXT SEARCH =====");
            Tracks::CycleToNextSearch();
        }
        else {
            LOG_VERBOSE("[F12] Search/switch operation already in progress! Please wait...");
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
        LOG_VERBOSE("[INSERT] Scroll delay decreased to " << newDelay << "ms");
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
        LOG_VERBOSE("[DELETE] Scroll delay increased to " << newDelay << "ms");
    }
    state.del = deleteIsPressed;
}

void HandleB(HotkeyState& state)
{
    bool bIsPressed = (GetAsyncKeyState('B') & 0x8000) != 0;
    if (bIsPressed && !state.b) {
        LOG_VERBOSE("[B] Dumping tweakables data...");
        if (g_DevMenu) {
            g_DevMenu->DumpTweakablesData();
        } else {
            LOG_ERROR("[B] DevMenu not initialized!");
        }
    }
    state.b = bIsPressed;
}

void HandleT(HotkeyState& state)
{
    bool tIsPressed = (GetAsyncKeyState('T') & 0x8000) != 0;
    bool shiftPressed = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    
    if (tIsPressed && !state.t) {
        LOG_VERBOSE("");
        
        if (shiftPressed) {
            // SHIFT+T: Show just single integer
            LOG_VERBOSE("[SHIFT+T] Calling ShowStartCountdownDirect(39)...");
            if (ActionScript::ShowStartCountdownDirect(39)) {
                LOG_VERBOSE("[SHIFT+T] Successfully showed countdown value 39");
            } else {
                LOG_ERROR("[SHIFT+T] Failed to show countdown");
            }
        } else {
            // Just T: Full countdown sequence (3, 2, 1, GO)
            LOG_VERBOSE("[T] Starting full countdown sequence (3, 2, 1, GO)...");
            if (ActionScript::ShowFullCountdownSequence()) {
                LOG_VERBOSE("[T] Full countdown sequence completed!");
            } else {
                LOG_ERROR("[T] Countdown sequence failed");
            }
        }
        LOG_VERBOSE("");
    }
    state.t = tIsPressed;
}

void HandleL(HotkeyState& state)
{
    bool lIsPressed = (GetAsyncKeyState('L') & 0x8000) != 0;
    if (lIsPressed && !state.l) {
        static bool loadingScreenVisible = false;
        
        LOG_VERBOSE("");
        LOG_VERBOSE("[L] === TOGGLING LOADING SCREEN ===");
        
        // Toggle the loading screen state
        loadingScreenVisible = !loadingScreenVisible;
        
        if (ActionScript::ShowLoadingScreen(loadingScreenVisible, "TESTING FROM C++")) {
            LOG_VERBOSE("[L] Successfully " << (loadingScreenVisible ? "SHOWED" : "HID") << " loading screen");
        } else {
            LOG_ERROR("[L] Failed to toggle loading screen");
        }
        
        LOG_VERBOSE("");
    }
    state.l = lIsPressed;
}

void HandleV(HotkeyState& state)
{
    bool vIsPressed = (GetAsyncKeyState('V') & 0x8000) != 0;
    if (vIsPressed && !state.v) {
        LOG_VERBOSE("");
        LOG_VERBOSE("[V] Calling HandleRaceFinish (proper race finish flow)...");
        if (ActionScript::CallHandleRaceFinish()) {
            LOG_VERBOSE("[V] HandleRaceFinish called successfully!");
        } else {
            LOG_ERROR("[V] HandleRaceFinish failed");
        }
        LOG_VERBOSE("");
    }
    state.v = vIsPressed;
}

void HandleEquals(HotkeyState& state)
{
    bool equalsIsPressed = (GetAsyncKeyState(VK_OEM_PLUS) & 0x8000) != 0;
    if (equalsIsPressed && !state.equals) {
        Logging::ToggleVerbose();
    }
    state.equals = equalsIsPressed;
}

void HandleHyphen(HotkeyState& state)
{
    bool hyphenIsPressed = (GetAsyncKeyState(VK_OEM_MINUS) & 0x8000) != 0;
    if (hyphenIsPressed && !state.hyphen) {
        LOG_INFO("");
        PrintHelpText();
    }
    state.hyphen = hyphenIsPressed;
}

void HandleClearConsole(HotkeyState& state)
{
    bool cIsPressed = (GetAsyncKeyState('C') & 0x8000) != 0;
    if (cIsPressed && !state.clearConsole) {
        system("cls");
        LOG_VERBOSE("[C] Debug console cleared");
    }
    state.clearConsole = cIsPressed;
}

void HandleHomeImGuiMenu(HotkeyState& state)
{
    bool homeIsPressed = (GetAsyncKeyState(VK_HOME) & 0x8000) != 0;
    if (homeIsPressed && !state.home) {
        if (g_DevMenu) {
            g_DevMenu->Toggle();
            LOG_VERBOSE("[HOME] Custom ImGui Dev Menu " << (g_DevMenu->IsVisible() ? "OPENED" : "CLOSED"));
        } else {
            LOG_ERROR("[HOME] Custom ImGui Dev Menu not initialized!");
        }
    }
    state.home = homeIsPressed;
}

// MAIN KEY MONITOR THREAD
DWORD WINAPI KeyMonitorThread(LPVOID lpParam)
{
    DWORD_PTR baseAddress = (DWORD_PTR)lpParam;

    if (baseAddress == 0) {
        LOG_INFO("Error: Could not retrieve base address for module.");
        return 1;
    }

    DumpStruct dummyObj{};
    auto disableMusic = (void(__cdecl*)())(baseAddress + 0x526D10);
    auto disableReverb = (void(__fastcall*)(DumpStruct obj))(baseAddress + 0xBD1FC0);

    PrintHelpText();

    HotkeyState hotkeyState;

    while (isRunning) {
        if (KeyPress(VK_END)) {
            LOG_INFO("[END] Shutting down and unloading...");
            
            // First, trigger shutdown which will clean up everything
            ShutdownPayload();
            
            // Now that shutdown is complete, schedule the DLL unload
            // We need to do this on a separate thread because we can't unload ourselves
            if (g_hModule != NULL) {
                LOG_INFO("[TFPayload] Scheduling DLL unload...");
                
                // Create a thread that will unload the DLL
                HANDLE hUnloadThread = CreateThread(NULL, 0, [](LPVOID param) -> DWORD {
                    HMODULE hMod = (HMODULE)param;
                    
                    // Wait a bit to ensure the key monitor thread has fully exited
                    Sleep(250);
                    
                    LOG_INFO("[TFPayload] Unloading DLL now...");
                    
                    // Unload the DLL and exit this thread
                    FreeLibraryAndExitThread(hMod, 0);
                    return 0;
                }, g_hModule, 0, NULL);
                
                if (hUnloadThread) {
                    CloseHandle(hUnloadThread);
                }
            }
            
            // Exit the key monitor thread
            return 0;
        }

        HandleF5(hotkeyState);
        HandleF6(hotkeyState);
        HandleF9(hotkeyState);
        HandleF12(hotkeyState);
        HandleInsert(hotkeyState);
        HandleDelete(hotkeyState);
        HandleB(hotkeyState);
        HandleT(hotkeyState);
        HandleL(hotkeyState);
        HandleV(hotkeyState);
        HandleEquals(hotkeyState);
        HandleHyphen(hotkeyState);
        HandleClearConsole(hotkeyState);
        HandleHomeImGuiMenu(hotkeyState);
        
        LeaderboardScanner::CheckHotkey();
        LeaderboardDirect::CheckHotkey();
        Pause::CheckHotkey();
        SaveStates::CheckHotkeys();
        
        // Check save-physics hotkeys (1 = save camera, 2 = load camera)
        static bool key1Pressed = false;
        bool key1IsPressed = (GetAsyncKeyState('1') & 0x8000) != 0;
        if (key1IsPressed && !key1Pressed) {
            SavePhysics::QuickSavePhysics();
        }
        key1Pressed = key1IsPressed;

        static bool key2Pressed = false;
        bool key2IsPressed = (GetAsyncKeyState('2') & 0x8000) != 0;
        if (key2IsPressed && !key2Pressed) {
            SavePhysics::QuickLoadPhysics();
        }
        key2Pressed = key2IsPressed;

        // Check save-bike hotkeys (3 = save bike, 4 = load bike)
        static bool key3Pressed = false;
        bool key3IsPressed = (GetAsyncKeyState('3') & 0x8000) != 0;
        if (key3IsPressed && !key3Pressed) {
            SaveBike::QuickSaveBike();
        }
        key3Pressed = key3IsPressed;

        static bool key4Pressed = false;
        bool key4IsPressed = (GetAsyncKeyState('4') & 0x8000) != 0;
        if (key4IsPressed && !key4Pressed) {
            SaveBike::QuickLoadBike();
        }
        key4Pressed = key4IsPressed;
        
        Sleep(125);
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
        g_hModule = hModule; // Store our module handle
        PayloadInit();
        break;

    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
        break;

    case DLL_PROCESS_DETACH:
        // Only shutdown if we haven't already (prevents double shutdown)
        if (!isShuttingDown) {
            ShutdownPayload();
        }
        MH_Uninitialize();
        break;
    }

    return TRUE;
}
