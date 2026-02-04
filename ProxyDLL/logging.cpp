#include "logging.h"
#include <Windows.h>
#include <ctime>
#include <iomanip>
#include <fstream>
#include <sstream>

namespace Logging {
    bool g_verboseLoggingEnabled = false;
    std::ofstream g_logFile;
    static std::string s_gameDirectory;
    
    // Use raw Windows API file writing as well for critical messages
    static HANDLE g_hLogFile = INVALID_HANDLE_VALUE;
    
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
    
    void WriteToWindowsLog(const char* msg) {
        if (g_hLogFile == INVALID_HANDLE_VALUE) {
            // Try game directory first
            std::string logPath = GetGameDirectory() + "proxy_debug.log";
            g_hLogFile = CreateFileA(
                logPath.c_str(),
                GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                NULL,
                CREATE_ALWAYS,
                FILE_ATTRIBUTE_NORMAL,
                NULL
            );
            
            // Fallback to current directory if that fails
            if (g_hLogFile == INVALID_HANDLE_VALUE) {
                g_hLogFile = CreateFileA(
                    "proxy_debug.log",
                    GENERIC_WRITE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL,
                    CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL,
                    NULL
                );
            }
        }
        
        if (g_hLogFile != INVALID_HANDLE_VALUE) {
            DWORD written;
            WriteFile(g_hLogFile, msg, (DWORD)strlen(msg), &written, NULL);
            WriteFile(g_hLogFile, "\r\n", 2, &written, NULL);
            FlushFileBuffers(g_hLogFile);
        }
    }
    
    void WriteToConsole(const char* msg) {
        // Only write to console if it actually exists
        HWND consoleWindow = GetConsoleWindow();
        if (!consoleWindow) {
            // Console doesn't exist yet, skip console output
            return;
        }
        
        // Try WriteConsole first (more reliable)
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        if (hConsole != INVALID_HANDLE_VALUE && hConsole != NULL) {
            DWORD written;
            WriteConsoleA(hConsole, msg, (DWORD)strlen(msg), &written, NULL);
            WriteConsoleA(hConsole, "\n", 1, &written, NULL);
        } else {
            // Fallback to std::cout only if WriteConsole failed
            std::cout << msg << std::endl;
            std::cout.flush();
        }
    }

    void Initialize() {
        // ALWAYS start with verbose logging OFF
        // User can toggle it on with '=' key, and it will be saved
        g_verboseLoggingEnabled = false;
        
        // Load config to see if user previously enabled it
        LoadConfig();
        
        // Open log file in the game directory
        std::string logPath = GetGameDirectory() + "proxydll_log.txt";
        g_logFile.open(logPath, std::ios::out | std::ios::trunc);
        if (g_logFile.is_open()) {
            // Write header with timestamp
            auto now = std::time(nullptr);
            struct tm tm;
            localtime_s(&tm, &now);
            g_logFile << "=== ProxyDLL Log Started at " 
                      << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") 
                      << " ===" << std::endl;
            g_logFile.flush();
        }
        
        // Silent initialization - TFPayload will handle user-facing logging messages
        WriteToWindowsLog(("[ProxyDLL] Log file: " + logPath).c_str());
        WriteToWindowsLog(("[ProxyDLL] Verbose logging: " + std::string(g_verboseLoggingEnabled ? "ON" : "OFF")).c_str());
        
        WriteToWindowsLog("=== ProxyDLL Logging System Initialized ===");
    }
    
    void Shutdown() {
        // Save current state before shutdown
        SaveConfig();
        
        if (g_logFile.is_open()) {
            g_logFile << "=== Log Ended ===" << std::endl;
            g_logFile.close();
        }
        
        if (g_hLogFile != INVALID_HANDLE_VALUE) {
            WriteToWindowsLog("=== ProxyDLL Logging System Shutdown ===");
            CloseHandle(g_hLogFile);
            g_hLogFile = INVALID_HANDLE_VALUE;
        }
    }

    void ToggleVerbose() {
        g_verboseLoggingEnabled = !g_verboseLoggingEnabled;
        // Save the new state (no console output - TFPayload handles that)
        SaveConfig();
        WriteToWindowsLog(("[ProxyDLL] Verbose logging " + std::string(g_verboseLoggingEnabled ? "ENABLED" : "DISABLED")).c_str());
    }
    
    void SetVerbose(bool enabled) {
        g_verboseLoggingEnabled = enabled;
        SaveConfig();
    }

    bool IsVerboseEnabled() {
        return g_verboseLoggingEnabled;
    }
    
    void WriteToFile(const std::string& msg) {
        // Write to Windows log file first (most reliable, always works)
        WriteToWindowsLog(msg.c_str());
        
        // Write to ofstream log if available
        if (g_logFile.is_open()) {
            g_logFile << msg << std::endl;
            g_logFile.flush();  // Flush immediately to catch crashes
        }
        
        // Write to console last (only if console exists)
        WriteToConsole(msg.c_str());
    }
    
    std::string GetConfigPath() {
        return GetGameDirectory() + "proxydll_logging.cfg";
    }
    
    bool SaveConfig() {
        std::string configPath = GetConfigPath();
        
        std::ofstream file(configPath);
        if (!file.is_open()) {
            std::string errMsg = std::string("[Logging] Failed to save config to: ") + configPath;
            WriteToConsole(errMsg.c_str());
            return false;
        }
        
        file << "# ProxyDLL Logging Configuration" << std::endl;
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
