#include "display.h"
#include "config.h"
#include "settings.h"
#include "portal.h"

#include <Arduino.h>
#include <WiFi.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <lvgl.h>
#include <SD.h>
#include <Preferences.h>

// ---------------------------------------------------------------------------
// Solardisplay – LVGL touch UI for the CYD 2432S028 (ILI9341 + XPT2046).
// ---------------------------------------------------------------------------

namespace {

// ---- Hardware -------------------------------------------------------------
TFT_eSPI            s_tft;
SPIClass            s_touchSpi(HSPI);
// Pass 255 (no IRQ pin) instead of TOUCH_IRQ_PIN. The XPT2046 library's
// IRQ-wake mechanism disarms touched() after the first poll cycle unless
// the ISR refires — but GPIO 36 on ESP32 is input-only without internal
// pull-up, so the IRQ line floats and the ISR never fires. Pure polling
// works fine for our refresh rate.
XPT2046_Touchscreen s_touch(TOUCH_CS_PIN, 255);

// ---- LVGL buffers ---------------------------------------------------------
constexpr int16_t  SCREEN_W     = 320;
constexpr int16_t  SCREEN_H     = 240;
constexpr uint16_t DRAW_BUF_ROWS = 10;

static lv_color_t s_buf1[SCREEN_W * DRAW_BUF_ROWS];
static lv_color_t s_buf2[SCREEN_W * DRAW_BUF_ROWS];

lv_display_t *s_disp  = nullptr;
lv_indev_t   *s_indev = nullptr;

// ---- Screens --------------------------------------------------------------
lv_obj_t *s_scrBoot   = nullptr;
lv_obj_t *s_scrPortal = nullptr;
lv_obj_t *s_scrDash   = nullptr;

// Boot
lv_obj_t *s_bootLine1 = nullptr;
lv_obj_t *s_bootLine2 = nullptr;

// Portal
lv_obj_t *s_ptlSsid = nullptr;
lv_obj_t *s_ptlIp   = nullptr;

// Dashboard root
lv_obj_t *s_tabv = nullptr;

// Overview tab
lv_obj_t           *s_ovPvTile  = nullptr;  // tile container (for accent stripe)
lv_obj_t           *s_ovHouseTile = nullptr;
lv_obj_t           *s_ovGridTile  = nullptr;
lv_obj_t           *s_ovAutTile   = nullptr;
lv_obj_t           *s_ovPv      = nullptr;
lv_obj_t           *s_ovPvHdr   = nullptr;
lv_obj_t           *s_ovHouse   = nullptr;
lv_obj_t           *s_ovHouseHdr = nullptr;
lv_obj_t           *s_ovGrid    = nullptr;
lv_obj_t           *s_ovGridHdr = nullptr;
lv_obj_t           *s_ovAut     = nullptr;
lv_obj_t           *s_ovAutHdr  = nullptr;

// Tile tap toggles (false = live W, true = daily kWh)
bool     s_pvShowKwh          = false;
bool     s_houseShowKwh       = false;
bool     s_gridShowKwh        = false;
bool     s_autShowKwh         = false;
// NVS day-start counters for energy-counter-based calculations.
float    s_dayStartImportWh   = 0;
float    s_dayStartExportWh   = 0;
float    s_dayStartETodayKwh  = 0;     // PV baseline at snapshot time
float    s_lastETodayKwh      = -1;    // track Growatt eTodayKwh for day rollover
bool     s_dayCountersLoaded  = false; // NVS read done?

// Frozen Shelly EMData snapshots (captured at Growatt sync moment).
float    s_snapImportWh       = 0;
float    s_snapExportWh       = 0;
float    s_snapETodayKwh      = 0;
bool     s_hasSnap            = false;

// Cached daily energy values (recomputed only when snapshot changes).
float    s_dayPvKwh           = 0;     // PV generated today
float    s_dayHouseKwh        = 0;     // house consumed today
float    s_dayImpKwh          = 0;     // grid import today
float    s_dayExpKwh          = 0;     // grid export today
int      s_autPct             = 0;
float    s_autSelfKwh         = 0;
lv_obj_t           *s_ovChart   = nullptr;
lv_chart_series_t  *s_ovSeries  = nullptr;
lv_obj_t           *s_ovLedSh   = nullptr;
lv_obj_t           *s_ovLedGw   = nullptr;
lv_obj_t           *s_ovLedFl   = nullptr;
lv_obj_t           *s_ovHint    = nullptr;

// Growatt tab
lv_obj_t *s_gwArc    = nullptr;
lv_obj_t *s_gwArcLbl = nullptr;
lv_obj_t *s_gwToday  = nullptr;
lv_obj_t *s_gwTotal  = nullptr;

// Shelly tab
lv_obj_t *s_shBarA  = nullptr;
lv_obj_t *s_shBarB  = nullptr;
lv_obj_t *s_shBarC  = nullptr;
lv_obj_t *s_shLblA  = nullptr;
lv_obj_t *s_shLblB  = nullptr;
lv_obj_t *s_shLblC  = nullptr;
lv_obj_t *s_shTotal = nullptr;

// System tab
lv_obj_t *s_sysIp   = nullptr;
lv_obj_t *s_sysRssi = nullptr;
lv_obj_t *s_sysUp   = nullptr;
lv_obj_t *s_sysHeap = nullptr;
lv_obj_t *s_sysSd   = nullptr;

// Settings tab
lv_obj_t *s_setLedDrop  = nullptr;
lv_obj_t *s_setBright   = nullptr;

uint8_t  s_backlightPct = TFT_BL_DEFAULT_PCT;
bool     s_missingConfig = false;

// ---- Colors ---------------------------------------------------------------
inline lv_color_t cAccent() { return lv_color_hex(0x58d0ff); }
inline lv_color_t cGreen()  { return lv_color_hex(0x4ade80); }
inline lv_color_t cYellow() { return lv_color_hex(0xfacc15); }
inline lv_color_t cRed()    { return lv_color_hex(0xf87171); }
inline lv_color_t cMuted()  { return lv_color_hex(0x7d8796); }
inline lv_color_t cPanel()  { return lv_color_hex(0x151c24); }
inline lv_color_t cPanel2() { return lv_color_hex(0x1e2730); }
inline lv_color_t cBg()     { return lv_color_hex(0x0b0f14); }
inline lv_color_t cBorder() { return lv_color_hex(0x263040); }
inline lv_color_t cText()   { return lv_color_hex(0xe6edf3); }

// ---- TFT/LVGL plumbing ----------------------------------------------------
void backlightSetupHw() {
    ledcSetup(TFT_BL_LEDC_CH, TFT_BL_LEDC_FREQ, TFT_BL_LEDC_RES);
    ledcAttachPin(TFT_BL_PIN, TFT_BL_LEDC_CH);
    const uint32_t duty = (s_backlightPct * 255u) / 100u;
    ledcWrite(TFT_BL_LEDC_CH, duty);
}

void lvglFlushCb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    const uint32_t w = area->x2 - area->x1 + 1;
    const uint32_t h = area->y2 - area->y1 + 1;

    s_tft.startWrite();
    s_tft.setAddrWindow(area->x1, area->y1, w, h);
    s_tft.pushPixels((uint16_t *)px_map, w * h);
    s_tft.endWrite();

    lv_display_flush_ready(disp);
}

