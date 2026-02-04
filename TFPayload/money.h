#pragma once
#include <Windows.h>
#include <cstdint>

namespace Money {
    // Initialize money system - call this on DLL load
    bool Initialize(DWORD_PTR baseAddress);

    // Add money to player's balance (calls game function AwardMoneyToPlayer)
    // amount: Number of money units to add
    // rewardType: Type of reward (default 0)
    // NOTE: Money changes should persist locally, but may be synced with server
    bool AddToBalance(int amount, int rewardType = 0);
    
    // Directly set the balance in memory
    // NOTE: Direct memory manipulation may not trigger UI updates
    bool SetBalance(int newBalance);

    // Get current money balance
    int GetBalance();
    
    // Get total money earned
    int64_t GetTotalEarned();
    
    // Get total money spent
    int64_t GetTotalSpent();
}
