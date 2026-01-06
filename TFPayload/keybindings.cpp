#include "pch.h"
#include "keybindings.h"
#include "logging.h"
#include <fstream>
#include <sstream>

// Static member initialization
std::unordered_map<Keybindings::Action, int> Keybindings::s_keybindings;
std::unordered_map<Keybindings::Action, bool> Keybindings::s_keyStates;
bool Keybindings::s_initialized = false;

void Keybindings::Initialize() {
    if (s_initialized) {
        return;
    }
    
    // Set default keybindings
    // System controls
    s_keybindings[Action::InstantFinish] = '`';  // Default to V key
    s_keybindings[Action::ToggleDevMenu] = VK_HOME;  // Default to HOME key
    s_keybindings[Action::ClearConsole] = 'C';  // Default to C key
    s_keybindings[Action::ToggleVerboseLogging] = VK_OEM_PLUS;  // Default to = key
    s_keybindings[Action::ShowHelpText] = VK_OEM_MINUS;  // Default to - key
    s_keybindings[Action::DumpTweakables] = 'B';  // Default to B key
    
    // Leaderboard Scanner
    s_keybindings[Action::ScanLeaderboardByID] = VK_F2;  // F2 key
    s_keybindings[Action::ScanCurrentLeaderboard] = VK_F3;  // F3 key
    
    // Track Central Auto-Scroll
    s_keybindings[Action::StartAutoScroll] = VK_F5;  // F5 key
    s_keybindings[Action::Killswitch] = VK_F6;  // F6 key
    s_keybindings[Action::CycleSearch] = VK_F7;  // F12 key
    s_keybindings[Action::DecreaseScrollDelay] = VK_INSERT;  // INSERT key
    s_keybindings[Action::IncreaseScrollDelay] = VK_DELETE;  // DELETE key
    
    // Leaderboard Direct
    s_keybindings[Action::TestFetchTrackID] = VK_F10;  // F10 key
    // Pause controls
    s_keybindings[Action::TogglePause] = '0';  // 0 key
    
    // Camera controls
    s_keybindings[Action::CycleHUD] = 'O';  // O key
    
    // Respawn controls
    s_keybindings[Action::RespawnAtCheckpoint] = 'W';  // Q key
    s_keybindings[Action::RespawnPrevCheckpoint] = 'Q';  // W key
    s_keybindings[Action::RespawnNextCheckpoint] = 'E';  // E key
    s_keybindings[Action::RespawnForward5] = 'F';  // F key
    
    // Fault controls
    s_keybindings[Action::IncrementFault] = 'F';  // G key
    s_keybindings[Action::DebugFaultCounter] = VK_OEM_6;  // H key
    s_keybindings[Action::Add100Faults] = 'J';  // J key
    s_keybindings[Action::Subtract100Faults] = 'H';  // K key
    s_keybindings[Action::ResetFaults] = '1';  // R key
    
    // Time controls
    s_keybindings[Action::DebugTimeCounter] = VK_OEM_4;  // [ key
    s_keybindings[Action::Add60Seconds] = 'U';  // Y key
    s_keybindings[Action::Subtract60Seconds] = 'I';  // U key
    s_keybindings[Action::Add10Minute] = 'Y';  // I key
    s_keybindings[Action::ResetTime] = '2';  // ] key
    
    // Limit controls
    s_keybindings[Action::RestoreDefaultLimits] = VK_OEM_3;  // ~ key
    s_keybindings[Action::DebugLimits] = VK_OEM_7;  // ' key
    s_keybindings[Action::ToggleLimitValidation] = VK_F4;  // F4 key
    
    // Multiplayer Monitoring
    s_keybindings[Action::SaveMultiplayerLogs] = 'M';  // M key
    s_keybindings[Action::CaptureSessionState] = 'N';  // N key
    
    // ActionScript Commands
    s_keybindings[Action::FullCountdownSequence] = 'T';  // T key
    s_keybindings[Action::ShowSingleCountdown] = VK_SHIFT;  // SHIFT+T (handled separately in code)
    s_keybindings[Action::ToggleLoadScreen] = 'L';  // Z key
    
    // Keybindings Menu
    s_keybindings[Action::ToggleKeybindingsMenu] = 'K';  // K key
    
    // Initialize key states
    s_keyStates[Action::InstantFinish] = false;
    s_keyStates[Action::ToggleDevMenu] = false;
    s_keyStates[Action::ClearConsole] = false;
    s_keyStates[Action::ToggleVerboseLogging] = false;
    s_keyStates[Action::ShowHelpText] = false;
    s_keyStates[Action::DumpTweakables] = false;
    s_keyStates[Action::ScanLeaderboardByID] = false;
    s_keyStates[Action::ScanCurrentLeaderboard] = false;
    s_keyStates[Action::StartAutoScroll] = false;
    s_keyStates[Action::Killswitch] = false;
    s_keyStates[Action::CycleSearch] = false;
    s_keyStates[Action::DecreaseScrollDelay] = false;
    s_keyStates[Action::IncreaseScrollDelay] = false;
    s_keyStates[Action::TestFetchTrackID] = false;
    s_keyStates[Action::TogglePause] = false;
    s_keyStates[Action::CycleHUD] = false;
    s_keyStates[Action::RespawnAtCheckpoint] = false;
    s_keyStates[Action::RespawnPrevCheckpoint] = false;
    s_keyStates[Action::RespawnNextCheckpoint] = false;
    s_keyStates[Action::RespawnForward5] = false;
    s_keyStates[Action::IncrementFault] = false;
    s_keyStates[Action::DebugFaultCounter] = false;
    s_keyStates[Action::Add100Faults] = false;
    s_keyStates[Action::Subtract100Faults] = false;
    s_keyStates[Action::ResetFaults] = false;
    s_keyStates[Action::DebugTimeCounter] = false;
    s_keyStates[Action::Add60Seconds] = false;
    s_keyStates[Action::Subtract60Seconds] = false;
    s_keyStates[Action::Add10Minute] = false;
    s_keyStates[Action::ResetTime] = false;
    s_keyStates[Action::RestoreDefaultLimits] = false;
    s_keyStates[Action::DebugLimits] = false;
    s_keyStates[Action::ToggleLimitValidation] = false;
    s_keyStates[Action::SaveMultiplayerLogs] = false;
    s_keyStates[Action::CaptureSessionState] = false;
    s_keyStates[Action::FullCountdownSequence] = false;
    s_keyStates[Action::ShowSingleCountdown] = false;
    s_keyStates[Action::ToggleLoadScreen] = false;
    s_keyStates[Action::ToggleKeybindingsMenu] = false;
    
    // Try to load from file
    if (!LoadFromFile()) {
        LOG_VERBOSE("[Keybindings] No config file found, using defaults");
        // Save defaults to create the file
        SaveToFile();
    } else {
        LOG_VERBOSE("[Keybindings] Loaded keybindings from config file");
    }
    
    s_initialized = true;
}

