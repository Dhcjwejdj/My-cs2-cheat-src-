#pragma once

#include "memory.h"

class Aimbot {
public:
    static void Run(Memory& mem, uintptr_t client);
    static void Render();
};
