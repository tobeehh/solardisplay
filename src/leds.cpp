#include "leds.h"
#include "config.h"
#include "model.h"

namespace {

void writePwm(uint8_t channel, uint8_t value) {
#if LED_COMMON_ANODE
    ledcWrite(channel, 255 - value);
#else
    ledcWrite(channel, value);
#endif
}

void writeRgb(uint8_t r, uint8_t g, uint8_t b) {
    writePwm(LEDC_CH_R, r);
    writePwm(LEDC_CH_G, g);
    writePwm(LEDC_CH_B, b);
}

void colorToRgb(LedColor c, uint8_t& r, uint8_t& g, uint8_t& b) {
    switch (c) {
        case LedColor::Off:    r = 0;   g = 0;   b = 0;   break;
        case LedColor::Red:    r = 255; g = 0;   b = 0;   break;
        case LedColor::Yellow: r = 220; g = 140; b = 0;   break;
        case LedColor::Green:  r = 0;   g = 200; b = 0;   break;
        case LedColor::Blue:   r = 0;   g = 0;   b = 255; break;
        case LedColor::White:  r = 180; g = 180; b = 180; break;
    }
}

LedColor flowColor(const ShellyData& sh, uint16_t deadbandW) {
    if (!sh.hasData) return LedColor::Red;
    const float p = sh.totalActPower;     // positive = import
    if (p >  (float)deadbandW) return LedColor::Red;
    if (p < -(float)deadbandW) return LedColor::Green;
    return LedColor::Yellow;
}

}  // namespace

namespace Leds {

void begin() {
    ledcSetup(LEDC_CH_R, LEDC_FREQ_HZ, LEDC_RES_BITS);
    ledcSetup(LEDC_CH_G, LEDC_FREQ_HZ, LEDC_RES_BITS);
    ledcSetup(LEDC_CH_B, LEDC_FREQ_HZ, LEDC_RES_BITS);
    ledcAttachPin(LED_R_PIN, LEDC_CH_R);
    ledcAttachPin(LED_G_PIN, LEDC_CH_G);
    ledcAttachPin(LED_B_PIN, LEDC_CH_B);
    writeRgb(0, 0, 0);
}

void set(LedColor color) {
    uint8_t r, g, b;
    colorToRgb(color, r, g, b);
    writeRgb(r, g, b);
}

LedColor freshnessColor(uint32_t lastOkMillis) {
    if (lastOkMillis == 0) return LedColor::Red;
    const uint32_t age = millis() - lastOkMillis;
    if (age < STATUS_GREEN_MS)  return LedColor::Green;
    if (age < STATUS_YELLOW_MS) return LedColor::Yellow;
    return LedColor::Red;
}

void update(const ShellyData& sh,
            const GrowattData& gw,
            LedMode mode,
            uint16_t deadbandW) {
    LedColor c = LedColor::Off;

    switch (mode) {
        case LedMode::Off:
            c = LedColor::Off;
            break;

        case LedMode::Flow:
            c = flowColor(sh, deadbandW);
            break;

        case LedMode::Aggregated: {
            const LedColor shF = freshnessColor(sh.lastOkMillis);
            const LedColor gwF = freshnessColor(gw.lastOkMillis);
            if (shF == LedColor::Red || gwF == LedColor::Red) {
                c = LedColor::Red;
            } else {
                c = flowColor(sh, deadbandW);
            }
            break;
        }

        case LedMode::Rotating: {
            // 3-phase cycle: 2s Shelly → 2s Growatt → 2s Flow
            const uint32_t phase = (millis() / 2000) % 3;
            switch (phase) {
                case 0: c = freshnessColor(sh.lastOkMillis); break;
                case 1: c = freshnessColor(gw.lastOkMillis); break;
                case 2: c = flowColor(sh, deadbandW);        break;
            }
            break;
        }
    }

    set(c);
}

}  // namespace Leds
