#define _CRT_SECURE_NO_WARNINGS
#include "pch.h"
#include "multiplayer.h"
#include "logging.h"
#include "keybindings.h"
#include <MinHook.h>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <mutex>

namespace Multiplayer {
    // Configuration
    static bool g_PacketLoggingEnabled = true;
    static bool g_SessionLoggingEnabled = true;
    static DWORD_PTR g_BaseAddress = 0;
    
    // Statistics
    static Stats g_Stats = {};
    static std::mutex g_StatsMutex;
    
    // Logged data
    static std::vector<SessionInfo> g_SessionHistory;
    static std::vector<NetworkPacketInfo> g_PacketLog;
    static std::vector<PlayerStateInfo> g_PlayerStates;
    static std::mutex g_LogMutex;
    
    // Current session state
    static SessionInfo g_CurrentSession = {};
    static bool g_InSession = false;
    
    // Hook storage
    typedef void* (__fastcall* MultiplayerServiceConstructor_t)(void* thisPtr, void* edx, void* param1, void* param2, void* param3);
    static MultiplayerServiceConstructor_t g_OriginalMultiplayerServiceConstructor = nullptr;
    
    typedef void (__fastcall* InitializeMultiplayerSession_t)(void);
    static InitializeMultiplayerSession_t g_OriginalInitializeMultiplayerSession = nullptr;
    
    typedef void* (__fastcall* CreateGameplaySession_t)(void* thisPtr, void* edx, void* param1, void* param2);
    static CreateGameplaySession_t g_OriginalCreateGameplaySession = nullptr;
    
    typedef void (__fastcall* ProcessNetworkPackets_t)(void* thisPtr, void* edx);
    static ProcessNetworkPackets_t g_OriginalProcessNetworkPackets = nullptr;
    
    typedef void (__fastcall* HandleNetworkPacket_t)(void* thisPtr, void* edx, void* packet);
    static HandleNetworkPacket_t g_OriginalHandleNetworkPacket = nullptr;
    
    typedef void (__fastcall* UpdateMultiplayerState_t)(void* thisPtr, void* edx);
    static UpdateMultiplayerState_t g_OriginalUpdateMultiplayerState = nullptr;
    
    typedef void (__fastcall* PrepareAndLoadMultiplayerMenu_t)(int param_1, void* edx);
    static PrepareAndLoadMultiplayerMenu_t g_OriginalPrepareAndLoadMultiplayerMenu = nullptr;
    
    typedef void (__fastcall* SendStartLiveQuickGameMessage_t)(int param_1, void* edx);
    static SendStartLiveQuickGameMessage_t g_OriginalSendStartLiveQuickGameMessage = nullptr;
    
    typedef void (__fastcall* SendCreatePrivateRaceMessage_t)(int param_1, void* edx);
    static SendCreatePrivateRaceMessage_t g_OriginalSendCreatePrivateRaceMessage = nullptr;
    
    typedef void (__fastcall* SendStartPrivateRaceMessage_t)(int param_1, void* edx);
    static SendStartPrivateRaceMessage_t g_OriginalSendStartPrivateRaceMessage = nullptr;
    
    typedef void (__fastcall* start_matchmaking_t)(void* thisPtr, void* edx, unsigned int param_1, int* param_2);
    static start_matchmaking_t g_Original_start_matchmaking = nullptr;
    
    typedef void (__fastcall* HandleMultiplayerLobbyStart_t)(void* thisPtr, void* edx, char param_1);
    static HandleMultiplayerLobbyStart_t g_OriginalHandleMultiplayerLobbyStart = nullptr;
    
    typedef void (__fastcall* BroadcastGameStartToPlayers_t)(int param_1, void* edx);
    static BroadcastGameStartToPlayers_t g_OriginalBroadcastGameStartToPlayers = nullptr;
    
    typedef void (__fastcall* SendMoveToLiveLobbyMessage_t)(int param_1, void* edx);
    static SendMoveToLiveLobbyMessage_t g_OriginalSendMoveToLiveLobbyMessage = nullptr;
    
    typedef void (__cdecl* MoveToLocalMultiplayerLobby_t)(void);
    static MoveToLocalMultiplayerLobby_t g_OriginalMoveToLocalMultiplayerLobby = nullptr;
    
    // MinHook doesn't support __thiscall directly, so we use __fastcall and ignore edx
    typedef void (__fastcall* SetMultiplayerMode_t)(void* thisPtr, void* edx, char mode);
    static SetMultiplayerMode_t g_OriginalSetMultiplayerMode = nullptr;
    
    // SetMultiplayerJoinMode - called when clicking Private Match / Private Match (Spectator) buttons
    // void __thiscall SetMultiplayerJoinMode(void *this, int param_1, char param_2)
    // param_1: mode (0=ranked, 2=private host, 3=private spectator)
    // param_2: some flag
    typedef void (__fastcall* SetMultiplayerJoinMode_t)(void* thisPtr, void* edx, int mode, char param_2);
    static SetMultiplayerJoinMode_t g_OriginalSetMultiplayerJoinMode = nullptr;
    
    // StartRace - actually launches the race (called after lobby setup)
    // Ghidra: 0x009da450
    typedef void (__fastcall* StartRace_t)(int* param_1, void* edx);
    static StartRace_t g_OriginalStartRace = nullptr;
    
    // New hooks for race start detection
    typedef void (__fastcall* StartGameOrReturnToLobby_t)(void* thisPtr, void* edx, uint32_t param_1);
    static StartGameOrReturnToLobby_t g_OriginalStartGameOrReturnToLobby = nullptr;
    
    typedef void (__cdecl* handle_game_start_by_mode_t)(char param_1);
    static handle_game_start_by_mode_t g_Original_handle_game_start_by_mode = nullptr;
    
    typedef void (__fastcall* HandleMultiplayerLobbyStart2_t)(void* thisPtr, void* edx, char param_1);
    static HandleMultiplayerLobbyStart2_t g_OriginalHandleMultiplayerLobbyStart2 = nullptr;
    
    typedef void (__fastcall* NotifyNetworkEvent_t)(void* thisPtr, void* edx, uint32_t eventType, void* eventData);
    static NotifyNetworkEvent_t g_OriginalNotifyNetworkEvent = nullptr;
    
    // StartMultiplayerSession
    typedef void (__fastcall* StartMultiplayerSession_t)(void* param_1, void* edx);
    static StartMultiplayerSession_t g_OriginalStartMultiplayerSession = nullptr;
    
    // Session creation/join hooks for capturing session IDs
    // HandleSessionCreateEvent signature from Ghidra:
    // void __thiscall HandleSessionCreateEvent(void *this, uint *param_1, uint param_2)
    typedef void (__fastcall* HandleSessionCreateEvent_t)(void* thisPtr, void* edx, uint32_t* param_1, uint32_t param_2);
    static HandleSessionCreateEvent_t g_OriginalHandleSessionCreateEvent = nullptr;
    
    // HandleSessionJoinRequest signature from Ghidra:
    // undefined4 * __thiscall HandleSessionJoinRequest(void *this, undefined4 *param_1, int *param_2)
    typedef void* (__fastcall* HandleSessionJoinRequest_t)(void* thisPtr, void* edx, void* param_1, int param_2);
    static HandleSessionJoinRequest_t g_OriginalHandleSessionJoinRequest = nullptr;
    
    // LoadAndStartRace - the key function!
    typedef char (__fastcall* LoadAndStartRace_t)(void* thisPtr, void* edx, int* param_1, int param_2, int* param_3, uint32_t param_4, char param_5);
    static LoadAndStartRace_t g_OriginalLoadAndStartRace = nullptr;
    
