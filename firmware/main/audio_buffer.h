#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Simple thread-safe ring buffer of int16 PCM samples. Used both for the
// capture-side send buffer (smooths jitter before Wi-Fi transmission) and
// the playback-side buffer (smooths jitter before writing to the speaker).

typedef struct audio_ring_buf audio_ring_buf_t;

// capacity_samples must be a power of two.
audio_ring_buf_t *audio_buffer_create(size_t capacity_samples);

// Returns number of samples actually written (may be less than num_samples
// if the buffer is full).
size_t audio_buffer_push(audio_ring_buf_t *rb, const int16_t *samples, size_t num_samples);

// Returns number of samples actually read (may be less than num_samples
// if the buffer doesn't have enough data yet).
size_t audio_buffer_pop(audio_ring_buf_t *rb, int16_t *out, size_t num_samples);

size_t audio_buffer_available(audio_ring_buf_t *rb);
