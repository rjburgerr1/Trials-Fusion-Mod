#include "pch.h"
#include "devMenu.h"
#include "devMenuSync.h"
#include "imgui/imgui.h"
#include "logging.h"
#include "respawn.h"
#include "actionscript.h"
#include "keybindings.h"
#include "multiplayer.h"
#include <algorithm>
#include <fstream>
#include <sstream>
#include <Windows.h>

// Global instance
DevMenu* g_DevMenu = nullptr;

std::shared_ptr<TweakableFloat> CreateSyncedFloat(int id, const std::string& name,
    float defaultVal, float minVal, float maxVal) {
    auto tweakable = std::make_shared<TweakableFloat>(id, name, defaultVal, minVal, maxVal);

    // Auto-sync to game memory on change
    tweakable->SetOnChangeCallback([id, name](float newValue) {
        LOG_VERBOSE("Float changed: " << name << " (ID=" << id << ") = " << newValue);
        if (!DevMenuSync::WriteValue<float>(id, newValue)) {
            LOG_WARNING("Failed to write Float ID=" << id << " to game memory!");
        }
        else {
            LOG_VERBOSE("Successfully wrote Float ID=" << id << " to game memory");
        }
        });

    return tweakable;
}

std::shared_ptr<TweakableInt> CreateSyncedInt(int id, const std::string& name,
    int defaultVal, int minVal, int maxVal) {
    auto tweakable = std::make_shared<TweakableInt>(id, name, defaultVal, minVal, maxVal);

    tweakable->SetOnChangeCallback([id, name](int newValue) {
        LOG_VERBOSE("Int changed: " << name << " (ID=" << id << ") = " << newValue);
        if (!DevMenuSync::WriteValue<int>(id, newValue)) {
            LOG_WARNING("Failed to write Int ID=" << id << " to game memory!");
        }
        else {
            LOG_VERBOSE("Successfully wrote Int ID=" << id << " to game memory");
        }
        });

    return tweakable;
}

std::shared_ptr<TweakableBool> CreateSyncedBool(int id, const std::string& name, bool defaultVal) {
    auto tweakable = std::make_shared<TweakableBool>(id, name, defaultVal);

    tweakable->SetOnChangeCallback([id, name](bool newValue) {
        LOG_VERBOSE("Bool changed: " << name << " (ID=" << id << ") = " << (newValue ? "true" : "false"));
        // Game uses int for bool (0 or 1)
        if (!DevMenuSync::WriteValue<int>(id, newValue ? 1 : 0)) {
            LOG_WARNING("Failed to write Bool ID=" << id << " to game memory!");
        }
        else {
            LOG_VERBOSE("Successfully wrote Bool ID=" << id << " to game memory");
        }
        });

    return tweakable;
}

// TweakableFloat Implementation
void TweakableFloat::Render() {
    float oldValue = m_value;

    // Create a unique ID for ImGui
    std::string label = "##" + m_name + std::to_string(m_id);

    ImGui::PushItemWidth(200.0f);

    // Use slider for float values
    if (ImGui::SliderFloat(label.c_str(), &m_value, m_minValue, m_maxValue, "%.3f")) {
        if (m_onChange) {
            m_onChange(m_value);
        }
    }

    ImGui::PopItemWidth();

    // Show the name after the slider
    ImGui::SameLine();
    ImGui::Text("%s", m_name.c_str());

    // Add reset button
    if (m_value != m_defaultValue) {
        ImGui::SameLine();
        std::string resetLabel = "Reset##" + std::to_string(m_id);
        if (ImGui::SmallButton(resetLabel.c_str())) {
            Reset();
            if (m_onChange) {
                m_onChange(m_value);
            }
        }
    }

    // Right-click for manual input
    if (ImGui::IsItemClicked(1)) {
        ImGui::OpenPopup(("Input##" + std::to_string(m_id)).c_str());
    }

    if (ImGui::BeginPopup(("Input##" + std::to_string(m_id)).c_str())) {
        ImGui::Text("Enter value:");
        if (ImGui::InputFloat("##input", &m_value, 0.0f, 0.0f, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue)) {
            if (m_onChange) {
                m_onChange(m_value);
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// TweakableInt Implementation
void TweakableInt::Render() {
    int oldValue = m_value;

    std::string label = "##" + m_name + std::to_string(m_id);

    ImGui::PushItemWidth(200.0f);

    // Use slider for int values
    if (ImGui::SliderInt(label.c_str(), &m_value, m_minValue, m_maxValue)) {
        if (m_onChange) {
            m_onChange(m_value);
        }
    }

    ImGui::PopItemWidth();

    ImGui::SameLine();
    ImGui::Text("%s", m_name.c_str());

    // Add reset button
    if (m_value != m_defaultValue) {
        ImGui::SameLine();
        std::string resetLabel = "Reset##" + std::to_string(m_id);
        if (ImGui::SmallButton(resetLabel.c_str())) {
            Reset();
            if (m_onChange) {
                m_onChange(m_value);
            }
        }
    }

    // Right-click for manual input
    if (ImGui::IsItemClicked(1)) {
        ImGui::OpenPopup(("Input##" + std::to_string(m_id)).c_str());
    }

    if (ImGui::BeginPopup(("Input##" + std::to_string(m_id)).c_str())) {
        ImGui::Text("Enter value:");
        if (ImGui::InputInt("##input", &m_value, 1, 10, ImGuiInputTextFlags_EnterReturnsTrue)) {
            if (m_onChange) {
                m_onChange(m_value);
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// TweakableBool Implementation
void TweakableBool::Render() {
    bool oldValue = m_value;

    std::string label = m_name + "##" + std::to_string(m_id);

    if (ImGui::Checkbox(label.c_str(), &m_value)) {
        if (m_onChange) {
            m_onChange(m_value);
        }
    }

    // Add reset button if value changed
    if (m_value != m_defaultValue) {
        ImGui::SameLine();
        std::string resetLabel = "Reset##" + std::to_string(m_id);
        if (ImGui::SmallButton(resetLabel.c_str())) {
            Reset();
            if (m_onChange) {
                m_onChange(m_value);
            }
        }
    }
}

// TweakableButton Implementation
void TweakableButton::Render() {
    std::string label = m_name + "##" + std::to_string(m_id);
    
    if (ImGui::Button(label.c_str())) {
        if (m_onClick) {
            m_onClick();
        }
    }
}

// TweakableFolder Implementation
void TweakableFolder::Render() {
    // Use TreeNode for folders
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_None;
    if (m_isOpen) {
        flags |= ImGuiTreeNodeFlags_DefaultOpen;
    }

    std::string label = m_name + "##" + std::to_string(m_id);

    if (ImGui::TreeNodeEx(label.c_str(), flags)) {
        m_isOpen = true;

        // Render all children
        for (auto& child : m_children) {
            child->Render();
        }

        ImGui::TreePop();
    }
    else {
        m_isOpen = false;
    }
}

void TweakableFolder::Reset() {
    for (auto& child : m_children) {
        child->Reset();
    }
}

// DevMenu Implementation
DevMenu::DevMenu()
    : m_isVisible(false)
    , m_menuWidth(600.0f)
    , m_menuHeight(800.0f)
    , m_showResetButton(true)
    , m_showSearchBar(true)
    , m_showKeybindingsWindow(false)
{
}

DevMenu::~DevMenu() {
}

void DevMenu::Initialize() {
    LOG_VERBOSE("[DevMenu] Initializing...");

    // Initialize all tweakable categories
    InitializeBikeSound();
    InitializeDynamicMusic();
    InitializeProgressionSystem();
    InitializeGarage();
    InitializeContentPack();
    InitializeDLC();
    InitializeEditor();
    InitializeMultiplayer();
    InitializeEvent();
    InitializeFMX();
    InitializeBike();
    InitializeRider();
    InitializeVibra();
    InitializeSoundSystem();
    InitializeReplayCRC();
    InitializeUtils();
    InitializeReplayCamera();
    InitializePhysics();
    InitializeFrameSkipper();
    InitializeGraphic();
    InitializePodium();
    InitializeXPSystem();
    InitializeTrackUpload();
    InitializeGameOption();
    InitializeGameSwf();
    InitializeGameTime();
    InitializeVariableFramerate();
    InitializeDebug();
    InitializeInGameHud();
    InitializeMainHub();
    InitializeMainMenu();
    InitializeFlash();
    InitializeGarbageCollector();
    InitializeSettings();
    InitializeDebugLocalization();
    InitializeMod();
    InitializeKeybindings();
    LOG_VERBOSE("[DevMenu] Initialized with " << m_rootFolders.size() << " root folders");
}

void DevMenu::Render() {
    // Render Keybindings window independently (even if main menu is hidden)
    if (m_showKeybindingsWindow) {
        ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(700, 50), ImGuiCond_FirstUseEver);
        
        if (ImGui::Begin("Keybindings", &m_showKeybindingsWindow)) {
            ImGui::Text("Configure hotkeys for mod actions:");
            ImGui::Separator();
            
            // Build a map of VK codes to count how many actions use each key
            std::unordered_map<int, int> keyUsageCount;
            size_t numKeybinds = m_keybindingItems.size() >= 2 ? m_keybindingItems.size() - 2 : 0;
            
            for (size_t i = 0; i < numKeybinds; i++) {
                int vkCode = Keybindings::GetKey(m_keybindingActions[i]);
                if (vkCode != 0) { // Ignore unbound keys
                    keyUsageCount[vkCode]++;
                }
            }
            
            // Calculate widths for alignment
            float maxActionWidth = 0.0f;
            float maxKeyWidth = 0.0f;
            
            for (size_t i = 0; i < numKeybinds; i++) {
                Keybindings::Action action = m_keybindingActions[i];
                std::string actionName = Keybindings::GetActionName(action);
                std::string keyName = Keybindings::GetKeyName(Keybindings::GetKey(action));
                
                ImVec2 actionSize = ImGui::CalcTextSize(actionName.c_str());
                ImVec2 keySize = ImGui::CalcTextSize(keyName.c_str());
                
                if (actionSize.x > maxActionWidth) maxActionWidth = actionSize.x;
                if (keySize.x > maxKeyWidth) maxKeyWidth = keySize.x;
            }
            
            // Add padding for the button
            float buttonPadding = ImGui::GetStyle().FramePadding.x * 2.0f;
            float colonWidth = ImGui::CalcTextSize(": ").x;
            float totalButtonWidth = maxActionWidth + colonWidth + maxKeyWidth + buttonPadding + 10.0f; // 10 extra padding
            
            // Render keybindings with aligned columns
            for (size_t i = 0; i < m_keybindingItems.size(); i++) {
                auto& item = m_keybindingItems[i];
                
                // Check if this is one of the control buttons at the end (Save as Default or Reset All)
                if (i >= m_keybindingItems.size() - 2) {
                    item->Render();
                    if (i < m_keybindingItems.size() - 1) {
                        ImGui::SameLine();
                    }
                    continue;
                }
                
                // Get info for this keybinding
                Keybindings::Action action = m_keybindingActions[i];
                std::string actionName = Keybindings::GetActionName(action);
                std::string keyName = Keybindings::GetKeyName(Keybindings::GetKey(action));
                int currentKey = Keybindings::GetKey(action);
                int defaultKey = m_keybindingDefaults[i];
                
                // Check if the button is in "waiting for key" mode by checking its actual name
                std::string buttonName = item->GetName();
                bool isWaitingForKey = (buttonName.find("Press any key...") != std::string::npos);
                
                // Check if this key is bound to multiple actions
                bool isOverbound = (keyUsageCount[currentKey] > 1);
                
                // Build the button label
                std::string buttonLabel;
                if (isWaitingForKey) {
                    // Show "Press any key..." message when rebinding
                    buttonLabel = "Press any key...##bind" + std::to_string(i);
                } else {
                    // Build button label with fixed-width action name for alignment
                    // Pad the action name to ensure consistent spacing
                    std::string paddedActionName = actionName;
                    float actionTextWidth = ImGui::CalcTextSize(actionName.c_str()).x;
                    float paddingNeeded = maxActionWidth - actionTextWidth;
                    
                    // Calculate number of spaces needed (approximate, using average char width)
                    int numSpaces = (int)(paddingNeeded / ImGui::CalcTextSize(" ").x);
                    for (int s = 0; s < numSpaces; s++) {
                        paddedActionName += " ";
                    }
                    
                    buttonLabel = paddedActionName + ": " + keyName + "##bind" + std::to_string(i);
                }
                
                // Render the button with fixed width and left-aligned text
                ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f)); // 0.0 = left align, 0.5 = vertical center
                
                // Show visual indicator when waiting for key press or if key is overbound
                if (isWaitingForKey) {
                    // Use a different color to indicate we're waiting for input
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.6f, 0.3f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.4f, 0.7f, 0.4f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.2f, 0.5f, 0.2f, 1.0f));
                } else if (isOverbound) {
                    // Use red color to indicate this key is bound to multiple actions
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.3f, 0.3f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.7f, 0.1f, 0.1f, 1.0f));
                }
                
                if (ImGui::Button(buttonLabel.c_str(), ImVec2(totalButtonWidth, 0))) {
                    // Trigger the keybinding capture callback
                    if (auto btn = std::dynamic_pointer_cast<TweakableButton>(item)) {
                        btn->TriggerClick();
                    }
                }
                
                // Add tooltip for overbound keys
                if (isOverbound && !isWaitingForKey) {
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("WARNING: This key is bound to multiple actions!");
                    }
                }
                
                if (isWaitingForKey || isOverbound) {
                    ImGui::PopStyleColor(3); // Pop the 3 color overrides
                }
                
                ImGui::PopStyleVar(); // Restore previous alignment
                
                // Add individual buttons next to it
                ImGui::SameLine();
                
                // Only show Reset and Save as Default buttons if current key differs from default
                if (currentKey != defaultKey) {
                    std::string saveLabel = "Save##" + std::to_string(i);
                    if (ImGui::SmallButton(saveLabel.c_str())) {
                        // Save this specific keybinding as the new default (updates the stored default)
                        m_keybindingDefaults[i] = currentKey;
                        Keybindings::SaveToFile();
                        LOG_VERBOSE("[DevMenu] Saved " << actionName << " = " << keyName << " as default");
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Save as Default");
                    }
                    
                    ImGui::SameLine();
                    
                    std::string resetLabel = "Reset##" + std::to_string(i);
                    if (ImGui::SmallButton(resetLabel.c_str())) {
                        // Reset this specific keybinding to default
                        Keybindings::SetKey(action, defaultKey);
                        std::string newKeyName = Keybindings::GetKeyName(defaultKey);
                        item->SetName("Bind " + actionName + ": " + newKeyName);
                        LOG_VERBOSE("[DevMenu] Reset " << actionName << " to default: " << newKeyName);
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Reset to Default");
                    }
                } else {
                    // Show disabled text to maintain alignment
                    ImGui::TextDisabled("(default)");
                }
            }
        }
        ImGui::End();
    }
    
    // Early return if main dev menu is not visible
    if (!m_isVisible) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(m_menuWidth, m_menuHeight), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(50, 50), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Developer Menu", &m_isVisible, ImGuiWindowFlags_MenuBar)) {
        ImGui::End();
        return;
    }

    // Menu bar
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Save Config")) {
                SaveConfig("devmenu_config.txt");
            }
            if (ImGui::MenuItem("Load Config")) {
                LoadConfig("devmenu_config.txt");
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Reset All")) {
                ResetAll();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Options")) {
            ImGui::MenuItem("Show Search Bar", nullptr, &m_showSearchBar);
            ImGui::MenuItem("Show Reset Buttons", nullptr, &m_showResetButton);
            ImGui::EndMenu();
        }
        
        // Keybindings menu
        if (ImGui::BeginMenu("Keybindings")) {
            ImGui::MenuItem("Show Keybindings Window", nullptr, &m_showKeybindingsWindow);
            ImGui::EndMenu();
        }

        ImGui::EndMenuBar();
    }

    // Search bar
    if (m_showSearchBar) {
        char searchBuffer[256] = { 0 };
        strncpy_s(searchBuffer, sizeof(searchBuffer), m_searchFilter.c_str(), _TRUNCATE);

        ImGui::PushItemWidth(-1);
        if (ImGui::InputText("##search", searchBuffer, sizeof(searchBuffer))) {
            m_searchFilter = searchBuffer;
        }
        ImGui::PopItemWidth();

        if (!m_searchFilter.empty()) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Clear")) {
                m_searchFilter.clear();
            }
        }

        ImGui::Separator();
    }

    // Render all root folders
    ImGui::BeginChild("ScrollingRegion", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

    for (auto& folder : m_rootFolders) {
        if (m_searchFilter.empty() || PassesFilter(folder->GetName())) {
            folder->Render();
        }
    }

    ImGui::EndChild();
    ImGui::End();
}

void DevMenu::ResetAll() {
    for (auto& folder : m_rootFolders) {
        folder->Reset();
    }
}

std::shared_ptr<TweakableFloat> DevMenu::GetFloat(int id) {
    return GetTweakable<TweakableFloat>(id);
}

std::shared_ptr<TweakableInt> DevMenu::GetInt(int id) {
    return GetTweakable<TweakableInt>(id);
}

std::shared_ptr<TweakableBool> DevMenu::GetBool(int id) {
    return GetTweakable<TweakableBool>(id);
}

std::shared_ptr<TweakableFolder> DevMenu::GetFolder(int id) {
    return GetTweakable<TweakableFolder>(id);
}

void DevMenu::SaveConfig(const std::string& filename) {
    LOG_INFO("[DevMenu] Saving config to " << filename);
    std::ofstream file(filename);
    if (!file.is_open()) {
        LOG_ERROR("[DevMenu] Failed to open file for saving: " << filename);
        return;
    }

    for (auto& pair : m_tweakableMap) {
        auto item = pair.second;

        switch (item->GetType()) {
        case TweakableType::Float: {
            auto floatItem = std::static_pointer_cast<TweakableFloat>(item);
            file << item->GetId() << " " << floatItem->GetValue() << "\n";
            break;
        }
        case TweakableType::Int: {
            auto intItem = std::static_pointer_cast<TweakableInt>(item);
            file << item->GetId() << " " << intItem->GetValue() << "\n";
            break;
        }
        case TweakableType::Bool: {
            auto boolItem = std::static_pointer_cast<TweakableBool>(item);
            file << item->GetId() << " " << (boolItem->GetValue() ? 1 : 0) << "\n";
            break;
        }
        default:
            break;
        }
    }

    file.close();
    LOG_INFO("[DevMenu] Config saved successfully");
}

void DevMenu::LoadConfig(const std::string& filename) {
    LOG_INFO("[DevMenu] Loading config from " << filename);
    std::ifstream file(filename);
    if (!file.is_open()) {
        LOG_WARNING("[DevMenu] Config file not found: " << filename);
        return;
    }

    int loadedCount = 0;
    std::string line;
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        int id;
        float value;

        if (iss >> id >> value) {
            auto it = m_tweakableMap.find(id);
            if (it != m_tweakableMap.end()) {
                auto item = it->second;

                switch (item->GetType()) {
                case TweakableType::Float: {
                    auto floatItem = std::static_pointer_cast<TweakableFloat>(item);
                    floatItem->SetValue(value);
                    loadedCount++;
                    break;
                }
                case TweakableType::Int: {
                    auto intItem = std::static_pointer_cast<TweakableInt>(item);
                    intItem->SetValue(static_cast<int>(value));
                    loadedCount++;
                    break;
                }
                case TweakableType::Bool: {
                    auto boolItem = std::static_pointer_cast<TweakableBool>(item);
                    boolItem->SetValue(value != 0.0f);
                    loadedCount++;
                    break;
                }
                default:
                    break;
                }
            }
            else {
                LOG_VERBOSE("[DevMenu] Tweakable ID " << id << " not found in map");
            }
        }
    }

    file.close();
    LOG_INFO("[DevMenu] Config loaded: " << loadedCount << " values restored");
}

void DevMenu::RegisterTweakable(std::shared_ptr<TweakableItem> item) {
    m_tweakableMap[item->GetId()] = item;
}

bool DevMenu::PassesFilter(const std::string& name) {
    if (m_searchFilter.empty()) return true;

    std::string lowerName = name;
    std::string lowerFilter = m_searchFilter;

    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
    std::transform(lowerFilter.begin(), lowerFilter.end(), lowerFilter.begin(), ::tolower);

    return lowerName.find(lowerFilter) != std::string::npos;
}

