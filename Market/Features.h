#pragma once
#include "ModContract.h"

namespace BazaarFilter {
    void Startup(const ModHost* Host);
    void Shutdown();
    void EarlyTick(TLBSWidget* Root, TickContext tickContext);
    bool* EnabledFlag();
}

namespace CharacterValue {
    void Startup(const ModHost* Host);
    void Shutdown();
    void EarlyTick(TLBSWidget* Root, TickContext tickContext);
    void Tick();
}
