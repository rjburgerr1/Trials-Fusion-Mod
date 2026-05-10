#define _CRT_SECURE_NO_WARNINGS
#include "pch.h"
#include "host-join.h"
#include "logging.h"
#include "base-address.h"
#include "imgui/imgui.h"
#include <MinHook.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <mutex>
#include <vector>
#include <utility>

namespace HostJoin {
    // ============================================================================
    // Game Memory Addresses - UPLAY VERSION (RVA offsets - Ghidra base 0x700000)
    // ============================================================================
    
    // DAT_0174d884 (race session state): Ghidra 0x0174d884, RVA = 0x0174d884 - 0x700000 = 0x104d884
    static constexpr uintptr_t RACE_SESSION_STATE_RVA_UPLAY = 0x104d884;
    
    // ConstructSessionJoinRequestMessage: Ghidra 0x0126a750, RVA = 0x0126a750 - 0x700000 = 0xb6a750
    static constexpr uintptr_t CONSTRUCT_SESSION_JOIN_REQUEST_RVA_UPLAY = 0xb6a750;
    
    // SubmitSessionMessage: Ghidra 0x011d9f60, RVA = 0x011d9f60 - 0x700000 = 0xad9f60
    static constexpr uintptr_t SUBMIT_SESSION_MESSAGE_RVA_UPLAY = 0xad9f60;
    
    // CreateAndSendSessionJoinRequest: Ghidra 0x012717d0, RVA = 0x012717d0 - 0x700000 = 0xb717d0
    static constexpr uintptr_t CREATE_AND_SEND_SESSION_JOIN_REQUEST_RVA_UPLAY = 0xb717d0;
    
    // AllocateMemoryBlock: Ghidra 0x00c6cff0, RVA = 0x00c6cff0 - 0x700000 = 0x56cff0
    static constexpr uintptr_t ALLOCATE_MEMORY_BLOCK_RVA_UPLAY = 0x56cff0;
    
    // FreeMemory: Ghidra 0x00c6bb30, RVA = 0x00c6bb30 - 0x700000 = 0x56bb30
    static constexpr uintptr_t FREE_MEMORY_RVA_UPLAY = 0x56bb30;
    
    // DAT_017a5dec (message type ID): Ghidra 0x017a5dec, RVA = 0x017a5dec - 0x700000 = 0x10a5dec
    static constexpr uintptr_t MESSAGE_TYPE_ID_RVA_UPLAY = 0x10a5dec;

    // ============================================================================
    // Game Memory Addresses - STEAM VERSION (RVA offsets - mapped from CSV)
    // ============================================================================
    
    // DAT_0174d884 -> DAT_0118f884: Ghidra 0x0118f884, RVA = 0x0118f884 - 0x140000 = 0x104f884
    static constexpr uintptr_t RACE_SESSION_STATE_RVA_STEAM = 0x104f884;
    
    // ConstructSessionJoinRequestMessage: Multiple matches, using first: Ghidra 0x00cacb70, RVA = 0x00cacb70 - 0x140000 = 0xb6cb70
    static constexpr uintptr_t CONSTRUCT_SESSION_JOIN_REQUEST_RVA_STEAM = 0xb6cb70;
    
    // FUN_011d9f60 (SubmitSessionMessage): Ghidra 0x00c1ae00, RVA = 0x00c1ae00 - 0x140000 = 0xadae00
    static constexpr uintptr_t SUBMIT_SESSION_MESSAGE_RVA_STEAM = 0xadae00;
    
    // CreateAndSendSessionJoinRequest: Multiple matches, using first: Ghidra 0x00c4cd00, RVA = 0x00c4cd00 - 0x140000 = 0xb0cd00
    static constexpr uintptr_t CREATE_AND_SEND_SESSION_JOIN_REQUEST_RVA_STEAM = 0xb0cd00;
    
    // AllocateMemoryBlock: Found at offset +0x50 in FUN_0136cfa0
    // Uplay: 0x0136cff0 -> Steam: 0x00dae190, RVA = 0x00dae190 - 0x140000 = 0xc6e190
    // Confidence: 100% (144/144 function mappings point to same address)
    static constexpr uintptr_t ALLOCATE_MEMORY_BLOCK_RVA_STEAM = 0xc6e190;
    
    // FreeMemory: Found at offset +0x30 in FUN_0136bb00
    // Uplay: 0x0136bb30 -> Steam: 0x00daccd0, RVA = 0x00daccd0 - 0x140000 = 0xc6ccd0
    // Confidence: 96.6% (86/89 function mappings point to same address)
    static constexpr uintptr_t FREE_MEMORY_RVA_STEAM = 0xc6ccd0;
    
    // DAT_017a5dec -> DAT_011e7dec: Ghidra 0x011e7dec, RVA = 0x011e7dec - 0x140000 = 0x10a7dec
    static constexpr uintptr_t MESSAGE_TYPE_ID_RVA_STEAM = 0x10a7dec;

