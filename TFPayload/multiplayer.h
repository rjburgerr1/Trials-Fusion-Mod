#pragma once
#include <Windows.h>
#include <cstdint>
#include <string>
#include <vector>
#include <fstream>

namespace Multiplayer {
    // Phase 1: Monitoring and Logging
    
    struct SessionInfo {
        uint32_t sessionId;
        std::string sessionName;
        bool isHost;
        uint32_t playerCount;
        uint32_t maxPlayers;
        std::string gameMode;
        uint64_t timestamp;
    };

    struct NetworkPacketInfo {
        uint32_t packetType;
        uint32_t packetSize;
        uint32_t sourcePlayerId;
        uint32_t destPlayerId;
        uint64_t timestamp;
        std::vector<uint8_t> data;
    };

    struct PlayerStateInfo {
        uint32_t playerId;
        std::string playerName;
        float posX, posY, posZ;
        float velX, velY, velZ;
        uint32_t checkpointIndex;
        bool isReady;
        uint64_t timestamp;
    };

    // Initialize the multiplayer monitoring system
    bool Initialize(DWORD_PTR baseAddress);
    
    // Shutdown and save all logs
    void Shutdown();
    
    // Enable/disable detailed packet logging
    void SetPacketLoggingEnabled(bool enabled);
    
    // Enable/disable session event logging
    void SetSessionLoggingEnabled(bool enabled);
    
    // Get current session info (if in a session)
    SessionInfo GetCurrentSession();
    
    // Check if currently in a multiplayer session
    bool IsInSession();
    
    // Save all captured data to files
    void SaveLogs();
    
    // Manual trigger to capture current state
    void CaptureCurrentState();
    
    // Get statistics
    struct Stats {
        uint32_t totalPacketsReceived;
        uint32_t totalPacketsSent;
        uint32_t sessionCount;
        uint32_t playerStateUpdates;
        uint64_t totalBytesReceived;
        uint64_t totalBytesSent;
    };
    Stats GetStats();
    
    // Hotkey handling for manual logging
    void CheckHotkey();
    
    // Phase 2+ Functions (Stubs for now - will be implemented later)
    // These are called by devMenu.cpp but not yet implemented
    void SendVoteTrack(uint32_t trackId = 0);
    void SendSelectBike(uint32_t bikeId = 0);
    void SendContinueToNextHeat();
    void SendContinueToVote();
    void SendReplayMultiplayer();
    void SendInitLive();
    void SendUninitLive();
    void SendStartLiveQuickGame();
    void SendStartLiveMotocross();
    void SendStartLiveSupercross();
    void SendStartLiveTrials();
    void SendStartPrivateRace();
    void SendCancelPrivateRace();
    void SendStartLocalRace();
    void SendJoinLocalMultiplayer();
    void SendLeaveLocalMultiplayer();
    void SendExitToLobby();
}
