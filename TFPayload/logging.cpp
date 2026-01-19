#include "pch.h"
#include "logging.h"
#include <ctime>
#include <iomanip>
#include <fstream>
#include <sstream>

namespace Logging {
    bool g_verboseLoggingEnabled = false;
    std::ofstream g_logFile;

    void Initialize() {
        // Try to load saved state, default to enabled if no config exists
        if (!LoadConfig()) {
            g_verboseLoggingEnabled = true;  // Default to enabled for new installs
            SaveConfig();  // Create the config file
        }
        
        // Open log file in the game directory
        g_logFile.open("tfpayload_log.txt", std::ios::out | std::ios::trunc);
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
        std::cout << "[Logging] Log file: tfpayload_log.txt" << std::endl;
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
        std::cout << "\n========================================" << std::endl;
        std::cout << "[Logging] Verbose logging " 
                  << (g_verboseLoggingEnabled ? "ENABLED" : "DISABLED") << std::endl;
        std::cout << "========================================\n" << std::endl;
        
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
    
    std::string GetConfigPath() {
        // Save to same location as keybindings config
        return "F:\\tfpayload_logging.cfg";
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
}
