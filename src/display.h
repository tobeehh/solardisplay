#pragma once

#include "model.h"

namespace Display {
    void begin();

    // LVGL tick/timer handler. Must be called frequently from the main loop
    // (ideally every few ms) so the UI stays responsive.
    void tickLvgl();

    // Show a simple boot screen while hardware/WLAN is coming up.
    void showBoot(const char* line1, const char* line2 = nullptr);

    // Announce the config portal so the user knows how to reach it.
    void showPortal(const char* apName, const char* ip);

    // Switch to the tabbed dashboard.
    void showDashboard();

    // Show a one-line "not configured" hint on the overview.
    void showMissingConfig();

    // Update all dashboard widgets with the current live data.
    // The `page` argument is kept for API compatibility but is ignored —
    // LVGL's own tabview tracks the active tab.
    void renderPage(Page page,
                    const ShellyData& shelly,
                    const GrowattData& growatt);

    // Override backlight brightness from external callers (0..100 %).
    void setBacklightPct(uint8_t pct);
    uint8_t backlightPct();
}
