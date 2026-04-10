# Solardisplay

Solar-Monitoring-Dashboard auf einem **ESP32 CYD 2432S028** (2.8" ILI9341 Touchscreen).

Zeigt Echtzeit-Daten von **Growatt** (PV-Anlage) und **Shelly Pro 3EM** (Stromzähler) auf einem modernen Touch-UI mit LVGL und liefert parallel ein Web-Dashboard im Browser.

![PlatformIO](https://img.shields.io/badge/PlatformIO-ESP32-orange)
![LVGL](https://img.shields.io/badge/LVGL-9.2-blue)
![License](https://img.shields.io/badge/License-MIT-green)

---

## Features

- **5-Tab Touch-Dashboard** (LVGL) mit Swipe-Navigation
- **Echtzeit-Energiefluss**: PV-Erzeugung, Netzbezug/-einspeisung, Hausverbrauch
- **Tagesstatistiken**: Autarkie, Eigenverbrauch, Saldo (Tap-to-Toggle auf jeder Kachel)
- **RGB-LED** zeigt Energiefluss-Status (4 Modi)
- **Web-Dashboard** mit Live-Daten, 5-Minuten-Verlaufschart und Konfiguration
- **OTA-Updates** via Browser (`/update`)
- **WiFiManager** Captive-Portal zur Ersteinrichtung
- **NVS-Persistenz** für alle Einstellungen

## Hardware

### Voraussetzungen

- **ESP32 CYD 2432S028** (ILI9341 320x240, XPT2046 Touch, RGB-LED)
- **Growatt** Solaranlage mit Cloud-Zugang (ShineServer)
- **Shelly Pro 3EM** (Gen2) im lokalen Netzwerk

### Pin-Belegung (vorverdrahtet auf dem CYD-Board)

| Funktion                          | GPIO                              |
|-----------------------------------|-----------------------------------|
| TFT SCK / MOSI / MISO            | 14 / 13 / 12                      |
| TFT CS / DC / RST / BL           | 15 / 2 / -1 / 21                  |
| Touch SCK / MOSI / MISO / CS / IRQ | 25 / 32 / 39 / 33 / 36          |
| RGB LED R / G / B                 | 4 / 16 / 17 (common-anode)        |
| SD Card CS                        | 5                                 |
| LDR (optional)                    | 34                                |

## Build & Flash

Voraussetzung: [PlatformIO](https://platformio.org/) (CLI oder VS Code Extension).

```bash
# Build
pio run

# Flash via USB
pio run -t upload

# Serial Monitor
pio device monitor -b 115200
```

## Erstinbetriebnahme

1. Nach dem ersten Flash bootet das Gerät in den AP-Modus **Solardisplay-Setup**.
2. Mit einem Handy verbinden — das Captive-Portal öffnet sich automatisch.
3. WLAN auswählen + Passwort, zusätzlich ausfüllen:
   - **Growatt Benutzer / Passwort** (Cloud-Login)
   - **Shelly Host/IP** (z.B. `192.168.1.42`)
   - **Flow-Schwelle** in Watt (Default: 100 W)
4. Speichern — ESP verbindet sich ins WLAN und zeigt die IP auf dem Display.

## Touch-UI

Das Dashboard hat 5 Tabs:

| Tab  | Inhalt |
|------|--------|
| **Home** | 4 Kacheln: PV, Netz, Haus, Autarkie + Verlaufs-Chart + Status-Dots |
| **PV**   | Growatt-Details: Arc mit PV-Leistung, Ertrag heute/gesamt |
| **Netz** | Shelly Phasen A/B/C als horizontale Bars + Gesamtleistung |
| **Sys**  | IP, RSSI, Uptime, Heap |
| **Cfg**  | LED-Modus, Helligkeit, Config-Portal, Reboot |

### Tap-to-Toggle

Jede Kachel auf dem Home-Tab lässt sich antippen, um zwischen Live-Watt und Tages-kWh umzuschalten:

| Kachel     | Normal      | Nach Tap            |
|------------|-------------|---------------------|
| **PV**     | Leistung (W) | Tageserzeugung (kWh) |
| **Haus**   | Leistung (W) | Tagesverbrauch (kWh) |
| **Bezug**  | Leistung (W) | Saldo (kWh, gruen=Export, rot=Import) |
| **Autarkie** | Prozent (%) | Eigenverbrauch (kWh) |

## LED-Modi

Die on-board RGB-LED unterstützt 4 Modi (umschaltbar im Touch-UI und Web-UI):

| Modus        | Verhalten |
|--------------|-----------|
| **Flow**     | Energiefluss: gruen = Export, gelb = neutral, rot = Bezug |
| **Aggregiert** | Rot wenn Shelly oder Growatt keine Daten liefern, sonst Flow |
| **Rotierend** | Alle 2s wechselnd: Shelly- / Growatt- / Flow-Farbe |
| **Aus**      | LED komplett aus |

## Web-UI

Erreichbar unter `http://<esp-ip>/` (Basic-Auth, User: `admin`).
Das Admin-Passwort wird beim Erstboot zufällig generiert und im Serial-Log ausgegeben.

| Route            | Beschreibung |
|------------------|-------------|
| `/`              | Live-Dashboard mit Leistung, Phasen, Verlaufschart, System-Info |
| `/config`        | Einstellungen: Growatt, Shelly, Flow-Schwelle, LED-Modus, Passwort |
| `/update`        | OTA Firmware-Update (Datei-Upload) |
| `/api/status`    | JSON-API fuer externe Integration |
| `POST /reboot`   | Neustart |
| `POST /factory-reset` | Alles loeschen + Neustart ins Setup-Portal |

## OTA-Updates

Firmware kann remote via Browser aktualisiert werden:

1. Projekt bauen: `pio run`
2. Im Browser `http://<esp-ip>/update` oeffnen
3. Datei `.pio/build/esp32dev/firmware.bin` hochladen
4. Geraet flasht und startet automatisch neu

## Projektstruktur

```
solardisplay/
├── include/
│   ├── config.h          # Hardware-Pins, Intervalle, Schwellwerte
│   └── lv_conf.h         # LVGL-Konfiguration (16-bit, Fonts, Widgets)
├── src/
│   ├── main.cpp          # Entry-Point, Init-Reihenfolge, Main-Loop
│   ├── model.h           # Datenstrukturen (ShellyData, GrowattData, Enums)
│   ├── display.{h,cpp}   # LVGL Touch-UI (Boot, Portal, Dashboard)
│   ├── growatt.{h,cpp}   # Growatt Cloud API Client (HTTPS, MD5-Auth)
│   ├── shelly.{h,cpp}    # Shelly Pro 3EM lokaler HTTP/RPC Client
│   ├── portal.{h,cpp}    # WiFiManager + Web-Server + OTA
│   ├── settings.{h,cpp}  # NVS-Persistenz (Preferences)
│   ├── leds.{h,cpp}      # RGB-LED PWM-Steuerung (4 Modi)
│   ├── ui.{h,cpp}        # UI-Refresh-Orchestrator
│   └── net.{h,cpp}       # WiFi-Watchdog (Auto-Reconnect)
├── scripts/
│   └── patch_lvgl.py     # Neutralisiert ARM-SIMD-Assembly fuer ESP32
├── platformio.ini        # Build-Konfiguration + Dependencies
├── LICENSE
└── README.md
```

## Architektur

```
┌─────────────┐     ┌──────────────┐
│ Growatt Cloud│     │ Shelly 3EM   │
│  (HTTPS/60s) │     │ (HTTP/5s)    │
└──────┬──────┘     └──────┬───────┘
       │                   │
       └─────────┬─────────┘
                 │
          ┌──────▼──────┐
          │   main.cpp  │  Poll-Sync: Growatt zuerst,
          │  (loop)     │  dann sofort Shelly nachladen
          └──────┬──────┘
                 │
    ┌────────────┼────────────┐
    │            │            │
┌───▼───┐  ┌────▼────┐  ┌────▼────┐
│Display │  │ Portal  │  │  LEDs   │
│(LVGL) │  │(WebServer│  │(RGB PWM)│
│Touch UI│  │ + OTA)  │  │ 4 Modi  │
└────────┘  └─────────┘  └─────────┘
                 │
          ┌──────▼──────┐
          │  Settings   │
          │   (NVS)     │
          └─────────────┘
```

## Datenquellen

### Growatt (Cloud API)
- Endpoint: `server.growatt.com` (HTTPS, inoffizielle API)
- Authentifizierung: MD5-gehashtes Passwort, Session-Cookie
- Daten: PV-Leistung (W), Tagesertrag (kWh), Gesamtertrag (kWh)
- Polling: alle 60 Sekunden mit Exponential-Backoff bei Fehlern
- **Hinweis**: Die API ist inoffiziell und kann sich aendern

### Shelly Pro 3EM (lokales Netzwerk)
- Endpoint: `http://<host>/rpc/EM.GetStatus?id=0` (Gen2 RPC)
- Zusätzlich: `EMData.GetStatus` fuer Lifetime-Energiezaehler
- Daten: Phasenleistung A/B/C (W), Gesamtleistung, Import/Export (Wh)
- Polling: alle 5 Sekunden

## Abhängigkeiten

| Bibliothek | Version | Zweck |
|-----------|---------|-------|
| [LVGL](https://lvgl.io/) | 9.2.2 | Touch-UI Framework |
| [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI) | ^2.5.43 | TFT-Display-Treiber |
| [ArduinoJson](https://arduinojson.org/) | ^7.1.0 | JSON-Parsing (Shelly/Growatt) |
| [WiFiManager](https://github.com/tzapu/WiFiManager) | ^2.0.17 | Captive-Portal zur WLAN-Einrichtung |
| [XPT2046_Touchscreen](https://github.com/PaulStoffregen/XPT2046_Touchscreen) | latest | Touch-Controller-Treiber |

## Hinweise

- HTTPS zu `server.growatt.com` laeuft ohne Zertifikats-Validierung (`setInsecure()`).
- TFT_eSPI wird self-contained über Build-Flags konfiguriert (kein `User_Setup.h`).
- Die LVGL-Konfiguration liegt in `include/lv_conf.h`.
- Partitionsschema: `min_spiffs.csv` (2x OTA-Slots, je ~1.9 MB).

## Lizenz

MIT License — siehe [LICENSE](LICENSE).
