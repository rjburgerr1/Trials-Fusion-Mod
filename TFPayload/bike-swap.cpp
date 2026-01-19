// bike-swap.cpp
// Implementation of runtime bike swapping functionality
// Strategy: Modify bike config in player settings, then simulate restart key press

#include "pch.h"
#include "bike-swap.h"
#include "respawn.h"
#include "logging.h"
#include "keybindings.h"
#include "base-address.h"
#include <Windows.h>
#include <unordered_map>

namespace BikeSwap {
    // ============================================================================
    // Game Memory Addresses (RVA offsets - subtract 0x700000 from Ghidra addresses)
    // ============================================================================

    static constexpr uintptr_t GLOBAL_STRUCT_RVA = 0x104b308;

    // Bike-related offsets within bike/rider object
    static constexpr uintptr_t BIKE_ID_OFFSET = 0x680;
    static constexpr uintptr_t BIKE_GFX_HASH_OFFSET = 0x67c;
    static constexpr uintptr_t BIKE_IS_REAL_OFFSET = 0x13c;
    
    // Player settings bike ID offset (from analysis of UpdatePlayerBikesAppearance)
    // The game reads bike ID from a lookup structure, not directly from bike+0x680
    // We need to find where the "selected bike" is stored in player profile/settings
    
    // Function RVAs
    static constexpr uintptr_t BIKE_LOOKUP_RVA = 0x19a30;
    
    // Bike database offset within global struct
    static constexpr uintptr_t BIKE_DATABASE_OFFSET = 0x118;

    // ============================================================================
    // Function Pointer Types
    // ============================================================================

    typedef void*(__thiscall* BikeLookupFunc)(void* bikeDatabase, uint8_t bikeId);

    // ============================================================================
    // Global State
    // ============================================================================

    static bool g_initialized = false;
    static uintptr_t g_baseAddress = 0;
    static void** g_globalStructPtr = nullptr;
    
    static BikeLookupFunc g_bikeLookup = nullptr;

    // Bike name lookup table
    static std::unordered_map<uint8_t, std::string> g_bikeNames = {
        {1, "Squid (125cc)"},
        {2, "Donkey (250cc)"},
        {3, "Pit Viper (450cc)"},
        {4, "Roach (FMX)"},
        {5, "Banshee (Quad)"},
        {6, "Turtle (Unicycle)"},
        {7, "Mantis (Helium)"},
        {8, "Rabbit (Raptor)"},
    };

    // Swap state
    static DWORD g_swapRequestTime = 0;
    static const DWORD SWAP_COOLDOWN_MS = 2000;  // 2 second cooldown

    // List of known valid bike IDs for cycling
    static const uint8_t g_validBikeIds[] = {1, 2, 3, 4, 5, 6, 7, 8};
    static const int g_validBikeCount = sizeof(g_validBikeIds) / sizeof(g_validBikeIds[0]);

    // ============================================================================
    // SEH-Safe Helper Functions
    // ============================================================================

    static void* SafeBikeLookup(void* bikeDatabase, uint8_t bikeId) {
        __try {
            return g_bikeLookup(bikeDatabase, bikeId);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return nullptr;
        }
    }

    // ============================================================================
    // Helper Functions
    // ============================================================================

    static void* GetBikePointer() {
        return Respawn::GetBikePointer();
    }

    static void* GetBikeDatabase() {
        if (!g_globalStructPtr || IsBadReadPtr(g_globalStructPtr, sizeof(void*))) {
            return nullptr;
        }

        void* globalStruct = *g_globalStructPtr;
        if (!globalStruct || IsBadReadPtr(globalStruct, 0x200)) {
            return nullptr;
        }

        uintptr_t dbAddr = reinterpret_cast<uintptr_t>(globalStruct) + BIKE_DATABASE_OFFSET;
        if (IsBadReadPtr((void*)dbAddr, sizeof(void*))) {
            return nullptr;
        }

        return *reinterpret_cast<void**>(dbAddr);
    }

