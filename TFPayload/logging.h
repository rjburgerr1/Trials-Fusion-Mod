#pragma once
#include <iostream>
#include <fstream>
#include <string>
#include <sstream>

namespace Logging {
    // Global verbose logging flag
    extern bool g_verboseLoggingEnabled;
    
    // Global file stream for logging
    extern std::ofstream g_logFile;

    // Initialize logging system
    void Initialize();
    
    // Shutdown logging system
    void Shutdown();

    // Toggle verbose logging
    void ToggleVerbose();

    // Check if verbose logging is enabled
    bool IsVerboseEnabled();
    
    // Write to log file
    void WriteToFile(const std::string& msg);

    // Logging macros for different levels
    #define LOG_INFO(...) \
        do { \
            std::ostringstream oss; \
            oss << __VA_ARGS__; \
            std::cout << oss.str() << std::endl; \
            Logging::WriteToFile(oss.str()); \
        } while(0)

    #define LOG_VERBOSE(...) \
        do { \
            if (Logging::IsVerboseEnabled()) { \
                std::ostringstream oss; \
                oss << "[VERBOSE] " << __VA_ARGS__; \
                std::cout << oss.str() << std::endl; \
                Logging::WriteToFile(oss.str()); \
            } \
        } while(0)

    #define LOG_WARNING(...) \
        do { \
            std::ostringstream oss; \
            oss << "[WARNING] " << __VA_ARGS__; \
            std::cout << oss.str() << std::endl; \
            Logging::WriteToFile(oss.str()); \
        } while(0)

    #define LOG_ERROR(...) \
        do { \
            std::ostringstream oss; \
            oss << "[ERROR] " << __VA_ARGS__; \
            std::cout << oss.str() << std::endl; \
            Logging::WriteToFile(oss.str()); \
        } while(0)
}
