#pragma once

#include <cstdint>

namespace SavedGamesRefresh {
    bool Initialize(uintptr_t baseAddress);
    void Shutdown();
    bool RefreshTrackCatalog();
    void CheckHotkey();
}
