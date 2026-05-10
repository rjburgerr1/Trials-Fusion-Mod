#pragma once
#include <Windows.h>
#include <cstdint>

namespace Acorns {
    // Initialize acorns system - call this on DLL load
    bool Initialize(DWORD_PTR baseAddress);

    // Add acorns to player's balance (calls game function)
    // amount: Number of acorns to add (can be negative to remove)
    // NOTE: Acorns are server-synced, changes may not persist
    bool AddToBalance(int amount);
    
    // Directly set the balance in memory
    // NOTE: Acorns are server-synced, changes may not persist
    bool SetBalance(int newBalance);

    // Get current acorn balance
    int GetBalance();
}
