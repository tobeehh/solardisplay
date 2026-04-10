#pragma once

#include <Arduino.h>

namespace Net {
    // Attempts WiFi.begin() using stored credentials; returns true once
    // connected. Non-blocking – call from the watchdog tick.
    bool begin();

    // Called periodically. Auto-reconnects if the link is lost.
    void tick();
}