// Initialization Functions - BikeSound
void DevMenu::InitializeBikeSound() {
    auto bikeSound = std::make_shared<TweakableFolder>(1, "BikeSound");

    // Top-level BikeSound parameters
    auto rpmIdle = CreateSyncedFloat(2, "RpmIdle", 2000.0f, 0.0f, 15000.0f);
    auto rpmFull = CreateSyncedFloat(3, "RpmFull", 10000.0f, 0.0f, 20000.0f);

    RegisterTweakable(rpmIdle);
    RegisterTweakable(rpmFull);
    bikeSound->AddChild(rpmIdle);
    bikeSound->AddChild(rpmFull);

    // Gear1 folder
    auto gear1 = std::make_shared<TweakableFolder>(4, "Gear1");
    auto g1_maxRPM = CreateSyncedFloat(5, "MaxRPM", 0.0f, 0.0f, 20000.0f);
    auto g1_min = CreateSyncedFloat(11, "Min", 0.0f, 0.0f, 100.0f);
    auto g1_max = CreateSyncedFloat(12, "Max", 10.0f, 0.0f, 100.0f);
    auto g1_medianRPM = CreateSyncedFloat(17, "MedianRPM", 5000.0f, 0.0f, 15000.0f);
    auto g1_clutchSeek = CreateSyncedFloat(20, "ClutchSeek", 0.1f, 0.0f, 1.0f);
    auto g1_rpmSeek = CreateSyncedFloat(23, "RPMSeek", 0.1f, 0.0f, 1.0f);
    auto g1_rpmSeekUpInAir = CreateSyncedFloat(26, "RPMSeekUpInAir", 0.2f, 0.0f, 1.0f);
    auto g1_rpmSeekDownInAir = CreateSyncedFloat(29, "RPMSeekDownInAir", 0.1f, 0.0f, 1.0f);
    auto g1_loadSeek = CreateSyncedFloat(32, "LoadSeek", 0.2f, 0.0f, 1.0f);
    auto g1_throttleSeek = CreateSyncedFloat(35, "ThrottleSeek", 0.25f, 0.0f, 1.0f);
    auto g1_externalLoadAmount = CreateSyncedFloat(38, "ExternalLoadAmount", 4.5f, 0.0f, 10.0f);
    auto g1_internalLoadAmount = CreateSyncedFloat(41, "InternalLoadAmount", 4.5f, 0.0f, 10.0f);
    auto g1_throttleLoadAmount = CreateSyncedFloat(44, "ThrottleLoadAmount", 0.2f, 0.0f, 5.0f);
    auto g1_shiftNegativeLoadAmount = CreateSyncedFloat(47, "ShiftNegativeLoadAmount", -0.5f, -5.0f, 5.0f);
    auto g1_throttleRpmResponse = CreateSyncedFloat(50, "ThrottleRpmResponse", 1500.0f, 0.0f, 5000.0f);
    auto g1_extraRPMInAir = CreateSyncedFloat(55, "ExtraRPMInAir", 0.0f, 0.0f, 5000.0f);
    auto g1_tractionSeekUp = CreateSyncedFloat(59, "TractionSeekUp", 0.2f, 0.0f, 1.0f);
    auto g1_tractionSeekDown = CreateSyncedFloat(62, "TractionSeekDown", 0.2f, 0.0f, 1.0f);
    auto g1_tractionFrames = CreateSyncedInt(65, "TractionFrames", 10, 0, 100);

    RegisterTweakable(g1_maxRPM);
    RegisterTweakable(g1_min);
    RegisterTweakable(g1_max);
    RegisterTweakable(g1_medianRPM);
    RegisterTweakable(g1_clutchSeek);
    RegisterTweakable(g1_rpmSeek);
    RegisterTweakable(g1_rpmSeekUpInAir);
    RegisterTweakable(g1_rpmSeekDownInAir);
    RegisterTweakable(g1_loadSeek);
    RegisterTweakable(g1_throttleSeek);
    RegisterTweakable(g1_externalLoadAmount);
    RegisterTweakable(g1_internalLoadAmount);
    RegisterTweakable(g1_throttleLoadAmount);
    RegisterTweakable(g1_shiftNegativeLoadAmount);
    RegisterTweakable(g1_throttleRpmResponse);
    RegisterTweakable(g1_extraRPMInAir);
    RegisterTweakable(g1_tractionSeekUp);
    RegisterTweakable(g1_tractionSeekDown);
    RegisterTweakable(g1_tractionFrames);

    gear1->AddChild(g1_maxRPM);
    gear1->AddChild(g1_min);
    gear1->AddChild(g1_max);
    gear1->AddChild(g1_medianRPM);
    gear1->AddChild(g1_clutchSeek);
    gear1->AddChild(g1_rpmSeek);
    gear1->AddChild(g1_rpmSeekUpInAir);
    gear1->AddChild(g1_rpmSeekDownInAir);
    gear1->AddChild(g1_loadSeek);
    gear1->AddChild(g1_throttleSeek);
    gear1->AddChild(g1_externalLoadAmount);
    gear1->AddChild(g1_internalLoadAmount);
    gear1->AddChild(g1_throttleLoadAmount);
    gear1->AddChild(g1_shiftNegativeLoadAmount);
    gear1->AddChild(g1_throttleRpmResponse);
    gear1->AddChild(g1_extraRPMInAir);
    gear1->AddChild(g1_tractionSeekUp);
    gear1->AddChild(g1_tractionSeekDown);
    gear1->AddChild(g1_tractionFrames);

    RegisterTweakable(gear1);
    bikeSound->AddChild(gear1);

    // Gear2 folder
    auto gear2 = std::make_shared<TweakableFolder>(6, "Gear2");
    auto g2_maxRPM = CreateSyncedFloat(7, "MaxRPM", 0.0f, 0.0f, 20000.0f);
    auto g2_min = CreateSyncedFloat(13, "Min", 10.0f, 0.0f, 100.0f);
    auto g2_max = CreateSyncedFloat(14, "Max", 20.0f, 0.0f, 100.0f);
    auto g2_medianRPM = CreateSyncedFloat(18, "MedianRPM", 5000.0f, 0.0f, 15000.0f);
    auto g2_clutchSeek = CreateSyncedFloat(21, "ClutchSeek", 0.1f, 0.0f, 1.0f);
    auto g2_rpmSeek = CreateSyncedFloat(24, "RPMSeek", 0.1f, 0.0f, 1.0f);
    auto g2_rpmSeekUpInAir = CreateSyncedFloat(27, "RPMSeekUpInAir", 0.2f, 0.0f, 1.0f);
    auto g2_rpmSeekDownInAir = CreateSyncedFloat(30, "RPMSeekDownInAir", 0.1f, 0.0f, 1.0f);
    auto g2_loadSeek = CreateSyncedFloat(33, "LoadSeek", 0.2f, 0.0f, 1.0f);
    auto g2_throttleSeek = CreateSyncedFloat(36, "ThrottleSeek", 0.25f, 0.0f, 1.0f);
    auto g2_externalLoadAmount = CreateSyncedFloat(39, "ExternalLoadAmount", 4.5f, 0.0f, 10.0f);
    auto g2_internalLoadAmount = CreateSyncedFloat(42, "InternalLoadAmount", 4.5f, 0.0f, 10.0f);
    auto g2_throttleLoadAmount = CreateSyncedFloat(45, "ThrottleLoadAmount", 0.2f, 0.0f, 5.0f);
    auto g2_shiftNegativeLoadAmount = CreateSyncedFloat(48, "ShiftNegativeLoadAmount", -0.5f, -5.0f, 5.0f);
    auto g2_throttleRpmResponse = CreateSyncedFloat(51, "ThrottleRpmResponse", 1500.0f, 0.0f, 5000.0f);
    auto g2_extraRPMInAir = CreateSyncedFloat(56, "ExtraRPMInAir", 0.0f, 0.0f, 5000.0f);
    auto g2_tractionSeekUp = CreateSyncedFloat(60, "TractionSeekUp", 0.2f, 0.0f, 1.0f);
    auto g2_tractionSeekDown = CreateSyncedFloat(63, "TractionSeekDown", 0.2f, 0.0f, 1.0f);
    auto g2_tractionFrames = CreateSyncedInt(66, "TractionFrames", 10, 0, 100);

    RegisterTweakable(g2_maxRPM);
    RegisterTweakable(g2_min);
    RegisterTweakable(g2_max);
    RegisterTweakable(g2_medianRPM);
    RegisterTweakable(g2_clutchSeek);
    RegisterTweakable(g2_rpmSeek);
    RegisterTweakable(g2_rpmSeekUpInAir);
    RegisterTweakable(g2_rpmSeekDownInAir);
    RegisterTweakable(g2_loadSeek);
    RegisterTweakable(g2_throttleSeek);
    RegisterTweakable(g2_externalLoadAmount);
    RegisterTweakable(g2_internalLoadAmount);
    RegisterTweakable(g2_throttleLoadAmount);
    RegisterTweakable(g2_shiftNegativeLoadAmount);
    RegisterTweakable(g2_throttleRpmResponse);
    RegisterTweakable(g2_extraRPMInAir);
    RegisterTweakable(g2_tractionSeekUp);
    RegisterTweakable(g2_tractionSeekDown);
    RegisterTweakable(g2_tractionFrames);

    gear2->AddChild(g2_maxRPM);
    gear2->AddChild(g2_min);
    gear2->AddChild(g2_max);
    gear2->AddChild(g2_medianRPM);
    gear2->AddChild(g2_clutchSeek);
    gear2->AddChild(g2_rpmSeek);
    gear2->AddChild(g2_rpmSeekUpInAir);
    gear2->AddChild(g2_rpmSeekDownInAir);
    gear2->AddChild(g2_loadSeek);
    gear2->AddChild(g2_throttleSeek);
    gear2->AddChild(g2_externalLoadAmount);
    gear2->AddChild(g2_internalLoadAmount);
    gear2->AddChild(g2_throttleLoadAmount);
    gear2->AddChild(g2_shiftNegativeLoadAmount);
    gear2->AddChild(g2_throttleRpmResponse);
    gear2->AddChild(g2_extraRPMInAir);
    gear2->AddChild(g2_tractionSeekUp);
    gear2->AddChild(g2_tractionSeekDown);
    gear2->AddChild(g2_tractionFrames);

    RegisterTweakable(gear2);
    bikeSound->AddChild(gear2);

    // Gear3 folder
    auto gear3 = std::make_shared<TweakableFolder>(8, "Gear3");
    auto g3_maxRPM = CreateSyncedFloat(9, "MaxRPM", 0.0f, 0.0f, 20000.0f);
    auto g3_min = CreateSyncedFloat(15, "Min", 20.0f, 0.0f, 100.0f);
    auto g3_max = CreateSyncedFloat(16, "Max", 30.0f, 0.0f, 100.0f);
    auto g3_medianRPM = CreateSyncedFloat(19, "MedianRPM", 5000.0f, 0.0f, 15000.0f);
    auto g3_clutchSeek = CreateSyncedFloat(22, "ClutchSeek", 0.1f, 0.0f, 1.0f);
    auto g3_rpmSeek = CreateSyncedFloat(25, "RPMSeek", 0.1f, 0.0f, 1.0f);
    auto g3_rpmSeekUpInAir = CreateSyncedFloat(28, "RPMSeekUpInAir", 0.2f, 0.0f, 1.0f);
    auto g3_rpmSeekDownInAir = CreateSyncedFloat(31, "RPMSeekDownInAir", 0.1f, 0.0f, 1.0f);
    auto g3_loadSeek = CreateSyncedFloat(34, "LoadSeek", 0.2f, 0.0f, 1.0f);
    auto g3_throttleSeek = CreateSyncedFloat(37, "ThrottleSeek", 0.25f, 0.0f, 1.0f);
    auto g3_externalLoadAmount = CreateSyncedFloat(40, "ExternalLoadAmount", 4.5f, 0.0f, 10.0f);
    auto g3_internalLoadAmount = CreateSyncedFloat(43, "InternalLoadAmount", 4.5f, 0.0f, 10.0f);
    auto g3_throttleLoadAmount = CreateSyncedFloat(46, "ThrottleLoadAmount", 0.2f, 0.0f, 5.0f);
    auto g3_shiftNegativeLoadAmount = CreateSyncedFloat(49, "ShiftNegativeLoadAmount", -0.5f, -5.0f, 5.0f);
    auto g3_throttleRpmResponse = CreateSyncedFloat(52, "ThrottleRpmResponse", 1500.0f, 0.0f, 5000.0f);
    auto g3_extraRPMInAir = CreateSyncedFloat(57, "ExtraRPMInAir", 0.0f, 0.0f, 5000.0f);
    auto g3_tractionSeekUp = CreateSyncedFloat(61, "TractionSeekUp", 0.2f, 0.0f, 1.0f);
    auto g3_tractionSeekDown = CreateSyncedFloat(64, "TractionSeekDown", 0.2f, 0.0f, 1.0f);
    auto g3_tractionFrames = CreateSyncedInt(67, "TractionFrames", 10, 0, 100);

    RegisterTweakable(g3_maxRPM);
    RegisterTweakable(g3_min);
    RegisterTweakable(g3_max);
    RegisterTweakable(g3_medianRPM);
    RegisterTweakable(g3_clutchSeek);
    RegisterTweakable(g3_rpmSeek);
    RegisterTweakable(g3_rpmSeekUpInAir);
    RegisterTweakable(g3_rpmSeekDownInAir);
    RegisterTweakable(g3_loadSeek);
    RegisterTweakable(g3_throttleSeek);
    RegisterTweakable(g3_externalLoadAmount);
    RegisterTweakable(g3_internalLoadAmount);
    RegisterTweakable(g3_throttleLoadAmount);
    RegisterTweakable(g3_shiftNegativeLoadAmount);
    RegisterTweakable(g3_throttleRpmResponse);
    RegisterTweakable(g3_extraRPMInAir);
    RegisterTweakable(g3_tractionSeekUp);
    RegisterTweakable(g3_tractionSeekDown);
    RegisterTweakable(g3_tractionFrames);

    gear3->AddChild(g3_maxRPM);
    gear3->AddChild(g3_min);
    gear3->AddChild(g3_max);
    gear3->AddChild(g3_medianRPM);
    gear3->AddChild(g3_clutchSeek);
    gear3->AddChild(g3_rpmSeek);
    gear3->AddChild(g3_rpmSeekUpInAir);
    gear3->AddChild(g3_rpmSeekDownInAir);
    gear3->AddChild(g3_loadSeek);
    gear3->AddChild(g3_throttleSeek);
    gear3->AddChild(g3_externalLoadAmount);
    gear3->AddChild(g3_internalLoadAmount);
    gear3->AddChild(g3_throttleLoadAmount);
    gear3->AddChild(g3_shiftNegativeLoadAmount);
    gear3->AddChild(g3_throttleRpmResponse);
    gear3->AddChild(g3_extraRPMInAir);
    gear3->AddChild(g3_tractionSeekUp);
    gear3->AddChild(g3_tractionSeekDown);
    gear3->AddChild(g3_tractionFrames);

    RegisterTweakable(gear3);
    bikeSound->AddChild(gear3);

    // Additional BikeSound parameters
    auto gearCount = CreateSyncedInt(10, "GearCount", 2, 1, 6);
    auto shiftDuration = CreateSyncedFloat(53, "ShiftDuration", 0.5f, 0.0f, 2.0f);
    auto cameraDebugRadius = CreateSyncedFloat(54, "CameraDebugRadius", 0.0f, 0.0f, 10.0f);
    auto numberOfFramesInAirRequired = CreateSyncedInt(58, "NumberOfFramesInAirRequired", 8, 0, 60);
    auto gainTire = CreateSyncedFloat(237, "GainTire", 1.0f, 0.0f, 5.0f);
    auto gainEngine = CreateSyncedFloat(238, "GainEngine", 1.0f, 0.0f, 5.0f);
    auto gainWind = CreateSyncedFloat(239, "GainWind", 1.0f, 0.0f, 5.0f);
    auto overrideTireSpeed = CreateSyncedFloat(240, "OverrideTireSpeed", 0.0f, 0.0f, 100.0f);
    auto printTerrainSoundMaterial = CreateSyncedBool(241, "PrintTerrainSoundMaterial", false);
    auto debugFmod = CreateSyncedBool(242, "DebugFmod", true);
    auto skidSoundDelay = CreateSyncedInt(254, "skidSoundDelay", 16, 0, 100);
    auto bodyCollisionSoundDelay = CreateSyncedInt(255, "bodyCollisionSoundDelay", 10, 0, 100);
    auto bikeCollisionSoundDelay = CreateSyncedInt(256, "bikeCollisionSoundDelay", 16, 0, 100);
    auto wheelCollisionSoundDelay = CreateSyncedInt(257, "wheelCollisionSoundDelay", 0, 0, 100);
    auto minimumCollisionSoundForce = CreateSyncedFloat(262, "MinimumCollisionSoundForce", 0.5f, 0.0f, 20.0f);
    auto maximumCollisionSoundForce = CreateSyncedFloat(263, "MaximumCollisionSoundForce", 8.5f, 0.0f, 50.0f);
    auto gainRolling = CreateSyncedFloat(342, "GainRolling", 1.0f, 0.0f, 5.0f);

    RegisterTweakable(gearCount);
    RegisterTweakable(shiftDuration);
    RegisterTweakable(cameraDebugRadius);
    RegisterTweakable(numberOfFramesInAirRequired);
    RegisterTweakable(gainTire);
    RegisterTweakable(gainEngine);
    RegisterTweakable(gainWind);
    RegisterTweakable(overrideTireSpeed);
    RegisterTweakable(printTerrainSoundMaterial);
    RegisterTweakable(debugFmod);
    RegisterTweakable(skidSoundDelay);
    RegisterTweakable(bodyCollisionSoundDelay);
    RegisterTweakable(bikeCollisionSoundDelay);
    RegisterTweakable(wheelCollisionSoundDelay);
    RegisterTweakable(minimumCollisionSoundForce);
    RegisterTweakable(maximumCollisionSoundForce);
    RegisterTweakable(gainRolling);

    bikeSound->AddChild(gearCount);
    bikeSound->AddChild(shiftDuration);
    bikeSound->AddChild(cameraDebugRadius);
    bikeSound->AddChild(numberOfFramesInAirRequired);
    bikeSound->AddChild(gainTire);
    bikeSound->AddChild(gainEngine);
    bikeSound->AddChild(gainWind);
    bikeSound->AddChild(overrideTireSpeed);
    bikeSound->AddChild(printTerrainSoundMaterial);
    bikeSound->AddChild(debugFmod);
    bikeSound->AddChild(skidSoundDelay);
    bikeSound->AddChild(bodyCollisionSoundDelay);
    bikeSound->AddChild(bikeCollisionSoundDelay);
    bikeSound->AddChild(wheelCollisionSoundDelay);
    bikeSound->AddChild(minimumCollisionSoundForce);
    bikeSound->AddChild(maximumCollisionSoundForce);
    bikeSound->AddChild(gainRolling);

    RegisterTweakable(bikeSound);
    m_rootFolders.push_back(bikeSound);
}

void DevMenu::InitializeDynamicMusic() {
    auto dynamicMusic = std::make_shared<TweakableFolder>(68, "DynamicMusic");

    auto progress = CreateSyncedFloat(69, "Progress", 0.0f, 0.0f, 1.0f);
    auto maxProgressAuto = CreateSyncedFloat(70, "MaxProgressAuto", 0.99f, 0.0f, 1.0f);
    auto leftFront = CreateSyncedFloat(71, "LeftFront", 0.92f, 0.0f, 1.0f);
    auto rightFront = CreateSyncedFloat(72, "RightFront", 0.92f, 0.0f, 1.0f);
    auto leftRear = CreateSyncedFloat(73, "LeftRear", 0.68f, 0.0f, 1.0f);
    auto rightRear = CreateSyncedFloat(74, "RightRear", 0.68f, 0.0f, 1.0f);
    auto leftSide = CreateSyncedFloat(75, "LeftSide", 0.5f, 0.0f, 1.0f);
    auto rightSide = CreateSyncedFloat(76, "RightSide", 0.5f, 0.0f, 1.0f);
    auto center = CreateSyncedFloat(77, "Center", 0.0f, 0.0f, 1.0f);
    auto lowFrequencyEmitter = CreateSyncedFloat(78, "LowFrequencyEmitter", 0.8f, 0.0f, 1.0f);

    RegisterTweakable(progress);
    RegisterTweakable(maxProgressAuto);
    RegisterTweakable(leftFront);
    RegisterTweakable(rightFront);
    RegisterTweakable(leftRear);
    RegisterTweakable(rightRear);
    RegisterTweakable(leftSide);
    RegisterTweakable(rightSide);
    RegisterTweakable(center);
    RegisterTweakable(lowFrequencyEmitter);

    dynamicMusic->AddChild(progress);
    dynamicMusic->AddChild(maxProgressAuto);
    dynamicMusic->AddChild(leftFront);
    dynamicMusic->AddChild(rightFront);
    dynamicMusic->AddChild(leftRear);
    dynamicMusic->AddChild(rightRear);
    dynamicMusic->AddChild(leftSide);
    dynamicMusic->AddChild(rightSide);
    dynamicMusic->AddChild(center);
    dynamicMusic->AddChild(lowFrequencyEmitter);

    RegisterTweakable(dynamicMusic);
    m_rootFolders.push_back(dynamicMusic);
}

void DevMenu::InitializeProgressionSystem() {
    auto progressionSystem = std::make_shared<TweakableFolder>(79, "ProgressionSystem");

    auto allBikesUnlocked = CreateSyncedBool(80, "AllBikesUnlocked", true);
    auto fmxTricksUnlocked = CreateSyncedBool(344, "FMXTricksUnlocked", true);
    auto allTracksUnlocked = CreateSyncedBool(491, "AllTracksUnlocked", true);

    RegisterTweakable(allBikesUnlocked);
    RegisterTweakable(fmxTricksUnlocked);
    RegisterTweakable(allTracksUnlocked);

    progressionSystem->AddChild(allBikesUnlocked);
    progressionSystem->AddChild(fmxTricksUnlocked);
    progressionSystem->AddChild(allTracksUnlocked);

    RegisterTweakable(progressionSystem);
    m_rootFolders.push_back(progressionSystem);
}

void DevMenu::InitializeGarage() {
    auto garage = std::make_shared<TweakableFolder>(81, "Garage");

    // Camera folder
    auto camera = std::make_shared<TweakableFolder>(82, "Camera");

    // Bike subfolder
    auto bike = std::make_shared<TweakableFolder>(83, "Bike");
    auto bike_shift = CreateSyncedFloat(84, "Shift", 1.04f, 0.0f, 10.0f);
    auto bike_rotShift = CreateSyncedFloat(85, "RotationShift", 1.97f, 0.0f, 10.0f);
    auto bike_rotX = CreateSyncedFloat(86, "CameraRotX", 3.14159f, -10.0f, 10.0f);
    auto bike_rotY = CreateSyncedFloat(87, "CameraRotY", 0.0f, -10.0f, 10.0f);
    auto bike_zoomDist = CreateSyncedFloat(88, "ZoomOutDistance", -5.0f, -50.0f, 50.0f);
    auto bike_zoomHeight = CreateSyncedFloat(89, "ZoomOutHeight", 1.5f, -10.0f, 10.0f);
    auto bike_zoomShift = CreateSyncedFloat(90, "ZoomOutShift", 1.0f, 0.0f, 10.0f);
    auto bike_minZoom = CreateSyncedFloat(91, "MinZoomCameraDistance", 1.6f, 0.0f, 20.0f);
    auto bike_maxZoom = CreateSyncedFloat(92, "MaxZoomCameraDistance", 6.0f, 0.0f, 20.0f);
    auto bike_camDist = CreateSyncedFloat(93, "CameraDistance", 3.0f, 0.0f, 20.0f);
    auto bike_camRotDist = CreateSyncedFloat(94, "CameraRotationDistance", 4.5f, 0.0f, 20.0f);
    auto bike_camHeight = CreateSyncedFloat(95, "CameraHeight", 0.75f, -10.0f, 10.0f);

    RegisterTweakable(bike_shift);
    RegisterTweakable(bike_rotShift);
    RegisterTweakable(bike_rotX);
    RegisterTweakable(bike_rotY);
    RegisterTweakable(bike_zoomDist);
    RegisterTweakable(bike_zoomHeight);
    RegisterTweakable(bike_zoomShift);
    RegisterTweakable(bike_minZoom);
    RegisterTweakable(bike_maxZoom);
    RegisterTweakable(bike_camDist);
    RegisterTweakable(bike_camRotDist);
    RegisterTweakable(bike_camHeight);

    bike->AddChild(bike_shift);
    bike->AddChild(bike_rotShift);
    bike->AddChild(bike_rotX);
    bike->AddChild(bike_rotY);
    bike->AddChild(bike_zoomDist);
    bike->AddChild(bike_zoomHeight);
    bike->AddChild(bike_zoomShift);
    bike->AddChild(bike_minZoom);
    bike->AddChild(bike_maxZoom);
    bike->AddChild(bike_camDist);
    bike->AddChild(bike_camRotDist);
    bike->AddChild(bike_camHeight);

    RegisterTweakable(bike);
    camera->AddChild(bike);

    // Rider subfolder
    auto rider = std::make_shared<TweakableFolder>(96, "Rider");
    auto rider_shift = CreateSyncedFloat(97, "Shift", 1.45f, 0.0f, 10.0f);
    auto rider_aspect = CreateSyncedFloat(98, "Aspect4x3", 1.2f, 0.0f, 5.0f);
    auto rider_shiftHelmet = CreateSyncedFloat(99, "ShiftHelmet", 0.3f, 0.0f, 5.0f);
    auto rider_shiftTop = CreateSyncedFloat(100, "ShiftTop", 0.41f, 0.0f, 5.0f);
    auto rider_shiftBottom = CreateSyncedFloat(101, "ShiftBottom", 0.3f, 0.0f, 5.0f);
    auto rider_rotShift = CreateSyncedFloat(102, "RotationShift", 2.43f, 0.0f, 10.0f);
    auto rider_rotShiftHelmet = CreateSyncedFloat(103, "RotationShiftHelmet", 0.52f, 0.0f, 5.0f);
    auto rider_rotShiftTop = CreateSyncedFloat(104, "RotationShiftTop", 1.08f, 0.0f, 5.0f);
    auto rider_rotShiftBottom = CreateSyncedFloat(105, "RotationShiftBottom", 1.0f, 0.0f, 5.0f);
    auto rider_camRotX = CreateSyncedFloat(106, "CameraRotX", -1.78f, -10.0f, 10.0f);
    auto rider_camRotY = CreateSyncedFloat(107, "CameraRotY", 0.0f, -10.0f, 10.0f);
    auto rider_zoomDist = CreateSyncedFloat(108, "ZoomOutDistance", -5.0f, -50.0f, 50.0f);
    auto rider_zoomHeight = CreateSyncedFloat(109, "ZoomOutHeight", 1.0f, -10.0f, 10.0f);
    auto rider_zoomShift = CreateSyncedFloat(110, "ZoomOutShift", 2.2f, 0.0f, 10.0f);
    auto rider_camDist = CreateSyncedFloat(111, "CameraDistance", 3.0f, 0.0f, 20.0f);
    auto rider_camRotDist = CreateSyncedFloat(112, "CameraRotationDistance", 4.5f, 0.0f, 20.0f);
    auto rider_camDistHelmet = CreateSyncedFloat(113, "CameraDistanceHelmet", 1.0f, 0.0f, 10.0f);
    auto rider_camDistTop = CreateSyncedFloat(114, "CameraDistanceTop", 1.45f, 0.0f, 10.0f);
    auto rider_camDistBottom = CreateSyncedFloat(115, "CameraDistanceBottom", 1.4f, 0.0f, 10.0f);
    auto rider_camRotDistHelmet = CreateSyncedFloat(116, "CameraRotationDistanceHelmet", 1.8f, 0.0f, 10.0f);
    auto rider_camRotDistTop = CreateSyncedFloat(117, "CameraRotationDistanceTop", 2.5f, 0.0f, 10.0f);
    auto rider_camRotDistBottom = CreateSyncedFloat(118, "CameraRotationDistanceBottom", 2.5f, 0.0f, 10.0f);
    auto rider_minZoomRotDist = CreateSyncedFloat(119, "MinZoomCameraRotationDistance", 3.0f, 0.0f, 10.0f);
    auto rider_maxZoomRotDist = CreateSyncedFloat(120, "MaxZoomCameraRotationDistance", 4.0f, 0.0f, 10.0f);
    auto rider_minZoomRotDistHelmet = CreateSyncedFloat(121, "MinZoomCameraRotationDistanceHelmet", 1.0f, 0.0f, 10.0f);
    auto rider_maxZoomRotDistHelmet = CreateSyncedFloat(122, "MaxZoomCameraRotationDistanceHelmet", 1.8f, 0.0f, 10.0f);
    auto rider_minZoomRotDistTop = CreateSyncedFloat(123, "MinZoomCameraRotationDistanceTop", 1.2f, 0.0f, 10.0f);
    auto rider_maxZoomRotDistTop = CreateSyncedFloat(124, "MaxZoomCameraRotationDistanceTop", 2.2f, 0.0f, 10.0f);
    auto rider_minZoomRotDistBottom = CreateSyncedFloat(125, "MinZoomCameraRotationDistanceBottom", 1.2f, 0.0f, 10.0f);
    auto rider_maxZoomRotDistBottom = CreateSyncedFloat(126, "MaxZoomCameraRotationDistanceBottom", 2.2f, 0.0f, 10.0f);
    auto rider_camHeight = CreateSyncedFloat(127, "CameraHeight", 1.0f, -10.0f, 10.0f);
    auto rider_camHeightHelmet = CreateSyncedFloat(128, "CameraHeightHelmet", 0.02f, -5.0f, 5.0f);
    auto rider_camHeightTop = CreateSyncedFloat(129, "CameraHeightTop", 0.01f, -5.0f, 5.0f);
    auto rider_camHeightBottom = CreateSyncedFloat(130, "CameraHeightBottom", -0.09f, -5.0f, 5.0f);

    RegisterTweakable(rider_shift);
    RegisterTweakable(rider_aspect);
    RegisterTweakable(rider_shiftHelmet);
    RegisterTweakable(rider_shiftTop);
    RegisterTweakable(rider_shiftBottom);
    RegisterTweakable(rider_rotShift);
    RegisterTweakable(rider_rotShiftHelmet);
    RegisterTweakable(rider_rotShiftTop);
    RegisterTweakable(rider_rotShiftBottom);
    RegisterTweakable(rider_camRotX);
    RegisterTweakable(rider_camRotY);
    RegisterTweakable(rider_zoomDist);
    RegisterTweakable(rider_zoomHeight);
    RegisterTweakable(rider_zoomShift);
    RegisterTweakable(rider_camDist);
    RegisterTweakable(rider_camRotDist);
    RegisterTweakable(rider_camDistHelmet);
    RegisterTweakable(rider_camDistTop);
    RegisterTweakable(rider_camDistBottom);
    RegisterTweakable(rider_camRotDistHelmet);
    RegisterTweakable(rider_camRotDistTop);
    RegisterTweakable(rider_camRotDistBottom);
    RegisterTweakable(rider_minZoomRotDist);
    RegisterTweakable(rider_maxZoomRotDist);
    RegisterTweakable(rider_minZoomRotDistHelmet);
    RegisterTweakable(rider_maxZoomRotDistHelmet);
    RegisterTweakable(rider_minZoomRotDistTop);
    RegisterTweakable(rider_maxZoomRotDistTop);
    RegisterTweakable(rider_minZoomRotDistBottom);
    RegisterTweakable(rider_maxZoomRotDistBottom);
    RegisterTweakable(rider_camHeight);
    RegisterTweakable(rider_camHeightHelmet);
    RegisterTweakable(rider_camHeightTop);
    RegisterTweakable(rider_camHeightBottom);

    rider->AddChild(rider_shift);
    rider->AddChild(rider_aspect);
    rider->AddChild(rider_shiftHelmet);
    rider->AddChild(rider_shiftTop);
    rider->AddChild(rider_shiftBottom);
    rider->AddChild(rider_rotShift);
    rider->AddChild(rider_rotShiftHelmet);
    rider->AddChild(rider_rotShiftTop);
    rider->AddChild(rider_rotShiftBottom);
    rider->AddChild(rider_camRotX);
    rider->AddChild(rider_camRotY);
    rider->AddChild(rider_zoomDist);
    rider->AddChild(rider_zoomHeight);
    rider->AddChild(rider_zoomShift);
    rider->AddChild(rider_camDist);
    rider->AddChild(rider_camRotDist);
    rider->AddChild(rider_camDistHelmet);
    rider->AddChild(rider_camDistTop);
    rider->AddChild(rider_camDistBottom);
    rider->AddChild(rider_camRotDistHelmet);
    rider->AddChild(rider_camRotDistTop);
    rider->AddChild(rider_camRotDistBottom);
    rider->AddChild(rider_minZoomRotDist);
    rider->AddChild(rider_maxZoomRotDist);
    rider->AddChild(rider_minZoomRotDistHelmet);
    rider->AddChild(rider_maxZoomRotDistHelmet);
    rider->AddChild(rider_minZoomRotDistTop);
    rider->AddChild(rider_maxZoomRotDistTop);
    rider->AddChild(rider_minZoomRotDistBottom);
    rider->AddChild(rider_maxZoomRotDistBottom);
    rider->AddChild(rider_camHeight);
    rider->AddChild(rider_camHeightHelmet);
    rider->AddChild(rider_camHeightTop);
    rider->AddChild(rider_camHeightBottom);

    RegisterTweakable(rider);
    camera->AddChild(rider);

    // Camera-level parameters
    auto cam_interp = CreateSyncedFloat(480, "Interpolation", 0.06f, 0.0f, 1.0f);
    auto cam_rotSpeed = CreateSyncedFloat(481, "RotationSpeed", 0.05f, 0.0f, 1.0f);
    auto cam_rotSpeedMouse = CreateSyncedFloat(482, "RotationSpeedMouse", 0.25f, 0.0f, 2.0f);

    RegisterTweakable(cam_interp);
    RegisterTweakable(cam_rotSpeed);
    RegisterTweakable(cam_rotSpeedMouse);
    camera->AddChild(cam_interp);
    camera->AddChild(cam_rotSpeed);
    camera->AddChild(cam_rotSpeedMouse);

    RegisterTweakable(camera);
    garage->AddChild(camera);

    // Garage-level parameters
    auto dynamicDof = CreateSyncedBool(470, "DynamicDof", true);
    auto dofNearBlur = CreateSyncedFloat(471, "DofNearBlur", 0.1f, 0.0f, 5.0f);
    auto dofFarBlur = CreateSyncedFloat(472, "DofFarBlur", 1.5f, 0.0f, 10.0f);
    auto dofFarBlurStart = CreateSyncedFloat(473, "DofFarBlurStart", 0.0f, 0.0f, 50.0f);
    auto dofFarBlurEnd = CreateSyncedFloat(474, "DofFarBlurEnd", 12.0f, 0.0f, 50.0f);
    auto lumValue = CreateSyncedFloat(475, "LuminanceValue", 0.5f, 0.0f, 2.0f);
    auto lumBlend = CreateSyncedFloat(476, "LuminanceBlend", 0.5f, 0.0f, 1.0f);
    auto sunAngle = CreateSyncedInt(477, "SunAngle", 10, 0, 360);
    auto sunIntensity = CreateSyncedInt(478, "SunIntensity", 255, 0, 255);
    auto longitude = CreateSyncedInt(479, "Longitude", 50, 0, 360);

    RegisterTweakable(dynamicDof);
    RegisterTweakable(dofNearBlur);
    RegisterTweakable(dofFarBlur);
    RegisterTweakable(dofFarBlurStart);
    RegisterTweakable(dofFarBlurEnd);
    RegisterTweakable(lumValue);
    RegisterTweakable(lumBlend);
    RegisterTweakable(sunAngle);
    RegisterTweakable(sunIntensity);
    RegisterTweakable(longitude);

    garage->AddChild(dynamicDof);
    garage->AddChild(dofNearBlur);
    garage->AddChild(dofFarBlur);
    garage->AddChild(dofFarBlurStart);
    garage->AddChild(dofFarBlurEnd);
    garage->AddChild(lumValue);
    garage->AddChild(lumBlend);
    garage->AddChild(sunAngle);
    garage->AddChild(sunIntensity);
    garage->AddChild(longitude);

    // Lights subfolder
    auto lights = std::make_shared<TweakableFolder>(483, "Lights");
    auto bikeSpotIntensity = CreateSyncedFloat(484, "BikeSpotIntensity", 20.0f, 0.0f, 100.0f);
    RegisterTweakable(bikeSpotIntensity);
    lights->AddChild(bikeSpotIntensity);
    RegisterTweakable(lights);
    garage->AddChild(lights);

    // Animation subfolder
    auto animation = std::make_shared<TweakableFolder>(485, "Animation");
    auto usePodiumAnims = CreateSyncedBool(486, "UsePodiumAnimations", false);
    auto activePodiumAnim = CreateSyncedInt(487, "ActivePodiumAnimation", 1, 0, 10);
    RegisterTweakable(usePodiumAnims);
    RegisterTweakable(activePodiumAnim);
    animation->AddChild(usePodiumAnims);
    animation->AddChild(activePodiumAnim);
    RegisterTweakable(animation);
    garage->AddChild(animation);

    RegisterTweakable(garage);
    m_rootFolders.push_back(garage);
}


