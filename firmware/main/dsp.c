#include "dsp.h"
#include <math.h>

static float s_hp_prev_in = 0.0f;
static float s_hp_prev_out = 0.0f;
static const float HP_ALPHA = 0.95f;   // cutoff about 134 Hz at 16 kHz: removes DC offset and low-frequency rumble

void dsp_init(void)
{
    s_hp_prev_in = 0.0f;
    s_hp_prev_out = 0.0f;
}

void dsp_high_pass(int16_t *samples, size_t num_samples)
{
    for (size_t i = 0; i < num_samples; i++) {
        float in = (float)samples[i];
        float out = HP_ALPHA * (s_hp_prev_out + in - s_hp_prev_in);
        s_hp_prev_in = in;
        s_hp_prev_out = out;

        if (out > 32767.0f) out = 32767.0f;
        if (out < -32768.0f) out = -32768.0f;
        samples[i] = (int16_t)out;
    }
}

float dsp_rms(const int16_t *samples, size_t num_samples)
{
    if (num_samples == 0) return 0.0f;
    double sum_sq = 0.0;
    for (size_t i = 0; i < num_samples; i++) {
        double s = (double)samples[i];
        sum_sq += s * s;
    }
    return (float)sqrt(sum_sq / (double)num_samples);
}
