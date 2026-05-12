#include "pch.h"
#include "saved-games-refresh.h"
#include "base-address.h"
#include "keybindings.h"
#include "logging.h"
#include <Windows.h>

namespace SavedGamesRefresh {
    static constexpr uintptr_t GAME_MANAGER_PTR_RVA_UPLAY = 0x104b308;
    static constexpr uintptr_t GAME_MANAGER_PTR_RVA_STEAM = 0x104d308;

    static constexpr uintptr_t USER_TRACK_CATALOG_OFFSET = 0x130;

    // Ghidra: RebuildUserTrackCatalogFromMountedFiles @ 0x00180d90, Steam base 0x00140000.
    static constexpr uintptr_t REBUILD_USER_TRACK_CATALOG_RVA_STEAM = 0x40d90;
    static constexpr uintptr_t REBUILD_USER_TRACK_CATALOG_RVA_UPLAY = 0;

    typedef void(__fastcall* RebuildUserTrackCatalogFunc)(void* catalogManager);

    static bool g_initialized = false;
    static uintptr_t g_baseAddress = 0;
    static void** g_gameManagerPtr = nullptr;
    static RebuildUserTrackCatalogFunc g_rebuildUserTrackCatalog = nullptr;

    static uintptr_t GetGameManagerPtrRVA() {
        return BaseAddress::IsSteamVersion() ? GAME_MANAGER_PTR_RVA_STEAM : GAME_MANAGER_PTR_RVA_UPLAY;
    }

    static uintptr_t GetRebuildUserTrackCatalogRVA() {
        return BaseAddress::IsSteamVersion() ? REBUILD_USER_TRACK_CATALOG_RVA_STEAM : REBUILD_USER_TRACK_CATALOG_RVA_UPLAY;
    }

    static void* GetGameManager() {
        if (!g_gameManagerPtr || IsBadReadPtr(g_gameManagerPtr, sizeof(void*))) {
            return nullptr;
        }

        void* gameManager = *g_gameManagerPtr;
        if (!gameManager || IsBadReadPtr(gameManager, 0x200)) {
            return nullptr;
        }

        return gameManager;
    }

    static void* GetUserTrackCatalogManager() {
        void* gameManager = GetGameManager();
        if (!gameManager) {
            return nullptr;
        }

        uintptr_t catalogPtrAddr = reinterpret_cast<uintptr_t>(gameManager) + USER_TRACK_CATALOG_OFFSET;
        if (IsBadReadPtr(reinterpret_cast<void*>(catalogPtrAddr), sizeof(void*))) {
            return nullptr;
        }

        void* catalogManager = *reinterpret_cast<void**>(catalogPtrAddr);
        if (!catalogManager || IsBadReadPtr(catalogManager, 0x100)) {
            return nullptr;
        }

        return catalogManager;
    }

    static bool CallRebuildCatalog_Inner(void* catalogManager, DWORD* exceptionCode) {
        *exceptionCode = 0;

        __try {
            g_rebuildUserTrackCatalog(catalogManager);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            *exceptionCode = GetExceptionCode();
            return false;
        }
    }

    bool Initialize(uintptr_t baseAddress) {
        if (g_initialized) {
            LOG_WARNING("[SavedGamesRefresh] Already initialized");
            return true;
        }

        if (baseAddress == 0) {
            LOG_ERROR("[SavedGamesRefresh] Invalid base address");
            return false;
        }

        uintptr_t rebuildRva = GetRebuildUserTrackCatalogRVA();
        if (rebuildRva == 0) {
            LOG_WARNING("[SavedGamesRefresh] User track catalog refresh address is not mapped for this game version");
            return false;
        }

        g_baseAddress = baseAddress;
        g_gameManagerPtr = reinterpret_cast<void**>(baseAddress + GetGameManagerPtrRVA());
        g_rebuildUserTrackCatalog = reinterpret_cast<RebuildUserTrackCatalogFunc>(baseAddress + rebuildRva);

        if (IsBadReadPtr(g_gameManagerPtr, sizeof(void*))) {
            LOG_ERROR("[SavedGamesRefresh] Invalid game manager pointer");
            Shutdown();
            return false;
        }

        g_initialized = true;
        LOG_INFO("[SavedGamesRefresh] Initialized. Catalog rebuild RVA=0x" << std::hex << rebuildRva << std::dec);
        return true;
    }

    void Shutdown() {
        g_initialized = false;
        g_baseAddress = 0;
        g_gameManagerPtr = nullptr;
        g_rebuildUserTrackCatalog = nullptr;
    }

    bool RefreshTrackCatalog() {
        if (!g_initialized || !g_rebuildUserTrackCatalog) {
            LOG_ERROR("[SavedGamesRefresh] Not initialized");
            return false;
        }

        void* catalogManager = GetUserTrackCatalogManager();
        if (!catalogManager) {
            LOG_ERROR("[SavedGamesRefresh] Could not resolve user track catalog manager at *(g_pGameManager+0x130)");
            return false;
        }

        LOG_INFO("[SavedGamesRefresh] Rebuilding user track catalog from mounted SavedGames files...");
        LOG_VERBOSE("[SavedGamesRefresh] Catalog manager=0x" << std::hex << reinterpret_cast<uintptr_t>(catalogManager));

        DWORD exceptionCode = 0;
        bool success = CallRebuildCatalog_Inner(catalogManager, &exceptionCode);
        if (!success) {
            LOG_ERROR("[SavedGamesRefresh] Exception while rebuilding user track catalog: 0x" << std::hex << exceptionCode);
            return false;
        }

        LOG_INFO("[SavedGamesRefresh] User track catalog rebuild completed");
        return true;
    }

    void CheckHotkey() {
        if (!g_initialized) {
            return;
        }

        if (Keybindings::IsActionPressed(Keybindings::Action::RefreshSavedGamesTracks)) {
            std::string keyName = Keybindings::GetKeyName(Keybindings::GetKey(Keybindings::Action::RefreshSavedGamesTracks));
            LOG_INFO("");
            LOG_INFO("[" << keyName << "] === REFRESHING SAVEDGAMES TRACK CATALOG ===");
            RefreshTrackCatalog();
            LOG_INFO("");
        }
    }
}
