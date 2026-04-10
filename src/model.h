#pragma once

#include <Arduino.h>

// Live data model shared between network clients, UI and web dashboard.
struct ShellyData {
    float    aActPower   = 0.0f;
    float    bActPower   = 0.0f;
    float    cActPower   = 0.0f;
    float    totalActPower = 0.0f;   // W; positive = import, negative = export
    float    totalEnergyKwh = 0.0f;  // kWh total consumed
    float    totalImportWh  = 0.0f;  // lifetime grid import (Wh) from EMData
    float    totalExportWh  = 0.0f;  // lifetime grid export (Wh) from EMData
    bool     hasEnergyData  = false; // true if EMData counters are available
    uint32_t lastOkMillis = 0;
    bool     hasData      = false;
    String   lastError;
};

struct GrowattData {
    float    pvPowerW       = 0.0f;  // current PV power
    float    eTodayKwh      = 0.0f;
    float    eTotalKwh      = 0.0f;
    uint32_t lastOkMillis   = 0;
    bool     loggedIn       = false;
    bool     hasData        = false;
    String   lastError;
};

enum class Page : uint8_t {
    Overview = 0,
    Growatt,
    Shelly,
    System,
    Settings,
    _Count
};

// Behaviour of the single on-board RGB LED.
enum class LedMode : uint8_t {
    Flow       = 0,  // grid export / neutral / import → green / yellow / red
    Aggregated = 1,  // red if either data source is stale, else Flow color
    Rotating   = 2,  // cycle every 2s: Shelly → Growatt → Flow
    Off        = 3
};
