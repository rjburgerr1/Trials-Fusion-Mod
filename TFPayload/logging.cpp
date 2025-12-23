#include "pch.h"
#include "logging.h"

namespace Logging {
    bool g_verboseLoggingEnabled = false;

    void Initialize() {
        g_verboseLoggingEnabled = false;
        std::cout << "[Logging] System initialized. Verbose logging: OFF" << std::endl;
        std::cout << "[Logging] Press '=' to toggle verbose logging" << std::endl;
    }

    void ToggleVerbose() {
        g_verboseLoggingEnabled = !g_verboseLoggingEnabled;
        std::cout << "\n========================================" << std::endl;
        std::cout << "[Logging] Verbose logging " 
                  << (g_verboseLoggingEnabled ? "ENABLED" : "DISABLED") << std::endl;
        std::cout << "========================================\n" << std::endl;
    }

    bool IsVerboseEnabled() {
        return g_verboseLoggingEnabled;
    }
}