void Keybindings::Shutdown() {
    if (!s_initialized) {
        return;
    }
    
    // Save current keybindings
    SaveToFile();
    
    s_keybindings.clear();
    s_keyStates.clear();
    s_initialized = false;
}

int Keybindings::GetKey(Action action) {
    auto it = s_keybindings.find(action);
    if (it != s_keybindings.end()) {
        return it->second;
    }
    return 0; // No key bound
}

void Keybindings::SetKey(Action action, int vkCode) {
    s_keybindings[action] = vkCode;
    SaveToFile();
    LOG_VERBOSE("[Keybindings] Set " << GetActionName(action) << " to " << GetKeyName(vkCode));
}

bool Keybindings::IsActionPressed(Action action) {
    int vkCode = GetKey(action);
    if (vkCode == 0) {
        return false;
    }
    
    bool currentlyPressed = (GetAsyncKeyState(vkCode) & 0x8000) != 0;
    bool wasPressed = s_keyStates[action];
    
    // Update state
    s_keyStates[action] = currentlyPressed;
    
    // Return true only on key press (not held)
    return currentlyPressed && !wasPressed;
}

std::string Keybindings::GetKeyName(int vkCode) {
    // Special keys
    switch (vkCode) {
        case VK_BACK: return "Backspace";
        case VK_TAB: return "Tab";
        case VK_RETURN: return "Enter";
        case VK_SHIFT: return "Shift";
        case VK_CONTROL: return "Ctrl";
        case VK_MENU: return "Alt";
        case VK_PAUSE: return "Pause";
        case VK_CAPITAL: return "Caps Lock";
        case VK_ESCAPE: return "Escape";
        case VK_SPACE: return "Space";
        case VK_PRIOR: return "Page Up";
        case VK_NEXT: return "Page Down";
        case VK_END: return "End";
        case VK_HOME: return "Home";
        case VK_LEFT: return "Left Arrow";
        case VK_UP: return "Up Arrow";
        case VK_RIGHT: return "Right Arrow";
        case VK_DOWN: return "Down Arrow";
        case VK_INSERT: return "Insert";
        case VK_DELETE: return "Delete";
        case VK_LWIN: return "Left Win";
        case VK_RWIN: return "Right Win";
        case VK_NUMPAD0: return "Numpad 0";
        case VK_NUMPAD1: return "Numpad 1";
        case VK_NUMPAD2: return "Numpad 2";
        case VK_NUMPAD3: return "Numpad 3";
        case VK_NUMPAD4: return "Numpad 4";
        case VK_NUMPAD5: return "Numpad 5";
        case VK_NUMPAD6: return "Numpad 6";
        case VK_NUMPAD7: return "Numpad 7";
        case VK_NUMPAD8: return "Numpad 8";
        case VK_NUMPAD9: return "Numpad 9";
        case VK_MULTIPLY: return "Numpad *";
        case VK_ADD: return "Numpad +";
        case VK_SUBTRACT: return "Numpad -";
        case VK_DECIMAL: return "Numpad .";
        case VK_DIVIDE: return "Numpad /";
        case VK_F1: return "F1";
        case VK_F2: return "F2";
        case VK_F3: return "F3";
        case VK_F4: return "F4";
        case VK_F5: return "F5";
        case VK_F6: return "F6";
        case VK_F7: return "F7";
        case VK_F8: return "F8";
        case VK_F9: return "F9";
        case VK_F10: return "F10";
        case VK_F11: return "F11";
        case VK_F12: return "F12";
        case VK_NUMLOCK: return "Num Lock";
        case VK_SCROLL: return "Scroll Lock";
        case VK_OEM_1: return "Semicolon";
        case VK_OEM_PLUS: return "Equals";
        case VK_OEM_COMMA: return "Comma";
        case VK_OEM_MINUS: return "Hyphen";
        case VK_OEM_PERIOD: return "Period";
        case VK_OEM_2: return "Forward Slash";
        case VK_OEM_3: return "Tilde";
        case VK_OEM_4: return "Left Bracket";
        case VK_OEM_5: return "Backslash";
        case VK_OEM_6: return "Right Bracket";
        case VK_OEM_7: return "Quote";
        case 0: return "None";
    }
    
    // Alphanumeric keys
    if ((vkCode >= '0' && vkCode <= '9') || (vkCode >= 'A' && vkCode <= 'Z')) {
        char keyChar = static_cast<char>(vkCode);
        return std::string(1, keyChar);
    }
    
    // Unknown key
    return "Unknown (" + std::to_string(vkCode) + ")";
}

