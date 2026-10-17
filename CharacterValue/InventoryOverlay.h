#pragma once
#include "ModContract.h"

namespace InventoryOverlay {
    void Startup(const ModHost* Host);
    void Shutdown();
    void EarlyTick(TLBSWidget* Root, const TickContext& Context);
    TLBSWidget* Window();
}