    // ============================================================================
    // Helper functions to get correct RVA based on detected version
    // ============================================================================
    
    static uintptr_t GetRaceSessionStateRVA() {
        return BaseAddress::IsSteamVersion() ? RACE_SESSION_STATE_RVA_STEAM : RACE_SESSION_STATE_RVA_UPLAY;
    }
    
    static uintptr_t GetConstructSessionJoinRequestRVA() {
        return BaseAddress::IsSteamVersion() ? CONSTRUCT_SESSION_JOIN_REQUEST_RVA_STEAM : CONSTRUCT_SESSION_JOIN_REQUEST_RVA_UPLAY;
    }
    
    static uintptr_t GetSubmitSessionMessageRVA() {
        return BaseAddress::IsSteamVersion() ? SUBMIT_SESSION_MESSAGE_RVA_STEAM : SUBMIT_SESSION_MESSAGE_RVA_UPLAY;
    }
    
    static uintptr_t GetCreateAndSendSessionJoinRequestRVA() {
        return BaseAddress::IsSteamVersion() ? CREATE_AND_SEND_SESSION_JOIN_REQUEST_RVA_STEAM : CREATE_AND_SEND_SESSION_JOIN_REQUEST_RVA_UPLAY;
    }
    
    static uintptr_t GetAllocateMemoryBlockRVA() {
        return BaseAddress::IsSteamVersion() ? ALLOCATE_MEMORY_BLOCK_RVA_STEAM : ALLOCATE_MEMORY_BLOCK_RVA_UPLAY;
    }
    
    static uintptr_t GetFreeMemoryRVA() {
        return BaseAddress::IsSteamVersion() ? FREE_MEMORY_RVA_STEAM : FREE_MEMORY_RVA_UPLAY;
    }
    
    static uintptr_t GetMessageTypeIdRVA() {
        return BaseAddress::IsSteamVersion() ? MESSAGE_TYPE_ID_RVA_STEAM : MESSAGE_TYPE_ID_RVA_UPLAY;
    }

    // CONFIGURATION & STATE

    static DWORD_PTR g_BaseAddress = 0;
    
    // Session state
    static uint32_t g_CurrentSessionId = 0;      // Session ID when hosting
    static uint32_t g_TargetSessionId = 0;       // Session ID to join
    static bool g_IsHosting = false;
    static std::string g_StatusMessage = "Not initialized";
    static std::mutex g_StateMutex;
    
    // Input buffer for session ID entry
    static char g_SessionIdInputBuffer[32] = "";
    
    // Pointers to game globals
    static uint32_t** g_pRaceSessionState = nullptr;  // DAT_0174d884
    
    // ============================================================================
    // GAME FUNCTION TYPES
    // ============================================================================
    
    typedef void* (__cdecl* AllocateMemoryBlock_t)(uint32_t size, uint32_t alignment, uint32_t flags1, uint32_t flags2);
    static AllocateMemoryBlock_t g_AllocateMemoryBlock = nullptr;
    
    typedef void (__cdecl* FreeMemory_t)(uint32_t ptr);
    static FreeMemory_t g_FreeMemory = nullptr;
    
    typedef void* (__thiscall* ConstructSessionJoinRequestMessage_t)(void* thisPtr, uint32_t param);
    static ConstructSessionJoinRequestMessage_t g_ConstructSessionJoinRequestMessage = nullptr;
    
    typedef void (__cdecl* SubmitSessionMessage_t)(uint32_t* result, uint32_t* msgId, int* message);
    static SubmitSessionMessage_t g_SubmitSessionMessage = nullptr;
    
    typedef void (__cdecl* CreateAndSendSessionJoinRequest_t)(uint32_t* result, uint32_t param);
    static CreateAndSendSessionJoinRequest_t g_CreateAndSendSessionJoinRequest = nullptr;
    
    // ============================================================================
    // UTILITY FUNCTIONS
    // ============================================================================
    
