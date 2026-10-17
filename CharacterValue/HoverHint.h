#pragma once
#include "ModContract.h"

namespace HoverHint {
    void Startup(const ModHost* Host);
    void Shutdown();
    void EarlyTick(TLBSWidget* Root, TLBSWidget* Inventory);
    uintptr_t HoveredItem();
}
