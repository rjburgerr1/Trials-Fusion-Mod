#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace BikeSwap {
    // ============================================================================
    // Bike ID Constants (from game data)
    // These are the byte values used internally by the game
    // ============================================================================
    
    enum class BikeType : uint8_t {
        Squid = 1,      // 125cc beginner bike
        Donkey = 2,     // 250cc medium bike  
        Pit_Viper = 3,  // 450cc pro bike
        Roach = 4,      // FMX trick bike
        Banshee = 5,    // Quad/ATV
        Turtle = 6,     // Unicycle (DLC)
        Mantis = 7,     // Helium (DLC)
        Rabbit = 8,     // Raptor (DLC - custom physics)
        // Note: Some bikes may have different IDs in different game versions
    };

    // Bike info structure
    struct BikeInfo {
        uint8_t id;
        std::string name;
        std::string internalName;
        bool available;  // Whether the bike data was found in memory
    };

    // ============================================================================
    // Initialization & Shutdown
    // ============================================================================
    
    // Initialize the bike swap system with the game's base address
    bool Initialize(uintptr_t baseAddress);

    // Shutdown and cleanup
    void Shutdown();

    // Check if system is initialized
    bool IsInitialized();

    // ============================================================================
    // Bike Information Functions
    // ============================================================================

    // Get a list of all available bikes
    std::vector<BikeInfo> GetAvailableBikes();

    // Get the current bike ID
    uint8_t GetCurrentBikeId();

    // Get the current bike name
    std::string GetCurrentBikeName();

    // Get bike name from ID
    std::string GetBikeNameFromId(uint8_t bikeId);

    // ============================================================================
    // Bike Swap Functions
    // ============================================================================

    // Set the bike type (changes bike ID, reloads settings AND mesh)
    // This uses the game's ChangeBikeWithMeshReload function for full swap
    // Returns true on success
    bool SetBike(BikeType bike);

    // Set bike by ID (for custom/unlisted bikes)
    // This will attempt full mesh reload, falling back to physics-only if needed
    bool SetBikeById(uint8_t bikeId);

    // Cycle to next bike (with full mesh reload)
    bool CycleNextBike();

    // Cycle to previous bike (with full mesh reload)
    bool CyclePreviousBike();

    // Reload current bike settings only (physics, no mesh change)
    bool ReloadBikeSettings();

    // ============================================================================
    // Advanced Functions
    // ============================================================================

    // Get the bike data pointer for a given bike ID
    // Returns nullptr if bike not found
    void* GetBikeDataPointer(uint8_t bikeId);

    // Force full bike reinitialization (includes visual/mesh reload)
    // This calls ChangeBikeWithMeshReload with the current bike ID
    bool ForceFullBikeReload();

    // Debug: dump current bike state
    void DebugDumpBikeState();

    // ============================================================================
    // Hotkey Handler
    // ============================================================================

    // Check and handle hotkeys for bike swapping
    // Default hotkeys:
    //   [ - Cycle to previous bike
    //   ] - Cycle to next bike  
    //   \ - Debug dump bike state
    void CheckHotkey();
}
