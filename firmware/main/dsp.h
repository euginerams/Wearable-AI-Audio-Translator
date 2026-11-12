#pragma once
#include <stdint.h>
#include <stddef.h>

// Resets internal filter state (call once at startup).
void dsp_init(void);

// In-place high-pass filter to strip DC offset and low-frequency drift.
void dsp_high_pass(int16_t *samples, size_t num_samples);

// Computes RMS energy of a frame (used by VAD).
float dsp_rms(const int16_t *samples, size_t num_samples);