void DevMenu::InitializeContentPack() {
    auto contentPack = std::make_shared<TweakableFolder>(131, "ContentPack");

    // Add all ContentPack tweakables
    auto enableAdvertEventsForContentPacks = CreateSyncedBool(132, "Enable Advert events for content packs", true);
    auto owned = CreateSyncedBool(133, "Owned", false);
    auto comingSoon = CreateSyncedBool(134, "Coming Soon", true);

    RegisterTweakable(enableAdvertEventsForContentPacks);
    RegisterTweakable(owned);
    RegisterTweakable(comingSoon);

    contentPack->AddChild(enableAdvertEventsForContentPacks);
    contentPack->AddChild(owned);
    contentPack->AddChild(comingSoon);

    RegisterTweakable(contentPack);
    m_rootFolders.push_back(contentPack);
}

void DevMenu::InitializeDLC() {
    auto dlc = std::make_shared<TweakableFolder>(135, "DLC");

    // Add all DLC tweakables
    auto conflictChecksEnabled = CreateSyncedBool(136, "ConflictChecksEnabled", true);

    RegisterTweakable(conflictChecksEnabled);

    dlc->AddChild(conflictChecksEnabled);

    RegisterTweakable(dlc);
    m_rootFolders.push_back(dlc);
}

void DevMenu::InitializeEditor() {
    auto editor = std::make_shared<TweakableFolder>(137, "Editor");

    // Top-level Editor tweakables
    auto saveLastUndo = CreateSyncedBool(138, "SaveLastUndo", true);
    auto saveTrackEnvironmentSettings = CreateSyncedBool(139, "SaveTrackEnvironmentSettings", false);
    auto printCurrentCameraLocation = CreateSyncedBool(140, "PrintCurrentCameraLocation", true);

    RegisterTweakable(saveLastUndo);
    RegisterTweakable(saveTrackEnvironmentSettings);
    RegisterTweakable(printCurrentCameraLocation);

    editor->AddChild(saveLastUndo);
    editor->AddChild(saveTrackEnvironmentSettings);
    editor->AddChild(printCurrentCameraLocation);

    // Features subfolder
    auto features = std::make_shared<TweakableFolder>(141, "Features");
    auto enableDrivingLineSplitting = CreateSyncedBool(142, "EnableDrivingLineSplitting", true);
    auto enableDrivingLineEvent = CreateSyncedBool(596, "EnableDrivingLineEvent", true);

    RegisterTweakable(enableDrivingLineSplitting);
    RegisterTweakable(enableDrivingLineEvent);

    features->AddChild(enableDrivingLineSplitting);
    features->AddChild(enableDrivingLineEvent);

    RegisterTweakable(features);
    editor->AddChild(features);

    // Additional Editor tweakables
    auto flipDirectionHeightAdjustment = CreateSyncedFloat(149, "FlipDirectionHeightAdjustment", 0.072f, 0.0f, 1.0f);
    auto fakeBikeAmount = CreateSyncedInt(274, "FakeBikeAmount", 7, 0, 100);
    auto simulatedDelayWithGhostsInEditor = CreateSyncedInt(275, "SimulatedDelayWithGhostsInEditor", 400, 0, 5000);
    auto loadHighEndLayer = CreateSyncedBool(353, "LoadHighEndLayer", true);
    auto cloudShadowTiling = CreateSyncedFloat(369, "CloudShadowTiling", 0.0025f, 0.0f, 1.0f);
    auto edgeCollisionFactorMul = CreateSyncedFloat(370, "EdgeCollisionFactorMul", 1.0f, 0.0f, 10.0f);
    auto edgeCollisionNormalPow = CreateSyncedFloat(371, "EdgeCollisionNormalPow", 0.2f, 0.0f, 5.0f);

    RegisterTweakable(flipDirectionHeightAdjustment);
    RegisterTweakable(fakeBikeAmount);
    RegisterTweakable(simulatedDelayWithGhostsInEditor);
    RegisterTweakable(loadHighEndLayer);
    RegisterTweakable(cloudShadowTiling);
    RegisterTweakable(edgeCollisionFactorMul);
    RegisterTweakable(edgeCollisionNormalPow);

    editor->AddChild(flipDirectionHeightAdjustment);
    editor->AddChild(fakeBikeAmount);
    editor->AddChild(simulatedDelayWithGhostsInEditor);
    editor->AddChild(loadHighEndLayer);
    editor->AddChild(cloudShadowTiling);
    editor->AddChild(edgeCollisionFactorMul);
    editor->AddChild(edgeCollisionNormalPow);

    // Overlays subfolder
    auto overlays = std::make_shared<TweakableFolder>(597, "Overlays");
    auto maxCount = CreateSyncedInt(598, "MaxCount", 50, 0, 1000);
    auto maxDistance = CreateSyncedInt(599, "MaxDistance", 50, 0, 1000);
    auto drawOverlayTexts = CreateSyncedBool(600, "DrawOverlayTexts", true);

    RegisterTweakable(maxCount);
    RegisterTweakable(maxDistance);
    RegisterTweakable(drawOverlayTexts);

    overlays->AddChild(maxCount);
    overlays->AddChild(maxDistance);
    overlays->AddChild(drawOverlayTexts);

    RegisterTweakable(overlays);
    editor->AddChild(overlays);

    RegisterTweakable(editor);
    m_rootFolders.push_back(editor);
}

void DevMenu::InitializeMultiplayer() {
    auto multiplayer = std::make_shared<TweakableFolder>(143, "Multiplayer");

    // Top-level Multiplayer tweakables
    auto showMaxPodiumRiders = CreateSyncedBool(144, "ShowMaxPodiumRiders", false);
    auto mpBoosterMaxPower = CreateSyncedFloat(264, "MP_BOOSTER_MAX_POWER", 300.0f, 0.0f, 1000.0f);
    auto enableSwitchFollowedPlayerWhileDriving = CreateSyncedBool(265, "EnableSwitchFollowedPlayerWhileDriving", false);
    auto resetCooldownTicks = CreateSyncedInt(266, "ResetCooldownTicks", 30, 0, 1000);
    auto fakeBikeTrialsDelayTicks = CreateSyncedInt(267, "FakeBikeTrialsDelayTicks", 3, 0, 100);
    auto hiddenRemoteBikeAmount = CreateSyncedInt(276, "HiddenRemoteBikeAmount", 0, 0, 100);
    auto refreshRate = CreateSyncedInt(277, "RefreshRate", 17, 0, 120);
    auto trialsDrivingLineAmount = CreateSyncedInt(278, "TrialsDrivingLineAmount", 1, 0, 100);
    auto followedUpdateMinIntervalTicks = CreateSyncedInt(279, "FollowedUpdateMinIntervalTicks", 120, 0, 1000);
    auto updateIngamePlayerList = CreateSyncedInt(343, "UpdateIngamePlayerList", 61, 0, 1000);
    auto xCrossShowPodiumAfterEachTrack = CreateSyncedBool(350, "XCrossShowPodiumAfterEachTrack", false);
    auto fmxBoostForce = CreateSyncedFloat(433, "FMXBoostForce", 500.0f, 0.0f, 2000.0f);
    auto fmxBoostLengthFactor = CreateSyncedFloat(434, "FMXBoostLengthFactor", 0.00015f, 0.0f, 0.01f);
    auto disconnectOnOutOfSync = CreateSyncedBool(489, "DisconnectOnOutOfSync", true);
    auto ghostFadeOutDistance = CreateSyncedFloat(490, "GhostFadeOutDistance", 25.0f, 0.0f, 200.0f);

    RegisterTweakable(showMaxPodiumRiders);
    RegisterTweakable(mpBoosterMaxPower);
    RegisterTweakable(enableSwitchFollowedPlayerWhileDriving);
    RegisterTweakable(resetCooldownTicks);
    RegisterTweakable(fakeBikeTrialsDelayTicks);
    RegisterTweakable(hiddenRemoteBikeAmount);
    RegisterTweakable(refreshRate);
    RegisterTweakable(trialsDrivingLineAmount);
    RegisterTweakable(followedUpdateMinIntervalTicks);
    RegisterTweakable(updateIngamePlayerList);
    RegisterTweakable(xCrossShowPodiumAfterEachTrack);
    RegisterTweakable(fmxBoostForce);
    RegisterTweakable(fmxBoostLengthFactor);
    RegisterTweakable(disconnectOnOutOfSync);
    RegisterTweakable(ghostFadeOutDistance);

    multiplayer->AddChild(showMaxPodiumRiders);
    multiplayer->AddChild(mpBoosterMaxPower);
    multiplayer->AddChild(enableSwitchFollowedPlayerWhileDriving);
    multiplayer->AddChild(resetCooldownTicks);
    multiplayer->AddChild(fakeBikeTrialsDelayTicks);
    multiplayer->AddChild(hiddenRemoteBikeAmount);
    multiplayer->AddChild(refreshRate);
    multiplayer->AddChild(trialsDrivingLineAmount);
    multiplayer->AddChild(followedUpdateMinIntervalTicks);
    multiplayer->AddChild(updateIngamePlayerList);
    multiplayer->AddChild(xCrossShowPodiumAfterEachTrack);
    multiplayer->AddChild(fmxBoostForce);
    multiplayer->AddChild(fmxBoostLengthFactor);
    multiplayer->AddChild(disconnectOnOutOfSync);
    multiplayer->AddChild(ghostFadeOutDistance);

    // ParameterDefaults subfolder
    auto parameterDefaults = std::make_shared<TweakableFolder>(571, "ParameterDefaults");
    auto heatsPerRace = CreateSyncedInt(572, "HeatsPerRace", 2, 0, 20);
    auto bailoutFinish = CreateSyncedBool(573, "BailoutFinish", false);
    auto fmxTricksEnabled = CreateSyncedBool(574, "FMXTricksEnabled", true);
    auto fmxTricksBoost = CreateSyncedInt(575, "FMXTricksBoost", 0, 0, 100);
    auto noLean = CreateSyncedBool(576, "NoLean", false);
    auto fullThrottle = CreateSyncedBool(577, "FullThrottle", false);
    auto invertControls = CreateSyncedBool(578, "InvertControls", false);
    auto bikeSpeed = CreateSyncedInt(579, "BikeSpeed", 100, 0, 500);
    auto gravity = CreateSyncedInt(580, "Gravity", 100, 0, 500);
    auto invisibleRider = CreateSyncedBool(581, "InvisibleRider", false);
    auto invisibleBike = CreateSyncedBool(582, "InvisibleBike", false);
    auto removeCheckpoints = CreateSyncedBool(583, "RemoveCheckpoints", false);
    auto endingTimeLimit = CreateSyncedInt(584, "EndingTimeLimit", 30, 0, 300);
    auto burningBike = CreateSyncedBool(585, "BurningBike", false);
    auto wheelieMode = CreateSyncedInt(586, "WheelieMode", 0, 0, 10);
    auto lives = CreateSyncedInt(587, "Lives", 500, 0, 10000);
    auto baggieAllowed = CreateSyncedBool(588, "BaggieAllowed", false);
    auto roachAllowed = CreateSyncedBool(589, "RoachAllowed", true);
    auto pitViperAllowed = CreateSyncedBool(590, "PitViperAllowed", true);
    auto foxbatAllowed = CreateSyncedBool(591, "FoxbatAllowed", true);
    auto quadAllowed = CreateSyncedBool(592, "QuadAllowed", true);
    auto bmxAllowed = CreateSyncedBool(593, "BMXAllowed", true);
    auto donkeyAllowed = CreateSyncedBool(594, "DonkeyAllowed", true);
    auto unicornAllowed = CreateSyncedBool(595, "UnicornAllowed", true);

    RegisterTweakable(heatsPerRace);
    RegisterTweakable(bailoutFinish);
    RegisterTweakable(fmxTricksEnabled);
    RegisterTweakable(fmxTricksBoost);
    RegisterTweakable(noLean);
    RegisterTweakable(fullThrottle);
    RegisterTweakable(invertControls);
    RegisterTweakable(bikeSpeed);
    RegisterTweakable(gravity);
    RegisterTweakable(invisibleRider);
    RegisterTweakable(invisibleBike);
    RegisterTweakable(removeCheckpoints);
    RegisterTweakable(endingTimeLimit);
    RegisterTweakable(burningBike);
    RegisterTweakable(wheelieMode);
    RegisterTweakable(lives);
    RegisterTweakable(baggieAllowed);
    RegisterTweakable(roachAllowed);
    RegisterTweakable(pitViperAllowed);
    RegisterTweakable(foxbatAllowed);
    RegisterTweakable(quadAllowed);
    RegisterTweakable(bmxAllowed);
    RegisterTweakable(donkeyAllowed);
    RegisterTweakable(unicornAllowed);

    parameterDefaults->AddChild(heatsPerRace);
    parameterDefaults->AddChild(bailoutFinish);
    parameterDefaults->AddChild(fmxTricksEnabled);
    parameterDefaults->AddChild(fmxTricksBoost);
    parameterDefaults->AddChild(noLean);
    parameterDefaults->AddChild(fullThrottle);
    parameterDefaults->AddChild(invertControls);
    parameterDefaults->AddChild(bikeSpeed);
    parameterDefaults->AddChild(gravity);
    parameterDefaults->AddChild(invisibleRider);
    parameterDefaults->AddChild(invisibleBike);
    parameterDefaults->AddChild(removeCheckpoints);
    parameterDefaults->AddChild(endingTimeLimit);
    parameterDefaults->AddChild(burningBike);
    parameterDefaults->AddChild(wheelieMode);
    parameterDefaults->AddChild(lives);
    parameterDefaults->AddChild(baggieAllowed);
    parameterDefaults->AddChild(roachAllowed);
    parameterDefaults->AddChild(pitViperAllowed);
    parameterDefaults->AddChild(foxbatAllowed);
    parameterDefaults->AddChild(quadAllowed);
    parameterDefaults->AddChild(bmxAllowed);
    parameterDefaults->AddChild(donkeyAllowed);
    parameterDefaults->AddChild(unicornAllowed);

    RegisterTweakable(parameterDefaults);
    multiplayer->AddChild(parameterDefaults);

    // Additional Multiplayer tweakables
    auto estimationMinimumAdvance = CreateSyncedFloat(788, "EstimationMinimumAdvance", 0.5f, 0.0f, 10.0f);
    auto remoteInterpolationCap = CreateSyncedFloat(789, "RemoteInterpolationCap", 120.0f, 0.0f, 500.0f);
    auto delayIncreaseDelay = CreateSyncedFloat(790, "DelayIncreaseDelay", 0.75f, 0.0f, 2.0f);
    auto delayDecreaseDelay = CreateSyncedFloat(791, "DelayDecreaseDelay", 0.998f, 0.0f, 1.0f);

    RegisterTweakable(estimationMinimumAdvance);
    RegisterTweakable(remoteInterpolationCap);
    RegisterTweakable(delayIncreaseDelay);
    RegisterTweakable(delayDecreaseDelay);

    multiplayer->AddChild(estimationMinimumAdvance);
    multiplayer->AddChild(remoteInterpolationCap);
    multiplayer->AddChild(delayIncreaseDelay);
    multiplayer->AddChild(delayDecreaseDelay);

    RegisterTweakable(multiplayer);
    m_rootFolders.push_back(multiplayer);
}

void DevMenu::InitializeEvent() {
    auto event = std::make_shared<TweakableFolder>(145, "Event");

    // Add all Event tweakables
    auto netDelayHigh = CreateSyncedInt(146, "NetDelayHigh", 12, 0, 100);
    auto netDelayMedium = CreateSyncedInt(147, "NetDelayMedium", 6, 0, 100);
    auto netDelayLow = CreateSyncedInt(148, "NetDelayLow", 1, 0, 100);

    RegisterTweakable(netDelayHigh);
    RegisterTweakable(netDelayMedium);
    RegisterTweakable(netDelayLow);

    event->AddChild(netDelayHigh);
    event->AddChild(netDelayMedium);
    event->AddChild(netDelayLow);

    RegisterTweakable(event);
    m_rootFolders.push_back(event);
}

void DevMenu::InitializeFMX() {
    auto fmx = std::make_shared<TweakableFolder>(150, "FMX");
    RegisterTweakable(fmx);
    m_rootFolders.push_back(fmx);
}

void DevMenu::InitializeBike() {
    auto bike = std::make_shared<TweakableFolder>(169, "Bike");

    // Top-level bike parameters
    auto tuneEnabled = CreateSyncedBool(170, "TuneEnabled", false);
    auto newAccelerationEnabled = CreateSyncedBool(171, "newAccelerationEnabled", false);
    auto idNumber = CreateSyncedInt(172, "IdNumber", 3, 0, 10);

    RegisterTweakable(tuneEnabled);
    RegisterTweakable(newAccelerationEnabled);
    RegisterTweakable(idNumber);

    bike->AddChild(tuneEnabled);
    bike->AddChild(newAccelerationEnabled);
    bike->AddChild(idNumber);

    // Engine subfolder
    auto engine = std::make_shared<TweakableFolder>(173, "Engine");
    auto eng_accelMul = CreateSyncedFloat(174, "AccelerationMultiplier", 1.0f, 0.0f, 5.0f);
    auto eng_accelSpeedDiv = CreateSyncedFloat(175, "AccelerationSpeedDivisor", 1.0f, 0.0f, 5.0f);
    auto eng_rpmSeekSpeedMul = CreateSyncedFloat(176, "RpmSeekSpeedMul", 0.0107f, 0.0f, 1.0f);
    auto eng_rpmMin = CreateSyncedFloat(179, "RpmMin", 1980.0f, 0.0f, 10000.0f);
    auto eng_rpmMax = CreateSyncedFloat(186, "RpmMax", 8400.0f, 0.0f, 15000.0f);
    auto eng_rpmMaxAdd = CreateSyncedFloat(187, "RpmMaxAdd", 600.0f, 0.0f, 2000.0f);
    auto eng_rpmMul = CreateSyncedFloat(188, "RpmMul", 0.081f, 0.0f, 1.0f);
    auto eng_rpmAccelCurrent = CreateSyncedFloat(189, "RpmAccelCurrent", 0.991f, 0.0f, 1.0f);
    auto eng_rpmDecelCurrent = CreateSyncedFloat(190, "RpmDecelCurrent", 0.975f, 0.0f, 1.0f);
    auto eng_rpmAccelTarget = CreateSyncedFloat(191, "RpmAccelTarget", 0.025f, 0.0f, 1.0f);
    auto eng_rpmDecelTarget = CreateSyncedFloat(192, "RpmDecelTarget", 0.7f, 0.0f, 1.0f);

    RegisterTweakable(eng_accelMul);
    RegisterTweakable(eng_accelSpeedDiv);
    RegisterTweakable(eng_rpmSeekSpeedMul);
    RegisterTweakable(eng_rpmMin);
    RegisterTweakable(eng_rpmMax);
    RegisterTweakable(eng_rpmMaxAdd);
    RegisterTweakable(eng_rpmMul);
    RegisterTweakable(eng_rpmAccelCurrent);
    RegisterTweakable(eng_rpmDecelCurrent);
    RegisterTweakable(eng_rpmAccelTarget);
    RegisterTweakable(eng_rpmDecelTarget);

    engine->AddChild(eng_accelMul);
    engine->AddChild(eng_accelSpeedDiv);
    engine->AddChild(eng_rpmSeekSpeedMul);
    engine->AddChild(eng_rpmMin);
    engine->AddChild(eng_rpmMax);
    engine->AddChild(eng_rpmMaxAdd);
    engine->AddChild(eng_rpmMul);
    engine->AddChild(eng_rpmAccelCurrent);
    engine->AddChild(eng_rpmDecelCurrent);
    engine->AddChild(eng_rpmAccelTarget);
    engine->AddChild(eng_rpmDecelTarget);

    RegisterTweakable(engine);
    bike->AddChild(engine);

    // Transmission subfolder
    auto transmission = std::make_shared<TweakableFolder>(177, "Transmission");
    auto trans_rpmClutch = CreateSyncedFloat(178, "RpmClutch", 0.97f, 0.0f, 1.0f);
    auto trans_rpmShiftDown = CreateSyncedFloat(180, "RpmShiftDown", 6120.0f, 0.0f, 15000.0f);
    auto trans_rpmGearDiv1 = CreateSyncedFloat(181, "RpmGearDiv1", 1.99f, 0.0f, 10.0f);
    auto trans_rpmGearDiv2 = CreateSyncedFloat(182, "RpmGearDiv2", 2.46f, 0.0f, 10.0f);
    auto trans_rpmGearDiv3 = CreateSyncedFloat(183, "RpmGearDiv3", 2.56f, 0.0f, 10.0f);
    auto trans_rpmGearDiv4 = CreateSyncedFloat(184, "RpmGearDiv4", 2.66f, 0.0f, 10.0f);
    auto trans_rpmGearDiv5 = CreateSyncedFloat(185, "RpmGearDiv5", 3.0f, 0.0f, 10.0f);
    auto trans_shiftLoadReduce = CreateSyncedFloat(193, "ShiftLoadReduce", 0.97f, 0.0f, 1.0f);

    RegisterTweakable(trans_rpmClutch);
    RegisterTweakable(trans_rpmShiftDown);
    RegisterTweakable(trans_rpmGearDiv1);
    RegisterTweakable(trans_rpmGearDiv2);
    RegisterTweakable(trans_rpmGearDiv3);
    RegisterTweakable(trans_rpmGearDiv4);
    RegisterTweakable(trans_rpmGearDiv5);
    RegisterTweakable(trans_shiftLoadReduce);

    transmission->AddChild(trans_rpmClutch);
    transmission->AddChild(trans_rpmShiftDown);
    transmission->AddChild(trans_rpmGearDiv1);
    transmission->AddChild(trans_rpmGearDiv2);
    transmission->AddChild(trans_rpmGearDiv3);
    transmission->AddChild(trans_rpmGearDiv4);
    transmission->AddChild(trans_rpmGearDiv5);
    transmission->AddChild(trans_shiftLoadReduce);

    RegisterTweakable(transmission);
    bike->AddChild(transmission);

    // Properties subfolder
    auto properties = std::make_shared<TweakableFolder>(194, "Properties");
    auto prop_accelPower = CreateSyncedFloat(195, "AccelerationPower", 28.0f, 0.0f, 100.0f);
    auto prop_accelBrake = CreateSyncedFloat(196, "AccelerationBrake", 27.0f, 0.0f, 100.0f);
    auto prop_accelForce = CreateSyncedFloat(197, "AccelerationForce", -0.25f, -10.0f, 10.0f);
    auto prop_engineDamping = CreateSyncedFloat(198, "EngineDamping", 0.0f, 0.0f, 10.0f);
    auto prop_maxVelocity = CreateSyncedFloat(199, "MaximumVelocity", 20.0f, 0.0f, 100.0f);
    auto prop_massFactor = CreateSyncedFloat(200, "MassFactor", 1.0f, 0.0f, 10.0f);
    auto prop_brakePowerFront = CreateSyncedFloat(201, "BrakePowerFront", 1.25f, 0.0f, 10.0f);
    auto prop_brakePowerBack = CreateSyncedFloat(207, "BrakePowerBack", 1.25f, 0.0f, 10.0f);
    auto prop_accelPowerQuadMP = CreateSyncedFloat(260, "AccelerationPowerQuadMP", 28.0f, 0.0f, 100.0f);
    auto prop_accelBrakeQuadMP = CreateSyncedFloat(261, "AccelerationBrakeQuadMP", 27.0f, 0.0f, 100.0f);
    auto prop_unicornWalkMul = CreateSyncedFloat(268, "UnicornWalkMultiplier", 1.0f, 0.0f, 10.0f);
    auto prop_unicornRunMul = CreateSyncedFloat(269, "UnicornRunMultiplier", 0.4f, 0.0f, 10.0f);
    auto prop_unicornGallopMul = CreateSyncedFloat(270, "UnicornGallopMultiplier", 0.14f, 0.0f, 10.0f);
    auto prop_unicornRunSpeed = CreateSyncedFloat(271, "UnicornRunSpeed", 42.0f, 0.0f, 200.0f);
    auto prop_unicornGallopSpeed = CreateSyncedFloat(272, "UnicornGallopSpeed", 125.0f, 0.0f, 300.0f);
    auto prop_unicornFireInitValue = CreateSyncedFloat(273, "UnicornFireInitValue", 0.0f, 0.0f, 100.0f);

    RegisterTweakable(prop_accelPower);
    RegisterTweakable(prop_accelBrake);
    RegisterTweakable(prop_accelForce);
    RegisterTweakable(prop_engineDamping);
    RegisterTweakable(prop_maxVelocity);
    RegisterTweakable(prop_massFactor);
    RegisterTweakable(prop_brakePowerFront);
    RegisterTweakable(prop_brakePowerBack);
    RegisterTweakable(prop_accelPowerQuadMP);
    RegisterTweakable(prop_accelBrakeQuadMP);
    RegisterTweakable(prop_unicornWalkMul);
    RegisterTweakable(prop_unicornRunMul);
    RegisterTweakable(prop_unicornGallopMul);
    RegisterTweakable(prop_unicornRunSpeed);
    RegisterTweakable(prop_unicornGallopSpeed);
    RegisterTweakable(prop_unicornFireInitValue);

    properties->AddChild(prop_accelPower);
    properties->AddChild(prop_accelBrake);
    properties->AddChild(prop_accelForce);
    properties->AddChild(prop_engineDamping);
    properties->AddChild(prop_maxVelocity);
    properties->AddChild(prop_massFactor);
    properties->AddChild(prop_brakePowerFront);
    properties->AddChild(prop_brakePowerBack);
    properties->AddChild(prop_accelPowerQuadMP);
    properties->AddChild(prop_accelBrakeQuadMP);
    properties->AddChild(prop_unicornWalkMul);
    properties->AddChild(prop_unicornRunMul);
    properties->AddChild(prop_unicornGallopMul);
    properties->AddChild(prop_unicornRunSpeed);
    properties->AddChild(prop_unicornGallopSpeed);
    properties->AddChild(prop_unicornFireInitValue);

    RegisterTweakable(properties);
    bike->AddChild(properties);

    // Suspension subfolder
    auto suspension = std::make_shared<TweakableFolder>(202, "Suspension");
    auto susp_frontSpringSoftness = CreateSyncedFloat(203, "FrontSpringSoftness", 4000.0f, 0.0f, 20000.0f);
    auto susp_frontSpringDamping = CreateSyncedFloat(204, "FrontSpringDamping", 0.001f, 0.0f, 1.0f);
    auto susp_frontWheelSpringSoftness = CreateSyncedFloat(205, "FrontWheelSpringSoftness", 0.325f, 0.0f, 5.0f);
    auto susp_frontWheelSpringDamping = CreateSyncedFloat(206, "FrontWheelSpringDamping", 0.2f, 0.0f, 5.0f);
    auto susp_backSpringSoftness = CreateSyncedFloat(208, "BackSpringSoftness", 4000.0f, 0.0f, 20000.0f);
    auto susp_backSpringDamping = CreateSyncedFloat(209, "BackSpringDamping", 0.001f, 0.0f, 1.0f);
    auto susp_backWheelSpringSoftness = CreateSyncedFloat(210, "BackWheelSpringSoftness", 0.2f, 0.0f, 5.0f);
    auto susp_backWheelSpringDamping = CreateSyncedFloat(211, "BackWheelSpringDamping", 0.3f, 0.0f, 5.0f);
    auto susp_backWheelSpring2Softness = CreateSyncedFloat(212, "BackWheelSpring2Softness", 0.0f, 0.0f, 5.0f);
    auto susp_backWheelSpring2Damping = CreateSyncedFloat(213, "BackWheelSpring2Damping", 1.0f, 0.0f, 5.0f);

    RegisterTweakable(susp_frontSpringSoftness);
    RegisterTweakable(susp_frontSpringDamping);
    RegisterTweakable(susp_frontWheelSpringSoftness);
    RegisterTweakable(susp_frontWheelSpringDamping);
    RegisterTweakable(susp_backSpringSoftness);
    RegisterTweakable(susp_backSpringDamping);
    RegisterTweakable(susp_backWheelSpringSoftness);
    RegisterTweakable(susp_backWheelSpringDamping);
    RegisterTweakable(susp_backWheelSpring2Softness);
    RegisterTweakable(susp_backWheelSpring2Damping);

    suspension->AddChild(susp_frontSpringSoftness);
    suspension->AddChild(susp_frontSpringDamping);
    suspension->AddChild(susp_frontWheelSpringSoftness);
    suspension->AddChild(susp_frontWheelSpringDamping);
    suspension->AddChild(susp_backSpringSoftness);
    suspension->AddChild(susp_backSpringDamping);
    suspension->AddChild(susp_backWheelSpringSoftness);
    suspension->AddChild(susp_backWheelSpringDamping);
    suspension->AddChild(susp_backWheelSpring2Softness);
    suspension->AddChild(susp_backWheelSpring2Damping);

    RegisterTweakable(suspension);
    bike->AddChild(suspension);

    RegisterTweakable(bike);
    m_rootFolders.push_back(bike);
}

