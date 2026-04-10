#pragma once

#include "model.h"

namespace Portal {
    // Run WiFiManager auto-connect (with custom params for Growatt/Shelly).
    // Blocks until WiFi is connected or the portal times out.
    // When params are saved, they are persisted to NVS via SettingsStore.
    void runProvisioning();

    // Start the always-on web UI on port 80.
    void startWebUi();

    // Let the web server handle pending clients (call from loop()).
    void tick();

    // Start the WiFiManager config portal on demand (long-press).
    void openConfigPortal();

    // Inject the latest data snapshot for the dashboard page.
    void setDataSnapshot(const ShellyData& sh, const GrowattData& gw);
}
