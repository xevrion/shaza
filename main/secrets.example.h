// Copy this to main/secrets.h and fill it in. secrets.h is git-ignored, so the password stays on
// your machine and in the firmware you flash, never in the repo.
#pragma once

#define WIFI_SSID "your-network"
// WPA2-Enterprise with PEAP and MSCHAPv2 inside, the usual campus setup.
#define WIFI_IDENTITY "your-login"
#define WIFI_PASSWORD "your-password"