void DevMenu::InitializeRider() {
    auto rider = std::make_shared<TweakableFolder>(214, "Rider");

    // TuneEnabled
    auto tuneEnabled = CreateSyncedBool(215, "TuneEnabled", false);
    RegisterTweakable(tuneEnabled);
    rider->AddChild(tuneEnabled);

    // Properties subfolder
    auto properties = std::make_shared<TweakableFolder>(216, "Properties");
    auto massFactor = CreateSyncedFloat(217, "MassFactor", 1.0f, 0.0f, 10.0f);

    RegisterTweakable(massFactor);
    properties->AddChild(massFactor);

    RegisterTweakable(properties);
    rider->AddChild(properties);

    RegisterTweakable(rider);
    m_rootFolders.push_back(rider);
}

void DevMenu::InitializeVibra() {
    auto vibra = std::make_shared<TweakableFolder>(243, "Vibra");

    // Add all Vibra tweakables
    auto brakeToLT = CreateSyncedFloat(244, "BrakeToLT", 0.5f, 0.0f, 2.0f);
    auto leftAndRightToLT = CreateSyncedFloat(245, "LeftAndRightToLT", 1.09f, 0.0f, 5.0f);
    auto loadToRT = CreateSyncedFloat(246, "LoadToRT", 0.5f, 0.0f, 2.0f);
    auto leftToRT = CreateSyncedFloat(247, "LeftToRT", 1.35f, 0.0f, 5.0f);
    auto airThreshold = CreateSyncedInt(248, "AirThreshold", 3, 0, 20);
    auto rtStartRPM = CreateSyncedFloat(249, "RTStartRPM", 0.0f, 0.0f, 1.0f);
    auto rtStartLoad = CreateSyncedFloat(250, "RTStartLoad", 0.59f, 0.0f, 2.0f);
    auto rtGasAddition = CreateSyncedFloat(251, "RTGasAddition", 0.0f, 0.0f, 2.0f);
    auto rtBackWheelStart = CreateSyncedFloat(252, "RTBackWheelStart", 0.2f, 0.0f, 2.0f);
    auto rtSlippingClutch = CreateSyncedFloat(253, "RTSlippingClutch", 0.2f, 0.0f, 2.0f);

    RegisterTweakable(brakeToLT);
    RegisterTweakable(leftAndRightToLT);
    RegisterTweakable(loadToRT);
    RegisterTweakable(leftToRT);
    RegisterTweakable(airThreshold);
    RegisterTweakable(rtStartRPM);
    RegisterTweakable(rtStartLoad);
    RegisterTweakable(rtGasAddition);
    RegisterTweakable(rtBackWheelStart);
    RegisterTweakable(rtSlippingClutch);

    vibra->AddChild(brakeToLT);
    vibra->AddChild(leftAndRightToLT);
    vibra->AddChild(loadToRT);
    vibra->AddChild(leftToRT);
    vibra->AddChild(airThreshold);
    vibra->AddChild(rtStartRPM);
    vibra->AddChild(rtStartLoad);
    vibra->AddChild(rtGasAddition);
    vibra->AddChild(rtBackWheelStart);
    vibra->AddChild(rtSlippingClutch);

    RegisterTweakable(vibra);
    m_rootFolders.push_back(vibra);
}

void DevMenu::InitializeSoundSystem() {
    auto soundSystem = std::make_shared<TweakableFolder>(258, "SoundSystem");

    auto freefallYellHeadVelocity = CreateSyncedFloat(259, "freefallYellHeadVelocity", 10.0f, 0.0f, 50.0f);
    auto audioDuckingFadeStep = CreateSyncedFloat(557, "audioDuckingFadeStep", 0.0121f, 0.0f, 1.0f);
    auto audioDuckingFactor = CreateSyncedFloat(558, "audioDuckingFactor", 0.39f, 0.0f, 1.0f);
    auto audioDuckingExtraTicks = CreateSyncedInt(559, "audioDuckingExtraTicks", 10, 0, 100);
    auto forceSynchronousProcessing = CreateSyncedBool(626, "ForceSynchronousProcessing", false);
    auto maxCommands = CreateSyncedInt(627, "MaxCommands", 400, 0, 2000);
    auto printCommandName = CreateSyncedBool(628, "PrintCommandName", true);

    RegisterTweakable(freefallYellHeadVelocity);
    RegisterTweakable(audioDuckingFadeStep);
    RegisterTweakable(audioDuckingFactor);
    RegisterTweakable(audioDuckingExtraTicks);
    RegisterTweakable(forceSynchronousProcessing);
    RegisterTweakable(maxCommands);
    RegisterTweakable(printCommandName);

    soundSystem->AddChild(freefallYellHeadVelocity);
    soundSystem->AddChild(audioDuckingFadeStep);
    soundSystem->AddChild(audioDuckingFactor);
    soundSystem->AddChild(audioDuckingExtraTicks);
    soundSystem->AddChild(forceSynchronousProcessing);
    soundSystem->AddChild(maxCommands);
    soundSystem->AddChild(printCommandName);

    RegisterTweakable(soundSystem);
    m_rootFolders.push_back(soundSystem);
}

void DevMenu::InitializeReplayCRC() {
    auto replayCRC = std::make_shared<TweakableFolder>(280, "Replay CRC check");

    auto active = CreateSyncedBool(281, "active", false);

    RegisterTweakable(active);
    replayCRC->AddChild(active);

    RegisterTweakable(replayCRC);
    m_rootFolders.push_back(replayCRC);
}

void DevMenu::InitializeUtils() {
    auto utils = std::make_shared<TweakableFolder>(282, "Utils");

    auto replayCameraEnabled = CreateSyncedBool(283, "ReplayCameraEnabled", true);
    auto inGameCountersHidden = CreateSyncedBool(605, "InGameCountersHidden", false);

    RegisterTweakable(replayCameraEnabled);
    RegisterTweakable(inGameCountersHidden);

    utils->AddChild(replayCameraEnabled);
    utils->AddChild(inGameCountersHidden);

    RegisterTweakable(utils);
    m_rootFolders.push_back(utils);
}

void DevMenu::InitializeReplayCamera() {
    auto replayCamera = std::make_shared<TweakableFolder>(284, "ReplayCamera");

    // FollowCamera subfolder
    auto followCamera = std::make_shared<TweakableFolder>(285, "FollowCamera");
    auto fc_fovSpeed = CreateSyncedFloat(286, "FOVSpeed", 30.0f, 0.0f, 100.0f);
    auto fc_fovMin = CreateSyncedFloat(287, "FOVMin", 20.0f, 0.0f, 180.0f);
    auto fc_fovMax = CreateSyncedFloat(288, "FOVMax", 90.0f, 0.0f, 180.0f);
    auto fc_panSpeed = CreateSyncedFloat(303, "PanSpeed", 6.0f, 0.0f, 50.0f);
    auto fc_panMin = CreateSyncedFloat(304, "PanMin", -5.0f, -50.0f, 50.0f);
    auto fc_panMax = CreateSyncedFloat(305, "PanMax", 0.0f, -50.0f, 50.0f);
    auto fc_distanceSpeed = CreateSyncedFloat(306, "DistanceSpeed", 10.0f, 0.0f, 100.0f);
    auto fc_distanceMin = CreateSyncedFloat(307, "DistanceMin", 2.0f, 0.0f, 100.0f);
    auto fc_distanceMax = CreateSyncedFloat(308, "DistanceMax", 50.0f, 0.0f, 200.0f);
    auto fc_rollSpeed = CreateSyncedFloat(309, "RollSpeed", 50.0f, 0.0f, 200.0f);
    auto fc_heightSpeed = CreateSyncedFloat(310, "HeightSpeed", 15.0f, 0.0f, 100.0f);
    auto fc_heightMin = CreateSyncedFloat(311, "HeightMin", -10.0f, -50.0f, 50.0f);
    auto fc_heightMax = CreateSyncedFloat(312, "HeightMax", 10.0f, -50.0f, 50.0f);
    auto fc_orbitSpeed = CreateSyncedFloat(313, "OrbitSpeed", 180.0f, 0.0f, 360.0f);
    auto fc_orbitMin = CreateSyncedFloat(314, "OrbitMin", -180.0f, -360.0f, 360.0f);
    auto fc_orbitMax = CreateSyncedFloat(315, "OrbitMax", 180.0f, -360.0f, 360.0f);
    auto fc_sensitivityX = CreateSyncedFloat(316, "SensitivityX", -0.1f, -2.0f, 2.0f);
    auto fc_sensitivityY = CreateSyncedFloat(317, "SensitivityY", -0.1f, -2.0f, 2.0f);
    auto fc_sensitivityZ = CreateSyncedFloat(318, "SensitivityZ", -0.1f, -2.0f, 2.0f);
    auto fc_apertureTime = CreateSyncedFloat(319, "ApertureTime", 0.5f, 0.0f, 5.0f);
    auto fc_filmWidth = CreateSyncedFloat(320, "FilmWidth", 0.1f, 0.0f, 5.0f);
    auto fc_nearBlur = CreateSyncedFloat(321, "NearBlur", 0.5f, 0.0f, 10.0f);
    auto fc_farBlur = CreateSyncedFloat(322, "FarBlur", 50.0f, 0.0f, 200.0f);
    auto fc_positionInterpolation = CreateSyncedFloat(323, "PositionInterpolation", 0.17f, 0.0f, 1.0f);
    auto fc_targetInterpolation = CreateSyncedFloat(324, "TargetInterpolation", 0.05f, 0.0f, 1.0f);

    RegisterTweakable(fc_fovSpeed);
    RegisterTweakable(fc_fovMin);
    RegisterTweakable(fc_fovMax);
    RegisterTweakable(fc_panSpeed);
    RegisterTweakable(fc_panMin);
    RegisterTweakable(fc_panMax);
    RegisterTweakable(fc_distanceSpeed);
    RegisterTweakable(fc_distanceMin);
    RegisterTweakable(fc_distanceMax);
    RegisterTweakable(fc_rollSpeed);
    RegisterTweakable(fc_heightSpeed);
    RegisterTweakable(fc_heightMin);
    RegisterTweakable(fc_heightMax);
    RegisterTweakable(fc_orbitSpeed);
    RegisterTweakable(fc_orbitMin);
    RegisterTweakable(fc_orbitMax);
    RegisterTweakable(fc_sensitivityX);
    RegisterTweakable(fc_sensitivityY);
    RegisterTweakable(fc_sensitivityZ);
    RegisterTweakable(fc_apertureTime);
    RegisterTweakable(fc_filmWidth);
    RegisterTweakable(fc_nearBlur);
    RegisterTweakable(fc_farBlur);
    RegisterTweakable(fc_positionInterpolation);
    RegisterTweakable(fc_targetInterpolation);

    followCamera->AddChild(fc_fovSpeed);
    followCamera->AddChild(fc_fovMin);
    followCamera->AddChild(fc_fovMax);
    followCamera->AddChild(fc_panSpeed);
    followCamera->AddChild(fc_panMin);
    followCamera->AddChild(fc_panMax);
    followCamera->AddChild(fc_distanceSpeed);
    followCamera->AddChild(fc_distanceMin);
    followCamera->AddChild(fc_distanceMax);
    followCamera->AddChild(fc_rollSpeed);
    followCamera->AddChild(fc_heightSpeed);
    followCamera->AddChild(fc_heightMin);
    followCamera->AddChild(fc_heightMax);
    followCamera->AddChild(fc_orbitSpeed);
    followCamera->AddChild(fc_orbitMin);
    followCamera->AddChild(fc_orbitMax);
    followCamera->AddChild(fc_sensitivityX);
    followCamera->AddChild(fc_sensitivityY);
    followCamera->AddChild(fc_sensitivityZ);
    followCamera->AddChild(fc_apertureTime);
    followCamera->AddChild(fc_filmWidth);
    followCamera->AddChild(fc_nearBlur);
    followCamera->AddChild(fc_farBlur);
    followCamera->AddChild(fc_positionInterpolation);
    followCamera->AddChild(fc_targetInterpolation);

    RegisterTweakable(followCamera);
    replayCamera->AddChild(followCamera);

    // SpectatorCamera subfolder
    auto spectatorCamera = std::make_shared<TweakableFolder>(289, "SpectatorCamera");
    auto sc_panSpeed = CreateSyncedFloat(290, "PanSpeed", 10.0f, 0.0f, 50.0f);
    auto sc_panMin = CreateSyncedFloat(291, "PanMin", 0.0f, -50.0f, 50.0f);
    auto sc_panMax = CreateSyncedFloat(292, "PanMax", 0.0f, -50.0f, 50.0f);
    auto sc_distanceSpeed = CreateSyncedFloat(293, "DistanceSpeed", 6.0f, 0.0f, 100.0f);
    auto sc_distanceMin = CreateSyncedFloat(294, "DistanceMin", 5.0f, 0.0f, 100.0f);
    auto sc_distanceMax = CreateSyncedFloat(295, "DistanceMax", 10.0f, 0.0f, 200.0f);
    auto sc_heightSpeed = CreateSyncedFloat(296, "HeightSpeed", 15.0f, 0.0f, 100.0f);
    auto sc_heightMin = CreateSyncedFloat(297, "HeightMin", 0.0f, -50.0f, 50.0f);
    auto sc_heightMax = CreateSyncedFloat(298, "HeightMax", 0.0f, -50.0f, 50.0f);
    auto sc_orbitSpeed = CreateSyncedFloat(299, "OrbitSpeed", 75.0f, 0.0f, 360.0f);
    auto sc_orbitMin = CreateSyncedFloat(300, "OrbitMin", -30.0f, -360.0f, 360.0f);
    auto sc_orbitMax = CreateSyncedFloat(301, "OrbitMax", 5.0f, -360.0f, 360.0f);
    auto sc_resetOnStickRelease = CreateSyncedBool(302, "ResetOnStickRelease", true);

    RegisterTweakable(sc_panSpeed);
    RegisterTweakable(sc_panMin);
    RegisterTweakable(sc_panMax);
    RegisterTweakable(sc_distanceSpeed);
    RegisterTweakable(sc_distanceMin);
    RegisterTweakable(sc_distanceMax);
    RegisterTweakable(sc_heightSpeed);
    RegisterTweakable(sc_heightMin);
    RegisterTweakable(sc_heightMax);
    RegisterTweakable(sc_orbitSpeed);
    RegisterTweakable(sc_orbitMin);
    RegisterTweakable(sc_orbitMax);
    RegisterTweakable(sc_resetOnStickRelease);

    spectatorCamera->AddChild(sc_panSpeed);
    spectatorCamera->AddChild(sc_panMin);
    spectatorCamera->AddChild(sc_panMax);
    spectatorCamera->AddChild(sc_distanceSpeed);
    spectatorCamera->AddChild(sc_distanceMin);
    spectatorCamera->AddChild(sc_distanceMax);
    spectatorCamera->AddChild(sc_heightSpeed);
    spectatorCamera->AddChild(sc_heightMin);
    spectatorCamera->AddChild(sc_heightMax);
    spectatorCamera->AddChild(sc_orbitSpeed);
    spectatorCamera->AddChild(sc_orbitMin);
    spectatorCamera->AddChild(sc_orbitMax);
    spectatorCamera->AddChild(sc_resetOnStickRelease);

    RegisterTweakable(spectatorCamera);
    replayCamera->AddChild(spectatorCamera);

    // FreeCamera subfolder
    auto freeCamera = std::make_shared<TweakableFolder>(325, "FreeCamera");
    auto frc_fovSpeed = CreateSyncedFloat(326, "FOVSpeed", 30.0f, 0.0f, 100.0f);
    auto frc_fovMin = CreateSyncedFloat(327, "FOVMin", 20.0f, 0.0f, 180.0f);
    auto frc_fovMax = CreateSyncedFloat(328, "FOVMax", 90.0f, 0.0f, 180.0f);
    auto frc_moveSpeed = CreateSyncedFloat(329, "MoveSpeed", 20.0f, 0.0f, 200.0f);
    auto frc_moveMin = CreateSyncedFloat(330, "MoveMin", -50.0f, -200.0f, 200.0f);
    auto frc_moveMax = CreateSyncedFloat(331, "MoveMax", 50.0f, -200.0f, 200.0f);
    auto frc_turnSpeed = CreateSyncedFloat(332, "TurnSpeed", 180.0f, 0.0f, 360.0f);
    auto frc_turnMin = CreateSyncedFloat(333, "TurnMin", -180.0f, -360.0f, 360.0f);
    auto frc_turnMax = CreateSyncedFloat(334, "TurnMax", 180.0f, -360.0f, 360.0f);
    auto frc_rollSpeed = CreateSyncedFloat(335, "RollSpeed", 50.0f, 0.0f, 200.0f);
    auto frc_sensitivityX = CreateSyncedFloat(336, "SensitivityX", 0.1f, -2.0f, 2.0f);
    auto frc_sensitivityY = CreateSyncedFloat(337, "SensitivityY", -0.1f, -2.0f, 2.0f);
    auto frc_sensitivityZ = CreateSyncedFloat(338, "SensitivityZ", -0.1f, -2.0f, 2.0f);
    auto frc_positionInterpolation = CreateSyncedFloat(339, "PositionInterpolation", 0.2f, 0.0f, 1.0f);
    auto frc_targetInterpolation = CreateSyncedFloat(340, "TargetInterpolation", 0.2f, 0.0f, 1.0f);

    RegisterTweakable(frc_fovSpeed);
    RegisterTweakable(frc_fovMin);
    RegisterTweakable(frc_fovMax);
    RegisterTweakable(frc_moveSpeed);
    RegisterTweakable(frc_moveMin);
    RegisterTweakable(frc_moveMax);
    RegisterTweakable(frc_turnSpeed);
    RegisterTweakable(frc_turnMin);
    RegisterTweakable(frc_turnMax);
    RegisterTweakable(frc_rollSpeed);
    RegisterTweakable(frc_sensitivityX);
    RegisterTweakable(frc_sensitivityY);
    RegisterTweakable(frc_sensitivityZ);
    RegisterTweakable(frc_positionInterpolation);
    RegisterTweakable(frc_targetInterpolation);

    freeCamera->AddChild(frc_fovSpeed);
    freeCamera->AddChild(frc_fovMin);
    freeCamera->AddChild(frc_fovMax);
    freeCamera->AddChild(frc_moveSpeed);
    freeCamera->AddChild(frc_moveMin);
    freeCamera->AddChild(frc_moveMax);
    freeCamera->AddChild(frc_turnSpeed);
    freeCamera->AddChild(frc_turnMin);
    freeCamera->AddChild(frc_turnMax);
    freeCamera->AddChild(frc_rollSpeed);
    freeCamera->AddChild(frc_sensitivityX);
    freeCamera->AddChild(frc_sensitivityY);
    freeCamera->AddChild(frc_sensitivityZ);
    freeCamera->AddChild(frc_positionInterpolation);
    freeCamera->AddChild(frc_targetInterpolation);

    RegisterTweakable(freeCamera);
    replayCamera->AddChild(freeCamera);

    // ReplayCamera-level parameters
    auto replayCameraYOffset = CreateSyncedFloat(341, "ReplayCameraYOffset", 1.65f, -10.0f, 10.0f);

    RegisterTweakable(replayCameraYOffset);
    replayCamera->AddChild(replayCameraYOffset);

    RegisterTweakable(replayCamera);
    m_rootFolders.push_back(replayCamera);
}

void DevMenu::InitializePhysics() {
    auto physics = std::make_shared<TweakableFolder>(345, "Physics");

    auto breakEffectLoadSmall = CreateSyncedInt(346, "breakEffectLoadSmall", 4, 0, 100);
    auto breakEffectLoadMedium = CreateSyncedInt(347, "breakEffectLoadMedium", 10, 0, 100);
    auto breakEffectCooldownTicks = CreateSyncedInt(348, "breakEffectCooldownTicks", 60, 0, 1000);
    auto superCrossPhysicsActivationDistance = CreateSyncedInt(349, "SuperCrossPhysicsActivationDistance", 20, 0, 200);

    RegisterTweakable(breakEffectLoadSmall);
    RegisterTweakable(breakEffectLoadMedium);
    RegisterTweakable(breakEffectCooldownTicks);
    RegisterTweakable(superCrossPhysicsActivationDistance);

    physics->AddChild(breakEffectLoadSmall);
    physics->AddChild(breakEffectLoadMedium);
    physics->AddChild(breakEffectCooldownTicks);
    physics->AddChild(superCrossPhysicsActivationDistance);

    RegisterTweakable(physics);
    m_rootFolders.push_back(physics);
}

