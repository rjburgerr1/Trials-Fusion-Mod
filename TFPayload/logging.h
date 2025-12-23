#pragma once
#include <iostream>
#include <string>
#include <sstream>

namespace Logging {
    // Global verbose logging flag
    extern bool g_verboseLoggingEnabled;

    // Initialize logging system
    void Initialize();

    // Toggle verbose logging
    void ToggleVerbose();

    // Check if verbose logging is enabled
    bool IsVerboseEnabled();

    // Logging macros for different levels
    #define LOG_INFO(...) \
        do { \
            std::ostringstream oss; \
            oss << __VA_ARGS__; \
            std::cout << oss.str() << std::endl; \
        } while(0)

    #define LOG_VERBOSE(...) \
        do { \
            if (Logging::IsVerboseEnabled()) { \
                std::ostringstream oss; \
                oss << "[VERBOSE] " << __VA_ARGS__; \
                std::cout << oss.str() << std::endl; \
            } \
        } while(0)

    #define LOG_WARNING(...) \
        do { \
            std::ostringstream oss; \
            oss << "[WARNING] " << __VA_ARGS__; \
            std::cout << oss.str() << std::endl; \
        } while(0)

    #define LOG_ERROR(...) \
        do { \
            std::ostringstream oss; \
            oss << "[ERROR] " << __VA_ARGS__; \
            std::cout << oss.str() << std::endl; \
        } while(0)
}