    // Simulate pressing the Backspace key (default restart key in Trials Fusion)
    static void SimulateRestartKeyPress() {
        // Send key down
        INPUT input = {0};
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = VK_BACK;  // Backspace is default restart key
        input.ki.dwFlags = 0;
        SendInput(1, &input, sizeof(INPUT));
        
        // Small delay
        Sleep(50);
        
        // Send key up
        input.ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(1, &input, sizeof(INPUT));
    }

    // ============================================================================
    // Initialization & Shutdown
    // ============================================================================

    bool Initialize(uintptr_t baseAddress) {
        if (g_initialized) {
            LOG_VERBOSE("[BikeSwap] Already initialized");
            return true;
        }

        // Check if Steam version - skip initialization until addresses are mapped
        if (BaseAddress::IsSteamVersion()) {
            LOG_WARNING("[BikeSwap] Steam version detected - bike swap disabled (addresses not yet mapped)");
            return false;
        }

        g_baseAddress = baseAddress;

        g_globalStructPtr = reinterpret_cast<void**>(baseAddress + GLOBAL_STRUCT_RVA);
        if (IsBadReadPtr(g_globalStructPtr, sizeof(void*))) {
            LOG_ERROR("[BikeSwap] Failed to get global struct pointer");
            return false;
        }

        g_bikeLookup = reinterpret_cast<BikeLookupFunc>(baseAddress + BIKE_LOOKUP_RVA);

        if (IsBadCodePtr((FARPROC)g_bikeLookup)) {
            LOG_ERROR("[BikeSwap] Invalid BikeLookup function pointer");
            return false;
        }

        g_initialized = true;
        LOG_VERBOSE("[BikeSwap] Initialized successfully");
        LOG_INFO("[BikeSwap] NOTE: Bike swap requires manual track restart (press Backspace) after changing bike ID");

        return true;
    }

    void Shutdown() {
        g_initialized = false;
        g_baseAddress = 0;
        g_globalStructPtr = nullptr;
        g_bikeLookup = nullptr;
        g_swapRequestTime = 0;
        
        LOG_VERBOSE("[BikeSwap] Shutdown complete");
    }

    bool IsInitialized() {
        return g_initialized;
    }

    // ============================================================================
    // Bike Information Functions
    // ============================================================================

    std::vector<BikeInfo> GetAvailableBikes() {
        std::vector<BikeInfo> bikes;

        void* bikeDatabase = GetBikeDatabase();

        for (int i = 0; i < g_validBikeCount; i++) {
            uint8_t id = g_validBikeIds[i];
            BikeInfo info;
            info.id = id;
            
            auto nameIt = g_bikeNames.find(id);
            if (nameIt != g_bikeNames.end()) {
                info.name = nameIt->second;
            } else {
                info.name = "Unknown Bike " + std::to_string(id);
            }
            
            info.internalName = "Bike_" + std::to_string(id);
            
            if (bikeDatabase && g_bikeLookup) {
                void* bikeData = SafeBikeLookup(bikeDatabase, id);
                info.available = (bikeData != nullptr);
            } else {
                info.available = false;
            }
            
            bikes.push_back(info);
        }

        return bikes;
    }

    uint8_t GetCurrentBikeId() {
        void* bikePtr = GetBikePointer();
        if (!bikePtr) {
            return 0;
        }

        uintptr_t bikeIdAddr = reinterpret_cast<uintptr_t>(bikePtr) + BIKE_ID_OFFSET;
        if (IsBadReadPtr((void*)bikeIdAddr, sizeof(int))) {
            return 0;
        }

        int bikeIdFull = *reinterpret_cast<int*>(bikeIdAddr);
        return static_cast<uint8_t>(bikeIdFull);
    }

    std::string GetCurrentBikeName() {
        uint8_t id = GetCurrentBikeId();
        return GetBikeNameFromId(id);
    }

    std::string GetBikeNameFromId(uint8_t bikeId) {
        auto it = g_bikeNames.find(bikeId);
        if (it != g_bikeNames.end()) {
            return it->second;
        }
        return "Unknown (" + std::to_string(bikeId) + ")";
    }

    // ============================================================================
    // Bike Swap Functions  
    // ============================================================================

