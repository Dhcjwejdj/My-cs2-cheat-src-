#pragma once
#include "imgui_layer.h"
#include "config.h"

class Menu {
public:
    void Draw();
    bool IsOpen() const { return g_Config.menu_open; }
};