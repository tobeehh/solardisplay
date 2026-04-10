#include "shelly.h"
#include "config.h"
#include "settings.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>

namespace {

uint32_t s_lastPoll       = 0;
bool     s_forceRefresh   = true;

bool doRequest(ShellyData& out) {
    const String& host = SettingsStore::get().shellyHost;
    if (host.isEmpty()) {
        out.lastError = "host missing";
        return false;
    }

    String url = "http://";
    url += host;
    url += "/rpc/EM.GetStatus?id=0";

    HTTPClient http;
    http.setConnectTimeout(HTTP_TIMEOUT_MS);
    http.setTimeout(HTTP_TIMEOUT_MS);

    if (!http.begin(url)) {
        out.lastError = "begin fail";
        return false;
    }

    const int code = http.GET();
    if (code != 200) {
        http.end();
        out.lastError = String("HTTP ") + code;
        return false;
    }

    // Stream parse to keep RAM low.
    JsonDocument doc;
    const DeserializationError err =
        deserializeJson(doc, http.getStream());
    http.end();

    if (err) {
        out.lastError = String("JSON ") + err.c_str();
        return false;
    }

    out.aActPower      = doc["a_act_power"]    | 0.0f;
    out.bActPower      = doc["b_act_power"]    | 0.0f;
    out.cActPower      = doc["c_act_power"]    | 0.0f;
    out.totalActPower  = doc["total_act_power"]| 0.0f;
    // Gen2 returns Wh; convert to kWh. Not all firmwares provide this.
    const float totalWh = doc["total_act"] | 0.0f;
    if (totalWh > 0.0f) out.totalEnergyKwh = totalWh / 1000.0f;

    out.hasData      = true;
    out.lastOkMillis = millis();
    out.lastError    = "";
    return true;
}

// Fetch lifetime energy counters from Shelly EMData endpoint (Gen2).
// Returns import (total_act) and export (total_act_ret) in Wh.
// This is best-effort — not all Shelly models support this endpoint.
bool doEnergyRequest(ShellyData& out) {
    const String& host = SettingsStore::get().shellyHost;
    if (host.isEmpty()) return false;

    String url = "http://";
    url += host;
    url += "/rpc/EMData.GetStatus?id=0";

    HTTPClient http;
    http.setConnectTimeout(HTTP_TIMEOUT_MS);
    http.setTimeout(HTTP_TIMEOUT_MS);
    if (!http.begin(url)) return false;

    const int code = http.GET();
    if (code != 200) { http.end(); return false; }

    JsonDocument doc;
    const DeserializationError err =
        deserializeJson(doc, http.getStream());
    http.end();
    if (err) return false;

    out.totalImportWh  = doc["total_act"]     | 0.0f;
    out.totalExportWh  = doc["total_act_ret"] | 0.0f;
    out.hasEnergyData  = true;
    return true;
}

}  // namespace

namespace Shelly {

void begin() {
    // Delay the first poll by one interval so the dashboard UI has a chance
    // to render before the (blocking) HTTP request hogs the main loop.
    s_lastPoll     = millis();
    s_forceRefresh = false;
}

void requestRefresh() { s_forceRefresh = true; }

void tick(ShellyData& out) {
    const uint32_t now = millis();
    if (!s_forceRefresh && (now - s_lastPoll) < SHELLY_POLL_MS) return;
    if (WiFi.status() != WL_CONNECTED) return;

    s_forceRefresh = false;
    s_lastPoll     = now;

    if (!doRequest(out)) {
        log_w("Shelly poll failed: %s", out.lastError.c_str());
    } else {
        // Energy counters are best-effort; non-fatal if unavailable.
        if (!doEnergyRequest(out)) {
            log_d("Shelly EMData not available (non-fatal)");
        }
    }
}

}  // namespace Shelly