    static bool TryReadMemory(DWORD_PTR address, void* output, size_t size) {
        __try {
            memcpy(output, (void*)address, size);
            return true;
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }
    
    static bool IsValidPointer(void* ptr) {
        if (ptr == nullptr) return false;
        DWORD_PTR addr = (DWORD_PTR)ptr;
        return (addr > 0x10000 && addr < 0x80000000);
    }
    
    static void LogToFile(const std::string& message) {
        std::ofstream logFile("F:/host_join_log.txt", std::ios::app);
        if (logFile.is_open()) {
            logFile << message << std::endl;
            logFile.close();
        }
    }
    
    static void SaveSessionIdToFile(uint32_t sessionId) {
        std::ofstream file("F:/session_id.txt");
        if (file.is_open()) {
            file << "0x" << std::hex << std::uppercase << sessionId << std::endl;
            file << std::dec << sessionId << std::endl;
            file.close();
            LOG_VERBOSE("[HOST-JOIN] Session ID saved to F:/session_id.txt");
        }
    }
    
    // ============================================================================
    // GET MANAGER FROM GAME GLOBALS
    // ============================================================================
    
    static void* GetManagerFromGlobals() {
        // The race session state is at DAT_0174d884
        // The manager (multiplayer service) is stored in this structure
        
        if (!g_pRaceSessionState) {
            return nullptr;
        }
        
        uint32_t raceState = 0;
        if (!TryReadMemory((DWORD_PTR)g_pRaceSessionState, &raceState, sizeof(raceState))) {
            return nullptr;
        }
        
        if (!IsValidPointer((void*)raceState)) {
            return nullptr;
        }
        
        // The manager pointer is somewhere in the race state structure
        // From the logs, we see the manager at param_1[1] in StartMultiplayerSession
        // Let's check various offsets
        
        // Try offset 0x4 (the second dword in the structure passed to StartMultiplayerSession)
        // Actually the race state itself has network session at +0x1A4
        
        return (void*)raceState;
    }
    
    // ============================================================================
    // SESSION HANDLER TREE TRAVERSAL
    // ============================================================================
    
    // Tree node structure from Ghidra analysis:
    // [0x00] - left child pointer
    // [0x04] - right child pointer  
    // [0x08] - parent pointer
    // [0x0C] - color (red/black tree)
    // [0x10] - key (session ID)
    // [0x14] - value (session object pointer)
    
    struct SessionTreeNode {
        uint32_t leftChild;     // +0x00
        uint32_t rightChild;    // +0x04
        uint32_t parent;        // +0x08
        uint32_t color;         // +0x0C
        uint32_t sessionId;     // +0x10 - THE KEY!
        uint32_t sessionObject; // +0x14 - pointer to actual session
    };
    
    static bool FindSessionIdFromTree(uint32_t treeRoot, uint32_t sentinel, uint32_t* outSessionId) {
        // Traverse the tree to find any valid session ID
        // For a host, there should be at least one session in the tree
        
        if (!IsValidPointer((void*)treeRoot) || treeRoot == sentinel) {
            return false;
        }
        
        SessionTreeNode node;
        if (!TryReadMemory(treeRoot, &node, sizeof(node))) {
            LOG_ERROR("[HOST-JOIN] Failed to read tree node at 0x" << std::hex << treeRoot);
            return false;
        }
        
        LOG_VERBOSE("[HOST-JOIN] Tree node at 0x" << std::hex << treeRoot
                 << ": sessionId=0x" << node.sessionId 
                 << ", sessionObj=0x" << node.sessionObject);
        
        // Check if this node has a valid session ID
        if (node.sessionId != 0 && node.sessionId != 0xFFFFFFFF && IsValidPointer((void*)node.sessionObject)) {
            *outSessionId = node.sessionId;
            return true;
        }
        
        // Try left subtree
        if (IsValidPointer((void*)node.leftChild) && node.leftChild != sentinel) {
            if (FindSessionIdFromTree(node.leftChild, sentinel, outSessionId)) {
                return true;
            }
        }
        
        // Try right subtree
        if (IsValidPointer((void*)node.rightChild) && node.rightChild != sentinel) {
            if (FindSessionIdFromTree(node.rightChild, sentinel, outSessionId)) {
                return true;
            }
        }
        
        return false;
    }
    
    // ============================================================================
    // SESSION ID SCANNING
    // ============================================================================
    
    static void ScanForSessionId() {
        LOG_VERBOSE("[HOST-JOIN] Scanning for session ID from game globals...");
        
        // Read the race session state pointer from the global
        if (!g_pRaceSessionState) {
            LOG_ERROR("[HOST-JOIN] Race session state pointer not initialized");
            std::lock_guard<std::mutex> lock(g_StateMutex);
            g_StatusMessage = "Error: Not initialized";
            return;
        }
        
        // DEBUG: Log addresses
        LOG_VERBOSE("[HOST-JOIN] DEBUG: g_BaseAddress = 0x" << std::hex << g_BaseAddress);
        LOG_VERBOSE("[HOST-JOIN] DEBUG: GetRaceSessionStateRVA() = 0x" << std::hex << GetRaceSessionStateRVA());
        LOG_VERBOSE("[HOST-JOIN] DEBUG: g_pRaceSessionState address = 0x" << std::hex << (uintptr_t)g_pRaceSessionState);
        LOG_VERBOSE("[HOST-JOIN] DEBUG: Version: " << (BaseAddress::IsSteamVersion() ? "Steam" : "Uplay"));
        
        // Try reading raw bytes to see what's there
        uint8_t rawBytes[16];
        if (TryReadMemory((DWORD_PTR)g_pRaceSessionState, rawBytes, 16)) {
            std::stringstream ss;
            for (int i = 0; i < 16; i++) {
                ss << std::hex << std::setw(2) << std::setfill('0') << (int)rawBytes[i] << " ";
            }
            LOG_VERBOSE("[HOST-JOIN] DEBUG: Raw bytes at address: " << ss.str());
        } else {
            LOG_ERROR("[HOST-JOIN] DEBUG: Cannot read memory at g_pRaceSessionState!");
        }
        
        // Try reading nearby offsets
        LOG_VERBOSE("[HOST-JOIN] DEBUG: Scanning nearby offsets for valid pointers...");
        for (int offset = -32; offset <= 32; offset += 4) {
            uint32_t value = 0;
            if (TryReadMemory((DWORD_PTR)g_pRaceSessionState + offset, &value, sizeof(value))) {
                if (IsValidPointer((void*)value)) {
                    LOG_VERBOSE("[HOST-JOIN] DEBUG:   Offset " << std::dec << std::setw(4) << offset << ": 0x" << std::hex << value << " (valid pointer)");
                }
            }
        }
        
        uint32_t raceState = 0;
        if (!TryReadMemory((DWORD_PTR)g_pRaceSessionState, &raceState, sizeof(raceState))) {
            LOG_ERROR("[HOST-JOIN] Failed to read race session state");
            std::lock_guard<std::mutex> lock(g_StateMutex);
            g_StatusMessage = "Error: Can't read race state";
            return;
        }
        
        LOG_VERBOSE("[HOST-JOIN] Race Session State: 0x" << std::hex << raceState);
        
        if (!IsValidPointer((void*)raceState)) {
            LOG_VERBOSE("[HOST-JOIN] Race session state is null - not in a session yet");
            std::lock_guard<std::mutex> lock(g_StateMutex);
            g_StatusMessage = "Not in a session - enter private match first";
            return;
        }
        
        // Read the network session at +0x1A4
        uint32_t netSessionAddr = 0;
        if (!TryReadMemory((DWORD_PTR)raceState + 0x1A4, &netSessionAddr, sizeof(netSessionAddr))) {
            LOG_ERROR("[HOST-JOIN] Failed to read network session pointer");
            std::lock_guard<std::mutex> lock(g_StateMutex);
            g_StatusMessage = "Error: Can't read net session";
            return;
        }
        
        LOG_VERBOSE("[HOST-JOIN] Network Session at: 0x" << std::hex << netSessionAddr);
        
        if (!IsValidPointer((void*)netSessionAddr)) {
            LOG_VERBOSE("[HOST-JOIN] Network session is null");
            std::lock_guard<std::mutex> lock(g_StateMutex);
            g_StatusMessage = "No network session active";
            return;
        }
        
        // The session handler has a tree structure at handler + 0x30 (sentinel) and handler + 0x38 (root)
        // From HandleSessionSetHandlerEvent analysis:
        //   - this + 0x30 is the sentinel node (marks end of tree)
        //   - this + 0x38 is the tree root pointer
        //   - Node at offset +0x10 has the session ID
        //   - Node at offset +0x14 has the session object pointer
        
        // The network session structure has the handler embedded or referenced
        // Let's scan multiple potential handler offsets
        std::vector<uint32_t> handlerOffsets = {0x00, 0x04, 0x08, 0x0C, 0x10, 0x14, 0x18, 0x1C, 0x20};
        
        uint32_t foundSessionId = 0;
        bool found = false;
        
        for (uint32_t offset : handlerOffsets) {
            uint32_t potentialHandler = 0;
            
            // Try reading as a pointer (handler could be referenced)
            if (!TryReadMemory((DWORD_PTR)netSessionAddr + offset, &potentialHandler, sizeof(potentialHandler))) {
                continue;
            }
            
            if (!IsValidPointer((void*)potentialHandler)) {
                // Maybe the handler IS at netSession + offset (embedded)
                potentialHandler = netSessionAddr + offset;
            }
            
            // Try reading tree root at handler + 0x38
            uint32_t treeRoot = 0;
            if (!TryReadMemory((DWORD_PTR)potentialHandler + 0x38, &treeRoot, sizeof(treeRoot))) {
                continue;
            }
            
            if (!IsValidPointer((void*)treeRoot)) {
                continue;
            }
            
            // Sentinel is at handler + 0x30
            uint32_t sentinel = potentialHandler + 0x30;
            
            LOG_VERBOSE("[HOST-JOIN] Trying handler at offset +0x" << std::hex << offset
                     << ": handler=0x" << potentialHandler 
                     << ", tree root=0x" << treeRoot);
            
            if (FindSessionIdFromTree(treeRoot, sentinel, &foundSessionId)) {
                LOG_VERBOSE("[HOST-JOIN] Found session ID 0x" << std::hex << std::uppercase << foundSessionId
                         << " from tree at handler offset +0x" << offset);
                found = true;
                break;
            }
        }
        
        // Fallback: Also try the direct approach - scan the network session for a stable looking ID
        // Session IDs tend to be medium-sized numbers that don't change rapidly
        if (!found) {
            LOG_VERBOSE("[HOST-JOIN] Tree scan didn't find session - trying direct memory scan...");
            
            // Scan through the network session structure looking for potential session IDs
            std::vector<std::pair<uint32_t, uint32_t>> candidates; // offset, value
            
            for (uint32_t offset = 0; offset < 0x200; offset += 4) {
                uint32_t value = 0;
                if (TryReadMemory((DWORD_PTR)netSessionAddr + offset, &value, sizeof(value))) {
                    // Session IDs are typically non-zero, non-pointer values
                    // They're usually generated as "sessID_%s_%u" format suggesting a moderate number
                    if (value > 0x1000 && value < 0x10000000 && !IsValidPointer((void*)value)) {
                        candidates.push_back({offset, value});
                    }
                }
            }
            
            LOG_VERBOSE("[HOST-JOIN] Found " << candidates.size() << " potential session ID candidates");
            for (const auto& c : candidates) {
                LOG_VERBOSE("[HOST-JOIN]   +0x" << std::hex << c.first << ": 0x" << c.second << " (" << std::dec << c.second << ")");
            }
        }
        
        if (found && foundSessionId != 0) {
            std::lock_guard<std::mutex> lock(g_StateMutex);
            g_CurrentSessionId = foundSessionId;
            g_IsHosting = true;
            std::stringstream ss;
            ss << "Session ID: 0x" << std::hex << std::uppercase << foundSessionId;
            g_StatusMessage = ss.str();
            SaveSessionIdToFile(foundSessionId);
            
            LOG_VERBOSE("[HOST-JOIN] *** SESSION ID CAPTURED: 0x" << std::hex << std::uppercase << foundSessionId << " ***");
        } else {
            LOG_VERBOSE("[HOST-JOIN] Could not locate session ID in tree structure");
            std::lock_guard<std::mutex> lock(g_StateMutex);
            g_StatusMessage = "Session ID not found - check logs";
        }
    }
    
    // ============================================================================
    // SESSION JOIN REQUEST MESSAGE STRUCTURE
    // ============================================================================
    
    #pragma pack(push, 1)
    struct SessionJoinRequestMessage {
        void* vtable;           // +0x00
        void* vtable2;          // +0x04
        uint32_t field_08;      // +0x08
        uint32_t field_0C;      // +0x0C
        uint32_t field_10;      // +0x10
        uint32_t field_14;      // +0x14
        uint32_t field_18;      // +0x18
        uint32_t field_1C;      // +0x1C
        uint32_t sessionId;     // +0x20 - THE KEY FIELD!
        uint32_t field_24;      // +0x24
        uint32_t* playerId;     // +0x28
        uint32_t* playerName;   // +0x2C
        uint32_t field_30;      // +0x30
        void* additionalData;   // +0x34
        uint32_t field_38;      // +0x38
        uint32_t field_3C;      // +0x3C
        uint32_t field_40;      // +0x40
    };
    #pragma pack(pop)
    
    static_assert(sizeof(SessionJoinRequestMessage) == 0x44, "SessionJoinRequestMessage must be 0x44 bytes");
    
    // ============================================================================
    // JOIN SESSION IMPLEMENTATION
    // ============================================================================
    
    bool JoinSessionById(uint32_t sessionId) {
        LOG_VERBOSE("[HOST-JOIN] Attempting to join session: 0x" << std::hex << std::uppercase << sessionId);
        
        if (sessionId == 0) {
            LOG_ERROR("[HOST-JOIN] Invalid session ID (0)");
            std::lock_guard<std::mutex> lock(g_StateMutex);
            g_StatusMessage = "Error: Invalid session ID";
            return false;
        }
        
        // Method 1: Try using the game's CreateAndSendSessionJoinRequest function
        if (g_CreateAndSendSessionJoinRequest) {
            LOG_VERBOSE("[HOST-JOIN] Using CreateAndSendSessionJoinRequest...");
            
            uint32_t result = 0;
            g_CreateAndSendSessionJoinRequest(&result, sessionId);
            
            LOG_VERBOSE("[HOST-JOIN] CreateAndSendSessionJoinRequest returned: " << result);
            
            std::lock_guard<std::mutex> lock(g_StateMutex);
            if (result == 1) {
                g_StatusMessage = "Join request sent successfully!";
                LOG_VERBOSE("[HOST-JOIN] *** JOIN REQUEST SENT! ***");
                return true;
            } else {
                g_StatusMessage = "Join request returned: " + std::to_string(result);
            }
        }
        
        // Method 2: Manually construct and send the join request message
        LOG_VERBOSE("[HOST-JOIN] Trying manual message construction...");
        
        if (!g_AllocateMemoryBlock || !g_ConstructSessionJoinRequestMessage) {
            LOG_ERROR("[HOST-JOIN] Required functions not found!");
            std::lock_guard<std::mutex> lock(g_StateMutex);
            g_StatusMessage = "Error: Game functions not initialized";
            return false;
        }
        
        // Allocate memory for the message
        void* msgMemory = g_AllocateMemoryBlock(0x44, 4, 0, 0x41200000);
        if (!msgMemory) {
            LOG_ERROR("[HOST-JOIN] Failed to allocate message memory!");
            std::lock_guard<std::mutex> lock(g_StateMutex);
            g_StatusMessage = "Error: Memory allocation failed";
            return false;
        }
        
        LOG_VERBOSE("[HOST-JOIN] Allocated message at: 0x" << std::hex << (uintptr_t)msgMemory);
        
        // Construct the message using the game's function
        void* constructedMsg = g_ConstructSessionJoinRequestMessage(msgMemory, 8);
        if (!constructedMsg) {
            LOG_ERROR("[HOST-JOIN] Failed to construct message!");
            if (g_FreeMemory) g_FreeMemory((uint32_t)(uintptr_t)msgMemory);
            std::lock_guard<std::mutex> lock(g_StateMutex);
            g_StatusMessage = "Error: Message construction failed";
            return false;
        }
        
        LOG_VERBOSE("[HOST-JOIN] Message constructed successfully");
        
        // Now set the session ID at offset 0x20
        SessionJoinRequestMessage* msg = (SessionJoinRequestMessage*)constructedMsg;
        msg->sessionId = sessionId;
        
        LOG_VERBOSE("[HOST-JOIN] Set session ID to: 0x" << std::hex << std::uppercase << sessionId);
        
        // Try to submit the message through the session manager
        if (g_SubmitSessionMessage) {
            uint32_t result = 0;
            uint32_t msgTypeId = 0;
            
            // The message type ID is stored at DAT_017a5dec
            DWORD_PTR msgTypeIdAddr = g_BaseAddress + GetMessageTypeIdRVA();
            if (TryReadMemory(msgTypeIdAddr, &msgTypeId, sizeof(msgTypeId))) {
                LOG_VERBOSE("[HOST-JOIN] Found message type ID: 0x" << std::hex << msgTypeId);
                
                g_SubmitSessionMessage(&result, &msgTypeId, (int*)constructedMsg);
                LOG_VERBOSE("[HOST-JOIN] SubmitSessionMessage returned: " << result);
                
                if (result == 1) {
                    std::lock_guard<std::mutex> lock(g_StateMutex);
                    g_StatusMessage = "Join request sent successfully!";
                    LOG_VERBOSE("[HOST-JOIN] *** JOIN REQUEST SENT! ***");
                    return true;
                }
            } else {
                LOG_ERROR("[HOST-JOIN] Could not read message type ID");
            }
        }
        
        // Clean up if we failed
        if (g_FreeMemory) {
            g_FreeMemory((uint32_t)(uintptr_t)msgMemory);
        }
        
        std::lock_guard<std::mutex> lock(g_StateMutex);
        g_StatusMessage = "Join request may have been sent - check if you joined";
        
        return false;
    }
    
    // ============================================================================
    // PUBLIC API IMPLEMENTATION
    // ============================================================================
    
    bool Initialize(DWORD_PTR baseAddress) {
        if (baseAddress == 0) {
            LOG_ERROR("[HOST-JOIN] Invalid base address!");
            return false;
        }
        
        g_BaseAddress = baseAddress;
        LOG_VERBOSE("[HOST-JOIN] Initializing host-join system...");
        LOG_VERBOSE("[HOST-JOIN] Base address: 0x" << std::hex << baseAddress);
        
        // Log version detection
        if (BaseAddress::IsSteamVersion()) {
            LOG_VERBOSE("[HOST-JOIN] Steam version detected - using Steam addresses");
            LOG_VERBOSE("[HOST-JOIN]   RaceSessionState RVA: 0x" << std::hex << RACE_SESSION_STATE_RVA_STEAM);
            LOG_VERBOSE("[HOST-JOIN]   ConstructSessionJoinRequest RVA: 0x" << std::hex << CONSTRUCT_SESSION_JOIN_REQUEST_RVA_STEAM);
            LOG_VERBOSE("[HOST-JOIN]   SubmitSessionMessage RVA: 0x" << std::hex << SUBMIT_SESSION_MESSAGE_RVA_STEAM);
            LOG_VERBOSE("[HOST-JOIN]   CreateAndSendSessionJoinRequest RVA: 0x" << std::hex << CREATE_AND_SEND_SESSION_JOIN_REQUEST_RVA_STEAM);
            LOG_VERBOSE("[HOST-JOIN]   AllocateMemoryBlock RVA: 0x" << std::hex << ALLOCATE_MEMORY_BLOCK_RVA_STEAM);
            LOG_VERBOSE("[HOST-JOIN]   FreeMemory RVA: 0x" << std::hex << FREE_MEMORY_RVA_STEAM);
        } else {
            LOG_VERBOSE("[HOST-JOIN] Uplay version detected - using Uplay addresses");
            LOG_VERBOSE("[HOST-JOIN]   RaceSessionState RVA: 0x" << std::hex << RACE_SESSION_STATE_RVA_UPLAY);
            LOG_VERBOSE("[HOST-JOIN]   ConstructSessionJoinRequest RVA: 0x" << std::hex << CONSTRUCT_SESSION_JOIN_REQUEST_RVA_UPLAY);
            LOG_VERBOSE("[HOST-JOIN]   SubmitSessionMessage RVA: 0x" << std::hex << SUBMIT_SESSION_MESSAGE_RVA_UPLAY);
            LOG_VERBOSE("[HOST-JOIN]   CreateAndSendSessionJoinRequest RVA: 0x" << std::hex << CREATE_AND_SEND_SESSION_JOIN_REQUEST_RVA_UPLAY);
            LOG_VERBOSE("[HOST-JOIN]   AllocateMemoryBlock RVA: 0x" << std::hex << ALLOCATE_MEMORY_BLOCK_RVA_UPLAY);
            LOG_VERBOSE("[HOST-JOIN]   FreeMemory RVA: 0x" << std::hex << FREE_MEMORY_RVA_UPLAY);
        }
        
        // Get pointer to race session state global (DAT_0174d884)
        g_pRaceSessionState = (uint32_t**)(baseAddress + GetRaceSessionStateRVA());
        LOG_VERBOSE("[HOST-JOIN] Race session state ptr at: 0x" << std::hex << (uintptr_t)g_pRaceSessionState);
        
        // Calculate function addresses using version-specific RVAs
        g_ConstructSessionJoinRequestMessage = (ConstructSessionJoinRequestMessage_t)(baseAddress + GetConstructSessionJoinRequestRVA());
        g_SubmitSessionMessage = (SubmitSessionMessage_t)(baseAddress + GetSubmitSessionMessageRVA());
        g_CreateAndSendSessionJoinRequest = (CreateAndSendSessionJoinRequest_t)(baseAddress + GetCreateAndSendSessionJoinRequestRVA());
        g_AllocateMemoryBlock = (AllocateMemoryBlock_t)(baseAddress + GetAllocateMemoryBlockRVA());
        g_FreeMemory = (FreeMemory_t)(baseAddress + GetFreeMemoryRVA());
        
        g_StatusMessage = "Ready - enter private match and scan";
        LOG_VERBOSE("[HOST-JOIN] Host-join system initialized (no hooks needed)");
        LOG_VERBOSE("[HOST-JOIN] ");
        LOG_VERBOSE("[HOST-JOIN] === INSTRUCTIONS ===");
        LOG_VERBOSE("[HOST-JOIN] 1. Click 'Private Match' to enter a private lobby");
        LOG_VERBOSE("[HOST-JOIN] 2. Click 'Scan for Session ID' in the dev menu");
        LOG_VERBOSE("[HOST-JOIN] 3. Share the session ID with Player 2");
        LOG_VERBOSE("[HOST-JOIN] ====================");
        
        return true;
    }
    
    void Shutdown() {
        LOG_VERBOSE("[HOST-JOIN] Shutting down host-join system...");
    }
    
    uint32_t GetCurrentSessionId() {
        std::lock_guard<std::mutex> lock(g_StateMutex);
        return g_CurrentSessionId;
    }
    
    bool IsHostingSession() {
        std::lock_guard<std::mutex> lock(g_StateMutex);
        return g_IsHosting;
    }
    
    void SetTargetSessionId(uint32_t sessionId) {
        std::lock_guard<std::mutex> lock(g_StateMutex);
        g_TargetSessionId = sessionId;
        LOG_VERBOSE("[HOST-JOIN] Target session ID set to: 0x" << std::hex << std::uppercase << sessionId);
    }
    
    uint32_t GetTargetSessionId() {
        std::lock_guard<std::mutex> lock(g_StateMutex);
        return g_TargetSessionId;
    }
    
    bool JoinSession() {
        uint32_t targetId;
        {
            std::lock_guard<std::mutex> lock(g_StateMutex);
            targetId = g_TargetSessionId;
        }
        return JoinSessionById(targetId);
    }
    
    void RefreshSessionId() {
        LOG_VERBOSE("[HOST-JOIN] Scanning for session ID...");
        ScanForSessionId();
    }
    
    bool CopySessionIdToClipboard() {
        uint32_t sessionId;
        {
            std::lock_guard<std::mutex> lock(g_StateMutex);
            sessionId = g_CurrentSessionId;
        }
        
        if (sessionId == 0) {
            LOG_WARNING("[HOST-JOIN] No session ID to copy");
            return false;
        }
        
        std::stringstream ss;
        ss << std::hex << std::uppercase << sessionId;
        std::string hexStr = ss.str();
        
        if (OpenClipboard(nullptr)) {
            EmptyClipboard();
            HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, hexStr.size() + 1);
            if (hMem) {
                memcpy(GlobalLock(hMem), hexStr.c_str(), hexStr.size() + 1);
                GlobalUnlock(hMem);
                SetClipboardData(CF_TEXT, hMem);
            }
            CloseClipboard();
            LOG_VERBOSE("[HOST-JOIN] Session ID copied to clipboard: " << hexStr);
            return true;
        }
        return false;
    }
    
