#include "pch.h"
#include "prevent-finish.h"
#include "respawn.h"
#include "limits.h"
#include "actionscript.h"
#include "Logging.h"
#include "base-address.h"
#include "gamemode.h"
#include "devMenuSync.h"
#include <MinHook.h>
#include <TlHelp32.h>
#include <unordered_map>

namespace PreventFinish {
    static bool g_enabled = false;
    static bool g_finishBlocked = false;
    static bool g_faultOutTriggered = false;
    static int g_lastSeenCheckpoint = -1;
    static int g_initialFaultCount = 0;
    static int g_initialTimeMs = 0;
    static int g_lastFaultCount = 0;
    static int g_lastTimeMs = 0;
    static bool g_lastResetState = false;  // Track if we were at reset last frame
    static bool g_lastModifiedState = false;  // Track if values were modified last check (prevents spam)
    static bool g_runTainted = false;  // Track if values were EVER modified during this run (anti-cheat)
    static bool g_permanentTaint = false;  // Track if checkpoint was skipped or time/faults reduced (can NEVER be cleared by reset button)
    
    // Track WHY the run was blocked (for anti-cheat)
    static bool g_taintedByCheckpointSkip = false;
    static bool g_taintedByFaultReduction = false;
    static bool g_taintedByTimeReduction = false;
    static bool g_taintedByBikeRider = false;
    
    // Runtime captured defaults (captured on track load)
    static bool g_defaultsCaptured = false;
    static std::unordered_map<int, int> g_capturedIntDefaults;
    static std::unordered_map<int, float> g_capturedFloatDefaults;

    // Structure to hold default values for bike/rider parameters
    struct DefaultValue {
        int id;
        int type; // 1=Bool, 2=Int, 3=Float
        union {
            bool boolVal;
            int intVal;
            float floatVal;
        };
    };

    // Helper functions to create DefaultValue entries
    static constexpr DefaultValue MakeBool(int id, bool val) {
        DefaultValue dv = {};
        dv.id = id;
        dv.type = 1;
        dv.boolVal = val;
        return dv;
    }
    
    static constexpr DefaultValue MakeInt(int id, int val) {
        DefaultValue dv = {};
        dv.id = id;
        dv.type = 2;
        dv.intVal = val;
        return dv;
    }
    
    static constexpr DefaultValue MakeFloat(int id, float val) {
        DefaultValue dv = {};
        dv.id = id;
        dv.type = 3;
        dv.floatVal = val;
        return dv;
    }

    // Default values for Bike folder (IDs 169-213)
    static const DefaultValue g_bikeDefaults[] = {
        // Top-level bike parameters
        MakeBool(170, false),    // TuneEnabled
        MakeBool(171, false),    // newAccelerationEnabled
        MakeInt(172, 3),         // IdNumber
        
        // Engine subfolder
        MakeFloat(174, 1.0f),      // AccelerationMultiplier
        MakeFloat(175, 1.0f),      // AccelerationSpeedDivisor
        MakeFloat(176, 0.0107f),   // RpmSeekSpeedMul
        MakeFloat(179, 1980.0f),   // RpmMin
        MakeFloat(186, 8400.0f),   // RpmMax
        MakeFloat(187, 600.0f),    // RpmMaxAdd
        MakeFloat(188, 0.081f),    // RpmMul
        MakeFloat(189, 0.991f),    // RpmAccelCurrent
        MakeFloat(190, 0.975f),    // RpmDecelCurrent
        MakeFloat(191, 0.025f),    // RpmAccelTarget
        MakeFloat(192, 0.7f),      // RpmDecelTarget
        
        // Transmission subfolder
        MakeFloat(178, 0.97f),     // RpmClutch
        MakeFloat(180, 6120.0f),   // RpmShiftDown
        MakeFloat(181, 1.99f),     // RpmGearDiv1
        MakeFloat(182, 2.46f),     // RpmGearDiv2
        MakeFloat(183, 2.56f),     // RpmGearDiv3
        MakeFloat(184, 2.66f),     // RpmGearDiv4
        MakeFloat(185, 3.0f),      // RpmGearDiv5
        MakeFloat(193, 0.97f),     // ShiftLoadReduce
        
        // Properties subfolder
        MakeFloat(195, 28.0f),     // AccelerationPower
        MakeFloat(196, 27.0f),     // AccelerationBrake
        MakeFloat(197, -0.25f),    // AccelerationForce
        MakeFloat(198, 0.0f),      // EngineDamping
        MakeFloat(199, 20.0f),     // MaximumVelocity
        MakeFloat(200, 1.0f),      // MassFactor
        MakeFloat(201, 1.25f),     // BrakePowerFront
        MakeFloat(207, 1.25f),     // BrakePowerBack
        MakeFloat(260, 28.0f),     // AccelerationPowerQuadMP
        MakeFloat(261, 27.0f),     // AccelerationBrakeQuadMP
        MakeFloat(268, 1.0f),      // UnicornWalkMultiplier
        MakeFloat(269, 0.4f),      // UnicornRunMultiplier
        MakeFloat(270, 0.14f),     // UnicornGallopMultiplier
        MakeFloat(271, 42.0f),     // UnicornRunSpeed
        MakeFloat(272, 125.0f),    // UnicornGallopSpeed
        MakeFloat(273, 0.0f),      // UnicornFireInitValue
        
        // Suspension subfolder
        MakeFloat(203, 4000.0f),   // FrontSpringSoftness
        MakeFloat(204, 0.001f),    // FrontSpringDamping
        MakeFloat(205, 0.325f),    // FrontWheelSpringSoftness
        MakeFloat(206, 0.2f),      // FrontWheelSpringDamping
        MakeFloat(208, 4000.0f),   // BackSpringSoftness
        MakeFloat(209, 0.001f),    // BackSpringDamping
        MakeFloat(210, 0.2f),      // BackWheelSpringSoftness
        MakeFloat(211, 0.3f),      // BackWheelSpringDamping
        MakeFloat(212, 0.0f),      // BackWheelSpring2Softness
        MakeFloat(213, 1.0f),      // BackWheelSpring2Damping
    };