void DevMenu::InitializeFrameSkipper() {
    auto frameSkipper = std::make_shared<TweakableFolder>(351, "FrameSkipper");

    auto skipAdditionalTicksOnReset = CreateSyncedInt(352, "SkipAdditionalTicksOnReset", 10, 0, 100);
    auto pressLBForDelayMS = CreateSyncedInt(488, "PressLBForDelayMS", 0, 0, 10000);
    auto enabled = CreateSyncedBool(520, "enabled", true);
    auto forceSkipperOn = CreateSyncedBool(521, "forceSkipperOn", false);
    auto forceStrictMode = CreateSyncedBool(522, "forceStrictMode", false);
    auto maxSkippedFrames = CreateSyncedInt(523, "maxSkippedFrames", 4, 0, 20);
    auto maxSkippedFramesUGC = CreateSyncedInt(524, "maxSkippedFramesUGC", 4, 0, 20);
    auto maxSkippedFramesInFastForward = CreateSyncedInt(525, "maxSkippedFramesInFastForward", 4, 0, 20);
    auto rateIncreaseDelay = CreateSyncedInt(526, "rateIncreaseDelay", 10, 0, 100);
    auto maxLateFrames = CreateSyncedInt(527, "maxLateFrames", 2, 0, 20);
    auto lateFramesDecayTime = CreateSyncedInt(528, "lateFramesDecayTime", 6, 0, 100);
    auto maxMissedFramesCumulative = CreateSyncedInt(529, "maxMissedFramesCumulative", 20, 0, 200);
    auto waitThreshold = CreateSyncedInt(530, "waitThreshold", -500000, -5000000, 5000000);
    auto framesBetweenSlowdowns = CreateSyncedInt(531, "framesBetweenSlowdowns", 10, 0, 100);
    auto speedUpThreshold = CreateSyncedInt(532, "speedUpThreshold", 50000, -1000000, 1000000);
    auto stopSpeedUpThreshold = CreateSyncedInt(533, "stopSpeedUpThreshold", -30000, -1000000, 1000000);
    auto catchUpThreshold = CreateSyncedInt(534, "catchUpThreshold", 500000, 0, 5000000);
    auto maxUnrenderedFrames = CreateSyncedInt(535, "maxUnrenderedFrames", 5, 0, 20);
    auto catchUpMaxRatio = CreateSyncedFloat(536, "catchUpMaxRatio", 0.15f, 0.0f, 1.0f);
    auto catchUpMinRatio = CreateSyncedFloat(537, "catchUpMinRatio", 0.05f, 0.0f, 1.0f);
    auto catchUpRatioIncreaseRate = CreateSyncedFloat(538, "catchUpRatioIncreaseRate", 0.75f, 0.0f, 5.0f);
    auto fakeFasterWithSkipping = CreateSyncedBool(539, "fakeFasterWithSkipping", true);
    auto fakeRenderDelay = CreateSyncedInt(540, "fakeRenderDelay", 0, 0, 10000);
    auto winMissReportTime = CreateSyncedInt(723, "winMissReportTime", 2, 0, 100);
    auto winAheadReportTime = CreateSyncedInt(724, "winAheadReportTime", 10, 0, 100);
    auto targetFrameRate = CreateSyncedInt(725, "targetFrameRate", 60, 1, 240);
    auto forceVblankWaitInterval = CreateSyncedInt(726, "forceVblankWaitInterval", 30, 0, 100);
    auto vsyncWorks = CreateSyncedBool(727, "vsyncWorks", true);
    auto maxValidFPS = CreateSyncedInt(728, "maxValidFPS", 63, 1, 240);
    auto minValidFPS = CreateSyncedInt(729, "minValidFPS", 57, 1, 240);
    auto fpsCheckInterval = CreateSyncedInt(730, "FPSCheckInterval", 2, 0, 100);
    auto abnormalFramesTolerated = CreateSyncedInt(731, "abnormalFramesTolerated", 4, 0, 100);

    RegisterTweakable(skipAdditionalTicksOnReset);
    RegisterTweakable(pressLBForDelayMS);
    RegisterTweakable(enabled);
    RegisterTweakable(forceSkipperOn);
    RegisterTweakable(forceStrictMode);
    RegisterTweakable(maxSkippedFrames);
    RegisterTweakable(maxSkippedFramesUGC);
    RegisterTweakable(maxSkippedFramesInFastForward);
    RegisterTweakable(rateIncreaseDelay);
    RegisterTweakable(maxLateFrames);
    RegisterTweakable(lateFramesDecayTime);
    RegisterTweakable(maxMissedFramesCumulative);
    RegisterTweakable(waitThreshold);
    RegisterTweakable(framesBetweenSlowdowns);
    RegisterTweakable(speedUpThreshold);
    RegisterTweakable(stopSpeedUpThreshold);
    RegisterTweakable(catchUpThreshold);
    RegisterTweakable(maxUnrenderedFrames);
    RegisterTweakable(catchUpMaxRatio);
    RegisterTweakable(catchUpMinRatio);
    RegisterTweakable(catchUpRatioIncreaseRate);
    RegisterTweakable(fakeFasterWithSkipping);
    RegisterTweakable(fakeRenderDelay);
    RegisterTweakable(winMissReportTime);
    RegisterTweakable(winAheadReportTime);
    RegisterTweakable(targetFrameRate);
    RegisterTweakable(forceVblankWaitInterval);
    RegisterTweakable(vsyncWorks);
    RegisterTweakable(maxValidFPS);
    RegisterTweakable(minValidFPS);
    RegisterTweakable(fpsCheckInterval);
    RegisterTweakable(abnormalFramesTolerated);

    frameSkipper->AddChild(skipAdditionalTicksOnReset);
    frameSkipper->AddChild(pressLBForDelayMS);
    frameSkipper->AddChild(enabled);
    frameSkipper->AddChild(forceSkipperOn);
    frameSkipper->AddChild(forceStrictMode);
    frameSkipper->AddChild(maxSkippedFrames);
    frameSkipper->AddChild(maxSkippedFramesUGC);
    frameSkipper->AddChild(maxSkippedFramesInFastForward);
    frameSkipper->AddChild(rateIncreaseDelay);
    frameSkipper->AddChild(maxLateFrames);
    frameSkipper->AddChild(lateFramesDecayTime);
    frameSkipper->AddChild(maxMissedFramesCumulative);
    frameSkipper->AddChild(waitThreshold);
    frameSkipper->AddChild(framesBetweenSlowdowns);
    frameSkipper->AddChild(speedUpThreshold);
    frameSkipper->AddChild(stopSpeedUpThreshold);
    frameSkipper->AddChild(catchUpThreshold);
    frameSkipper->AddChild(maxUnrenderedFrames);
    frameSkipper->AddChild(catchUpMaxRatio);
    frameSkipper->AddChild(catchUpMinRatio);
    frameSkipper->AddChild(catchUpRatioIncreaseRate);
    frameSkipper->AddChild(fakeFasterWithSkipping);
    frameSkipper->AddChild(fakeRenderDelay);
    frameSkipper->AddChild(winMissReportTime);
    frameSkipper->AddChild(winAheadReportTime);
    frameSkipper->AddChild(targetFrameRate);
    frameSkipper->AddChild(forceVblankWaitInterval);
    frameSkipper->AddChild(vsyncWorks);
    frameSkipper->AddChild(maxValidFPS);
    frameSkipper->AddChild(minValidFPS);
    frameSkipper->AddChild(fpsCheckInterval);
    frameSkipper->AddChild(abnormalFramesTolerated);

    RegisterTweakable(frameSkipper);
    m_rootFolders.push_back(frameSkipper);
}

void DevMenu::InitializeGraphic() {
    auto graphic = std::make_shared<TweakableFolder>(354, "Graphic");

    // Advance subfolder
    auto advance = std::make_shared<TweakableFolder>(355, "Advance");

    // RenderingDebug subfolder
    auto renderingDebug = std::make_shared<TweakableFolder>(356, "RenderingDebug");
    auto rd_enablePostEffects = CreateSyncedBool(357, "EnablePostEffects", true);
    auto rd_underwaterFogExp = CreateSyncedFloat(361, "UnderwaterFogExp", 0.35f, 0.0f, 5.0f);
    auto rd_underwaterSkyFogExp = CreateSyncedFloat(362, "UnderwaterSkyFogExp", 1.0f, 0.0f, 5.0f);
    auto rd_underwaterFogColorTint = CreateSyncedFloat(363, "UnderwaterFogColorTint", 0.5f, 0.0f, 2.0f);
    auto rd_underwaterColorTint = CreateSyncedFloat(364, "UnderwaterColorTint", 0.5f, 0.0f, 2.0f);
    auto rd_underwaterPostTechnqiue = CreateSyncedInt(365, "UnderwaterPostTechnqiue", 6, 0, 20);
    auto rd_drivingLineBikeWheelWidth = CreateSyncedFloat(366, "DrivingLineBikeWheelWidth", 0.12f, 0.0f, 1.0f);
    auto rd_drivingLineQuadWheelWidth = CreateSyncedFloat(367, "DrivingLineQuadWheelWidth", 0.29f, 0.0f, 1.0f);
    auto rd_drivingLinesQuadWheelDistance = CreateSyncedFloat(368, "DrivingLinesQuadWheelDistance", 0.57f, 0.0f, 2.0f);
    auto rd_lodDissolveFarest = CreateSyncedFloat(641, "lodDissolveFarest", 0.95f, 0.0f, 1.0f);
    auto rd_lodDissolveNearest = CreateSyncedFloat(642, "lodDissolveNearest", 0.75f, 0.0f, 1.0f);
    auto rd_lodDissolveConfine = CreateSyncedFloat(643, "lodDissolveConfine", 0.85f, 0.0f, 1.0f);
    auto rd_lodDissolveSpeed = CreateSyncedFloat(644, "lodDissolveSpeed", 0.01f, 0.0f, 1.0f);
    auto rd_enableShadow = CreateSyncedBool(653, "EnableShadow", true);
    auto rd_enableDebugRendering = CreateSyncedBool(655, "EnableDebugRendering", false);
    auto rd_debugRenderingIndex = CreateSyncedInt(665, "DebugRenderingIndex", 0, 0, 15);
    auto rd_debugSpotLightCulling = CreateSyncedBool(666, "DebugSpotLightCulling", false);
    auto rd_underwaterDofDist = CreateSyncedFloat(755, "UnderwaterDofDist", 5.0f, 0.0f, 50.0f);
    auto rd_underwaterDofNear = CreateSyncedFloat(756, "UnderwaterDofNear", 0.3f, 0.0f, 5.0f);
    auto rd_underwaterDofFar = CreateSyncedFloat(757, "UnderwaterDofFar", 0.3f, 0.0f, 5.0f);

    RegisterTweakable(rd_enablePostEffects);
    RegisterTweakable(rd_underwaterFogExp);
    RegisterTweakable(rd_underwaterSkyFogExp);
    RegisterTweakable(rd_underwaterFogColorTint);
    RegisterTweakable(rd_underwaterColorTint);
    RegisterTweakable(rd_underwaterPostTechnqiue);
    RegisterTweakable(rd_drivingLineBikeWheelWidth);
    RegisterTweakable(rd_drivingLineQuadWheelWidth);
    RegisterTweakable(rd_drivingLinesQuadWheelDistance);
    RegisterTweakable(rd_lodDissolveFarest);
    RegisterTweakable(rd_lodDissolveNearest);
    RegisterTweakable(rd_lodDissolveConfine);
    RegisterTweakable(rd_lodDissolveSpeed);
    RegisterTweakable(rd_enableShadow);
    RegisterTweakable(rd_enableDebugRendering);
    RegisterTweakable(rd_debugRenderingIndex);
    RegisterTweakable(rd_debugSpotLightCulling);
    RegisterTweakable(rd_underwaterDofDist);
    RegisterTweakable(rd_underwaterDofNear);
    RegisterTweakable(rd_underwaterDofFar);

    renderingDebug->AddChild(rd_enablePostEffects);
    renderingDebug->AddChild(rd_underwaterFogExp);
    renderingDebug->AddChild(rd_underwaterSkyFogExp);
    renderingDebug->AddChild(rd_underwaterFogColorTint);
    renderingDebug->AddChild(rd_underwaterColorTint);
    renderingDebug->AddChild(rd_underwaterPostTechnqiue);
    renderingDebug->AddChild(rd_drivingLineBikeWheelWidth);
    renderingDebug->AddChild(rd_drivingLineQuadWheelWidth);
    renderingDebug->AddChild(rd_drivingLinesQuadWheelDistance);
    renderingDebug->AddChild(rd_lodDissolveFarest);
    renderingDebug->AddChild(rd_lodDissolveNearest);
    renderingDebug->AddChild(rd_lodDissolveConfine);
    renderingDebug->AddChild(rd_lodDissolveSpeed);
    renderingDebug->AddChild(rd_enableShadow);
    renderingDebug->AddChild(rd_enableDebugRendering);
    renderingDebug->AddChild(rd_debugRenderingIndex);
    renderingDebug->AddChild(rd_debugSpotLightCulling);
    renderingDebug->AddChild(rd_underwaterDofDist);
    renderingDebug->AddChild(rd_underwaterDofNear);
    renderingDebug->AddChild(rd_underwaterDofFar);

    RegisterTweakable(renderingDebug);
    advance->AddChild(renderingDebug);

    // Occlusion subfolder
    auto occlusion = std::make_shared<TweakableFolder>(358, "Occlusion");
    auto occ_predictMainCamera = CreateSyncedBool(359, "PredictMainCamera", true);
    auto occ_predictFrames = CreateSyncedInt(360, "PredictFrames", 6, 0, 20);
    auto occ_predictShadowCamera = CreateSyncedBool(702, "PredictShadowCamera", true);
    auto occ_useExtraCascadeCullPlanes = CreateSyncedBool(703, "UseExtraCascadeCullPlanes", true);
    auto occ_perObject = CreateSyncedBool(713, "PerObject", true);
    auto occ_perNode = CreateSyncedBool(714, "PerNode", true);
    auto occ_perItem = CreateSyncedBool(715, "PerItem", true);
    auto occ_enable = CreateSyncedBool(716, "Enable", true);
    auto occ_alwaysFullCull = CreateSyncedBool(721, "alwaysFullCull", false);
    auto occ_deferredOcclusion = CreateSyncedBool(722, "deferredOcclusion", true);

    RegisterTweakable(occ_predictMainCamera);
    RegisterTweakable(occ_predictFrames);
    RegisterTweakable(occ_predictShadowCamera);
    RegisterTweakable(occ_useExtraCascadeCullPlanes);
    RegisterTweakable(occ_perObject);
    RegisterTweakable(occ_perNode);
    RegisterTweakable(occ_perItem);
    RegisterTweakable(occ_enable);
    RegisterTweakable(occ_alwaysFullCull);
    RegisterTweakable(occ_deferredOcclusion);

    occlusion->AddChild(occ_predictMainCamera);
    occlusion->AddChild(occ_predictFrames);
    occlusion->AddChild(occ_predictShadowCamera);
    occlusion->AddChild(occ_useExtraCascadeCullPlanes);
    occlusion->AddChild(occ_perObject);
    occlusion->AddChild(occ_perNode);
    occlusion->AddChild(occ_perItem);
    occlusion->AddChild(occ_enable);
    occlusion->AddChild(occ_alwaysFullCull);
    occlusion->AddChild(occ_deferredOcclusion);

    RegisterTweakable(occlusion);
    advance->AddChild(occlusion);

    // RGBM_3DLookup subfolder
    auto rgbm3DLookup = std::make_shared<TweakableFolder>(376, "RGBM_3DLookup");
    auto rgbm = CreateSyncedBool(377, "RGBM", false);
    auto lookup3D = CreateSyncedBool(378, "3DLookup", true);

    RegisterTweakable(rgbm);
    RegisterTweakable(lookup3D);

    rgbm3DLookup->AddChild(rgbm);
    rgbm3DLookup->AddChild(lookup3D);

    RegisterTweakable(rgbm3DLookup);
    advance->AddChild(rgbm3DLookup);

    auto rgbm3DLookupInt = CreateSyncedInt(552, "RGBM_3DLookup", 0, 0, 10);
    RegisterTweakable(rgbm3DLookupInt);
    advance->AddChild(rgbm3DLookupInt);

    // MSSAO subfolder
    auto mssao = std::make_shared<TweakableFolder>(553, "MSSAO");
    auto mssao_enable = CreateSyncedBool(554, "MSSAOEnable", true);
    auto mssao_intensity = CreateSyncedFloat(776, "IntensityMSSAO", 2.2f, 0.0f, 10.0f);
    auto mssao_nearRadius = CreateSyncedFloat(777, "t_ao_near_radius", 0.3f, 0.0f, 5.0f);
    auto mssao_farRadius = CreateSyncedFloat(778, "t_ao_far_radius", 20.0f, 0.0f, 100.0f);
    auto mssao_farDistance = CreateSyncedFloat(779, "t_ao_far_distance", 250.0f, 0.0f, 1000.0f);
    auto mssao_gammaDistance = CreateSyncedFloat(780, "t_ao_gamma_distance", 1.0f, 0.0f, 10.0f);
    auto mssao_angleBias = CreateSyncedFloat(781, "t_ao_angle_bias", 14.0f, 0.0f, 90.0f);
    auto mssao_frontFaceStrength = CreateSyncedFloat(782, "t_ao_front_face_strength", 0.4f, 0.0f, 2.0f);
    auto mssao_hbaoRadius = CreateSyncedFloat(783, "t_ao_hbao_radius", 0.5f, 0.0f, 5.0f);
    auto mssao_hbaoMaxPixelRadius = CreateSyncedFloat(784, "t_ao_hbao_max_pixel_radius", 0.25f, 0.0f, 2.0f);

    RegisterTweakable(mssao_enable);
    RegisterTweakable(mssao_intensity);
    RegisterTweakable(mssao_nearRadius);
    RegisterTweakable(mssao_farRadius);
    RegisterTweakable(mssao_farDistance);
    RegisterTweakable(mssao_gammaDistance);
    RegisterTweakable(mssao_angleBias);
    RegisterTweakable(mssao_frontFaceStrength);
    RegisterTweakable(mssao_hbaoRadius);
    RegisterTweakable(mssao_hbaoMaxPixelRadius);

    mssao->AddChild(mssao_enable);
    mssao->AddChild(mssao_intensity);
    mssao->AddChild(mssao_nearRadius);
    mssao->AddChild(mssao_farRadius);
    mssao->AddChild(mssao_farDistance);
    mssao->AddChild(mssao_gammaDistance);
    mssao->AddChild(mssao_angleBias);
    mssao->AddChild(mssao_frontFaceStrength);
    mssao->AddChild(mssao_hbaoRadius);
    mssao->AddChild(mssao_hbaoMaxPixelRadius);

    RegisterTweakable(mssao);
    advance->AddChild(mssao);

    auto parallaxMapping = CreateSyncedBool(555, "ParallaxMapping", true);
    auto decalBlurEnable = CreateSyncedBool(647, "DecalBlurEnable", true);
    auto decalBlurPasses = CreateSyncedInt(648, "DecalBlurPasses", 2, 0, 10);
    auto decalNewBlur = CreateSyncedBool(649, "DecalNewBlur", false);

    RegisterTweakable(parallaxMapping);
    RegisterTweakable(decalBlurEnable);
    RegisterTweakable(decalBlurPasses);
    RegisterTweakable(decalNewBlur);

    advance->AddChild(parallaxMapping);
    advance->AddChild(decalBlurEnable);
    advance->AddChild(decalBlurPasses);
    advance->AddChild(decalNewBlur);

    // Particle/Fog subfolder
    auto particleFog = std::make_shared<TweakableFolder>(650, "Particle/Fog");
    auto pf_renderBillboards = CreateSyncedBool(651, "RenderBillboards", true);
    auto pf_renderParticles = CreateSyncedBool(652, "RenderParticles", true);
    auto pf_particleOverdraw = CreateSyncedBool(659, "ParticleOverdraw", false);
    auto pf_forceFogOff = CreateSyncedBool(660, "forceFogOff", false);
    auto pf_particleFogOff = CreateSyncedBool(661, "particleFogOff", false);

    RegisterTweakable(pf_renderBillboards);
    RegisterTweakable(pf_renderParticles);
    RegisterTweakable(pf_particleOverdraw);
    RegisterTweakable(pf_forceFogOff);
    RegisterTweakable(pf_particleFogOff);

    particleFog->AddChild(pf_renderBillboards);
    particleFog->AddChild(pf_renderParticles);
    particleFog->AddChild(pf_particleOverdraw);
    particleFog->AddChild(pf_forceFogOff);
    particleFog->AddChild(pf_particleFogOff);

    RegisterTweakable(particleFog);
    advance->AddChild(particleFog);

    auto bloom = CreateSyncedBool(654, "Bloom", true);
    auto reloadShaders = CreateSyncedBool(656, "ReloadShaders", false);
    auto wireFrameMode = CreateSyncedBool(657, "WireFrameMode", false);
    auto disableVTJittering = CreateSyncedBool(658, "DisableVTJittering", false);
    auto postMeshesEnabled = CreateSyncedBool(662, "PostMeshesEnabled", true);
    auto lightFlareEnabled = CreateSyncedBool(663, "LightFlareEnabled", true);
    auto renderLightTileWithQuads = CreateSyncedBool(664, "RenderLightTileWithQuads", true);
    auto enableAA = CreateSyncedBool(667, "EnableAA", true);
    auto useDownsample4x4Optimize = CreateSyncedBool(668, "useDownsample4x4Optimize", true);

    RegisterTweakable(bloom);
    RegisterTweakable(reloadShaders);
    RegisterTweakable(wireFrameMode);
    RegisterTweakable(disableVTJittering);
    RegisterTweakable(postMeshesEnabled);
    RegisterTweakable(lightFlareEnabled);
    RegisterTweakable(renderLightTileWithQuads);
    RegisterTweakable(enableAA);
    RegisterTweakable(useDownsample4x4Optimize);

    advance->AddChild(bloom);
    advance->AddChild(reloadShaders);
    advance->AddChild(wireFrameMode);
    advance->AddChild(disableVTJittering);
    advance->AddChild(postMeshesEnabled);
    advance->AddChild(lightFlareEnabled);
    advance->AddChild(renderLightTileWithQuads);
    advance->AddChild(enableAA);
    advance->AddChild(useDownsample4x4Optimize);

    // Shadow subfolder
    auto shadow = std::make_shared<TweakableFolder>(669, "Shadow");
    auto sh_shadowBias = CreateSyncedFloat(670, "shadowBias", 0.004f, 0.0f, 0.1f);
    auto sh_preShadowZScale = CreateSyncedFloat(671, "PreShadowZScale", 0.97f, 0.0f, 2.0f);
    auto sh_preShadowZShift = CreateSyncedFloat(672, "PreShadowZShift", 0.03f, 0.0f, 1.0f);
    auto sh_shadowDebugLevel = CreateSyncedInt(673, "ShadowDebugLevel", 0, 0, 10);
    auto sh_pssmCullingRoundedWay = CreateSyncedInt(674, "PSSMCullingRoundedWay", 2, 0, 5);
    auto sh_pssmUseUniform = CreateSyncedBool(675, "PSSMUseUniform", false);
    auto sh_spotlightBiasHack = CreateSyncedBool(676, "SpotlightBiasHack", true);
    auto sh_pssmCompressShadowRate = CreateSyncedFloat(677, "PSSMCompressShadowRate", 1.0f, 0.0f, 5.0f);
    auto sh_pssmInterleavedRendering = CreateSyncedBool(678, "PSSMInterleavedRendering", true);

    RegisterTweakable(sh_shadowBias);
    RegisterTweakable(sh_preShadowZScale);
    RegisterTweakable(sh_preShadowZShift);
    RegisterTweakable(sh_shadowDebugLevel);
    RegisterTweakable(sh_pssmCullingRoundedWay);
    RegisterTweakable(sh_pssmUseUniform);
    RegisterTweakable(sh_spotlightBiasHack);
    RegisterTweakable(sh_pssmCompressShadowRate);
    RegisterTweakable(sh_pssmInterleavedRendering);

    shadow->AddChild(sh_shadowBias);
    shadow->AddChild(sh_preShadowZScale);
    shadow->AddChild(sh_preShadowZShift);
    shadow->AddChild(sh_shadowDebugLevel);
    shadow->AddChild(sh_pssmCullingRoundedWay);
    shadow->AddChild(sh_pssmUseUniform);
    shadow->AddChild(sh_spotlightBiasHack);
    shadow->AddChild(sh_pssmCompressShadowRate);
    shadow->AddChild(sh_pssmInterleavedRendering);

    // Scrolling subfolder (within Shadow)
    auto scrolling = std::make_shared<TweakableFolder>(679, "Scrolling");
    auto sc_pssmScrollingLogIndex = CreateSyncedInt(680, "PSSMScrollingLogIndex", 0, 0, 10);
    auto sc_pssmAlwaysInvalidCache = CreateSyncedBool(681, "PSSMAlwaysInvalidCache", false);
    auto sc_pssmScrollingCacheDebug = CreateSyncedInt(682, "PSSMScrollingCacheDebug", 0, 0, 10);
    auto sc_pssmLogPredictScrollOffset = CreateSyncedBool(683, "PSSMLogPredictScrollOffset", false);
    auto sc_pssmLogPredictOrthoChange = CreateSyncedBool(684, "PSSMLogPredictOrthoChange", false);
    auto sc_pssmOrthoChangePredictRate = CreateSyncedFloat(685, "PSSMOrthoChangePredictRate", 2.0f, 0.0f, 10.0f);
    auto sc_pssmOutOfRangePredictRate = CreateSyncedFloat(686, "PSSMOutOfRangePredictRate", 1.5f, 0.0f, 10.0f);
    auto sc_changeTerrainDynamicFlag = CreateSyncedBool(772, "ChangeTerrainDynamicFlag", true);
    auto sc_dynamicLODThreshold = CreateSyncedInt(773, "DynamicLODThreshold", 3, 0, 10);
    auto sc_changeTerrainDynamicFlagTess = CreateSyncedBool(786, "ChangeTerrainDynamicFlagTess", true);
    auto sc_dynamicLODThresholdTess = CreateSyncedInt(787, "DynamicLODThresholdTess", 2, 0, 10);

    RegisterTweakable(sc_pssmScrollingLogIndex);
    RegisterTweakable(sc_pssmAlwaysInvalidCache);
    RegisterTweakable(sc_pssmScrollingCacheDebug);
    RegisterTweakable(sc_pssmLogPredictScrollOffset);
    RegisterTweakable(sc_pssmLogPredictOrthoChange);
    RegisterTweakable(sc_pssmOrthoChangePredictRate);
    RegisterTweakable(sc_pssmOutOfRangePredictRate);
    RegisterTweakable(sc_changeTerrainDynamicFlag);
    RegisterTweakable(sc_dynamicLODThreshold);
    RegisterTweakable(sc_changeTerrainDynamicFlagTess);
    RegisterTweakable(sc_dynamicLODThresholdTess);

    scrolling->AddChild(sc_pssmScrollingLogIndex);
    scrolling->AddChild(sc_pssmAlwaysInvalidCache);
    scrolling->AddChild(sc_pssmScrollingCacheDebug);
    scrolling->AddChild(sc_pssmLogPredictScrollOffset);
    scrolling->AddChild(sc_pssmLogPredictOrthoChange);
    scrolling->AddChild(sc_pssmOrthoChangePredictRate);
    scrolling->AddChild(sc_pssmOutOfRangePredictRate);
    scrolling->AddChild(sc_changeTerrainDynamicFlag);
    scrolling->AddChild(sc_dynamicLODThreshold);
    scrolling->AddChild(sc_changeTerrainDynamicFlagTess);
    scrolling->AddChild(sc_dynamicLODThresholdTess);

    RegisterTweakable(scrolling);
    shadow->AddChild(scrolling);

    auto sh_pssmCullingAreaExtendMul = CreateSyncedFloat(687, "PSSMCullingAreaExtendMul", 1.05f, 0.0f, 5.0f);
    auto sh_pssmCullingAreaExtendAdd = CreateSyncedFloat(688, "PSSMCullingAreaExtendAdd", 5.0f, 0.0f, 50.0f);
    auto sh_pssmDepthMinMaxPredict = CreateSyncedFloat(689, "PSSMDepthMinMaxPredict", 6.0f, 0.0f, 20.0f);
    auto sh_useScryControlShadowPCFsteps = CreateSyncedBool(690, "UseScryControlShadowPCFsteps", false);
    auto sh_shadowPCFFilter = CreateSyncedInt(691, "ShadowPCFFilter", 1, 0, 10);
    auto sh_maxShadowViewDistance = CreateSyncedFloat(700, "MaxShadowViewDistance", 400.0f, 0.0f, 2000.0f);
    auto sh_enableTweakingShadowDrawDistance = CreateSyncedBool(701, "enableTweakingShadowDrawDistance", false);
    auto sh_fixedShadowRangeAllTime = CreateSyncedBool(704, "FixedShadowRangeAllTime", false);
    auto sh_limitCascadeChange = CreateSyncedBool(705, "LimitCascadeChange", true);
    auto sh_limitCascadeMinMax = CreateSyncedBool(706, "LimitCascadeMinMax", true);

    RegisterTweakable(sh_pssmCullingAreaExtendMul);
    RegisterTweakable(sh_pssmCullingAreaExtendAdd);
    RegisterTweakable(sh_pssmDepthMinMaxPredict);
    RegisterTweakable(sh_useScryControlShadowPCFsteps);
    RegisterTweakable(sh_shadowPCFFilter);
    RegisterTweakable(sh_maxShadowViewDistance);
    RegisterTweakable(sh_enableTweakingShadowDrawDistance);
    RegisterTweakable(sh_fixedShadowRangeAllTime);
    RegisterTweakable(sh_limitCascadeChange);
    RegisterTweakable(sh_limitCascadeMinMax);

    shadow->AddChild(sh_pssmCullingAreaExtendMul);
    shadow->AddChild(sh_pssmCullingAreaExtendAdd);
    shadow->AddChild(sh_pssmDepthMinMaxPredict);
    shadow->AddChild(sh_useScryControlShadowPCFsteps);
    shadow->AddChild(sh_shadowPCFFilter);
    shadow->AddChild(sh_maxShadowViewDistance);
    shadow->AddChild(sh_enableTweakingShadowDrawDistance);
    shadow->AddChild(sh_fixedShadowRangeAllTime);
    shadow->AddChild(sh_limitCascadeChange);
    shadow->AddChild(sh_limitCascadeMinMax);

    RegisterTweakable(shadow);
    advance->AddChild(shadow);

    // GlobalAmbient subfolder
    auto globalAmbient = std::make_shared<TweakableFolder>(692, "GlobalAmbient");
    auto ga_rebake = CreateSyncedBool(693, "GlobalAmbientRebake", false);
    auto ga_enabled = CreateSyncedBool(738, "Enabled", false);
    auto ga_blend = CreateSyncedFloat(739, "GlobalAmbientBlend", 0.0f, 0.0f, 2.0f);
    auto ga_normalScaleXY = CreateSyncedFloat(740, "NormalScaleXY", 1.6f, 0.0f, 10.0f);
    auto ga_normalScaleZ = CreateSyncedFloat(741, "NormalScaleZ", 1.0f, 0.0f, 10.0f);
    auto ga_maxDeltaHeight = CreateSyncedFloat(742, "MaxDeltaHeight", 200.0f, 0.0f, 1000.0f);
    auto ga_pixelOcclusionFalloffY = CreateSyncedFloat(743, "PixelOcclusionFalloffY", 16.0f, 0.0f, 100.0f);
    auto ga_pixelOcclusionMin = CreateSyncedFloat(744, "PixelOcclusionMin", 0.65f, 0.0f, 2.0f);
    auto ga_zRangeMultiplier = CreateSyncedFloat(745, "ZRangeMultiplier", 128.0f, 0.0f, 500.0f);
    auto ga_zRangeOffset = CreateSyncedFloat(746, "ZRangeOffset", 0.0f, 0.0f, 100.0f);
    auto ga_intensity = CreateSyncedFloat(747, "Intensity", 0.23f, 0.0f, 5.0f);

    RegisterTweakable(ga_rebake);
    RegisterTweakable(ga_enabled);
    RegisterTweakable(ga_blend);
    RegisterTweakable(ga_normalScaleXY);
    RegisterTweakable(ga_normalScaleZ);
    RegisterTweakable(ga_maxDeltaHeight);
    RegisterTweakable(ga_pixelOcclusionFalloffY);
    RegisterTweakable(ga_pixelOcclusionMin);
    RegisterTweakable(ga_zRangeMultiplier);
    RegisterTweakable(ga_zRangeOffset);
    RegisterTweakable(ga_intensity);

    globalAmbient->AddChild(ga_rebake);
    globalAmbient->AddChild(ga_enabled);
    globalAmbient->AddChild(ga_blend);
    globalAmbient->AddChild(ga_normalScaleXY);
    globalAmbient->AddChild(ga_normalScaleZ);
    globalAmbient->AddChild(ga_maxDeltaHeight);
    globalAmbient->AddChild(ga_pixelOcclusionFalloffY);
    globalAmbient->AddChild(ga_pixelOcclusionMin);
    globalAmbient->AddChild(ga_zRangeMultiplier);
    globalAmbient->AddChild(ga_zRangeOffset);
    globalAmbient->AddChild(ga_intensity);

    RegisterTweakable(globalAmbient);
    advance->AddChild(globalAmbient);

    // AverageLuminance subfolder
    auto averageLuminance = std::make_shared<TweakableFolder>(694, "AverageLuminance");
    auto al_debugEnabled = CreateSyncedBool(695, "DebugEnabled", false);
    auto al_averageLuminance = CreateSyncedFloat(696, "averageLuminance", 1.0f, 0.0f, 10.0f);
    auto al_averageLuminanceBlend = CreateSyncedFloat(697, "averageLuminanceBlend", 0.5f, 0.0f, 2.0f);

    RegisterTweakable(al_debugEnabled);
    RegisterTweakable(al_averageLuminance);
    RegisterTweakable(al_averageLuminanceBlend);

    averageLuminance->AddChild(al_debugEnabled);
    averageLuminance->AddChild(al_averageLuminance);
    averageLuminance->AddChild(al_averageLuminanceBlend);

    RegisterTweakable(averageLuminance);
    advance->AddChild(averageLuminance);

    auto windGustStr = CreateSyncedFloat(698, "windGustStr", 1.5f, 0.0f, 10.0f);
    auto debugSkyCubemapMipLevel = CreateSyncedInt(699, "DebugSkyCubemapMipLevel", 0, 0, 10);

    RegisterTweakable(windGustStr);
    RegisterTweakable(debugSkyCubemapMipLevel);

    advance->AddChild(windGustStr);
    advance->AddChild(debugSkyCubemapMipLevel);

    // Bokeh subfolder
    auto bokeh = std::make_shared<TweakableFolder>(707, "Bokeh");
    auto bk_singleVP = CreateSyncedBool(708, "SingleVP", false);
    auto bk_debugView = CreateSyncedBool(709, "DebugView", false);
    auto bk_optimization = CreateSyncedBool(710, "Optimization", true);
    auto bk_brightnessThreshold = CreateSyncedFloat(748, "BrightnessThreshold", 1.2f, 0.0f, 10.0f);
    auto bk_cocThreshold = CreateSyncedFloat(749, "CoCThreshold", 0.7f, 0.0f, 2.0f);
    auto bk_blurSize = CreateSyncedFloat(750, "BlurSize", 32.0f, 0.0f, 100.0f);
    auto bk_blurLimitNear = CreateSyncedFloat(751, "BlurLimitNear", 0.9f, 0.0f, 2.0f);
    auto bk_blurLimitFar = CreateSyncedFloat(752, "BlurLimitFar", 0.5f, 0.0f, 2.0f);
    auto bk_lowResRange = CreateSyncedFloat(753, "LowResRange", 0.75f, 0.0f, 2.0f);
    auto bk_enableParamDebug = CreateSyncedBool(754, "EnableParamDebug", false);

    RegisterTweakable(bk_singleVP);
    RegisterTweakable(bk_debugView);
    RegisterTweakable(bk_optimization);
    RegisterTweakable(bk_brightnessThreshold);
    RegisterTweakable(bk_cocThreshold);
    RegisterTweakable(bk_blurSize);
    RegisterTweakable(bk_blurLimitNear);
    RegisterTweakable(bk_blurLimitFar);
    RegisterTweakable(bk_lowResRange);
    RegisterTweakable(bk_enableParamDebug);

    bokeh->AddChild(bk_singleVP);
    bokeh->AddChild(bk_debugView);
    bokeh->AddChild(bk_optimization);
    bokeh->AddChild(bk_brightnessThreshold);
    bokeh->AddChild(bk_cocThreshold);
    bokeh->AddChild(bk_blurSize);
    bokeh->AddChild(bk_blurLimitNear);
    bokeh->AddChild(bk_blurLimitFar);
    bokeh->AddChild(bk_lowResRange);
    bokeh->AddChild(bk_enableParamDebug);

    RegisterTweakable(bokeh);
    advance->AddChild(bokeh);

    // CMAA subfolder
    auto cmaa = std::make_shared<TweakableFolder>(711, "CMAA");
    auto cmaa_enable = CreateSyncedBool(712, "Enable", true);
    auto cmaa_edgeDetectionThreshold = CreateSyncedFloat(736, "EdgeDetectionThreshold", 0.0769231f, 0.0f, 1.0f);
    auto cmaa_nonDominantEdgeRemovalAmount = CreateSyncedFloat(737, "NonDominantEdgeRemovalAmount", 0.35f, 0.0f, 2.0f);

    RegisterTweakable(cmaa_enable);
    RegisterTweakable(cmaa_edgeDetectionThreshold);
    RegisterTweakable(cmaa_nonDominantEdgeRemovalAmount);

    cmaa->AddChild(cmaa_enable);
    cmaa->AddChild(cmaa_edgeDetectionThreshold);
    cmaa->AddChild(cmaa_nonDominantEdgeRemovalAmount);

    RegisterTweakable(cmaa);
    advance->AddChild(cmaa);

    auto instancingEnable = CreateSyncedBool(717, "InstancingEnable", true);
    auto lowEndLodRange = CreateSyncedFloat(718, "lowEndLodRange", 0.6f, 0.0f, 2.0f);
    auto highEndLodRange = CreateSyncedFloat(719, "highEndLodRange", 1.0f, 0.0f, 2.0f);
    auto cullSpotLightByCone = CreateSyncedBool(720, "CullSpotLightByCone", true);

    RegisterTweakable(instancingEnable);
    RegisterTweakable(lowEndLodRange);
    RegisterTweakable(highEndLodRange);
    RegisterTweakable(cullSpotLightByCone);

    advance->AddChild(instancingEnable);
    advance->AddChild(lowEndLodRange);
    advance->AddChild(highEndLodRange);
    advance->AddChild(cullSpotLightByCone);

    // LocalReflectionParameter subfolder
    auto localReflectionParameter = std::make_shared<TweakableFolder>(758, "LocalReflectionParameter");
    auto lr_minAmbientFresnel = CreateSyncedFloat(759, "MinAmbientFresnel", 0.001f, 0.0f, 1.0f);
    auto lr_initRayStepLength = CreateSyncedFloat(760, "InitRayStepLength", 2.0f, 0.0f, 10.0f);
    auto lr_maxRayJittering = CreateSyncedFloat(761, "MaxRayJittering", 0.1f, 0.0f, 2.0f);
    auto lr_vignetteSizeScale = CreateSyncedFloat(762, "VignetteSizeScale", -5.0f, -20.0f, 20.0f);
    auto lr_vignetteSizeBias = CreateSyncedFloat(763, "VignetteSizeBias", 2.5f, 0.0f, 10.0f);
    auto lr_maxReflectionBrightness = CreateSyncedFloat(764, "MaxReflectionBrightness", 0.8f, 0.0f, 5.0f);
    auto lr_reflectionScale = CreateSyncedFloat(765, "ReflectionScale", 2.0f, 0.0f, 10.0f);
    auto lr_maxRayDistance = CreateSyncedFloat(766, "MaxRayDistance", 5.0f, 0.0f, 50.0f);
    auto lr_maxRayDelta = CreateSyncedFloat(767, "MaxRayDelta", 0.125f, 0.0f, 2.0f);
    auto lr_maxRayStep = CreateSyncedFloat(768, "MaxRayStep", 400.0f, 0.0f, 2000.0f);
    auto lr_farClipDistance = CreateSyncedFloat(769, "FarClipDistance", 0.999f, 0.0f, 2.0f);
    auto lr_reflectionBlurriness = CreateSyncedFloat(770, "ReflectionBlurriness", 300.0f, 0.0f, 1000.0f);
    auto lr_minBlurRadius = CreateSyncedFloat(771, "MinBlurRadius", 2.0f, 0.0f, 10.0f);

    RegisterTweakable(lr_minAmbientFresnel);
    RegisterTweakable(lr_initRayStepLength);
    RegisterTweakable(lr_maxRayJittering);
    RegisterTweakable(lr_vignetteSizeScale);
    RegisterTweakable(lr_vignetteSizeBias);
    RegisterTweakable(lr_maxReflectionBrightness);
    RegisterTweakable(lr_reflectionScale);
    RegisterTweakable(lr_maxRayDistance);
    RegisterTweakable(lr_maxRayDelta);
    RegisterTweakable(lr_maxRayStep);
    RegisterTweakable(lr_farClipDistance);
    RegisterTweakable(lr_reflectionBlurriness);
    RegisterTweakable(lr_minBlurRadius);

    localReflectionParameter->AddChild(lr_minAmbientFresnel);
    localReflectionParameter->AddChild(lr_initRayStepLength);
    localReflectionParameter->AddChild(lr_maxRayJittering);
    localReflectionParameter->AddChild(lr_vignetteSizeScale);
    localReflectionParameter->AddChild(lr_vignetteSizeBias);
    localReflectionParameter->AddChild(lr_maxReflectionBrightness);
    localReflectionParameter->AddChild(lr_reflectionScale);
    localReflectionParameter->AddChild(lr_maxRayDistance);
    localReflectionParameter->AddChild(lr_maxRayDelta);
    localReflectionParameter->AddChild(lr_maxRayStep);
    localReflectionParameter->AddChild(lr_farClipDistance);
    localReflectionParameter->AddChild(lr_reflectionBlurriness);
    localReflectionParameter->AddChild(lr_minBlurRadius);

    RegisterTweakable(localReflectionParameter);
    advance->AddChild(localReflectionParameter);

    auto terrainLODEdgesOnly = CreateSyncedBool(774, "TerrainLODEdgesOnly", true);
    auto terrainLODEdgesAreFat = CreateSyncedBool(775, "TerrainLODEdgesAreFat", true);
    auto tessQuality = CreateSyncedFloat(785, "TessQuality", 0.3f, 0.0f, 2.0f);

    RegisterTweakable(terrainLODEdgesOnly);
    RegisterTweakable(terrainLODEdgesAreFat);
    RegisterTweakable(tessQuality);

    advance->AddChild(terrainLODEdgesOnly);
    advance->AddChild(terrainLODEdgesAreFat);
    advance->AddChild(tessQuality);

    RegisterTweakable(advance);
    graphic->AddChild(advance);

    RegisterTweakable(graphic);
    m_rootFolders.push_back(graphic);
}