// Low-level XPT2046 read via the raw SPI transactions. This bypasses the
// Paul Stoffregen library which disarms itself when the IRQ ISR does not
// fire and is fiddly to debug. Returns true and fills x/y (raw 12-bit) if
// the touch is currently pressed.
static bool readTouchRaw(int16_t& xraw, int16_t& yraw, int16_t& zraw) {
    static SPISettings kTouchSpi(2000000, MSBFIRST, SPI_MODE0);
    s_touchSpi.beginTransaction(kTouchSpi);
    digitalWrite(TOUCH_CS_PIN, LOW);

    // Pressure: z1 + 4095 - z2
    s_touchSpi.transfer(0xB1);
    int16_t z1 = s_touchSpi.transfer16(0xC1) >> 3;
    int16_t z  = z1 + 4095;
    int16_t z2 = s_touchSpi.transfer16(0x91) >> 3;
    z -= z2;

    int16_t x = 0, y = 0;
    const bool pressed = (z >= 300);
    if (pressed) {
        // Dummy X read (first is noisy)
        s_touchSpi.transfer16(0x91);
        int16_t y1 = s_touchSpi.transfer16(0xD1) >> 3;
        int16_t x1 = s_touchSpi.transfer16(0x91) >> 3;
        int16_t y2 = s_touchSpi.transfer16(0xD1) >> 3;
        int16_t x2 = s_touchSpi.transfer16(0x91) >> 3;
        x = (x1 + x2) / 2;
        y = (y1 + y2) / 2;
    }
    // Power down the chip
    s_touchSpi.transfer16(0xD0);
    s_touchSpi.transfer16(0x00);

    digitalWrite(TOUCH_CS_PIN, HIGH);
    s_touchSpi.endTransaction();

    zraw = z;
    xraw = x;
    yraw = y;
    return pressed;
}

