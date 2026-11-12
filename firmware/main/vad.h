#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

void vad_init(void);

// Feed one frame's RMS energy in; returns true if this frame is judged
// to contain speech. Internally tracks an adaptive noise floor so it
// self-calibrates to ambient conditions during silence.
bool vad_process(float frame_rms);

// Current estimate of the background-noise level (RMS), for diagnostics.
float vad_noise_floor(void);
