#include "audio_buffer.h"
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

struct audio_ring_buf {
    int16_t *data;
    size_t capacity;      // power of two
    size_t mask;
    size_t head;          // write index
    size_t tail;          // read index
    size_t count;         // samples currently stored
    SemaphoreHandle_t lock;
};

audio_ring_buf_t *audio_buffer_create(size_t capacity_samples)
{
    audio_ring_buf_t *rb = calloc(1, sizeof(audio_ring_buf_t));
    if (!rb) return NULL;

    rb->data = calloc(capacity_samples, sizeof(int16_t));
    rb->capacity = capacity_samples;
    rb->mask = capacity_samples - 1;   // caller guarantees power of two
    rb->head = 0;
    rb->tail = 0;
    rb->count = 0;
    rb->lock = xSemaphoreCreateMutex();
    return rb;
}

size_t audio_buffer_push(audio_ring_buf_t *rb, const int16_t *samples, size_t num_samples)
{
    xSemaphoreTake(rb->lock, portMAX_DELAY);
    size_t free_space = rb->capacity - rb->count;
    size_t to_write = (num_samples < free_space) ? num_samples : free_space;

    for (size_t i = 0; i < to_write; i++) {
        rb->data[rb->head] = samples[i];
        rb->head = (rb->head + 1) & rb->mask;
    }
    rb->count += to_write;
    xSemaphoreGive(rb->lock);
    return to_write;
}

size_t audio_buffer_pop(audio_ring_buf_t *rb, int16_t *out, size_t num_samples)
{
    xSemaphoreTake(rb->lock, portMAX_DELAY);
    size_t to_read = (num_samples < rb->count) ? num_samples : rb->count;

    for (size_t i = 0; i < to_read; i++) {
        out[i] = rb->data[rb->tail];
        rb->tail = (rb->tail + 1) & rb->mask;
    }
    rb->count -= to_read;
    xSemaphoreGive(rb->lock);
    return to_read;
}

size_t audio_buffer_available(audio_ring_buf_t *rb)
{
    xSemaphoreTake(rb->lock, portMAX_DELAY);
    size_t n = rb->count;
    xSemaphoreGive(rb->lock);
    return n;
}
