#pragma once

// =====================================================================
// Hardware used in this project
//   Board      : ESP32 DevKit V1
//   Microphone : INMP441 I2S MEMS microphone
//   Amplifier  : MAX98357A I2S amplifier
//   Speaker    : 8 ohm speaker on the amplifier output
//
// Pin numbers below are GPIO numbers. On the DevKit V1 the pin labelled
// "D14" is GPIO14, "D27" is GPIO27, and so on.
// =====================================================================

// INMP441 microphone (I2S port 0, receive). Its L/R pin is tied to GND.
#define PIN_I2S_MIC_SCK       14   // INMP441 SCK (bit clock)       board label D14
#define PIN_I2S_MIC_WS        27   // INMP441 WS  (word select)     board label D27
#define PIN_I2S_MIC_SD        33   // INMP441 SD  (data from mic)   board label D33

// MAX98357A amplifier (I2S port 1, transmit)
#define PIN_I2S_AMP_BCLK      19   // MAX98357A BCLK                board label D19
#define PIN_I2S_AMP_LRC       21   // MAX98357A LRC                 board label D21
#define PIN_I2S_AMP_DOUT      18   // MAX98357A DIN                 board label D18

// Session button: touch this pin to GND to start or stop a listening session
#define PIN_BUTTON            4    // board label D4
#define BUTTON_DEBOUNCE_MS    50
// Language button: each press selects the next target language (board label D2).
// GPIO2 is also the blue LED and a start-up pin: do not hold it down at power-on.
#define PIN_BUTTON_LANG       2
#define DEFAULT_TARGET_INDEX  1
#define END_OF_SPEECH_MS      600

// ---------------------------------------------------------------------
// Audio format
// ---------------------------------------------------------------------
#define SAMPLE_RATE_HZ        16000
#define FRAME_MS              20
#define FRAME_SAMPLES         (SAMPLE_RATE_HZ * FRAME_MS / 1000)   // 320 samples per frame

#define MIC_SHIFT             14   // right-shift applied to the 32-bit microphone samples (smaller = louder)
#define SPEAKER_GAIN          1    // software volume multiplier for playback

// ---------------------------------------------------------------------
// Wi-Fi and translation server (currently a PC; later the Raspberry Pi)
//
// WIFI_SSID, WIFI_PASS and SERVER_IP are kept in wifi_secrets.h, which is
// NOT committed to git (see .gitignore). To set it up:
//   copy main/wifi_secrets.example.h to main/wifi_secrets.h and fill it in.
// ---------------------------------------------------------------------
#if __has_include("wifi_secrets.h")
#include "wifi_secrets.h"
#else
#error "main/wifi_secrets.h is missing: copy main/wifi_secrets.example.h to main/wifi_secrets.h and fill in your Wi-Fi details"
#endif

#define SERVER_PORT           5005    // must match PORT in server/server.py
