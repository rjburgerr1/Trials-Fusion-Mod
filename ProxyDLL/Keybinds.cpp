#include "Keybinds.h"
#include <Windows.h>

bool g_ShowOverlay = true;

void UpdateKeybinds()
{
    static bool lastState = false;
    bool down = (GetAsyncKeyState(VK_F4) & 0x8000);

    if (down && !lastState)
        g_ShowOverlay = !g_ShowOverlay;

    lastState = down;
}