    bool PasteSessionIdFromClipboard() {
        if (!OpenClipboard(nullptr)) {
            return false;
        }
        
        HANDLE hData = GetClipboardData(CF_TEXT);
        if (!hData) {
            CloseClipboard();
            return false;
        }
        
        char* pszText = static_cast<char*>(GlobalLock(hData));
        if (!pszText) {
            CloseClipboard();
            return false;
        }
        
        std::string text(pszText);
        GlobalUnlock(hData);
        CloseClipboard();
        
        // Parse as hex (with or without 0x prefix)
        uint32_t sessionId = 0;
        if (text.substr(0, 2) == "0x" || text.substr(0, 2) == "0X") {
            sessionId = std::stoul(text, nullptr, 16);
        } else {
            try {
                sessionId = std::stoul(text, nullptr, 16);
            } catch (...) {
                try {
                    sessionId = std::stoul(text, nullptr, 10);
                } catch (...) {
                    LOG_ERROR("[HOST-JOIN] Failed to parse session ID from clipboard");
                    return false;
                }
            }
        }
        
        SetTargetSessionId(sessionId);
        snprintf(g_SessionIdInputBuffer, sizeof(g_SessionIdInputBuffer), "%X", sessionId);
        
        LOG_VERBOSE("[HOST-JOIN] Session ID pasted from clipboard: 0x" << std::hex << std::uppercase << sessionId);
        return true;
    }
    