void DevMenu::InitializePodium() {
    auto podium = std::make_shared<TweakableFolder>(372, "Podium");

    auto cameraOffsetX = CreateSyncedFloat(373, "cameraOffsetX", -2.6f, -10.0f, 10.0f);
    auto cameraOffsetY = CreateSyncedFloat(374, "cameraOffsetY", 0.0f, -10.0f, 10.0f);
    auto cameraOffsetZ = CreateSyncedFloat(375, "cameraOffsetZ", 0.0f, -10.0f, 10.0f);

    RegisterTweakable(cameraOffsetX);
    RegisterTweakable(cameraOffsetY);
    RegisterTweakable(cameraOffsetZ);

    podium->AddChild(cameraOffsetX);
    podium->AddChild(cameraOffsetY);
    podium->AddChild(cameraOffsetZ);

    RegisterTweakable(podium);
    m_rootFolders.push_back(podium);
}

void DevMenu::InitializeXPSystem() {
    auto xpSystem = std::make_shared<TweakableFolder>(492, "XPSystem");

    auto level1XPLimit = CreateSyncedInt(493, "Level1XPLimit", 100, 0, 100000);
    auto levelXPLimitVal1 = CreateSyncedInt(494, "LevelXPLimitVal1 (Val1 * (Val2 * level + Val3) * (level - 1))", 12, 0, 1000);
    auto levelXPLimitVal2 = CreateSyncedInt(495, "LevelXPLimitVal2", 4, 0, 1000);
    auto levelXPLimitVal3 = CreateSyncedInt(496, "LevelXPLimitVal3", 72, 0, 1000);
    auto xpRewardBeginnerTrackCompleted = CreateSyncedInt(497, "XPRewardBeginnerTrackCompleted", 0, 0, 10000);
    auto xpRewardEasyTrackCompleted = CreateSyncedInt(498, "XPRewardEasyTrackCompleted", 0, 0, 10000);
    auto xpRewardMediumTrackCompleted = CreateSyncedInt(499, "XPRewardMediumTrackCompleted", 0, 0, 10000);
    auto xpRewardHardTrackCompleted = CreateSyncedInt(500, "XPRewardHardTrackCompleted", 0, 0, 10000);
    auto xpRewardExtremeTrackCompleted = CreateSyncedInt(501, "XPRewardExtremeTrackCompleted", 0, 0, 10000);
    auto xpRewardTraining1Passed = CreateSyncedInt(502, "XPRewardTraining1Passed", 100, 0, 10000);
    auto xpRewardTraining2Passed = CreateSyncedInt(503, "XPRewardTraining2Passed", 1000, 0, 10000);
    auto xpRewardTraining3Passed = CreateSyncedInt(504, "XPRewardTraining3Passed", 2000, 0, 10000);
    auto xpRewardTrainingFMXPassed = CreateSyncedInt(505, "XPRewardTrainingFMXPassed", 2000, 0, 10000);
    auto xpRewardTraining5Passed = CreateSyncedInt(506, "XPRewardTraining5Passed", 4500, 0, 10000);
    auto xpRewardBronzeMedal = CreateSyncedInt(507, "XPRewardBronzeMedal", 200, 0, 10000);
    auto xpRewardSilverMedal = CreateSyncedInt(508, "XPRewardSilverMedal", 300, 0, 10000);
    auto xpRewardGoldMedal = CreateSyncedInt(509, "XPRewardGoldMedal", 500, 0, 10000);
    auto xpRewardPlatinumMedal = CreateSyncedInt(510, "XPRewardPlatinumMedal", 1000, 0, 10000);
    auto xpRewardUGCTrackCompleted = CreateSyncedInt(511, "XPRewardUGCTrackCompleted", 50, 0, 10000);
    auto xpRewardMPTournamentPlayed = CreateSyncedInt(512, "XPRewardMPTournamentPlayed", 0, 0, 10000);
    auto xpRewardAllBeginnerTracksCompleted = CreateSyncedInt(513, "XPRewardAllBeginnerTracksCompleted", 0, 0, 10000);
    auto xpRewardAllEasyTracksCompleted = CreateSyncedInt(514, "XPRewardAllEasyTracksCompleted", 0, 0, 10000);
    auto xpRewardAllMediumTracksCompleted = CreateSyncedInt(515, "XPRewardAllMediumTracksCompleted", 0, 0, 10000);
    auto xpRewardAllHardTracksCompleted = CreateSyncedInt(516, "XPRewardAllHardTracksCompleted", 0, 0, 10000);
    auto xpRewardAllExtremeTracksCompleted = CreateSyncedInt(517, "XPRewardAllExtremeTracksCompleted", 0, 0, 10000);

    RegisterTweakable(level1XPLimit);
    RegisterTweakable(levelXPLimitVal1);
    RegisterTweakable(levelXPLimitVal2);
    RegisterTweakable(levelXPLimitVal3);
    RegisterTweakable(xpRewardBeginnerTrackCompleted);
    RegisterTweakable(xpRewardEasyTrackCompleted);
    RegisterTweakable(xpRewardMediumTrackCompleted);
    RegisterTweakable(xpRewardHardTrackCompleted);
    RegisterTweakable(xpRewardExtremeTrackCompleted);
    RegisterTweakable(xpRewardTraining1Passed);
    RegisterTweakable(xpRewardTraining2Passed);
    RegisterTweakable(xpRewardTraining3Passed);
    RegisterTweakable(xpRewardTrainingFMXPassed);
    RegisterTweakable(xpRewardTraining5Passed);
    RegisterTweakable(xpRewardBronzeMedal);
    RegisterTweakable(xpRewardSilverMedal);
    RegisterTweakable(xpRewardGoldMedal);
    RegisterTweakable(xpRewardPlatinumMedal);
    RegisterTweakable(xpRewardUGCTrackCompleted);
    RegisterTweakable(xpRewardMPTournamentPlayed);
    RegisterTweakable(xpRewardAllBeginnerTracksCompleted);
    RegisterTweakable(xpRewardAllEasyTracksCompleted);
    RegisterTweakable(xpRewardAllMediumTracksCompleted);
    RegisterTweakable(xpRewardAllHardTracksCompleted);
    RegisterTweakable(xpRewardAllExtremeTracksCompleted);

    xpSystem->AddChild(level1XPLimit);
    xpSystem->AddChild(levelXPLimitVal1);
    xpSystem->AddChild(levelXPLimitVal2);
    xpSystem->AddChild(levelXPLimitVal3);
    xpSystem->AddChild(xpRewardBeginnerTrackCompleted);
    xpSystem->AddChild(xpRewardEasyTrackCompleted);
    xpSystem->AddChild(xpRewardMediumTrackCompleted);
    xpSystem->AddChild(xpRewardHardTrackCompleted);
    xpSystem->AddChild(xpRewardExtremeTrackCompleted);
    xpSystem->AddChild(xpRewardTraining1Passed);
    xpSystem->AddChild(xpRewardTraining2Passed);
    xpSystem->AddChild(xpRewardTraining3Passed);
    xpSystem->AddChild(xpRewardTrainingFMXPassed);
    xpSystem->AddChild(xpRewardTraining5Passed);
    xpSystem->AddChild(xpRewardBronzeMedal);
    xpSystem->AddChild(xpRewardSilverMedal);
    xpSystem->AddChild(xpRewardGoldMedal);
    xpSystem->AddChild(xpRewardPlatinumMedal);
    xpSystem->AddChild(xpRewardUGCTrackCompleted);
    xpSystem->AddChild(xpRewardMPTournamentPlayed);
    xpSystem->AddChild(xpRewardAllBeginnerTracksCompleted);
    xpSystem->AddChild(xpRewardAllEasyTracksCompleted);
    xpSystem->AddChild(xpRewardAllMediumTracksCompleted);
    xpSystem->AddChild(xpRewardAllHardTracksCompleted);
    xpSystem->AddChild(xpRewardAllExtremeTracksCompleted);

    RegisterTweakable(xpSystem);
    m_rootFolders.push_back(xpSystem);
}

void DevMenu::InitializeTrackUpload() {
    auto trackUpload = std::make_shared<TweakableFolder>(518, "TrackUpload");

    // Add all TrackUpload tweakables
    auto corruptTracks = CreateSyncedBool(519, "corruptTracks", false);

    RegisterTweakable(corruptTracks);

    trackUpload->AddChild(corruptTracks);

    RegisterTweakable(trackUpload);
    m_rootFolders.push_back(trackUpload);
}

void DevMenu::InitializeGameOption() {
    auto gameOption = std::make_shared<TweakableFolder>(541, "GameOption");

    // VideoOption subfolder
    auto videoOption = std::make_shared<TweakableFolder>(542, "VideoOption");
    auto fullScreen = CreateSyncedBool(543, "FullScreen", true);
    auto vsync = CreateSyncedBool(544, "Vsync", true);
    auto fxaa = CreateSyncedInt(545, "FXAA", -1, -1, 2);
    auto showFoliage = CreateSyncedBool(546, "ShowFoliage", true);
    auto shadowQuality = CreateSyncedInt(547, "ShadowQuality", 1, 0, 3);
    auto graphicQuality = CreateSyncedInt(548, "GraphicQuality(low/normal/high)", 1, 0, 2);
    auto geometryQuality = CreateSyncedInt(549, "GeometryQuality(low/high)", 0, 0, 1);
    auto bloomQuality = CreateSyncedInt(550, "BloomQuality(low/high)", 0, 0, 1);
    auto particleQuality = CreateSyncedInt(551, "ParticleQuality(normal/high/ultra)", 0, 0, 2);
    auto bokehDof = CreateSyncedBool(556, "BokehDof", false);

    RegisterTweakable(fullScreen);
    RegisterTweakable(vsync);
    RegisterTweakable(fxaa);
    RegisterTweakable(showFoliage);
    RegisterTweakable(shadowQuality);
    RegisterTweakable(graphicQuality);
    RegisterTweakable(geometryQuality);
    RegisterTweakable(bloomQuality);
    RegisterTweakable(particleQuality);
    RegisterTweakable(bokehDof);

    videoOption->AddChild(fullScreen);
    videoOption->AddChild(vsync);
    videoOption->AddChild(fxaa);
    videoOption->AddChild(showFoliage);
    videoOption->AddChild(shadowQuality);
    videoOption->AddChild(graphicQuality);
    videoOption->AddChild(geometryQuality);
    videoOption->AddChild(bloomQuality);
    videoOption->AddChild(particleQuality);
    videoOption->AddChild(bokehDof);

    RegisterTweakable(videoOption);
    gameOption->AddChild(videoOption);

    RegisterTweakable(gameOption);
    m_rootFolders.push_back(gameOption);
}

void DevMenu::InitializeGameSwf() {
    auto gameSwf = std::make_shared<TweakableFolder>(560, "GameSwf");

    // Top-level GameSwf tweakables
    auto onlineMenuNormalSkip = CreateSyncedInt(561, "OnlineMenuNormalSkip", 1, 0, 10);
    auto onlineMenuUpdateMaxSkip = CreateSyncedInt(562, "OnlineMenuUpdateMaxSkip", 4, 0, 10);

    RegisterTweakable(onlineMenuNormalSkip);
    RegisterTweakable(onlineMenuUpdateMaxSkip);

    gameSwf->AddChild(onlineMenuNormalSkip);
    gameSwf->AddChild(onlineMenuUpdateMaxSkip);

    // Render subfolder
    auto render = std::make_shared<TweakableFolder>(620, "Render");
    auto minTextAutoScale = CreateSyncedFloat(621, "MinTextAutoScale", 0.25f, 0.0f, 2.0f);
    auto textScaleMultiplier = CreateSyncedFloat(622, "TextScaleMultiplier", 1.0f, 0.0f, 5.0f);
    auto zDepth = CreateSyncedFloat(792, "Z-Depth", 0.01f, 0.0f, 1.0f);
    auto resolution = CreateSyncedFloat(793, "Resolution", 1.0f, 0.0f, 4.0f);
    auto textureScale = CreateSyncedFloat(794, "TextureScale", 1.0f, 0.0f, 4.0f);

    RegisterTweakable(minTextAutoScale);
    RegisterTweakable(textScaleMultiplier);
    RegisterTweakable(zDepth);
    RegisterTweakable(resolution);
    RegisterTweakable(textureScale);

    render->AddChild(minTextAutoScale);
    render->AddChild(textScaleMultiplier);
    render->AddChild(zDepth);
    render->AddChild(resolution);
    render->AddChild(textureScale);

    RegisterTweakable(render);
    gameSwf->AddChild(render);

    // Video subfolder
    auto video = std::make_shared<TweakableFolder>(795, "Video");
    auto textureColorR = CreateSyncedFloat(796, "TextureColorR", 1.0f, 0.0f, 2.0f);
    auto textureColorG = CreateSyncedFloat(797, "TextureColorG", 1.0f, 0.0f, 2.0f);
    auto textureColorB = CreateSyncedFloat(798, "TextureColorB", 1.0f, 0.0f, 2.0f);
    auto textureColorA = CreateSyncedFloat(799, "TextureColorA", 1.0f, 0.0f, 2.0f);
    auto textureAddColorR = CreateSyncedFloat(800, "TextureAddColorR", 1.0f, 0.0f, 2.0f);
    auto textureAddColorG = CreateSyncedFloat(801, "TextureAddColorG", 1.0f, 0.0f, 2.0f);
    auto textureAddColorB = CreateSyncedFloat(802, "TextureAddColorB", 1.0f, 0.0f, 2.0f);
    auto textureAddColorA = CreateSyncedFloat(803, "TextureAddColorA", 1.0f, 0.0f, 2.0f);
    auto fillMode = CreateSyncedInt(804, "FillMode", 0, 0, 5);
    auto technique = CreateSyncedInt(805, "Technique", 1, 0, 5);
    auto alphaModX = CreateSyncedFloat(806, "AlphaModX", 1.0f, 0.0f, 2.0f);
    auto alphaModY = CreateSyncedFloat(807, "AlphaModY", 1.0f, 0.0f, 2.0f);

    RegisterTweakable(textureColorR);
    RegisterTweakable(textureColorG);
    RegisterTweakable(textureColorB);
    RegisterTweakable(textureColorA);
    RegisterTweakable(textureAddColorR);
    RegisterTweakable(textureAddColorG);
    RegisterTweakable(textureAddColorB);
    RegisterTweakable(textureAddColorA);
    RegisterTweakable(fillMode);
    RegisterTweakable(technique);
    RegisterTweakable(alphaModX);
    RegisterTweakable(alphaModY);

    video->AddChild(textureColorR);
    video->AddChild(textureColorG);
    video->AddChild(textureColorB);
    video->AddChild(textureColorA);
    video->AddChild(textureAddColorR);
    video->AddChild(textureAddColorG);
    video->AddChild(textureAddColorB);
    video->AddChild(textureAddColorA);
    video->AddChild(fillMode);
    video->AddChild(technique);
    video->AddChild(alphaModX);
    video->AddChild(alphaModY);

    RegisterTweakable(video);
    gameSwf->AddChild(video);

    RegisterTweakable(gameSwf);
    m_rootFolders.push_back(gameSwf);
}

