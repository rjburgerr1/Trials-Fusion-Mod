# Save Physics System

A proper physics save/load system that uses Trials Fusion's native `SetBike*` functions instead of raw memory manipulation.

## Features

### Uses Native Game Functions
- **SetBikePosition** (0x936540) - Sets position at +0x58, auto-updates prev position at +0x40
- **SetBikeVelocity** (0x936570) - Sets velocity at +0x64, auto-updates prev velocity at +0x4C
- **SetBikeRotation** (0x9365a0) - Sets rotation at +0x70
- **SetBikeFieldOfView** (0x9362d0) - Sets FOV at +0x7C

### Physics State Structure
```cpp
struct BikePhysicsState {
    float position[3];          // +0x58: Current position
    float velocity[3];          // +0x64: Current velocity
    float rotation[3];          // +0x70: Rotation
    float fieldOfView;          // +0x7C: FOV
    float prevPosition[3];      // +0x40: Previous position (auto-managed)
    float prevVelocity[3];      // +0x4C: Previous velocity (auto-managed)
    uint8_t additionalData[64]; // +0x80 to +0xC0: Extra physics data
    bool isValid;
    uint64_t timestamp;
};
```

## Hotkeys

- **CTRL+F7** - Quick save physics (slot 0)
- **CTRL+F8** - Quick load physics (slot 0)
- **CTRL+SHIFT+1-9** - Save physics to slot 1-9
- **CTRL+1-9** - Load physics from slot 1-9

## API

### Initialization
```cpp
bool SavePhysics::Initialize(uintptr_t baseAddress);
void SavePhysics::Shutdown();
bool SavePhysics::IsInitialized();
```

### Save/Load
```cpp
bool SavePhysics::SavePhysics(int slot);      // Save to slot 0-9
bool SavePhysics::LoadPhysics(int slot);      // Load from slot 0-9
bool SavePhysics::QuickSavePhysics();         // Save to slot 0
bool SavePhysics::QuickLoadPhysics();         // Load from slot 0
```

### Direct State Access
```cpp
bool GetCurrentPhysicsState(BikePhysicsState& state);
bool SetPhysicsState(const BikePhysicsState& state);
```

### Slot Management
```cpp
bool HasSavedPhysics(int slot);
SlotInfo GetSlotInfo(int slot);
void ClearSlot(int slot);
void ClearAllSlots();
```

### Import/Export
```cpp
bool ExportState(int slot, void* buffer, size_t bufferSize);
bool ImportState(int slot, const void* buffer, size_t bufferSize);
size_t GetExportSize();
```

### Debugging
```cpp
bool ValidatePhysicsPointer();
void LogPhysicsState(const char* prefix = "");
void CompareStates(int slot1, int slot2);
```

## Advantages Over save-states.cpp

1. **Uses Game's Native API** - Calls the actual SetBike* functions instead of raw memcpy
2. **Handles Side Effects** - Game automatically updates previous state and internal bookkeeping
3. **Safer** - Less likely to corrupt game state
4. **More Complete** - Includes FOV and proper velocity tracking
5. **Better Structured** - Typed struct instead of raw byte array
6. **Export/Import** - Can serialize states for file storage

## Memory Layout

The system saves the following memory regions from the bike physics object:

| Offset | Size | Description | Managed By |
|--------|------|-------------|------------|
| +0x40  | 12   | Previous position (X,Y,Z) | SetBikePosition auto-update |
| +0x4C  | 12   | Previous velocity (X,Y,Z) | SetBikeVelocity auto-update |
| +0x58  | 12   | Current position (X,Y,Z) | SetBikePosition |
| +0x64  | 12   | Current velocity (X,Y,Z) | SetBikeVelocity |
| +0x70  | 12   | Rotation (X,Y,Z) | SetBikeRotation |
| +0x7C  | 4    | Field of view | SetBikeFieldOfView |
| +0x80  | 64   | Additional physics data | Direct memcpy |

Total: 128 bytes (0x80) of physics state

## Implementation Details

### Reading State
- Reads directly from memory into the BikePhysicsState structure
- Validates pointer access before reading
- Captures timestamp for each save

### Writing State
- Uses the game's SetBike* functions for primary state
- Directly writes additional data region that functions don't touch
- Exception handling for safety

### Function Pointers
Function pointers are initialized at startup:
```cpp
g_setBikePosition = (SetBikePositionFn)(baseAddress + 0x936540);
g_setBikeVelocity = (SetBikeVelocityFn)(baseAddress + 0x936570);
g_setBikeRotation = (SetBikeRotationFn)(baseAddress + 0x9365a0);
g_setBikeFieldOfView = (SetBikeFieldOfViewFn)(baseAddress + 0x9362d0);
```

## Example Usage

```cpp
// Initialize the system
SavePhysics::Initialize(baseAddress);

// Save current state
SavePhysics::SavePhysics(1);

// Load a state
SavePhysics::LoadPhysics(1);

// Get current state without saving
BikePhysicsState state;
if (SavePhysics::GetCurrentPhysicsState(state)) {
    std::cout << "Position: " << state.position[0] << ", " 
              << state.position[1] << ", " << state.position[2] << std::endl;
}

// Export for file storage
uint8_t buffer[1024];
if (SavePhysics::ExportState(1, buffer, sizeof(buffer))) {
    // Write buffer to file...
}

// Debug comparison
SavePhysics::CompareStates(1, 2);
```

## Notes

- The system maintains 10 save slots (0-9)
- Slot 0 is used for quick save/load
- States include timestamps for tracking
- All operations are protected with exception handling
- Uses the global structure at RVA 0x104b308 to find the bike object
