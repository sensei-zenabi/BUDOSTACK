#include "bss_engine.h"

#include <math.h>
#include <stddef.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static double clamp_double(double value, double min, double max) {
    if (value < min)
        return min;
    if (value > max)
        return max;
    return value;
}

static double envelope_level(const BSSVoice *voice, double t, double duration_s) {
    double attack = voice->attack_s;
    double decay = voice->decay_s;
    double sustain = voice->sustain_level;
    double release = voice->release_s;
    double release_start = duration_s - release;

    if (release_start < 0.0)
        release_start = 0.0;

    if (attack > 0.0 && t < attack)
        return t / attack;

    if (decay > 0.0 && t < attack + decay) {
        double decay_pos = (t - attack) / decay;
        return 1.0 + (sustain - 1.0) * decay_pos;
    }

    if (t < release_start)
        return sustain;

    if (release > 0.0 && t < duration_s) {
        double release_pos = (t - release_start) / release;
        release_pos = clamp_double(release_pos, 0.0, 1.0);
        return sustain * (1.0 - release_pos);
    }

    return 0.0;
}

static double waveform_sample(BSSEngine *engine, size_t index, double phase) {
    const BSSVoice *voice = &engine->voices[index];

    switch (voice->waveform) {
    case BSS_WAVE_TRIANGLE:
        return 1.0 - 4.0 * fabs(phase - 0.5);
    case BSS_WAVE_SAW:
        return 2.0 * phase - 1.0;
    case BSS_WAVE_PULSE:
        return (phase < voice->pulse_width) ? 1.0 : -1.0;
    case BSS_WAVE_NOISE:
    default: {
        uint32_t state = engine->noise_state[index];
        state = state * 1664525u + 1013904223u;
        engine->noise_state[index] = state;
        return ((double)(state >> 1) / 2147483648.0) * 2.0 - 1.0;
    }
    }
}

void bss_init(BSSEngine *engine, unsigned int sample_rate) {
    if (!engine)
        return;

    engine->sample_rate = sample_rate;

    for (size_t i = 0; i < BSS_MAX_VOICES; ++i) {
        engine->voices[i].waveform = BSS_WAVE_TRIANGLE;
        engine->voices[i].frequency = 440.0;
        engine->voices[i].volume = 0.25;
        engine->voices[i].pulse_width = 0.5;
        engine->voices[i].attack_s = 0.01;
        engine->voices[i].decay_s = 0.08;
        engine->voices[i].sustain_level = 0.7;
        engine->voices[i].release_s = 0.12;
        engine->phases[i] = 0.0;
        engine->noise_state[i] = (uint32_t)(0x12345678u + i * 1103515245u);
    }
}

int bss_configure_voice(BSSEngine *engine, size_t voice, const BSSVoice *settings) {
    if (!engine || !settings || voice >= BSS_MAX_VOICES)
        return -1;

    BSSVoice *target = &engine->voices[voice];
    target->waveform = settings->waveform;
    target->frequency = clamp_double(settings->frequency, 0.0, 20000.0);
    target->volume = clamp_double(settings->volume, 0.0, 1.0);
    target->pulse_width = clamp_double(settings->pulse_width, 0.05, 0.95);
    target->attack_s = clamp_double(settings->attack_s, 0.0, 10.0);
    target->decay_s = clamp_double(settings->decay_s, 0.0, 10.0);
    target->sustain_level = clamp_double(settings->sustain_level, 0.0, 1.0);
    target->release_s = clamp_double(settings->release_s, 0.0, 10.0);

    return 0;
}

void bss_render_note(BSSEngine *engine, int16_t *buffer, size_t frames, double duration_s) {
    if (!engine || !buffer || frames == 0 || duration_s <= 0.0)
        return;

    double sample_rate = (double)engine->sample_rate;

    for (size_t frame = 0; frame < frames; ++frame) {
        double t = (double)frame / sample_rate;
        double mixed = 0.0;

        for (size_t voice = 0; voice < BSS_MAX_VOICES; ++voice) {
            const BSSVoice *settings = &engine->voices[voice];
            if (settings->frequency <= 0.0 || settings->volume <= 0.0)
                continue;

            double env = envelope_level(settings, t, duration_s);
            double phase = engine->phases[voice];
            double sample = waveform_sample(engine, voice, phase);

            mixed += sample * env * settings->volume;

            phase += settings->frequency / sample_rate;
            if (phase >= 1.0)
                phase -= floor(phase);
            engine->phases[voice] = phase;
        }

        mixed = clamp_double(mixed, -1.0, 1.0);
        buffer[frame] = (int16_t)lrint(mixed * 32767.0);
    }
}
