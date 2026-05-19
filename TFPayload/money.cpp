#include "pch.h"
#include "money.h"
#include "logging.h"
#include "base-address.h"

namespace Money {
    static DWORD_PTR g_BaseAddress = 0;

    // Function pointer type for AwardMoneyToPlayer
    // void __thiscall AwardMoneyToPlayer(void *this, uint amount, uint rewardType, uint param_3)
    typedef void(__thiscall* AwardMoneyToPlayer_t)(void* thisPtr, uint32_t amount, uint32_t rewardType, uint32_t param_3);
    typedef void(__thiscall* SaveMoneyStatistics_t)(void* thisPtr);
    typedef void(__thiscall* BuildCareerStatsSnapshot_t)(void* thisPtr, char useSecondarySnapshot);
    
    // ============================================================================
    // UPLAY VERSION ADDRESSES (Ghidra base 0x700000)
    // ============================================================================
    
    // AwardMoneyToPlayer: Ghidra 0x00A09B30, RVA = 0x00A09B30 - 0x700000 = 0x309B30
    static constexpr uintptr_t AWARD_MONEY_TO_PLAYER_RVA_UPLAY = 0x309B30;

    // SaveMoneyStatistics: Ghidra 0x00A09530, RVA = 0x00A09530 - 0x700000 = 0x309530
    static constexpr uintptr_t SAVE_MONEY_STATISTICS_RVA_UPLAY = 0x309530;

    // BuildCareerStatsSnapshot: Ghidra 0x00A70910, RVA = 0x00A70910 - 0x700000 = 0x370910
    static constexpr uintptr_t BUILD_CAREER_STATS_SNAPSHOT_RVA_UPLAY = 0x370910;
    
    // g_pGameManager: Ghidra 0x0174B308, RVA = 0x0174B308 - 0x700000 = 0x104B308
    static constexpr uintptr_t GAME_MANAGER_PTR_RVA_UPLAY = 0x104B308;

    // ============================================================================
    // STEAM VERSION ADDRESSES (Ghidra base 0x140000)
    // ============================================================================
    
    // AwardMoneyToPlayer Steam: mapped from reverse_engineering_docs/uplay-to-steam-function-map.csv
    // Uplay 0x00A09B30 -> Steam 0x00448DC0, RVA = 0x00448DC0 - 0x00140000 = 0x00308DC0
    static constexpr uintptr_t AWARD_MONEY_TO_PLAYER_RVA_STEAM = 0x308DC0;

    // SaveMoneyStatistics: exact function match, Steam 0x004487C0 - 0x00140000 = 0x003087C0
    static constexpr uintptr_t SAVE_MONEY_STATISTICS_RVA_STEAM = 0x3087C0;

    // BuildCareerStatsSnapshot: implied match, Steam 0x004B0260 - 0x00140000 = 0x00370260
    static constexpr uintptr_t BUILD_CAREER_STATS_SNAPSHOT_RVA_STEAM = 0x370260;
    
    // g_pGameManager Steam: Ghidra 0x0118D308, RVA = 0x0118D308 - 0x140000 = 0x104D308
    static constexpr uintptr_t GAME_MANAGER_PTR_RVA_STEAM = 0x104D308;

    // ============================================================================
    // Helper functions to get correct RVA based on detected version
    // ============================================================================
    
    static uintptr_t GetAwardMoneyToPlayerRVA() {
        return BaseAddress::IsSteamVersion() ? AWARD_MONEY_TO_PLAYER_RVA_STEAM : AWARD_MONEY_TO_PLAYER_RVA_UPLAY;
    }
    
    static uintptr_t GetGameManagerPtrRVA() {
        return BaseAddress::IsSteamVersion() ? GAME_MANAGER_PTR_RVA_STEAM : GAME_MANAGER_PTR_RVA_UPLAY;
    }

    static uintptr_t GetSaveMoneyStatisticsRVA() {
        return BaseAddress::IsSteamVersion() ? SAVE_MONEY_STATISTICS_RVA_STEAM : SAVE_MONEY_STATISTICS_RVA_UPLAY;
    }

    static uintptr_t GetBuildCareerStatsSnapshotRVA() {
        return BaseAddress::IsSteamVersion() ? BUILD_CAREER_STATS_SNAPSHOT_RVA_STEAM : BUILD_CAREER_STATS_SNAPSHOT_RVA_UPLAY;
    }
    
    // ============================================================================
    // Player Stats Offsets (same for both versions based on struct layout)
    // From GrantUplayReward: *(void **)(*(int *)(g_pGameManager + 0x120) + 8)
    // ============================================================================
    