    bool SetBike(BikeType bike) {
        return SetBikeById(static_cast<uint8_t>(bike));
    }

    bool SetBikeById(uint8_t bikeId) {
        if (!g_initialized) {
            LOG_ERROR("[BikeSwap] Not initialized");
            return false;
        }

        // Check cooldown
        DWORD currentTime = GetTickCount();
        if (g_swapRequestTime != 0 && (currentTime - g_swapRequestTime) < SWAP_COOLDOWN_MS) {
            LOG_VERBOSE("[BikeSwap] Swap on cooldown, please wait");
            return false;
        }

        void* bikePtr = GetBikePointer();
        if (!bikePtr) {
            LOG_ERROR("[BikeSwap] No active bike found - are you in a track?");
            return false;
        }

        // Verify bike ID is valid
        void* bikeDatabase = GetBikeDatabase();
        if (!bikeDatabase) {
            LOG_ERROR("[BikeSwap] Could not get bike database");
            return false;
        }

        void* bikeData = SafeBikeLookup(bikeDatabase, bikeId);
        if (!bikeData) {
            LOG_ERROR("[BikeSwap] Bike ID " << (int)bikeId << " not found in database");
            return false;
        }

        uint8_t currentId = GetCurrentBikeId();
        if (currentId == bikeId) {
            LOG_VERBOSE("[BikeSwap] Already using bike ID " << (int)bikeId);
            return true;
        }

        std::string fromName = GetBikeNameFromId(currentId);
        std::string toName = GetBikeNameFromId(bikeId);
        
        LOG_INFO("[BikeSwap] Preparing swap from " << fromName << " to " << toName);

        // Write the new bike ID to the bike object
        // This changes what bike ID the game thinks we're using
        uintptr_t bikeIdAddr = reinterpret_cast<uintptr_t>(bikePtr) + BIKE_ID_OFFSET;
        if (IsBadWritePtr((void*)bikeIdAddr, sizeof(int))) {
            LOG_ERROR("[BikeSwap] Cannot write to bike ID address");
            return false;
        }
        
        *reinterpret_cast<int*>(bikeIdAddr) = static_cast<int>(bikeId);
        LOG_VERBOSE("[BikeSwap] Set bike+0x680 to " << (int)bikeId);

        // Simulate restart key press to trigger track reload
        // This causes the game to call its own bike reload logic
        LOG_INFO("[BikeSwap] Simulating restart key press...");
        SimulateRestartKeyPress();

        g_swapRequestTime = currentTime;
        LOG_INFO("[BikeSwap] Track restart triggered - bike should change to " << toName);
        
        return true;
    }

    bool CycleNextBike() {
        uint8_t currentId = GetCurrentBikeId();
        if (currentId == 0) {
            LOG_ERROR("[BikeSwap] Could not get current bike ID");
            return false;
        }

        int currentIndex = -1;
        for (int i = 0; i < g_validBikeCount; i++) {
            if (g_validBikeIds[i] == currentId) {
                currentIndex = i;
                break;
            }
        }

        int nextIndex = (currentIndex + 1) % g_validBikeCount;
        uint8_t nextId = g_validBikeIds[nextIndex];

        return SetBikeById(nextId);
    }

    bool CyclePreviousBike() {
        uint8_t currentId = GetCurrentBikeId();
        if (currentId == 0) {
            LOG_ERROR("[BikeSwap] Could not get current bike ID");
            return false;
        }

        int currentIndex = -1;
        for (int i = 0; i < g_validBikeCount; i++) {
            if (g_validBikeIds[i] == currentId) {
                currentIndex = i;
                break;
            }
        }

        int prevIndex = (currentIndex - 1 + g_validBikeCount) % g_validBikeCount;
        uint8_t prevId = g_validBikeIds[prevIndex];

        return SetBikeById(prevId);
    }

    bool ReloadBikeSettings() {
        SimulateRestartKeyPress();
        return true;
    }

    // ============================================================================
    // Advanced Functions
    // ============================================================================

    void* GetBikeDataPointer(uint8_t bikeId) {
        if (!g_initialized) {
            return nullptr;
        }

        void* bikeDatabase = GetBikeDatabase();
        if (!bikeDatabase) {
            return nullptr;
        }

        return SafeBikeLookup(bikeDatabase, bikeId);
    }

