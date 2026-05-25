#pragma once

#include <Windows.h>
#include <string>
#include <unordered_map>

// Keybinding system for configurable hotkeys
class Keybindings {
public:
    enum class Action {
        InstantFinish,
        ToggleDevMenu,
        ClearConsole,
        ToggleVerboseLogging,
        ShowHelpText,
        DumpTweakables,
        ScanLeaderboardByID,
        ScanCurrentLeaderboard,
        StartAutoScroll,
        Killswitch,
        CycleSearch,
        DecreaseScrollDelay,
        IncreaseScrollDelay,
        TestFetchTrackID,
        TogglePatch,
        TogglePause,
        CycleHUD,
        RespawnAtCheckpoint,
        RespawnPrevCheckpoint,
        RespawnNextCheckpoint,
        RespawnForward5,
        IncrementFault,
        DebugFaultCounter,
        Add100Faults,
        Subtract100Faults,
        ResetFaults,
        DebugTimeCounter,
        Add60Seconds,
        Subtract60Seconds,
        Add10Minute,
        ResetTime,
        RestoreDefaultLimits,
        DebugLimits,
        ToggleLimitValidation, 
        SaveMultiplayerLogs,
        CaptureSessionState,
        TogglePhysicsLogging,
        DumpPhysicsLog,
        ModifyXPosition,
        FullCountdownSequence,
        ShowSingleCountdown,
        ToggleLoadScreen,
        ToggleOverlay,
        ToggleKeybindingsMenu,
        DebugGameState,
        SwapNextBike,
        SwapPrevBike,
        DebugBikeInfo,
        ToggleConsole,
        EditorScaleDecrease,
        EditorScaleIncrease,
    };

    static void Initialize();
    static void Shutdown();
    
    // Get the current key for an action
    static int GetKey(Action action);
    
    // Set a new key for an action (and save to file).
    // Pass 0 to unbind the action.
    static void SetKey(Action action, int vkCode);

    // Get/set the secondary gamepad button for an action.
    // Pass 0 to unbind the action.
    static int GetGamepadButton(Action action);
    static void SetGamepadButton(Action action, int gamepadButton);
    
    // Check if a key is currently pressed for an action
    static bool IsActionPressed(Action action);

    // Check if a key or gamepad button is currently held for an action
    static bool IsActionDown(Action action);
    
    // Get the name of a virtual key code
    static std::string GetKeyName(int vkCode);

    // Get the name of a gamepad button code
    static std::string GetGamepadButtonName(int gamepadButton);

    // Returns one currently pressed gamepad button, or 0 if none is pressed.
    static int GetPressedGamepadButton();
    
    // Get the name of an action
    static std::string GetActionName(Action action);
    
    // Save keybindings to file
    static bool SaveToFile();
    
    // Load keybindings from file
    static bool LoadFromFile();
    
    // Get config file path
    static std::string GetConfigPath();

private:
    static std::unordered_map<Action, int> s_keybindings;
    static std::unordered_map<Action, int> s_gamepadBindings;
    static std::unordered_map<Action, bool> s_keyStates;
    static std::unordered_map<Action, bool> s_gamepadStates;
    static bool s_initialized;
};
