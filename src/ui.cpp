#include "ui.h"
#include "config.h"
#include "display.h"
#include "leds.h"
#include "settings.h"

namespace {

uint32_t s_lastLedUpdate = 0;
uint32_t s_lastUiRefresh = 0;
uint32_t s_lastLvglTick  = 0;

}  // namespace

namespace Ui {

void begin() {
    s_lastLedUpdate = 0;
    s_lastUiRefresh = 0;
    s_lastLvglTick  = 0;
}

void tick(const ShellyData& sh, const GrowattData& gw) {
    const uint32_t now = millis();

    // LVGL needs to be ticked frequently so touches and animations feel
    // responsive. ~5 ms is the usual target.
    if ((now - s_lastLvglTick) >= LVGL_TICK_MS) {
        s_lastLvglTick = now;
        Display::tickLvgl();
    }

    // Value refresh on the dashboard.
    if ((now - s_lastUiRefresh) >= UI_REFRESH_MS) {
        s_lastUiRefresh = now;
        Display::renderPage(Page::Overview, sh, gw);
        if (!SettingsStore::isConfigured()) {
            Display::showMissingConfig();
        }
    }

    // On-board RGB LED update.
    if ((now - s_lastLedUpdate) >= LED_UPDATE_MS) {
        s_lastLedUpdate = now;
        const Settings& s = SettingsStore::get();
        Leds::update(sh, gw, s.ledMode, s.flowDeadbandW);
    }
}

}  // namespace Ui
