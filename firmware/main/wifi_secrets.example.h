#pragma once

// Copy this file to wifi_secrets.h (same folder) and fill in your own values.
// wifi_secrets.h is listed in .gitignore, so your password is never pushed to git.

#define WIFI_SSID   "your-wifi-name"        // 2.4 GHz network (the ESP32 has no 5 GHz radio)
#define WIFI_PASS   "your-wifi-password"
#define SERVER_IP   "192.168.0.100"         // IPv4 address of the PC or Raspberry Pi running server.py
