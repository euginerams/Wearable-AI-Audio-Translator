#include "vad.h"

// Voice activity detection: each frame's loudness is compared with an
// estimate of the background noise level (the "noise floor").
//
// The noise floor falls quickly when the signal gets quieter and rises only
// slowly. It therefore settles on the real background level even when the
// room is noisier than the starting guess, and speech does not pull it up.

#define FLOOR_START          200.0f    // starting guess for the noise floor (RMS)
#define CALIBRATION_FRAMES   100       // first 2 s: follow the signal in both directions
#define CALIBRATION_RATE     0.05f
#define FLOOR_FALL_RATE      0.10f     // per frame, when the signal is below the floor
#define FLOOR_RISE_RATE      0.0003f   // per frame, when the signal is above the floor (about 67 s)
#define SPEECH_RATIO         3.0f      // speech = louder than the floor by this factor (about 9.5 dB)
#define HANGOVER_FRAMES      8         // stay "speech" for 160 ms after the last loud frame

static float s_noise_floor = FLOOR_START;
static int s_frames_seen = 0;
static int s_hangover = 0;

void vad_init(void)
{
    s_noise_floor = FLOOR_START;
    s_frames_seen = 0;
    s_hangover = 0;
}

bool vad_process(float frame_rms)
{
    if (s_frames_seen < CALIBRATION_FRAMES) {
        s_frames_seen++;
        s_noise_floor += CALIBRATION_RATE * (frame_rms - s_noise_floor);
    } else if (frame_rms < s_noise_floor) {
        s_noise_floor += FLOOR_FALL_RATE * (frame_rms - s_noise_floor);
    } else {
        s_noise_floor += FLOOR_RISE_RATE * (frame_rms - s_noise_floor);
    }
    if (s_noise_floor < 1.0f) s_noise_floor = 1.0f;

    if (frame_rms > s_noise_floor * SPEECH_RATIO) {
        s_hangover = HANGOVER_FRAMES;
        return true;
    }
    if (s_hangover > 0) {
        s_hangover--;
        return true;
    }
    return false;
}

float vad_noise_floor(void)
{
    return s_noise_floor;
}
