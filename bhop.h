#pragma once
#include "memory.h"

// Bhop: FL_ONGROUND rising-edge + debounced SendInput (VK + scan code).
namespace Bhop {
    void Run(Memory& mem, uintptr_t client);
}