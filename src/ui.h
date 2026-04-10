#pragma once

#include "model.h"

namespace Ui {
    void begin();

    // Periodic refresh:
    //   - advances LVGL's timer (touch, animations, widget updates)
    //   - re-renders the dashboard values at UI_REFRESH_MS cadence
    //   - drives the on-board RGB LED according to the current LED mode
    void tick(const ShellyData& sh, const GrowattData& gw);
}
