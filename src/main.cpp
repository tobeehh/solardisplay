// ---------------------------------------------------------------------------
// Solardisplay – entry point
//
// Target: ESP32 CYD 2432S028 (ILI9341 + XPT2046 touch, on-board RGB LED)
//
// Orchestrates:
//   Settings NVS  → Display (TFT+LVGL) → LED → WiFiManager provisioning
//   → Web UI → Shelly/Growatt pollers → UI state machine
// ---------------------------------------------------------------------------

#include <Arduino.h>
#include <WiFi.h>
#include <SD.h>
#include <lvgl.h>

#include "config.h"
#include "settings.h"
#include "leds.h"
#include "display.h"
#include "net.h"
#include "portal.h"
#include "shelly.h"
#include "growatt.h"
#include "ui.h"

namespace {
ShellyData   g_shelly;
GrowattData  g_growatt;
uint32_t     g_lastHeapLog = 0;
}  // namespace

void setup() {
    Serial.begin(115200);
    delay(50);
    log_i("Solardisplay booting");

    // NVS settings first – every module reads from it.
    SettingsStore::begin();

    // Display (TFT + LVGL) first so we have visual feedback during WLAN.
    Display::begin();
    Display::showBoot("Starte...", "");

    // SD card (shares SPI bus with TFT, CS on GPIO 5).
    if (SD.begin(SD_CS_PIN)) {
        log_i("SD card: type=%d, size=%lluMB",
              SD.cardType(), SD.cardSize() / (1024ULL * 1024ULL));
    } else {
        log_w("No SD card found (slot empty)");
    }

    // On-board RGB LED (blue while provisioning).
    Leds::begin();
    Leds::set(LedColor::Blue);

    // Provisioning / WiFi connect (blocks until WiFi is up or reboot).
    Display::showBoot("WLAN verbinden", "oder Setup-AP");
    Portal::runProvisioning();

    // By now WiFi is connected.
    Display::showBoot("WLAN OK",
                      WiFi.localIP().toString().c_str());
    delay(800);

    // Start always-on web UI.
    Portal::startWebUi();

    // Kick off pollers & UI state.
    Shelly::begin();
    Growatt::begin();
    Ui::begin();

    // Switch to the tabbed dashboard and let the first tick fill it in.
    Display::showDashboard();
    Leds::set(LedColor::Off);
}

void loop() {
    // Network housekeeping.
    Net::tick();

    // HTTP server.
    Portal::tick();

    // Data sources — poll Growatt first. When it gets new data, force an
    // immediate Shelly refresh so both snapshots are from the same moment.
    // This prevents house = stale_pv + fresh_grid mismatches.
    const uint32_t prevGw = g_growatt.lastOkMillis;
    Growatt::tick(g_growatt);
    if (g_growatt.lastOkMillis != prevGw && g_growatt.hasData) {
        Shelly::requestRefresh();
    }
    Shelly::tick(g_shelly);

    // Share snapshot with web dashboard.
    Portal::setDataSnapshot(g_shelly, g_growatt);

    // Output (LVGL tick, UI refresh, LED update).
    Ui::tick(g_shelly, g_growatt);

    // Periodic heap logging (helps catch leaks).
    const uint32_t now = millis();
    if ((now - g_lastHeapLog) >= HEAP_LOG_MS) {
        g_lastHeapLog = now;
        lv_mem_monitor_t mon;
        lv_mem_monitor(&mon);
        log_i("heap=%u rssi=%d lvgl=%d%%used(%uB free)",
              ESP.getFreeHeap(), WiFi.RSSI(),
              (int)mon.used_pct, (unsigned)mon.free_size);
    }

    // Small yield so WiFi/TCP stacks can breathe.
    delay(1);
}