void DevMenu::InitializeGameTime() {
    auto gameTime = std::make_shared<TweakableFolder>(563, "GameTime");

    // Add all GameTime tweakables
    auto logicTimeMaxAhead = CreateSyncedInt(564, "LogicTimeMaxAhead", 4000000, 0, 100000000);
    auto logicTimeMaxBehind = CreateSyncedInt(565, "LogicTimeMaxBehind", 10000000, 0, 100000000);

    RegisterTweakable(logicTimeMaxAhead);
    RegisterTweakable(logicTimeMaxBehind);

    gameTime->AddChild(logicTimeMaxAhead);
    gameTime->AddChild(logicTimeMaxBehind);

    RegisterTweakable(gameTime);
    m_rootFolders.push_back(gameTime);
}

void DevMenu::InitializeVariableFramerate() {
    auto variableFramerate = std::make_shared<TweakableFolder>(566, "VariableFramerateDebug");

    // Add all VariableFramerateDebug tweakables
    auto gpuStall = CreateSyncedInt(567, "GPUStall", 0, 0, 100);
    auto limitingAllowed = CreateSyncedBool(568, "LimitingAllowed", true);
    auto fastForwardRatio = CreateSyncedFloat(569, "FastForwardRatio", 2.0f, 0.0f, 10.0f);
    auto slowMotionRatio = CreateSyncedInt(570, "SlowMotionRatio", 5, 0, 20);
    auto cameraShowInterpolatedAmplitude = CreateSyncedFloat(631, "CameraShowInterpolatedAmplitude", 0.0f, 0.0f, 10.0f);
    auto cameraShowInterpolatedFrequency = CreateSyncedFloat(632, "CameraShowInterpolatedFrequency", 1.0f, 0.0f, 10.0f);
    auto cameraDisableWholeThing = CreateSyncedBool(633, "CameraDisableWholeThing", false);
    auto showInterpolatedObjects = CreateSyncedBool(634, "ShowInterpolatedObjects", false);
    auto doTranslation = CreateSyncedBool(635, "DoTranslation", true);
    auto doRotation = CreateSyncedBool(636, "DoRotation", true);
    auto disableWholeThing = CreateSyncedBool(637, "DisableWholeThing", false);
    auto doNothingInInterpolateMatrices = CreateSyncedBool(638, "DoNothingInInterpolateMatrices", false);
    auto useAsRotation = CreateSyncedBool(639, "UseAsRotation", false);
    auto useAsTranslation = CreateSyncedBool(640, "UseAsTranslation", false);
    auto showInterpolatedBoneObjects = CreateSyncedBool(645, "ShowInterpolatedBoneObjects", false);
    auto disableWholeThingBone = CreateSyncedBool(646, "DisableWholeThingBone", true);
    auto alpha = CreateSyncedFloat(732, "Alpha", 1.0f, 0.0f, 2.0f);
    auto negAlpha = CreateSyncedBool(733, "NegAlpha", false);

    RegisterTweakable(gpuStall);
    RegisterTweakable(limitingAllowed);
    RegisterTweakable(fastForwardRatio);
    RegisterTweakable(slowMotionRatio);
    RegisterTweakable(cameraShowInterpolatedAmplitude);
    RegisterTweakable(cameraShowInterpolatedFrequency);
    RegisterTweakable(cameraDisableWholeThing);
    RegisterTweakable(showInterpolatedObjects);
    RegisterTweakable(doTranslation);
    RegisterTweakable(doRotation);
    RegisterTweakable(disableWholeThing);
    RegisterTweakable(doNothingInInterpolateMatrices);
    RegisterTweakable(useAsRotation);
    RegisterTweakable(useAsTranslation);
    RegisterTweakable(showInterpolatedBoneObjects);
    RegisterTweakable(disableWholeThingBone);
    RegisterTweakable(alpha);
    RegisterTweakable(negAlpha);

    variableFramerate->AddChild(gpuStall);
    variableFramerate->AddChild(limitingAllowed);
    variableFramerate->AddChild(fastForwardRatio);
    variableFramerate->AddChild(slowMotionRatio);
    variableFramerate->AddChild(cameraShowInterpolatedAmplitude);
    variableFramerate->AddChild(cameraShowInterpolatedFrequency);
    variableFramerate->AddChild(cameraDisableWholeThing);
    variableFramerate->AddChild(showInterpolatedObjects);
    variableFramerate->AddChild(doTranslation);
    variableFramerate->AddChild(doRotation);
    variableFramerate->AddChild(disableWholeThing);
    variableFramerate->AddChild(doNothingInInterpolateMatrices);
    variableFramerate->AddChild(useAsRotation);
    variableFramerate->AddChild(useAsTranslation);
    variableFramerate->AddChild(showInterpolatedBoneObjects);
    variableFramerate->AddChild(disableWholeThingBone);
    variableFramerate->AddChild(alpha);
    variableFramerate->AddChild(negAlpha);

    RegisterTweakable(variableFramerate);
    m_rootFolders.push_back(variableFramerate);
}

void DevMenu::InitializeDebug() {
    auto debug = std::make_shared<TweakableFolder>(601, "Debug");

    // Add all Debug tweakables
    auto scoreSaveStateOverride = CreateSyncedInt(602, "ScoreSaveStateOverride", 0, 0, 10);

    RegisterTweakable(scoreSaveStateOverride);

    debug->AddChild(scoreSaveStateOverride);

    RegisterTweakable(debug);
    m_rootFolders.push_back(debug);
}

void DevMenu::InitializeInGameHud() {
    auto inGameHud = std::make_shared<TweakableFolder>(603, "InGameHud");

    // Add all InGameHud tweakables
    auto multiplayerMarkerInterpolation = CreateSyncedFloat(604, "MultiplayerMarkerInterpolation", 0.98f, 0.0f, 1.0f);

    RegisterTweakable(multiplayerMarkerInterpolation);

    inGameHud->AddChild(multiplayerMarkerInterpolation);

    RegisterTweakable(inGameHud);
    m_rootFolders.push_back(inGameHud);
}

void DevMenu::InitializeMainHub() {
    auto mainHub = std::make_shared<TweakableFolder>(606, "MainHub");

    // Add all MainHub tweakables
    auto tournamentsEnabled = CreateSyncedBool(607, "TournamentsEnabled", true);
    auto disableUplayCheck = CreateSyncedBool(613, "Disable Uplay Check", true);

    RegisterTweakable(tournamentsEnabled);
    RegisterTweakable(disableUplayCheck);

    mainHub->AddChild(tournamentsEnabled);
    mainHub->AddChild(disableUplayCheck);

    RegisterTweakable(mainHub);
    m_rootFolders.push_back(mainHub);
}

void DevMenu::InitializeMainMenu() {
    auto mainMenu = std::make_shared<TweakableFolder>(608, "MainMenu");

    // Add all MainMenu tweakables
    auto bannerContentPackId = CreateSyncedInt(609, "BannerContentPackId", 0, 0, 100);
    auto bannerContentPackComingSoon = CreateSyncedBool(610, "BannerContentPackComingSoon", true);

    RegisterTweakable(bannerContentPackId);
    RegisterTweakable(bannerContentPackComingSoon);

    mainMenu->AddChild(bannerContentPackId);
    mainMenu->AddChild(bannerContentPackComingSoon);

    RegisterTweakable(mainMenu);
    m_rootFolders.push_back(mainMenu);
}

void DevMenu::InitializeFlash() {
    auto flash = std::make_shared<TweakableFolder>(611, "Flash");

    // Add all Flash tweakables
    auto errorsEnabled = CreateSyncedBool(612, "ErrorsEnabled", true);

    RegisterTweakable(errorsEnabled);

    flash->AddChild(errorsEnabled);

    RegisterTweakable(flash);
    m_rootFolders.push_back(flash);
}

void DevMenu::InitializeGarbageCollector() {
    auto garbageCollector = std::make_shared<TweakableFolder>(614, "GarbageCollector");

    // Add all GarbageCollector tweakables
    auto printObjectCtorDtor = CreateSyncedBool(615, "PrintObjectCtorDtor", true);
    auto sweepTreshold = CreateSyncedInt(616, "SweepTreshold", 60, 0, 1000);
    auto objCountBegin = CreateSyncedInt(617, "ObjCountBegin", 10000, 0, 100000);
    auto objCountEnd = CreateSyncedInt(618, "ObjCountEnd", 20000, 0, 100000);
    auto sanityCheckInterval = CreateSyncedInt(619, "SanityCheckInterval", 15, 0, 1000);
    auto markMaxMicroseconds = CreateSyncedInt(623, "MarkMaxMicroseconds", 1000000, 0, 10000000);
    auto checkAfterAlive = CreateSyncedBool(624, "CheckAfterAlive", false);
    auto printMarkInfo = CreateSyncedBool(625, "PrintMarkInfo", true);

    RegisterTweakable(printObjectCtorDtor);
    RegisterTweakable(sweepTreshold);
    RegisterTweakable(objCountBegin);
    RegisterTweakable(objCountEnd);
    RegisterTweakable(sanityCheckInterval);
    RegisterTweakable(markMaxMicroseconds);
    RegisterTweakable(checkAfterAlive);
    RegisterTweakable(printMarkInfo);

    garbageCollector->AddChild(printObjectCtorDtor);
    garbageCollector->AddChild(sweepTreshold);
    garbageCollector->AddChild(objCountBegin);
    garbageCollector->AddChild(objCountEnd);
    garbageCollector->AddChild(sanityCheckInterval);
    garbageCollector->AddChild(markMaxMicroseconds);
    garbageCollector->AddChild(checkAfterAlive);
    garbageCollector->AddChild(printMarkInfo);

    RegisterTweakable(garbageCollector);
    m_rootFolders.push_back(garbageCollector);
}

void DevMenu::InitializeSettings() {
    auto settings = std::make_shared<TweakableFolder>(629, "Settings");

    // Add all Settings tweakables
    auto mouseActivationThreshold = CreateSyncedInt(630, "MouseActivationThreshold", 3, 0, 100);

    RegisterTweakable(mouseActivationThreshold);

    settings->AddChild(mouseActivationThreshold);

    RegisterTweakable(settings);
    m_rootFolders.push_back(settings);
}

void DevMenu::InitializeDebugLocalization() {
    auto debugLocalization = std::make_shared<TweakableFolder>(734, "debug");

    // Add all debug tweakables
    auto localization = CreateSyncedBool(735, "localization", false);

    RegisterTweakable(localization);

    debugLocalization->AddChild(localization);

    RegisterTweakable(debugLocalization);
    m_rootFolders.push_back(debugLocalization);
}

// ============================================================================
// Tweakables Dump Functionality (from devTweaks)
// ============================================================================

// Function addresses from Ghidra
typedef void* (__fastcall* InitializeDevMenuDataFunc)(int param_1);
typedef void(__cdecl* BuildTweakablesListFunc)(void* this_ptr, void* outputArray, int categoryId);

const uintptr_t INIT_DEV_MENU_DATA_ADDR = 0x00cef440;
const uintptr_t BUILD_TWEAKABLES_LIST_ADDR = 0x00d623e0;
const uintptr_t GLOBAL_DEV_MENU_DATA = 0x01755230;

