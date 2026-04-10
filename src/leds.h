#pragma once

#include <Arduino.h>

#include "model.h"

enum class LedColor : uint8_t {
    Off,
    Red,
    Yellow,
    Green,
    Blue,   // used during portal / provisioning
    White
};

namespace Leds {
    void begin();

    // Directly drive the on-board RGB LED to a discrete color.
    // Intended for boot/provisioning phases (where the mode-aware update()
    // is not yet applicable).
    void set(LedColor color);

    // Mode-aware update: evaluates `mode` every call, driving the on-board
    // RGB LED based on live data (for Aggregated/Flow) or a millis()-based
    // rotation (for Rotating). `deadbandW` defines the neutral-flow band.
    void update(const struct ShellyData& sh,
                const struct GrowattData& gw,
                LedMode mode,
                uint16_t deadbandW);

    // Convenience: derive Red/Yellow/Green from a "last successful update"
    // timestamp (millis-domain). 0 means "never".
    LedColor freshnessColor(uint32_t lastOkMillis);
}
