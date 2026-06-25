// Runtime configuration layer.
//
// All_Settings.h provides compile-time defaults; values stored in NVS (set via
// the captive portal launched by the web flasher) override those defaults at
// boot. The "normal install" PlatformIO path is unchanged: edit All_Settings.h,
// flash, the placeholder check passes through and the .ino reads cfg.* which
// were seeded directly from the All_Settings.h symbols.
//
// Do NOT include NTP_Time.h from this header — that file defines globals
// (Timezone objects, UDP, packet buffers) and must remain single-TU.

#pragma once

#include <Arduino.h>
#include <Timezone.h>

struct Config {
  String    wifiSsid;
  String    wifiPassword;
  String    apiKey;
  String    latitude;
  String    longitude;
  String    units;         // "metric" | "imperial"
  String    tzName;        // e.g. "usCT", "IST"
  Timezone* tz;            // resolved from tzName via the TZ table
  int       nightOffHour;  // backlight cuts at HH:59
  int       nightOnHour;   // backlight resumes at HH:00
};

extern Config cfg;

// Seed cfg from the All_Settings.h compile-time defaults, then overlay any
// NVS-stored values. Call once early in setup() before WiFi.begin().
void configBegin(const char* defSsid,
                 const char* defPass,
                 const String& defApiKey,
                 const String& defLat,
                 const String& defLon,
                 const String& defUnits,
                 const char*  defTzName,
                 Timezone*    defTz,
                 int defNightOff,
                 int defNightOn);

// True when cfg.wifiSsid still looks like the All_Settings.h placeholder.
// Used to decide whether to launch the captive portal on first boot.
bool configIsPlaceholder();

// Returns true once a successful portal save has marked config as committed,
// OR when configIsPlaceholder() is false (i.e. user edited All_Settings.h).
bool configIsReady();

// Run the AP-mode captive portal. Saves to NVS and reboots on submit.
// Never returns.
void startCaptivePortal();

// Pointer to the currently-selected Timezone object. Falls back to the
// compile-time default passed into configBegin() when nothing else is set.
Timezone* currentTZ();
