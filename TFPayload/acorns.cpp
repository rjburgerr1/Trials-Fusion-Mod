#include "pch.h"
#include "acorns.h"
#include "logging.h"
#include "base-address.h"

namespace Acorns {
    static DWORD_PTR g_BaseAddress = 0;

    // Function pointer type for AddAcornsToBalance
    // void __thiscall AddAcornsToBalance(void *this, int param_1)
    typedef void(__thiscall* AddAcornsToBalance_t)(void* thisPtr, int amount);
    
    // AddAcornsToBalance: Ghidra 0x00A17420, RVA = 0x00A17420 - 0x700000 = 0x317420
    static constexpr uintptr_t ADD_ACORNS_TO_BALANCE_RVA_UPLAY = 0x317420;
    
    // g_pGameManager: Ghidra 0x0174B308, RVA = 0x0174B308 - 0x700000 = 0x104B308
    static constexpr uintptr_t GAME_MANAGER_PTR_RVA_UPLAY = 0x104B308;

    // AddAcornsToBalance: Ghidra 0x004565F0, RVA = 0x004565F0 - 0x140000 = 0x3165F0
    static constexpr uintptr_t ADD_ACORNS_TO_BALANCE_RVA_STEAM = 0x3165F0;
    
    // g_pGameManager: Ghidra 0x0118D308, RVA = 0x0118D308 - 0x140000 = 0x104D308
    static constexpr uintptr_t GAME_MANAGER_PTR_RVA_STEAM = 0x104D308;

    // ============================================================================
    // Helper functions to get correct RVA based on detected version
    // ============================================================================
    
    static uintptr_t GetAddAcornsToBalanceRVA() {
        return BaseAddress::IsSteamVersion() ? ADD_ACORNS_TO_BALANCE_RVA_STEAM : ADD_ACORNS_TO_BALANCE_RVA_UPLAY;
    }
    
    static uintptr_t GetGameManagerPtrRVA() {
        return BaseAddress::IsSteamVersion() ? GAME_MANAGER_PTR_RVA_STEAM : GAME_MANAGER_PTR_RVA_UPLAY;
    }
    
    // GameManager + 0x1B4 = pointer to PlayerProfile (used for acorns)
    static constexpr uintptr_t PLAYER_PROFILE_OFFSET = 0x1B4;
    
    // Player profile structure offsets
    static constexpr uintptr_t ACORN_BALANCE_OFFSET = 0x50;

