# Pause System - Address Translation Reference

## Ghidra Base Address
- Base: `0x700000`

## Address Translation Formula
```
RVA = Ghidra_Address - 0x700000
Actual_Address = trials_fusion.exe_base + RVA
```

## Function Addresses

### PauseGameCallback (CORRECTED)
- **Ghidra Address**: `0xb67950`
- **RVA Calculation**: `0xb67950 - 0x700000 = 0x467950`
- **Usage**: `baseAddress + 0x467950`

### ResumeGameCallback (CORRECTED)
- **Ghidra Address**: `0xb67960`
- **RVA Calculation**: `0xb67960 - 0x700000 = 0x467960`
- **Usage**: `baseAddress + 0x467960`

### TogglePause (Service Method - Don't call directly)
- **Ghidra Address**: `0xb67b40`
- **RVA Calculation**: `0xb67b40 - 0x700000 = 0x467b40`
- **Note**: This is a service method pointer, not the actual implementation

### Global Structure Pointer (DAT_0174b308)
- **Ghidra Address**: `0x174b308`
- **RVA Calculation**: `0x174b308 - 0x700000 = 0x104b308`
- **Usage**: `baseAddress + 0x104b308`
- **Purpose**: Points to the global game structure that contains the InGameService pointer

## Function Signatures

### PauseGameCallback
```cpp
void __fastcall PauseGameCallback(void* inGameServicePtr)
{
    // Sets pause flag at offset +0x2e8 to 1
    *(uint8_t*)(inGameServicePtr + 0x2e8) = 1;
    FUN_00b676c0(inGameServicePtr);  // Handles pause state changes
}
```

### ResumeGameCallback
```cpp
void __fastcall ResumeGameCallback(void* inGameServicePtr)
{
    // Sets pause flag at offset +0x2e8 to 0
    *(uint8_t*)(inGameServicePtr + 0x2e8) = 0;
    if (DAT_01755260 != 0) {
        FUN_010cce60(DAT_01755260);  // Additional resume logic
    }
    FUN_00b676c0(inGameServicePtr);  // Handles pause state changes
}
```

## How to Use

1. Get the base address of `trials_fusion.exe` in memory
2. Add the RVA to get the actual function address
3. Read the current pause state from `inGameServicePtr + 0x2e8`
4. Call the appropriate callback based on current state

Example:
```cpp
uintptr_t baseAddress = GetModuleBaseAddress(...);
auto pauseCallback = (void(__fastcall*)(void*))(baseAddress + 0x467950);
auto resumeCallback = (void(__fastcall*)(void*))(baseAddress + 0x467960);

void* servicePtr = GetInGameServicePointer();
uint8_t isPaused = *(uint8_t*)(servicePtr + 0x2e8);

if (isPaused) {
    resumeCallback(servicePtr);  // Resume
} else {
    pauseCallback(servicePtr);   // Pause
}
```

## InGameService Pointer Location

The InGameService instance pointer is stored in a global structure:
- Access the global structure at `baseAddress + 0x104b308`
- Dereference to get the pointer to the game state structure
- InGameService pointer is at offset +0x174 within that structure
- Pause flag is at offset +0x2e8 within InGameService

Full access pattern:
```cpp
void** globalStructPtr = (void**)(baseAddress + 0x104b308);
void* globalStruct = *globalStructPtr;
void* inGameServicePtr = *(void**)((uintptr_t)globalStruct + 0x174);
uint8_t pauseFlag = *(uint8_t*)((uintptr_t)inGameServicePtr + 0x2e8);
```

## Important Notes

- **ALWAYS** read the current pause state before toggling
- Don't track pause state manually - read it from the game
- ResumeGameCallback does more than just set the flag (calls additional functions)
- Both callbacks call FUN_00b676c0 which handles state propagation

## Verification

When the pause system initializes, it will log the RVAs being used:
```
[Pause] - PauseGameCallback RVA: 0x467950 (CORRECTED)
[Pause] - ResumeGameCallback RVA: 0x467960 (CORRECTED)
[Pause] - Global struct RVA: 0x104b308
```
