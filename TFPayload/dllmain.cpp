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
#include "limits.h"
#include "camera.h"
#include "multiplayer.h"
#include "keybindings.h"
#include "acorns.h"
#include "money.h"
#include "host-join.h"
#include "base-address.h"
#include "prevent-finish.h"
#include "gamemode.h"
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
    HostJoin::Shutdown();
    BikeSwap::Shutdown();
    Keybindings::Shutdown();

    LOG_VERBOSE("[Main] All resources cleaned up.");
}

// C-style function for safe DevMenuSync call (no C++ objects with destructors)
static bool TrySyncFromGame() {
    __try {
        DevMenuSync::SyncFromGame();
        return true;  // Success
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;  // Failed
    }
}

// Global crash tracking
static volatile bool g_inCriticalSection = false;
static volatile const char* g_lastModule = nullptr;

// SEH wrapper for initialization calls - Pure C version to avoid C2712
typedef bool (*InitFunc)(void* userData);

// Pure C logging functions (no C++ objects)
static void LogCrash(const char* moduleName, DWORD exceptionCode) {
    char buffer[256];
    sprintf_s(buffer, sizeof(buffer), "[CRASH] %s crashed with exception 0x%08X\n", moduleName, exceptionCode);
    OutputDebugStringA(buffer);
    printf("%s", buffer);
}

static void LogInitFailed(const char* moduleName) {
    char buffer[256];
    sprintf_s(buffer, sizeof(buffer), "[INIT FAILED] %s returned false\n", moduleName);
    OutputDebugStringA(buffer);
    printf("%s", buffer);
}

// Log init start immediately to crash trace file
static void LogInitStart(const char* moduleName) {
    char buffer[256];
    sprintf_s(buffer, sizeof(buffer), "[INIT START] %s", moduleName);
    Logging::WriteImmediate(buffer);
}

static void LogInitEnd(const char* moduleName, bool success) {
    char buffer[256];
    sprintf_s(buffer, sizeof(buffer), "[INIT END] %s - %s", moduleName, success ? "OK" : "FAILED");
    Logging::WriteImmediate(buffer);
}

static bool SafeInitCall(const char* moduleName, InitFunc func, void* userData) {
    // Log BEFORE attempting init - this will be the last line if we crash
    LogInitStart(moduleName);
    
    g_lastModule = moduleName;
    g_inCriticalSection = true;
    
    bool result = false;
    bool crashed = false;
    DWORD exceptionCode = 0;
    
    __try {
        result = func(userData);
        g_inCriticalSection = false;
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        exceptionCode = GetExceptionCode();
        crashed = true;
        g_inCriticalSection = false;
    }
    
    // Log AFTER __try block using pure C functions
    if (crashed) {
        LogCrash(moduleName, exceptionCode);
        return false;
    } else if (!result) {
        LogInitFailed(moduleName);
    }
    
    // Log successful completion
    LogInitEnd(moduleName, result);
    
    return result;
}

// Helper struct to pass baseAddress to init functions
struct InitContext {
    DWORD_PTR baseAddress;
};

// C-style init functions for SafeInitCall (to avoid C2712 error with lambdas)
static bool Init_Tracks(void* userData) {
    Tracks::SetLoggingEnabled(true);
    Tracks::SetUpdateCallback(OnTrackUpdate);
    if (Tracks::Initialize()) {
        LOG_VERBOSE("[TFPayload] Track metadata hook initialized successfully!");
        if (!Tracks::LoadSearchTerms("F:/search_terms.txt")) {
            LOG_VERBOSE("[TFPayload] Could not load F:/search_terms.txt, using default search terms");
        }
        return true;
    }
    return false;
}

static bool Init_LeaderboardScanner(void* userData) {
    InitContext* ctx = (InitContext*)userData;
    LeaderboardScanner::Initialize(ctx->baseAddress);
    LOG_VERBOSE("[TFPayload] Leaderboard scanner initialized");
    return true;
}

static bool Init_LeaderboardDirect(void* userData) {
    InitContext* ctx = (InitContext*)userData;
    LeaderboardDirect::Initialize(ctx->baseAddress);
    LOG_VERBOSE("[TFPayload] Leaderboard direct initialized");
    return true;
}

