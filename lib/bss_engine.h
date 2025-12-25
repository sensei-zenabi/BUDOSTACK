#ifndef BSS_ENGINE_H
#define BSS_ENGINE_H

#include <stddef.h>
#include <stdint.h>

#define BSS_MAX_VOICES 3

typedef enum {
    BSS_WAVE_TRIANGLE = 0,
    BSS_WAVE_SAW,
    BSS_WAVE_PULSE,
    BSS_WAVE_NOISE
} BSSWaveform;

typedef struct {
    BSSWaveform waveform;
    double frequency;
    double volume;
    double pulse_width;
    double attack_s;
    double decay_s;
    double sustain_level;
    double release_s;
} BSSVoice;

typedef struct {
    unsigned int sample_rate;
    BSSVoice voices[BSS_MAX_VOICES];
    double phases[BSS_MAX_VOICES];
    uint32_t noise_state[BSS_MAX_VOICES];
} BSSEngine;

void bss_init(BSSEngine *engine, unsigned int sample_rate);
int bss_configure_voice(BSSEngine *engine, size_t voice, const BSSVoice *settings);
void bss_render_note(BSSEngine *engine, int16_t *buffer, size_t frames, double duration_s);

#endif