std::string Keybindings::GetActionName(Action action) {
    switch (action) {
        // System controls
        case Action::InstantFinish:
            return "Instant Finish";
        case Action::ToggleDevMenu:
            return "Toggle Dev Menu";
        case Action::ClearConsole:
            return "Clear Console";
        case Action::ToggleVerboseLogging:
            return "Toggle Verbose Logging";
        case Action::ShowHelpText:
            return "Show Help Text";
        case Action::DumpTweakables:
            return "Dump Tweakables";
        // Leaderboard Scanner
        case Action::ScanLeaderboardByID:
            return "Scan Leaderboard By ID";
        case Action::ScanCurrentLeaderboard:
            return "Scan Current Leaderboard";
        // Track Central Auto-Scroll
        case Action::StartAutoScroll:
            return "Start Auto Scroll";
        case Action::Killswitch:
            return "Auto Scroll Killswitch";
        case Action::CycleSearch:
            return "Cycle Search";
        case Action::DecreaseScrollDelay:
            return "Decrease Scroll Delay";
        case Action::IncreaseScrollDelay:
            return "Increase Scroll Delay";
        // Leaderboard Direct
        case Action::TestFetchTrackID:
            return "Test Fetch Track ID";
        // Pause controls
        case Action::TogglePause:
            return "Toggle Pause";
        // Camera controls
        case Action::CycleHUD:
            return "Cycle Camera Mode";
        // Respawn controls
        case Action::RespawnAtCheckpoint:
            return "Respawn At Checkpoint";
        case Action::RespawnPrevCheckpoint:
            return "Respawn Prev Checkpoint";
        case Action::RespawnNextCheckpoint:
            return "Respawn Next Checkpoint";
        case Action::RespawnForward5:
            return "Respawn +5 Checkpoints ahead";
        // Fault controls
        case Action::IncrementFault:
            return "Increment Fault";
        case Action::DebugFaultCounter:
            return "Debug Fault Counter";
        case Action::Add100Faults:
            return "+100 Faults+";
        case Action::Subtract100Faults:
            return "-100 Faults-";
        case Action::ResetFaults:
            return "Reset Faults";
        // Time controls
        case Action::DebugTimeCounter:
            return "Debug Time Counter";
        case Action::Add60Seconds:
            return "+60 Seconds+";
        case Action::Subtract60Seconds:
            return "-60 Seconds-";
        case Action::Add10Minute:
            return "+10 Minute+";
        case Action::ResetTime:
            return "Reset Time";
        case Action::ToggleLimitValidation:
            return "Toggle Limit Validation";
        // Multiplayer Monitoring
        case Action::SaveMultiplayerLogs:
            return "Save Multiplayer Logs";
        case Action::CaptureSessionState:
            return "Capture Session State";
        // ActionScript Commands
        case Action::FullCountdownSequence:
            return "Full Countdown Sequence";
        case Action::ShowSingleCountdown:
            return "Show Single Countdown";
        case Action::ToggleLoadScreen:
            return "Toggle Load Screen";
        case Action::ToggleKeybindingsMenu:
            return "Toggle Keybindings Menu";
        default:
            return "Unknown Action";
    }
}

