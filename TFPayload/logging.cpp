#include "pch.h"
#include "logging.h"
#include "imgui/imgui.h"
#include <ctime>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <functional>
#include <algorithm>
#include <Windows.h>

namespace Logging {
    bool g_verboseLoggingEnabled = false;
    std::ofstream g_logFile;
    static std::string s_gameDirectory;

    static bool HasNonWhitespace(const std::string& msg) {
        return msg.find_first_not_of(" \t\r\n") != std::string::npos;
    }

    static void ForEachNonEmptyLogLine(const std::string& msg, const std::function<void(const std::string&)>& writeLine) {
        size_t lineStart = 0;
        while (lineStart <= msg.size()) {
            size_t lineEnd = msg.find_first_of("\r\n", lineStart);
            std::string line = msg.substr(lineStart, lineEnd == std::string::npos ? std::string::npos : lineEnd - lineStart);

            if (HasNonWhitespace(line)) {
                writeLine(line);
            }

            if (lineEnd == std::string::npos) {
                break;
            }

            lineStart = lineEnd + 1;
            if (msg[lineEnd] == '\r' && lineStart < msg.size() && msg[lineStart] == '\n') {
                ++lineStart;
            }
        }
    }

    void WriteToConsole(const char* msg) {
        if (!msg) {
            return;
        }

        OutputDebugStringA(msg);
        OutputDebugStringA("\n");

        if (!GetConsoleWindow()) {
            return;
        }

        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        if (hConsole == INVALID_HANDLE_VALUE || hConsole == NULL) {
            return;
        }

        DWORD written = 0;
        WriteConsoleA(hConsole, msg, (DWORD)strlen(msg), &written, NULL);
        WriteConsoleA(hConsole, "\n", 1, &written, NULL);
    }
    
    // ImGui console state
    bool g_consoleVisible = false;
    std::vector<ConsoleEntry> g_consoleBuffer;
    std::mutex g_consoleMutex;
    bool g_autoScroll = true;
    bool g_showVerbose = true;
    static const size_t MAX_CONSOLE_ENTRIES = 1000;  // Limit buffer size

    static bool WriteTextToClipboard(const std::string& text) {
        if (text.empty()) {
            return false;
        }

        if (!OpenClipboard(NULL)) {
            return false;
        }

        bool success = false;
        if (EmptyClipboard()) {
            HGLOBAL clipboardMemory = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1);
            if (clipboardMemory) {
                void* clipboardData = GlobalLock(clipboardMemory);
                if (clipboardData) {
                    memcpy(clipboardData, text.c_str(), text.size() + 1);
                    GlobalUnlock(clipboardMemory);

                    if (SetClipboardData(CF_TEXT, clipboardMemory)) {
                        clipboardMemory = NULL;
                        success = true;
                    }
                }

                if (clipboardMemory) {
                    GlobalFree(clipboardMemory);
                }
            }
        }

        CloseClipboard();
        return success;
    }

    static bool ShouldIncludeConsoleEntry(const ConsoleEntry& entry) {
        return g_showVerbose || entry.type != ConsoleEntry::Type::Verbose;
    }

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
        
        WriteToConsole((std::string("[Logging] System initialized. Verbose logging: ") +
            (g_verboseLoggingEnabled ? "ON" : "OFF")).c_str());
        WriteToConsole((std::string("[Logging] Log file: ") + logPath).c_str());
        WriteToConsole("[Logging] Press '=' to toggle verbose logging");
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
        ForEachNonEmptyLogLine(msg, [](const std::string& line) {
            if (g_logFile.is_open()) {
                g_logFile << line << std::endl;
                g_logFile.flush();  // Flush immediately to catch crashes
            }

            WriteToConsole(line.c_str());
        });
    }
    
    // Pure C immediate write - no C++ objects, no exceptions
    // Opens file, writes, flushes, closes - guaranteed to persist before crash
    // Works even BEFORE Logging::Initialize() is called
    void WriteImmediate(const char* msg) {
        if (!msg || !HasNonWhitespace(msg)) {
            return;
        }

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
        
        WriteToConsole(msg);
    }
    
    std::string GetConfigPath() {
        // Save to game directory
        return GetGameDirectory() + "tfpayload_logging.cfg";
    }
    
    bool SaveConfig() {
        std::string configPath = GetConfigPath();
        
        std::ofstream file(configPath);
        if (!file.is_open()) {
            WriteToConsole((std::string("[Logging] Failed to save config to: ") + configPath).c_str());
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
#ifndef DEVELOPMENT_MODE
        (void)type;
        (void)msg;
        return;
#else
        std::lock_guard<std::mutex> lock(g_consoleMutex);

        ForEachNonEmptyLogLine(msg, [type](const std::string& line) {
            g_consoleBuffer.emplace_back(type, line);

            // Limit buffer size
            if (g_consoleBuffer.size() > MAX_CONSOLE_ENTRIES) {
                g_consoleBuffer.erase(g_consoleBuffer.begin());
            }
        });
#endif
    }
    
    void RenderConsole() {
#ifndef DEVELOPMENT_MODE
        return;
#else
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

            if (ImGui::Button("Copy 10")) {
                CopyLastConsoleLines(10);
            }
            ImGui::SameLine();

            if (ImGui::Button("Copy 100")) {
                CopyLastConsoleLines(100);
            }
            ImGui::SameLine();

            if (ImGui::Button("Copy 500")) {
                CopyLastConsoleLines(500);
            }
            ImGui::SameLine();

            if (ImGui::Button("Copy 1000")) {
                CopyLastConsoleLines(1000);
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
                if (!ShouldIncludeConsoleEntry(entry)) {
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
#endif
    }
    
    void ToggleConsole() {
#ifdef DEVELOPMENT_MODE
        g_consoleVisible = !g_consoleVisible;
        LOG_INFO("ImGui Console " << (g_consoleVisible ? "OPENED" : "CLOSED"));
#endif
    }
    
    bool IsConsoleVisible() {
        return g_consoleVisible;
    }
    
    void ClearConsole() {
        {
            std::lock_guard<std::mutex> lock(g_consoleMutex);
            g_consoleBuffer.clear();
        }

        LOG_INFO("Console cleared");
    }

    bool CopyLastConsoleLines(size_t lineCount) {
#ifndef DEVELOPMENT_MODE
        (void)lineCount;
        return false;
#else
        if (lineCount == 0) {
            return false;
        }

        std::vector<std::string> lines;
        {
            std::lock_guard<std::mutex> lock(g_consoleMutex);
            const size_t reserveCount = lineCount < g_consoleBuffer.size() ? lineCount : g_consoleBuffer.size();
            lines.reserve(reserveCount);

            for (auto it = g_consoleBuffer.rbegin(); it != g_consoleBuffer.rend() && lines.size() < lineCount; ++it) {
                if (ShouldIncludeConsoleEntry(*it)) {
                    lines.push_back(it->message);
                }
            }
        }

        if (lines.empty()) {
            LOG_WARNING("[Console] No log lines available to copy");
            return false;
        }

        std::ostringstream clipboardText;
        for (auto it = lines.rbegin(); it != lines.rend(); ++it) {
            clipboardText << *it;
            if (it + 1 != lines.rend()) {
                clipboardText << "\r\n";
            }
        }

        if (!WriteTextToClipboard(clipboardText.str())) {
            LOG_ERROR("[Console] Failed to copy log lines to clipboard");
            return false;
        }

        LOG_INFO("[Console] Copied last " << lines.size() << " log line(s) to clipboard");
        return true;
#endif
    }
}
