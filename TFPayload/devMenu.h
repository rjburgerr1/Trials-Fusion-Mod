#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>
#include "keybindings.h"

// Forward declarations
class DevMenuNode;
class DevMenuFolder;

// Enum for tweakable types
enum class TweakableType {
    Float,
    Int,
    Bool,
    Button,
    Folder
};

// Base class for all tweakable items
class TweakableItem {
public:
    TweakableItem(int id, const std::string& name, TweakableType type)
        : m_id(id), m_name(name), m_type(type) {}
    
    virtual ~TweakableItem() = default;
    
    int GetId() const { return m_id; }
    const std::string& GetName() const { return m_name; }
    void SetName(const std::string& name) { m_name = name; }
    TweakableType GetType() const { return m_type; }
    
    virtual void Render() = 0;
    virtual void Reset() = 0;

protected:
    int m_id;
    std::string m_name;
    TweakableType m_type;
};

// Float tweakable
class TweakableFloat : public TweakableItem {
public:
    TweakableFloat(int id, const std::string& name, float defaultValue, 
                   float minValue = 0.0f, float maxValue = 100.0f)
        : TweakableItem(id, name, TweakableType::Float),
          m_value(defaultValue),
          m_defaultValue(defaultValue),
          m_minValue(minValue),
          m_maxValue(maxValue) {}
    
    void Render() override;
    void Reset() override { m_value = m_defaultValue; }
    
    float GetValue() const { return m_value; }
    void SetValue(float value) { m_value = value; }
    
    void SetRange(float min, float max) {
        m_minValue = min;
        m_maxValue = max;
    }
    
    void SetOnChangeCallback(std::function<void(float)> callback) {
        m_onChange = callback;
    }

private:
    float m_value;
    float m_defaultValue;
    float m_minValue;
    float m_maxValue;
    std::function<void(float)> m_onChange;
};

// Int tweakable
class TweakableInt : public TweakableItem {
public:
    TweakableInt(int id, const std::string& name, int defaultValue,
                 int minValue = 0, int maxValue = 1000)
        : TweakableItem(id, name, TweakableType::Int),
          m_value(defaultValue),
          m_defaultValue(defaultValue),
          m_minValue(minValue),
          m_maxValue(maxValue) {}
    
    void Render() override;
    void Reset() override { m_value = m_defaultValue; }
    
    int GetValue() const { return m_value; }
    void SetValue(int value) { m_value = value; }
    
    void SetRange(int min, int max) {
        m_minValue = min;
        m_maxValue = max;
    }
    
    void SetOnChangeCallback(std::function<void(int)> callback) {
        m_onChange = callback;
    }

private:
    int m_value;
    int m_defaultValue;
    int m_minValue;
    int m_maxValue;
    std::function<void(int)> m_onChange;
};

// Bool tweakable
class TweakableBool : public TweakableItem {
public:
    TweakableBool(int id, const std::string& name, bool defaultValue)
        : TweakableItem(id, name, TweakableType::Bool),
          m_value(defaultValue),
          m_defaultValue(defaultValue) {}
    
    void Render() override;
    void Reset() override { m_value = m_defaultValue; }
    
    bool GetValue() const { return m_value; }
    void SetValue(bool value) { m_value = value; }
    
    void SetOnChangeCallback(std::function<void(bool)> callback) {
        m_onChange = callback;
    }

private:
    bool m_value;
    bool m_defaultValue;
    std::function<void(bool)> m_onChange;
};

// Button tweakable (triggers callback when clicked)
class TweakableButton : public TweakableItem {
public:
    TweakableButton(int id, const std::string& name)
        : TweakableItem(id, name, TweakableType::Button) {}
    
    void Render() override;
    void Reset() override {} // Buttons don't need reset
    
    void SetOnClickCallback(std::function<void()> callback) {
        m_onClick = callback;
    }
    
    // Trigger the click callback directly (for custom button rendering)
    void TriggerClick() {
        if (m_onClick) {
            m_onClick();
        }
    }

private:
    std::function<void()> m_onClick;
};

// Folder tweakable (contains other tweakables)
class TweakableFolder : public TweakableItem {
public:
    TweakableFolder(int id, const std::string& name)
        : TweakableItem(id, name, TweakableType::Folder),
          m_isOpen(false) {}
    
    void Render() override;
    void Reset() override;
    
    void AddChild(std::shared_ptr<TweakableItem> child) {
        m_children.push_back(child);
    }
    
