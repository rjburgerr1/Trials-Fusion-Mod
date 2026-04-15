#include "pch.h"
#include "logging.h"
#include "imgui/imgui.h"
#include <ctime>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <Windows.h>

namespace Logging {
    bool g_verboseLoggingEnabled = false;
    std::ofstream g_logFile;
    static std::string s_gameDirectory;
    
    // ImGui console state
    bool g_consoleVisible = false;
    std::vector<ConsoleEntry> g_consoleBuffer;
    std::mutex g_consoleMutex;
    bool g_autoScroll = true;
    bool g_showVerbose = true;
    static const size_t MAX_CONSOLE_ENTRIES = 1000;  // Limit buffer size

    // Get the directory where the game executable is located
    static std::string GetGameDirectory() {
        if (!s_gameDirectory.empty()) {
            return s_gameDirectory;
        }

        char path[MAX_PATH];
        HMODULE hModule = NULL;
        
        // Get the handle to this DLL
        GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            (LPCSTR)&GetGameDirectory,
            &hModule
        );
        
        if (GetModuleFileNameA(hModule, path, MAX_PATH) > 0) {
            std::string fullPath(path);
            size_t lastSlash = fullPath.find_last_of("\\/");
            if (lastSlash != std::string::npos) {
                s_gameDirectory = fullPath.substr(0, lastSlash + 1);
                return s_gameDirectory;
            }
        }
        
