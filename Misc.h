#pragma once
#include "memory.h"

namespace Misc {
    // Renders all Misc features: radar, spectator list, custom crosshair, clock,
    // local player info panel.  Called once per frame from the main render loop.
    void Render(Memory& mem, uintptr_t client, int screen_w, int screen_h);
}
