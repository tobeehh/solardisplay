#pragma once

#include "model.h"

namespace Shelly {
    void begin();

    // Fires an HTTP request to the configured Shelly host if the poll
    // interval has elapsed and WLAN is up. Non-blocking from the caller's
    // perspective (one request per call, ~100–500 ms HTTP latency).
    void tick(ShellyData& out);

    // Force an immediate poll on the next tick() call.
    void requestRefresh();
}
