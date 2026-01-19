#include "pch.h"
#include "base-address.h"
#include "logging.h"
#include <algorithm>

namespace BaseAddress {
    bool IsSteamVersion() {
        // Method 1: Check for steam_api64.dll (most reliable)
        HMODULE steamModule = GetModuleHandleA("steam_api64.dll");
        if (steamModule != nullptr) {
            return true;
        }
        
        // Method 2: Check for steamclient64.dll (backup)
        steamModule = GetModuleHandleA("steamclient64.dll");
        if (steamModule != nullptr) {
            return true;
        }
        
        // Method 3: Check executable path for "steam" keyword
        char exePath[MAX_PATH];
        if (GetModuleFileNameA(nullptr, exePath, MAX_PATH) > 0) {
            std::string path(exePath);
            
            // Convert to lowercase for case-insensitive comparison
            std::transform(path.begin(), path.end(), path.begin(), ::tolower);
            
            if (path.find("steam") != std::string::npos) {
                return true;
            }
        }
        
        return false;
    }
}