    bool ForceFullBikeReload() {
        SimulateRestartKeyPress();
        return true;
    }

    void DebugDumpBikeState() {
        LOG_INFO("");
        LOG_INFO("=== BIKE SWAP DEBUG INFO ===");
        LOG_INFO("Initialized: " << (g_initialized ? "Yes" : "No"));
        
        void* bikePtr = GetBikePointer();
        if (bikePtr) {
            LOG_INFO("Bike Pointer: 0x" << std::hex << reinterpret_cast<uintptr_t>(bikePtr));
            
            uint8_t bikeId = GetCurrentBikeId();
            LOG_INFO("Current Bike ID: " << std::dec << (int)bikeId);
            LOG_INFO("Current Bike Name: " << GetBikeNameFromId(bikeId));

            uintptr_t bikePtrAddr = reinterpret_cast<uintptr_t>(bikePtr);
            
            if (!IsBadReadPtr((void*)(bikePtrAddr + BIKE_GFX_HASH_OFFSET), sizeof(uint32_t))) {
                uint32_t gfxHash = *reinterpret_cast<uint32_t*>(bikePtrAddr + BIKE_GFX_HASH_OFFSET);
                LOG_INFO("Bike GFX Hash: 0x" << std::hex << gfxHash);
            }
            
            if (!IsBadReadPtr((void*)(bikePtrAddr + BIKE_IS_REAL_OFFSET), sizeof(uint8_t))) {
                uint8_t isReal = *reinterpret_cast<uint8_t*>(bikePtrAddr + BIKE_IS_REAL_OFFSET);
                LOG_INFO("Is Real Bike: " << (isReal ? "Yes" : "No (Ghost/Replay)"));
            }
        } else {
            LOG_INFO("Bike Pointer: NULL (not in track?)");
        }

        void* bikeDb = GetBikeDatabase();
        LOG_INFO("Bike Database: " << (bikeDb ? "0x" : "NULL") << std::hex << reinterpret_cast<uintptr_t>(bikeDb));

        LOG_INFO("");
        LOG_INFO("Available Bikes:");
        std::vector<BikeInfo> bikes = GetAvailableBikes();
        for (size_t i = 0; i < bikes.size(); i++) {
            const BikeInfo& bike = bikes[i];
            LOG_INFO("  ID " << std::dec << (int)bike.id << ": " << bike.name 
                     << (bike.available ? " [Available]" : " [Not Found]"));
        }
        
        LOG_INFO("============================");
        LOG_INFO("");
    }

    // ============================================================================
    // Hotkey Handler
    // ============================================================================

    static bool g_bracketLeftPressed = false;
    static bool g_bracketRightPressed = false;
    static bool g_backslashPressed = false;

    void CheckHotkey() {
        if (!g_initialized) {
            return;
        }

        bool bracketLeftDown = (GetAsyncKeyState(VK_OEM_4) & 0x8000) != 0;
        if (bracketLeftDown && !g_bracketLeftPressed) {
            g_bracketLeftPressed = true;
            LOG_VERBOSE("[BikeSwap] '[' pressed - Cycling to previous bike");
            CyclePreviousBike();
        } else if (!bracketLeftDown) {
            g_bracketLeftPressed = false;
        }

        bool bracketRightDown = (GetAsyncKeyState(VK_OEM_6) & 0x8000) != 0;
        if (bracketRightDown && !g_bracketRightPressed) {
            g_bracketRightPressed = true;
            LOG_VERBOSE("[BikeSwap] ']' pressed - Cycling to next bike");
            CycleNextBike();
        } else if (!bracketRightDown) {
            g_bracketRightPressed = false;
        }

        bool backslashDown = (GetAsyncKeyState(VK_OEM_5) & 0x8000) != 0;
        if (backslashDown && !g_backslashPressed) {
            g_backslashPressed = true;
            DebugDumpBikeState();
        } else if (!backslashDown) {
            g_backslashPressed = false;
        }
    }

} // namespace BikeSwap
