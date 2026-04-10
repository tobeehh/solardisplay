#pragma once

#include <Arduino.h>
#include "model.h"

// Persistent settings backed by NVS (Preferences).
// All values are user-editable via the web portal / WiFiManager.
struct Settings {
    String   growattUser;
    String   growattPass;
    String   shellyHost;
    uint16_t flowDeadbandW;
    String   adminPass;    // Basic-Auth password for the web UI
    LedMode  ledMode;      // behaviour of the single RGB status LED
};

namespace SettingsStore {
    // Load from NVS; fills defaults for missing keys, generating a random
    // admin password on first boot (also persisted).
    void begin();

    // Current cached settings (read-only reference).
    const Settings& get();

    // Replace all fields at once and persist.
    void save(const Settings& s);

    // Convenience helpers for individual updates.
    void setGrowattCredentials(const String& user, const String& pass);
    void setShellyHost(const String& host);
    void setFlowDeadband(uint16_t watts);
    void setAdminPass(const String& pass);
    void setLedMode(LedMode mode);

    // Wipe NVS namespace.
    void factoryReset();

    // True if Growatt + Shelly host are both non-empty.
    bool isConfigured();
}
