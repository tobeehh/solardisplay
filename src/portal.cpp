#include "portal.h"
#include "config.h"
#include "settings.h"
#include "display.h"
#include "growatt.h"
#include "shelly.h"

#include <WiFi.h>
#include <WebServer.h>
#include <WiFiManager.h>
#include <Update.h>

namespace {

WebServer    s_server(80);
WiFiManager  s_wm;

ShellyData   s_snapSh;
GrowattData  s_snapGw;

// ---- In-RAM history (ring buffer) ----------------------------------------
// 60 samples × 5 s push interval = 5 min rolling window.
constexpr uint8_t kHistSize = 60;
struct HistSample {
    int16_t pv;
    int16_t grid;
};
HistSample s_hist[kHistSize] = {};
uint8_t    s_histCount   = 0;
uint8_t    s_histHead    = 0;   // next write index
uint32_t   s_histLastPush = 0;

void histPush(int16_t pv, int16_t grid) {
    const uint32_t now = millis();
    if (s_histCount > 0 && (now - s_histLastPush) < 5000) return;
    s_histLastPush = now;
    s_hist[s_histHead] = {pv, grid};
    s_histHead = (s_histHead + 1) % kHistSize;
    if (s_histCount < kHistSize) s_histCount++;
}

// WiFiManager custom parameters – kept alive for the lifetime of the portal.
WiFiManagerParameter s_pGwUser("gw_user",  "Growatt Benutzer", "", 64);
WiFiManagerParameter s_pGwPass("gw_pass",  "Growatt Passwort", "", 64,
                               "type=\"password\"");
WiFiManagerParameter s_pShelly("shelly",   "Shelly Host/IP",   "", 64);
WiFiManagerParameter s_pFlow  ("flow",     "Flow Schwelle [W]","100", 6);

bool s_shouldSave = false;

void onSaveParams() { s_shouldSave = true; }

// ---- Helpers --------------------------------------------------------------

String htmlEscape(const String& in) {
    String out;
    out.reserve(in.length());
    for (size_t i = 0; i < in.length(); ++i) {
        const char c = in[i];
        switch (c) {
            case '<': out += "&lt;";  break;
            case '>': out += "&gt;";  break;
            case '&': out += "&amp;"; break;
            case '"': out += "&quot;";break;
            default:  out += c;       break;
        }
    }
    return out;
}

bool authenticate() {
    const String& pw = SettingsStore::get().adminPass;
    if (!s_server.authenticate(WEB_USER, pw.c_str())) {
        s_server.requestAuthentication();
        return false;
    }
    return true;
}

const char* freshnessName(uint32_t lastOkMs) {
    if (lastOkMs == 0) return "red";
    const uint32_t age = millis() - lastOkMs;
    if (age < STATUS_GREEN_MS)  return "green";
    if (age < STATUS_YELLOW_MS) return "yellow";
    return "red";
}

const char* flowColorName() {
    if (!s_snapSh.hasData) return "red";
    const float p = s_snapSh.totalActPower;
    const uint16_t db = SettingsStore::get().flowDeadbandW;
    if (p >  (float)db) return "red";
    if (p < -(float)db) return "green";
    return "yellow";
}

const char* ledModeName(LedMode m) {
    switch (m) {
        case LedMode::Flow:       return "flow";
        case LedMode::Aggregated: return "aggregated";
        case LedMode::Rotating:   return "rotating";
        case LedMode::Off:        return "off";
    }
    return "flow";
}

LedMode ledModeFromString(const String& s) {
    if (s == "flow")       return LedMode::Flow;
    if (s == "aggregated") return LedMode::Aggregated;
    if (s == "rotating")   return LedMode::Rotating;
    if (s == "off")        return LedMode::Off;
    return LedMode::Flow;
}

// ---- CSS (shared across all pages) ---------------------------------------

static const char CSS_COMMON[] PROGMEM = R"CSS(
:root{--bg:#0b0f14;--panel:#151c24;--panel2:#1e2730;--text:#e6edf3;
--muted:#7d8796;--accent:#58d0ff;--green:#4ade80;--yellow:#facc15;
--red:#f87171;--border:#263040}
*{box-sizing:border-box}
body{margin:0;font-family:-apple-system,system-ui,Segoe UI,Roboto,sans-serif;
background:var(--bg);color:var(--text);padding:16px;max-width:720px;
margin-left:auto;margin-right:auto;-webkit-font-smoothing:antialiased}
nav{display:flex;gap:16px;margin-bottom:18px;border-bottom:1px solid var(--border);
padding-bottom:10px}
nav a{color:var(--muted);text-decoration:none;font-size:.95em;
padding:4px 0;border-bottom:2px solid transparent}
nav a:hover{color:var(--text)}
nav a.active{color:var(--accent);border-bottom-color:var(--accent)}
h1{font-size:.85em;font-weight:500;color:var(--muted);margin:0 0 16px;
letter-spacing:1.2px;text-transform:uppercase}
h2{margin:0 0 10px;font-size:.72em;color:var(--muted);
text-transform:uppercase;letter-spacing:1px;font-weight:500}
.grid{display:grid;grid-template-columns:repeat(3,1fr);gap:12px;margin-bottom:14px}
.tile{background:var(--panel);border:1px solid var(--border);
border-radius:14px;padding:18px 16px;position:relative;overflow:hidden}
.tile .label{font-size:.7em;color:var(--muted);
text-transform:uppercase;letter-spacing:1px}
.tile .value{font-size:1.9em;font-weight:600;margin-top:8px;
font-variant-numeric:tabular-nums;line-height:1.1}
.tile .unit{font-size:.48em;color:var(--muted);margin-left:6px;font-weight:400}
.tile.pv .value{color:var(--green)}
.tile.netz .value.imp{color:var(--red)}
.tile.netz .value.exp{color:var(--green)}
.tile.haus .value{color:var(--accent)}
.row{display:flex;gap:12px;margin-bottom:14px;flex-wrap:wrap}
.card{flex:1 1 220px;background:var(--panel);border:1px solid var(--border);
border-radius:14px;padding:16px}
.card.wide{flex:2 1 360px}
.leds{display:flex;justify-content:space-around;gap:8px;padding:8px 0}
.led{display:flex;flex-direction:column;align-items:center;gap:8px;
font-size:.7em;color:var(--muted);text-transform:uppercase;letter-spacing:.5px}
.dot{width:26px;height:26px;border-radius:50%;background:#2a333e;
box-shadow:inset 0 0 6px rgba(0,0,0,.6);transition:all .3s}
.dot.green{background:var(--green);box-shadow:0 0 14px rgba(74,222,128,.7),inset 0 0 4px rgba(0,0,0,.3)}
.dot.yellow{background:var(--yellow);box-shadow:0 0 14px rgba(250,204,21,.7),inset 0 0 4px rgba(0,0,0,.3)}
.dot.red{background:var(--red);box-shadow:0 0 14px rgba(248,113,113,.7),inset 0 0 4px rgba(0,0,0,.3)}
.spark{width:100%;height:70px;display:block}
.stats{display:flex;flex-direction:column;gap:0;font-size:.88em}
.stats .k{color:var(--muted)}
.stats .v{color:var(--text);font-variant-numeric:tabular-nums}
.stats div{display:flex;justify-content:space-between;padding:6px 0;
border-bottom:1px solid var(--border)}
.stats div:last-child{border-bottom:none}
button{background:var(--panel2);color:var(--text);border:1px solid var(--border);
border-radius:8px;padding:10px 16px;cursor:pointer;font-size:.9em;
font-family:inherit;margin-top:8px}
button:hover{background:#26303c;border-color:#3a4656}
button.danger{border-color:#5a2a2a;color:#fca5a5}
button.danger:hover{background:#3a1f1f;border-color:#7a3a3a}
input{width:100%;padding:10px 12px;background:var(--panel2);color:var(--text);
border:1px solid var(--border);border-radius:8px;font-size:.95em;font-family:inherit}
input:focus{outline:none;border-color:var(--accent)}
label{display:block;margin-top:14px;font-size:.75em;color:var(--muted);
text-transform:uppercase;letter-spacing:.5px}
label input{margin-top:6px;text-transform:none}
label small{text-transform:none;color:var(--muted);font-size:.9em;margin-left:6px}
.hint{color:var(--muted);font-size:.8em;margin:8px 0}
hr{border:none;border-top:1px solid var(--border);margin:20px 0}
.footer{color:var(--muted);font-size:.7em;text-align:center;margin-top:20px;
text-transform:uppercase;letter-spacing:1px}
.badge{display:inline-block;padding:2px 8px;border-radius:10px;
font-size:.7em;text-transform:uppercase;letter-spacing:.5px}
.badge.ok{background:rgba(74,222,128,.15);color:var(--green)}
.badge.bad{background:rgba(248,113,113,.15);color:var(--red)}
@media(max-width:560px){.grid{grid-template-columns:1fr}.tile .value{font-size:1.6em}}
)CSS";

// ---- Dashboard HTML (static; data fetched via /api/status) ---------------

static const char DASHBOARD_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="de"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Solardisplay</title><style>%CSS%</style></head><body>
<nav><a href="/" class="active">Status</a><a href="/config">Konfiguration</a><a href="/update">Update</a></nav>
<h1>Solardisplay</h1>
<div class="grid">
 <div class="tile pv"><div class="label">PV</div><div class="value" id="pv">–<span class="unit">W</span></div></div>
 <div class="tile netz"><div class="label">Netz</div><div class="value" id="grid">–<span class="unit">W</span></div></div>
 <div class="tile haus"><div class="label">Haus</div><div class="value" id="house">–<span class="unit">W</span></div></div>
</div>
<div class="row">
 <div class="card wide"><h2>Verlauf PV (5 min)</h2>
  <svg class="spark" id="spark" viewBox="0 0 300 70" preserveAspectRatio="none"></svg>
 </div>
 <div class="card"><h2>Status</h2><div class="leds">
  <div class="led"><div class="dot" id="led-sh"></div>Shelly</div>
  <div class="led"><div class="dot" id="led-gw"></div>Growatt</div>
  <div class="led"><div class="dot" id="led-fl"></div>Fluss</div>
 </div></div>
</div>
<div class="row">
 <div class="card"><h2>Ertrag</h2><div class="stats">
  <div><span class="k">Heute</span><span class="v" id="today">–</span></div>
  <div><span class="k">Gesamt</span><span class="v" id="total">–</span></div>
 </div></div>
 <div class="card"><h2>Shelly Phasen</h2><div class="stats">
  <div><span class="k">Phase A</span><span class="v" id="pa">–</span></div>
  <div><span class="k">Phase B</span><span class="v" id="pb">–</span></div>
  <div><span class="k">Phase C</span><span class="v" id="pc">–</span></div>
 </div></div>
 <div class="card"><h2>System</h2><div class="stats">
  <div><span class="k">IP</span><span class="v" id="ip">–</span></div>
  <div><span class="k">RSSI</span><span class="v" id="rssi">–</span></div>
  <div><span class="k">Uptime</span><span class="v" id="up">–</span></div>
  <div><span class="k">Heap</span><span class="v" id="heap">–</span></div>
 </div></div>
</div>
<form method="POST" action="/reboot"><button>Reboot</button></form>
<div class="footer">Auto-Refresh alle 3 s</div>
<script>
const $=id=>document.getElementById(id);
const fmtW=v=>v==null?'–':((v<0?'-':'')+Math.abs(v|0).toLocaleString('de-DE'));
const fmtKwh=(v,d=1)=>v==null?'–':v.toFixed(d).replace('.',',')+' kWh';
const fmtUp=s=>{const h=s/3600|0,m=(s/60|0)%60;return h+'h '+String(m).padStart(2,'0')+'m'};
function spark(data){
 const svg=$('spark');svg.innerHTML='';
 if(!data||data.length<2)return;
 const W=300,H=70;
 let mn=Math.min(...data),mx=Math.max(...data);
 if(mx-mn<10){mx=mn+10}
 const r=mx-mn,st=W/(data.length-1);
 let d='M';
 data.forEach((v,i)=>{const x=(i*st).toFixed(1);const y=(H-((v-mn)/r)*(H-6)-3).toFixed(1);d+=(i?' L':'')+x+','+y});
 const ns='http://www.w3.org/2000/svg';
 const f=document.createElementNS(ns,'path');
 f.setAttribute('d',d+' L'+W+','+H+' L0,'+H+' Z');
 f.setAttribute('fill','#4ade80');f.setAttribute('opacity','.15');svg.appendChild(f);
 const p=document.createElementNS(ns,'path');
 p.setAttribute('d',d);p.setAttribute('fill','none');
 p.setAttribute('stroke','#4ade80');p.setAttribute('stroke-width','1.8');
 p.setAttribute('stroke-linejoin','round');svg.appendChild(p);
}
function setLed(id,c){$(id).className='dot '+(c||'')}
async function refresh(){
 try{
  const r=await fetch('/api/status',{cache:'no-store'});
  if(!r.ok)return;
  const j=await r.json();
  $('pv').innerHTML=fmtW(j.pv)+'<span class="unit">W</span>';
  const g=j.grid,ge=$('grid');
  const arrow=g<-10?' ↑':(g>10?' ↓':'');
  ge.innerHTML=fmtW(Math.abs(g))+arrow+'<span class="unit">W</span>';
  ge.className='value '+(g<-10?'exp':(g>10?'imp':''));
  $('house').innerHTML=fmtW(j.house)+'<span class="unit">W</span>';
  $('today').textContent=fmtKwh(j.today,2);
  $('total').textContent=fmtKwh(j.total,1);
  $('pa').textContent=fmtW(j.shelly.a)+' W';
  $('pb').textContent=fmtW(j.shelly.b)+' W';
  $('pc').textContent=fmtW(j.shelly.c)+' W';
  $('ip').textContent=j.sys.ip;
  $('rssi').textContent=j.sys.rssi+' dBm';
  $('up').textContent=fmtUp(j.sys.up);
  $('heap').textContent=(j.sys.heap/1024|0)+' kB';
  setLed('led-sh',j.leds.shelly);
  setLed('led-gw',j.leds.growatt);
  setLed('led-fl',j.leds.flow);
  spark(j.hist);
 }catch(e){}
}
refresh();setInterval(refresh,3000);
</script></body></html>)HTML";

// Send a page body wrapped in the standard shell with CSS injected.
void sendPage(const __FlashStringHelper* bodyHtml, const char* title,
              const char* activeNav) {
    String html;
    html.reserve(4096);
    html += F("<!doctype html><html lang=\"de\"><head><meta charset=\"utf-8\">");
    html += F("<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">");
    html += F("<title>");
    html += title;
    html += F("</title><style>");
    html += FPSTR(CSS_COMMON);
    html += F("</style></head><body><nav>");
    html += String("<a href=\"/\"") +
            (strcmp(activeNav, "status") == 0 ? " class=\"active\"" : "") +
            ">Status</a>";
    html += String("<a href=\"/config\"") +
            (strcmp(activeNav, "config") == 0 ? " class=\"active\"" : "") +
            ">Konfiguration</a>";
    html += String("<a href=\"/update\"") +
            (strcmp(activeNav, "update") == 0 ? " class=\"active\"" : "") +
            ">Update</a>";
    html += F("</nav><h1>");
    html += title;
    html += F("</h1>");
    html += bodyHtml;
    html += F("</body></html>");
    s_server.send(200, "text/html", html);
}

void sendPageStr(const String& body, const char* title, const char* activeNav) {
    String html;
    html.reserve(4096 + body.length());
    html += F("<!doctype html><html lang=\"de\"><head><meta charset=\"utf-8\">");
    html += F("<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">");
    html += F("<title>");
    html += title;
    html += F("</title><style>");
    html += FPSTR(CSS_COMMON);
    html += F("</style></head><body><nav>");
    html += String("<a href=\"/\"") +
            (strcmp(activeNav, "status") == 0 ? " class=\"active\"" : "") +
            ">Status</a>";
    html += String("<a href=\"/config\"") +
            (strcmp(activeNav, "config") == 0 ? " class=\"active\"" : "") +
            ">Konfiguration</a>";
    html += String("<a href=\"/update\"") +
            (strcmp(activeNav, "update") == 0 ? " class=\"active\"" : "") +
            ">Update</a>";
    html += F("</nav><h1>");
    html += title;
    html += F("</h1>");
    html += body;
    html += F("</body></html>");
    s_server.send(200, "text/html", html);
}

// ---- Routes --------------------------------------------------------------

void handleRoot() {
    if (!authenticate()) return;
    // Replace %CSS% placeholder with the common CSS block.
    String html;
    html.reserve(10240);
    const char* p = DASHBOARD_HTML;
    while (true) {
        const char* tag = strstr_P(p, PSTR("%CSS%"));
        if (!tag) { html += FPSTR(p); break; }
        while (p < tag) html += (char)pgm_read_byte(p++);
        html += FPSTR(CSS_COMMON);
        p += 5;  // skip "%CSS%"
    }
    s_server.send(200, "text/html", html);
}

void handleApiStatus() {
    if (!authenticate()) return;

    const int   pvRaw = s_snapGw.hasData ? (int)s_snapGw.pvPowerW       : 0;
    const int   grid  = s_snapSh.hasData ? (int)s_snapSh.totalActPower  : 0;
    // When exporting more than Growatt reports, Growatt is stale → infer PV.
    const int   pv    = (grid < 0 && (-grid) > pvRaw) ? -grid : pvRaw;
    const int   house = (s_snapGw.hasData || s_snapSh.hasData)
                        ? pv + grid : 0;

    String j;
    j.reserve(1024);
    j += '{';
    j += "\"pv\":";    j += pv;
    j += ",\"grid\":"; j += grid;
    j += ",\"house\":";j += house;
    j += ",\"today\":"; j += String(s_snapGw.eTodayKwh, 2);
    j += ",\"total\":"; j += String(s_snapGw.eTotalKwh, 1);

    j += ",\"shelly\":{\"a\":";
    j += (int)s_snapSh.aActPower;
    j += ",\"b\":"; j += (int)s_snapSh.bActPower;
    j += ",\"c\":"; j += (int)s_snapSh.cActPower;
    j += ",\"ok\":"; j += (s_snapSh.hasData ? "true" : "false");
    j += '}';

    j += ",\"growatt\":{\"ok\":";
    j += (s_snapGw.hasData ? "true" : "false");
    j += '}';

    j += ",\"leds\":{\"shelly\":\"";
    j += freshnessName(s_snapSh.lastOkMillis);
    j += "\",\"growatt\":\"";
    j += freshnessName(s_snapGw.lastOkMillis);
    j += "\",\"flow\":\"";
    j += flowColorName();
    j += "\"}";

    j += ",\"ledMode\":\"";
    j += ledModeName(SettingsStore::get().ledMode);
    j += "\"";

    j += ",\"sys\":{\"ip\":\"";
    j += WiFi.localIP().toString();
    j += "\",\"rssi\":"; j += WiFi.RSSI();
    j += ",\"up\":";     j += (millis() / 1000);
    j += ",\"heap\":";   j += ESP.getFreeHeap();
    j += '}';

    // PV history (oldest → newest)
    j += ",\"hist\":[";
    for (uint8_t i = 0; i < s_histCount; ++i) {
        const uint8_t idx =
            (s_histHead + kHistSize - s_histCount + i) % kHistSize;
        if (i) j += ',';
        j += s_hist[idx].pv;
    }
    j += "]}";

    s_server.sendHeader("Cache-Control", "no-store");
    s_server.send(200, "application/json", j);
}

void handleConfigGet() {
    if (!authenticate()) return;
    const Settings& s = SettingsStore::get();

    String body;
    body.reserve(2048);
    body += F("<div class=\"card\">"
              "<form method=\"POST\" action=\"/config\">");
    body += F("<label>Growatt Benutzer<input name=\"gw_user\" value=\"");
    body += htmlEscape(s.growattUser);
    body += F("\"></label>");
    body += F("<label>Growatt Passwort<small>(leer = unverändert)</small>"
              "<input name=\"gw_pass\" type=\"password\" value=\"\"></label>");
    body += F("<label>Shelly Host/IP<input name=\"shelly\" value=\"");
    body += htmlEscape(s.shellyHost);
    body += F("\"></label>");
    body += F("<label>Flow-Schwelle [W]<input name=\"flow\" type=\"number\" value=\"");
    body += String(s.flowDeadbandW);
    body += F("\"></label>");

    body += F("<label>LED Modus<select name=\"led_mode\" style=\""
              "width:100%;padding:10px 12px;background:var(--panel2);"
              "color:var(--text);border:1px solid var(--border);"
              "border-radius:8px;font-size:.95em;font-family:inherit;"
              "margin-top:6px\">");
    struct { const char *val; const char *label; LedMode m; } opts[] = {
        {"flow",       "Flow (gruen/gelb/rot nach Stromfluss)", LedMode::Flow},
        {"aggregated", "Aggregiert (rot bei stale Daten)",      LedMode::Aggregated},
        {"rotating",   "Rotierend (Shelly/Growatt/Flow)",       LedMode::Rotating},
        {"off",        "Aus",                                   LedMode::Off},
    };
    for (auto& o : opts) {
        body += F("<option value=\"");
        body += o.val;
        body += F("\"");
        if (s.ledMode == o.m) body += F(" selected");
        body += F(">");
        body += o.label;
        body += F("</option>");
    }
    body += F("</select></label>");

    body += F("<label>Neues Admin-Passwort<small>(leer = unverändert)</small>"
              "<input name=\"admin\" type=\"password\" value=\"\"></label>");
    body += F("<button>Speichern</button></form></div>");
    body += F("<div class=\"card\" style=\"margin-top:12px\">"
              "<h2>Gefahrenzone</h2>"
              "<p class=\"hint\">Löscht WLAN-Daten, Credentials und "
              "Admin-Passwort. Gerät startet neu.</p>"
              "<form method=\"POST\" action=\"/factory-reset\" "
              "onsubmit=\"return confirm('Wirklich alles löschen?')\">"
              "<button class=\"danger\">Factory Reset</button></form></div>");
    sendPageStr(body, "Konfiguration", "config");
}

void handleConfigPost() {
    if (!authenticate()) return;

    Settings s = SettingsStore::get();
    if (s_server.hasArg("gw_user")) s.growattUser = s_server.arg("gw_user");
    if (s_server.hasArg("gw_pass")) {
        const String p = s_server.arg("gw_pass");
        if (!p.isEmpty()) s.growattPass = p;
    }
    if (s_server.hasArg("shelly")) s.shellyHost = s_server.arg("shelly");
    if (s_server.hasArg("flow")) {
        const long v = s_server.arg("flow").toInt();
        if (v >= 0 && v <= 65535) s.flowDeadbandW = (uint16_t)v;
    }
    if (s_server.hasArg("led_mode")) {
        s.ledMode = ledModeFromString(s_server.arg("led_mode"));
    }
    if (s_server.hasArg("admin")) {
        const String p = s_server.arg("admin");
        if (!p.isEmpty()) s.adminPass = p;
    }
    SettingsStore::save(s);

    Growatt::logout();
    Growatt::requestRefresh();
    Shelly::requestRefresh();

    String body;
    body += F("<div class=\"card\"><p>"
              "<span class=\"badge ok\">gespeichert</span></p>"
              "<p class=\"hint\">Einstellungen wurden übernommen.</p>"
              "<a href=\"/config\"><button>Zurück</button></a></div>");
    sendPageStr(body, "Gespeichert", "config");
}

void handleReboot() {
    if (!authenticate()) return;
    String body = F("<div class=\"card\"><p>Gerät startet neu …</p></div>");
    sendPageStr(body, "Reboot", "status");
    delay(300);
    ESP.restart();
}

void handleFactoryReset() {
    if (!authenticate()) return;
    SettingsStore::factoryReset();
    s_wm.resetSettings();
    String body = F("<div class=\"card\"><p>"
                    "<span class=\"badge bad\">gelöscht</span></p>"
                    "<p class=\"hint\">Gerät startet neu ins Setup-Portal.</p>"
                    "</div>");
    sendPageStr(body, "Factory Reset", "config");
    delay(500);
    ESP.restart();
}

// ---- OTA Firmware Update ---------------------------------------------------

void handleUpdateGet() {
    if (!authenticate()) return;
    sendPage(F(
        "<div class=\"card\">"
        "<h2>Firmware Update</h2>"
        "<p class=\"hint\">Wähle eine .bin-Datei aus und lade sie hoch. "
        "Das Gerät startet nach dem Update automatisch neu.</p>"
        "<form method=\"POST\" action=\"/update\" enctype=\"multipart/form-data\">"
        "<input type=\"file\" name=\"firmware\" accept=\".bin\" "
        "style=\"margin-bottom:12px\">"
        "<button>Upload &amp; Flash</button>"
        "</form></div>"), "Firmware Update", "update");
}

void handleUpdatePost() {
    if (!authenticate()) return;
    const bool ok = !Update.hasError();
    String body;
    if (ok) {
        body = F("<div class=\"card\"><p>"
                 "<span class=\"badge ok\">OK</span></p>"
                 "<p class=\"hint\">Firmware erfolgreich geflasht. "
                 "Gerät startet neu …</p></div>");
    } else {
        body = F("<div class=\"card\"><p>"
                 "<span class=\"badge bad\">Fehler</span></p>"
                 "<p class=\"hint\">Update fehlgeschlagen.</p>"
                 "<a href=\"/update\"><button>Zurück</button></a></div>");
    }
    sendPageStr(body, "Firmware Update", "update");
    if (ok) { delay(500); ESP.restart(); }
}

void handleUpdateUpload() {
    if (!authenticate()) return;
    HTTPUpload& upload = s_server.upload();
    if (upload.status == UPLOAD_FILE_START) {
        log_i("OTA start: %s", upload.filename.c_str());
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
            Update.printError(Serial);
        }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (Update.write(upload.buf, upload.currentSize) !=
            upload.currentSize) {
            Update.printError(Serial);
        }
    } else if (upload.status == UPLOAD_FILE_END) {
        if (Update.end(true)) {
            log_i("OTA success: %u bytes", upload.totalSize);
        } else {
            Update.printError(Serial);
        }
    }
}

}  // namespace

namespace Portal {

void runProvisioning() {
    const Settings& s = SettingsStore::get();
    s_pGwUser.setValue(s.growattUser.c_str(), 64);
    s_pShelly.setValue(s.shellyHost.c_str(), 64);

    s_wm.addParameter(&s_pGwUser);
    s_wm.addParameter(&s_pGwPass);
    s_wm.addParameter(&s_pShelly);
    s_wm.addParameter(&s_pFlow);
    s_wm.setSaveParamsCallback(onSaveParams);
    s_wm.setConfigPortalTimeout(PORTAL_AP_TIMEOUT_S);
    s_wm.setBreakAfterConfig(true);

    Display::showPortal(PORTAL_AP_NAME, "192.168.4.1");

    if (!s_wm.autoConnect(PORTAL_AP_NAME)) {
        log_w("WiFiManager autoConnect failed / timeout, restarting");
        delay(1000);
        ESP.restart();
    }

    if (s_shouldSave) {
        Settings ns = SettingsStore::get();
        ns.growattUser = s_pGwUser.getValue();
        const String newPass = s_pGwPass.getValue();
        if (!newPass.isEmpty()) ns.growattPass = newPass;
        ns.shellyHost = s_pShelly.getValue();
        const long flow = String(s_pFlow.getValue()).toInt();
        if (flow >= 0 && flow <= 65535) ns.flowDeadbandW = (uint16_t)flow;
        SettingsStore::save(ns);
        s_shouldSave = false;
    }
}

void startWebUi() {
    s_server.on("/",              HTTP_GET,  handleRoot);
    s_server.on("/api/status",    HTTP_GET,  handleApiStatus);
    s_server.on("/config",        HTTP_GET,  handleConfigGet);
    s_server.on("/config",        HTTP_POST, handleConfigPost);
    s_server.on("/reboot",        HTTP_POST, handleReboot);
    s_server.on("/factory-reset", HTTP_POST, handleFactoryReset);
    s_server.on("/update",        HTTP_GET,  handleUpdateGet);
    s_server.on("/update",        HTTP_POST, handleUpdatePost, handleUpdateUpload);
    s_server.begin();
    log_i("Web UI on http://%s", WiFi.localIP().toString().c_str());
    log_i("Admin password: %s", SettingsStore::get().adminPass.c_str());
}

void tick() { s_server.handleClient(); }

void openConfigPortal() {
    Display::showPortal(PORTAL_AP_NAME, "192.168.4.1");
    s_wm.setConfigPortalTimeout(PORTAL_AP_TIMEOUT_S);
    s_wm.startConfigPortal(PORTAL_AP_NAME);
    if (s_shouldSave) {
        Settings ns = SettingsStore::get();
        ns.growattUser = s_pGwUser.getValue();
        const String newPass = s_pGwPass.getValue();
        if (!newPass.isEmpty()) ns.growattPass = newPass;
        ns.shellyHost = s_pShelly.getValue();
        const long flow = String(s_pFlow.getValue()).toInt();
        if (flow >= 0 && flow <= 65535) ns.flowDeadbandW = (uint16_t)flow;
        SettingsStore::save(ns);
        s_shouldSave = false;
        Growatt::logout();
        Growatt::requestRefresh();
        Shelly::requestRefresh();
    }
}

void setDataSnapshot(const ShellyData& sh, const GrowattData& gw) {
    s_snapSh = sh;
    s_snapGw = gw;

    // Push a history sample at most every 5 s; only store when we have
    // at least one real data source to avoid a long zero-only prefix.
    if (sh.hasData || gw.hasData) {
        const int16_t pv   = gw.hasData ? (int16_t)gw.pvPowerW      : 0;
        const int16_t grid = sh.hasData ? (int16_t)sh.totalActPower : 0;
        histPush(pv, grid);
    }
}

}  // namespace Portal