    // Utility functions
    static uint64_t GetTimestamp() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
    }
    
    static std::string GetTimestampString() {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
        return ss.str();
    }
    
    static void LogSessionEvent(const std::string& event, const std::string& details = "") {
        if (!g_SessionLoggingEnabled) return;
        
        LOG_VERBOSE("[MP-SESSION] [" << GetTimestampString() << "] " << event 
            << (details.empty() ? "" : ": " + details));
        
        // Also write to dedicated session log file
        std::ofstream logFile("F:/mp_session_log.txt", std::ios::app);
        if (logFile.is_open()) {
            logFile << "[" << GetTimestampString() << "] " << event;
            if (!details.empty()) {
                logFile << ": " << details;
            }
            logFile << std::endl;
            logFile.close();
        }
    }
    
    static void LogPacket(const std::string& direction, uint32_t type, uint32_t size, void* data = nullptr) {
        if (!g_PacketLoggingEnabled) return;
        
        std::lock_guard<std::mutex> lock(g_StatsMutex);
        if (direction == "RECV") {
            g_Stats.totalPacketsReceived++;
            g_Stats.totalBytesReceived += size;
        } else {
            g_Stats.totalPacketsSent++;
            g_Stats.totalBytesSent += size;
        }
        
        LOG_VERBOSE("[MP-PACKET] " << direction << " Type: 0x" << std::hex << type 
            << " Size: " << std::dec << size << " bytes");
        
        // Store packet info if we want detailed analysis
        if (data && g_PacketLog.size() < 10000) { // Limit to prevent memory issues
            NetworkPacketInfo packet;
            packet.packetType = type;
            packet.packetSize = size;
            packet.timestamp = GetTimestamp();
            // We could copy data here if needed
            std::lock_guard<std::mutex> lock(g_LogMutex);
            g_PacketLog.push_back(packet);
        }
    }
    
    // Helper function for safe memory reading (no C++ objects with destructors)
    static bool TryReadMemory(DWORD_PTR address, unsigned char* output, size_t size) {
        __try {
            unsigned char* ptr = (unsigned char*)address;
            for (size_t i = 0; i < size; i++) {
                output[i] = ptr[i];
            }
            return true;
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }
    
    // Helper to safely read int values
    static bool TryReadInt(void* address, int* output) {
        unsigned char bytes[4];
        if (TryReadMemory((DWORD_PTR)address, bytes, 4)) {
            *output = *(int*)bytes;
            return true;
        }
        return false;
    }
    
    // === HOOK FUNCTIONS ===
    
    void* __fastcall Hook_MultiplayerServiceConstructor(void* thisPtr, void* edx, void* param1, void* param2, void* param3) {
        LogSessionEvent("MultiplayerServiceConstructor CALLED", 
            "thisPtr: 0x" + std::to_string((uintptr_t)thisPtr));
        
        // Log parameters
        LOG_VERBOSE("[MP-CONSTRUCTOR] param1: 0x" << std::hex << (uintptr_t)param1);
        LOG_VERBOSE("[MP-CONSTRUCTOR] param2: 0x" << std::hex << (uintptr_t)param2);
        LOG_VERBOSE("[MP-CONSTRUCTOR] param3: 0x" << std::hex << (uintptr_t)param3);
        
        // Call original
        void* result = g_OriginalMultiplayerServiceConstructor(thisPtr, edx, param1, param2, param3);
        
        LogSessionEvent("MultiplayerServiceConstructor COMPLETED");
        return result;
    }
    
    void __fastcall Hook_InitializeMultiplayerSession() {
        LogSessionEvent("InitializeMultiplayerSession CALLED", "Creating new multiplayer session");
        
        // Update session state
        g_InSession = true;
        g_CurrentSession.timestamp = GetTimestamp();
        g_CurrentSession.sessionId = static_cast<uint32_t>(GetTimestamp() & 0xFFFFFFFF);
        
        std::lock_guard<std::mutex> lock(g_StatsMutex);
        g_Stats.sessionCount++;
        
        // Call original
        g_OriginalInitializeMultiplayerSession();
        
        LogSessionEvent("InitializeMultiplayerSession COMPLETED", 
            "Session ID: " + std::to_string(g_CurrentSession.sessionId));
        
        // Store in history
        std::lock_guard<std::mutex> historyLock(g_LogMutex);
        g_SessionHistory.push_back(g_CurrentSession);
    }
    
    void* __fastcall Hook_CreateGameplaySession(void* thisPtr, void* edx, void* param1, void* param2) {
        LogSessionEvent("CreateGameplaySession CALLED",
            "thisPtr: 0x" + std::to_string((uintptr_t)thisPtr));
        
        LOG_VERBOSE("[MP-SESSION] param1: 0x" << std::hex << (uintptr_t)param1);
        LOG_VERBOSE("[MP-SESSION] param2: 0x" << std::hex << (uintptr_t)param2);
        
        // Try to read session data from param2 if it's valid
        if (param2) {
            // The CreateGameplaySession function expects specific data structures
            // We'll log what we can see
            uint32_t* data = static_cast<uint32_t*>(param2);
            LOG_VERBOSE("[MP-SESSION] Param2 data [0]: 0x" << std::hex << data[0]);
            LOG_VERBOSE("[MP-SESSION] Param2 data [1]: 0x" << std::hex << data[1]);
        }
        
        // Call original
        void* result = g_OriginalCreateGameplaySession(thisPtr, edx, param1, param2);
        
        LogSessionEvent("CreateGameplaySession COMPLETED",
            "Result: 0x" + std::to_string((uintptr_t)result));
        
        return result;
    }
    
    void __fastcall Hook_ProcessNetworkPackets(void* thisPtr, void* edx) {
        // Don't log every call as this happens frequently
        // Just count them
        static uint32_t callCount = 0;
        callCount++;
        
        if (callCount % 100 == 0) {
            LOG_VERBOSE("[MP-NETWORK] ProcessNetworkPackets called " << callCount << " times");
        }
        
        // Call original
        g_OriginalProcessNetworkPackets(thisPtr, edx);
    }
    
    void __fastcall Hook_HandleNetworkPacket(void* thisPtr, void* edx, void* packet) {
        if (packet) {
            // Try to read packet header
            uint32_t* packetData = static_cast<uint32_t*>(packet);
            uint32_t packetType = packetData[0];
            uint32_t packetSize = packetData[1];
            
            LogPacket("RECV", packetType, packetSize, packet);
        }
        
        // Call original
        g_OriginalHandleNetworkPacket(thisPtr, edx, packet);
    }
    
    void __fastcall Hook_UpdateMultiplayerState(void* thisPtr, void* edx) {
        static uint32_t updateCount = 0;
        updateCount++;
        
        if (updateCount % 60 == 0) { // Log every 60 updates
            LogSessionEvent("UpdateMultiplayerState", 
                "Update count: " + std::to_string(updateCount));
            
            std::lock_guard<std::mutex> lock(g_StatsMutex);
            g_Stats.playerStateUpdates = updateCount;
        }
        
        // Call original
        g_OriginalUpdateMultiplayerState(thisPtr, edx);
    }
    
    void __fastcall Hook_PrepareAndLoadMultiplayerMenu(int param_1, void* edx) {
        LOG_VERBOSE("");
        LOG_VERBOSE("[MP-HOOK] ===== PREPARE AND LOAD MULTIPLAYER MENU =====");
        LOG_VERBOSE("[MP-HOOK] User clicked MULTIPLAYER from main menu!");
        LOG_VERBOSE("[MP-HOOK] param_1: 0x" << std::hex << param_1);
        
        // Log to file
        std::ofstream logFile("F:/mp_session_log.txt", std::ios::app);
        if (logFile.is_open()) {
            logFile << "[PrepareAndLoadMultiplayerMenu] CLICKED MULTIPLAYER!" << std::endl;
            logFile.close();
        }
        
        // Call original
        if (g_OriginalPrepareAndLoadMultiplayerMenu) {
            g_OriginalPrepareAndLoadMultiplayerMenu(param_1, edx);
            LOG_VERBOSE("[MP-HOOK] Original PrepareAndLoadMultiplayerMenu called successfully");
        }
        
        LOG_VERBOSE("[MP-HOOK] ===== PREPARE AND LOAD COMPLETE =====");
        LOG_VERBOSE("");
    }
    
    void __fastcall Hook_SendStartLiveQuickGameMessage(int param_1, void* edx) {
        // Call original FIRST to prevent crashes
        if (g_OriginalSendStartLiveQuickGameMessage) {
            g_OriginalSendStartLiveQuickGameMessage(param_1, edx);
        }
        
        // Then log (safe even if this fails)
        LOG_VERBOSE("");
        LOG_VERBOSE("[MP-HOOK] ===== RANKED/QUICK MATCH STARTED =====");
        LOG_VERBOSE("[MP-HOOK] param_1: 0x" << std::hex << param_1);
        LOG_VERBOSE("");
        
        std::ofstream logFile("F:/mp_session_log.txt", std::ios::app);
        if (logFile.is_open()) {
            logFile << "[SendStartLiveQuickGameMessage] RANKED MATCH STARTED" << std::endl;
        }
    }
    
    void __fastcall Hook_SendCreatePrivateRaceMessage(int param_1, void* edx) {
        // Call original FIRST to prevent crashes
        if (g_OriginalSendCreatePrivateRaceMessage) {
            g_OriginalSendCreatePrivateRaceMessage(param_1, edx);
        }
        
        // Then log (safe even if this fails)
        LOG_VERBOSE("");
        LOG_VERBOSE("[MP-HOOK] ===== PRIVATE MATCH CREATED =====");
        LOG_VERBOSE("[MP-HOOK] param_1: 0x" << std::hex << param_1);
        LOG_VERBOSE("");
        
        std::ofstream logFile("F:/mp_session_log.txt", std::ios::app);
        if (logFile.is_open()) {
            logFile << "[SendCreatePrivateRaceMessage] PRIVATE MATCH CREATED" << std::endl;
        }
    }
    
    void __fastcall Hook_SendStartPrivateRaceMessage(int param_1, void* edx) {
        // Call original FIRST to prevent crashes
        if (g_OriginalSendStartPrivateRaceMessage) {
            g_OriginalSendStartPrivateRaceMessage(param_1, edx);
        }
        
        // Then log (safe even if this fails)
        LOG_VERBOSE("");
        LOG_VERBOSE("[MP-HOOK] ===== PRIVATE RACE STARTED =====");
        LOG_VERBOSE("[MP-HOOK] Clicked START in private match lobby!");
        LOG_VERBOSE("[MP-HOOK] param_1: 0x" << std::hex << param_1);
        LOG_VERBOSE("");
        
        std::ofstream logFile("F:/mp_session_log.txt", std::ios::app);
        if (logFile.is_open()) {
            logFile << "[SendStartPrivateRaceMessage] PRIVATE RACE STARTED" << std::endl;
        }
    }
    
    void __fastcall Hook_start_matchmaking(void* thisPtr, void* edx, unsigned int param_1, int* param_2) {
        // Call original FIRST to prevent crashes
        if (g_Original_start_matchmaking) {
            g_Original_start_matchmaking(thisPtr, edx, param_1, param_2);
        }
        
        // Then log (safe even if this fails)
        LOG_VERBOSE("");
        LOG_VERBOSE("[MP-HOOK] ===== MATCHMAKING STARTED =====");
        LOG_VERBOSE("[MP-HOOK] thisPtr: 0x" << std::hex << (uintptr_t)thisPtr);
        LOG_VERBOSE("[MP-HOOK] param_1: " << std::dec << param_1);
        LOG_VERBOSE("");
        
        std::ofstream logFile("F:/mp_session_log.txt", std::ios::app);
        if (logFile.is_open()) {
            logFile << "[start_matchmaking] MATCHMAKING STARTED" << std::endl;
        }
    }
    
    void __fastcall Hook_HandleMultiplayerLobbyStart(void* thisPtr, void* edx, char param_1) {
        // Call original FIRST to prevent crashes  
        if (g_OriginalHandleMultiplayerLobbyStart) {
            g_OriginalHandleMultiplayerLobbyStart(thisPtr, edx, param_1);
        }
        
        // Then log (safe even if this fails)
        LOG_VERBOSE("");
        LOG_VERBOSE("[MP-HOOK] ===== MULTIPLAYER LOBBY EVENT =====");
        LOG_VERBOSE("[MP-HOOK] thisPtr: 0x" << std::hex << (uintptr_t)thisPtr);
        LOG_VERBOSE("[MP-HOOK] param_1 (mode/state): " << std::dec << (int)param_1);
        LOG_VERBOSE("");
        
        std::ofstream logFile("F:/mp_session_log.txt", std::ios::app);
        if (logFile.is_open()) {
            logFile << "[HandleMultiplayerLobbyStart] Mode/State: " << (int)param_1 << std::endl;
        }
    }
    
    void __fastcall Hook_BroadcastGameStartToPlayers(int param_1, void* edx) {
        // Call original FIRST to prevent crashes  
        if (g_OriginalBroadcastGameStartToPlayers) {
            g_OriginalBroadcastGameStartToPlayers(param_1, edx);
        }
        
        // Then log (safe even if this fails)
        LOG_VERBOSE("");
        LOG_VERBOSE("[MP-HOOK] ===== RACE IS LAUNCHING! =====");
        LOG_VERBOSE("[MP-HOOK] BroadcastGameStartToPlayers called!");
        LOG_VERBOSE("[MP-HOOK] param_1 (game state ptr): 0x" << std::hex << param_1);
        LOG_VERBOSE("");
        
        std::ofstream logFile("F:/mp_session_log.txt", std::ios::app);
        if (logFile.is_open()) {
            logFile << "[BroadcastGameStartToPlayers] RACE LAUNCH!" << std::endl;
        }
    }
    
    void __fastcall Hook_SendMoveToLiveLobbyMessage(int param_1, void* edx) {
        // Call original FIRST to prevent crashes  
        if (g_OriginalSendMoveToLiveLobbyMessage) {
            g_OriginalSendMoveToLiveLobbyMessage(param_1, edx);
        }
        
        // Then log
        LOG_VERBOSE("");
        LOG_VERBOSE("[MP-HOOK] ===== MOVING TO ONLINE LOBBY =====");
        LOG_VERBOSE("[MP-HOOK] User clicked Private Match (online)!");
        LOG_VERBOSE("[MP-HOOK] param_1: 0x" << std::hex << param_1);
        LOG_VERBOSE("");
        
        std::ofstream logFile("F:/mp_session_log.txt", std::ios::app);
        if (logFile.is_open()) {
            logFile << "[SendMoveToLiveLobbyMessage] ONLINE LOBBY" << std::endl;
        }
    }
    
    void __cdecl Hook_MoveToLocalMultiplayerLobby(void) {
        // Call original FIRST to prevent crashes  
        if (g_OriginalMoveToLocalMultiplayerLobby) {
            g_OriginalMoveToLocalMultiplayerLobby();
        }
        
        // Then log
        LOG_VERBOSE("");
        LOG_VERBOSE("[MP-HOOK] ===== MOVING TO LOCAL LOBBY =====");
        LOG_VERBOSE("[MP-HOOK] User selected local/splitscreen multiplayer!");
        LOG_VERBOSE("");
        
        std::ofstream logFile("F:/mp_session_log.txt", std::ios::app);
        if (logFile.is_open()) {
            logFile << "[MoveToLocalMultiplayerLobby] LOCAL LOBBY" << std::endl;
        }
    }
    
    void __fastcall Hook_SetMultiplayerMode(void* thisPtr, void* edx, char mode) {
        // Log IMMEDIATELY to see if hook is being called
        static int callCount = 0;
        callCount++;
        
        // Try to log to console first (this should always work)
        LOG_VERBOSE("[MP-HOOK] SetMultiplayerMode called! Count: " << callCount << ", mode: " << (int)mode);
        
        // Call original FIRST to be safe
        if (g_OriginalSetMultiplayerMode) {
            g_OriginalSetMultiplayerMode(thisPtr, edx, mode);
            LOG_VERBOSE("[MP-HOOK] Original function called successfully");
        } else {
            LOG_ERROR("[MP-HOOK] Original function pointer is NULL!");
        }
        
        // Then try to log to file (but don't crash if this fails)
        try {
            const char* modeStr = "UNKNOWN";
            const char* modeDesc = "";
            switch (mode) {
                case 0: 
                    modeStr = "ONLINE"; 
                    modeDesc = "(Online multiplayer)";
                    break;
                case 1: 
                    modeStr = "LOCAL/SPLITSCREEN"; 
                    modeDesc = "(Local/splitscreen multiplayer)";
                    break;
                default: 
                    modeStr = "CUSTOM"; 
                    modeDesc = "(Unknown mode)";
                    break;
            }
            
            LOG_VERBOSE("[MP-HOOK] Mode: " << modeStr << " " << modeDesc);
            
            // Simple logging only - no complex operations
            std::ofstream logFile("F:/mp_session_log.txt", std::ios::app);
            if (logFile.is_open()) {
                logFile << "[SetMultiplayerMode] " << modeStr << " (" << (int)mode << ")" << std::endl;
                logFile.close();
                LOG_VERBOSE("[MP-HOOK] Logged to file successfully");
            } else {
                LOG_ERROR("[MP-HOOK] Failed to open log file!");
            }
        }
        catch (...) {
            LOG_ERROR("[MP-HOOK] Exception in logging code!");
        }
        
        LOG_VERBOSE("[MP-HOOK] SetMultiplayerMode hook complete");
    }
    
    void __fastcall Hook_NotifyNetworkEvent(void* thisPtr, void* edx, uint32_t eventType, void* eventData) {
        LogSessionEvent("NotifyNetworkEvent",
            "Type: 0x" + std::to_string(eventType) + 
            " Data: 0x" + std::to_string((uintptr_t)eventData));
        
        // Call original
        g_OriginalNotifyNetworkEvent(thisPtr, edx, eventType, eventData);
    }
    
    // Hook for SetMultiplayerJoinMode - called when clicking buttons in multiplayer menu
    // Mode values:
    //   0 = Ranked Match
    //   2 = Private Match (host)
    //   3 = Private Match (Spectator Mode)
    void __fastcall Hook_SetMultiplayerJoinMode(void* thisPtr, void* edx, int mode, char param_2) {
        // Call original FIRST to prevent crashes
        if (g_OriginalSetMultiplayerJoinMode) {
            g_OriginalSetMultiplayerJoinMode(thisPtr, edx, mode, param_2);
        }
        
        // Determine mode string
        const char* modeStr = "UNKNOWN";
        switch (mode) {
            case 0:
                modeStr = "RANKED MATCH";
                break;
            case 2:
                modeStr = "PRIVATE MATCH (Host)";
                break;
            case 3:
                modeStr = "PRIVATE MATCH (Spectator Mode)";
                break;
            default:
                modeStr = "UNKNOWN MODE";
                break;
        }
        
        LOG_VERBOSE("");
        LOG_VERBOSE("[MP-HOOK] ===== SetMultiplayerJoinMode CALLED =====");
        LOG_VERBOSE("[MP-HOOK] Mode: " << mode << " = " << modeStr);
        LOG_VERBOSE("[MP-HOOK] param_2: " << (int)param_2);
        LOG_VERBOSE("[MP-HOOK] thisPtr: 0x" << std::hex << (uintptr_t)thisPtr);
        LOG_VERBOSE("");
        
        std::ofstream logFile("F:/mp_session_log.txt", std::ios::app);
        if (logFile.is_open()) {
            logFile << "[SetMultiplayerJoinMode] Mode: " << mode << " = " << modeStr << ", param_2: " << (int)param_2 << std::endl;
            logFile.close();
        }
    }
    
    // Hook for StartRace - the actual race launch function
    void __fastcall Hook_StartRace(int* param_1, void* edx) {
        LOG_VERBOSE("");
        LOG_VERBOSE("[MP-HOOK] ========================================");
        LOG_VERBOSE("[MP-HOOK] ===== StartRace CALLED - RACE LAUNCHING! =====");
        LOG_VERBOSE("[MP-HOOK] ========================================");
        LOG_VERBOSE("[MP-HOOK] param_1 (game session ptr): 0x" << std::hex << (uintptr_t)param_1);
        
        // Try to read some info from the session structure
        if (param_1) {
            LOG_VERBOSE("[MP-HOOK] Session data[0]: 0x" << std::hex << param_1[0]);
            LOG_VERBOSE("[MP-HOOK] Session data[1]: 0x" << std::hex << param_1[1]);
            LOG_VERBOSE("[MP-HOOK] Session data[2]: 0x" << std::hex << param_1[2]);
        }
        LOG_VERBOSE("");
        
        std::ofstream logFile("F:/mp_session_log.txt", std::ios::app);
        if (logFile.is_open()) {
            logFile << "[StartRace] RACE LAUNCHING! param_1: 0x" << std::hex << (uintptr_t)param_1 << std::endl;
            logFile.close();
        }
        
        // Call original
        if (g_OriginalStartRace) {
            g_OriginalStartRace(param_1, edx);
        }
    }
    
    // Hook for StartGameOrReturnToLobby - called right before race loads
    void __fastcall Hook_StartGameOrReturnToLobby(void* thisPtr, void* edx, uint32_t param_1) {
        LOG_VERBOSE("");
        LOG_VERBOSE("[MP-HOOK] ========================================");
        LOG_VERBOSE("[MP-HOOK] ===== StartGameOrReturnToLobby CALLED =====");
        LOG_VERBOSE("[MP-HOOK] ===== MULTIPLAYER RACE LOADING! =====");
        LOG_VERBOSE("[MP-HOOK] ========================================");
        LOG_VERBOSE("[MP-HOOK] thisPtr: 0x" << std::hex << (uintptr_t)thisPtr);
        LOG_VERBOSE("[MP-HOOK] param_1: 0x" << std::hex << param_1);
        LOG_VERBOSE("");
        
        std::ofstream logFile("F:/mp_session_log.txt", std::ios::app);
        if (logFile.is_open()) {
            logFile << "[StartGameOrReturnToLobby] MULTIPLAYER RACE LOADING!" << std::endl;
            logFile.close();
        }
        
        // Call original
        if (g_OriginalStartGameOrReturnToLobby) {
            g_OriginalStartGameOrReturnToLobby(thisPtr, edx, param_1);
        }
    }
    
    // Hook for handle_game_start_by_mode - handles all game starts
    void __cdecl Hook_handle_game_start_by_mode(char param_1) {
        LOG_VERBOSE("");
        LOG_VERBOSE("[MP-HOOK] ========================================");
        LOG_VERBOSE("[MP-HOOK] ===== handle_game_start_by_mode CALLED =====");
        LOG_VERBOSE("[MP-HOOK] param_1 (from_invite): " << (int)param_1);
        LOG_VERBOSE("[MP-HOOK] ========================================");
        LOG_VERBOSE("");
        
        std::ofstream logFile("F:/mp_session_log.txt", std::ios::app);
        if (logFile.is_open()) {
            logFile << "[handle_game_start_by_mode] from_invite: " << (int)param_1 << std::endl;
            logFile.close();
        }
        
        // Call original
        if (g_Original_handle_game_start_by_mode) {
            g_Original_handle_game_start_by_mode(param_1);
        }
    }
    
    // Hook for LoadAndStartRace - THIS SHOULD BE THE ONE!
    char __fastcall Hook_LoadAndStartRace(void* thisPtr, void* edx, int* param_1, int param_2, int* param_3, uint32_t param_4, char param_5) {
        LOG_VERBOSE("");
        LOG_VERBOSE("[MP-HOOK] ================================================");
        LOG_VERBOSE("[MP-HOOK] ===== LoadAndStartRace CALLED - RACE STARTING! =====");
        LOG_VERBOSE("[MP-HOOK] ================================================");
        LOG_VERBOSE("[MP-HOOK] thisPtr: 0x" << std::hex << (uintptr_t)thisPtr);
        LOG_VERBOSE("[MP-HOOK] param_2: " << std::dec << param_2);
        LOG_VERBOSE("[MP-HOOK] param_5: " << (int)param_5);
        LOG_VERBOSE("");
        
        std::ofstream logFile("F:/mp_session_log.txt", std::ios::app);
        if (logFile.is_open()) {
            logFile << "[LoadAndStartRace] RACE STARTING! param_2=" << param_2 << std::endl;
            logFile.close();
        }
        
        // Call original
        if (g_OriginalLoadAndStartRace) {
            return g_OriginalLoadAndStartRace(thisPtr, edx, param_1, param_2, param_3, param_4, param_5);
        }
        return 0;
    }
    
    // Hook for StartMultiplayerSession
    void __fastcall Hook_StartMultiplayerSession(void* param_1, void* edx) {
        static int callCount = 0;
        callCount++;
        
        LOG_VERBOSE("");
        LOG_VERBOSE("[MP-HOOK] ========================================");
        LOG_VERBOSE("[MP-HOOK] ===== StartMultiplayerSession CALLED =====");
        LOG_VERBOSE("[MP-HOOK] Call #" << callCount);
        LOG_VERBOSE("[MP-HOOK] param_1: 0x" << std::hex << (uintptr_t)param_1);
        
        // Try to read the join mode from param_1 structure
        // Based on decompilation, param_1+4 might contain player/game state
        if (param_1) {
            int data[3];
            if (TryReadInt(param_1, &data[0])) {
                LOG_VERBOSE("[MP-HOOK] param_1[0]: 0x" << std::hex << data[0]);
            }
            if (TryReadInt((void*)((uintptr_t)param_1 + 4), &data[1])) {
                LOG_VERBOSE("[MP-HOOK] param_1[1]: 0x" << std::hex << data[1]);
                
                // param_1[1] points to the multiplayer manager - try to dump its structure
                void* manager = (void*)data[1];
                LOG_VERBOSE("[MP-HOOK] === Dumping Manager Structure at 0x" << std::hex << (uintptr_t)manager << " ===");
                
                // Dump some key offsets we know about from the decompilation
                uint32_t* mgr = (uint32_t*)manager;
                unsigned char buffer[4];
                
                // Offset 0x234 = join mode (from SetMultiplayerJoinMode)
                if (TryReadMemory((DWORD_PTR)manager + 0x234, buffer, 4)) {
                    uint32_t joinMode = *(uint32_t*)buffer;
                    LOG_VERBOSE("[MP-HOOK] Manager+0x234 (join mode): " << joinMode);
                }
                
                // Try to find any session ID-looking values (dump more of the structure)
                LOG_VERBOSE("[MP-HOOK] Manager memory dump:");
                for (int offset = 0; offset < 0x300; offset += 0x10) {
                    if (TryReadMemory((DWORD_PTR)manager + offset, buffer, 4)) {
                        uint32_t val = *(uint32_t*)buffer;
                        if (val != 0) {  // Only log non-zero values
                            LOG_VERBOSE("[MP-HOOK]   +0x" << std::hex << offset << ": 0x" << val);
                        }
                    }
                }
                
                // Check for network session manager pointer at known offsets
                // From ConstructNetworkSessionManager, there's a session structure
                LOG_VERBOSE("[MP-HOOK] === Checking for Network Session Data ===");
                
                // Try offset 0x1A4 which might point to network session data
                unsigned char ptrBuf[4];
                if (TryReadMemory((DWORD_PTR)manager + 0x1A4, ptrBuf, 4)) {
                    void* netSessionPtr = *(void**)ptrBuf;
                    LOG_VERBOSE("[MP-HOOK] Manager+0x1A4 (possible net session ptr): 0x" << std::hex << (uintptr_t)netSessionPtr);
                    
                    // If this pointer is valid, check inside it for session ID
                    if (netSessionPtr && (uintptr_t)netSessionPtr > 0x10000) {
                        LOG_VERBOSE("[MP-HOOK] === NetSession Structure Dump ===");
                        // Dump more offsets to find session ID
                        for (int nsOffset = 0; nsOffset <= 0x200; nsOffset += 0x10) {
                            if (TryReadMemory((DWORD_PTR)netSessionPtr + nsOffset, buffer, 4)) {
                                uint32_t val = *(uint32_t*)buffer;
                                if (val != 0) {
                                    LOG_VERBOSE("[MP-HOOK] NetSession+0x" << std::hex << nsOffset << ": 0x" << val);
                                    
                                    // Try to interpret as ASCII string for some values
                                    if (val > 0x20202020 && val < 0x7F7F7F7F) {
                                        char str[5] = {0};
                                        memcpy(str, &val, 4);
                                        LOG_VERBOSE("[MP-HOOK]   (as ASCII: '" << str << "')");
                                    }
                                }
                            }
                        }
                        
                        // Highlight the most interesting offsets
                        LOG_VERBOSE("[MP-HOOK] === Key Offsets (compare these between matches!) ===");
                        if (TryReadMemory((DWORD_PTR)netSessionPtr + 0x10, buffer, 4)) {
                            uint32_t val = *(uint32_t*)buffer;
                            LOG_VERBOSE("[MP-HOOK] *** NetSession+0x10: 0x" << std::hex << val);
                        }
                        if (TryReadMemory((DWORD_PTR)netSessionPtr + 0x20, buffer, 4)) {
                            uint32_t val = *(uint32_t*)buffer;
                            LOG_VERBOSE("[MP-HOOK] *** NetSession+0x20: 0x" << std::hex << val);
                        }
                        if (TryReadMemory((DWORD_PTR)netSessionPtr + 0x120, buffer, 4)) {
                            uint32_t val = *(uint32_t*)buffer;
                            LOG_VERBOSE("[MP-HOOK] *** NetSession+0x120: 0x" << std::hex << val);
                        }
                    }
                }
            }
            if (TryReadInt((void*)((uintptr_t)param_1 + 8), &data[2])) {
                LOG_VERBOSE("[MP-HOOK] param_1[2]: 0x" << std::hex << data[2]);
            }
        }
        
        if (callCount == 1) {
            LOG_VERBOSE("[MP-HOOK] >>> First call - Entering private match lobby");
        } else if (callCount == 2) {
            LOG_VERBOSE("[MP-HOOK] ================================================");
            LOG_VERBOSE("[MP-HOOK] >>> Second call - RACE IS STARTING NOW! <<<");
            LOG_VERBOSE("[MP-HOOK] ================================================");
        } else {
            LOG_VERBOSE("[MP-HOOK] >>> Call #" << callCount);
        }
        
        LOG_VERBOSE("[MP-HOOK] ========================================");
        LOG_VERBOSE("");
        
        std::ofstream logFile("F:/mp_session_log.txt", std::ios::app);
        if (logFile.is_open()) {
            logFile << "[StartMultiplayerSession] Call #" << callCount;
            if (callCount == 2) {
                logFile << " - RACE STARTING!";
            }
            logFile << std::endl;
            logFile.close();
        }
        
        // Call original
        if (g_OriginalStartMultiplayerSession) {
            g_OriginalStartMultiplayerSession(param_1, edx);
        }
    }
    
    // Hook for HandleSessionCreateEvent - captures session IDs when sessions are created
    void __fastcall Hook_HandleSessionCreateEvent(void* thisPtr, void* edx, uint32_t* param_1, uint32_t param_2) {
        LOG_VERBOSE("");
        LOG_VERBOSE("[SESSION-CREATE] !!!! HOOK CALLED !!!!");
        LOG_VERBOSE("[SESSION-CREATE] thisPtr: 0x" << std::hex << (uintptr_t)thisPtr);
        LOG_VERBOSE("[SESSION-CREATE] param_1: 0x" << std::hex << (uintptr_t)param_1);
        LOG_VERBOSE("[SESSION-CREATE] param_2: 0x" << std::hex << param_2);
        
        uint32_t* event_data = (uint32_t*)param_2;
        
        // Safety check - make sure param_2 is a valid pointer
        if (param_2 < 0x10000) {
            LOG_ERROR("[SESSION-CREATE] param_2 looks like a value, not a pointer!");
            LOG_VERBOSE("[SESSION-CREATE] Trying to read session ID as direct value: 0x" << std::hex << param_2);
            
            // Store it anyway in case this is actually the session ID
            g_CurrentSession.sessionId = param_2;
            g_InSession = true;
            
            // Call original and return
            if (g_OriginalHandleSessionCreateEvent) {
                g_OriginalHandleSessionCreateEvent(thisPtr, edx, param_1, param_2);
            }
            return;
        }
        
        uint32_t session_id = 0;
        
        // Try to read session ID at offset 0x20
        unsigned char buffer[4];
        if (TryReadMemory((DWORD_PTR)event_data + 0x20, buffer, 4)) {
            session_id = *(uint32_t*)buffer;
        } else {
            LOG_ERROR("[SESSION-CREATE] Could not read memory at event_data + 0x20");
            // Try reading event_data as array
            int tempId;
            if (TryReadInt((void*)event_data, &tempId)) {
                LOG_VERBOSE("[SESSION-CREATE] event_data[0]: 0x" << std::hex << tempId);
            }
        }
        
        LOG_VERBOSE("");
        LOG_VERBOSE("[SESSION-CREATE] ===============================");
        LOG_VERBOSE("[SESSION-CREATE] Session ID: 0x" << std::hex << session_id);
        LOG_VERBOSE("[SESSION-CREATE] Event data at: 0x" << std::hex << (uintptr_t)event_data);
        LOG_VERBOSE("[SESSION-CREATE] +0x08: 0x" << std::hex << event_data[0x08/4]);
        LOG_VERBOSE("[SESSION-CREATE] +0x20 (SESSION ID): 0x" << std::hex << session_id);
        LOG_VERBOSE("[SESSION-CREATE] +0x28 (player name): 0x" << std::hex << event_data[0x28/4]);
        LOG_VERBOSE("[SESSION-CREATE] +0x2C: 0x" << std::hex << event_data[0x2C/4]);
        LOG_VERBOSE("[SESSION-CREATE] +0x30: 0x" << std::hex << event_data[0x30/4]);
        LOG_VERBOSE("[SESSION-CREATE] ===============================");
        LOG_VERBOSE("");
        
        // Store session ID in current session
        g_CurrentSession.sessionId = session_id;
        g_InSession = true;
        
        std::ofstream logFile("F:/mp_session_log.txt", std::ios::app);
        if (logFile.is_open()) {
            logFile << "[HandleSessionCreateEvent] SESSION ID: 0x" << std::hex << session_id << std::endl;
            logFile.close();
        }
        
        // Call original
        if (g_OriginalHandleSessionCreateEvent) {
            g_OriginalHandleSessionCreateEvent(thisPtr, edx, param_1, param_2);
        }
    }
    
    // Hook for HandleSessionJoinRequest - captures session IDs when joining
    void* __fastcall Hook_HandleSessionJoinRequest(void* thisPtr, void* edx, void* param_1, int param_2) {
        uint32_t* join_data = (uint32_t*)param_2;
        uint32_t target_session_id = join_data[0x20/4];
        
        LOG_VERBOSE("");
        LOG_VERBOSE("[SESSION-JOIN] ===============================");
        LOG_VERBOSE("[SESSION-JOIN] TARGET Session ID: 0x" << std::hex << target_session_id);
        LOG_VERBOSE("[SESSION-JOIN] Join request at: 0x" << std::hex << (uintptr_t)join_data);
        LOG_VERBOSE("[SESSION-JOIN] +0x08: 0x" << std::hex << join_data[0x08/4]);
        LOG_VERBOSE("[SESSION-JOIN] +0x20 (TARGET SESSION): 0x" << std::hex << target_session_id);
        LOG_VERBOSE("[SESSION-JOIN] +0x28 (player name): 0x" << std::hex << join_data[0x28/4]);
        LOG_VERBOSE("[SESSION-JOIN] +0x2C (player data): 0x" << std::hex << join_data[0x2C/4]);
        LOG_VERBOSE("[SESSION-JOIN] ===============================");
        LOG_VERBOSE("");
        
        std::ofstream logFile("F:/mp_session_log.txt", std::ios::app);
        if (logFile.is_open()) {
            logFile << "[HandleSessionJoinRequest] JOINING SESSION: 0x" << std::hex << target_session_id << std::endl;
            logFile.close();
        }
        
        // Call original
        if (g_OriginalHandleSessionJoinRequest) {
            return g_OriginalHandleSessionJoinRequest(thisPtr, edx, param_1, param_2);
        }
        return nullptr;
    }
    
    // Hook for HandleMultiplayerLobbyStart (the specific one from 0x009d9380)
    void __fastcall Hook_HandleMultiplayerLobbyStart2(void* thisPtr, void* edx, char param_1) {
        LOG_VERBOSE("");
        LOG_VERBOSE("[MP-HOOK] ========================================");
        LOG_VERBOSE("[MP-HOOK] ===== HandleMultiplayerLobbyStart2 CALLED =====");
        LOG_VERBOSE("[MP-HOOK] ===== MULTIPLAYER LOBBY -> RACE! =====");
        LOG_VERBOSE("[MP-HOOK] ========================================");
        LOG_VERBOSE("[MP-HOOK] thisPtr: 0x" << std::hex << (uintptr_t)thisPtr);
        LOG_VERBOSE("[MP-HOOK] param_1: " << (int)param_1);
        LOG_VERBOSE("");
        
        std::ofstream logFile("F:/mp_session_log.txt", std::ios::app);
        if (logFile.is_open()) {
            logFile << "[HandleMultiplayerLobbyStart2] LOBBY->RACE! param_1: " << (int)param_1 << std::endl;
            logFile.close();
        }
        
        // Call original
        if (g_OriginalHandleMultiplayerLobbyStart2) {
            g_OriginalHandleMultiplayerLobbyStart2(thisPtr, edx, param_1);
        }
    }
    
    // === PUBLIC API ===
    
    bool Initialize(DWORD_PTR baseAddress) {
        if (baseAddress == 0) {
            LOG_ERROR("[MP] Invalid base address!");
            return false;
        }
        
        g_BaseAddress = baseAddress;
        LOG_VERBOSE("[MP] Initializing multiplayer monitoring system...");
        LOG_VERBOSE("[MP] Base address: 0x" << std::hex << baseAddress);
        
        // Calculate addresses (RVA + base - ghidra base 0x700000)
        DWORD_PTR ghidraBase = 0x700000;
        
        // Key function addresses from Ghidra
        DWORD_PTR prepareAndLoadMultiplayerMenuAddr = baseAddress + (0x00ae9530 - ghidraBase);
        DWORD_PTR sendStartLiveQuickGameAddr = baseAddress + (0x009dbd20 - ghidraBase);
        DWORD_PTR sendCreatePrivateRaceAddr = baseAddress + (0x009dbee0 - ghidraBase);
        DWORD_PTR sendStartPrivateRaceAddr = baseAddress + (0x009dbf60 - ghidraBase);
        DWORD_PTR startMatchmakingAddr = baseAddress + (0x00f0cc20 - ghidraBase);
        DWORD_PTR handleMultiplayerLobbyStartAddr = baseAddress + (0x009d9380 - ghidraBase);
        DWORD_PTR broadcastGameStartToPlayersAddr = baseAddress + (0x00ecaf30 - ghidraBase);
        DWORD_PTR sendMoveToLiveLobbyAddr = baseAddress + (0x009dbad0 - ghidraBase);
        DWORD_PTR moveToLocalMultiplayerLobbyAddr = baseAddress + (0x00ab78e0 - ghidraBase);
        DWORD_PTR initMultiplayerSessionAddr = baseAddress + (0x9D8C90 - ghidraBase);
        DWORD_PTR createGameplaySessionAddr = baseAddress + (0x122E8F0 - ghidraBase);
        DWORD_PTR processNetworkPacketsAddr = baseAddress + (0xE711E0 - ghidraBase);
        DWORD_PTR handleNetworkPacketAddr = baseAddress + (0x86CC60 - ghidraBase);
        DWORD_PTR updateMultiplayerStateAddr = baseAddress + (0xAB9020 - ghidraBase);
        DWORD_PTR setMultiplayerModeAddr = baseAddress + (0x00abd1a0 - ghidraBase);
        DWORD_PTR notifyNetworkEventAddr = baseAddress + (0xAB8F80 - ghidraBase);
        // SetMultiplayerJoinMode - Ghidra address 0x00abbc00
        DWORD_PTR setMultiplayerJoinModeAddr = baseAddress + (0x00abbc00 - ghidraBase);
        // StartRace - Ghidra address 0x009da450 - actual race launch
        DWORD_PTR startRaceAddr = baseAddress + (0x009da450 - ghidraBase);
        
        LOG_VERBOSE("[MP] Hook addresses calculated:");
        LOG_VERBOSE("[MP]   PrepareAndLoadMultiplayerMenu: 0x" << std::hex << prepareAndLoadMultiplayerMenuAddr);
        LOG_VERBOSE("[MP]   SendStartLiveQuickGame: 0x" << std::hex << sendStartLiveQuickGameAddr);
        LOG_VERBOSE("[MP]   SendCreatePrivateRace: 0x" << std::hex << sendCreatePrivateRaceAddr);
        LOG_VERBOSE("[MP]   SendStartPrivateRace: 0x" << std::hex << sendStartPrivateRaceAddr);
        LOG_VERBOSE("[MP]   start_matchmaking: 0x" << std::hex << startMatchmakingAddr);
        LOG_VERBOSE("[MP]   HandleMultiplayerLobbyStart: 0x" << std::hex << handleMultiplayerLobbyStartAddr);
        LOG_VERBOSE("[MP]   BroadcastGameStartToPlayers: 0x" << std::hex << broadcastGameStartToPlayersAddr);
        LOG_VERBOSE("[MP]   SendMoveToLiveLobby: 0x" << std::hex << sendMoveToLiveLobbyAddr);
        LOG_VERBOSE("[MP]   MoveToLocalMultiplayerLobby: 0x" << std::hex << moveToLocalMultiplayerLobbyAddr);
        LOG_VERBOSE("[MP]   InitializeMultiplayerSession: 0x" << std::hex << initMultiplayerSessionAddr);
        LOG_VERBOSE("[MP]   CreateGameplaySession: 0x" << std::hex << createGameplaySessionAddr);
        LOG_VERBOSE("[MP]   ProcessNetworkPackets: 0x" << std::hex << processNetworkPacketsAddr);
        LOG_VERBOSE("[MP]   HandleNetworkPacket: 0x" << std::hex << handleNetworkPacketAddr);
        LOG_VERBOSE("[MP]   UpdateMultiplayerState: 0x" << std::hex << updateMultiplayerStateAddr);
        LOG_VERBOSE("[MP]   SetMultiplayerMode: 0x" << std::hex << setMultiplayerModeAddr);
        LOG_VERBOSE("[MP]   NotifyNetworkEvent: 0x" << std::hex << notifyNetworkEventAddr);
        LOG_VERBOSE("[MP]   SetMultiplayerJoinMode: 0x" << std::hex << setMultiplayerJoinModeAddr);
        LOG_VERBOSE("[MP]   StartRace: 0x" << std::hex << startRaceAddr);
        
        // ======================================================================
        // INCREMENTAL HOOK TESTING - Enable ONE at a time
        // ======================================================================
        // Instructions:
        // 1. Uncomment ONE hook block below
        // 2. Test if game loads and doesn't crash
        // 3. Try to join/create multiplayer session
        // 4. If it crashes, that hook is bad - comment it out again
        // 5. If it works, leave it enabled and try the next one
        // ======================================================================
        
        MH_STATUS status;
        int hooksEnabled = 0;
        
        // TEST 0: PrepareAndLoadMultiplayerMenu (MOST LIKELY to be called when clicking multiplayer!)
        LOG_VERBOSE("[MP] Installing PrepareAndLoadMultiplayerMenu hook...");
        status = MH_CreateHook(
            (LPVOID)prepareAndLoadMultiplayerMenuAddr,
            (LPVOID)&Hook_PrepareAndLoadMultiplayerMenu,
            (LPVOID*)&g_OriginalPrepareAndLoadMultiplayerMenu
        );
        if (status == MH_OK) {
            MH_EnableHook((LPVOID)prepareAndLoadMultiplayerMenuAddr);
            LOG_VERBOSE("[MP] ✓ PrepareAndLoadMultiplayerMenu hook installed");
            hooksEnabled++;
        } else {
            LOG_ERROR("[MP] ✗ Failed to hook PrepareAndLoadMultiplayerMenu: " << MH_StatusToString(status));
        }
        
        // NEW: Hook for Ranked/Quick Match button
        LOG_VERBOSE("[MP] Installing SendStartLiveQuickGame hook...");
        status = MH_CreateHook(
            (LPVOID)sendStartLiveQuickGameAddr,
            (LPVOID)&Hook_SendStartLiveQuickGameMessage,
            (LPVOID*)&g_OriginalSendStartLiveQuickGameMessage
        );
        if (status == MH_OK) {
            MH_EnableHook((LPVOID)sendStartLiveQuickGameAddr);
            LOG_VERBOSE("[MP] ✓ SendStartLiveQuickGame hook installed");
            hooksEnabled++;
        } else {
            LOG_ERROR("[MP] ✗ Failed to hook SendStartLiveQuickGame: " << MH_StatusToString(status));
        }
        
        // NEW: Hook for Private Match button
        LOG_VERBOSE("[MP] Installing SendCreatePrivateRace hook...");
        status = MH_CreateHook(
            (LPVOID)sendCreatePrivateRaceAddr,
            (LPVOID)&Hook_SendCreatePrivateRaceMessage,
            (LPVOID*)&g_OriginalSendCreatePrivateRaceMessage
        );
        if (status == MH_OK) {
            MH_EnableHook((LPVOID)sendCreatePrivateRaceAddr);
            LOG_VERBOSE("[MP] ✓ SendCreatePrivateRace hook installed");
            hooksEnabled++;
        } else {
            LOG_ERROR("[MP] ✗ Failed to hook SendCreatePrivateRace: " << MH_StatusToString(status));
        }
        
        // NEW: Hook for Private Match START button
        LOG_VERBOSE("[MP] Installing SendStartPrivateRace hook...");
        status = MH_CreateHook(
            (LPVOID)sendStartPrivateRaceAddr,
            (LPVOID)&Hook_SendStartPrivateRaceMessage,
            (LPVOID*)&g_OriginalSendStartPrivateRaceMessage
        );
        if (status == MH_OK) {
            MH_EnableHook((LPVOID)sendStartPrivateRaceAddr);
            LOG_VERBOSE("[MP] ✓ SendStartPrivateRace hook installed");
            hooksEnabled++;
        } else {
            LOG_ERROR("[MP] ✗ Failed to hook SendStartPrivateRace: " << MH_StatusToString(status));
        }
        
        // NEW: Hook for matchmaking system
        LOG_VERBOSE("[MP] Installing start_matchmaking hook...");
        status = MH_CreateHook(
            (LPVOID)startMatchmakingAddr,
            (LPVOID)&Hook_start_matchmaking,
            (LPVOID*)&g_Original_start_matchmaking
        );
        if (status == MH_OK) {
            MH_EnableHook((LPVOID)startMatchmakingAddr);
            LOG_VERBOSE("[MP] ✓ start_matchmaking hook installed");
            hooksEnabled++;
        } else {
            LOG_ERROR("[MP] ✗ Failed to hook start_matchmaking: " << MH_StatusToString(status));
        }
        
        // NEW: Hook for lobby creation
        LOG_VERBOSE("[MP] Installing HandleMultiplayerLobbyStart hook...");
        status = MH_CreateHook(
            (LPVOID)handleMultiplayerLobbyStartAddr,
            (LPVOID)&Hook_HandleMultiplayerLobbyStart,
            (LPVOID*)&g_OriginalHandleMultiplayerLobbyStart
        );
        if (status == MH_OK) {
            MH_EnableHook((LPVOID)handleMultiplayerLobbyStartAddr);
            LOG_VERBOSE("[MP] ✓ HandleMultiplayerLobbyStart hook installed");
            hooksEnabled++;
        } else {
            LOG_ERROR("[MP] ✗ Failed to hook HandleMultiplayerLobbyStart: " << MH_StatusToString(status));
        }
        
        // NEW: Hook for race launch (when game actually starts)
        LOG_VERBOSE("[MP] Installing BroadcastGameStartToPlayers hook...");
        status = MH_CreateHook(
            (LPVOID)broadcastGameStartToPlayersAddr,
            (LPVOID)&Hook_BroadcastGameStartToPlayers,
            (LPVOID*)&g_OriginalBroadcastGameStartToPlayers
        );
        if (status == MH_OK) {
            MH_EnableHook((LPVOID)broadcastGameStartToPlayersAddr);
            LOG_VERBOSE("[MP] ✓ BroadcastGameStartToPlayers hook installed");
            hooksEnabled++;
        } else {
            LOG_ERROR("[MP] ✗ Failed to hook BroadcastGameStartToPlayers: " << MH_StatusToString(status));
        }
        
        // NEW: Hook for moving to online/private match lobby
        LOG_VERBOSE("[MP] Installing SendMoveToLiveLobby hook...");
        status = MH_CreateHook(
            (LPVOID)sendMoveToLiveLobbyAddr,
            (LPVOID)&Hook_SendMoveToLiveLobbyMessage,
            (LPVOID*)&g_OriginalSendMoveToLiveLobbyMessage
        );
        if (status == MH_OK) {
            MH_EnableHook((LPVOID)sendMoveToLiveLobbyAddr);
            LOG_VERBOSE("[MP] ✓ SendMoveToLiveLobby hook installed");
            hooksEnabled++;
        } else {
            LOG_ERROR("[MP] ✗ Failed to hook SendMoveToLiveLobby: " << MH_StatusToString(status));
        }
        
        // NEW: Hook for moving to local/splitscreen lobby
        LOG_VERBOSE("[MP] Installing MoveToLocalMultiplayerLobby hook...");
        status = MH_CreateHook(
            (LPVOID)moveToLocalMultiplayerLobbyAddr,
            (LPVOID)&Hook_MoveToLocalMultiplayerLobby,
            (LPVOID*)&g_OriginalMoveToLocalMultiplayerLobby
        );
        if (status == MH_OK) {
            MH_EnableHook((LPVOID)moveToLocalMultiplayerLobbyAddr);
            LOG_VERBOSE("[MP] ✓ MoveToLocalMultiplayerLobby hook installed");
            hooksEnabled++;
        } else {
            LOG_ERROR("[MP] ✗ Failed to hook MoveToLocalMultiplayerLobby: " << MH_StatusToString(status));
        }
        
        // NEW: Hook for SetMultiplayerJoinMode - called when clicking Private Match buttons
        LOG_VERBOSE("[MP] Installing SetMultiplayerJoinMode hook...");
        status = MH_CreateHook(
            (LPVOID)setMultiplayerJoinModeAddr,
            (LPVOID)&Hook_SetMultiplayerJoinMode,
            (LPVOID*)&g_OriginalSetMultiplayerJoinMode
        );
        if (status == MH_OK) {
            MH_EnableHook((LPVOID)setMultiplayerJoinModeAddr);
            LOG_VERBOSE("[MP] ✓ SetMultiplayerJoinMode hook installed");
            hooksEnabled++;
        } else {
            LOG_ERROR("[MP] ✗ Failed to hook SetMultiplayerJoinMode: " << MH_StatusToString(status));
        }
        
        // NEW: Hook for StartRace - the actual race launch function
        LOG_VERBOSE("[MP] Installing StartRace hook...");
        status = MH_CreateHook(
            (LPVOID)startRaceAddr,
            (LPVOID)&Hook_StartRace,
            (LPVOID*)&g_OriginalStartRace
        );
        if (status == MH_OK) {
            MH_EnableHook((LPVOID)startRaceAddr);
            LOG_VERBOSE("[MP] ✓ StartRace hook installed");
            hooksEnabled++;
        } else {
            LOG_ERROR("[MP] ✗ Failed to hook StartRace: " << MH_StatusToString(status));
        }
        
        // NEW: LoadAndStartRace - RVA from 0x00c4c060 - THIS IS THE KEY ONE!
        DWORD_PTR loadAndStartRaceAddr = baseAddress + (0x00c4c060 - ghidraBase);
        LOG_VERBOSE("[MP] Installing LoadAndStartRace hook...");
        LOG_VERBOSE("[MP]   LoadAndStartRace: 0x" << std::hex << loadAndStartRaceAddr);
        status = MH_CreateHook(
            (LPVOID)loadAndStartRaceAddr,
            (LPVOID)&Hook_LoadAndStartRace,
            (LPVOID*)&g_OriginalLoadAndStartRace
        );
        if (status == MH_OK) {
            MH_EnableHook((LPVOID)loadAndStartRaceAddr);
            LOG_VERBOSE("[MP] ✓ LoadAndStartRace hook installed");
            hooksEnabled++;
        } else {
            LOG_ERROR("[MP] ✗ Failed to hook LoadAndStartRace: " << MH_StatusToString(status));
        }
        
        // NEW: StartMultiplayerSession - RVA from 0x00aea5e0
        // This is called when clicking Private Match button
        DWORD_PTR startMultiplayerSessionAddr = baseAddress + (0x00aea5e0 - ghidraBase);
        LOG_VERBOSE("[MP] Installing StartMultiplayerSession hook...");
        LOG_VERBOSE("[MP]   StartMultiplayerSession: 0x" << std::hex << startMultiplayerSessionAddr);
        status = MH_CreateHook(
            (LPVOID)startMultiplayerSessionAddr,
            (LPVOID)&Hook_StartMultiplayerSession,
            (LPVOID*)&g_OriginalStartMultiplayerSession
        );
        if (status == MH_OK) {
            MH_EnableHook((LPVOID)startMultiplayerSessionAddr);
            LOG_VERBOSE("[MP] ✓ StartMultiplayerSession hook installed");
            hooksEnabled++;
        } else {
            LOG_ERROR("[MP] ✗ Failed to hook StartMultiplayerSession: " << MH_StatusToString(status));
        }
        
        // NEW: HandleSessionCreateEvent - RVA from 0x0126db50
        // This is called when a session is created and contains the session ID!
        DWORD_PTR handleSessionCreateEventAddr = baseAddress + (0x0126db50 - ghidraBase);
        LOG_VERBOSE("[MP] Installing HandleSessionCreateEvent hook...");
        LOG_VERBOSE("[MP]   HandleSessionCreateEvent: 0x" << std::hex << handleSessionCreateEventAddr);
        
        // Verify address is valid before hooking
        unsigned char testBytes[16];
        if (TryReadMemory(handleSessionCreateEventAddr, testBytes, 16)) {
            LOG_VERBOSE("[MP]   First bytes: " << std::hex 
                << (int)testBytes[0] << " " << (int)testBytes[1] << " " 
                << (int)testBytes[2] << " " << (int)testBytes[3]);
            
            status = MH_CreateHook(
                (LPVOID)handleSessionCreateEventAddr,
                (LPVOID)&Hook_HandleSessionCreateEvent,
                (LPVOID*)&g_OriginalHandleSessionCreateEvent
            );
            if (status == MH_OK) {
                MH_EnableHook((LPVOID)handleSessionCreateEventAddr);
                LOG_VERBOSE("[MP] ✓ HandleSessionCreateEvent hook installed");
                hooksEnabled++;
            } else {
                LOG_ERROR("[MP] ✗ Failed to hook HandleSessionCreateEvent: " << MH_StatusToString(status));
            }
        } else {
            LOG_ERROR("[MP] ✗ HandleSessionCreateEvent address is invalid - cannot read memory");
        }
        
        // NEW: HandleSessionJoinRequest - RVA from 0x0126dc30
        // This is called when joining a session and contains the target session ID!
        DWORD_PTR handleSessionJoinRequestAddr = baseAddress + (0x0126dc30 - ghidraBase);
        LOG_VERBOSE("[MP] Installing HandleSessionJoinRequest hook...");
        LOG_VERBOSE("[MP]   HandleSessionJoinRequest: 0x" << std::hex << handleSessionJoinRequestAddr);
        status = MH_CreateHook(
            (LPVOID)handleSessionJoinRequestAddr,
            (LPVOID)&Hook_HandleSessionJoinRequest,
            (LPVOID*)&g_OriginalHandleSessionJoinRequest
        );
        if (status == MH_OK) {
            MH_EnableHook((LPVOID)handleSessionJoinRequestAddr);
            LOG_VERBOSE("[MP] ✓ HandleSessionJoinRequest hook installed");
            hooksEnabled++;
        } else {
            LOG_ERROR("[MP] ✗ Failed to hook HandleSessionJoinRequest: " << MH_StatusToString(status));
        }
        
        // TEST 2: SetMultiplayerMode (ADDRESS VERIFIED - ENABLING HOOK)
        LOG_VERBOSE("[MP] SetMultiplayerMode address: 0x" << std::hex << setMultiplayerModeAddr);
        LOG_VERBOSE("[MP] Checking if address is valid...");
        
        // Read first few bytes at the address to verify it's valid code
        unsigned char bytes[4] = {0};
        if (TryReadMemory(setMultiplayerModeAddr, bytes, 4)) {
            LOG_VERBOSE("[MP] First bytes at SetMultiplayerMode: " 
                << std::hex << (int)bytes[0] << " " 
                << (int)bytes[1] << " " 
                << (int)bytes[2] << " " 
                << (int)bytes[3]);
            
            // Address is valid - install hook!
            LOG_VERBOSE("[MP] Installing SetMultiplayerMode hook...");
            status = MH_CreateHook(
                (LPVOID)setMultiplayerModeAddr,
                (LPVOID)&Hook_SetMultiplayerMode,
                (LPVOID*)&g_OriginalSetMultiplayerMode
            );
            if (status == MH_OK) {
                MH_EnableHook((LPVOID)setMultiplayerModeAddr);
                LOG_VERBOSE("[MP] ✓ SetMultiplayerMode hook installed");
                hooksEnabled++;
            } else {
                LOG_ERROR("[MP] ✗ Failed to hook SetMultiplayerMode: " << MH_StatusToString(status));
            }
        } else {
            LOG_ERROR("[MP] ✗ SetMultiplayerMode address is INVALID - cannot read memory!");
            LOG_ERROR("[MP] This means the address calculation is wrong.");
        }
        
        LOG_VERBOSE("[MP] === Multiplayer monitoring initialized ===");
        LOG_VERBOSE("[MP] Hooks enabled: " << hooksEnabled << "/15");
        if (hooksEnabled > 0) {
            LOG_INFO("[MP] Session events will be logged to: F:/mp_session_log.txt");
            LOG_INFO("[MP] Press M to save all captured data");
        } else {
            LOG_WARNING("[MP] No hooks enabled - running in passive mode");
            LOG_WARNING("[MP] Uncomment hooks in multiplayer.cpp to enable monitoring");
        }
        
        return true;
    }
    
    void Shutdown() {
        LOG_VERBOSE("[MP] Shutting down multiplayer monitoring...");
        
        // Save all logs before shutdown
        SaveLogs();
        
        // Disable all hooks
        if (g_OriginalMultiplayerServiceConstructor) {
            MH_DisableHook(MH_ALL_HOOKS);
        }
        
        LOG_VERBOSE("[MP] Multiplayer monitoring shutdown complete");
    }
    
    void SetPacketLoggingEnabled(bool enabled) {
        g_PacketLoggingEnabled = enabled;
        LOG_VERBOSE("[MP] Packet logging " << (enabled ? "ENABLED" : "DISABLED"));
    }
    
    void SetSessionLoggingEnabled(bool enabled) {
        g_SessionLoggingEnabled = enabled;
        LOG_VERBOSE("[MP] Session logging " << (enabled ? "ENABLED" : "DISABLED"));
    }
    
    SessionInfo GetCurrentSession() {
        return g_CurrentSession;
    }
    
    bool IsInSession() {
        return g_InSession;
    }
    
    void SaveLogs() {
        LOG_VERBOSE("[MP] Saving all captured data...");
        
        // Save session history
        {
            std::ofstream sessionFile("F:/mp_sessions.csv");
            if (sessionFile.is_open()) {
                sessionFile << "SessionID,Timestamp,IsHost,PlayerCount,MaxPlayers,GameMode\n";
                
                std::lock_guard<std::mutex> lock(g_LogMutex);
                for (const auto& session : g_SessionHistory) {
                    sessionFile << session.sessionId << ","
                               << session.timestamp << ","
                               << (session.isHost ? "1" : "0") << ","
                               << session.playerCount << ","
                               << session.maxPlayers << ","
                               << session.gameMode << "\n";
                }
                sessionFile.close();
                LOG_VERBOSE("[MP] Saved " << g_SessionHistory.size() << " sessions to mp_sessions.csv");
            }
        }
        
        // Save packet log
        {
            std::ofstream packetFile("F:/mp_packets.csv");
            if (packetFile.is_open()) {
                packetFile << "PacketType,Size,SourcePlayer,DestPlayer,Timestamp\n";
                
                std::lock_guard<std::mutex> lock(g_LogMutex);
                for (const auto& packet : g_PacketLog) {
                    packetFile << std::hex << packet.packetType << std::dec << ","
                              << packet.packetSize << ","
                              << packet.sourcePlayerId << ","
                              << packet.destPlayerId << ","
                              << packet.timestamp << "\n";
                }
                packetFile.close();
                LOG_VERBOSE("[MP] Saved " << g_PacketLog.size() << " packets to mp_packets.csv");
            }
        }
        
        // Save statistics
        {
            std::ofstream statsFile("F:/mp_stats.txt");
            if (statsFile.is_open()) {
                std::lock_guard<std::mutex> lock(g_StatsMutex);
                statsFile << "=== MULTIPLAYER STATISTICS ===\n";
                statsFile << "Total Sessions: " << g_Stats.sessionCount << "\n";
                statsFile << "Total Packets Received: " << g_Stats.totalPacketsReceived << "\n";
                statsFile << "Total Packets Sent: " << g_Stats.totalPacketsSent << "\n";
                statsFile << "Total Bytes Received: " << g_Stats.totalBytesReceived << "\n";
                statsFile << "Total Bytes Sent: " << g_Stats.totalBytesSent << "\n";
                statsFile << "Player State Updates: " << g_Stats.playerStateUpdates << "\n";
                statsFile.close();
                LOG_VERBOSE("[MP] Saved statistics to mp_stats.txt");
            }
        }
        
        LOG_VERBOSE("[MP] All data saved successfully!");
    }
    
    void CaptureCurrentState() {
        LOG_VERBOSE("[MP] Capturing current multiplayer state...");
        
        if (g_InSession) {
            LOG_VERBOSE("[MP] Current Session:");
            LOG_VERBOSE("[MP]   ID: " << g_CurrentSession.sessionId);
            LOG_VERBOSE("[MP]   Mode: " << g_CurrentSession.gameMode);
            LOG_VERBOSE("[MP]   Is Host: " << (g_CurrentSession.isHost ? "Yes" : "No"));
            LOG_VERBOSE("[MP]   Players: " << g_CurrentSession.playerCount << "/" << g_CurrentSession.maxPlayers);
        } else {
            LOG_VERBOSE("[MP] Not currently in a session");
        }
        
        Stats stats = GetStats();
        LOG_VERBOSE("[MP] Network Statistics:");
        LOG_VERBOSE("[MP]   Packets RX: " << stats.totalPacketsReceived);
        LOG_VERBOSE("[MP]   Packets TX: " << stats.totalPacketsSent);
        LOG_VERBOSE("[MP]   Bytes RX: " << stats.totalBytesReceived);
        LOG_VERBOSE("[MP]   Bytes TX: " << stats.totalBytesSent);
    }
    
    Stats GetStats() {
        std::lock_guard<std::mutex> lock(g_StatsMutex);
        return g_Stats;
    }
    
    void CheckHotkey() {
        // Use keybindings system for Save all logs action
        if (Keybindings::IsActionPressed(Keybindings::Action::SaveMultiplayerLogs)) {
            std::string keyName = Keybindings::GetKeyName(Keybindings::GetKey(Keybindings::Action::SaveMultiplayerLogs));
            LOG_VERBOSE("");
            LOG_VERBOSE("[" << keyName << "] === SAVING MULTIPLAYER DATA ===");
            SaveLogs();
            CaptureCurrentState();
            LOG_VERBOSE("");
        }
        
        // Use keybindings system for Capture current state action
        if (Keybindings::IsActionPressed(Keybindings::Action::CaptureSessionState)) {
            LOG_VERBOSE("");
            CaptureCurrentState();
            LOG_VERBOSE("");
        }
    }
    
    // === PHASE 2+ STUB IMPLEMENTATIONS ===
    // These functions are called by devMenu but will be implemented in Phase 2+
    // For now they just log that they were called
    
    void SendVoteTrack(uint32_t trackId) {
        LOG_VERBOSE("[MP-STUB] SendVoteTrack called with trackId: " << trackId);
        LOG_WARNING("[MP-STUB] This function will be implemented in Phase 2+");
    }
    
    void SendSelectBike(uint32_t bikeId) {
        LOG_VERBOSE("[MP-STUB] SendSelectBike called with bikeId: " << bikeId);
        LOG_WARNING("[MP-STUB] This function will be implemented in Phase 2+");
    }
    
    void SendContinueToNextHeat() {
        LOG_VERBOSE("[MP-STUB] SendContinueToNextHeat called");
        LOG_WARNING("[MP-STUB] This function will be implemented in Phase 2+");
    }
    
    void SendContinueToVote() {
        LOG_VERBOSE("[MP-STUB] SendContinueToVote called");
        LOG_WARNING("[MP-STUB] This function will be implemented in Phase 2+");
    }
    
    void SendReplayMultiplayer() {
        LOG_VERBOSE("[MP-STUB] SendReplayMultiplayer called");
        LOG_WARNING("[MP-STUB] This function will be implemented in Phase 2+");
    }
    
    void SendInitLive() {
        LOG_VERBOSE("[MP-STUB] SendInitLive called");
        LOG_WARNING("[MP-STUB] This function will be implemented in Phase 2+");
    }
    
    void SendUninitLive() {
        LOG_VERBOSE("[MP-STUB] SendUninitLive called");
        LOG_WARNING("[MP-STUB] This function will be implemented in Phase 2+");
    }
    
    void SendStartLiveQuickGame() {
        LOG_VERBOSE("[MP-STUB] SendStartLiveQuickGame called");
        LOG_WARNING("[MP-STUB] This function will be implemented in Phase 2+");
    }
    
    void SendStartLiveMotocross() {
        LOG_VERBOSE("[MP-STUB] SendStartLiveMotocross called");
        LOG_WARNING("[MP-STUB] This function will be implemented in Phase 2+");
    }
    
    void SendStartLiveSupercross() {
        LOG_VERBOSE("[MP-STUB] SendStartLiveSupercross called");
        LOG_WARNING("[MP-STUB] This function will be implemented in Phase 2+");
    }
    
    void SendStartLiveTrials() {
        LOG_VERBOSE("[MP-STUB] SendStartLiveTrials called");
        LOG_WARNING("[MP-STUB] This function will be implemented in Phase 2+");
    }
    
    void SendStartPrivateRace() {
        LOG_VERBOSE("[MP-STUB] SendStartPrivateRace called");
        LOG_WARNING("[MP-STUB] This function will be implemented in Phase 2+");
    }
    
    void SendCancelPrivateRace() {
        LOG_VERBOSE("[MP-STUB] SendCancelPrivateRace called");
        LOG_WARNING("[MP-STUB] This function will be implemented in Phase 2+");
    }
    
    void SendStartLocalRace() {
        LOG_VERBOSE("[MP-STUB] SendStartLocalRace called");
        LOG_WARNING("[MP-STUB] This function will be implemented in Phase 2+");
    }
    
    void SendJoinLocalMultiplayer() {
        LOG_VERBOSE("[MP-STUB] SendJoinLocalMultiplayer called");
        LOG_WARNING("[MP-STUB] This function will be implemented in Phase 2+");
    }
    
    void SendLeaveLocalMultiplayer() {
        LOG_VERBOSE("[MP-STUB] SendLeaveLocalMultiplayer called");
        LOG_WARNING("[MP-STUB] This function will be implemented in Phase 2+");
    }
    
    void SendExitToLobby() {
        LOG_VERBOSE("[MP-STUB] SendExitToLobby called");
        LOG_WARNING("[MP-STUB] This function will be implemented in Phase 2+");
    }
}
