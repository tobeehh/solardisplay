#include "growatt.h"
#include "config.h"
#include "settings.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <MD5Builder.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

// NOTE: The Growatt ShineServer endpoints used here are unofficial and may
// change without notice. The general flow is:
//   1. POST /newTwoLoginAPI.do         → JSESSIONID cookie
//      (userName=.. & password=<modified MD5 of raw password>)
//   2. POST /index/getPlantListTitle   → first plantId
//   3. POST /panel/getDevicesByPlantList → deviceSn + live data
//
// Password hashing quirk (per community-maintained clients like
// indykoning/PyPi_GrowattServer): take lowercase hex MD5 of the password and
// replace every '0' at an even index with 'c'. The Growatt backend rejects
// plaintext passwords on this endpoint.
//
// This module implements a minimal, defensive version: any parse error or
// non-200 response is treated as "not ok" and causes a backoff + relogin on
// the next attempt.

namespace {

constexpr const char* kBaseHost = "server.growatt.com";

String   s_cookie;              // "JSESSIONID=..."
String   s_plantId;
String   s_deviceSn;

uint32_t s_lastPoll      = 0;
uint32_t s_nextAllowedMs = 0;   // backoff gate
uint32_t s_backoffMs     = 0;
bool     s_forceRefresh  = true;

void scheduleBackoff(uint32_t baseMs) {
    if (s_backoffMs == 0) s_backoffMs = baseMs;
    else s_backoffMs = min<uint32_t>(s_backoffMs * 2, 5UL * 60UL * 1000UL);
    s_nextAllowedMs = millis() + s_backoffMs;
}

void clearBackoff() {
    s_backoffMs     = 0;
    s_nextAllowedMs = 0;
}

bool beginRequest(HTTPClient& http, WiFiClientSecure& client,
                  const String& path) {
    client.setInsecure();  // no cert validation; simplest path on ESP32
    String url = "https://";
    url += kBaseHost;
    url += path;
    if (!http.begin(client, url)) return false;
    http.setConnectTimeout(HTTP_TIMEOUT_MS);
    http.setTimeout(HTTP_TIMEOUT_MS);
    http.addHeader("User-Agent",
                   "Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
                   "AppleWebKit/537.36 (KHTML, like Gecko) "
                   "Chrome/120.0.0.0 Safari/537.36");
    http.addHeader("Accept", "application/json, text/plain, */*");
    http.addHeader("Accept-Language", "en-US,en;q=0.9");
    http.addHeader("Content-Type",
                   "application/x-www-form-urlencoded; charset=UTF-8");
    http.addHeader("Origin",  "https://server.growatt.com");
    http.addHeader("Referer", "https://server.growatt.com/index");
    if (!s_cookie.isEmpty()) http.addHeader("Cookie", s_cookie);
    const char* collect[] = {"Set-Cookie"};
    http.collectHeaders(collect, 1);
    return true;
}

// Merge a "name=value" pair into s_cookie, replacing any existing value for
// the same name. s_cookie is a semicolon-separated Cookie: header value.
void mergeCookiePair(const String& pair) {
    if (pair.isEmpty()) return;
    const int eq = pair.indexOf('=');
    if (eq <= 0) return;
    const String name = pair.substring(0, eq);

    // Remove any existing pair with the same name.
    int start = 0;
    while (start < (int)s_cookie.length()) {
        int sep = s_cookie.indexOf(';', start);
        if (sep < 0) sep = s_cookie.length();
        String seg = s_cookie.substring(start, sep);
        seg.trim();
        const int segEq = seg.indexOf('=');
        if (segEq > 0 && seg.substring(0, segEq) == name) {
            int removeStart = start;
            int removeEnd   = (sep < (int)s_cookie.length()) ? sep + 1 : sep;
            if (removeStart > 0 && s_cookie.charAt(removeStart - 1) == ' ')
                removeStart--;
            if (removeStart > 0 && s_cookie.charAt(removeStart - 1) == ';')
                removeStart--;
            s_cookie.remove(removeStart, removeEnd - removeStart);
            sep = removeStart;  // loop will reread from this point
        }
        start = sep + 1;
    }

    if (!s_cookie.isEmpty()) s_cookie += "; ";
    s_cookie += pair;
}

// Extract the session-identifying name=value pair from a Set-Cookie header
// (called by HTTPClient-based requests). Note: HTTPClient only keeps ONE
// Set-Cookie per response; if the server sends several, we lose all but one.
// For the login specifically we use rawHttpsPostLogin() below which captures
// every Set-Cookie line manually.
void captureCookie(HTTPClient& http) {
    const String sc = http.header("Set-Cookie");
    if (sc.isEmpty()) return;
    const int end = sc.indexOf(';');
    const String pair = (end < 0) ? sc : sc.substring(0, end);
    mergeCookiePair(pair);
}

// Perform the login POST with a raw HTTPS request so we can capture *all*
// Set-Cookie headers from the response (HTTPClient::collectHeaders only keeps
// one value per header name, which drops important cookies on Growatt's
// multi-cookie login response). Fills s_cookie with every cookie received.
// Returns the body payload string on success, empty on failure.
String rawHttpsPostLogin(const String& body, String& outError) {
    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(HTTP_TIMEOUT_MS / 1000);
    if (!client.connect(kBaseHost, 443)) {
        outError = "connect fail";
        return String();
    }

    // Build HTTP/1.1 request manually.
    String req;
    req.reserve(512 + body.length());
    req += "POST /newTwoLoginAPI.do HTTP/1.1\r\n";
    req += "Host: "; req += kBaseHost; req += "\r\n";
    req += "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
           "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 "
           "Safari/537.36\r\n";
    req += "Accept: application/json, text/plain, */*\r\n";
    req += "Accept-Language: en-US,en;q=0.9\r\n";
    req += "Origin: https://"; req += kBaseHost; req += "\r\n";
    req += "Referer: https://"; req += kBaseHost; req += "/login\r\n";
    req += "Content-Type: application/x-www-form-urlencoded; charset=UTF-8\r\n";
    req += "Content-Length: "; req += String(body.length()); req += "\r\n";
    req += "Connection: close\r\n";
    req += "\r\n";
    req += body;
    client.print(req);

    // Read status line.
    const uint32_t deadline = millis() + HTTP_TIMEOUT_MS;
    String statusLine;
    while (client.connected() && !client.available()) {
        if ((int32_t)(millis() - deadline) > 0) {
            outError = "read timeout";
            client.stop();
            return String();
        }
        delay(5);
    }
    statusLine = client.readStringUntil('\n');
    statusLine.trim();
    if (!statusLine.startsWith("HTTP/")) {
        outError = "bad status";
        client.stop();
        return String();
    }
    const int sp1 = statusLine.indexOf(' ');
    const int sp2 = (sp1 > 0) ? statusLine.indexOf(' ', sp1 + 1) : -1;
    const int code = (sp1 > 0)
        ? statusLine.substring(sp1 + 1, sp2 > 0 ? sp2 : statusLine.length()).toInt()
        : 0;
    if (code != 200) {
        outError = String("HTTP ") + code;
        client.stop();
        return String();
    }

    // Read headers. Capture every Set-Cookie line.
    size_t contentLength = 0;
    bool   chunked       = false;
    while (client.connected()) {
        if ((int32_t)(millis() - deadline) > 0) {
            outError = "header timeout";
            client.stop();
            return String();
        }
        String line = client.readStringUntil('\n');
        // Trim trailing \r
        if (line.endsWith("\r")) line.remove(line.length() - 1);
        if (line.isEmpty()) break;  // end of headers

        // Header name is case-insensitive.
        const int colon = line.indexOf(':');
        if (colon < 0) continue;
        String name  = line.substring(0, colon);
        String value = line.substring(colon + 1);
        value.trim();
        name.toLowerCase();

        if (name == "set-cookie") {
            const int end = value.indexOf(';');
            const String pair = (end < 0) ? value : value.substring(0, end);
            mergeCookiePair(pair);
        } else if (name == "content-length") {
            contentLength = (size_t)value.toInt();
        } else if (name == "transfer-encoding") {
            value.toLowerCase();
            if (value.indexOf("chunked") >= 0) chunked = true;
        }
    }

    // Read body.
    String payload;
    if (chunked) {
        // Minimal chunked decoder.
        while (client.connected()) {
            if ((int32_t)(millis() - deadline) > 0) break;
            String sizeLine = client.readStringUntil('\n');
            if (sizeLine.endsWith("\r")) sizeLine.remove(sizeLine.length() - 1);
            sizeLine.trim();
            if (sizeLine.isEmpty()) continue;
            const long sz = strtol(sizeLine.c_str(), nullptr, 16);
            if (sz <= 0) break;
            payload.reserve(payload.length() + sz);
            long remaining = sz;
            while (remaining > 0 && client.connected()) {
                if ((int32_t)(millis() - deadline) > 0) break;
                if (client.available()) {
                    payload += (char)client.read();
                    remaining--;
                } else {
                    delay(2);
                }
            }
            // Discard trailing CRLF after the chunk
            client.readStringUntil('\n');
        }
    } else {
        const size_t want = contentLength > 0 ? contentLength : 16384;
        payload.reserve(want);
        while (client.connected() && payload.length() < want) {
            if ((int32_t)(millis() - deadline) > 0) break;
            while (client.available() && payload.length() < want) {
                payload += (char)client.read();
            }
            if (contentLength > 0 && payload.length() >= contentLength) break;
            delay(2);
        }
    }

    client.stop();
    return payload;
}

// Compute the Growatt-flavoured MD5 of a raw password: lowercase hex MD5
// with every '0' at an even index replaced with 'c'.
String hashGrowattPassword(const String& pw) {
    MD5Builder md5;
    md5.begin();
    md5.add(pw);
    md5.calculate();
    String hex = md5.toString();  // lowercase 32-char hex
    for (int i = 0; i < (int)hex.length(); i += 2) {
        if (hex.charAt(i) == '0') hex.setCharAt(i, 'c');
    }
    return hex;
}

// ----- step 1: login -------------------------------------------------------
bool login(GrowattData& out) {
    const Settings& cfg = SettingsStore::get();
    if (cfg.growattUser.isEmpty() || cfg.growattPass.isEmpty()) {
        out.lastError = "no credentials";
        return false;
    }

    String body = "userName=";
    body += cfg.growattUser;
    body += "&password=";
    body += hashGrowattPassword(cfg.growattPass);

    s_cookie.clear();
    String rawErr;
    const String payload = rawHttpsPostLogin(body, rawErr);
    if (payload.isEmpty()) {
        out.lastError = String("login ") + rawErr;
        return false;
    }

    JsonDocument doc;
    auto err = deserializeJson(doc, payload);
    if (err) {
        out.lastError = String("login JSON ") + err.c_str();
        return false;
    }
    // Success shape: {"back":{"success":true,"data":[{"plantId":"..",...}],
    //                         "user":{...},...}}
    // Failure shape: {"back":{"success":false,"msg":"501|502|...","error":""}}
    //   501=user not found, 502=wrong password, 507=rate-limited/UA blocked.
    JsonVariant back = doc["back"];
    if (!back.is<JsonObject>() || !(back["success"] | false)) {
        const char* msg = back["msg"] | "denied";
        out.lastError = String("login ") + msg;
        return false;
    }
    if (s_cookie.isEmpty()) {
        out.lastError = "no cookie";
        return false;
    }

    // Plant list is included in the login response — grab the first plantId
    // directly so we can skip the separate /index/getPlantListTitle call.
    JsonVariant plants = back["data"];
    if (plants.is<JsonArray>() && plants.as<JsonArray>().size() > 0) {
        s_plantId = String((const char*)(plants[0]["plantId"] | ""));
    }
    log_i("Growatt login OK, plantId=%s", s_plantId.c_str());
    out.loggedIn = true;
    return true;
}

// ----- step 2: live data via API endpoint ----------------------------------
// newTwoLoginAPI.do cookies are valid for the *API* routes (all paths ending
// in *API.do). /newTwoPlantAPI.do?op=getAllDeviceList returns the plant's
// inverter list with live pac/eToday/eTotal fields in one call.
bool fetchLiveData(GrowattData& out) {
    WiFiClientSecure client;
    HTTPClient http;

    String path = "/newTwoPlantAPI.do?op=getAllDeviceList&plantId=";
    path += s_plantId;
    path += "&pageNum=1&pageSize=10";

    if (!beginRequest(http, client, path)) {
        out.lastError = "dev begin";
        return false;
    }
    const int code = http.GET();
    if (code != 200) {
        http.end();
        out.lastError = String("dev HTTP ") + code;
        return false;
    }
    const String payload = http.getString();
    http.end();

    JsonDocument doc;
    auto err = deserializeJson(doc, payload);
    if (err) {
        out.lastError = String("dev JSON ") + err.c_str();
        return false;
    }

    // Plant-level totals are at the top level of the response:
    //   "invTodayPpv": current PV power across all inverters, watts
    //   "todayEnergy": today's generated energy, kWh
    //   "totalEnergy": lifetime generated energy, kWh
    //   "deviceList":  list of inverters (we record the first SN)
    auto numeric = [&](const char* key) -> float {
        JsonVariant v = doc[key];
        if (v.is<float>() || v.is<int>()) return v.as<float>();
        if (v.is<const char*>()) return String((const char*)v).toFloat();
        return 0.0f;
    };

    JsonVariant list = doc["deviceList"];
    if (list.is<JsonArray>() && list.as<JsonArray>().size() > 0) {
        s_deviceSn = String((const char*)(list[0]["deviceSn"] | ""));
    }

    out.pvPowerW     = numeric("invTodayPpv");
    out.eTodayKwh    = numeric("todayEnergy");
    out.eTotalKwh    = numeric("totalEnergy");
    out.hasData      = true;
    out.lastOkMillis = millis();
    out.lastError    = "";
    return true;
}

bool runOneCycle(GrowattData& out) {
    if (!out.loggedIn) {
        if (!login(out)) return false;
    }
    if (s_plantId.isEmpty()) {
        out.lastError = "no plantId";
        out.loggedIn  = false;
        return false;
    }
    if (!fetchLiveData(out)) { out.loggedIn = false; return false; }
    return true;
}

}  // namespace

namespace Growatt {

void begin() {
    s_cookie.clear();
    s_plantId.clear();
    s_deviceSn.clear();
    // Delay the first poll by one interval so the dashboard UI has a chance
    // to render before the (blocking) HTTPS login/plant/device flow runs.
    s_lastPoll     = millis();
    s_forceRefresh = true;   // poll immediately on first tick
    clearBackoff();
}

void logout() {
    s_cookie.clear();
    s_plantId.clear();
    s_deviceSn.clear();
}

void requestRefresh() { s_forceRefresh = true; }

void tick(GrowattData& out) {
    const uint32_t now = millis();
    if (WiFi.status() != WL_CONNECTED) return;
    if (!s_forceRefresh && (now - s_lastPoll) < GROWATT_POLL_MS) return;
    if (s_nextAllowedMs && (int32_t)(now - s_nextAllowedMs) < 0) return;

    s_forceRefresh = false;
    s_lastPoll     = now;

    if (runOneCycle(out)) {
        clearBackoff();
    } else {
        log_w("Growatt cycle failed: %s", out.lastError.c_str());
        scheduleBackoff(30UL * 1000UL);
    }
}

}  // namespace Growatt
