#include "RuntimeConfig.h"

#include <DNSServer.h>
#include <Preferences.h>
#include <TFT_eSPI.h>
#include <WebServer.h>
#include <WiFi.h>

// Timezone objects are defined in NTP_Time.h; we re-declare as extern here so
// this TU never includes that header (which has additional global definitions
// — UDP, packet buffer — that must remain in a single translation unit).
extern Timezone UK;
extern Timezone euCET;
extern Timezone ausET;
extern Timezone usET;
extern Timezone usCT;
extern Timezone usMT;
extern Timezone usAZ;
extern Timezone usPT;
extern Timezone IST;

extern TFT_eSPI tft;

Config cfg;

namespace {

constexpr const char* NVS_NS               = "cyd-weather";
constexpr const char* KEY_CONFIGURED       = "ready";
constexpr const char* KEY_SSID             = "ssid";
constexpr const char* KEY_PASS             = "pass";
constexpr const char* KEY_API              = "apikey";
constexpr const char* KEY_LAT              = "lat";
constexpr const char* KEY_LON              = "lon";
constexpr const char* KEY_UNITS            = "units";
constexpr const char* KEY_TZ               = "tz";
constexpr const char* KEY_NIGHT_OFF        = "noff";
constexpr const char* KEY_NIGHT_ON         = "non";
constexpr const char* WIFI_PLACEHOLDER     = "-your-router-name-";
constexpr const char* AP_SSID              = "cyd-weather-setup";
constexpr uint8_t     DNS_PORT             = 53;

struct TzEntry { const char* name; Timezone* tz; };

const TzEntry TZ_TABLE[] = {
  { "UK",    &UK    },
  { "euCET", &euCET },
  { "ausET", &ausET },
  { "usET",  &usET  },
  { "usCT",  &usCT  },
  { "usMT",  &usMT  },
  { "usAZ",  &usAZ  },
  { "usPT",  &usPT  },
  { "IST",   &IST   },
};

Timezone* tzLookup(const String& name, Timezone* fallback) {
  for (const auto& e : TZ_TABLE) {
    if (name == e.name) return e.tz;
  }
  return fallback;
}

// Compile-time defaults captured by configBegin() — used when NVS is empty or
// when an NVS value is missing/invalid, and as fallback for currentTZ().
struct Defaults {
  String    ssid, pass, api, lat, lon, units, tzName;
  Timezone* tz = nullptr;
  int       nightOff = 23;
  int       nightOn  = 6;
} g_def;

bool g_ready = false;

DNSServer  dnsServer;
WebServer  webServer(80);

String htmlEscape(const String& s) {
  String out;
  out.reserve(s.length());
  for (size_t i = 0; i < s.length(); ++i) {
    char c = s[i];
    switch (c) {
      case '&':  out += "&amp;";  break;
      case '<':  out += "&lt;";   break;
      case '>':  out += "&gt;";   break;
      case '"':  out += "&quot;"; break;
      case '\'': out += "&#39;";  break;
      default:   out += c;
    }
  }
  return out;
}

String renderForm(const String& notice = "") {
  String ssidOpts;
  int n = WiFi.scanComplete();
  if (n < 0) {
    WiFi.scanNetworks(/*async=*/true);
  } else {
    for (int i = 0; i < n; ++i) {
      ssidOpts += "<option value=\"" + htmlEscape(WiFi.SSID(i)) + "\">"
               + htmlEscape(WiFi.SSID(i)) + " (" + String(WiFi.RSSI(i)) + " dBm)</option>";
    }
  }

  String tzOpts;
  for (const auto& e : TZ_TABLE) {
    tzOpts += "<option value=\"";
    tzOpts += e.name;
    tzOpts += "\"></option>";
  }

  String noticeBlock;
  if (notice.length()) {
    noticeBlock = "<p class=\"notice\">" + htmlEscape(notice) + "</p>";
  }

  String html;
  html.reserve(4096);
  html += F("<!doctype html><html><head><meta charset=\"utf-8\">"
            "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
            "<title>CYD Weather Setup</title>"
            "<style>"
            "body{font-family:system-ui,sans-serif;max-width:32rem;margin:1.5rem auto;padding:0 1rem;color:#222;background:#fafafa}"
            "h1{font-size:1.3rem;margin:0 0 1rem}"
            "label{display:block;margin:0.75rem 0 0.25rem;font-weight:600}"
            "input,select{width:100%;padding:0.5rem;font-size:1rem;box-sizing:border-box;border:1px solid #bbb;border-radius:4px}"
            ".row{display:flex;gap:0.5rem}.row>div{flex:1}"
            "button{margin-top:1.25rem;padding:0.75rem;font-size:1rem;width:100%;background:#1976d2;color:#fff;border:0;border-radius:4px;cursor:pointer}"
            ".hint{font-size:0.8rem;color:#666;margin-top:0.15rem}"
            ".notice{background:#fff3cd;border:1px solid #e6c200;padding:0.5rem 0.75rem;border-radius:4px}"
            "</style></head><body>");
  html += "<h1>CYD Weather Setup</h1>";
  html += noticeBlock;
  html += "<form method=\"POST\" action=\"/save\">";

  html += "<label>WiFi network</label>";
  if (ssidOpts.length()) {
    html += "<select name=\"ssid\" required>" + ssidOpts + "</select>";
  } else {
    html += "<input name=\"ssid\" value=\"" + htmlEscape(cfg.wifiSsid) + "\" required>";
    html += "<p class=\"hint\">Scanning… you can also type the SSID manually.</p>";
  }

  html += "<label>WiFi password</label>";
  html += "<input name=\"pass\" type=\"password\" value=\"" + htmlEscape(cfg.wifiPassword) + "\">";

  html += "<label>OpenWeatherMap API key</label>";
  html += "<input name=\"apikey\" value=\"" + htmlEscape(cfg.apiKey) + "\" required>";
  html += "<p class=\"hint\">Free key from openweathermap.org.</p>";

  html += "<div class=\"row\">";
  html += "<div><label>Latitude</label><input name=\"lat\" value=\"" + htmlEscape(cfg.latitude) + "\" required></div>";
  html += "<div><label>Longitude</label><input name=\"lon\" value=\"" + htmlEscape(cfg.longitude) + "\" required></div>";
  html += "</div>";

  html += "<label>Units</label>";
  html += "<select name=\"units\">";
  html += "<option value=\"metric\"";
  if (cfg.units == "metric") html += " selected";
  html += ">metric (°C, m/s)</option>";
  html += "<option value=\"imperial\"";
  if (cfg.units == "imperial") html += " selected";
  html += ">imperial (°F, mph)</option>";
  html += "</select>";

  html += "<label>Timezone</label>";
  html += "<input name=\"tz\" list=\"tzlist\" value=\"" + htmlEscape(cfg.tzName) + "\">";
  html += "<datalist id=\"tzlist\">" + tzOpts + "</datalist>";
  html += "<p class=\"hint\">Pick from the list or type one. Unknown values default to ";
  html += htmlEscape(g_def.tzName);
  html += ".</p>";

  html += "<div class=\"row\">";
  html += "<div><label>Sleep start (hour, screen off at HH:59)</label>"
          "<input name=\"noff\" type=\"number\" min=\"0\" max=\"23\" value=\"" + String(cfg.nightOffHour) + "\" required></div>";
  html += "<div><label>Sleep end (hour, screen on at HH:00)</label>"
          "<input name=\"non\" type=\"number\" min=\"0\" max=\"23\" value=\"" + String(cfg.nightOnHour) + "\" required></div>";
  html += "</div>";

  html += "<button type=\"submit\">Save &amp; Reboot</button>";
  html += "</form></body></html>";
  return html;
}

void handleRoot() {
  webServer.send(200, "text/html", renderForm());
}

void handleSave() {
  String ssid = webServer.arg("ssid");
  if (!ssid.length()) {
    webServer.send(400, "text/html", renderForm("SSID is required."));
    return;
  }
  String tzName = webServer.arg("tz");
  Timezone* tz = tzLookup(tzName, g_def.tz);
  String tzNotice;
  if (tz != tzLookup(tzName, nullptr)) {
    // typed value didn't match any known zone
    tzNotice = String("Unknown timezone '") + tzName + "', defaulted to " + g_def.tzName + ".";
    tzName = g_def.tzName;
    tz = g_def.tz;
  }

  int noff = webServer.arg("noff").toInt();
  int non  = webServer.arg("non").toInt();
  if (noff < 0 || noff > 23) noff = g_def.nightOff;
  if (non  < 0 || non  > 23) non  = g_def.nightOn;

  Preferences p;
  p.begin(NVS_NS, /*readOnly=*/false);
  p.putString(KEY_SSID,  ssid);
  p.putString(KEY_PASS,  webServer.arg("pass"));
  p.putString(KEY_API,   webServer.arg("apikey"));
  p.putString(KEY_LAT,   webServer.arg("lat"));
  p.putString(KEY_LON,   webServer.arg("lon"));
  p.putString(KEY_UNITS, webServer.arg("units"));
  p.putString(KEY_TZ,    tzName);
  p.putInt   (KEY_NIGHT_OFF, noff);
  p.putInt   (KEY_NIGHT_ON,  non);
  p.putBool  (KEY_CONFIGURED, true);
  p.end();

  String body = "<!doctype html><meta charset=\"utf-8\">"
                "<title>Saved</title>"
                "<body style=\"font-family:system-ui;max-width:24rem;margin:2rem auto;padding:0 1rem\">"
                "<h2>Saved.</h2>";
  if (tzNotice.length()) {
    body += "<p>" + htmlEscape(tzNotice) + "</p>";
  }
  body += "<p>The device will restart and connect to your WiFi now.</p></body>";
  webServer.send(200, "text/html", body);
  delay(800);
  ESP.restart();
}

void drawPortalScreen(const IPAddress& ip) {
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setTextFont(4);
  tft.drawString("Setup needed", 120, 40);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextFont(2);
  tft.drawString("Connect to WiFi AP:", 120, 90);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextFont(4);
  tft.drawString(AP_SSID, 120, 115);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextFont(2);
  tft.drawString("then open in browser:", 120, 165);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextFont(4);
  tft.drawString(ip.toString(), 120, 190);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.setTextFont(2);
  tft.drawString("(captive portal will pop up", 120, 240);
  tft.drawString("automatically on most phones)", 120, 260);
}

}  // namespace