        // Fallback to current directory
        return "./";
    }

    void Initialize() {
        // Try to load saved state, default to enabled if no config exists
        if (!LoadConfig()) {
            g_verboseLoggingEnabled = true;  // Default to enabled for new installs
            SaveConfig();  // Create the config file
        }
        
        // Clear crash trace file at startup (so we can see just this session's activity)
        std::string crashTracePath = GetGameDirectory() + "tfpayload_crash_trace.txt";
        FILE* crashFile = nullptr;
        fopen_s(&crashFile, crashTracePath.c_str(), "w");
        if (crashFile) {
            fprintf(crashFile, "=== TFPayload Crash Trace Started ===\n");
            fprintf(crashFile, "This file logs hook entries/exits to identify crashes.\n");
            fprintf(crashFile, "The last entry before a crash indicates the failing module.\n\n");
            fclose(crashFile);
        }
        
        // Open log file in the game directory
        std::string logPath = GetGameDirectory() + "tfpayload_log.txt";
        g_logFile.open(logPath, std::ios::out | std::ios::trunc);
        if (g_logFile.is_open()) {
            // Write header with timestamp
            auto now = std::time(nullptr);
            struct tm tm;
            localtime_s(&tm, &now);
            g_logFile << "=== TFPayload Log Started at " 
                      << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") 
                      << " ===" << std::endl;
            g_logFile.flush();
        }
        
        std::cout << "[Logging] System initialized. Verbose logging: " 
                  << (g_verboseLoggingEnabled ? "ON" : "OFF") << std::endl;
        std::cout << "[Logging] Log file: " << logPath << std::endl;
        std::cout << "[Logging] Press '=' to toggle verbose logging" << std::endl;
    }
    
    void Shutdown() {
        // Save current state before shutdown
        SaveConfig();
        
        if (g_logFile.is_open()) {
            g_logFile << "=== Log Ended ===" << std::endl;
            g_logFile.close();
        }
    }

    void ToggleVerbose() {
        g_verboseLoggingEnabled = !g_verboseLoggingEnabled;
        
        // Print a clean, single message
        std::cout << "Verbose logging " << (g_verboseLoggingEnabled ? "ENABLED" : "DISABLED") << std::endl;
        
        WriteToFile(std::string("Verbose logging ") + (g_verboseLoggingEnabled ? "ENABLED" : "DISABLED"));
        
        // Save the new state
        SaveConfig();
    }
    
    void SetVerbose(bool enabled) {
        g_verboseLoggingEnabled = enabled;
        SaveConfig();
    }

    bool IsVerboseEnabled() {
        return g_verboseLoggingEnabled;
    }
    
    void WriteToFile(const std::string& msg) {
        if (g_logFile.is_open()) {
            g_logFile << msg << std::endl;
            g_logFile.flush();  // Flush immediately to catch crashes
        }
    }
    
    // Pure C immediate write - no C++ objects, no exceptions
    // Opens file, writes, flushes, closes - guaranteed to persist before crash
    // Works even BEFORE Logging::Initialize() is called
    void WriteImmediate(const char* msg) {
        // Write to the existing log file stream if open
        if (g_logFile.is_open()) {
            g_logFile << msg << std::endl;
            g_logFile.flush();
        }
        
        // Also write to a separate crash-safe file using pure C file I/O
        // Use GetGameDirectory which works standalone
        std::string crashLogPath = GetGameDirectory() + "tfpayload_crash_trace.txt";
        FILE* crashFile = nullptr;
        fopen_s(&crashFile, crashLogPath.c_str(), "a");
        if (crashFile) {
            fprintf(crashFile, "%s\n", msg);
            fflush(crashFile);
            fclose(crashFile);
        }
        
        // Also write to console for immediate feedback
        printf("%s\n", msg);
    }
    
    std::string GetConfigPath() {
        // Save to game directory
        return GetGameDirectory() + "tfpayload_logging.cfg";
    }
    
    bool SaveConfig() {
        std::string configPath = GetConfigPath();
        
        std::ofstream file(configPath);
        if (!file.is_open()) {
            std::cout << "[Logging] Failed to save config to: " << configPath << std::endl;
            return false;
        }
        
        file << "# TFPayload Logging Configuration" << std::endl;
        file << "# This file is auto-generated. Edit with caution." << std::endl;
        file << std::endl;
        file << "VerboseLogging=" << (g_verboseLoggingEnabled ? "1" : "0") << std::endl;
        
        file.close();
        return true;
    }
    
    bool LoadConfig() {
        std::string configPath = GetConfigPath();
        
        std::ifstream file(configPath);
        if (!file.is_open()) {
            return false;  // No config file, use defaults
        }
        
        std::string line;
        while (std::getline(file, line)) {
            // Skip comments and empty lines
            if (line.empty() || line[0] == '#') {
                continue;
            }
            
            // Parse line: Key=Value
            size_t equalsPos = line.find('=');
            if (equalsPos == std::string::npos) {
                continue;
            }
            
            std::string key = line.substr(0, equalsPos);
            std::string value = line.substr(equalsPos + 1);
            
            // Trim whitespace
            value.erase(0, value.find_first_not_of(" \t"));
            value.erase(value.find_last_not_of(" \t") + 1);
            
            if (key == "VerboseLogging") {
                g_verboseLoggingEnabled = (value == "1" || value == "true");
            }
        }
        
        file.close();
        return true;
    }
    
    // ImGui console functions
    void AddConsoleEntry(ConsoleEntry::Type type, const std::string& msg) {
        std::lock_guard<std::mutex> lock(g_consoleMutex);
        
        g_consoleBuffer.emplace_back(type, msg);
        
        // Limit buffer size
        if (g_consoleBuffer.size() > MAX_CONSOLE_ENTRIES) {
            g_consoleBuffer.erase(g_consoleBuffer.begin());
        }
    }
    
    void RenderConsole() {
        if (!g_consoleVisible) {
            return;
        }
        
        // Get ImGui context
        ImGuiContext* ctx = ImGui::GetCurrentContext();
        if (!ctx) {
            return;
        }
        
        ImGui::SetNextWindowSize(ImVec2(800, 400), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(50, 50), ImGuiCond_FirstUseEver);
        
        if (ImGui::Begin("TFPayload Console", &g_consoleVisible)) {
            // Console controls
            if (ImGui::Button("Clear")) {
                ClearConsole();
            }
            ImGui::SameLine();
            
            ImGui::Checkbox("Auto-scroll", &g_autoScroll);
            ImGui::SameLine();
            
            ImGui::Checkbox("Show Verbose", &g_showVerbose);
            ImGui::SameLine();
            
            // Show entry count
            std::lock_guard<std::mutex> lock(g_consoleMutex);
            ImGui::Text("Entries: %zu / %zu", g_consoleBuffer.size(), MAX_CONSOLE_ENTRIES);
            
            ImGui::Separator();
            
            // Console text area
            ImGui::BeginChild("ConsoleScrolling", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
            
            // Render each entry with appropriate color
            for (const auto& entry : g_consoleBuffer) {
                // Skip verbose entries if not showing them
                if (entry.type == ConsoleEntry::Type::Verbose && !g_showVerbose) {
                    continue;
                }
                
                // Set color based on type
                ImVec4 color;
                switch (entry.type) {
                    case ConsoleEntry::Type::Error:
                        color = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);  // Red
                        break;
                    case ConsoleEntry::Type::Warning:
                        color = ImVec4(1.0f, 1.0f, 0.3f, 1.0f);  // Yellow
                        break;
                    case ConsoleEntry::Type::Verbose:
                        color = ImVec4(0.6f, 0.6f, 0.6f, 1.0f);  // Gray
                        break;
                    case ConsoleEntry::Type::Info:
                    default:
                        color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);  // White
                        break;
                }
                
                ImGui::PushStyleColor(ImGuiCol_Text, color);
                ImGui::TextUnformatted(entry.message.c_str());
                ImGui::PopStyleColor();
            }
            
            // Auto-scroll to bottom
            if (g_autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
                ImGui::SetScrollHereY(1.0f);
            }
            
            ImGui::EndChild();
        }
        ImGui::End();
    }
    
    void ToggleConsole() {
        g_consoleVisible = !g_consoleVisible;
        LOG_INFO("ImGui Console " << (g_consoleVisible ? "OPENED" : "CLOSED"));
    }
    
    bool IsConsoleVisible() {
        return g_consoleVisible;
    }
    
    void ClearConsole() {
        std::lock_guard<std::mutex> lock(g_consoleMutex);
        g_consoleBuffer.clear();
        LOG_INFO("Console cleared");
    }
}