// Helper functions to safely read values (SEH-enabled, no C++ objects)
static bool SafeReadName(char* namePtr, char* buffer, int bufferSize) {
    __try {
        if (namePtr != nullptr && namePtr[0] >= 0x20 && namePtr[0] < 0x7F) {
            int i = 0;
            while (i < bufferSize - 1 && namePtr[i] != '\0' && namePtr[i] >= 0x20 && namePtr[i] < 0x7F) {
                buffer[i] = namePtr[i];
                i++;
            }
            buffer[i] = '\0';
            return true;
        }
        return false;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SafeReadBoolValue(void** valuePtr, int* outValue) {
    __try {
        if (*valuePtr != nullptr) {
            int* intValue = (int*)*valuePtr;
            *outValue = *intValue;
            return true;
        }
        return false;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SafeReadIntValue(void** valuePtr, int* outValue) {
    __try {
        if (*valuePtr != nullptr) {
            int* intValue = (int*)*valuePtr;
            *outValue = *intValue;
            return true;
        }
        return false;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SafeReadFloatValue(void** valuePtr, float* outValue) {
    __try {
        if (*valuePtr != nullptr) {
            float* floatValue = (float*)*valuePtr;
            *outValue = *floatValue;
            return true;
        }
        return false;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Recursive helper function to dump a category and all its children
static void DumpCategoryRecursive(void* devMenuData, uintptr_t buildTweakablesListAddr, int categoryId, int depth) {
    int outputArray[3] = { 0, 2, 0 };
    void* arrayData = malloc(8);
    outputArray[2] = (int)arrayData;

    __asm {
        mov ecx, devMenuData
        lea eax, outputArray
        push categoryId
        push eax
        call buildTweakablesListAddr
    }

    LOG_VERBOSE("Category " << categoryId << " (depth " << depth << "): Found " << outputArray[0] << " items");

    void** tweakablePointers = (void**)outputArray[2];

    for (int i = 0; i < outputArray[0]; i++) {
        void* tweakablePtr = tweakablePointers[i];

        if (tweakablePtr != nullptr) {
            unsigned int* data = (unsigned int*)tweakablePtr;
            int type = data[1];  // +0x04
            int id = data[2];     // +0x08
            char* namePtr = *(char**)(data + 0x14 / 4);

            const char* typeStr = "Unknown";
            if (type == 0) typeStr = "Folder";
            else if (type == 1) typeStr = "Bool";
            else if (type == 2) typeStr = "Int";
            else if (type == 3) typeStr = "Float";

            // Build indentation string
            std::string indent(depth * 2, ' ');

            // Try to read the name safely
            char nameBuffer[256];
            if (SafeReadName(namePtr, nameBuffer, sizeof(nameBuffer))) {
                std::string output = indent + nameBuffer + " (" + typeStr + ", ID=" + std::to_string(id) + ")";

                // Show value for non-folders
                if (type >= 1 && type <= 3) {
                    void** valuePtr = (void**)(data + 0x1bc / 4);

                    if (type == 1) {  // Bool
                        int value;
                        if (SafeReadBoolValue(valuePtr, &value)) {
                            output += " = " + std::string(value ? "true" : "false");
                        }
                        else {
                            output += " = (read failed)";
                        }
                    }
                    else if (type == 2) {  // Int
                        int value;
                        if (SafeReadIntValue(valuePtr, &value)) {
                            output += " = " + std::to_string(value);
                        }
                        else {
                            output += " = (read failed)";
                        }
                    }
                    else if (type == 3) {  // Float
                        float value;
                        if (SafeReadFloatValue(valuePtr, &value)) {
                            char floatBuf[32];
                            snprintf(floatBuf, sizeof(floatBuf), " = %.3f", value);
                            output += floatBuf;
                        }
                        else {
                            output += " = (read failed)";
                        }
                    }
                }

                LOG_INFO(output);

                // Recursively dump if it's a folder
                if (type == 0) {
                    DumpCategoryRecursive(devMenuData, buildTweakablesListAddr, id, depth + 1);
                }
            }
            else {
                std::string failMsg = indent + "(name read failed, ID=" + std::to_string(id) + ")";
                LOG_INFO(failMsg);
            }
        }
    }

    free(arrayData);
}

void DevMenu::DumpTweakablesData() {
    LOG_INFO("===== DUMPING ALL TWEAKABLES (RECURSIVE) =====");

    uintptr_t baseAddress = (uintptr_t)GetModuleHandle(NULL);

    // Get pointer to global dev menu data
    void** globalDevMenuDataPtr = (void**)(baseAddress + GLOBAL_DEV_MENU_DATA - 0x700000);
    void* devMenuData = *globalDevMenuDataPtr;

    LOG_VERBOSE("Base address: 0x" << std::hex << baseAddress);
    LOG_VERBOSE("Global dev menu data ptr: 0x" << std::hex << (uintptr_t)globalDevMenuDataPtr);

    if (devMenuData == nullptr) {
        LOG_INFO("Initializing dev menu data...");
        InitializeDevMenuDataFunc initDevMenuData = (InitializeDevMenuDataFunc)(baseAddress + INIT_DEV_MENU_DATA_ADDR - 0x700000);
        devMenuData = initDevMenuData(0);
        *globalDevMenuDataPtr = devMenuData;
    }

    if (devMenuData == nullptr) {
        LOG_ERROR("Failed to initialize dev menu data!");
        return;
    }

    LOG_INFO("Dev menu data @ 0x" << std::hex << (uintptr_t)devMenuData);

    uintptr_t buildTweakablesListAddr = baseAddress + BUILD_TWEAKABLES_LIST_ADDR - 0x700000;
    LOG_VERBOSE("BuildTweakablesList @ 0x" << std::hex << buildTweakablesListAddr);
    LOG_INFO("");

    // Start recursive dump from category 0 (top level)
    DumpCategoryRecursive(devMenuData, buildTweakablesListAddr, 0, 0);

    LOG_INFO("");
    LOG_INFO("===== END DUMP =====");
}

// ============================================================================
// MOD Category - Custom Modded Values (not synced to game tweakables)
// ============================================================================

void DevMenu::InitializeMod() {
    auto mod = std::make_shared<TweakableFolder>(10000, "Mod");

    // ============================================================================
    // Respawn Controls
    // ============================================================================
    
    // Respawn at Current Checkpoint
    auto respawnCurrent = std::make_shared<TweakableButton>(
        10002,
        "Respawn at Current Checkpoint"
    );
    respawnCurrent->SetOnClickCallback([]() {
        Respawn::RespawnAtCheckpoint();
    });
    RegisterTweakable(respawnCurrent);
    mod->AddChild(respawnCurrent);

    // Respawn at Next Checkpoint
    auto respawnNext = std::make_shared<TweakableButton>(
        10003,
        "Respawn at Next Checkpoint"
    );
    respawnNext->SetOnClickCallback([]() {
        Respawn::RespawnAtNextCheckpoint();
    });
    RegisterTweakable(respawnNext);
    mod->AddChild(respawnNext);

    // Respawn at Previous Checkpoint
    auto respawnPrev = std::make_shared<TweakableButton>(
        10004,
        "Respawn at Previous Checkpoint"
    );
    respawnPrev->SetOnClickCallback([]() {
        Respawn::RespawnAtPreviousCheckpoint();
    });
    RegisterTweakable(respawnPrev);
    mod->AddChild(respawnPrev);

    // Respawn at Checkpoint Index - Slider (no auto-respawn)
    auto checkpointIndexSlider = std::make_shared<TweakableInt>(
        10005,
        "Select Checkpoint Index",
        0,      // Default: 0
        0,      // Min: 0
        100     // Max: 100 (will be updated dynamically)
    );
    // Update the max value based on current track's checkpoint count
    int currentCheckpointCount = Respawn::GetCheckpointCount();
    if (currentCheckpointCount > 0) {
        checkpointIndexSlider->SetRange(0, currentCheckpointCount - 1);
        std::string label = "Select Checkpoint (0-" + std::to_string(currentCheckpointCount - 1) + ")";
        checkpointIndexSlider->SetName(label);
    }
    // No callback - we'll use a button to apply the respawn
    RegisterTweakable(checkpointIndexSlider);
    mod->AddChild(checkpointIndexSlider);

    // Go to Selected Checkpoint Button
    auto goToCheckpointButton = std::make_shared<TweakableButton>(
        10014,
        "Go to Selected Checkpoint"
    );
    goToCheckpointButton->SetOnClickCallback([checkpointIndexSlider]() {
        int selectedIndex = checkpointIndexSlider->GetValue();
        int checkpointCount = Respawn::GetCheckpointCount();
        
        // Update slider range in case track changed
        if (checkpointCount > 0) {
            checkpointIndexSlider->SetRange(0, checkpointCount - 1);
            std::string label = "Select Checkpoint (0-" + std::to_string(checkpointCount - 1) + ")";
            checkpointIndexSlider->SetName(label);
            
            // Clamp selected index to valid range
            if (selectedIndex >= checkpointCount) {
                selectedIndex = checkpointCount - 1;
                checkpointIndexSlider->SetValue(selectedIndex);
            }
        }
        
        // Check if user selected the last checkpoint (finish line)
        if (selectedIndex == checkpointCount - 1 && checkpointCount > 0) {
            LOG_INFO("[DevMenu] Last checkpoint selected - triggering race finish instead of respawn");
            LOG_INFO("[DevMenu] (Respawning at finish line causes softlock)");
            
            // Trigger proper race finish using ActionScript
            if (ActionScript::CallHandleRaceFinish()) {
                LOG_VERBOSE("[DevMenu] Successfully called HandleRaceFinish!");
            } else {
                LOG_ERROR("[DevMenu] Failed to call HandleRaceFinish!");
            }
        } else {
            LOG_INFO("[DevMenu] Going to checkpoint " << selectedIndex << " (Total checkpoints: " << checkpointCount << ")");
            Respawn::RespawnAtCheckpointIndex(selectedIndex);
        }
    });
    RegisterTweakable(goToCheckpointButton);
    mod->AddChild(goToCheckpointButton);

    // Toggle Selected Checkpoint Enabled/Disabled Button
    auto toggleCheckpointButton = std::make_shared<TweakableButton>(
        10015,
        "Toggle Selected Checkpoint (Disable/Enable)"
    );
    toggleCheckpointButton->SetOnClickCallback([checkpointIndexSlider, toggleCheckpointButton]() {
        int selectedIndex = checkpointIndexSlider->GetValue();
        int checkpointCount = Respawn::GetCheckpointCount();
        
        // Update slider range in case track changed
        if (checkpointCount > 0) {
            checkpointIndexSlider->SetRange(0, checkpointCount - 1);
            std::string label = "Select Checkpoint (0-" + std::to_string(checkpointCount - 1) + ")";
            checkpointIndexSlider->SetName(label);
            
            // Clamp selected index to valid range
            if (selectedIndex >= checkpointCount) {
                selectedIndex = checkpointCount - 1;
                checkpointIndexSlider->SetValue(selectedIndex);
            }
        }
        
        if (checkpointCount <= 0) {
            LOG_ERROR("[DevMenu] No checkpoints available on this track");
            return;
        }
        
        // Toggle the checkpoint's enabled state
        bool wasEnabled = Respawn::IsCheckpointEnabled(selectedIndex);
        if (Respawn::ToggleCheckpoint(selectedIndex)) {
            bool isNowEnabled = Respawn::IsCheckpointEnabled(selectedIndex);
            if (isNowEnabled) {
                LOG_INFO("[DevMenu] Checkpoint " << selectedIndex << " ENABLED - will trigger when crossed");
            } else {
                LOG_INFO("[DevMenu] Checkpoint " << selectedIndex << " DISABLED - won't trigger when crossed");
            }
            
            // Update button label to show current state of selected checkpoint
            std::string stateStr = isNowEnabled ? "ENABLED" : "DISABLED";
            toggleCheckpointButton->SetName("Toggle Checkpoint " + std::to_string(selectedIndex) + " (" + stateStr + ")");
        } else {
            LOG_ERROR("[DevMenu] Failed to toggle checkpoint " << selectedIndex);
        }
    });
    RegisterTweakable(toggleCheckpointButton);
    mod->AddChild(toggleCheckpointButton);

    // Debug Checkpoint Structure Button
    auto debugCheckpointButton = std::make_shared<TweakableButton>(
        10016,
        "Debug Selected Checkpoint Structure"
    );
    debugCheckpointButton->SetOnClickCallback([checkpointIndexSlider]() {
        int selectedIndex = checkpointIndexSlider->GetValue();
        Respawn::DebugCheckpointStructure(selectedIndex);
    });
    RegisterTweakable(debugCheckpointButton);
    mod->AddChild(debugCheckpointButton);

    // ============================================================================
    // Limit Controls
    // ============================================================================

    // Toggle Fault Limit
    auto toggleFaultLimit = std::make_shared<TweakableButton>(
        10006,
        "Toggle Fault Limit [Click to check status]"
    );
    toggleFaultLimit->SetOnClickCallback([toggleFaultLimit]() {
        bool isDisabled = Respawn::IsFaultValidationDisabled();
        if (isDisabled) {
            LOG_INFO("[DevMenu] Fault limit is currently DISABLED. Re-enabling now...");
            Respawn::EnableFaultValidation();
            int currentFaults = Respawn::GetFaultCount();
            uint32_t faultLimit = Respawn::GetFaultLimit();
            LOG_INFO("[DevMenu] Fault limit ENABLED! (" << faultLimit << " faults, currently at " << currentFaults << ")");
        } else {
            LOG_INFO("[DevMenu] Fault limit is currently ENABLED. Disabling now...");
            Respawn::DisableFaultLimit();
            Respawn::DisableFaultValidation();
            LOG_INFO("[DevMenu] Fault limit DISABLED!");
        }
        
        // Update button label to show current state
        isDisabled = Respawn::IsFaultValidationDisabled();
        std::string newLabel = isDisabled 
            ? "Fault Limit: DISABLED (Infinite faults)"
            : "Fault Limit: ENABLED (500 faults)";
        toggleFaultLimit->SetName(newLabel);
    });
    RegisterTweakable(toggleFaultLimit);
    mod->AddChild(toggleFaultLimit);

    // Toggle Time Limit
    auto toggleTimeLimit = std::make_shared<TweakableButton>(
        10007,
        "Toggle Time Limit [Click to check status]"
    );
    toggleTimeLimit->SetOnClickCallback([toggleTimeLimit]() {
        bool isDisabled = Respawn::IsTimeValidationDisabled();
        if (isDisabled) {
            LOG_INFO("[DevMenu] Time limit is currently DISABLED. Re-enabling now...");
            Respawn::EnableTimeValidation();
            int currentTimeMs = Respawn::GetRaceTimeMs();
            uint32_t timeLimit = Respawn::GetTimeLimit();
            int timeLimitMs = (int)(timeLimit * 1000 / 60);  // Convert ticks to ms
            LOG_INFO("[DevMenu] Time limit ENABLED! (" << timeLimitMs / 60 << " minutes, currently at " << currentTimeMs / 1000 << "s)");
        } else {
            LOG_INFO("[DevMenu] Time limit is currently ENABLED. Disabling now...");
            Respawn::DisableTimeLimit();
            Respawn::DisableTimeValidation();
            Respawn::DisableRaceUpdateTimerFreeze();  // Also disable timer freeze
            Respawn::DisableTimeCompletionCheck2();   // And the second time check
            LOG_INFO("[DevMenu] Time limit DISABLED!");
        }
        
        // Update button label to show current state
        isDisabled = Respawn::IsTimeValidationDisabled();
        std::string newLabel = isDisabled 
            ? "Time Limit: DISABLED (Infinite time)"
            : "Time Limit: ENABLED (30 minutes)";
        toggleTimeLimit->SetName(newLabel);
    });
    RegisterTweakable(toggleTimeLimit);
    mod->AddChild(toggleTimeLimit);

    // ============================================================================
    // Fault Controls
    // ============================================================================

    // Fault Once
    auto faultOnce = std::make_shared<TweakableBool>(
        10008,
        "Fault Once",
        false
    );
    faultOnce->SetOnChangeCallback([faultOnce](bool value) {
        if (value) {
            Respawn::IncrementFaultCounter();
            faultOnce->SetValue(false);
        }
    });
    RegisterTweakable(faultOnce);
    mod->AddChild(faultOnce);

    // Add/Remove Faults
    auto faultAdjust = std::make_shared<TweakableInt>(
        10009,
        "Add/Remove Faults",
        0,      // Default: 0
        -500,   // Min: -500
        500     // Max: +500
    );
    faultAdjust->SetOnChangeCallback([faultAdjust](int value) {
        if (value != 0) {
            Respawn::IncrementFaultCounterBy(value);
            faultAdjust->SetValue(0);
        }
    });
    RegisterTweakable(faultAdjust);
    mod->AddChild(faultAdjust);

    // Instant Fault Out (500 faults) - Trigger instant fault out finish
    auto instantFaultOut = std::make_shared<TweakableButton>(
        10010,
        "Instant Fault Out (500 faults)"
    );
    instantFaultOut->SetOnClickCallback([]() {
        Respawn::InstantFaultOut();
    });
    RegisterTweakable(instantFaultOut);
    mod->AddChild(instantFaultOut);

    // ============================================================================
    // Time Controls
    // ============================================================================

    // Add/Remove Time (seconds)
    auto timeAdjust = std::make_shared<TweakableInt>(
        10011,
        "Add/Remove Time (seconds)",
        0,      // Default: 0
        -1800,  // Min: -30 minutes
        1800    // Max: +30 minutes
    );
    timeAdjust->SetOnChangeCallback([timeAdjust](int value) {
        if (value != 0) {
            Respawn::AdjustRaceTimeMs(value * 1000); // Convert to milliseconds
            timeAdjust->SetValue(0);
        }
    });
    RegisterTweakable(timeAdjust);
    mod->AddChild(timeAdjust);

    // Instant Time Out (30 minutes) - Trigger instant timeout finish
    auto instantTimeOut = std::make_shared<TweakableButton>(
        10012,
        "Instant Time Out (30 minutes)"
    );
    instantTimeOut->SetOnClickCallback([]() {
        Respawn::InstantTimeOut();
    });
    RegisterTweakable(instantTimeOut);
    mod->AddChild(instantTimeOut);

    // Instant Finish (normal) - Trigger normal instant finish using HandleRaceFinish
    auto instantFinish = std::make_shared<TweakableButton>(
        10013,
        "Instant Finish (normal)"
    );
    instantFinish->SetOnClickCallback([]() {
        LOG_VERBOSE("[DevMenu] Instant Finish button pressed - calling HandleRaceFinish...");
        if (ActionScript::CallHandleRaceFinish()) {
            LOG_VERBOSE("[DevMenu] Successfully called HandleRaceFinish!");
        } else {
            LOG_ERROR("[DevMenu] Failed to call HandleRaceFinish!");
        }
    });
    RegisterTweakable(instantFinish);
    mod->AddChild(instantFinish);

    // ============================================================================
    // Prevent Finish Controls
    // ============================================================================

    // Toggle Prevent Finish (Disable/Enable Finish Line)
    // When enabled, crossing the finish line won't end the race
    auto togglePreventFinish = std::make_shared<TweakableButton>(
        10017,
        "Prevent Finish: OFF (Click to toggle)"
    );
    togglePreventFinish->SetOnClickCallback([togglePreventFinish]() {
        bool wasEnabled = Respawn::IsFinishLineEnabled();
        
        if (Respawn::ToggleFinishLine()) {
            bool isNowEnabled = Respawn::IsFinishLineEnabled();
            
            if (isNowEnabled) {
                // Finish line is enabled = race CAN end = prevent finish is OFF
                LOG_INFO("[DevMenu] Prevent Finish: OFF - Race will end when crossing finish line");
                togglePreventFinish->SetName("Prevent Finish: OFF (Race will end normally)");
            } else {
                // Finish line is disabled = race CAN'T end = prevent finish is ON
                LOG_INFO("[DevMenu] Prevent Finish: ON - Race will NOT end when crossing finish line");
                togglePreventFinish->SetName("Prevent Finish: ON (Finish line disabled)");
            }
        } else {
            LOG_ERROR("[DevMenu] Failed to toggle finish line state");
        }
    });
    RegisterTweakable(togglePreventFinish);
    mod->AddChild(togglePreventFinish);

    RegisterTweakable(mod);
    m_rootFolders.push_back(mod);
}

// Keybindings Tab - Menu bar accessible keybinding configuration
// ============================================================================

// Thread data structure for keybinding capture
struct KeybindThreadData {
    TweakableButton* button;
    Keybindings::Action action;
    bool* waitingFlag;
};

// Helper function to create a keybinding button
static std::shared_ptr<TweakableButton> CreateKeybindButton(
    int id,
    Keybindings::Action action,
    bool* waitingFlag,
    DevMenu* devMenu)
{
    auto button = std::make_shared<TweakableButton>(
        id,
        "Bind " + Keybindings::GetActionName(action) + ": " + 
        Keybindings::GetKeyName(Keybindings::GetKey(action))
    );
    
    button->SetOnClickCallback([button, action, waitingFlag]() {
        *waitingFlag = true;
        button->SetName("Press any key...");
        LOG_VERBOSE("[DevMenu] Waiting for key press to bind " << Keybindings::GetActionName(action) << "...");
        
        // Create thread data
        KeybindThreadData* data = new KeybindThreadData;
        data->button = button.get();
        data->action = action;
        data->waitingFlag = waitingFlag;
        
        // Start a thread to capture the key press
        CreateThread(NULL, 0, [](LPVOID param) -> DWORD {
            KeybindThreadData* data = (KeybindThreadData*)param;
            
            // Wait for any key press
            while (*data->waitingFlag) {
                for (int vk = 0x08; vk <= 0xFE; vk++) {
                    // Skip mouse buttons
                    if (vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON ||
                        vk == VK_XBUTTON1 || vk == VK_XBUTTON2) {
                        continue;
                    }
                    
                    if (GetAsyncKeyState(vk) & 0x8000) {
                        // Wait for key release
                        while (GetAsyncKeyState(vk) & 0x8000) {
                            Sleep(10);
                        }
                        
                        // Set the new key
                        Keybindings::SetKey(data->action, vk);
                        std::string keyName = Keybindings::GetKeyName(vk);
                        data->button->SetName("Bind " + Keybindings::GetActionName(data->action) + ": " + keyName);
                        LOG_VERBOSE("[DevMenu] " << Keybindings::GetActionName(data->action) << " bound to: " << keyName);
                        *data->waitingFlag = false;
                        delete data;
                        return 0;
                    }
                }
                Sleep(50);
            }
            delete data;
            return 0;
        }, data, 0, NULL);
    });
    
    return button;
}

void DevMenu::InitializeKeybindings() {
    // Static flags for each keybinding (to track if waiting for key press)
    static bool waitingForInstantFinish = false;
    static bool waitingForToggleDevMenu = false;
    static bool waitingForToggleKeybindingsMenu = false;
    static bool waitingForClearConsole = false;
    static bool waitingForToggleVerbose = false;
    static bool waitingForShowHelp = false;
    static bool waitingForDumpTweakables = false;
    static bool waitingForCycleHUD = false;
    static bool waitingForRespawnAtCheckpoint = false;
    static bool waitingForRespawnPrevCheckpoint = false;
    static bool waitingForRespawnNextCheckpoint = false;
    static bool waitingForRespawnForward5 = false;
    static bool waitingForIncrementFault = false;
    static bool waitingForDebugFaultCounter = false;
    static bool waitingForAdd100Faults = false;
    static bool waitingForSubtract100Faults = false;
    static bool waitingForResetFaults = false;
    static bool waitingForDebugTimeCounter = false;
    static bool waitingForAdd10Seconds = false;
    static bool waitingForSubtract10Seconds = false;
    static bool waitingForAdd1Minute = false;
    static bool waitingForResetTime = false;
    static bool waitingForRestoreDefaultLimits = false;
    static bool waitingForDebugLimits = false;
    static bool waitingForToggleLimitValidation = false;
    static bool waitingForTogglePause = false;
    static bool waitingForScanLeaderboardByID = false;
    static bool waitingForScanCurrentLeaderboard = false;
    static bool waitingForStartAutoScroll = false;
    static bool waitingForKillswitch = false;
    static bool waitingForCycleSearch = false;
    static bool waitingForDecreaseScrollDelay = false;
    static bool waitingForIncreaseScrollDelay = false;
    static bool waitingForTestFetchTrackID = false;
    static bool waitingForTogglePatch = false;
    static bool waitingForSaveMultiplayerLogs = false;
    static bool waitingForCaptureSessionState = false;
    static bool waitingForTogglePhysicsLogging = false;
    static bool waitingForDumpPhysicsLog = false;
    static bool waitingForModifyXPosition = false;
    static bool waitingForFullCountdownSequence = false;
    static bool waitingForShowSingleCountdown = false;
    static bool waitingForToggleLoadScreen = false;
    
    // Clear the action and default vectors in case of re-initialization
    m_keybindingActions.clear();
    m_keybindingDefaults.clear();
    
    // === General Controls ===
    
    // Toggle Dev Menu
    auto toggleDevMenuBtn = CreateKeybindButton(10101, Keybindings::Action::ToggleDevMenu, &waitingForToggleDevMenu, this);
    RegisterTweakable(toggleDevMenuBtn);
    m_keybindingItems.push_back(toggleDevMenuBtn);
    m_keybindingActions.push_back(Keybindings::Action::ToggleDevMenu);
    m_keybindingDefaults.push_back(VK_HOME);
    
    // Toggle Keybindings Menu
    auto toggleKeybindingsMenuBtn = CreateKeybindButton(10100, Keybindings::Action::ToggleKeybindingsMenu, &waitingForToggleKeybindingsMenu, this);
    RegisterTweakable(toggleKeybindingsMenuBtn);
    m_keybindingItems.push_back(toggleKeybindingsMenuBtn);
    m_keybindingActions.push_back(Keybindings::Action::ToggleKeybindingsMenu);
    m_keybindingDefaults.push_back('K');
    
    // Clear Console
    auto clearConsoleBtn = CreateKeybindButton(10102, Keybindings::Action::ClearConsole, &waitingForClearConsole, this);
    RegisterTweakable(clearConsoleBtn);
    m_keybindingItems.push_back(clearConsoleBtn);
    m_keybindingActions.push_back(Keybindings::Action::ClearConsole);
    m_keybindingDefaults.push_back('C');
    
    // Toggle Verbose Logging
    auto toggleVerboseBtn = CreateKeybindButton(10103, Keybindings::Action::ToggleVerboseLogging, &waitingForToggleVerbose, this);
    RegisterTweakable(toggleVerboseBtn);
    m_keybindingItems.push_back(toggleVerboseBtn);
    m_keybindingActions.push_back(Keybindings::Action::ToggleVerboseLogging);
    m_keybindingDefaults.push_back(VK_OEM_PLUS);
    
    // Show Help Text
    auto showHelpBtn = CreateKeybindButton(10104, Keybindings::Action::ShowHelpText, &waitingForShowHelp, this);
    RegisterTweakable(showHelpBtn);
    m_keybindingItems.push_back(showHelpBtn);
    m_keybindingActions.push_back(Keybindings::Action::ShowHelpText);
    m_keybindingDefaults.push_back(VK_OEM_MINUS);
    
    // Dump Tweakables
    auto dumpTweakablesBtn = CreateKeybindButton(10105, Keybindings::Action::DumpTweakables, &waitingForDumpTweakables, this);
    RegisterTweakable(dumpTweakablesBtn);
    m_keybindingItems.push_back(dumpTweakablesBtn);
    m_keybindingActions.push_back(Keybindings::Action::DumpTweakables);
    m_keybindingDefaults.push_back('D');
    
    // Instant Finish
    auto instantFinishBtn = CreateKeybindButton(10106, Keybindings::Action::InstantFinish, &waitingForInstantFinish, this);
    RegisterTweakable(instantFinishBtn);
    m_keybindingItems.push_back(instantFinishBtn);
    m_keybindingActions.push_back(Keybindings::Action::InstantFinish);
    m_keybindingDefaults.push_back('`');
    
    // === Camera Controls ===
    
    // Cycle HUD Visibility
    auto CycleHUDBtn = CreateKeybindButton(10107, Keybindings::Action::CycleHUD, &waitingForCycleHUD, this);
    RegisterTweakable(CycleHUDBtn);
    m_keybindingItems.push_back(CycleHUDBtn);
    m_keybindingActions.push_back(Keybindings::Action::CycleHUD);
    m_keybindingDefaults.push_back('V');
    
    // === Respawn Controls ===
    
    // Respawn At Checkpoint
    auto respawnAtCheckpointBtn = CreateKeybindButton(10108, Keybindings::Action::RespawnAtCheckpoint, &waitingForRespawnAtCheckpoint, this);
    RegisterTweakable(respawnAtCheckpointBtn);
    m_keybindingItems.push_back(respawnAtCheckpointBtn);
    m_keybindingActions.push_back(Keybindings::Action::RespawnAtCheckpoint);
    m_keybindingDefaults.push_back('W');
    
    // Respawn Prev Checkpoint
    auto respawnPrevCheckpointBtn = CreateKeybindButton(10109, Keybindings::Action::RespawnPrevCheckpoint, &waitingForRespawnPrevCheckpoint, this);
    RegisterTweakable(respawnPrevCheckpointBtn);
    m_keybindingItems.push_back(respawnPrevCheckpointBtn);
    m_keybindingActions.push_back(Keybindings::Action::RespawnPrevCheckpoint);
    m_keybindingDefaults.push_back('Q');
    
    // Respawn Next Checkpoint
    auto respawnNextCheckpointBtn = CreateKeybindButton(10110, Keybindings::Action::RespawnNextCheckpoint, &waitingForRespawnNextCheckpoint, this);
    RegisterTweakable(respawnNextCheckpointBtn);
    m_keybindingItems.push_back(respawnNextCheckpointBtn);
    m_keybindingActions.push_back(Keybindings::Action::RespawnNextCheckpoint);
    m_keybindingDefaults.push_back('E');
    
    // Respawn Forward 5
    auto respawnForward5Btn = CreateKeybindButton(10111, Keybindings::Action::RespawnForward5, &waitingForRespawnForward5, this);
    RegisterTweakable(respawnForward5Btn);
    m_keybindingItems.push_back(respawnForward5Btn);
    m_keybindingActions.push_back(Keybindings::Action::RespawnForward5);
    m_keybindingDefaults.push_back('5');
    
    // === Fault Controls ===
    // Increment Fault
    auto incrementFaultBtn = CreateKeybindButton(10112, Keybindings::Action::IncrementFault, &waitingForIncrementFault, this);
    RegisterTweakable(incrementFaultBtn);
    m_keybindingItems.push_back(incrementFaultBtn);
    m_keybindingActions.push_back(Keybindings::Action::IncrementFault);
    m_keybindingDefaults.push_back('F');
    
    // Add 100 Faults
    auto add100FaultsBtn = CreateKeybindButton(10114, Keybindings::Action::Add100Faults, &waitingForAdd100Faults, this);
    RegisterTweakable(add100FaultsBtn);
    m_keybindingItems.push_back(add100FaultsBtn);
    m_keybindingActions.push_back(Keybindings::Action::Add100Faults);
    m_keybindingDefaults.push_back('J');
    
    // Subtract 100 Faults
    auto subtract100FaultsBtn = CreateKeybindButton(10115, Keybindings::Action::Subtract100Faults, &waitingForSubtract100Faults, this);
    RegisterTweakable(subtract100FaultsBtn);
    m_keybindingItems.push_back(subtract100FaultsBtn);
    m_keybindingActions.push_back(Keybindings::Action::Subtract100Faults);
    m_keybindingDefaults.push_back('H');
    
    // Reset Faults
    auto resetFaultsBtn = CreateKeybindButton(10116, Keybindings::Action::ResetFaults, &waitingForResetFaults, this);
    RegisterTweakable(resetFaultsBtn);
    m_keybindingItems.push_back(resetFaultsBtn);
    m_keybindingActions.push_back(Keybindings::Action::ResetFaults);
    m_keybindingDefaults.push_back('1');

    // Reset Time
    auto resetTimeBtn = CreateKeybindButton(10121, Keybindings::Action::ResetTime, &waitingForResetTime, this);
    RegisterTweakable(resetTimeBtn);
    m_keybindingItems.push_back(resetTimeBtn);
    m_keybindingActions.push_back(Keybindings::Action::ResetTime);
    m_keybindingDefaults.push_back('2');
    
    // === Time Controls ===
    
    // Debug Time Counter
    auto debugTimeCounterBtn = CreateKeybindButton(10117, Keybindings::Action::DebugTimeCounter, &waitingForDebugTimeCounter, this);
    RegisterTweakable(debugTimeCounterBtn);
    m_keybindingItems.push_back(debugTimeCounterBtn);
    m_keybindingActions.push_back(Keybindings::Action::DebugTimeCounter);
    m_keybindingDefaults.push_back(VK_OEM_4);

    // Debug Fault Counter
    auto debugFaultCounterBtn = CreateKeybindButton(10113, Keybindings::Action::DebugFaultCounter, &waitingForDebugFaultCounter, this);
    RegisterTweakable(debugFaultCounterBtn);
    m_keybindingItems.push_back(debugFaultCounterBtn);
    m_keybindingActions.push_back(Keybindings::Action::DebugFaultCounter);
    m_keybindingDefaults.push_back(VK_OEM_6);
    
    // Add 10 Seconds
    auto add10SecondsBtn = CreateKeybindButton(10118, Keybindings::Action::Add60Seconds, &waitingForAdd10Seconds, this);
    RegisterTweakable(add10SecondsBtn);
    m_keybindingItems.push_back(add10SecondsBtn);
    m_keybindingActions.push_back(Keybindings::Action::Add60Seconds);
    m_keybindingDefaults.push_back('U');
    
    // Subtract 10 Seconds
    auto subtract10SecondsBtn = CreateKeybindButton(10119, Keybindings::Action::Subtract60Seconds, &waitingForSubtract10Seconds, this);
    RegisterTweakable(subtract10SecondsBtn);
    m_keybindingItems.push_back(subtract10SecondsBtn);
    m_keybindingActions.push_back(Keybindings::Action::Subtract60Seconds);
    m_keybindingDefaults.push_back('I');
    
    // Add 1 Minute
    auto add1MinuteBtn = CreateKeybindButton(10120, Keybindings::Action::Add10Minute, &waitingForAdd1Minute, this);
    RegisterTweakable(add1MinuteBtn);
    m_keybindingItems.push_back(add1MinuteBtn);
    m_keybindingActions.push_back(Keybindings::Action::Add10Minute);
    m_keybindingDefaults.push_back('Y');
    
    // === Limit Controls ===
    // Toggle Limit Validation
    auto toggleLimitValidationBtn = CreateKeybindButton(10125, Keybindings::Action::ToggleLimitValidation, &waitingForToggleLimitValidation, this);
    RegisterTweakable(toggleLimitValidationBtn);
    m_keybindingItems.push_back(toggleLimitValidationBtn);
    m_keybindingActions.push_back(Keybindings::Action::ToggleLimitValidation);
    m_keybindingDefaults.push_back(VK_F4);
    
    // === Pause Controls ===
    
    // Toggle Pause
    auto togglePauseBtn = CreateKeybindButton(10126, Keybindings::Action::TogglePause, &waitingForTogglePause, this);
    RegisterTweakable(togglePauseBtn);
    m_keybindingItems.push_back(togglePauseBtn);
    m_keybindingActions.push_back(Keybindings::Action::TogglePause);
    m_keybindingDefaults.push_back('P');
    
    // === Leaderboard Scanner Controls ===
    
    // Scan Leaderboard By ID
    auto scanLeaderboardByIDBtn = CreateKeybindButton(10127, Keybindings::Action::ScanLeaderboardByID, &waitingForScanLeaderboardByID, this);
    RegisterTweakable(scanLeaderboardByIDBtn);
    m_keybindingItems.push_back(scanLeaderboardByIDBtn);
    m_keybindingActions.push_back(Keybindings::Action::ScanLeaderboardByID);
    m_keybindingDefaults.push_back(VK_F2);
    
    // Scan Current Leaderboard
    auto scanCurrentLeaderboardBtn = CreateKeybindButton(10128, Keybindings::Action::ScanCurrentLeaderboard, &waitingForScanCurrentLeaderboard, this);
    RegisterTweakable(scanCurrentLeaderboardBtn);
    m_keybindingItems.push_back(scanCurrentLeaderboardBtn);
    m_keybindingActions.push_back(Keybindings::Action::ScanCurrentLeaderboard);
    m_keybindingDefaults.push_back(VK_F3);
    
    // === Track Central Auto-Scroll Controls ===
    
    // Start Auto Scroll
    auto startAutoScrollBtn = CreateKeybindButton(10129, Keybindings::Action::StartAutoScroll, &waitingForStartAutoScroll, this);
    RegisterTweakable(startAutoScrollBtn);
    m_keybindingItems.push_back(startAutoScrollBtn);
    m_keybindingActions.push_back(Keybindings::Action::StartAutoScroll);
    m_keybindingDefaults.push_back(VK_F5);
    
    // Killswitch
    auto killswitchBtn = CreateKeybindButton(10130, Keybindings::Action::Killswitch, &waitingForKillswitch, this);
    RegisterTweakable(killswitchBtn);
    m_keybindingItems.push_back(killswitchBtn);
    m_keybindingActions.push_back(Keybindings::Action::Killswitch);
    m_keybindingDefaults.push_back(VK_F6);
    
    // Cycle Search
    auto cycleSearchBtn = CreateKeybindButton(10131, Keybindings::Action::CycleSearch, &waitingForCycleSearch, this);
    RegisterTweakable(cycleSearchBtn);
    m_keybindingItems.push_back(cycleSearchBtn);
    m_keybindingActions.push_back(Keybindings::Action::CycleSearch);
    m_keybindingDefaults.push_back(VK_F7);
    
    // Decrease Scroll Delay
    auto decreaseScrollDelayBtn = CreateKeybindButton(10132, Keybindings::Action::DecreaseScrollDelay, &waitingForDecreaseScrollDelay, this);
    RegisterTweakable(decreaseScrollDelayBtn);
    m_keybindingItems.push_back(decreaseScrollDelayBtn);
    m_keybindingActions.push_back(Keybindings::Action::DecreaseScrollDelay);
    m_keybindingDefaults.push_back(VK_INSERT);
    
    // Increase Scroll Delay
    auto increaseScrollDelayBtn = CreateKeybindButton(10133, Keybindings::Action::IncreaseScrollDelay, &waitingForIncreaseScrollDelay, this);
    RegisterTweakable(increaseScrollDelayBtn);
    m_keybindingItems.push_back(increaseScrollDelayBtn);
    m_keybindingActions.push_back(Keybindings::Action::IncreaseScrollDelay);
    m_keybindingDefaults.push_back(VK_DELETE);
    
    // === Leaderboard Direct Controls ===
    
    // Test Fetch Track ID
    auto testFetchTrackIDBtn = CreateKeybindButton(10134, Keybindings::Action::TestFetchTrackID, &waitingForTestFetchTrackID, this);
    RegisterTweakable(testFetchTrackIDBtn);
    m_keybindingItems.push_back(testFetchTrackIDBtn);
    m_keybindingActions.push_back(Keybindings::Action::TestFetchTrackID);
    m_keybindingDefaults.push_back(VK_F10);
    
    // === Multiplayer Monitoring Controls ===
    // Save Multiplayer Logs
    auto saveMultiplayerLogsBtn = CreateKeybindButton(10136, Keybindings::Action::SaveMultiplayerLogs, &waitingForSaveMultiplayerLogs, this);
    RegisterTweakable(saveMultiplayerLogsBtn);
    m_keybindingItems.push_back(saveMultiplayerLogsBtn);
    m_keybindingActions.push_back(Keybindings::Action::SaveMultiplayerLogs);
    m_keybindingDefaults.push_back('M');
    
    // Capture Session State
    auto captureSessionStateBtn = CreateKeybindButton(10137, Keybindings::Action::CaptureSessionState, &waitingForCaptureSessionState, this);
    RegisterTweakable(captureSessionStateBtn);
    m_keybindingItems.push_back(captureSessionStateBtn);
    m_keybindingActions.push_back(Keybindings::Action::CaptureSessionState);
    m_keybindingDefaults.push_back('N');
    
    // === ActionScript Controls ===
    // Full Countdown Sequence
    auto fullCountdownSequenceBtn = CreateKeybindButton(10141, Keybindings::Action::FullCountdownSequence, &waitingForFullCountdownSequence, this);
    RegisterTweakable(fullCountdownSequenceBtn);
    m_keybindingItems.push_back(fullCountdownSequenceBtn);
    m_keybindingActions.push_back(Keybindings::Action::FullCountdownSequence);
    m_keybindingDefaults.push_back('Z');
    
    // Show Single Countdown
    auto showSingleCountdownBtn = CreateKeybindButton(10142, Keybindings::Action::ShowSingleCountdown, &waitingForShowSingleCountdown, this);
    RegisterTweakable(showSingleCountdownBtn);
    m_keybindingItems.push_back(showSingleCountdownBtn);
    m_keybindingActions.push_back(Keybindings::Action::ShowSingleCountdown);
    m_keybindingDefaults.push_back(VK_SHIFT);
    
    // Toggle Load Screen
    auto ToggleLoadScreenBtn = CreateKeybindButton(10143, Keybindings::Action::ToggleLoadScreen, &waitingForToggleLoadScreen, this);
    RegisterTweakable(ToggleLoadScreenBtn);
    m_keybindingItems.push_back(ToggleLoadScreenBtn);
    m_keybindingActions.push_back(Keybindings::Action::ToggleLoadScreen);
    m_keybindingDefaults.push_back('L');
    
    // Save as Default button - explicitly saves current keybindings to config file
    auto saveKeybindings = std::make_shared<TweakableButton>(
        10198,
        "Save as Default"
    );
    saveKeybindings->SetOnClickCallback([]() {
        if (Keybindings::SaveToFile()) {
            LOG_INFO("[DevMenu] Keybindings saved to config file");
        } else {
            LOG_ERROR("[DevMenu] Failed to save keybindings to config file");
        }
    });
    RegisterTweakable(saveKeybindings);
    m_keybindingItems.push_back(saveKeybindings);
    
    // Reset all to defaults button
    auto resetKeybindings = std::make_shared<TweakableButton>(
        10199,
        "Reset All to Defaults"
    );
    resetKeybindings->SetOnClickCallback([this]() {
        // Reset each keybinding to its default using the stored mappings
        for (size_t i = 0; i < m_keybindingActions.size(); i++) {
            Keybindings::Action action = m_keybindingActions[i];
            int defaultKey = m_keybindingDefaults[i];
            
            // Reset the keybinding
            Keybindings::SetKey(action, defaultKey);
            
            // Update the button label
            std::string keyName = Keybindings::GetKeyName(defaultKey);
            m_keybindingItems[i]->SetName("Bind " + Keybindings::GetActionName(action) + ": " + keyName);
        }
        
        LOG_VERBOSE("[DevMenu] Reset all keybindings to defaults");
    });
    RegisterTweakable(resetKeybindings);
    m_keybindingItems.push_back(resetKeybindings);
}