    // Default values for Rider folder (IDs 214-217)
    static const DefaultValue g_riderDefaults[] = {
        MakeBool(215, false),    // TuneEnabled
        MakeFloat(217, 1.0f),    // MassFactor
    };

    static constexpr int g_bikeDefaultsCount = sizeof(g_bikeDefaults) / sizeof(DefaultValue);
    static constexpr int g_riderDefaultsCount = sizeof(g_riderDefaults) / sizeof(DefaultValue);

    typedef void* (__thiscall* InitializeObjectStructFunc)(void* obj, int p1, int p2, int p3, int p4);
    static InitializeObjectStructFunc g_OriginalInitObjectStruct = nullptr;
    static uintptr_t g_baseAddress = 0;

    static constexpr int MESSAGE_TYPE_FINISH = 0x00000018;
    static constexpr int MESSAGE_TYPE_TIMEOUT = 1;
    static constexpr int MESSAGE_TYPE_FAULTOUT = 2;

    static DWORD_PTR GetModuleBaseAddress(DWORD processID, const wchar_t* moduleName) {
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

    void* __fastcall Hook_InitObjectStruct(void* obj, void* edx, int p1, int p2, int p3, int p4) {
        static int s_callCount = 0;
        s_callCount++;

        if (g_enabled && g_finishBlocked) {
            const char* messageType = "UNKNOWN";
            if (p1 == 1) {
                if (p2 == 0) messageType = "FINISH (p2=0)";
                else if (p2 == 1) messageType = "TIMEOUT";
                else if (p2 == 2) messageType = "FAULTOUT";
                else if (p2 == MESSAGE_TYPE_FINISH) messageType = "FINISH (p2=0x18)";
            }

            LOG_VERBOSE("[PreventFinish] #" << s_callCount
                << " p1=" << p1
                << " p2=0x" << std::hex << p2 << std::dec << " (" << p2 << ")"
                << " p3=" << p3
                << " p4=" << p4
                << " -> " << messageType);
        }

        // FAULT LIMIT BYPASS: If fault validation is disabled, change faultout to normal finish
        bool isFaultOut = (p1 == 1 && p2 == MESSAGE_TYPE_FAULTOUT);
        if (isFaultOut && Limits::IsFaultValidationDisabled()) {
            LOG_VERBOSE("[PreventFinish] *** FAULTOUT BYPASSED (fault validation disabled) ***");
            LOG_VERBOSE("[PreventFinish] Changing FAULTOUT (p2=2) -> NORMAL FINISH (p2=0)");
            
            // Change faultout to normal finish
            void* result = g_OriginalInitObjectStruct(obj, p1, 0, p3, p4);
            
            LOG_VERBOSE("[PreventFinish] Faultout converted to normal finish!");
            return result;
        }

        // Check for ANY finish message:
        // - Normal finish from crossing line: p1=1, p2=0
        // - Instant finish via Respawn::InstantFinish: p1=1, p2=0
        // - Checkpoint skip finish: p1=1, p2=0
        // - Alternative finish variant: p1=1, p2=0x18 (if game uses this)
        bool isFinishMessage = (p1 == 1 && (p2 == 0 || p2 == MESSAGE_TYPE_FINISH));

        if (isFinishMessage && g_enabled && g_finishBlocked) {
            LOG_VERBOSE("[PreventFinish] *** FINISH MESSAGE INTERCEPTED! ***");
            LOG_VERBOSE("[PreventFinish] Source could be: line crossing, instant finish, or checkpoint skip");
            LOG_VERBOSE("[PreventFinish] Changing to FAULTOUT (p2=" << MESSAGE_TYPE_FAULTOUT << ")");

            // Change finish to fault out
            void* result = g_OriginalInitObjectStruct(obj, p1, MESSAGE_TYPE_FAULTOUT, p3, p4);

            LOG_VERBOSE("[PreventFinish] Modified message sent - should fault out!");
            return result;
        }

        return g_OriginalInitObjectStruct(obj, p1, p2, p3, p4);
    }

    void Initialize() {
        g_enabled = false;
        g_finishBlocked = false;
        g_faultOutTriggered = false;
        g_lastSeenCheckpoint = -1;
        g_initialFaultCount = 0;
        g_initialTimeMs = 0;
        g_lastFaultCount = 0;
        g_lastTimeMs = 0;
        g_lastResetState = false;
        g_lastModifiedState = false;
        g_runTainted = false;
        g_permanentTaint = false;
        g_taintedByCheckpointSkip = false;
        g_taintedByFaultReduction = false;
        g_taintedByTimeReduction = false;
        g_taintedByBikeRider = false;

        g_baseAddress = GetModuleBaseAddress(GetCurrentProcessId(), L"trials_fusion.exe");
        if (g_baseAddress == 0) {
            LOG_ERROR("[PreventFinish] Failed to get base address");
            return;
        }

        bool isSteam = BaseAddress::IsSteamVersion();
        uintptr_t initObjectStructRVA = isSteam ? 0x55a70 : 0x55a00;
        uintptr_t initObjectStructAddr = g_baseAddress + initObjectStructRVA;

        LOG_VERBOSE("[PreventFinish] Base: 0x" << std::hex << g_baseAddress);
        LOG_VERBOSE("[PreventFinish] Hook: 0x" << std::hex << initObjectStructAddr);
        LOG_VERBOSE("[PreventFinish] Version: " << (isSteam ? "Steam" : "Uplay"));
        
        // VERIFIED: Steam address 0x55a70 confirmed via CSV mapping (Uplay 0x755a00 -> Steam 0x195a70)
        // Address mapping verified: Uplay Ghidra 0x00755a00 -> Steam Ghidra 0x00195a70
        // Hook should work safely on both versions

        MH_STATUS status = MH_CreateHook(
            (LPVOID)initObjectStructAddr,
            (LPVOID)&Hook_InitObjectStruct,
            (LPVOID*)&g_OriginalInitObjectStruct
        );

        if (status != MH_OK) {
            LOG_ERROR("[PreventFinish] Hook create failed: " << MH_StatusToString(status));
            return;
        }

        status = MH_EnableHook((LPVOID)initObjectStructAddr);
        if (status != MH_OK) {
            LOG_ERROR("[PreventFinish] Hook enable failed: " << MH_StatusToString(status));
            return;
        }

        LOG_VERBOSE("[PreventFinish] Hook installed successfully!");
        LOG_VERBOSE("[PreventFinish] Will intercept: Line crossing, Instant finish (via Respawn::InstantFinish), Checkpoint skips");
    }

    void CaptureDefaults() {
        g_capturedIntDefaults.clear();
        g_capturedFloatDefaults.clear();
        
        LOG_VERBOSE("[PreventFinish] Capturing baseline bike/rider values...");
        
        int capturedCount = 0;
        
        // Capture all bike values
        for (int i = 0; i < g_bikeDefaultsCount; i++) {
            const DefaultValue& def = g_bikeDefaults[i];
            
            if (def.type == 1 || def.type == 2) { // Bool or Int
                int value;
                if (DevMenuSync::ReadValue<int>(def.id, value)) {
                    g_capturedIntDefaults[def.id] = value;
                    capturedCount++;
                }
            }
            else if (def.type == 3) { // Float
                float value;
                if (DevMenuSync::ReadValue<float>(def.id, value)) {
                    g_capturedFloatDefaults[def.id] = value;
                    capturedCount++;
                }
            }
        }
        
        // Capture all rider values
        for (int i = 0; i < g_riderDefaultsCount; i++) {
            const DefaultValue& def = g_riderDefaults[i];
            
            if (def.type == 1) { // Bool
                int value;
                if (DevMenuSync::ReadValue<int>(def.id, value)) {
                    g_capturedIntDefaults[def.id] = value;
                    capturedCount++;
                }
            }
            else if (def.type == 3) { // Float
                float value;
                if (DevMenuSync::ReadValue<float>(def.id, value)) {
                    g_capturedFloatDefaults[def.id] = value;
                    capturedCount++;
                }
            }
        }
        
        g_defaultsCaptured = true;
        LOG_VERBOSE("[PreventFinish] Captured " << capturedCount << " baseline values from game");
    }

    bool IsEnabled() { return g_enabled; }

    void Enable() {
        if (g_enabled) return;
        g_enabled = true;
        g_faultOutTriggered = false;
        g_lastSeenCheckpoint = Respawn::GetCurrentCheckpointIndex();
        g_initialFaultCount = Respawn::GetFaultCount();
        g_initialTimeMs = Respawn::GetRaceTimeMs();
        g_lastFaultCount = g_initialFaultCount;
        g_lastTimeMs = g_initialTimeMs;
        g_runTainted = false;  // Clear taint on enable (new run)
        g_permanentTaint = false;  // Clear permanent taint
        g_taintedByCheckpointSkip = false;
        g_taintedByFaultReduction = false;
        g_taintedByTimeReduction = false;
        g_taintedByBikeRider = false;
        g_lastModifiedState = false;  // Reset to allow first-frame check to log
        g_defaultsCaptured = false;  // Mark that we need to capture defaults
        
        g_finishBlocked = false;
        LOG_VERBOSE("[PreventFinish] *** ENABLED (will capture baseline values on first reset) ***");
        
        LOG_VERBOSE("[PreventFinish] Initial state: CP=" << g_lastSeenCheckpoint 
            << " Faults=" << g_initialFaultCount 
            << " Time=" << g_initialTimeMs << "ms");
        LOG_VERBOSE("[PreventFinish] Finish blocking will activate on: checkpoint skip, fault reduction, time reduction, or modified bike/rider values");
    }

    void Disable() {
        if (!g_enabled) return;
        g_enabled = false;
        g_finishBlocked = false;
        g_faultOutTriggered = false;
        g_lastSeenCheckpoint = -1;
        g_initialFaultCount = 0;
        g_initialTimeMs = 0;
        g_lastFaultCount = 0;
        g_lastTimeMs = 0;
        g_lastResetState = false;
        g_lastModifiedState = false;
        g_runTainted = false;
        g_permanentTaint = false;
        g_taintedByCheckpointSkip = false;
        g_taintedByFaultReduction = false;
        g_taintedByTimeReduction = false;
        g_taintedByBikeRider = false;
        LOG_VERBOSE("[PreventFinish] Disabled");
    }

    void Toggle() {
        if (g_enabled) Disable();
        else Enable();
    }

    void SafeInstantFinish() {
        LOG_VERBOSE("[PreventFinish] SafeInstantFinish() called");

        // In singleplayer with prevent-finish active: instant finish is ALWAYS blocked.
        // The player must physically cross the finish line. This applies regardless
        // of taint state — instant finish keybind/button is simply not allowed in SP.
        if (g_enabled && GameMode::IsSinglePlayerTrack()) {
            LOG_VERBOSE("[PreventFinish] Singleplayer detected - instant finish BLOCKED (must cross finish line)");
            Limits::InstantFaultOut();
            return;
        }

        // Not in singleplayer (multiplayer/editor/etc) or prevent-finish not active:
        // allow instant finish normally.
        LOG_VERBOSE("[PreventFinish] Not in SP or not enabled - calling normal ActionScript finish");
        ActionScript::CallHandleRaceFinish();
    }

    void NotifyCheckpointSkip() {
        if (!g_enabled || g_finishBlocked) return;
        
        LOG_VERBOSE("[PreventFinish] Checkpoint skip detected - BLOCKING finish");
        g_finishBlocked = true;
        g_permanentTaint = true;  // Mark as permanently tainted
        g_taintedByCheckpointSkip = true;
    }

    void NotifyFaultReduction() {
        if (!g_enabled || g_finishBlocked) return;
        
        int currentFaults = Respawn::GetFaultCount();
        LOG_VERBOSE("[PreventFinish] Fault reduction detected (" << currentFaults 
            << " < " << g_lastFaultCount << ") - BLOCKING finish");
        g_finishBlocked = true;
        g_permanentTaint = true;  // Mark as permanently tainted
        g_taintedByFaultReduction = true;
        g_lastFaultCount = currentFaults;
    }

    void NotifyTimeReduction() {
        if (!g_enabled || g_finishBlocked) return;
        
        int currentTime = Respawn::GetRaceTimeMs();
        LOG_VERBOSE("[PreventFinish] Time reduction detected (" << currentTime 
            << "ms < " << g_lastTimeMs << "ms) - BLOCKING finish");
        g_finishBlocked = true;
        g_permanentTaint = true;  // Mark as permanently tainted
        g_taintedByTimeReduction = true;
        g_lastTimeMs = currentTime;
    }

    void NotifyBikeModification() {
        if (!g_enabled || g_finishBlocked) return;
        
        // Check if we're in multiplayer - only block in singleplayer
        if (GameMode::IsInMultiplayerMode()) return;
        
        LOG_VERBOSE("[PreventFinish] Bike value modified in singleplayer - BLOCKING finish");
        g_finishBlocked = true;
        g_runTainted = true;  // Mark run as tainted
        g_taintedByBikeRider = true;
    }

    void NotifyRiderModification() {
        if (!g_enabled || g_finishBlocked) return;
        
        // Check if we're in multiplayer - only block in singleplayer
        if (GameMode::IsInMultiplayerMode()) return;
        
        LOG_VERBOSE("[PreventFinish] Rider value modified in singleplayer - BLOCKING finish");
        g_finishBlocked = true;
        g_runTainted = true;  // Mark run as tainted
        g_taintedByBikeRider = true;
    }

    void Update() {
        // Auto-enable/disable based on game mode
        bool isPlaying = GameMode::IsPlaying();
        bool isInEditor = GameMode::IsInEditor();
        
        if (isPlaying && !g_enabled) {
            LOG_VERBOSE("[PreventFinish] Auto-enabled (IsPlaying)");
            Enable();
        }
        else if (isInEditor && g_enabled) {
            LOG_VERBOSE("[PreventFinish] Auto-disabled (IsInEditor)");
            Disable();
        }

        if (!g_enabled) return;

        int checkpointCount = Respawn::GetCheckpointCount();
        if (checkpointCount <= 0) {
            g_faultOutTriggered = false;
            g_lastSeenCheckpoint = -1;
            return;
        }

        int currentCheckpoint = Respawn::GetCurrentCheckpointIndex();
        int currentFaults = Respawn::GetFaultCount();
        int currentTime = Respawn::GetRaceTimeMs();

        // Check for reset condition: back to checkpoint 0 with 0 faults
        bool isAtReset = (currentCheckpoint == 0 && currentFaults == 0);
        
        // Detect restart: either transition to reset state OR time goes back to 0
        bool justRestarted = (isAtReset && !g_lastResetState) || (isAtReset && currentTime == 0 && g_lastTimeMs > 0);
        
        // CRITICAL: Clear taint when player resets to CP1
        // This happens on: transition to CP1, or when time resets to 0 at CP1
        if (justRestarted && (g_runTainted || g_permanentTaint)) {
            LOG_VERBOSE("[PreventFinish] *** RESTART DETECTED - CLEARING TAINTS ***");
            g_runTainted = false;
            g_permanentTaint = false;
            g_taintedByCheckpointSkip = false;
            g_taintedByFaultReduction = false;
            g_taintedByTimeReduction = false;
            g_taintedByBikeRider = false;
            g_finishBlocked = false;  // Also clear the block since we're starting fresh
            g_faultOutTriggered = false;
            g_initialFaultCount = 0;
            g_initialTimeMs = currentTime;
            g_lastFaultCount = 0;
            g_lastTimeMs = currentTime;
            LOG_VERBOSE("[PreventFinish] Run is now CLEAN - you can finish if you don't cheat");
        }
        
        // Check bike/rider values every frame when at reset
        if (isAtReset) {
            bool valuesAreModified = CheckForModifiedValues();
            
            // If values are modified, mark run as tainted (but NOT permanently - can be fixed with reset button)
            if (valuesAreModified && !g_taintedByBikeRider) {
                LOG_VERBOSE("[PreventFinish] Bike/rider values modified - run is now TAINTED");
                g_runTainted = true;
                g_taintedByBikeRider = true;
                g_finishBlocked = true;
            }
            
            // If run has permanent taint (checkpoint skip, fault/time reduction), finishing stays blocked
            if (g_permanentTaint) {
                if (!g_finishBlocked) {
                    LOG_VERBOSE("[PreventFinish] Run has PERMANENT TAINT - finish remains BLOCKED (anti-cheat)");
                    g_finishBlocked = true;
                }
            }
            // If run only has bike/rider taint and values are now at defaults, allow finishing
            else if (g_taintedByBikeRider && !valuesAreModified && g_finishBlocked) {
                LOG_VERBOSE("[PreventFinish] Manual reset detected - bike/rider values now at defaults, RE-ENABLING finish");
                g_finishBlocked = false;
                g_runTainted = false;
                g_taintedByBikeRider = false;
                g_faultOutTriggered = false;
                g_initialFaultCount = 0;
                g_initialTimeMs = currentTime;
                g_lastFaultCount = 0;
                g_lastTimeMs = currentTime;
            }
        }
        // NOTE: The "transition into reset" bike/rider check that was here is
        // dead code — isAtReset==true always hits the if(isAtReset) branch above,
        // which already does the valuesAreModified && !g_taintedByBikeRider check.
        // Removed to avoid confusion.
        
        g_lastResetState = isAtReset;

        // Monitor for automatic fault reduction (not already blocked, and NOT at reset)
        if (!g_finishBlocked && !isAtReset && currentFaults < g_lastFaultCount) {
            NotifyFaultReduction();
        }

        // Monitor for automatic time reduction (not already blocked, and NOT at reset)
        if (!g_finishBlocked && !isAtReset && currentTime < g_lastTimeMs && g_lastTimeMs > 0) {
            NotifyTimeReduction();
        }

        // Update tracked values
        g_lastFaultCount = currentFaults;
        g_lastTimeMs = currentTime;

        static int s_debugLastCp = -999;
        if (currentCheckpoint != s_debugLastCp) {
            LOG_VERBOSE("[PreventFinish] CP: " << s_debugLastCp << " -> " << currentCheckpoint 
                << " (Faults=" << currentFaults << ", Blocked=" << (g_finishBlocked ? "YES" : "NO") << ")");
            s_debugLastCp = currentCheckpoint;
        }

        const int finishLineIndex = 0;
        bool justCrossedFinish = (currentCheckpoint == finishLineIndex) && (g_lastSeenCheckpoint > 0);

        if (justCrossedFinish && !g_faultOutTriggered && g_finishBlocked) {
            LOG_VERBOSE("[PreventFinish] Finish line crossed! (was CP" << g_lastSeenCheckpoint << ")");
            LOG_VERBOSE("[PreventFinish] Hook will intercept the finish message...");
            g_faultOutTriggered = true;
        }

        if (currentCheckpoint != finishLineIndex && g_faultOutTriggered) {
            g_faultOutTriggered = false;
        }

        g_lastSeenCheckpoint = currentCheckpoint;
    }

    const char* GetStatusString() {
        if (!g_enabled) {
            return "Prevent-Finish: OFF";
        }
        if (g_permanentTaint) {
            return "Finishing: BLOCKED (PERMANENT - Reset to CP1)";
        }
        if (g_runTainted) {
            return "Finishing: BLOCKED (TAINTED - Reset Values)";
        }
        if (g_finishBlocked) {
            return "Finishing: BLOCKED";
        }
        return "Finishing: ALLOWED (monitoring)";
    }

    bool CheckForModifiedValues() {
        // Only check in singleplayer
        if (GameMode::IsInMultiplayerMode()) {
            return false;
        }

        // If we haven't captured defaults yet, capture them now
        if (!g_defaultsCaptured) {
            CaptureDefaults();
            // After capturing, nothing is "modified" yet
            return false;
        }

        int modifiedCount = 0;
        
        // Check bike values against captured defaults
        for (int i = 0; i < g_bikeDefaultsCount; i++) {
            const DefaultValue& def = g_bikeDefaults[i];
            
            if (def.type == 1 || def.type == 2) { // Bool or Int
                int currentValue;
                if (DevMenuSync::ReadValue<int>(def.id, currentValue)) {
                    auto it = g_capturedIntDefaults.find(def.id);
                    if (it != g_capturedIntDefaults.end()) {
                        if (currentValue != it->second) {
                            modifiedCount++;
                        }
                    }
                }
            }
            else if (def.type == 3) { // Float
                float currentValue;
                if (DevMenuSync::ReadValue<float>(def.id, currentValue)) {
                    auto it = g_capturedFloatDefaults.find(def.id);
                    if (it != g_capturedFloatDefaults.end()) {
                        // Use epsilon comparison for floats
                        const float epsilon = 0.0001f;
                        if (std::abs(currentValue - it->second) > epsilon) {
                            modifiedCount++;
                        }
                    }
                }
            }
        }
        
        // Check rider values against captured defaults
        for (int i = 0; i < g_riderDefaultsCount; i++) {
            const DefaultValue& def = g_riderDefaults[i];
            
            if (def.type == 1) { // Bool
                int currentValue;
                if (DevMenuSync::ReadValue<int>(def.id, currentValue)) {
                    auto it = g_capturedIntDefaults.find(def.id);
                    if (it != g_capturedIntDefaults.end()) {
                        if (currentValue != it->second) {
                            modifiedCount++;
                        }
                    }
                }
            }
            else if (def.type == 3) { // Float
                float currentValue;
                if (DevMenuSync::ReadValue<float>(def.id, currentValue)) {
                    auto it = g_capturedFloatDefaults.find(def.id);
                    if (it != g_capturedFloatDefaults.end()) {
                        const float epsilon = 0.0001f;
                        if (std::abs(currentValue - it->second) > epsilon) {
                            modifiedCount++;
                        }
                    }
                }
            }
        }
        
        bool isModified = (modifiedCount > 0);
        
        // Only log when the state CHANGES (prevents spam)
        if (isModified != g_lastModifiedState) {
            if (isModified) {
                LOG_VERBOSE("[PreventFinish] Found " << modifiedCount << " modified bike/rider values!");
                
                // Log detailed info about each modified value (only on state change)
                for (int i = 0; i < g_bikeDefaultsCount; i++) {
                    const DefaultValue& def = g_bikeDefaults[i];
                    
                    if (def.type == 1 || def.type == 2) { // Bool or Int
                        int currentValue;
                        if (DevMenuSync::ReadValue<int>(def.id, currentValue)) {
                            auto it = g_capturedIntDefaults.find(def.id);
                            if (it != g_capturedIntDefaults.end() && currentValue != it->second) {
                                const char* typeName = (def.type == 1) ? "BOOL" : "INT";
                                if (def.type == 1) {
                                    LOG_VERBOSE("[PreventFinish] [MODIFIED] ID=" << def.id 
                                        << " (" << typeName << ") Current=" << (currentValue ? "true" : "false")
                                        << " Baseline=" << (it->second ? "true" : "false"));
                                } else {
                                    LOG_VERBOSE("[PreventFinish] [MODIFIED] ID=" << def.id 
                                        << " (" << typeName << ") Current=" << currentValue
                                        << " Baseline=" << it->second);
                                }
                            }
                        }
                    }
                    else if (def.type == 3) { // Float
                        float currentValue;
                        if (DevMenuSync::ReadValue<float>(def.id, currentValue)) {
                            auto it = g_capturedFloatDefaults.find(def.id);
                            if (it != g_capturedFloatDefaults.end()) {
                                const float epsilon = 0.0001f;
                                if (std::abs(currentValue - it->second) > epsilon) {
                                    LOG_VERBOSE("[PreventFinish] [MODIFIED] ID=" << def.id 
                                        << " (FLOAT) Current=" << currentValue
                                        << " Baseline=" << it->second
                                        << " Diff=" << std::abs(currentValue - it->second));
                                }
                            }
                        }
                    }
                }
                
                // Check rider values
                for (int i = 0; i < g_riderDefaultsCount; i++) {
                    const DefaultValue& def = g_riderDefaults[i];
                    
                    if (def.type == 1) { // Bool
                        int currentValue;
                        if (DevMenuSync::ReadValue<int>(def.id, currentValue)) {
                            auto it = g_capturedIntDefaults.find(def.id);
                            if (it != g_capturedIntDefaults.end() && currentValue != it->second) {
                                LOG_VERBOSE("[PreventFinish] [MODIFIED] ID=" << def.id 
                                    << " (BOOL) Current=" << (currentValue ? "true" : "false")
                                    << " Baseline=" << (it->second ? "true" : "false"));
                            }
                        }
                    }
                    else if (def.type == 3) { // Float
                        float currentValue;
                        if (DevMenuSync::ReadValue<float>(def.id, currentValue)) {
                            auto it = g_capturedFloatDefaults.find(def.id);
                            if (it != g_capturedFloatDefaults.end()) {
                                const float epsilon = 0.0001f;
                                if (std::abs(currentValue - it->second) > epsilon) {
                                    LOG_VERBOSE("[PreventFinish] [MODIFIED] ID=" << def.id 
                                        << " (FLOAT) Current=" << currentValue
                                        << " Baseline=" << it->second
                                        << " Diff=" << std::abs(currentValue - it->second));
                                }
                            }
                        }
                    }
                }
            } else {
                LOG_VERBOSE("[PreventFinish] All bike/rider values match baseline");
            }
            g_lastModifiedState = isModified;
        }
        
        return isModified;
    }
    
    void ResetToAllowFinish() {
        LOG_VERBOSE("[PreventFinish] ResetToAllowFinish() called");
        
        // This function ONLY resets bike/rider values to baseline
        // It CANNOT clear permanent taint (checkpoint skip, fault/time reduction)
        // Permanent taint can ONLY be cleared by resetting to CP1
        
        if (!g_defaultsCaptured) {
            LOG_WARNING("[PreventFinish] No baseline values captured yet - cannot reset");
            return;
        }
        
        if (g_permanentTaint) {
            LOG_WARNING("[PreventFinish] Run has PERMANENT TAINT - reset button CANNOT clear this!");
            if (g_taintedByCheckpointSkip) {
                LOG_WARNING("[PreventFinish] Reason: Checkpoint skip detected");
            }
            if (g_taintedByFaultReduction) {
                LOG_WARNING("[PreventFinish] Reason: Fault count was reduced");
            }
            if (g_taintedByTimeReduction) {
                LOG_WARNING("[PreventFinish] Reason: Race time was reduced");
            }
            LOG_WARNING("[PreventFinish] You MUST reset to CP1 to clear permanent taint and start a clean run");
            LOG_VERBOSE("[PreventFinish] Resetting bike/rider values anyway (but finish remains blocked)...");
        } else if (g_finishBlocked) {
            LOG_VERBOSE("[PreventFinish] Resetting bike/rider values to captured baseline...");
        }
        
        int resetCount = 0;
        
        // Reset all bike values to captured baseline
        for (int i = 0; i < g_bikeDefaultsCount; i++) {
            const DefaultValue& def = g_bikeDefaults[i];
            
            if (def.type == 1 || def.type == 2) { // Bool or Int
                auto it = g_capturedIntDefaults.find(def.id);
                if (it != g_capturedIntDefaults.end()) {
                    if (DevMenuSync::WriteValue<int>(def.id, it->second)) {
                        resetCount++;
                    }
                }
            }
            else if (def.type == 3) { // Float
                auto it = g_capturedFloatDefaults.find(def.id);
                if (it != g_capturedFloatDefaults.end()) {
                    if (DevMenuSync::WriteValue<float>(def.id, it->second)) {
                        resetCount++;
                    }
                }
            }
        }
        
        // Reset all rider values to captured baseline
        for (int i = 0; i < g_riderDefaultsCount; i++) {
            const DefaultValue& def = g_riderDefaults[i];
            
            if (def.type == 1) { // Bool
                auto it = g_capturedIntDefaults.find(def.id);
                if (it != g_capturedIntDefaults.end()) {
                    if (DevMenuSync::WriteValue<int>(def.id, it->second)) {
                        resetCount++;
                    }
                }
            }
            else if (def.type == 3) { // Float
                auto it = g_capturedFloatDefaults.find(def.id);
                if (it != g_capturedFloatDefaults.end()) {
                    if (DevMenuSync::WriteValue<float>(def.id, it->second)) {
                        resetCount++;
                    }
                }
            }
        }
        
        LOG_VERBOSE("[PreventFinish] Reset " << resetCount << " bike/rider values to baseline");
        
        // Now sync FROM game memory back to DevMenu UI to update the sliders/checkboxes
        LOG_VERBOSE("[PreventFinish] Syncing reset values to DevMenu UI...");
        DevMenuSync::SyncFromGame();
        LOG_VERBOSE("[PreventFinish] DevMenu UI synchronized with reset values");
        
        // Reset player to checkpoint 1 (index 0)
        LOG_VERBOSE("[PreventFinish] Resetting player to checkpoint 1...");
        if (Respawn::RespawnAtCheckpointIndex(0)) {
            LOG_VERBOSE("[PreventFinish] Player reset to checkpoint 1 successfully");
        } else {
            LOG_WARNING("[PreventFinish] Failed to reset player to checkpoint 1 - you may need to manually reset");
        }
        
        // Summary message
        LOG_VERBOSE("[PreventFinish] Reset complete");
        if (g_permanentTaint) {
            LOG_WARNING("[PreventFinish] Finish remains PERMANENTLY BLOCKED - restart race to clear");
        } else if (g_finishBlocked) {
            LOG_VERBOSE("[PreventFinish] Finish will be re-enabled when taint is cleared at CP1");
        }
    }
}
