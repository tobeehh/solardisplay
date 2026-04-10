#include "settings.h"
#include "config.h"

#include <Preferences.h>

namespace {
Preferences prefs;
Settings    s_current;

String randomPassword(size_t len = 8) {
    static const char charset[] =
        "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789";
    String out;
    out.reserve(len);
    for (size_t i = 0; i < len; ++i) {
        out += charset[esp_random() % (sizeof(charset) - 1)];
    }
    return out;
}

LedMode ledModeFromU8(uint8_t v) {
    switch (v) {
        case 0: return LedMode::Flow;
        case 1: return LedMode::Aggregated;
        case 2: return LedMode::Rotating;
        case 3: return LedMode::Off;
        default: return LedMode::Flow;
    }
}
}  // namespace

namespace SettingsStore {

void begin() {
    prefs.begin(NVS_NAMESPACE, /*readOnly=*/false);

    s_current.growattUser   = prefs.getString("gw_user", "");
    s_current.growattPass   = prefs.getString("gw_pass", "");
    s_current.shellyHost    = prefs.getString("shelly_host", "");
    s_current.flowDeadbandW = prefs.getUShort("flow_dead_w",
                                              POWER_FLOW_DEADBAND_W_DEFAULT);
    s_current.adminPass     = prefs.getString("admin_pass", "");
    s_current.ledMode       = ledModeFromU8(
        prefs.getUChar("led_mode", static_cast<uint8_t>(LedMode::Flow)));

    if (s_current.adminPass.isEmpty()) {
        s_current.adminPass = randomPassword();
        prefs.putString("admin_pass", s_current.adminPass);
        log_i("Generated initial admin password: %s",
              s_current.adminPass.c_str());
    }
}

const Settings& get() { return s_current; }

void save(const Settings& s) {
    s_current = s;
    prefs.putString("gw_user", s.growattUser);
    prefs.putString("gw_pass", s.growattPass);
    prefs.putString("shelly_host", s.shellyHost);
    prefs.putUShort("flow_dead_w", s.flowDeadbandW);
    prefs.putString("admin_pass", s.adminPass);
    prefs.putUChar("led_mode", static_cast<uint8_t>(s.ledMode));
}

void setGrowattCredentials(const String& user, const String& pass) {
    s_current.growattUser = user;
    s_current.growattPass = pass;
    prefs.putString("gw_user", user);
    prefs.putString("gw_pass", pass);
}

void setShellyHost(const String& host) {
    s_current.shellyHost = host;
    prefs.putString("shelly_host", host);
}

void setFlowDeadband(uint16_t watts) {
    s_current.flowDeadbandW = watts;
    prefs.putUShort("flow_dead_w", watts);
}

void setAdminPass(const String& pass) {
    s_current.adminPass = pass;
    prefs.putString("admin_pass", pass);
}

void setLedMode(LedMode mode) {
    s_current.ledMode = mode;
    prefs.putUChar("led_mode", static_cast<uint8_t>(mode));
}


void factoryReset() {
    prefs.clear();
    s_current = {};
    s_current.flowDeadbandW    = POWER_FLOW_DEADBAND_W_DEFAULT;
    s_current.ledMode          = LedMode::Flow;
    s_current.adminPass        = randomPassword();
    prefs.putString("admin_pass", s_current.adminPass);
    prefs.putUChar("led_mode", static_cast<uint8_t>(s_current.ledMode));
}

bool isConfigured() {
    return !s_current.growattUser.isEmpty() &&
           !s_current.growattPass.isEmpty() &&
           !s_current.shellyHost.isEmpty();
}

}  // namespace SettingsStore
