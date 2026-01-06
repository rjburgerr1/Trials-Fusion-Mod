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
#include "leaderboard_direct.h"
#include "pause.h"
#include "devMenu.h"
#include "devMenuSync.h"
#include "rendering.h"
#include "actionscript.h"
#include "logging.h"
#include "respawn.h"
#include "camera.h"
#include "multiplayer.h"
#include "keybindings.h"
#include "bike-swap.h"
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

// SHUTDOWN
extern "C" __declspec(dllexport) void ShutdownPayload()
{
    // Prevent re-entry
    if (isShuttingDown) {
        return;
    }
    isShuttingDown = true;

    LOG_VERBOSE("\n[TFPayload] Shutting down for rebuild (Automated Trigger)...");

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
    LeaderboardDirect::Shutdown();
    Pause::Shutdown();
    Respawn::Shutdown();
    Camera::Shutdown();
    Multiplayer::Shutdown();
    Keybindings::Shutdown();
    BikeSwap::Shutdown();

    LOG_VERBOSE("[Main] All resources cleaned up.");
}

// PAYLOAD INITIALIZATION (called when hot-loaded)
extern "C" __declspec(dllexport) void PayloadInit()
{
    LOG_INFO("\n[TFPayload] PayloadInit called - Starting payload operations...");

    if (isRunning) {
        LOG_VERBOSE("[TFPayload] Already running, skipping init");
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

    // Initialize leaderboard direct (patch-based, UI-independent)
    LeaderboardDirect::Initialize(baseAddress);
    LOG_VERBOSE("[TFPayload] Leaderboard direct initialized");

    // Initialize pause system
    Pause::Initialize(baseAddress);
    LOG_VERBOSE("[TFPayload] Pause system initialized");

    // Initialize ActionScript messaging system
    if (ActionScript::Initialize()) {
        LOG_VERBOSE("[TFPayload] ActionScript messaging system initialized");
    } else {
        LOG_ERROR("[TFPayload] Failed to initialize ActionScript messaging!");
    }

    // Initialize respawn system
    if (Respawn::Initialize(baseAddress)) {
        LOG_VERBOSE("[TFPayload] Respawn system initialized");
    } else {
        LOG_ERROR("[TFPayload] Failed to initialize respawn system!");
    }

    // Initialize camera system
    if (Camera::Initialize(baseAddress)) {
        LOG_VERBOSE("[TFPayload] Camera system initialized");
    } else {
        LOG_ERROR("[TFPayload] Failed to initialize camera system!");
    }
    
    // Initialize multiplayer monitoring (Phase 1)
    if (Multiplayer::Initialize(baseAddress)) {
        LOG_VERBOSE("[TFPayload] Multiplayer monitoring initialized (Phase 1)");
    } else {
        LOG_ERROR("[TFPayload] Failed to initialize multiplayer monitoring!");
    }
    
    // Initialize bike swap system
    if (BikeSwap::Initialize(baseAddress)) {
        LOG_VERBOSE("[TFPayload] Bike swap system initialized");
    } else {
        LOG_ERROR("[TFPayload] Failed to initialize bike swap system!");
    }
    
    // Initialize logging system
    Logging::Initialize();
    
    // Initialize keybindings system BEFORE dev menu
    Keybindings::Initialize();

    // Wait a moment to ensure ProxyDLL has hooked D3D11
    LOG_VERBOSE("[TFPayload] Waiting for ProxyDLL to initialize D3D11...");
    Sleep(400);

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
    bool t = false;
    bool l = false;
    bool k = false;
    bool y = false;
    bool p = false;
    bool o = false;
    bool equals = false;
    bool hyphen = false;
    bool clearConsole = false;
    bool semicolon = false;
};





// HELP TEXT
void PrintHelpText()
{
    // Get keybinding names at runtime
    std::string InstantFinishKey = Keybindings::GetKeyName(Keybindings::GetKey(Keybindings::Action::InstantFinish));
    std::string ToggleDevMenuKey = Keybindings::GetKeyName(Keybindings::GetKey(Keybindings::Action::ToggleDevMenu));
    std::string ClearConsoleKey = Keybindings::GetKeyName(Keybindings::GetKey(Keybindings::Action::ClearConsole));
    std::string ToggleVerboseLoggingKey = Keybindings::GetKeyName(Keybindings::GetKey(Keybindings::Action::ToggleVerboseLogging));
    std::string ShowHelpTextKey = Keybindings::GetKeyName(Keybindings::GetKey(Keybindings::Action::ShowHelpText));
    std::string DumpTweakablesKey = Keybindings::GetKeyName(Keybindings::GetKey(Keybindings::Action::DumpTweakables));
    std::string CycleHUDKey = Keybindings::GetKeyName(Keybindings::GetKey(Keybindings::Action::CycleHUD));
    std::string RespawnAtCheckpointKey = Keybindings::GetKeyName(Keybindings::GetKey(Keybindings::Action::RespawnAtCheckpoint));
    std::string RespawnPrevCheckpointKey = Keybindings::GetKeyName(Keybindings::GetKey(Keybindings::Action::RespawnPrevCheckpoint));
    std::string RespawnNextCheckpointKey = Keybindings::GetKeyName(Keybindings::GetKey(Keybindings::Action::RespawnNextCheckpoint));
    std::string RespawnForward5Key = Keybindings::GetKeyName(Keybindings::GetKey(Keybindings::Action::RespawnForward5));
    std::string IncrementFaultKey = Keybindings::GetKeyName(Keybindings::GetKey(Keybindings::Action::IncrementFault));
    std::string DebugFaultCounterKey = Keybindings::GetKeyName(Keybindings::GetKey(Keybindings::Action::DebugFaultCounter));
    std::string Add100FaultsKey = Keybindings::GetKeyName(Keybindings::GetKey(Keybindings::Action::Add100Faults));
    std::string Subtract100FaultsKey = Keybindings::GetKeyName(Keybindings::GetKey(Keybindings::Action::Subtract100Faults));
    std::string ResetFaultsKey = Keybindings::GetKeyName(Keybindings::GetKey(Keybindings::Action::ResetFaults));
    std::string DebugTimeCounterKey = Keybindings::GetKeyName(Keybindings::GetKey(Keybindings::Action::DebugTimeCounter));
    std::string Add60SecondsKey = Keybindings::GetKeyName(Keybindings::GetKey(Keybindings::Action::Add60Seconds));
    std::string Subtract60SecondsKey = Keybindings::GetKeyName(Keybindings::GetKey(Keybindings::Action::Subtract60Seconds));
    std::string Add10MinuteKey = Keybindings::GetKeyName(Keybindings::GetKey(Keybindings::Action::Add10Minute));
    std::string ResetTimeKey = Keybindings::GetKeyName(Keybindings::GetKey(Keybindings::Action::ResetTime));
    std::string ToggleLimitValidationKey = Keybindings::GetKeyName(Keybindings::GetKey(Keybindings::Action::ToggleLimitValidation));
    // Leaderboard Scanner
    std::string ScanLeaderboardByIDKey = Keybindings::GetKeyName(Keybindings::GetKey(Keybindings::Action::ScanLeaderboardByID));
    std::string ScanCurrentLeaderboardKey = Keybindings::GetKeyName(Keybindings::GetKey(Keybindings::Action::ScanCurrentLeaderboard));
    // Track Central Auto-Scroll
    std::string StartAutoScrollKey = Keybindings::GetKeyName(Keybindings::GetKey(Keybindings::Action::StartAutoScroll));
    std::string KillswitchKey = Keybindings::GetKeyName(Keybindings::GetKey(Keybindings::Action::Killswitch));
    std::string CycleSearchKey = Keybindings::GetKeyName(Keybindings::GetKey(Keybindings::Action::CycleSearch));
    std::string DecreaseScrollDelayKey = Keybindings::GetKeyName(Keybindings::GetKey(Keybindings::Action::DecreaseScrollDelay));
    std::string IncreaseScrollDelayKey = Keybindings::GetKeyName(Keybindings::GetKey(Keybindings::Action::IncreaseScrollDelay));
    // Leaderboard Direct
    std::string TestFetchTrackIDKey = Keybindings::GetKeyName(Keybindings::GetKey(Keybindings::Action::TestFetchTrackID));
    std::string TogglePatchKey = Keybindings::GetKeyName(Keybindings::GetKey(Keybindings::Action::TogglePatch));
    // Pause controls
    std::string TogglePauseKey = Keybindings::GetKeyName(Keybindings::GetKey(Keybindings::Action::TogglePause));
    // Multiplayer Monitoring
    std::string SaveMultiplayerLogsKey = Keybindings::GetKeyName(Keybindings::GetKey(Keybindings::Action::SaveMultiplayerLogs));
    std::string CaptureSessionStateKey = Keybindings::GetKeyName(Keybindings::GetKey(Keybindings::Action::CaptureSessionState));
    // Physics Logging
    std::string TogglePhysicsLoggingKey = Keybindings::GetKeyName(Keybindings::GetKey(Keybindings::Action::TogglePhysicsLogging));
    std::string DumpPhysicsLogKey = Keybindings::GetKeyName(Keybindings::GetKey(Keybindings::Action::DumpPhysicsLog));
    std::string ModifyXPositionKey = Keybindings::GetKeyName(Keybindings::GetKey(Keybindings::Action::ModifyXPosition));
    // ActionScript Commands
    std::string FullCountdownSequenceKey = Keybindings::GetKeyName(Keybindings::GetKey(Keybindings::Action::FullCountdownSequence));
    std::string ShowSingleCountdownKey = Keybindings::GetKeyName(Keybindings::GetKey(Keybindings::Action::ShowSingleCountdown));
    std::string ToggleLoadScreen = Keybindings::GetKeyName(Keybindings::GetKey(Keybindings::Action::ToggleLoadScreen));
    
    LOG_INFO("  TRIALS FUSION MOD - HOTKEY CONTROLS");
    LOG_INFO("========================================");
    LOG_INFO("Help Commands:");
    LOG_INFO("\t" << ClearConsoleKey << "\t\t\t\t- Clear debug console");
    LOG_INFO("\t" << ShowHelpTextKey << "\t\t\t\t- Show this help text");
    LOG_INFO("\t" << ToggleVerboseLoggingKey << "\t\t\t\t- Toggle verbose logging (ON/OFF)");
    LOG_INFO("\tEND\t\t\t\t- Shutdown and unload TFPayload.dll(automatic)");
    LOG_INFO("\tF1\t\t\t\t- Reload TFPayload.dll (load/unload toggle)");
    LOG_INFO("\t" << ToggleDevMenuKey << "\t\t\t\t- Open DevMenu");
    LOG_INFO("\tK\t\t\t\t- Open Keybindings Menu");
    LOG_INFO("");
    LOG_INFO("Track Functions:");
    LOG_INFO("\t" << InstantFinishKey << "\t\t\t\t- Instant Pass Track");
    LOG_INFO("\t" << CycleHUDKey << "\t\t\t\t- Cycle HUD Visibility in Track/Replay");
    LOG_INFO("\t" << TogglePauseKey << "\t\t\t\t- Toggle time/physics ON/OFF");
    LOG_INFO("\t" << Add60SecondsKey << "\t\t\t\t- Add 1 Minute to Timer");
    LOG_INFO("\t" << Subtract60SecondsKey << "\t\t\t\t- Subtract 1 Minute from Timer");
    LOG_INFO("\t" << Add10MinuteKey << "\t\t\t\t- Add 10 minutes to Timer");
    LOG_INFO("\t" << Add100FaultsKey << "\t\t\t\t- Add 100 faults to fault-counter");
    LOG_INFO("\t" << Subtract100FaultsKey << "\t\t\t\t- Subtract 100 faults from fault-counter");
    LOG_INFO("\t" << ResetFaultsKey << "\t\t\t\t- Reset Faults to 0");
    LOG_INFO("\t" << IncrementFaultKey << "\t\t\t\t- Add 1 fault to fault-counter");
    LOG_INFO("\t" << ToggleLimitValidationKey << "\t\t\t\t- Toggle Fault/Time Limits");
    LOG_INFO("");
    LOG_INFO("Leaderboard:");
    LOG_INFO("\t" << ScanLeaderboardByIDKey << "\t\t\t\t- Scan current leaderboard (friends/overall/myscore) for single track by ID (default: 221120)");
    LOG_INFO("\t" << ScanCurrentLeaderboardKey << "\t\t\t\t- Scan current open leaderboard");
    LOG_INFO("\t" << TestFetchTrackIDKey << "\t\t\t\t- Test fetch track ID 221120");
    LOG_INFO("");
    LOG_INFO("Track Central:");
    LOG_INFO("\t" << StartAutoScrollKey << "\t\t\t\t- Start auto-scroll search-pages");
    LOG_INFO("\t" << KillswitchKey << "\t\t\t\t- KILLSWITCH (Emergency stop ALL operations)");
    LOG_INFO("\t" << CycleSearchKey << "\t\t\t\t- Cycle through searches: Ninja -> Mountain -> Speed");
    LOG_INFO("\t" << DecreaseScrollDelayKey << "\t\t\t\t- Decrease scroll delay (-200ms)");
    LOG_INFO("\t" << IncreaseScrollDelayKey << "\t\t\t\t- Increase scroll delay (+200ms)");
    LOG_VERBOSE("  NOTE: All tracks auto-saved to F:/tracks_data.csv");
    LOG_INFO("");
    LOG_INFO("Checkpoints:");
    LOG_INFO("\t" << RespawnAtCheckpointKey << "\t\t\t\t- Respawn at current checkpoint");
    LOG_INFO("\t" << RespawnNextCheckpointKey << "\t\t\t\t- Respawn at next checkpoint");
    LOG_INFO("\t" << RespawnPrevCheckpointKey << "\t\t\t\t- Respawn at previous checkpoint");
    LOG_INFO("\t" << RespawnForward5Key << "\t\t\t\t- Respawn 5 checkpoints ahead");
    LOG_INFO("");
    LOG_INFO("Multiplayer(Phase 1):");
    LOG_INFO("\t" << SaveMultiplayerLogsKey << "\t\t\t\t- Save all multiplayer logs (sessions, packets, stats)");
    LOG_INFO("\t" << CaptureSessionStateKey << "\t\t\t\t- Capture current session state");
    LOG_VERBOSE("Logs: F:/mp_session_log.txt, F:/mp_sessions.csv, F:/mp_packets.csv");
    LOG_INFO("");
    LOG_INFO("Dev Menu:");
    LOG_INFO("\t" << DumpTweakablesKey << "\t\t\t\t- Dump tweakables data (see what's available)");
    LOG_INFO("");
    LOG_INFO("ACTIONSCRIPT COMMANDS:");
    LOG_INFO("\t" << FullCountdownSequenceKey << "\t\t\t\t- Full countdown sequence (3, 2, 1, GO, Ready!) with auto-timing");
    LOG_INFO("");
    LOG_INFO("ACTIONSCRIPT/FLASH COMMAND MESSAGING:");
    LOG_INFO("\t" << ToggleLoadScreen << "\t\t\t\t- Toggle loading screen");
    LOG_INFO("");
    LOG_INFO("BIKE SWAP (Runtime Bike Changing):");
    LOG_INFO("\t[\t\t\t\t- Cycle to previous bike");
    LOG_INFO("\t]\t\t\t\t- Cycle to next bike");
    LOG_INFO("\t\\\t\t\t\t- Debug dump bike state");
    LOG_INFO("");
    LOG_VERBOSE("Results: F:/tracks_data.csv, F:/leaderboard_scans.txt & feed_data.csv");
}

// HOTKEY HANDLERS
void HandleF5()
{
    if (Keybindings::IsActionPressed(Keybindings::Action::StartAutoScroll)) {
        std::string keyName = Keybindings::GetKeyName(Keybindings::GetKey(Keybindings::Action::StartAutoScroll));
        if (!Tracks::IsAutoScrolling()) {
            LOG_INFO("\n[" << keyName << "] ===== TRACK AUTO-SCROLL STARTED =====");
            Tracks::AutoScrollConfig config;
            config.delayMs = 2000;
            config.maxScrolls = 0;
            config.useRightArrow = true;
            Tracks::StartAutoScroll(config);
            LOG_INFO("[" << keyName << "] Auto-scrolling with 2s delay (Right Arrow)");
            LOG_INFO("[" << keyName << "] Press F6 to stop");
        }
        else {
            LOG_INFO("[" << keyName << "] Auto-scroll already running! Press F6 to stop first.");
        }
    }
}

void HandleF6()
{
    if (Keybindings::IsActionPressed(Keybindings::Action::Killswitch)) {
        std::string keyName = Keybindings::GetKeyName(Keybindings::GetKey(Keybindings::Action::Killswitch));
        if (Tracks::IsKillSwitchActivated()) {
            Tracks::DeactivateKillSwitch();
            LOG_INFO("[" << keyName << "] Killswitch DEACTIVATED - ready for new operations");
        }
        else {
            Tracks::ActivateKillSwitch();
            LOG_INFO("[" << keyName << "] *** KILLSWITCH ACTIVATED ***");
        }
    }
}

void HandleF12()
{
    if (Keybindings::IsActionPressed(Keybindings::Action::CycleSearch)) {
        std::string keyName = Keybindings::GetKeyName(Keybindings::GetKey(Keybindings::Action::CycleSearch));
        if (!Tracks::IsAutoSearching()) {
            LOG_VERBOSE("\n[" << keyName << "] ===== CYCLING TO NEXT SEARCH =====");
            Tracks::CycleToNextSearch();
        }
        else {
            LOG_VERBOSE("[" << keyName << "] Search/switch operation already in progress! Please wait...");
        }
    }
}

void HandleInsert()
{
    if (Keybindings::IsActionPressed(Keybindings::Action::DecreaseScrollDelay)) {
        std::string keyName = Keybindings::GetKeyName(Keybindings::GetKey(Keybindings::Action::DecreaseScrollDelay));
        auto config = Tracks::GetAutoScrollConfig();
        uint32_t newDelay = (config.delayMs > 200) ? config.delayMs - 200 : 200;
        Tracks::SetAutoScrollDelay(newDelay);
        LOG_VERBOSE("[" << keyName << "] Scroll delay decreased to " << newDelay << "ms");
    }
}

void HandleDelete()
{
    if (Keybindings::IsActionPressed(Keybindings::Action::IncreaseScrollDelay)) {
        std::string keyName = Keybindings::GetKeyName(Keybindings::GetKey(Keybindings::Action::IncreaseScrollDelay));
        auto config = Tracks::GetAutoScrollConfig();
        uint32_t newDelay = config.delayMs + 200;
        Tracks::SetAutoScrollDelay(newDelay);
        LOG_VERBOSE("[" << keyName << "] Scroll delay increased to " << newDelay << "ms");
    }
}

void HandleDumpTweakables()
{
    if (Keybindings::IsActionPressed(Keybindings::Action::DumpTweakables)) {
        std::string keyName = Keybindings::GetKeyName(Keybindings::GetKey(Keybindings::Action::DumpTweakables));
        LOG_VERBOSE("[" << keyName << "] Dumping tweakables data...");
        if (g_DevMenu) {
            g_DevMenu->DumpTweakablesData();
        } else {
            LOG_ERROR("[" << keyName << "] DevMenu not initialized!");
        }
    }
}

void HandleT()
{
    // Check for SHIFT+T (ShowSingleCountdown) first
    bool shiftPressed = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    
    if (shiftPressed && Keybindings::IsActionPressed(Keybindings::Action::ShowSingleCountdown)) {
        std::string keyName = Keybindings::GetKeyName(Keybindings::GetKey(Keybindings::Action::ShowSingleCountdown));
        LOG_VERBOSE("");
        LOG_VERBOSE("[" << keyName << "] Calling ShowStartCountdownDirect(39)...");
        if (ActionScript::ShowStartCountdownDirect(39)) {
            LOG_VERBOSE("[" << keyName << "] Successfully showed countdown value 39");
        } else {
            LOG_ERROR("[" << keyName << "] Failed to show countdown");
        }
        LOG_VERBOSE("");
    }
    // Check for regular T (FullCountdownSequence)
    else if (Keybindings::IsActionPressed(Keybindings::Action::FullCountdownSequence)) {
        std::string keyName = Keybindings::GetKeyName(Keybindings::GetKey(Keybindings::Action::FullCountdownSequence));
        LOG_VERBOSE("");
        LOG_VERBOSE("[" << keyName << "] Starting full countdown sequence (3, 2, 1, GO)...");
        if (ActionScript::ShowFullCountdownSequence()) {
            LOG_VERBOSE("[" << keyName << "] Full countdown sequence completed!");
        } else {
            LOG_ERROR("[" << keyName << "] Countdown sequence failed");
        }
        LOG_VERBOSE("");
    }
}

void HandleL()
{
    if (Keybindings::IsActionPressed(Keybindings::Action::ToggleLoadScreen)) {
        std::string keyName = Keybindings::GetKeyName(Keybindings::GetKey(Keybindings::Action::ToggleLoadScreen));
        static bool loadingScreenVisible = false;
        
        LOG_VERBOSE("");
        LOG_VERBOSE("[" << keyName << "] === TOGGLING LOADING SCREEN ===");
        
        // Toggle the loading screen state
        loadingScreenVisible = !loadingScreenVisible;
        
        if (ActionScript::ShowLoadingScreen(loadingScreenVisible, "TESTING FROM C++")) {
            LOG_VERBOSE("[" << keyName << "] Successfully " << (loadingScreenVisible ? "SHOWED" : "HID") << " loading screen");
        } else {
            LOG_ERROR("[" << keyName << "] Failed to toggle loading screen");
        }
        
        LOG_VERBOSE("");
    }
}

void HandleInstantFinish()
{
    if (Keybindings::IsActionPressed(Keybindings::Action::InstantFinish)) {
        std::string keyName = Keybindings::GetKeyName(Keybindings::GetKey(Keybindings::Action::InstantFinish));
        LOG_VERBOSE("");
        LOG_VERBOSE("[" << keyName << "] Calling HandleRaceFinish (proper race finish flow)...");
        if (ActionScript::CallHandleRaceFinish()) {
            LOG_VERBOSE("[" << keyName << "] HandleRaceFinish called successfully!");
        } else {
            LOG_ERROR("[" << keyName << "] HandleRaceFinish failed");
        }
        LOG_VERBOSE("");
    }
}

void HandleToggleVerbose()
{
    if (Keybindings::IsActionPressed(Keybindings::Action::ToggleVerboseLogging)) {
        Logging::ToggleVerbose();
    }
}

void HandleShowHelp()
{
    if (Keybindings::IsActionPressed(Keybindings::Action::ShowHelpText)) {
        LOG_INFO("");
        PrintHelpText();
    }
}

void HandleClearConsole()
{
    if (Keybindings::IsActionPressed(Keybindings::Action::ClearConsole)) {
        system("cls");
        std::string keyName = Keybindings::GetKeyName(Keybindings::GetKey(Keybindings::Action::ClearConsole));
        LOG_VERBOSE("[" << keyName << "] Debug console cleared");
    }
}

void HandleHomeImGuiMenu()
{
    if (Keybindings::IsActionPressed(Keybindings::Action::ToggleDevMenu)) {
        if (g_DevMenu) {
            g_DevMenu->Toggle();
            LOG_VERBOSE("[HOME] Custom ImGui Dev Menu " << (g_DevMenu->IsVisible() ? "OPENED" : "CLOSED"));
        } else {
            LOG_ERROR("[HOME] Custom ImGui Dev Menu not initialized!");
        }
    }
}

void HandleKeybindingsMenu()
{
    if (Keybindings::IsActionPressed(Keybindings::Action::ToggleKeybindingsMenu)) {
        if (g_DevMenu) {
            g_DevMenu->ToggleKeybindingsWindow();
            std::string keyName = Keybindings::GetKeyName(Keybindings::GetKey(Keybindings::Action::ToggleKeybindingsMenu));
            LOG_VERBOSE("[" << keyName << "] Keybindings Menu " << (g_DevMenu->IsKeybindingsWindowVisible() ? "OPENED" : "CLOSED"));
        } else {
            LOG_ERROR("[K] Keybindings Menu not available - DevMenu not initialized!");
        }
    }
}
// Helper function to check if game window has focus
static bool IsGameWindowFocused()
{
    HWND foregroundWindow = GetForegroundWindow();
    if (!foregroundWindow) {
        return false;
    }
    
    // Get the process ID of the foreground window
    DWORD foregroundPID = 0;
    GetWindowThreadProcessId(foregroundWindow, &foregroundPID);
    
    // Compare with our own process ID
    return (foregroundPID == GetCurrentProcessId());
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
        // Only process keypresses when the game window is focused
        if (!IsGameWindowFocused()) {
            Sleep(125);
            continue;
        }
        
        if (KeyPress(VK_END)) {
            LOG_INFO("[END] Shutting down and unloading...");
            
            // First, trigger shutdown which will clean up everything
            ShutdownPayload();
            
            // Now that shutdown is complete, schedule the DLL unload
            // We need to do this on a separate thread because we can't unload ourselves
            if (g_hModule != NULL) {
                LOG_VERBOSE("[TFPayload] Scheduling DLL unload...");
                
                // Create a thread that will unload the DLL
                HANDLE hUnloadThread = CreateThread(NULL, 0, [](LPVOID param) -> DWORD {
                    HMODULE hMod = (HMODULE)param;

                    LOG_INFO("[TFPayload] Unloaded DLL");
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

        HandleF5();
        HandleF6();
        HandleF12();
        HandleInsert();
        HandleDelete();
        HandleDumpTweakables();
        HandleT();
        HandleL();
        HandleInstantFinish();
        
        HandleToggleVerbose();
        HandleShowHelp();
        HandleClearConsole();
        HandleHomeImGuiMenu();
        HandleKeybindingsMenu();
        
        LeaderboardScanner::CheckHotkey();
        LeaderboardDirect::CheckHotkey();
        Pause::CheckHotkey();
        Respawn::CheckHotkey();
        Camera::CheckHotkey();
        Multiplayer::CheckHotkey();
        BikeSwap::CheckHotkey();
        
        Sleep(80);
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
