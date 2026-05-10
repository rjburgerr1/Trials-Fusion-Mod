#pragma once
#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <vector>
#include <mutex>

namespace Logging {
    // Console entry structure
    struct ConsoleEntry {
        enum class Type {
            Info,
            Warning,
            Error,
            Verbose
        };
        
        Type type;
        std::string message;
        
        ConsoleEntry(Type t, const std::string& msg) : type(t), message(msg) {}
    };
    
    // Global verbose logging flag
    extern bool g_verboseLoggingEnabled;
    
    // Global file stream for logging
    extern std::ofstream g_logFile;
    
    // ImGui console state
    extern bool g_consoleVisible;
    extern std::vector<ConsoleEntry> g_consoleBuffer;
    extern std::mutex g_consoleMutex;
    extern bool g_autoScroll;
    extern bool g_showVerbose;

    // Initialize logging system
    void Initialize();
    
    // Shutdown logging system
    void Shutdown();

    // Toggle verbose logging
    void ToggleVerbose();

    // Check if verbose logging is enabled
    bool IsVerboseEnabled();
    
    // Set verbose logging state directly
    void SetVerbose(bool enabled);
    
    // Config persistence
    bool SaveConfig();
    bool LoadConfig();
    std::string GetConfigPath();
    
    // Write to log file
    void WriteToFile(const std::string& msg);
    
    // Write to log file immediately (for crash debugging) - pure C, no exceptions
    void WriteImmediate(const char* msg);
    
    // ImGui console functions
    void AddConsoleEntry(ConsoleEntry::Type type, const std::string& msg);
    void RenderConsole();
    void ToggleConsole();
    bool IsConsoleVisible();
    void ClearConsole();

    // Logging macros for different levels
    #define LOG_INFO(...) \
        do { \
            std::ostringstream oss; \
            oss << __VA_ARGS__; \
            Logging::WriteToFile(oss.str()); \
            Logging::AddConsoleEntry(Logging::ConsoleEntry::Type::Info, oss.str()); \
        } while(0)

    #define LOG_VERBOSE(...) \
        do { \
            if (Logging::IsVerboseEnabled()) { \
                std::ostringstream oss; \
                oss << "[VERBOSE] " << __VA_ARGS__; \
                Logging::WriteToFile(oss.str()); \
                Logging::AddConsoleEntry(Logging::ConsoleEntry::Type::Verbose, oss.str()); \
            } \
        } while(0)

    #define LOG_WARNING(...) \
        do { \
            std::ostringstream oss; \
            oss << "[WARNING] " << __VA_ARGS__; \
            Logging::WriteToFile(oss.str()); \
            Logging::AddConsoleEntry(Logging::ConsoleEntry::Type::Warning, oss.str()); \
        } while(0)

    #define LOG_ERROR(...) \
        do { \
            std::ostringstream oss; \
            oss << "[ERROR] " << __VA_ARGS__; \
            Logging::WriteToFile(oss.str()); \
            Logging::AddConsoleEntry(Logging::ConsoleEntry::Type::Error, oss.str()); \
        } while(0)

    // Hook entry/exit logging - writes immediately to file for crash debugging
    // Use this at the START of every hooked function to track which hook crashes
    #define LOG_HOOK_ENTRY(hookName) \
        Logging::WriteImmediate("[HOOK ENTRY] " hookName)
    
    #define LOG_HOOK_EXIT(hookName) \
        Logging::WriteImmediate("[HOOK EXIT] " hookName)
    
    // For init functions - log before calling
    #define LOG_INIT_START(moduleName) \
        Logging::WriteImmediate("[INIT START] " moduleName)
    
    #define LOG_INIT_END(moduleName) \
        Logging::WriteImmediate("[INIT END] " moduleName)
}