void configBegin(const char* defSsid,
                 const char* defPass,
                 const String& defApiKey,
                 const String& defLat,
                 const String& defLon,
                 const String& defUnits,
                 const char*  defTzName,
                 Timezone*    defTz,
                 int defNightOff,
                 int defNightOn) {
  g_def.ssid     = defSsid;
  g_def.pass     = defPass;
  g_def.api      = defApiKey;
  g_def.lat      = defLat;
  g_def.lon      = defLon;
  g_def.units    = defUnits;
  g_def.tzName   = defTzName;
  g_def.tz       = defTz ? defTz : tzLookup(defTzName, &usCT);
  g_def.nightOff = defNightOff;
  g_def.nightOn  = defNightOn;

  // Seed cfg with the compile-time defaults so the normal-install path
  // (no NVS, real values in All_Settings.h) Just Works.
  cfg.wifiSsid      = g_def.ssid;
  cfg.wifiPassword  = g_def.pass;
  cfg.apiKey        = g_def.api;
  cfg.latitude      = g_def.lat;
  cfg.longitude     = g_def.lon;
  cfg.units         = g_def.units;
  cfg.tzName        = g_def.tzName;
  cfg.tz            = g_def.tz;
  cfg.nightOffHour  = g_def.nightOff;
  cfg.nightOnHour   = g_def.nightOn;

  Preferences p;
  if (!p.begin(NVS_NS, /*readOnly=*/true)) {
    g_ready = !configIsPlaceholder();
    return;
  }
  bool configured = p.getBool(KEY_CONFIGURED, false);
  if (configured) {
    cfg.wifiSsid     = p.getString(KEY_SSID,  cfg.wifiSsid);
    cfg.wifiPassword = p.getString(KEY_PASS,  cfg.wifiPassword);
    cfg.apiKey       = p.getString(KEY_API,   cfg.apiKey);
    cfg.latitude     = p.getString(KEY_LAT,   cfg.latitude);
    cfg.longitude    = p.getString(KEY_LON,   cfg.longitude);
    cfg.units        = p.getString(KEY_UNITS, cfg.units);
    cfg.tzName       = p.getString(KEY_TZ,    cfg.tzName);
    cfg.tz           = tzLookup(cfg.tzName, g_def.tz);
    cfg.nightOffHour = p.getInt   (KEY_NIGHT_OFF, cfg.nightOffHour);
    cfg.nightOnHour  = p.getInt   (KEY_NIGHT_ON,  cfg.nightOnHour);
  }
  p.end();

  g_ready = configured || !configIsPlaceholder();
}

bool configIsPlaceholder() {
  return cfg.wifiSsid == WIFI_PLACEHOLDER || cfg.wifiSsid.length() == 0;
}

bool configIsReady() {
  return g_ready;
}

Timezone* currentTZ() {
  if (cfg.tz) return cfg.tz;
  return g_def.tz ? g_def.tz : &usCT;
}

void startCaptivePortal() {
  Serial.print("Starting captive portal AP: ");
  Serial.println(AP_SSID);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID);
  IPAddress ip = WiFi.softAPIP();
  Serial.print("AP IP: "); Serial.println(ip);

  dnsServer.start(DNS_PORT, "*", ip);

  webServer.on("/", HTTP_GET, handleRoot);
  webServer.on("/save", HTTP_POST, handleSave);
  // Captive-portal redirect for any other path.
  webServer.onNotFound(handleRoot);
  webServer.begin();

  WiFi.scanNetworks(/*async=*/true);
  drawPortalScreen(ip);

  for (;;) {
    dnsServer.processNextRequest();
    webServer.handleClient();
    delay(2);
  }
}
