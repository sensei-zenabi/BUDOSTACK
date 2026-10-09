#ifndef BUDOSTACK_PCSPEAKER_H
#define BUDOSTACK_PCSPEAKER_H

#include "budo_gfx.h"
#include <stdlib.h>

static inline int pcspeaker_valid(const struct budo_gfx_tone *tones, size_t count)
{
    unsigned int total = 0;
    if (count > BUDO_GFX_TONE_LIMIT || (count && !tones)) return 0;
    for (size_t i = 0; i < count; ++i) {
        if (tones[i].frequency_hz > BUDO_GFX_TONE_MAX_HZ || !tones[i].duration_ms ||
            tones[i].duration_ms > BUDO_GFX_TONE_MAX_MS) return 0;
        total += tones[i].duration_ms;
    }
    return total <= BUDO_GFX_SOUND_MAX_MS;
}

/* Interleaved float PCM, owned by the caller. Tiny ramps soften note edges
 * while retaining the two-level PC-speaker waveform. */
static inline float *pcspeaker_render(const struct budo_gfx_tone *tones, size_t count,
                                      unsigned int rate, unsigned int channels, size_t *frames)
{
    if (!frames) return NULL;
    *frames = 0;
    if (!count || !pcspeaker_valid(tones, count) || rate < 8000 || rate > 192000 ||
        channels < 1 || channels > 8) return NULL;
    for (size_t n = 0; n < count; ++n) *frames += (size_t)tones[n].duration_ms * rate / 1000;
    float *samples = malloc(*frames * channels * sizeof(*samples));
    if (!samples) { *frames = 0; return NULL; }
    size_t offset = 0;
    for (size_t n = 0; n < count; ++n) {
        size_t length = (size_t)tones[n].duration_ms * rate / 1000;
        size_t ramp = rate / 500;
        for (size_t i = 0; i < length; ++i) {
            float sample = 0;
            if (tones[n].frequency_hz) {
                sample = (i * tones[n].frequency_hz % rate) < rate / 2 ? 0.2f : -0.2f;
                size_t edge = i < length - 1 - i ? i : length - 1 - i;
                if (edge < ramp) sample *= (float)edge / (float)ramp;
            }
            for (unsigned int c = 0; c < channels; ++c) samples[(offset + i) * channels + c] = sample;
        }
        offset += length;
    }
    return samples;
}

#endif