void lvglTouchReadCb(lv_indev_t * /*indev*/, lv_indev_data_t *data) {
    int16_t xr = 0, yr = 0, zr = 0;
    const bool pressed = readTouchRaw(xr, yr, zr);

    if (pressed) {
        // Map raw to landscape 320x240 screen space. The CYD touch panel is
        // natively portrait, so for TFT rotation 1 (landscape) we swap axes
        // (raw Y → screen X, raw X → screen Y). Both axes run the same
        // direction as the display on this board, so no inversion.
        int32_t sx = map(yr, TOUCH_Y_MIN, TOUCH_Y_MAX, 0, SCREEN_W - 1);
        int32_t sy = map(xr, TOUCH_X_MIN, TOUCH_X_MAX, 0, SCREEN_H - 1);
        if (sx < 0) sx = 0; else if (sx > SCREEN_W - 1) sx = SCREEN_W - 1;
        if (sy < 0) sy = 0; else if (sy > SCREEN_H - 1) sy = SCREEN_H - 1;
        data->point.x = sx;
        data->point.y = sy;
        data->state   = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

// ---- Widget helpers -------------------------------------------------------
void stylePanel(lv_obj_t *o) {
    lv_obj_set_style_bg_color(o, cPanel(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(o, cBorder(), LV_PART_MAIN);
    lv_obj_set_style_border_width(o, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(o, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_all(o, 6, LV_PART_MAIN);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t *makeTile(lv_obj_t *parent, const char *label,
                   lv_color_t valueColor,
                   lv_obj_t **outValueLabel,
                   lv_obj_t **outHeaderLabel = nullptr) {
    lv_obj_t *tile = lv_obj_create(parent);
    lv_obj_set_size(tile, 74, 78);
    stylePanel(tile);
    lv_obj_set_style_pad_all(tile, 6, LV_PART_MAIN);
    // Colored left accent stripe
    lv_obj_set_style_border_side(tile, LV_BORDER_SIDE_LEFT, LV_PART_MAIN);
    lv_obj_set_style_border_width(tile, 3, LV_PART_MAIN);
    lv_obj_set_style_border_color(tile, valueColor, LV_PART_MAIN);

    lv_obj_t *lbl = lv_label_create(tile);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_color(lbl, cMuted(), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *val = lv_label_create(tile);
    lv_label_set_text(val, "--");
    lv_obj_set_style_text_color(val, valueColor, 0);
    lv_obj_set_style_text_font(val, &lv_font_montserrat_20, 0);
    lv_obj_align(val, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    *outValueLabel = val;
    if (outHeaderLabel) *outHeaderLabel = lbl;
    return tile;
}

lv_obj_t *makeDot(lv_obj_t *parent, const char *label) {
    lv_obj_t *wrap = lv_obj_create(parent);
    lv_obj_set_size(wrap, 86, 28);
    lv_obj_set_style_bg_opa(wrap, LV_OPA_0, LV_PART_MAIN);
    lv_obj_set_style_border_width(wrap, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(wrap, 0, LV_PART_MAIN);
    lv_obj_clear_flag(wrap, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *dot = lv_obj_create(wrap);
    lv_obj_set_size(dot, 16, 16);
    lv_obj_align(dot, LV_ALIGN_LEFT_MID, 4, 0);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(dot, cMuted(), LV_PART_MAIN);
    lv_obj_set_style_border_width(dot, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(dot, 0, LV_PART_MAIN);
    // Subtle glow ring around the dot
    lv_obj_set_style_outline_width(dot, 3, LV_PART_MAIN);
    lv_obj_set_style_outline_color(dot, cMuted(), LV_PART_MAIN);
    lv_obj_set_style_outline_opa(dot, LV_OPA_30, LV_PART_MAIN);
    lv_obj_set_style_outline_pad(dot, 0, LV_PART_MAIN);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = lv_label_create(wrap);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_color(lbl, cMuted(), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 24, 0);

    return dot;
}

void setDotColor(lv_obj_t *dot, lv_color_t color) {
    if (!dot) return;
    lv_obj_set_style_bg_color(dot, color, LV_PART_MAIN);
    lv_obj_set_style_outline_color(dot, color, LV_PART_MAIN);
}

lv_color_t freshnessToColor(uint32_t lastOkMs) {
    if (lastOkMs == 0) return cRed();
    const uint32_t age = millis() - lastOkMs;
    if (age < STATUS_GREEN_MS)  return cGreen();
    if (age < STATUS_YELLOW_MS) return cYellow();
    return cRed();
}

// ---- Screen: Boot ---------------------------------------------------------
void buildBootScreen() {
    s_scrBoot = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(s_scrBoot, cBg(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_scrBoot, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_t *card = lv_obj_create(s_scrBoot);
    lv_obj_set_size(card, 280, 160);
    lv_obj_center(card);
    stylePanel(card);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *title = lv_label_create(card);
    lv_label_set_text(title, "Solardisplay");
    lv_obj_set_style_text_color(title, cAccent(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_32, 0);

    s_bootLine1 = lv_label_create(card);
    lv_label_set_text(s_bootLine1, "");
    lv_obj_set_style_text_color(s_bootLine1, cText(), 0);
    lv_obj_set_style_text_font(s_bootLine1, &lv_font_montserrat_16, 0);

    s_bootLine2 = lv_label_create(card);
    lv_label_set_text(s_bootLine2, "");
    lv_obj_set_style_text_color(s_bootLine2, cMuted(), 0);
    lv_obj_set_style_text_font(s_bootLine2, &lv_font_montserrat_14, 0);
}

// ---- Screen: Portal -------------------------------------------------------
void buildPortalScreen() {
    s_scrPortal = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(s_scrPortal, cBg(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_scrPortal, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_t *card = lv_obj_create(s_scrPortal);
    lv_obj_set_size(card, 310, 220);
    lv_obj_center(card);
    stylePanel(card);
    lv_obj_set_style_pad_all(card, 10, LV_PART_MAIN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    // Left side: text labels
    lv_obj_t *textCol = lv_obj_create(card);
    lv_obj_set_size(textCol, 160, 196);
    lv_obj_align(textCol, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_opa(textCol, LV_OPA_0, LV_PART_MAIN);
    lv_obj_set_style_border_width(textCol, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(textCol, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(textCol, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(textCol, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(textCol, 6, LV_PART_MAIN);
    lv_obj_clear_flag(textCol, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(textCol);
    lv_label_set_text(title, "WLAN Setup");
    lv_obj_set_style_text_color(title, cAccent(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);

    lv_obj_t *hint = lv_label_create(textCol);
    lv_label_set_text(hint, "QR-Code scannen\noder manuell\nverbinden:");
    lv_obj_set_style_text_color(hint, cMuted(), 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);

    s_ptlSsid = lv_label_create(textCol);
    lv_label_set_text(s_ptlSsid, "AP: --");
    lv_obj_set_style_text_color(s_ptlSsid, cText(), 0);
    lv_obj_set_style_text_font(s_ptlSsid, &lv_font_montserrat_14, 0);

    s_ptlIp = lv_label_create(textCol);
    lv_label_set_text(s_ptlIp, "http://192.168.4.1");
    lv_obj_set_style_text_color(s_ptlIp, cAccent(), 0);
    lv_obj_set_style_text_font(s_ptlIp, &lv_font_montserrat_14, 0);

    // Right side: WiFi QR code (dark-on-light for best scanner compat)
    static const char s_qrPayload[] =
        "WIFI:S:" PORTAL_AP_NAME ";T:nopass;;";

    lv_obj_t *qr = lv_qrcode_create(card);
    lv_qrcode_set_size(qr, 130);
    lv_qrcode_set_dark_color(qr, lv_color_black());
    lv_qrcode_set_light_color(qr, lv_color_white());
    lv_result_t res = lv_qrcode_update(qr, s_qrPayload, strlen(s_qrPayload));
    log_i("QR code update: %s (%d bytes)",
          res == LV_RESULT_OK ? "OK" : "FAIL", strlen(s_qrPayload));
    lv_obj_align(qr, LV_ALIGN_RIGHT_MID, -2, 0);
    // No LVGL border — it would overlap QR modules. Use outline for quiet zone.
    lv_obj_set_style_border_width(qr, 0, LV_PART_MAIN);
    lv_obj_set_style_outline_width(qr, 4, LV_PART_MAIN);
    lv_obj_set_style_outline_color(qr, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_outline_opa(qr, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_outline_pad(qr, 0, LV_PART_MAIN);
}

// ---- Event callbacks ------------------------------------------------------
void onLedModeChanged(lv_event_t *e) {
    lv_obj_t *dd = (lv_obj_t *)lv_event_get_target(e);
    uint16_t sel = lv_dropdown_get_selected(dd);
    if (sel > 3) sel = 0;
    SettingsStore::setLedMode(static_cast<LedMode>(sel));
}

void onBrightChanged(lv_event_t *e) {
    lv_obj_t *sl = (lv_obj_t *)lv_event_get_target(e);
    int32_t v = lv_slider_get_value(sl);
    if (v < 5) v = 5;
    Display::setBacklightPct((uint8_t)v);
}

void onPortalBtn(lv_event_t *) {
    Portal::openConfigPortal();
}

void onRebootBtn(lv_event_t *) {
    delay(200);
    ESP.restart();
}

void onPvTap(lv_event_t *) {
    s_pvShowKwh = !s_pvShowKwh;
    if (s_ovPvHdr)
        lv_label_set_text(s_ovPvHdr, s_pvShowKwh ? "PV kWh" : "PV");
}

void onHouseTap(lv_event_t *) {
    s_houseShowKwh = !s_houseShowKwh;
    if (s_ovHouseHdr)
        lv_label_set_text(s_ovHouseHdr, s_houseShowKwh ? "Verbr." : "Haus");
}

void onGridTap(lv_event_t *) {
    s_gridShowKwh = !s_gridShowKwh;
    if (s_ovGridHdr)
        lv_label_set_text(s_ovGridHdr, s_gridShowKwh ? "Saldo" : "Bezug");
}

void onAutarkieTap(lv_event_t *) {
    s_autShowKwh = !s_autShowKwh;
    if (s_ovAutHdr)
        lv_label_set_text(s_ovAutHdr, s_autShowKwh ? "Eigen" : "Autark.");
}

// ---- Tab builders ---------------------------------------------------------
void buildOverviewTab(lv_obj_t *tab) {
    lv_obj_set_style_pad_all(tab, 6, LV_PART_MAIN);
    lv_obj_clear_flag(tab, LV_OBJ_FLAG_SCROLLABLE);

    // Row of 3 tiles
    lv_obj_t *row = lv_obj_create(tab);
    lv_obj_set_size(row, 316, 84);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_0, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    s_ovPvTile    = makeTile(row, "PV",      cGreen(),  &s_ovPv,    &s_ovPvHdr);
    s_ovHouseTile = makeTile(row, "Haus",    cGreen(),  &s_ovHouse, &s_ovHouseHdr);
    s_ovGridTile  = makeTile(row, "Bezug",   cRed(),    &s_ovGrid,  &s_ovGridHdr);
    s_ovAutTile   = makeTile(row, "Autark.", cGreen(),  &s_ovAut,   &s_ovAutHdr);

    lv_obj_add_flag(s_ovPvTile, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_ovPvTile, onPvTap, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_flag(s_ovHouseTile, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_ovHouseTile, onHouseTap, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_flag(s_ovGridTile, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_ovGridTile, onGridTap, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_flag(s_ovAutTile, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_ovAutTile, onAutarkieTap, LV_EVENT_CLICKED, nullptr);

    // Chart + status dots below
    lv_obj_t *bottom = lv_obj_create(tab);
    lv_obj_set_size(bottom, 316, 104);
    lv_obj_align(bottom, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(bottom, LV_OPA_0, LV_PART_MAIN);
    lv_obj_set_style_border_width(bottom, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(bottom, 0, LV_PART_MAIN);
    lv_obj_clear_flag(bottom, LV_OBJ_FLAG_SCROLLABLE);

    s_ovChart = lv_chart_create(bottom);
    lv_obj_set_size(s_ovChart, 208, 100);
    lv_obj_align(s_ovChart, LV_ALIGN_LEFT_MID, 0, 0);
    stylePanel(s_ovChart);
    lv_chart_set_type(s_ovChart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(s_ovChart, 48);
    lv_chart_set_update_mode(s_ovChart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_chart_set_range(s_ovChart, LV_CHART_AXIS_PRIMARY_Y, 0, 2800);
    // Hide point dots, thicker line.
    lv_obj_set_style_size(s_ovChart, 0, 0, LV_PART_INDICATOR);
    lv_obj_set_style_line_width(s_ovChart, 2, LV_PART_ITEMS);
    // Subtle horizontal grid lines (4 divisions = lines at 25/50/75 %).
    lv_chart_set_div_line_count(s_ovChart, 3, 0);
    lv_obj_set_style_line_color(s_ovChart, lv_color_hex(0x1e2730),
                                LV_PART_MAIN);
    lv_obj_set_style_line_opa(s_ovChart, LV_OPA_COVER, LV_PART_MAIN);
    s_ovSeries = lv_chart_add_series(s_ovChart, cGreen(),
                                     LV_CHART_AXIS_PRIMARY_Y);

    // Dots card – width matches the "Haus" tile above so the grid aligns.
    lv_obj_t *dots = lv_obj_create(bottom);
    lv_obj_set_size(dots, 100, 100);
    lv_obj_align(dots, LV_ALIGN_RIGHT_MID, 0, 0);
    stylePanel(dots);
    lv_obj_set_style_pad_all(dots, 6, LV_PART_MAIN);

    // NOTE: LVGL Montserrat 14 has no umlauts → use ASCII spelling.
    s_ovLedSh = makeDot(dots, "Zaehler");
    lv_obj_align(lv_obj_get_parent(s_ovLedSh), LV_ALIGN_TOP_LEFT, 0, 0);
    s_ovLedGw = makeDot(dots, "Growatt");
    lv_obj_align(lv_obj_get_parent(s_ovLedGw), LV_ALIGN_LEFT_MID, 0, 0);
    s_ovLedFl = makeDot(dots, "Saldo");
    lv_obj_align(lv_obj_get_parent(s_ovLedFl), LV_ALIGN_BOTTOM_LEFT, 0, 0);

    s_ovHint = lv_label_create(tab);
    lv_label_set_text(s_ovHint, "");
    lv_obj_set_style_text_color(s_ovHint, cYellow(), 0);
    lv_obj_set_style_text_font(s_ovHint, &lv_font_montserrat_14, 0);
    lv_obj_align(s_ovHint, LV_ALIGN_BOTTOM_MID, 0, -2);
    lv_obj_add_flag(s_ovHint, LV_OBJ_FLAG_HIDDEN);
}

void buildGrowattTab(lv_obj_t *tab) {
    lv_obj_set_style_pad_all(tab, 8, LV_PART_MAIN);
    lv_obj_clear_flag(tab, LV_OBJ_FLAG_SCROLLABLE);

    s_gwArc = lv_arc_create(tab);
    lv_obj_set_size(s_gwArc, 160, 160);
    lv_obj_align(s_gwArc, LV_ALIGN_LEFT_MID, 0, 0);
    lv_arc_set_rotation(s_gwArc, 135);
    lv_arc_set_bg_angles(s_gwArc, 0, 270);
    lv_arc_set_range(s_gwArc, 0, 2800);
    lv_arc_set_value(s_gwArc, 0);
    lv_obj_remove_style(s_gwArc, nullptr, LV_PART_KNOB);
    lv_obj_set_style_arc_color(s_gwArc, cBorder(), LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_gwArc, cGreen(), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(s_gwArc, 14, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_gwArc, 14, LV_PART_INDICATOR);

    s_gwArcLbl = lv_label_create(s_gwArc);
    lv_label_set_text(s_gwArcLbl, "-- W");
    lv_obj_center(s_gwArcLbl);
    lv_obj_set_style_text_color(s_gwArcLbl, cGreen(), 0);
    lv_obj_set_style_text_font(s_gwArcLbl, &lv_font_montserrat_20, 0);

    lv_obj_t *infoCard = lv_obj_create(tab);
    lv_obj_set_size(infoCard, 130, 160);
    lv_obj_align(infoCard, LV_ALIGN_RIGHT_MID, 0, 0);
    stylePanel(infoCard);
    lv_obj_set_flex_flow(infoCard, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(infoCard, 6, LV_PART_MAIN);

    lv_obj_t *l1 = lv_label_create(infoCard);
    lv_label_set_text(l1, "Heute");
    lv_obj_set_style_text_color(l1, cMuted(), 0);
    lv_obj_set_style_text_font(l1, &lv_font_montserrat_14, 0);
    s_gwToday = lv_label_create(infoCard);
    lv_label_set_text(s_gwToday, "-- kWh");
    lv_obj_set_style_text_color(s_gwToday, cGreen(), 0);
    lv_obj_set_style_text_font(s_gwToday, &lv_font_montserrat_20, 0);

    // Separator
    lv_obj_t *sep = lv_obj_create(infoCard);
    lv_obj_set_size(sep, 110, 1);
    lv_obj_set_style_bg_color(sep, cBorder(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(sep, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(sep, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(sep, 0, LV_PART_MAIN);
    lv_obj_clear_flag(sep, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *l2 = lv_label_create(infoCard);
    lv_label_set_text(l2, "Gesamt");
    lv_obj_set_style_text_color(l2, cMuted(), 0);
    lv_obj_set_style_text_font(l2, &lv_font_montserrat_14, 0);
    s_gwTotal = lv_label_create(infoCard);
    lv_label_set_text(s_gwTotal, "-- kWh");
    lv_obj_set_style_text_color(s_gwTotal, cText(), 0);
    lv_obj_set_style_text_font(s_gwTotal, &lv_font_montserrat_20, 0);
}

void buildShellyPhaseRow(lv_obj_t *parent, int y,
                         const char *label,
                         lv_obj_t **outBar, lv_obj_t **outVal) {
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_color(lbl, cMuted(), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 0, y);

    lv_obj_t *bar = lv_bar_create(parent);
    lv_obj_set_size(bar, 200, 14);
    lv_obj_align(bar, LV_ALIGN_TOP_LEFT, 28, y + 2);
    lv_bar_set_range(bar, 0, 3000);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar, cPanel2(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, cAccent(), LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 7, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 7, LV_PART_INDICATOR);

    lv_obj_t *val = lv_label_create(parent);
    lv_label_set_text(val, "-- W");
    lv_obj_set_style_text_color(val, cText(), 0);
    lv_obj_set_style_text_font(val, &lv_font_montserrat_14, 0);
    lv_obj_align(val, LV_ALIGN_TOP_LEFT, 236, y);

    *outBar = bar;
    *outVal = val;
}

void buildShellyTab(lv_obj_t *tab) {
    lv_obj_set_style_pad_all(tab, 6, LV_PART_MAIN);
    lv_obj_clear_flag(tab, LV_OBJ_FLAG_SCROLLABLE);

    // Phase bars in a card
    lv_obj_t *card = lv_obj_create(tab);
    lv_obj_set_size(card, 308, 100);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 0);
    stylePanel(card);
    lv_obj_set_style_pad_all(card, 10, LV_PART_MAIN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    buildShellyPhaseRow(card,  0, "A", &s_shBarA, &s_shLblA);
    buildShellyPhaseRow(card, 30, "B", &s_shBarB, &s_shLblB);
    buildShellyPhaseRow(card, 60, "C", &s_shBarC, &s_shLblC);

    // Total in a separate accent card
    lv_obj_t *totCard = lv_obj_create(tab);
    lv_obj_set_size(totCard, 308, 74);
    lv_obj_align(totCard, LV_ALIGN_BOTTOM_MID, 0, 0);
    stylePanel(totCard);
    lv_obj_set_style_pad_all(totCard, 10, LV_PART_MAIN);
    lv_obj_set_style_border_side(totCard, LV_BORDER_SIDE_LEFT, LV_PART_MAIN);
    lv_obj_set_style_border_width(totCard, 3, LV_PART_MAIN);
    lv_obj_set_style_border_color(totCard, cAccent(), LV_PART_MAIN);
    lv_obj_clear_flag(totCard, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *tLbl = lv_label_create(totCard);
    lv_label_set_text(tLbl, "Total");
    lv_obj_set_style_text_color(tLbl, cMuted(), 0);
    lv_obj_set_style_text_font(tLbl, &lv_font_montserrat_14, 0);
    lv_obj_align(tLbl, LV_ALIGN_TOP_LEFT, 0, 0);

    s_shTotal = lv_label_create(totCard);
    lv_label_set_text(s_shTotal, "-- W");
    lv_obj_set_style_text_color(s_shTotal, cAccent(), 0);
    lv_obj_set_style_text_font(s_shTotal, &lv_font_montserrat_32, 0);
    lv_obj_align(s_shTotal, LV_ALIGN_BOTTOM_LEFT, 0, 0);
}

void buildSystemTab(lv_obj_t *tab) {
    lv_obj_set_style_pad_all(tab, 6, LV_PART_MAIN);
    lv_obj_clear_flag(tab, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *card = lv_obj_create(tab);
    lv_obj_set_size(card, 308, 182);
    lv_obj_center(card);
    stylePanel(card);
    lv_obj_set_style_pad_all(card, 12, LV_PART_MAIN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    auto row = [&](int y, const char *k, lv_obj_t **outV) {
        lv_obj_t *lk = lv_label_create(card);
        lv_label_set_text(lk, k);
        lv_obj_set_style_text_color(lk, cMuted(), 0);
        lv_obj_set_style_text_font(lk, &lv_font_montserrat_14, 0);
        lv_obj_align(lk, LV_ALIGN_TOP_LEFT, 0, y);

        lv_obj_t *lv = lv_label_create(card);
        lv_label_set_text(lv, "--");
        lv_obj_set_style_text_color(lv, cText(), 0);
        lv_obj_set_style_text_font(lv, &lv_font_montserrat_16, 0);
        lv_obj_align(lv, LV_ALIGN_TOP_LEFT, 80, y - 2);
        *outV = lv;
    };

    row( 0, "IP",     &s_sysIp);
    row(28, "RSSI",   &s_sysRssi);
    row(56, "Uptime", &s_sysUp);
    row(84, "Heap",   &s_sysHeap);
    row(112,"SD",     &s_sysSd);

    // Separator lines between rows
    for (int y : {24, 52, 80, 108}) {
        lv_obj_t *ln = lv_obj_create(card);
        lv_obj_set_size(ln, 280, 1);
        lv_obj_align(ln, LV_ALIGN_TOP_LEFT, 0, y);
        lv_obj_set_style_bg_color(ln, cBorder(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(ln, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(ln, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(ln, 0, LV_PART_MAIN);
        lv_obj_clear_flag(ln, LV_OBJ_FLAG_SCROLLABLE);
    }
}

void buildSettingsTab(lv_obj_t *tab) {
    lv_obj_set_style_pad_all(tab, 10, LV_PART_MAIN);
    lv_obj_clear_flag(tab, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *l1 = lv_label_create(tab);
    lv_label_set_text(l1, "LED Modus");
    lv_obj_set_style_text_color(l1, cMuted(), 0);
    lv_obj_set_style_text_font(l1, &lv_font_montserrat_14, 0);
    lv_obj_align(l1, LV_ALIGN_TOP_LEFT, 0, 4);

    s_setLedDrop = lv_dropdown_create(tab);
    lv_dropdown_set_options(s_setLedDrop,
        "Flow\nAggregiert\nRotierend\nAus");
    lv_obj_set_width(s_setLedDrop, 160);
    lv_obj_align(s_setLedDrop, LV_ALIGN_TOP_LEFT, 90, 0);
    lv_obj_add_event_cb(s_setLedDrop, onLedModeChanged,
                        LV_EVENT_VALUE_CHANGED, nullptr);

    lv_obj_t *l2 = lv_label_create(tab);
    lv_label_set_text(l2, "Helligkeit");
    lv_obj_set_style_text_color(l2, cMuted(), 0);
    lv_obj_set_style_text_font(l2, &lv_font_montserrat_14, 0);
    lv_obj_align(l2, LV_ALIGN_TOP_LEFT, 0, 46);

    s_setBright = lv_slider_create(tab);
    lv_obj_set_size(s_setBright, 160, 14);
    lv_obj_align(s_setBright, LV_ALIGN_TOP_LEFT, 90, 50);
    lv_slider_set_range(s_setBright, 10, 100);
    lv_slider_set_value(s_setBright, s_backlightPct, LV_ANIM_OFF);
    lv_obj_add_event_cb(s_setBright, onBrightChanged,
                        LV_EVENT_VALUE_CHANGED, nullptr);

    lv_obj_t *btn1 = lv_button_create(tab);
    lv_obj_set_size(btn1, 140, 36);
    lv_obj_align(btn1, LV_ALIGN_TOP_LEFT, 0, 86);
    lv_obj_set_style_bg_color(btn1, lv_color_hex(0x0e3a4d), LV_PART_MAIN);
    lv_obj_set_style_border_color(btn1, cAccent(), LV_PART_MAIN);
    lv_obj_set_style_border_width(btn1, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(btn1, 10, LV_PART_MAIN);
    lv_obj_add_event_cb(btn1, onPortalBtn, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *lbl1 = lv_label_create(btn1);
    lv_label_set_text(lbl1, "Setup-Portal");
    lv_obj_center(lbl1);

    lv_obj_t *btn2 = lv_button_create(tab);
    lv_obj_set_size(btn2, 140, 36);
    lv_obj_align(btn2, LV_ALIGN_TOP_LEFT, 160, 86);
    lv_obj_set_style_bg_color(btn2, lv_color_hex(0x4a1a1a), LV_PART_MAIN);
    lv_obj_set_style_border_color(btn2, cRed(), LV_PART_MAIN);
    lv_obj_set_style_border_width(btn2, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(btn2, 10, LV_PART_MAIN);
    lv_obj_add_event_cb(btn2, onRebootBtn, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *lbl2 = lv_label_create(btn2);
    lv_label_set_text(lbl2, "Reboot");
    lv_obj_center(lbl2);

    // Sync the dropdown with the persisted setting.
    const uint8_t idx = static_cast<uint8_t>(SettingsStore::get().ledMode);
    lv_dropdown_set_selected(s_setLedDrop, idx > 3 ? 0 : idx);
}

void buildDashboardScreen() {
    s_scrDash = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(s_scrDash, cBg(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_scrDash, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_scrDash, 0, LV_PART_MAIN);

    s_tabv = lv_tabview_create(s_scrDash);
    lv_tabview_set_tab_bar_position(s_tabv, LV_DIR_TOP);
    lv_tabview_set_tab_bar_size(s_tabv, 32);
    lv_obj_set_size(s_tabv, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(s_tabv, cBg(), LV_PART_MAIN);

    lv_obj_t *tBar = lv_tabview_get_tab_bar(s_tabv);
    lv_obj_set_style_bg_color(tBar, cPanel(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(tBar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(tBar, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(tBar, 0, LV_PART_MAIN);
    // Inactive tab buttons: muted text, transparent bg
    lv_obj_set_style_text_color(tBar, cMuted(), LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(tBar, LV_OPA_0, LV_PART_ITEMS);
    lv_obj_set_style_border_width(tBar, 0, LV_PART_ITEMS);
    lv_obj_set_style_radius(tBar, 0, LV_PART_ITEMS);
    // Active (checked) tab: bright text, accent underline
    lv_obj_set_style_text_color(tBar, cText(), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(tBar, cPanel2(), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(tBar, LV_OPA_COVER, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_border_color(tBar, cAccent(), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_border_width(tBar, 2, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_border_side(tBar, LV_BORDER_SIDE_BOTTOM, LV_PART_ITEMS | LV_STATE_CHECKED);

    lv_obj_t *t1 = lv_tabview_add_tab(s_tabv, "Start");
    lv_obj_t *t2 = lv_tabview_add_tab(s_tabv, "PV");
    lv_obj_t *t3 = lv_tabview_add_tab(s_tabv, "Netz");
    lv_obj_t *t4 = lv_tabview_add_tab(s_tabv, "System");
    lv_obj_t *t5 = lv_tabview_add_tab(s_tabv, "Konfig");

    // Style all tab contents with the background color
    for (lv_obj_t *t : {t1, t2, t3, t4, t5}) {
        lv_obj_set_style_bg_color(t, cBg(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(t, LV_OPA_COVER, LV_PART_MAIN);
    }

    buildOverviewTab(t1);
    buildGrowattTab(t2);
    buildShellyTab(t3);
    buildSystemTab(t4);
    buildSettingsTab(t5);
}

}  // namespace

// ===========================================================================
// Public API
// ===========================================================================
namespace Display {

void begin() {
    // --- TFT ---
    s_tft.begin();
    s_tft.setRotation(1);    // landscape 320x240
    s_tft.fillScreen(TFT_BLACK);
    s_tft.setSwapBytes(true);

    // --- Backlight PWM ---
    backlightSetupHw();

    // --- Touch on HSPI (raw, not via XPT2046_Touchscreen library) ---
    s_touchSpi.begin(TOUCH_SCLK_PIN, TOUCH_MISO_PIN, TOUCH_MOSI_PIN,
                     TOUCH_CS_PIN);
    pinMode(TOUCH_CS_PIN, OUTPUT);
    digitalWrite(TOUCH_CS_PIN, HIGH);

    // --- LVGL ---
    lv_init();
    // LVGL 9 no longer supports LV_TICK_CUSTOM from lv_conf.h — we must
    // register a tick-get callback explicitly, otherwise lv_tick_get()
    // always returns 0 and no LVGL timers ever fire (refresh, indev read,
    // animations). Arduino's millis() already returns uint32_t so we can
    // use it directly as the tick source.
    lv_tick_set_cb([]() -> uint32_t { return millis(); });

    s_disp = lv_display_create(SCREEN_W, SCREEN_H);
    lv_display_set_flush_cb(s_disp, lvglFlushCb);
    lv_display_set_buffers(s_disp, s_buf1, s_buf2,
                           sizeof(s_buf1),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    s_indev = lv_indev_create();
    lv_indev_set_type(s_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(s_indev, lvglTouchReadCb);
    lv_indev_set_display(s_indev, s_disp);
    lv_indev_set_mode(s_indev, LV_INDEV_MODE_TIMER);
    lv_indev_enable(s_indev, true);

    // --- Global screen background (fall-back) ---
    lv_obj_set_style_bg_color(lv_screen_active(), cBg(), LV_PART_MAIN);

    buildBootScreen();
    buildPortalScreen();
    buildDashboardScreen();

    lv_screen_load(s_scrBoot);

    // Prime one refresh cycle so the boot screen is visible immediately.
    lv_timer_handler();
}

void tickLvgl() {
    lv_timer_handler();
}

// LVGL screen loads + label changes only become visible after the next
// refresh cycle. The caller (boot flow) typically blocks afterwards (e.g.
// WiFiManager::autoConnect for up to PORTAL_AP_TIMEOUT_S), so we must
// force a synchronous full render pass here before returning.
// lv_timer_handler() alone is not enough because the refresh timer only
// fires every LV_DEF_REFR_PERIOD ms; lv_refr_now() bypasses the timer and
// renders all invalidated areas right now.
static void renderNowBlocking() {
    lv_refr_now(nullptr);
}

void showBoot(const char* line1, const char* line2) {
    if (s_bootLine1) lv_label_set_text(s_bootLine1, line1 ? line1 : "");
    if (s_bootLine2) lv_label_set_text(s_bootLine2, line2 ? line2 : "");
    if (s_scrBoot)   lv_screen_load(s_scrBoot);
    renderNowBlocking();
}

void showPortal(const char* apName, const char* ip) {
    if (s_ptlSsid) {
        lv_label_set_text_fmt(s_ptlSsid, "AP: %s", apName ? apName : "--");
    }
    if (s_ptlIp) {
        lv_label_set_text_fmt(s_ptlIp, "http://%s",
                              ip ? ip : "192.168.4.1");
    }
    if (s_scrPortal) lv_screen_load(s_scrPortal);
    renderNowBlocking();
}

void showDashboard() {
    if (!s_scrDash) return;
    // Reflect persisted LED mode in the dropdown each time the screen is
    // shown (covers factory-reset → dashboard transitions).
    if (s_setLedDrop) {
        const uint8_t idx =
            static_cast<uint8_t>(SettingsStore::get().ledMode);
        lv_dropdown_set_selected(s_setLedDrop, idx > 3 ? 0 : idx);
    }
    lv_screen_load(s_scrDash);
    // Force a full render pass before the main loop's blocking HTTP pollers
    // start running. Without this the boot-screen "WLAN OK" lingers on the
    // panel for 20+ seconds while Shelly/Growatt perform their first poll.
    renderNowBlocking();
}

void showMissingConfig() {
    s_missingConfig = true;
    if (!s_ovHint) return;
    lv_label_set_text(s_ovHint, "Konfiguration fehlt");
    lv_obj_clear_flag(s_ovHint, LV_OBJ_FLAG_HIDDEN);
}

void setBacklightPct(uint8_t pct) {
    if (pct > 100) pct = 100;
    s_backlightPct = pct;
    const uint32_t duty = (pct * 255u) / 100u;
    ledcWrite(TFT_BL_LEDC_CH, duty);
}

uint8_t backlightPct() { return s_backlightPct; }

void renderPage(Page /*page*/,
                const ShellyData& sh,
                const GrowattData& gw) {
    if (!s_scrDash) return;

    // ---- Overview ---------------------------------------------------------
    // All three power values (PV, Grid, House) are frozen together at the
    // PV is frozen at Growatt sync (~60 s cadence) with plausibility
    // correction.  Grid is always live from Shelly (5 s cadence).
    // House is derived: House = PV + Grid, so all three are always
    // mathematically consistent regardless of update cadence mismatch.
    const int pvRaw  = gw.hasData ? (int)gw.pvPowerW      : 0;
    const int gridRaw = sh.hasData ? (int)sh.totalActPower : 0;

    static int      s_frozenPv     = 0;
    static uint32_t s_syncStamp    = 0;
    {
        if (gw.hasData && sh.hasData && gw.lastOkMillis != s_syncStamp) {
            s_syncStamp = gw.lastOkMillis;
            s_frozenPv  = pvRaw;
            // Freeze EMData counters at this sync moment.
            if (sh.hasEnergyData) {
                s_snapImportWh  = sh.totalImportWh;
                s_snapExportWh  = sh.totalExportWh;
                s_snapETodayKwh = gw.eTodayKwh;
                s_hasSnap       = true;
            }
            log_i("SYNC pvRaw=%d grid=%d", s_frozenPv, gridRaw);
        }
    }

    // Plausibility: if exporting more than Growatt PV, Growatt is stale.
    // PV must be at least |export|.  This self-corrects when Growatt
    // catches up because pvRaw will then exceed |export|.
    const int pv    = (gridRaw < 0 && (-gridRaw) > s_frozenPv)
                      ? -gridRaw : s_frozenPv;
    const int grid  = gridRaw;       // always live from Shelly
    const int house = pv + grid;     // derived → always consistent

    auto fmtPower = [](lv_obj_t *lbl, int w) {
        if (!lbl) return;
        const int abs_w = w < 0 ? -w : w;
        if (abs_w >= 1000) {
            lv_label_set_text_fmt(lbl, "%.1f kW", w / 1000.0f);
        } else {
            lv_label_set_text_fmt(lbl, "%d W", w);
        }
    };

    // Helper: format kWh value with unit for tile display (fits 74px).
    auto fmtKwh = [](lv_obj_t *lbl, float kwh) {
        if (!lbl) return;
        if (kwh >= 10.0f) {
            lv_label_set_text_fmt(lbl, "%.0f kWh", kwh);
        } else {
            lv_label_set_text_fmt(lbl, "%.2f kWh", kwh);
        }
    };

    // PV tile: tap toggles live W ↔ daily kWh
    {
        lv_color_t pvCol = cRed();
        if (pv >= 300)      pvCol = cGreen();
        else if (pv > 0)    pvCol = cYellow();
        if (s_ovPv) {
            if (s_pvShowKwh) {
                fmtKwh(s_ovPv, s_dayPvKwh);
            } else {
                fmtPower(s_ovPv, pv);
            }
            lv_obj_set_style_text_color(s_ovPv, pvCol, 0);
        }
        if (s_ovPvTile) lv_obj_set_style_border_color(s_ovPvTile, pvCol, LV_PART_MAIN);
    }

    // House tile: tap toggles live W ↔ daily kWh
    {
        const int h = house > 0 ? house : 0;
        lv_color_t hausCol = cGreen();
        if (h >= 1000)      hausCol = cRed();
        else if (h >= 600)  hausCol = cYellow();
        if (s_ovHouse) {
            if (s_houseShowKwh) {
                fmtKwh(s_ovHouse, s_dayHouseKwh);
            } else {
                fmtPower(s_ovHouse, h);
            }
            lv_obj_set_style_text_color(s_ovHouse, hausCol, 0);
        }
        if (s_ovHouseTile) lv_obj_set_style_border_color(s_ovHouseTile, hausCol, LV_PART_MAIN);
    }

    // Grid tile: tap toggles live W ↔ daily saldo kWh (green=export, red=import)
    if (s_ovGrid) {
        if (s_gridShowKwh) {
            // Saldo: export - import (positive = net export = good)
            const float saldo = s_dayExpKwh - s_dayImpKwh;
            lv_color_t saldoCol = (saldo >= 0) ? cGreen() : cRed();
            const float absSaldo = saldo < 0 ? -saldo : saldo;
            fmtKwh(s_ovGrid, absSaldo);
            lv_obj_set_style_text_color(s_ovGrid, saldoCol, 0);
            if (s_ovGridTile) lv_obj_set_style_border_color(
                s_ovGridTile, saldoCol, LV_PART_MAIN);
        } else {
            lv_color_t gridCol;
            if (grid > 0) {
                if (s_ovGridHdr) lv_label_set_text(s_ovGridHdr, "Bezug");
                fmtPower(s_ovGrid, grid);
                gridCol = cRed();
            } else if (grid < 0) {
                if (s_ovGridHdr) lv_label_set_text(s_ovGridHdr, "Einsp.");
                fmtPower(s_ovGrid, -grid);
                gridCol = cGreen();
            } else {
                if (s_ovGridHdr) lv_label_set_text(s_ovGridHdr, "Bezug");
                lv_label_set_text(s_ovGrid, "0");
                gridCol = cGreen();
            }
            lv_obj_set_style_text_color(s_ovGrid, gridCol, 0);
            if (s_ovGridTile) lv_obj_set_style_border_color(
                s_ovGridTile, gridCol, LV_PART_MAIN);
        }
    }

    // ---- Autarkie (daily self-sufficiency) -----------------------------------
    // Uses absolute energy counters directly — no power integration needed.
    //   PV today:  Growatt eTodayKwh (resets at midnight)
    //   Import:    Shelly EMData total_act      (lifetime Wh, delta from NVS baseline)
    //   Export:    Shelly EMData total_act_ret   (lifetime Wh, delta from NVS baseline)
    //
    //   selfConsumed = PV - export
    //   house        = selfConsumed + import
    //   autarkie%    = selfConsumed / house × 100
    {
        const bool hasEnergy = sh.hasEnergyData && gw.hasData;

        // --- NVS load (once) ---
        if (hasEnergy && !s_dayCountersLoaded) {
            s_dayCountersLoaded = true;
            Preferences dp;
            dp.begin("solday", true);
            float nvsImp = dp.getFloat("imp", 0);
            float nvsExp = dp.getFloat("exp", 0);
            float nvsEtd = dp.getFloat("etd", -1);
            dp.end();

            if (nvsEtd >= 0 && nvsImp > 0 &&
                gw.eTodayKwh >= nvsEtd - 0.5f) {
                // Same day → use stored day-start.
                s_dayStartImportWh  = nvsImp;
                s_dayStartExportWh  = nvsExp;
                s_dayStartETodayKwh = nvsEtd;
                log_i("Autarkie: NVS loaded (etd=%.2f)", nvsEtd);
            } else {
                // New day or first boot.  PV baseline = 0 because Growatt
                // eTodayKwh already counts from midnight.  For Shelly we
                // only have "now" — nighttime import before boot is missed,
                // but export before boot is ~0 (no PV at night) so
                // self = eTodayKwh - exportSinceBoot is close to correct.
                // After the first midnight rollover, NVS will have proper
                // midnight baselines for everything.
                s_dayStartImportWh  = sh.totalImportWh;
                s_dayStartExportWh  = sh.totalExportWh;
                s_dayStartETodayKwh = 0;
                Preferences dpw;
                dpw.begin("solday", false);
                dpw.putFloat("imp", s_dayStartImportWh);
                dpw.putFloat("exp", s_dayStartExportWh);
                dpw.putFloat("etd", 0.0f);
                dpw.end();
                log_i("Autarkie: new snapshot (pv baseline=0)");
            }
            s_lastETodayKwh = gw.eTodayKwh;
        }

        // --- Day rollover detection (eTodayKwh drops at midnight) ---
        if (gw.hasData && s_dayCountersLoaded &&
            s_lastETodayKwh > 0.5f &&
            gw.eTodayKwh < s_lastETodayKwh - 0.5f) {
            if (sh.hasEnergyData) {
                s_dayStartImportWh  = sh.totalImportWh;
                s_dayStartExportWh  = sh.totalExportWh;
                Preferences dp;
                dp.begin("solday", false);
                dp.putFloat("imp", s_dayStartImportWh);
                dp.putFloat("exp", s_dayStartExportWh);
                dp.putFloat("etd", gw.eTodayKwh);
                dp.end();
            }
            s_dayStartETodayKwh = gw.eTodayKwh;
            log_i("Autarkie: day rollover");
        }
        if (gw.hasData) s_lastETodayKwh = gw.eTodayKwh;

        // --- Calculate & display ---
        // Uses frozen snapshot values (s_snapImportWh / s_snapExportWh /
        // s_snapETodayKwh) captured at the Growatt sync moment.
        // Only recalculate when eTodayKwh changes (Growatt bottleneck).
        // Between Growatt updates, Shelly counters grow but PV is stale,
        // which would cause self=PV-Exp to shrink → Autarkie drift.
        static float s_lastSnapEtd = -1;

        if (s_hasSnap && s_dayCountersLoaded &&
            s_snapETodayKwh != s_lastSnapEtd) {
            s_lastSnapEtd = s_snapETodayKwh;

            // Compute deltas directly from snapshots — no monotonic
            // constraints.  All three values are from the same Growatt
            // sync instant.
            s_dayPvKwh  = s_snapETodayKwh - s_dayStartETodayKwh;
            s_dayImpKwh = (s_snapImportWh - s_dayStartImportWh) / 1000.0f;
            s_dayExpKwh = (s_snapExportWh - s_dayStartExportWh) / 1000.0f;
            if (s_dayPvKwh  < 0) s_dayPvKwh  = 0;
            if (s_dayImpKwh < 0) s_dayImpKwh = 0;
            if (s_dayExpKwh < 0) s_dayExpKwh = 0;

            s_autSelfKwh = s_dayPvKwh - s_dayExpKwh;
            if (s_autSelfKwh < 0) s_autSelfKwh = 0;

            s_dayHouseKwh = s_autSelfKwh + s_dayImpKwh;

            float rawPct = (s_dayHouseKwh > 0.01f)
                ? (s_autSelfKwh * 100.0f / s_dayHouseKwh) : 0;
            if (rawPct > 100) rawPct = 100;
            if (rawPct < 0)   rawPct = 0;

            // 3-point moving average to smooth Growatt quantization
            // noise (eTodayKwh has 0.1 kWh steps → ±3% jitter).
            static float s_autBuf[3] = {0, 0, 0};
            static int   s_autBufN   = 0;
            s_autBuf[s_autBufN % 3] = rawPct;
            s_autBufN++;
            const int cnt = (s_autBufN < 3) ? s_autBufN : 3;
            float sum = 0;
            for (int i = 0; i < cnt; i++) sum += s_autBuf[i];
            s_autPct = (int)(sum / cnt + 0.5f);

            log_i("AUT etd=%.2f pv=%.2f imp=%.2f exp=%.2f self=%.2f house=%.2f raw=%.1f%% aut=%d%%",
                  s_snapETodayKwh, s_dayPvKwh, s_dayImpKwh, s_dayExpKwh,
                  s_autSelfKwh, s_dayHouseKwh, rawPct, s_autPct);
        }

        if (s_ovAut && s_dayCountersLoaded) {
            if (s_autShowKwh) {
                fmtKwh(s_ovAut, s_autSelfKwh);
            } else {
                lv_label_set_text_fmt(s_ovAut, "%d%%", s_autPct);
            }

            lv_color_t autCol = cRed();
            if (s_autPct >= 50)      autCol = cGreen();
            else if (s_autPct >= 20) autCol = cYellow();
            lv_obj_set_style_text_color(s_ovAut, autCol, 0);
            if (s_ovAutTile) lv_obj_set_style_border_color(
                s_ovAutTile, autCol, LV_PART_MAIN);
        }
    }

    // PV day history chart – one sample every 10 minutes (48 pts = 8 h).
    if (s_ovChart && s_ovSeries) {
        static uint32_t s_lastChart = 0;
        static int32_t  s_pvPeak    = 2800;  // Y-axis max, starts at rated
        const uint32_t  now         = millis();
        // First call (s_lastChart==0): push an initial point immediately.
        if (s_lastChart == 0 || (now - s_lastChart) >= 600000u) {
            s_lastChart = now;
            const int32_t val = pvRaw > 0 ? pvRaw : 0;
            lv_chart_set_next_value(s_ovChart, s_ovSeries, val);
            if (val > s_pvPeak) s_pvPeak = ((val / 500) + 1) * 500;
            lv_chart_set_range(s_ovChart, LV_CHART_AXIS_PRIMARY_Y, 0, s_pvPeak);
            lv_chart_refresh(s_ovChart);
        }
    }

    setDotColor(s_ovLedSh, freshnessToColor(sh.lastOkMillis));
    setDotColor(s_ovLedGw, freshnessToColor(gw.lastOkMillis));
    // Flow dot
    {
        const uint16_t db = SettingsStore::get().flowDeadbandW;
        lv_color_t c = cRed();
        if (sh.hasData) {
            if      (sh.totalActPower >  (float)db) c = cRed();
            else if (sh.totalActPower < -(float)db) c = cGreen();
            else                                     c = cYellow();
        }
        setDotColor(s_ovLedFl, c);
    }

    if (s_ovHint) {
        if (s_missingConfig) {
            lv_obj_clear_flag(s_ovHint, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_ovHint, LV_OBJ_FLAG_HIDDEN);
        }
    }

    // ---- Growatt ----------------------------------------------------------
    if (s_gwArc) {
        int32_t v = pvRaw;
        if (v < 0) v = 0;
        if (v > 5000) {
            lv_arc_set_range(s_gwArc, 0, 10000);
        } else {
            lv_arc_set_range(s_gwArc, 0, 2800);
        }
        lv_arc_set_value(s_gwArc, v);
        // Dynamic arc color: green → yellow → accent at high load
        lv_color_t arcCol = cGreen();
        if (v > 2000)      arcCol = cAccent();
        else if (v > 1200) arcCol = cYellow();
        lv_obj_set_style_arc_color(s_gwArc, arcCol, LV_PART_INDICATOR);
    }
    if (s_gwArcLbl) {
        lv_label_set_text_fmt(s_gwArcLbl, "%d W", pvRaw);
        // Match arc color
        lv_color_t lblCol = cGreen();
        if (pvRaw > 2000)      lblCol = cAccent();
        else if (pvRaw > 1200) lblCol = cYellow();
        lv_obj_set_style_text_color(s_gwArcLbl, lblCol, 0);
    }
    if (s_gwToday)  lv_label_set_text_fmt(s_gwToday, "%.2f kWh",
                                         (double)gw.eTodayKwh);
    if (s_gwTotal)  lv_label_set_text_fmt(s_gwTotal, "%.1f kWh",
                                         (double)gw.eTotalKwh);

    // ---- Shelly -----------------------------------------------------------
    auto setBar = [](lv_obj_t *bar, lv_obj_t *val, float w) {
        int iw = (int)w;
        int abs = iw < 0 ? -iw : iw;
        if (abs > 3000) abs = 3000;
        lv_bar_set_value(bar, abs, LV_ANIM_OFF);
        lv_label_set_text_fmt(val, "%d W", iw);
    };
    if (s_shBarA) setBar(s_shBarA, s_shLblA, sh.aActPower);
    if (s_shBarB) setBar(s_shBarB, s_shLblB, sh.bActPower);
    if (s_shBarC) setBar(s_shBarC, s_shLblC, sh.cActPower);
    if (s_shTotal) {
        lv_label_set_text_fmt(s_shTotal, "%d W",
                              (int)sh.totalActPower);
    }

    // ---- System -----------------------------------------------------------
    if (s_sysIp) {
        lv_label_set_text(s_sysIp, WiFi.isConnected()
                          ? WiFi.localIP().toString().c_str()
                          : "offline");
    }
    if (s_sysRssi) {
        if (WiFi.isConnected()) {
            lv_label_set_text_fmt(s_sysRssi, "%d dBm", WiFi.RSSI());
        } else {
            lv_label_set_text(s_sysRssi, "--");
        }
    }
    if (s_sysUp) {
        const uint32_t s = millis() / 1000;
        lv_label_set_text_fmt(s_sysUp, "%luh %02lum",
                              (unsigned long)(s / 3600),
                              (unsigned long)((s / 60) % 60));
    }
    if (s_sysHeap) {
        lv_label_set_text_fmt(s_sysHeap, "%u kB",
                              (unsigned)(ESP.getFreeHeap() / 1024));
    }
    if (s_sysSd) {
        const uint8_t ct = SD.cardType();
        if (ct == CARD_NONE || ct == CARD_UNKNOWN) {
            lv_label_set_text(s_sysSd, "---");
        } else {
            lv_label_set_text_fmt(s_sysSd, "%lluMB",
                                  SD.cardSize() / (1024ULL * 1024ULL));
        }
    }
}

}  // namespace Display
