# Firmware: ESP32 wearable

ESP-IDF project for the wearable part of the Distributed Wearable AI Audio Translator. The wearable
captures speech, sends it over Wi-Fi to the translation server (`../server`), and plays the
translated speech back. The server currently runs on a PC; it is intended to run on a Raspberry Pi.

## Hardware

| Part | Role |
|---|---|
| ESP32 DevKit V1 | Audio capture, DSP, buffering, Wi-Fi streaming, playback |
| INMP441 I2S MEMS microphone | Audio input (I2S port 0) |
| MAX98357A I2S amplifier | Audio output driver (I2S port 1) |
| 8 ohm speaker (BCM76G) | Audio output |

An analog MAX4466 microphone module was tried first. It produced no usable signal, and the design
moved to the INMP441. No analog microphone code remains in this project.

## Wiring

On the ESP32 DevKit V1 the label printed next to each pin is the GPIO number with a "D" in front
(GPIO14 is the pin labelled D14). These are not the physical pin positions (1 to 30) that appear on
some schematic symbols.

| Connection | GPIO (used in `config.h`) | Label on the DevKit V1 |
|---|---|---|
| INMP441 SCK | GPIO14 | D14 |
| INMP441 WS | GPIO27 | D27 |
| INMP441 SD | GPIO33 | D33 |
| INMP441 L/R, GND, VDD | GND, GND, 3V3 | GND, GND, 3V3 |
| MAX98357A BCLK | GPIO19 | D19 |
| MAX98357A LRC | GPIO21 | D21 |
| MAX98357A DIN | GPIO18 | D18 |
| MAX98357A VDD, GND, SD | 3V3 (or 5V for more volume), GND, 3V3 | 3V3 (or VIN), GND, 3V3 |
| Speaker | MAX98357A output terminals (+ and -) | - |
| Session button | GPIO4 to GND | D4 |
| Language button | GPIO2 to GND | D2 |

GPIO2 is also the board's blue LED and a start-up (strapping) pin, so do not hold the language
button down while the board is powering on or being flashed. To move the button to D23 instead,
rewire it and set `PIN_BUTTON_LANG` to 23 in `config.h`.

## How to use

1. Power on. A short beep confirms the speaker works. The board joins Wi-Fi and connects to the server.
2. **Language button** (D2): each press selects the next target language and beeps its position:
   1 = English, 2 = Spanish, 3 = French, 4 = German, 5 = Portuguese, 6 = Afrikaans.
3. **Session button** (D4): one press starts listening (high beep), the next press stops it (low beep).
4. While listening, just speak. When you stop talking for about 0.6 s the sentence is sent for
   translation automatically. The translation plays on the speaker and listening continues.
5. The dashboard on the server PC (http://localhost:8080) shows the transcript and timings.

## Software structure (`main/`)

| File | Responsibility |
|---|---|
| `audio_hal_i2s.c` | All access to the microphone and amplifier |
| `dsp.c` | High-pass filter (cutoff about 134 Hz, removes DC offset and low-frequency rumble) and RMS level |
| `vad.c` | Voice activity detection: loudness compared with a self-adjusting noise floor |
| `audio_buffer.c` | Thread-safe ring buffers |
| `net_stream.c` | Wi-Fi and TCP link, typed frames |
| `language.c` | Target language list and selection |
| `button.c` | Session button and language button |
| `metrics.c` | Counters used for evaluation |
| `main.c` | Capture, playback and network tasks |
| `config.h` | Pins, audio format and tuning values |
| `wifi_secrets.example.h` | Template for your Wi-Fi name, password and server IP |

Audio format: 16 kHz, 16-bit mono, 20 ms frames (320 samples).

## Network protocol

Every frame starts with a 4-byte little-endian word: the top byte is the frame type and the low
24 bits are the payload length in bytes.

| Type | Direction | Payload |
|---|---|---|
| 0 audio | both | 16-bit mono PCM (a 20 ms frame) |
| 1 set language | device to server | language code, for example `fr` |
| 2 end of utterance | device to server | none: the speaker has finished the sentence |

The server returns the translation as type-0 frames, followed by an empty frame as the end marker.
The device sends each frame in one write and disables Nagle's algorithm, so small frames are not
held back waiting for acknowledgements.

## Build and run

Needs [ESP-IDF](https://docs.espressif.com/projects/esp-idf/) (developed with v6.1) and the ESP-IDF
terminal or the VS Code ESP-IDF extension. Run these commands from this `firmware` folder.

1. Copy `main/wifi_secrets.example.h` to `main/wifi_secrets.h` and fill in `WIFI_SSID`, `WIFI_PASS`
   and `SERVER_IP` (the IPv4 address of the computer running `server.py`; run `ipconfig` on Windows).
   `wifi_secrets.h` is ignored by git, so your password is never pushed.
2. The ESP32 only connects to **2.4 GHz** Wi-Fi. On an iPhone hotspot, turn on "Maximise Compatibility".
3. Start the server first (see `../server/README.md`).
4. Build, flash and open the serial monitor (replace `COM3` with your board's port):
   ```
   idf.py set-target esp32
   idf.py build
   idf.py -p COM3 flash monitor
   ```
5. If the device cannot reach the server (`link=0`), check `SERVER_IP`, that both are on the same
   network, and that Windows Firewall allows Python on port 5005.

## Reading the serial monitor

A `stats` line is printed once per second:

| Field | Meaning |
|---|---|
| `raw_rms`, `dc`, `rms` | Loudness before filtering, DC offset, loudness after filtering |
| `floor` | Voice detector's background-noise estimate |
| `snr_db` | Average speech level over average non-speech level in that second, in dB (approximate) |
| `speech=a/b` | Frames marked as speech out of the frames in that second |
| `session`, `lang`, `link` | Session on (1) or off (0), target language code, connected to the server (1) |
| `tx_bytes_s`, `frames_sent`, `utterances` | Bytes sent in the last second, frames sent in total, sentences sent in total |
| `drops(send/play/nolink)` | Samples lost to a full send buffer, a full playback buffer, and frames lost while the link was down |
| `send_fail`, `reconnects`, `recovery_ms` | Failed sends, restored connections, and how long the last recovery took |
| `rx_frames` | Translated-audio frames received from the server |
| `latency_ms` | Last speech frame sent to first translated audio received (includes the 0.6 s end-of-speech wait) |
| `stack_free_B` | Smallest amount of free stack the capture task has had, in bytes |

## Known limitations

- The server is a PC stand-in for the Raspberry Pi.
- Delay is dominated by speech-to-text on the server (see `../docs/test_results.csv`).
- Voice-detection thresholds have not yet been tuned across different noise conditions.
- The link is not encrypted.
- Wi-Fi reliability on a phone hotspot is limited. The firmware reconnects automatically and records the recovery time.
- Language detection on very short phrases can be wrong.