    // GameManager + 0x120 = pointer to player stats manager
    static constexpr uintptr_t PLAYER_STATS_MANAGER_OFFSET = 0x120;
    
    // Player stats manager + 0x8 = pointer to money stats object
    static constexpr uintptr_t MONEY_STATS_OBJECT_OFFSET = 0x8;
    
    // Money stats structure offsets (from AwardMoneyToPlayer decompilation)
    static constexpr uintptr_t MONEY_BALANCE_OFFSET = 0x98;       // Current balance (64-bit)
    static constexpr uintptr_t MONEY_TOTAL_EARNED_OFFSET = 0x88;  // Total earned (64-bit)
    static constexpr uintptr_t MONEY_TOTAL_SPENT_OFFSET = 0x90;   // Total spent (64-bit)

    // GameManager + 0x178 = career stats/snapshot owner used by AwardMoneyToPlayer
    static constexpr uintptr_t CAREER_STATS_OWNER_OFFSET = 0x178;

    // ============================================================================
    // SEH-safe memory access helpers
    // ============================================================================
    
    static uintptr_t SafeReadPtr(uintptr_t* address, bool* success) {
        __try {
            *success = true;
            return *address;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            *success = false;
            return 0;
        }
    }
    
    static void* SafeReadVoidPtr(void** address, bool* success) {
        __try {
            *success = true;
            return *address;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            *success = false;
            return nullptr;
        }
    }