bool Keybindings::SaveToFile() {
    std::string configPath = GetConfigPath();
    
    std::ofstream file(configPath);
    if (!file.is_open()) {
        LOG_ERROR("[Keybindings] Failed to open config file for writing: " << configPath);
        return false;
    }
    
    file << "# TFPayload Keybindings Configuration" << std::endl;
    file << "# Format: ActionName=VirtualKeyCode" << std::endl;
    file << "# Virtual Key Codes: https://docs.microsoft.com/en-us/windows/win32/inputdev/virtual-key-codes" << std::endl;
    file << std::endl;
    
    for (const auto& pair : s_keybindings) {
        file << GetActionName(pair.first) << "=" << pair.second << " # " << GetKeyName(pair.second) << std::endl;
    }
    
    file.close();
    LOG_VERBOSE("[Keybindings] Saved keybindings to: " << configPath);
    return true;
}

bool Keybindings::LoadFromFile() {
    std::string configPath = GetConfigPath();
    
    std::ifstream file(configPath);
    if (!file.is_open()) {
        return false;
    }
    
    std::string line;
    while (std::getline(file, line)) {
        // Skip comments and empty lines
        if (line.empty() || line[0] == '#') {
            continue;
        }
        
        // Parse line: ActionName=VirtualKeyCode
        size_t equalsPos = line.find('=');
        if (equalsPos == std::string::npos) {
            continue;
        }
        
        std::string actionName = line.substr(0, equalsPos);
        std::string vkCodeStr = line.substr(equalsPos + 1);
        
        // Remove any comments after the value
        size_t commentPos = vkCodeStr.find('#');
        if (commentPos != std::string::npos) {
            vkCodeStr = vkCodeStr.substr(0, commentPos);
        }
        
        // Trim whitespace
        vkCodeStr.erase(0, vkCodeStr.find_first_not_of(" \t"));
        vkCodeStr.erase(vkCodeStr.find_last_not_of(" \t") + 1);
        
        try {
            int vkCode = std::stoi(vkCodeStr);
            
            // Map action name to Action enum
            // System controls
            if (actionName == "Instant Finish") {
                s_keybindings[Action::InstantFinish] = vkCode;
            } else if (actionName == "Toggle Dev Menu") {
                s_keybindings[Action::ToggleDevMenu] = vkCode;
            } else if (actionName == "Clear Console") {
                s_keybindings[Action::ClearConsole] = vkCode;
            } else if (actionName == "Toggle Verbose Logging") {
                s_keybindings[Action::ToggleVerboseLogging] = vkCode;
            } else if (actionName == "Show Help Text") {
                s_keybindings[Action::ShowHelpText] = vkCode;
            } else if (actionName == "Dump Tweakables") {
                s_keybindings[Action::DumpTweakables] = vkCode;
            // Leaderboard Scanner
            } else if (actionName == "Scan Leaderboard By ID") {
                s_keybindings[Action::ScanLeaderboardByID] = vkCode;
            } else if (actionName == "Scan Current Leaderboard") {
                s_keybindings[Action::ScanCurrentLeaderboard] = vkCode;
            // Track Central Auto-Scroll
            } else if (actionName == "Start Auto Scroll") {
                s_keybindings[Action::StartAutoScroll] = vkCode;
            } else if (actionName == "Auto Scroll Killswitch") {
                s_keybindings[Action::Killswitch] = vkCode;
            } else if (actionName == "Cycle Search") {
                s_keybindings[Action::CycleSearch] = vkCode;
            } else if (actionName == "Decrease Scroll Delay") {
                s_keybindings[Action::DecreaseScrollDelay] = vkCode;
            } else if (actionName == "Increase Scroll Delay") {
                s_keybindings[Action::IncreaseScrollDelay] = vkCode;
            // Leaderboard Direct
            } else if (actionName == "Test Fetch Track ID") {
                s_keybindings[Action::TestFetchTrackID] = vkCode;
            // Pause controls
            } else if (actionName == "Toggle Pause") {
                s_keybindings[Action::TogglePause] = vkCode;
            // Camera controls
            } else if (actionName == "Cycle Camera Mode") {
                s_keybindings[Action::CycleHUD] = vkCode;
            // Respawn controls
            } else if (actionName == "Respawn At Checkpoint") {
                s_keybindings[Action::RespawnAtCheckpoint] = vkCode;
            } else if (actionName == "Respawn Prev Checkpoint") {
                s_keybindings[Action::RespawnPrevCheckpoint] = vkCode;
            } else if (actionName == "Respawn Next Checkpoint") {
                s_keybindings[Action::RespawnNextCheckpoint] = vkCode;
            } else if (actionName == "Respawn +5 Checkpoints ahead") {
                s_keybindings[Action::RespawnForward5] = vkCode;
            // Fault controls
            } else if (actionName == "Increment Fault") {
                s_keybindings[Action::IncrementFault] = vkCode;
            } else if (actionName == "Debug Fault Counter") {
                s_keybindings[Action::DebugFaultCounter] = vkCode;
            } else if (actionName == "+100 Faults+") {
                s_keybindings[Action::Add100Faults] = vkCode;
            } else if (actionName == "-100 Faults-") {
                s_keybindings[Action::Subtract100Faults] = vkCode;
            } else if (actionName == "Reset Faults") {
                s_keybindings[Action::ResetFaults] = vkCode;
            // Time controls
            } else if (actionName == "Debug Time Counter") {
                s_keybindings[Action::DebugTimeCounter] = vkCode;
            } else if (actionName == "+60 Seconds+" || actionName == "Add 10 Seconds") {
                s_keybindings[Action::Add60Seconds] = vkCode;
            } else if (actionName == "-60 Seconds-" || actionName == "Subtract 10 Seconds") {
                s_keybindings[Action::Subtract60Seconds] = vkCode;
            } else if (actionName == "+10 Minute+" || actionName == "Add 1 Minute") {
                s_keybindings[Action::Add10Minute] = vkCode;
            } else if (actionName == "Reset Time") {
                s_keybindings[Action::ResetTime] = vkCode;
            } else if (actionName == "Toggle Limit Validation" || actionName == "Disable All Limit Validation") {
                s_keybindings[Action::ToggleLimitValidation] = vkCode;
            // Multiplayer Monitoring
            } else if (actionName == "Save Multiplayer Logs") {
                s_keybindings[Action::SaveMultiplayerLogs] = vkCode;
            } else if (actionName == "Capture Session State") {
                s_keybindings[Action::CaptureSessionState] = vkCode;
            // ActionScript Commands
            } else if (actionName == "Full Countdown Sequence") {
                s_keybindings[Action::FullCountdownSequence] = vkCode;
            } else if (actionName == "Show Single Countdown") {
                s_keybindings[Action::ShowSingleCountdown] = vkCode;
            } else if (actionName == "Toggle Load Screen") {
                s_keybindings[Action::ToggleLoadScreen] = vkCode;
            } else if (actionName == "Toggle Keybindings Menu") {
                s_keybindings[Action::ToggleKeybindingsMenu] = vkCode;
            }
        } catch (const std::exception& e) {
            LOG_WARNING("[Keybindings] Failed to parse line: " << line << ": " << e.what());
        }
    }
    
    file.close();
    return true;
}

std::string Keybindings::GetConfigPath() {
    // Save to F:\tfpayload_keybindings.cfg
    return "F:\\tfpayload_keybindings.cfg";
}
