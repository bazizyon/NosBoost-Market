#pragma once
#include "ModContract.h"

namespace ItemEditor {
    void Startup(const ModHost* Host);
    void Shutdown();
    void EarlyTick(TLBSWidget* Root, const TickContext& Context);
    void Toggle();
    void Select(int Vnum);
    bool WantsDataTable();
}