    static bool CallAwardMoneyInternal(void* moneyStatsPtr, uint32_t amount, uint32_t rewardType, uintptr_t funcAddress) {
        __try {
            AwardMoneyToPlayer_t awardMoney = (AwardMoneyToPlayer_t)funcAddress;
            awardMoney(moneyStatsPtr, amount, rewardType, 0);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    static bool CallSaveMoneyStatisticsInternal(void* moneyStatsPtr, uintptr_t funcAddress) {
        __try {
            SaveMoneyStatistics_t saveMoneyStatistics = (SaveMoneyStatistics_t)funcAddress;
            saveMoneyStatistics(moneyStatsPtr);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    static bool CallBuildCareerStatsSnapshotInternal(void* careerStatsOwnerPtr, uintptr_t funcAddress) {
        __try {
            BuildCareerStatsSnapshot_t buildCareerStatsSnapshot = (BuildCareerStatsSnapshot_t)funcAddress;
            buildCareerStatsSnapshot(careerStatsOwnerPtr, 0);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    static void RefreshSteamMoneyUiInternal(uintptr_t gameManager, void* moneyStatsPtr) {
        if (!BaseAddress::IsSteamVersion()) {
            return;
        }

        uintptr_t saveMoneyFunc = g_BaseAddress + GetSaveMoneyStatisticsRVA();
        if (CallSaveMoneyStatisticsInternal(moneyStatsPtr, saveMoneyFunc)) {
            LOG_VERBOSE("[Money] Steam refresh: SaveMoneyStatistics called at 0x" << std::hex << saveMoneyFunc);
        } else {
            LOG_WARNING("[Money] Steam refresh: SaveMoneyStatistics call failed");
        }

        bool success = false;
        void* careerStatsOwner = SafeReadVoidPtr((void**)(gameManager + CAREER_STATS_OWNER_OFFSET), &success);
        if (!success || careerStatsOwner == nullptr) {
            LOG_WARNING("[Money] Steam refresh: career stats owner unavailable");
            return;
        }

        uintptr_t snapshotFunc = g_BaseAddress + GetBuildCareerStatsSnapshotRVA();
        if (CallBuildCareerStatsSnapshotInternal(careerStatsOwner, snapshotFunc)) {
            LOG_VERBOSE("[Money] Steam refresh: BuildCareerStatsSnapshot called at 0x" << std::hex << snapshotFunc);
        } else {
            LOG_WARNING("[Money] Steam refresh: BuildCareerStatsSnapshot call failed");
        }
    }

    static bool ReadBalanceInternal(void* moneyStatsPtr, int* outBalance) {
        __try {
            // Money balance is stored as 64-bit value at offset 0x98
            // But we'll just read the low 32 bits for display purposes
            int64_t* pBalance = (int64_t*)((uintptr_t)moneyStatsPtr + MONEY_BALANCE_OFFSET);
            *outBalance = (int)(*pBalance);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }
    
    static bool WriteBalanceInternal(void* moneyStatsPtr, int newBalance) {
        __try {
            // Write to the 64-bit balance (sign-extend the int32)
            int64_t* pBalance = (int64_t*)((uintptr_t)moneyStatsPtr + MONEY_BALANCE_OFFSET);
            *pBalance = (int64_t)newBalance;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    static bool AddBalanceFieldsInternal(void* moneyStatsPtr, int amount) {
        __try {
            int64_t* pBalance = (int64_t*)((uintptr_t)moneyStatsPtr + MONEY_BALANCE_OFFSET);
            int64_t* pTotalEarned = (int64_t*)((uintptr_t)moneyStatsPtr + MONEY_TOTAL_EARNED_OFFSET);

            *pBalance += amount;
            *pTotalEarned += amount;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }
    
    static bool ReadTotalEarnedInternal(void* moneyStatsPtr, int64_t* outValue) {
        __try {
            int64_t* pValue = (int64_t*)((uintptr_t)moneyStatsPtr + MONEY_TOTAL_EARNED_OFFSET);
            *outValue = *pValue;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }
    
    static bool ReadTotalSpentInternal(void* moneyStatsPtr, int64_t* outValue) {
        __try {
            int64_t* pValue = (int64_t*)((uintptr_t)moneyStatsPtr + MONEY_TOTAL_SPENT_OFFSET);
            *outValue = *pValue;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    // ============================================================================
    // Get money stats object pointer
    // Based on: *(void **)(*(int *)(g_pGameManager + 0x120) + 8)
    // ============================================================================
    
    static void* GetMoneyStatsPtrInternal(uintptr_t* outGameManager = nullptr) {
        if (g_BaseAddress == 0) {
            LOG_ERROR("[Money] g_BaseAddress is 0");
            return nullptr;
        }

        // Calculate address of g_pGameManager using version-aware RVA
        uintptr_t gameManagerPtrRVA = GetGameManagerPtrRVA();
        uintptr_t* pGameManagerPtr = (uintptr_t*)(g_BaseAddress + gameManagerPtrRVA);
        
        LOG_VERBOSE("[Money] g_BaseAddress: 0x" << std::hex << g_BaseAddress);
        LOG_VERBOSE("[Money] GameManagerPtr RVA: 0x" << std::hex << gameManagerPtrRVA);
        LOG_VERBOSE("[Money] pGameManagerPtr address: 0x" << std::hex << (uintptr_t)pGameManagerPtr);
        
        // Read GameManager pointer
        bool success = false;
        uintptr_t gameManager = SafeReadPtr(pGameManagerPtr, &success);
        if (!success) {
            LOG_ERROR("[Money] Exception reading GameManager pointer");
            return nullptr;
        }
        
        LOG_VERBOSE("[Money] GameManager value: 0x" << std::hex << gameManager);
        
        if (outGameManager) *outGameManager = gameManager;
        
        if (gameManager == 0) {
            LOG_WARNING("[Money] GameManager is NULL");
            return nullptr;
        }

        // Read the player stats manager pointer at GameManager + 0x120
        void** pPlayerStatsManager = (void**)(gameManager + PLAYER_STATS_MANAGER_OFFSET);
        LOG_VERBOSE("[Money] pPlayerStatsManager address: 0x" << std::hex << (uintptr_t)pPlayerStatsManager);
        
        void* playerStatsManager = SafeReadVoidPtr(pPlayerStatsManager, &success);
        if (!success) {
            LOG_ERROR("[Money] Exception reading PlayerStatsManager pointer");
            return nullptr;
        }
        
        LOG_VERBOSE("[Money] PlayerStatsManager value: 0x" << std::hex << (uintptr_t)playerStatsManager);
        
        if (playerStatsManager == nullptr) {
            LOG_WARNING("[Money] PlayerStatsManager is NULL");
            return nullptr;
        }

        // Read the money stats object pointer at PlayerStatsManager + 0x8
        void** pMoneyStats = (void**)((uintptr_t)playerStatsManager + MONEY_STATS_OBJECT_OFFSET);
        LOG_VERBOSE("[Money] pMoneyStats address: 0x" << std::hex << (uintptr_t)pMoneyStats);
        
        void* moneyStats = SafeReadVoidPtr(pMoneyStats, &success);
        if (!success) {
            LOG_ERROR("[Money] Exception reading MoneyStats pointer");
            return nullptr;
        }
        
        LOG_VERBOSE("[Money] MoneyStats value: 0x" << std::hex << (uintptr_t)moneyStats);
        
        return moneyStats;
    }

    bool Initialize(DWORD_PTR baseAddress) {
        g_BaseAddress = baseAddress;
        
        // Log version detection
        if (BaseAddress::IsSteamVersion()) {
            LOG_VERBOSE("[Money] Steam version detected - using mapped Steam addresses");
            LOG_VERBOSE("[Money]   AwardMoneyToPlayer RVA: 0x" << std::hex << AWARD_MONEY_TO_PLAYER_RVA_STEAM);
            LOG_VERBOSE("[Money]   g_pGameManager RVA: 0x" << std::hex << GAME_MANAGER_PTR_RVA_STEAM);
        } else {
            LOG_VERBOSE("[Money] Uplay version detected - using Uplay addresses");
            LOG_VERBOSE("[Money]   AwardMoneyToPlayer RVA: 0x" << std::hex << AWARD_MONEY_TO_PLAYER_RVA_UPLAY);
            LOG_VERBOSE("[Money]   g_pGameManager RVA: 0x" << std::hex << GAME_MANAGER_PTR_RVA_UPLAY);
        }
        
        LOG_VERBOSE("[Money] Initialized with base address: 0x" << std::hex << baseAddress);
        return true;
    }

    bool AddToBalance(int amount, int rewardType) {
        if (g_BaseAddress == 0) {
            LOG_ERROR("[Money] Not initialized! Call Initialize() first.");
            return false;
        }

        if (amount <= 0) {
            LOG_WARNING("[Money] AwardMoneyToPlayer only supports positive amounts. Use SetBalance for negative adjustments.");
            return false;
        }

        // Get function address using version-aware RVA
        uintptr_t funcAddress = g_BaseAddress + GetAwardMoneyToPlayerRVA();

        // Get the money stats pointer
        uintptr_t gameManager = 0;
        void* moneyStatsPtr = GetMoneyStatsPtrInternal(&gameManager);
        
        if (moneyStatsPtr == nullptr) {
            LOG_ERROR("[Money] Money stats pointer not found - are you in-game?");
            return false;
        }

        // Debug: Read balance before
        int balanceBefore = -1;
        ReadBalanceInternal(moneyStatsPtr, &balanceBefore);

        LOG_VERBOSE("[Money] GameManager: 0x" << std::hex << gameManager);
        LOG_VERBOSE("[Money] MoneyStats: 0x" << std::hex << (uintptr_t)moneyStatsPtr);
        LOG_VERBOSE("[Money] Balance BEFORE: " << std::dec << balanceBefore);
        LOG_VERBOSE("[Money] Adding " << amount << " money (rewardType=" << rewardType << ")...");
        LOG_VERBOSE("[Money] Calling function at 0x" << std::hex << funcAddress);

        // The Steam CSV maps AwardMoneyToPlayer as an implied match, but this function has
        // many duplicate instruction matches and has proven unreliable in practice. Prefer
        // the already-resolved money stats object on Steam so the dev button actually moves
        // the balance instead of depending on a fragile call target.
        if (BaseAddress::IsSteamVersion()) {
            LOG_WARNING("[Money] Steam path: using direct balance update because AwardMoneyToPlayer mapping is unreliable");

            if (!AddBalanceFieldsInternal(moneyStatsPtr, amount)) {
                LOG_ERROR("[Money] Steam direct balance update failed!");
                return false;
            }

            RefreshSteamMoneyUiInternal(gameManager, moneyStatsPtr);

            int balanceAfter = -1;
            ReadBalanceInternal(moneyStatsPtr, &balanceAfter);
            LOG_VERBOSE("[Money] Balance AFTER: " << std::dec << balanceAfter);
            LOG_VERBOSE("[Money] Difference: " << (balanceAfter - balanceBefore));
            return balanceBefore < 0 || balanceAfter == balanceBefore + amount;
        }

        bool callSucceeded = CallAwardMoneyInternal(moneyStatsPtr, (uint32_t)amount, (uint32_t)rewardType, funcAddress);
        if (callSucceeded) {
            // Debug: Read balance after
            int balanceAfter = -1;
            ReadBalanceInternal(moneyStatsPtr, &balanceAfter);
            
            LOG_VERBOSE("[Money] Balance AFTER: " << std::dec << balanceAfter);
            LOG_VERBOSE("[Money] Difference: " << (balanceAfter - balanceBefore));

            if (balanceBefore >= 0 && balanceAfter == balanceBefore) {
                LOG_WARNING("[Money] AwardMoneyToPlayer returned but balance did not change; applying direct balance fallback");

                if (!AddBalanceFieldsInternal(moneyStatsPtr, amount)) {
                    LOG_ERROR("[Money] Direct balance fallback failed!");
                    return false;
                }

                int fallbackBalance = -1;
                ReadBalanceInternal(moneyStatsPtr, &fallbackBalance);
                LOG_VERBOSE("[Money] Fallback balance AFTER: " << std::dec << fallbackBalance);
            }

            return true;
        } else {
            LOG_ERROR("[Money] Exception occurred while calling AwardMoneyToPlayer!");

            if (!BaseAddress::IsSteamVersion()) {
                return false;
            }

            LOG_WARNING("[Money] Steam fallback: applying direct balance update after mapped function call failed");
            if (!AddBalanceFieldsInternal(moneyStatsPtr, amount)) {
                LOG_ERROR("[Money] Steam direct balance fallback failed!");
                return false;
            }

            int fallbackBalance = -1;
            ReadBalanceInternal(moneyStatsPtr, &fallbackBalance);
            LOG_VERBOSE("[Money] Steam fallback balance AFTER: " << std::dec << fallbackBalance);
            return true;
        }
    }
    
    bool SetBalance(int newBalance) {
        if (g_BaseAddress == 0) {
            LOG_ERROR("[Money] Not initialized! Call Initialize() first.");
            return false;
        }

        void* moneyStatsPtr = GetMoneyStatsPtrInternal();
        
        if (moneyStatsPtr == nullptr) {
            LOG_ERROR("[Money] Money stats pointer not found - are you in-game?");
            return false;
        }

        int balanceBefore = -1;
        ReadBalanceInternal(moneyStatsPtr, &balanceBefore);
        
        LOG_VERBOSE("[Money] Setting balance from " << std::dec << balanceBefore << " to " << newBalance);

        if (WriteBalanceInternal(moneyStatsPtr, newBalance)) {
            int balanceAfter = -1;
            ReadBalanceInternal(moneyStatsPtr, &balanceAfter);
            LOG_VERBOSE("[Money] Balance set to: " << std::dec << balanceAfter);
            LOG_WARNING("[Money] NOTE: Direct memory changes may not update UI. Consider using AddToBalance instead.");
            return true;
        } else {
            LOG_ERROR("[Money] Exception writing balance!");
            return false;
        }
    }

    int GetBalance() {
        if (g_BaseAddress == 0) {
            LOG_ERROR("[Money] Not initialized!");
            return -1;
        }

        void* moneyStatsPtr = GetMoneyStatsPtrInternal();
        
        if (moneyStatsPtr == nullptr) {
            LOG_WARNING("[Money] Money stats not available - are you in-game?");
            return -1;
        }

        int balance = -1;
        if (ReadBalanceInternal(moneyStatsPtr, &balance)) {
            LOG_VERBOSE("[Money] Current balance: " << std::dec << balance);
            return balance;
        } else {
            LOG_ERROR("[Money] Exception reading balance!");
            return -1;
        }
    }
    
    int64_t GetTotalEarned() {
        if (g_BaseAddress == 0) {
            LOG_ERROR("[Money] Not initialized!");
            return -1;
        }

        void* moneyStatsPtr = GetMoneyStatsPtrInternal();
        
        if (moneyStatsPtr == nullptr) {
            LOG_WARNING("[Money] Money stats not available - are you in-game?");
            return -1;
        }

        int64_t totalEarned = -1;
        if (ReadTotalEarnedInternal(moneyStatsPtr, &totalEarned)) {
            LOG_VERBOSE("[Money] Total earned: " << std::dec << totalEarned);
            return totalEarned;
        } else {
            LOG_ERROR("[Money] Exception reading total earned!");
            return -1;
        }
    }
    
    int64_t GetTotalSpent() {
        if (g_BaseAddress == 0) {
            LOG_ERROR("[Money] Not initialized!");
            return -1;
        }

        void* moneyStatsPtr = GetMoneyStatsPtrInternal();
        
        if (moneyStatsPtr == nullptr) {
            LOG_WARNING("[Money] Money stats not available - are you in-game?");
            return -1;
        }

        int64_t totalSpent = -1;
        if (ReadTotalSpentInternal(moneyStatsPtr, &totalSpent)) {
            LOG_VERBOSE("[Money] Total spent: " << std::dec << totalSpent);
            return totalSpent;
        } else {
            LOG_ERROR("[Money] Exception reading total spent!");
            return -1;
        }
    }
}
