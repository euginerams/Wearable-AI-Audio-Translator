#pragma once
#include <stdint.h>
#include <stddef.h>

// ---------------------------------------------------------------------
// Audio hardware interface.
//
// The rest of the firmware (DSP, voice detection, buffering, networking)
// only calls these functions. All access to the INMP441 microphone
// and the MAX98357A amplifier is kept in audio_hal_i2s.c.
// ---------------------------------------------------------------------

// One-time setup of the microphone (I2S port 0) and amplifier (I2S port 1).
void audio_hal_init(void);

// Blocking read of `num_samples` 16-bit mono samples from the microphone.
// Returns the number of samples actually read.
size_t audio_hal_read(int16_t *out, size_t num_samples);

// Blocking write of `num_samples` 16-bit mono samples to the speaker.
// Returns the number of samples written.
size_t audio_hal_write(const int16_t *in, size_t num_samples);

// Plays a short beep through the speaker. Used for the power-on speaker
// self-test and for the session start/stop sounds.
void audio_hal_beep(int freq_hz, int duration_ms);