    std::string GetStatusMessage() {
        std::lock_guard<std::mutex> lock(g_StateMutex);
        return g_StatusMessage;
    }
    
    void RenderDevMenuControls() {
        ImGui::Text("Host-Join System");
        ImGui::Separator();
        
        // Show race session state status
        uint32_t raceState = 0;
        bool hasRaceState = false;
        if (g_pRaceSessionState) {
            hasRaceState = TryReadMemory((DWORD_PTR)g_pRaceSessionState, &raceState, sizeof(raceState));
        }
        
        if (hasRaceState && IsValidPointer((void*)raceState)) {
            ImGui::TextColored(ImVec4(0, 1, 0, 1), "Race State: 0x%08X", raceState);
            
            // Show network session info
            uint32_t netSession = 0;
            if (TryReadMemory((DWORD_PTR)raceState + 0x1A4, &netSession, sizeof(netSession))) {
                if (IsValidPointer((void*)netSession)) {
                    ImGui::TextColored(ImVec4(0, 1, 0, 1), "Net Session: 0x%08X", netSession);
                } else {
                    ImGui::TextColored(ImVec4(1, 1, 0, 1), "Net Session: null");
                }
            }
        } else {
            ImGui::TextColored(ImVec4(1, 1, 0, 1), "Race State: Not active");
            ImGui::Text("Enter a multiplayer lobby");
        }
        
        ImGui::Separator();
        
        // Scan button
        if (ImGui::Button("Scan for Session ID", ImVec2(200, 30))) {
            RefreshSessionId();
        }
        
        // Show current session ID if we found one
        uint32_t currentId = GetCurrentSessionId();
        if (currentId != 0) {
            ImGui::TextColored(ImVec4(0, 1, 0, 1), "Session ID:");
            ImGui::SameLine();
            ImGui::Text("0x%08X", currentId);
            
            if (ImGui::Button("Copy to Clipboard")) {
                CopySessionIdToClipboard();
            }
            ImGui::SameLine();
            if (ImGui::Button("Save to File")) {
                SaveSessionIdToFile(currentId);
            }
        } else {
            ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "No session ID found yet");
        }
        