static bool Init_Pause(void* userData) {
    InitContext* ctx = (InitContext*)userData;
    Pause::Initialize(ctx->baseAddress);
    LOG_VERBOSE("[TFPayload] Pause system initialized");
    return true;
}

static bool Init_ActionScript(void* userData) {
    InitContext* ctx = (InitContext*)userData;
    if (ActionScript::Initialize(ctx->baseAddress)) {
        LOG_VERBOSE("[TFPayload] ActionScript messaging system initialized");
        return true;
    }
    return false;
}

static bool Init_Respawn(void* userData) {
    InitContext* ctx = (InitContext*)userData;
    return Respawn::Initialize(ctx->baseAddress);
}

static bool Init_Camera(void* userData) {
    InitContext* ctx = (InitContext*)userData;
    return Camera::Initialize(ctx->baseAddress);
}

static bool Init_Multiplayer(void* userData) {
    InitContext* ctx = (InitContext*)userData;
    return Multiplayer::Initialize(ctx->baseAddress);
}

static bool Init_HostJoin(void* userData) {
    InitContext* ctx = (InitContext*)userData;
    return HostJoin::Initialize(ctx->baseAddress);
}

static bool Init_Acorns(void* userData) {
    InitContext* ctx = (InitContext*)userData;
    return Acorns::Initialize(ctx->baseAddress);
}

static bool Init_Money(void* userData) {
    InitContext* ctx = (InitContext*)userData;
    return Money::Initialize(ctx->baseAddress);
}

static bool Init_PreventFinish(void* userData) {
    PreventFinish::Initialize();
    LOG_VERBOSE("[TFPayload] Prevent-finish system initialized");
    return true;
}

static bool Init_GameMode(void* userData) {
    InitContext* ctx = (InitContext*)userData;
    return GameMode::Initialize(ctx->baseAddress);
}

static bool Init_BikeSwap(void* userData) {
    InitContext* ctx = (InitContext*)userData;
    return BikeSwap::Initialize(ctx->baseAddress);
}

static bool Init_Logging(void* userData) {
    Logging::Initialize();
    return true;
}

static bool Init_Keybindings(void* userData) {
    Keybindings::Initialize();
    return true;
}

static bool Init_Rendering(void* userData) {
    if (!Rendering::Initialize()) {
        LOG_ERROR("[TFPayload] Failed to initialize rendering system!");
        LOG_WARNING("[TFPayload] Dev menu will not be available");
        return false;
    }
    LOG_VERBOSE("[TFPayload] Rendering system connected to ProxyDLL");
    return true;
}

static bool Init_DevMenu(void* userData) {
    g_DevMenu = new DevMenu();
    g_DevMenu->Initialize();
    LOG_VERBOSE("[TFPayload] ImGui Dev Menu initialized");
    return true;
}

static bool Init_DevMenuSync(void* userData) {
    InitContext* ctx = (InitContext*)userData;
    if (!DevMenuSync::Initialize(ctx->baseAddress)) {
        LOG_ERROR("[TFPayload] Failed to initialize DevMenu sync!");
        LOG_WARNING("[TFPayload] Changes in menu will not affect game!");
        return false;
    }
    
    LOG_VERBOSE("[TFPayload] DevMenuSync initialized");
    
    // SAFETY: Call sync in separate C-style function with exception handling
    if (TrySyncFromGame()) {
        LOG_VERBOSE("[TFPayload] Initial sync complete");
    } else {
        LOG_WARNING("[TFPayload] Initial DevMenu sync failed (game not ready yet)");
        LOG_WARNING("[TFPayload] Sync will retry when menu opens");
    }
    
    LOG_VERBOSE("[TFPayload] Dev Menu ready! (Press HOME to toggle)");
    return true;
}

// Helper to attach to existing console
void AttachToExistingConsole()
{
    // Re-attach stdout/stderr to the existing console
    FILE* fDummy;
    freopen_s(&fDummy, "CONOUT$", "w", stdout);
    freopen_s(&fDummy, "CONOUT$", "w", stderr);
    freopen_s(&fDummy, "CONIN$", "r", stdin);
    
    // Clear error state and sync with C++ streams
    std::cout.clear();
    std::cerr.clear();
    std::cin.clear();
}

