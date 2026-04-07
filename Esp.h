#pragma once
#pragma once
#include "memory.h"
#include "config.h"

class ESP {
public:
    static void Render(Memory& mem, uintptr_t client, int screen_w, int screen_h);
};