    // Internal SEH wrapper for calling the game function
    static bool CallAddAcornsInternal(void* playerProfilePtr, int amount, uintptr_t funcAddress) {
        __try {
            AddAcornsToBalance_t addAcorns = (AddAcornsToBalance_t)funcAddress;
            addAcorns(playerProfilePtr, amount);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    // Internal SEH wrapper for reading balance
    static bool ReadBalanceInternal(void* playerProfilePtr, int* outBalance) {
        __try {
            int* pBalance = (int*)((uintptr_t)playerProfilePtr + ACORN_BALANCE_OFFSET);
            *outBalance = *pBalance;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }
    
    // Internal SEH wrapper for writing balance directly
    static bool WriteBalanceInternal(void* playerProfilePtr, int newBalance) {
        __try {
            int* pBalance = (int*)((uintptr_t)playerProfilePtr + ACORN_BALANCE_OFFSET);
            *pBalance = newBalance;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    // SEH-safe memory read helpers (no C++ objects allowed)
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

    // Internal function for reading pointers with debug info
    // Based on GrantUplayReward decompilation:
    //   AddAcornsToBalance(*(void **)(g_pGameManager + 0x1b4), amount)
    // So: g_pGameManager is a direct pointer to GameManager, and +0x1B4 is PlayerProfile ptr
    static void* GetPlayerProfilePtrInternal(uintptr_t* outGameManager = nullptr) {
        if (g_BaseAddress == 0) {
            LOG_ERROR("[Acorns] g_BaseAddress is 0");
            return nullptr;
        }

        // Calculate address of g_pGameManager using version-aware RVA
        uintptr_t gameManagerPtrRVA = GetGameManagerPtrRVA();
        uintptr_t* pGameManagerPtr = (uintptr_t*)(g_BaseAddress + gameManagerPtrRVA);
        
        LOG_VERBOSE("[Acorns] g_BaseAddress: 0x" << std::hex << g_BaseAddress);
        LOG_VERBOSE("[Acorns] GameManagerPtr RVA: 0x" << std::hex << gameManagerPtrRVA);
        LOG_VERBOSE("[Acorns] pGameManagerPtr address: 0x" << std::hex << (uintptr_t)pGameManagerPtr);
        
        // Read GameManager pointer
        bool success = false;
        uintptr_t gameManager = SafeReadPtr(pGameManagerPtr, &success);
        if (!success) {
            LOG_ERROR("[Acorns] Exception reading GameManager pointer");
            return nullptr;
        }
        
        LOG_VERBOSE("[Acorns] GameManager value: 0x" << std::hex << gameManager);
        
        if (outGameManager) *outGameManager = gameManager;
        
        if (gameManager == 0) {
            LOG_WARNING("[Acorns] GameManager is NULL");
            return nullptr;
        }

        // Read the PlayerProfile pointer at GameManager + 0x1B4
        void** pPlayerProfile = (void**)(gameManager + PLAYER_PROFILE_OFFSET);
        LOG_VERBOSE("[Acorns] pPlayerProfile address: 0x" << std::hex << (uintptr_t)pPlayerProfile);
        
        void* playerProfile = SafeReadVoidPtr(pPlayerProfile, &success);
        if (!success) {
            LOG_ERROR("[Acorns] Exception reading PlayerProfile pointer");
            return nullptr;
        }
        
        LOG_VERBOSE("[Acorns] PlayerProfile value: 0x" << std::hex << (uintptr_t)playerProfile);
        
        return playerProfile;
    }

    bool Initialize(DWORD_PTR baseAddress) {
        g_BaseAddress = baseAddress;
        
        // Log version detection
        if (BaseAddress::IsSteamVersion()) {
            LOG_VERBOSE("[Acorns] Steam version detected - using Steam addresses");
            LOG_VERBOSE("[Acorns]   AddAcornsToBalance RVA: 0x" << std::hex << ADD_ACORNS_TO_BALANCE_RVA_STEAM);
            LOG_VERBOSE("[Acorns]   g_pGameManager RVA: 0x" << std::hex << GAME_MANAGER_PTR_RVA_STEAM);
        } else {
            LOG_VERBOSE("[Acorns] Uplay version detected - using Uplay addresses");
            LOG_VERBOSE("[Acorns]   AddAcornsToBalance RVA: 0x" << std::hex << ADD_ACORNS_TO_BALANCE_RVA_UPLAY);
            LOG_VERBOSE("[Acorns]   g_pGameManager RVA: 0x" << std::hex << GAME_MANAGER_PTR_RVA_UPLAY);
        }
        
        LOG_VERBOSE("[Acorns] Initialized with base address: 0x" << std::hex << baseAddress);
        return true;
    }

    bool AddToBalance(int amount) {
        if (g_BaseAddress == 0) {
            LOG_ERROR("[Acorns] Not initialized! Call Initialize() first.");
            return false;
        }

        // Get function address using version-aware RVA
        uintptr_t funcAddress = g_BaseAddress + GetAddAcornsToBalanceRVA();

        // Get the player profile pointer via GameManager
        uintptr_t gameManager = 0;
        void* playerProfilePtr = GetPlayerProfilePtrInternal(&gameManager);
        
        if (playerProfilePtr == nullptr) {
            LOG_ERROR("[Acorns] Player profile pointer not found - are you in-game?");
            return false;
        }

        // Debug: Read balance before
        int balanceBefore = -1;
        ReadBalanceInternal(playerProfilePtr, &balanceBefore);

        LOG_VERBOSE("[Acorns] GameManager: 0x" << std::hex << gameManager);
        LOG_VERBOSE("[Acorns] PlayerProfile: 0x" << std::hex << (uintptr_t)playerProfilePtr);
        LOG_VERBOSE("[Acorns] Balance BEFORE: " << std::dec << balanceBefore);
        LOG_VERBOSE("[Acorns] Adding " << amount << " acorns...");
        LOG_VERBOSE("[Acorns] Calling function at 0x" << std::hex << funcAddress);

        if (CallAddAcornsInternal(playerProfilePtr, amount, funcAddress)) {
            // Debug: Read balance after
            int balanceAfter = -1;
            ReadBalanceInternal(playerProfilePtr, &balanceAfter);
            
            LOG_VERBOSE("[Acorns] Balance AFTER: " << std::dec << balanceAfter);
            LOG_VERBOSE("[Acorns] Difference: " << (balanceAfter - balanceBefore));
            LOG_VERBOSE("[Acorns] NOTE: Acorns are server-synced. Changes may not persist after server sync.");
            return true;
        } else {
            LOG_ERROR("[Acorns] Exception occurred while adding acorns!");
            return false;
        }
    }
    
    bool SetBalance(int newBalance) {
        if (g_BaseAddress == 0) {
            LOG_ERROR("[Acorns] Not initialized! Call Initialize() first.");
            return false;
        }

        void* playerProfilePtr = GetPlayerProfilePtrInternal();
        
        if (playerProfilePtr == nullptr) {
            LOG_ERROR("[Acorns] Player profile pointer not found - are you in-game?");
            return false;
        }

        int balanceBefore = -1;
        ReadBalanceInternal(playerProfilePtr, &balanceBefore);
        
        LOG_VERBOSE("[Acorns] Setting balance from " << std::dec << balanceBefore << " to " << newBalance);

        if (WriteBalanceInternal(playerProfilePtr, newBalance)) {
            int balanceAfter = -1;
            ReadBalanceInternal(playerProfilePtr, &balanceAfter);
            LOG_VERBOSE("[Acorns] Balance set to: " << std::dec << balanceAfter);
            LOG_VERBOSE("[Acorns] NOTE: Acorns are server-synced. Changes may not persist.");
            return true;
        } else {
            LOG_ERROR("[Acorns] Exception writing balance!");
            return false;
        }
    }

    int GetBalance() {
        if (g_BaseAddress == 0) {
            LOG_ERROR("[Acorns] Not initialized!");
            return -1;
        }

        void* playerProfilePtr = GetPlayerProfilePtrInternal();
        
        if (playerProfilePtr == nullptr) {
            LOG_WARNING("[Acorns] Player profile not available - are you in-game?");
            return -1;
        }

        int balance = -1;
        if (ReadBalanceInternal(playerProfilePtr, &balance)) {
            LOG_VERBOSE("[Acorns] Current balance: " << std::dec << balance);
            return balance;
        } else {
            LOG_ERROR("[Acorns] Exception reading balance!");
            return -1;
        }
    }
}