    const std::vector<std::shared_ptr<TweakableItem>>& GetChildren() const {
        return m_children;
    }
    
    bool IsOpen() const { return m_isOpen; }
    void SetOpen(bool open) { m_isOpen = open; }

private:
    std::vector<std::shared_ptr<TweakableItem>> m_children;
    bool m_isOpen;
};

// Main Dev Menu class
class DevMenu {
public:
    DevMenu();
    ~DevMenu();
    
    // Initialize the menu with all tweakables from the game
    void Initialize();
    
    // Render the menu (call this every frame when menu should be visible)
    void Render();
    
    // Toggle menu visibility
    void Toggle() { m_isVisible = !m_isVisible; }
    void Show() { m_isVisible = true; }
    void Hide() { m_isVisible = false; }
    bool IsVisible() const { return m_isVisible; }
    
    // Toggle keybindings window visibility
    void ToggleKeybindingsWindow() { m_showKeybindingsWindow = !m_showKeybindingsWindow; }
    void ShowKeybindingsWindow() { m_showKeybindingsWindow = true; }
    void HideKeybindingsWindow() { m_showKeybindingsWindow = false; }
    bool IsKeybindingsWindowVisible() const { return m_showKeybindingsWindow; }
    
    // Reset all values to defaults
    void ResetAll();
    
    // Search functionality
    void SetSearchFilter(const std::string& filter) { m_searchFilter = filter; }
    
    // Get tweakable by ID
    template<typename T>
    std::shared_ptr<T> GetTweakable(int id) {
        auto it = m_tweakableMap.find(id);
        if (it != m_tweakableMap.end()) {
            return std::dynamic_pointer_cast<T>(it->second);
        }
        return nullptr;
    }
    
    // Quick access to common tweakables
    std::shared_ptr<TweakableFloat> GetFloat(int id);
    std::shared_ptr<TweakableInt> GetInt(int id);
    std::shared_ptr<TweakableBool> GetBool(int id);
    std::shared_ptr<TweakableFolder> GetFolder(int id);
    
    // Save/Load configurations
    void SaveConfig(const std::string& filename);
    void LoadConfig(const std::string& filename);
    
    // Dump all tweakables from game memory (recursive)
    void DumpTweakablesData();

private:
    void InitializeBikeSound();
    void InitializeDynamicMusic();
    void InitializeProgressionSystem();
    void InitializeGarage();
    void InitializeContentPack();
    void InitializeDLC();
    void InitializeEditor();
    void InitializeMultiplayer();
    void InitializeEvent();
    void InitializeFMX();
    void InitializeBike();
    void InitializeRider();
    void InitializeVibra();
    void InitializeSoundSystem();
    void InitializeReplayCRC();
    void InitializeUtils();
    void InitializeReplayCamera();
    void InitializePhysics();
    void InitializeFrameSkipper();
    void InitializeGraphic();
    void InitializePodium();
    void InitializeXPSystem();
    void InitializeTrackUpload();
    void InitializeGameOption();
    void InitializeGameSwf();
    void InitializeGameTime();
    void InitializeVariableFramerate();
    void InitializeDebug();
    void InitializeInGameHud();
    void InitializeMainHub();
    void InitializeMainMenu();
    void InitializeFlash();
    void InitializeGarbageCollector();
    void InitializeSettings();
    void InitializeDebugLocalization();
    
    // NEW: Mod-specific tweakables (not synced to game)
    void InitializeMod();
    
    // NEW: Keybindings tab (top-level category)
    void InitializeKeybindings();
    
    // Helper functions
    void RegisterTweakable(std::shared_ptr<TweakableItem> item);
    bool PassesFilter(const std::string& name);
    
    std::vector<std::shared_ptr<TweakableFolder>> m_rootFolders;
    std::unordered_map<int, std::shared_ptr<TweakableItem>> m_tweakableMap;
    
    // Keybindings storage (separate from root folders)
    std::vector<std::shared_ptr<TweakableItem>> m_keybindingItems;
    std::vector<Keybindings::Action> m_keybindingActions; // Stores the Action for each keybinding button
    std::vector<int> m_keybindingDefaults; // Stores the default key for each keybinding button
    bool m_showKeybindingsWindow;
    
    bool m_isVisible;
    std::string m_searchFilter;
    
    // UI state
    float m_menuWidth;
    float m_menuHeight;
    bool m_showResetButton;
    bool m_showSearchBar;
};

// Global instance (optional - you can use dependency injection instead)
extern DevMenu* g_DevMenu;
