#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Brings up Wi-Fi station mode and connects to WIFI_SSID/WIFI_PASS from
// config.h. Blocks until Wi-Fi is connected.
void net_stream_wifi_init(void);

// Opens a TCP connection to SERVER_IP:SERVER_PORT. Returns true on success.
// Blocks until the connection succeeds or fails.
bool net_stream_connect(void);

// Closes the connection (safe to call when already closed).
void net_stream_disconnect(void);

// True while a TCP connection to the server is open. A failed send or
// receive closes the connection, so this turns false when the link is lost.
bool net_stream_is_connected(void);

// Every frame starts with a 4-byte little-endian word: the top byte is the
// frame type and the low 24 bits are the payload length in bytes.
#define NET_FRAME_AUDIO              0   // payload: 16-bit mono PCM
#define NET_FRAME_SET_LANGUAGE       1   // payload: language code, for example "fr"
#define NET_FRAME_END_OF_UTTERANCE   2   // payload: none (the speaker has finished)

// Sends one frame of PCM audio to the server.
// Returns false (and closes the connection) if the send fails.
bool net_stream_send_frame(const int16_t *samples, size_t num_samples);

// Sends a control frame (language change, end of utterance).
// Returns false (and closes the connection) if the send fails.
bool net_stream_send_control(uint8_t type, const uint8_t *payload, size_t len);

// Blocking receive of one length-prefixed frame of translated audio.
// Returns the number of samples received. Returns 0 for an empty
// end-of-utterance frame, and also (after closing the connection) when
// the link is lost.
size_t net_stream_recv_frame(int16_t *out, size_t max_samples);