// PAYLOAD INITIALIZATION (called when hot-loaded)
extern "C" __declspec(dllexport) void PayloadInit()
{
    // FIRST: Attach to the existing console created by ProxyDLL
    AttachToExistingConsole();
    
    // Write crash trace IMMEDIATELY - this works even before Logging::Initialize()
    Logging::WriteImmediate("[STARTUP] PayloadInit() entered");
    
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
    
    // Log all MinHook hooks for debugging conflicts
    FILE* hookLog = nullptr;
    fopen_s(&hookLog, "tfpayload_hooks.log", "w");
    if (hookLog) {
        fprintf(hookLog, "TFPayload Hook Addresses (MinHook)\n");
        fprintf(hookLog, "=====================================\n\n");
        fprintf(hookLog, "NOTE: These hooks will be installed as modules initialize below.\n");
        fprintf(hookLog, "Check this file again after initialization completes.\n\n");
        fclose(hookLog);
    }

    const wchar_t* processName = L"trials_fusion.exe";
    const wchar_t* moduleName = L"trials_fusion.exe";
    
    DWORD_PTR rawBaseAddress = GetModuleBaseAddress(GetProcessID(processName), moduleName);
    if (rawBaseAddress == 0) {
        LOG_ERROR("[TFPayload] Could not retrieve base address!");
        return;
    }

    // NOTE: Don't use GetCorrectedBaseAddress - it incorrectly adds 0x8192 offset
    // The RVA constants already handle version differences
    DWORD_PTR baseAddress = rawBaseAddress;
    
    // Detect and log game version
    bool isSteam = BaseAddress::IsSteamVersion();
    Logging::WriteImmediate(isSteam ? "[VERSION] Steam version detected" : "[VERSION] Uplay version detected");
    LOG_VERBOSE("[TFPayload] Game Base Address: 0x" << std::hex << baseAddress << std::dec);
    LOG_VERBOSE("[TFPayload] Version: " << (isSteam ? "Steam" : "Uplay"));

    // Initialize logging FIRST so crash tracing works
    SafeInitCall("Logging", Init_Logging, nullptr);
    
    LOG_VERBOSE("[TFPayload] Beginning module initialization with crash protection...");
    
    // Create context for passing baseAddress to init functions
    InitContext ctx;
    ctx.baseAddress = baseAddress;
    
    SafeInitCall("Tracks", Init_Tracks, nullptr);
    SafeInitCall("LeaderboardScanner", Init_LeaderboardScanner, &ctx);
    SafeInitCall("LeaderboardDirect", Init_LeaderboardDirect, &ctx);
    SafeInitCall("Pause", Init_Pause, &ctx);
    SafeInitCall("ActionScript", Init_ActionScript, &ctx);
    SafeInitCall("Respawn", Init_Respawn, &ctx);
    SafeInitCall("Camera", Init_Camera, &ctx);
    SafeInitCall("Multiplayer", Init_Multiplayer, &ctx);
    SafeInitCall("HostJoin", Init_HostJoin, &ctx);
    SafeInitCall("Acorns", Init_Acorns, &ctx);
    SafeInitCall("Money", Init_Money, &ctx);
    SafeInitCall("PreventFinish", Init_PreventFinish, nullptr);
    SafeInitCall("GameMode", Init_GameMode, &ctx);
    SafeInitCall("BikeSwap", Init_BikeSwap, &ctx);
    SafeInitCall("Keybindings", Init_Keybindings, nullptr);

    // Wait a moment to ensure ProxyDLL has hooked D3D11
    LOG_VERBOSE("[TFPayload] Waiting for ProxyDLL to initialize D3D11...");
    Sleep(400);

    // Initialize rendering system (connects to ProxyDLL's hook)
    SafeInitCall("Rendering", Init_Rendering, nullptr);
    
    // Initialize DevMenu with crash protection
    SafeInitCall("DevMenu", Init_DevMenu, nullptr);
    
    // Initialize DevMenuSync with crash protection
    SafeInitCall("DevMenuSync", Init_DevMenuSync, &ctx);

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
    LOG_INFO("\tHelp Commands");
    LOG_INFO("\t" << ClearConsoleKey << "\t\t\t- Clear debug console");
    LOG_INFO("\t" << ShowHelpTextKey << "\t\t\t- Show this help text");
    LOG_INFO("\t" << ToggleVerboseLoggingKey << "\t\t\t- Toggle verbose logging (ON/OFF)");
    LOG_INFO("\tEND\t\t\t- Shutdown and unload TFPayload.dll(automatic)");
    LOG_INFO("\tF1\t\t\t- Reload TFPayload.dll (load/unload toggle)");
    LOG_INFO("\t" << ToggleDevMenuKey << "\t\t\t- Open DevMenu");
    LOG_INFO("\tK\t\t\t- Open Keybindings Menu");
    LOG_INFO("");
    LOG_INFO("\tTrack Functions");
    LOG_INFO("\t" << InstantFinishKey << "\t\t\t- Instant Pass Track");
    LOG_INFO("\t" << CycleHUDKey << "\t\t\t- Cycle HUD Visibility in Track/Replay");
    LOG_INFO("\t" << TogglePauseKey << "\t\t\t- Toggle time/physics ON/OFF");
    LOG_INFO("\t" << Add60SecondsKey << "\t\t\t- Add 1 Minute to Timer");
    LOG_INFO("\t" << Subtract60SecondsKey << "\t\t\t- Subtract 1 Minute from Timer");
    LOG_INFO("\t" << Add10MinuteKey << "\t\t\t- Add 10 minutes to Timer");
    LOG_INFO("\t" << Add100FaultsKey << "\t\t\t- Add 100 faults to fault-counter");
    LOG_INFO("\t" << Subtract100FaultsKey << "\t\t\t- Subtract 100 faults from fault-counter");
    LOG_INFO("\t" << ResetFaultsKey << "\t\t\t- Reset Faults to 0");
    LOG_INFO("\t" << IncrementFaultKey << "\t\t\t- Add 1 fault to fault-counter");
    LOG_INFO("\t" << ToggleLimitValidationKey << "\t\t\t- Toggle Fault/Time Limits");
    LOG_INFO("");
    LOG_INFO("\tLeaderboard");
    LOG_INFO("\t" << ScanLeaderboardByIDKey << "\t\t\t- Scan current leaderboard (friends/overall/myscore) for single track by ID (default: 221120)");
    LOG_INFO("\t" << ScanCurrentLeaderboardKey << "\t\t\t- Scan current open leaderboard");
    LOG_INFO("\t" << TestFetchTrackIDKey << "\t\t\t- Test fetch track ID 221120");
    LOG_INFO("");
    LOG_INFO("\tTrack Central");
    LOG_INFO("\t" << StartAutoScrollKey << "\t\t\t- Start auto-scroll search-pages");
    LOG_INFO("\t" << KillswitchKey << "\t\t\t- KILLSWITCH (Emergency stop ALL operations)");
    LOG_INFO("\t" << CycleSearchKey << "\t\t\t- Cycle through searches: Ninja -> Mountain -> Speed");
    LOG_INFO("\t" << DecreaseScrollDelayKey << "\t\t\t- Decrease scroll delay (-200ms)");
    LOG_INFO("\t" << IncreaseScrollDelayKey << "\t\t\t- Increase scroll delay (+200ms)");
    LOG_VERBOSE("  NOTE: All tracks auto-saved to datapack/tracks_data.csv");
    LOG_INFO("");
    LOG_INFO("\tCheckpoints");
    LOG_INFO("\t" << RespawnAtCheckpointKey << "\t\t\t- Respawn at current checkpoint");
    LOG_INFO("\t" << RespawnNextCheckpointKey << "\t\t\t- Respawn at next checkpoint");
    LOG_INFO("\t" << RespawnPrevCheckpointKey << "\t\t\t- Respawn at previous checkpoint");
    LOG_INFO("\t" << RespawnForward5Key << "\t\t\t- Respawn 5 checkpoints ahead");
    LOG_INFO("");
    LOG_INFO("\tMultiplayer(Phase 1)");
    LOG_INFO("\t" << SaveMultiplayerLogsKey << "\t\t\t- Save all multiplayer logs (sessions, packets, stats)");
    LOG_INFO("\t" << CaptureSessionStateKey << "\t\t\t- Capture current session state");
    LOG_VERBOSE("Logs: datapack/mp_session_log.txt, datapack/mp_sessions.csv, datapack/mp_packets.csv");
    LOG_INFO("");
    LOG_INFO("\tDev Menu");
    LOG_INFO("\t" << DumpTweakablesKey << "\t\t\t- Dump tweakables data (see what's available)");
    LOG_INFO("");
    LOG_INFO("\tActionscript Commands");
    LOG_INFO("\t" << FullCountdownSequenceKey << "\t\t\t- Full countdown sequence (3, 2, 1, GO, Ready!) with auto-timing");
    LOG_INFO("\t" << ToggleLoadScreen << "\t\t\t- Toggle loading screen");
    LOG_INFO("");
    LOG_INFO("\tBIKE SWAP (Runtime Bike Changing)");
    LOG_INFO("\t" << Keybindings::GetKeyName(Keybindings::GetKey(Keybindings::Action::SwapPrevBike)) << "\t\t\t- Cycle to previous bike");
    LOG_INFO("\t" << Keybindings::GetKeyName(Keybindings::GetKey(Keybindings::Action::SwapNextBike)) << "\t\t\t- Cycle to next bike");
    LOG_INFO("\t" << Keybindings::GetKeyName(Keybindings::GetKey(Keybindings::Action::DebugBikeInfo)) << "\t\t\t- Debug dump bike state");
    LOG_INFO("");
    LOG_VERBOSE("Results: datapack/tracks_data.csv, datapack/leaderboard_scans.txt & feed_data.csv");
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
        LOG_VERBOSE("[" << keyName << "] Instant Finish button pressed - calling SafeInstantFinish...");
        PreventFinish::SafeInstantFinish();
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
        std::string keyName = Keybindings::GetKeyName(Keybindings::GetKey(Keybindings::Action::ToggleDevMenu));
        LOG_VERBOSE("[" << keyName << "] Toggle DevMenu key pressed!");
        if (g_DevMenu) {
            g_DevMenu->Toggle();
            LOG_VERBOSE("[" << keyName << "] Custom ImGui Dev Menu " << (g_DevMenu->IsVisible() ? "OPENED" : "CLOSED"));
        } else {
            LOG_ERROR("[" << keyName << "] Custom ImGui Dev Menu not initialized!");
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

void HandleDebugGameState()
{
    if (Keybindings::IsActionPressed(Keybindings::Action::DebugGameState)) {
        GameMode::DebugPrintState();
    }
}

void HandleToggleLimitValidation()
{
    if (Keybindings::IsActionPressed(Keybindings::Action::ToggleLimitValidation)) {
        std::string keyName = Keybindings::GetKeyName(Keybindings::GetKey(Keybindings::Action::ToggleLimitValidation));
        
        // Check current state (both should be in sync)
        bool faultDisabled = Limits::IsFaultValidationDisabled();
        bool timeDisabled = Limits::IsTimeValidationDisabled();
        
        if (faultDisabled && timeDisabled) {
            // Re-enable both limits
            LOG_INFO("");
            LOG_INFO("[" << keyName << "] === ENABLING FAULT/TIME LIMITS ===");
            if (Limits::EnableAllLimitValidation()) {
                LOG_INFO("[" << keyName << "] Fault/Time limits are now ACTIVE");
            } else {
                LOG_ERROR("[" << keyName << "] Failed to enable limit validation");
            }
            LOG_INFO("");
        } else {
            // Disable both limits
            LOG_INFO("");
            LOG_INFO("[" << keyName << "] === DISABLING FAULT/TIME LIMITS ===");
            if (Limits::DisableAllLimitValidation()) {
                LOG_INFO("[" << keyName << "] Fault/Time limits are now BYPASSED");
            } else {
                LOG_ERROR("[" << keyName << "] Failed to disable limit validation");
            }
            LOG_INFO("");
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
        HandleDebugGameState();
        HandleToggleLimitValidation();
        
        LeaderboardScanner::CheckHotkey();
        LeaderboardDirect::CheckHotkey();
        Pause::CheckHotkey();
        Respawn::CheckHotkey();
        Camera::CheckHotkey();
        Multiplayer::CheckHotkey();
        BikeSwap::CheckHotkey();
        PreventFinish::Update();
        
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
