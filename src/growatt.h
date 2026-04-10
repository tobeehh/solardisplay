#pragma once

#include "model.h"

namespace Growatt {
    void begin();
    void tick(GrowattData& out);
    void requestRefresh();
    void logout();  // forces re-login on next tick
}
