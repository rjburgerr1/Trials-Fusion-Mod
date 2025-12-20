# Pause System - Address Translation Reference

## Ghidra Base Address
- Base: `0x700000`

## Address Translation Formula
```
RVA = Ghidra_Address - 0x700000
Actual_Address = trials_fusion.exe_base + RVA
```

## Function Addresses

### PauseGameCallback
- **Ghidra Address**: `0xb67b40`
- **RVA Calculation**: `0xb67b40 - 0x700000 = 0x467b40`
- **Usage**: `baseAddress + 0x467b40`

### ResumeGameCallback
- **Ghidra Address**: `0xb67b60`
- **RVA Calculation**: `0xb67b60 - 0x700000 = 0x467b60`
- **Usage**: `baseAddress + 0x467b60`

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
    *(byte*)(inGameServicePtr + 0x2e8) = 1;
    FUN_00b676c0(inGameServicePtr);
}
```

### ResumeGameCallback
```cpp
void __fastcall ResumeGameCallback(void* inGameServicePtr)
{
    // Sets pause flag at offset +0x2e8 to 0
    *(byte*)(inGameServicePtr + 0x2e8) = 0;
    FUN_00b676c0(inGameServicePtr);
}
```

## How to Use

1. Get the base address of `trials_fusion.exe` in memory
2. Add the RVA to get the actual function address
3. Call with a pointer to the InGameService instance

Example:
```cpp
uintptr_t baseAddress = GetModuleBaseAddress(...);
auto pauseCallback = (void(__fastcall*)(void*))(baseAddress + 0x467b40);
void* servicePtr = GetInGameServicePointer();
pauseCallback(servicePtr);
```

## InGameService Pointer Location

The InGameService instance pointer is stored in a global structure:
- Access the global structure at `baseAddress + 0x104b308`
- Dereference to get the pointer to the service instance
- This pointer is then passed to the pause/resume callbacks

## Verification

When the pause system initializes, it will log the RVAs being used:
```
[Pause] - PauseGameCallback RVA: 0x467b40
[Pause] - ResumeGameCallback RVA: 0x467b60
[Pause] - Global struct RVA: 0x104b308
```
