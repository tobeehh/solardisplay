#include "net.h"
#include "config.h"

#include <WiFi.h>

namespace {
uint32_t s_lastCheck    = 0;
uint32_t s_lastConnectAt = 0;
}

namespace Net {

bool begin() {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.persistent(true);
    WiFi.begin();  // uses previously stored credentials from WiFiManager
    return WiFi.status() == WL_CONNECTED;
}

void tick() {
    const uint32_t now = millis();
    if ((now - s_lastCheck) < WIFI_WATCHDOG_MS) return;
    s_lastCheck = now;

    if (WiFi.status() == WL_CONNECTED) {
        s_lastConnectAt = now;
        return;
    }
    // Only kick a reconnect every 10s to avoid flapping
    if ((now - s_lastConnectAt) > 10000) {
        log_w("WiFi down, attempting reconnect");
        WiFi.disconnect();
        WiFi.begin();
        s_lastConnectAt = now;
    }
}

}  // namespace Net
