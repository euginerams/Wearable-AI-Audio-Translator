#pragma once
#include <stdint.h>

// Counters used to evaluate the system. They are printed once per second
// as a "stats" line in the serial monitor, so results can be copied into
// tables and graphs.
typedef struct {
    uint32_t frames_sent;                // frames sent to the server
    uint32_t utterances_sent;            // sentences finished and handed to the server
    uint32_t bytes_sent;                 // audio bytes sent to the server
    uint32_t frames_dropped_no_link;     // speech frames discarded because the link was down
    uint32_t send_buf_dropped_samples;   // samples lost because the send buffer was full
    uint32_t playback_buf_dropped_samples; // samples lost because the playback buffer was full
    uint32_t frames_received;            // translated-audio frames received
    uint32_t send_failures;              // failed sends (link lost while sending)
    uint32_t reconnects;                 // times the link was restored after a loss
    uint32_t last_recovery_ms;           // how long the last recovery took
    uint32_t last_latency_ms;            // last speech frame sent -> first translated audio received
} metrics_t;

extern metrics_t g_metrics;
