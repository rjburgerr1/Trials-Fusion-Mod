#include "pch.h"
#include "logging.h"
#include <ctime>
#include <iomanip>

namespace Logging {
    bool g_verboseLoggingEnabled = false;
    std::ofstream g_logFile;

    void Initialize() {
        g_verboseLoggingEnabled = true;  // Enable verbose by default for debugging
        
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
        
        std::cout << "[Logging] System initialized. Verbose logging: ON" << std::endl;
        std::cout << "[Logging] Log file: tfpayload_log.txt" << std::endl;
        std::cout << "[Logging] Press '=' to toggle verbose logging" << std::endl;
    }
    
    void Shutdown() {
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
}