        ImGui::Separator();
        ImGui::Text("Join Another Session:");
        
        // Input for session ID
        ImGui::InputText("Session ID (hex)", g_SessionIdInputBuffer, sizeof(g_SessionIdInputBuffer), 
                        ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CharsUppercase);
        
        ImGui::SameLine();
        if (ImGui::Button("Paste")) {
            PasteSessionIdFromClipboard();
        }
        
        // Parse and show what we have
        if (strlen(g_SessionIdInputBuffer) > 0) {
            uint32_t inputId = std::strtoul(g_SessionIdInputBuffer, nullptr, 16);
            if (inputId != 0) {
                ImGui::Text("Parsed: 0x%08X (%u)", inputId, inputId);
                
                if (ImGui::Button("Join Session", ImVec2(200, 30))) {
                    SetTargetSessionId(inputId);
                    JoinSession();
                }
            }
        }
        
        ImGui::Separator();
        
        // Status message
        std::string status = GetStatusMessage();
        ImGui::Text("Status: %s", status.c_str());
        
        // Debug info
        if (ImGui::CollapsingHeader("Debug Info")) {
            ImGui::Text("Base Address: 0x%p", (void*)g_BaseAddress);
            ImGui::Text("Race State Ptr: 0x%p", g_pRaceSessionState);
            ImGui::Text("Current Session ID: 0x%08X", currentId);
            ImGui::Text("Target Session ID: 0x%08X", GetTargetSessionId());
        }
    }
}
