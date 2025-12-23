# Save States System - Implementation Summary

## Overview
Implemented a TAS-style save state system for Trials Fusion that allows you to save and load game states at any point during gameplay.

## Files Created
- **save-states.h** - Header file with public interface
- **save-states.cpp** - Implementation with memory snapshot approach

## How It Works

### Memory Snapshot Approach
1. Locates the main game state structure at offset +0xdc from the global structure
2. Copies 1MB of memory (conservative estimate) to a save slot buffer
3. Restores by copying the buffer back to game memory
4. Supports 10 save slots (0-9)

### Key Structures
- Global structure pointer at RVA 0x104b308 (same as pause system)
- Game state structure at global+0xdc
- Each slot stores 1MB of game state data

## Controls

### Quick Save/Load
- **F7** - Quick save to slot 0
- **F8** - Quick load from slot 0

### Numbered Slots (1-9)
- **SHIFT + 1-9** - Save to specific slot
- **1-9** (no shift) - Load from specific slot

## Features

### Safety Checks
- Validates base address hasn't changed between save and load
- Checks for valid game state pointer before operations
- Prevents loading from empty slots
- Bounds checking on slot numbers

### Memory Management
- Lazy allocation - only allocates memory when a slot is first used
- Proper cleanup on shutdown
- Tracks which slots are in use

### Session Validation
- Stores the base address with each save
- Prevents loading saves from previous game sessions
- This avoids crashes from stale pointer data

## Usage

1. **During Gameplay:**
   - Press F7 to quick save your current position
   - Try a difficult section
   - Press F8 to instantly restore to your save point
   
2. **Multiple Checkpoints:**
   - Use SHIFT+1 through SHIFT+9 to create multiple save points
   - Press 1-9 (no shift) to jump between them
   - Perfect for practicing different parts of a track

3. **TAS Recording:**
   - Save at key points in a run
   - Retry sections until perfect
   - Build up a flawless run piece by piece

## Technical Details

### State Size
- Currently saves 1MB per slot (conservative estimate)
- Maximum memory usage: 10MB (if all slots used)
- Includes physics state, position, velocity, checkpoint progress, etc.

### What Gets Saved
The 1MB snapshot likely includes:
- Player/bike position, velocity, rotation
- Wheel positions and physics state
- Checkpoint progress
- Time, faults counters
- Camera state
- Other game state variables

### Limitations
1. **Memory-based only** - saves are lost when game closes
2. **Same session only** - can't load saves after restarting game
3. **Fixed size** - may save more than needed (but safer)
4. **No persistence** - doesn't save to disk

## Future Improvements

### Possible Enhancements
1. **Disk persistence** - Save states to files for later use
2. **Metadata** - Store track name, time, position with each save
3. **Selective copying** - Identify exact offsets to reduce memory usage
4. **Compression** - Compress save data to reduce memory footprint
5. **UI feedback** - Visual confirmation of save/load operations

### Alternative Approaches
1. **Checkpoint exploitation** - Hook into game's existing checkpoint system
2. **Selective state** - Only save/restore specific variables
3. **Ring buffer** - Auto-save recent history for instant replay

## Integration
- Fully integrated with existing TFPayload systems
- Shares global structure pointer with pause system
- Hotkey handling in main game loop
- Proper initialization and shutdown lifecycle

## Notes
- F8 previously used for "Stop feed fetching" - now repurposed for Quick Load
- This is more valuable for gameplay than feed management
- Feed fetching can still be stopped with END key shutdown
