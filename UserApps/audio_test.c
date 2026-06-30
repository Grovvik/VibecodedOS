#include "userlib.h"

/*
 * audio_test - User-space HD Audio test application
 * Generates a 440 Hz sine wave (16-bit stereo 48 kHz) and plays it.
 */

#define SAMPLE_RATE   48000
#define DURATION_MS   500
#define NUM_CHANNELS  2
#define NUM_SAMPLES   ((SAMPLE_RATE * DURATION_MS) / 1000)
#define BUFFER_SIZE   (NUM_SAMPLES * NUM_CHANNELS * 2)

#define PI2  6.283185307f

static short g_pcm_buffer[NUM_SAMPLES * NUM_CHANNELS];

/* Simple fixed-point sine approximation using precomputed quarter-wave */
static const short sin_table[64] = {
    0,   804,  1607,  2410,  3211,  4011,  4807,  5601,
 6392,  7179,  7961,  8739,  9511, 10278, 11038, 11792,
12539, 13278, 14009, 14732, 15446, 16151, 16845, 17530,
18204, 18867, 19519, 20159, 20787, 21402, 22005, 22594,
23169, 23731, 24278, 24811, 25329, 25832, 26319, 26790,
27245, 27683, 28105, 28510, 28897, 29268, 29621, 29956,
30273, 30571, 30852, 31113, 31356, 31580, 31785, 31970,
32137, 32284, 32412, 32521, 32609, 32678, 32728, 32757
};

static short fast_sin(u32 angle) {
    /* angle: 0..255 maps to 0..2*PI */
    u32 quadrant = (angle >> 6) & 3;
    u32 idx = angle & 63;
    if (quadrant == 0) return sin_table[idx];
    if (quadrant == 1) return sin_table[63 - idx];
    if (quadrant == 2) return -sin_table[idx];
    return -sin_table[63 - idx];
}

static void generate_sine(short* buf, u32 num_samples, u32 channels, u32 freq) {
    /* phase increment per sample: freq * 256 / SAMPLE_RATE */
    u32 phase_inc = (freq * 256) / SAMPLE_RATE;
    u32 phase = 0;
    for (u32 i = 0; i < num_samples; i++) {
        short s = (short)((fast_sin(phase) * 3) / 4); /* 75% volume */
        for (u32 ch = 0; ch < channels; ch++) {
            buf[i * channels + ch] = s;
        }
        phase += phase_inc;
    }
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    printf("Audio test: initializing HD Audio...\n");

    if (audio_init() != 0) {
        printf("Failed to initialize audio!\n");
        return 1;
    }

    printf("Audio initialized. Generating 220 Hz sine wave...\n");
    generate_sine(g_pcm_buffer, NUM_SAMPLES, NUM_CHANNELS, 220);

    printf("Playing %u ms of audio...\n", DURATION_MS);
    if (audio_play(g_pcm_buffer, BUFFER_SIZE) != 0) {
        printf("Audio playback failed!\n");
        return 1;
    }

    printf("Playback complete.\n");
    return 0;
}
