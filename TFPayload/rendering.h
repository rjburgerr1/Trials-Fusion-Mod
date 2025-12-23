#pragma once

#include <d3d11.h>
#include <dxgi.h>

namespace Rendering {
    bool Initialize();
    void Shutdown();
    void RenderFrame();
}
