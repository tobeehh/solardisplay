#pragma once

// ---------------------------------------------------------------------------
// Solardisplay – central hardware/runtime configuration
// Target board: ESP32 CYD 2432S028 (2.8" ILI9341 + XPT2046 Touch)
// ---------------------------------------------------------------------------

// ----- TFT (ILI9341, 240x320) ----------------------------------------------
// NOTE: TFT pins are configured via TFT_eSPI -D flags in platformio.ini.
//       These defines are only used for documentation and for the backlight
//       PWM setup.
#define TFT_W_PX            240
#define TFT_H_PX            320
#define TFT_BL_PIN          21
#define TFT_BL_LEDC_CH      7
#define TFT_BL_LEDC_FREQ    5000
#define TFT_BL_LEDC_RES     8
#define TFT_BL_DEFAULT_PCT  80


// ----- Touch (XPT2046) -----------------------------------------------------
// Uses a dedicated second SPI bus (HSPI) separate from the TFT bus.
#define TOUCH_SCLK_PIN      25
#define TOUCH_MOSI_PIN      32
#define TOUCH_MISO_PIN      39
#define TOUCH_CS_PIN        33
#define TOUCH_IRQ_PIN       36

// Raw XPT2046 calibration constants (tweak if rotation/mirroring wrong).
#define TOUCH_X_MIN         200
#define TOUCH_X_MAX         3900
#define TOUCH_Y_MIN         240
#define TOUCH_Y_MAX         3800

// ----- On-board RGB LED (common-anode, active LOW) -------------------------
#define LED_COMMON_ANODE    1
#define LED_R_PIN           4
#define LED_G_PIN           16
#define LED_B_PIN           17

// LEDC PWM config for the RGB LED (3 channels).
#define LEDC_FREQ_HZ        5000
#define LEDC_RES_BITS       8
#define LEDC_CH_R           0
#define LEDC_CH_G           1
#define LEDC_CH_B           2

// ----- Polling / task intervals (ms) ---------------------------------------
#define UI_REFRESH_MS         250    // value update cadence on the UI
#define LVGL_TICK_MS          5      // LVGL timer handler cadence
#define LED_UPDATE_MS         100
#define SHELLY_POLL_MS        5000
#define GROWATT_POLL_MS       60000
#define WIFI_WATCHDOG_MS      2000
#define HEAP_LOG_MS           300000  // 5 min

// ----- Status freshness thresholds (ms) ------------------------------------
#define STATUS_GREEN_MS       75000
#define STATUS_YELLOW_MS      150000

// ----- HTTP client timeouts (ms) -------------------------------------------
#define HTTP_TIMEOUT_MS       5000

// ----- Power flow defaults -------------------------------------------------
#define POWER_FLOW_DEADBAND_W_DEFAULT 100

// ----- WiFiManager / Portal ------------------------------------------------
#define PORTAL_AP_NAME        "Solardisplay-Setup"
#define PORTAL_AP_TIMEOUT_S   300  // auto-close AP after 5 min idle
#define WEB_USER              "admin"

// ----- SD Card (shares SPI bus with TFT, CS on GPIO 5) --------------------
#define SD_CS_PIN             5

// ----- NVS namespace -------------------------------------------------------
#define NVS_NAMESPACE         "solardisp"
