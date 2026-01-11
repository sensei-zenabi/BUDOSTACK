#include "budo_audio.h"

#include <SDL.h>
#include <stdio.h>
#include <string.h>

#if defined(BUDO_USE_SDL_MIXER) && BUDO_USE_SDL_MIXER
#if defined(__has_include)
#if __has_include(<SDL_mixer.h>)
#include <SDL_mixer.h>
#define BUDO_HAVE_SDL_MIXER 1
#else
#define BUDO_HAVE_SDL_MIXER 0
#endif
#else
#define BUDO_HAVE_SDL_MIXER 0
#endif
#else
#define BUDO_HAVE_SDL_MIXER 0
#endif

static void budo_audio_warn_no_mixer(const char *action) {
    fprintf(stderr, "SDL_mixer not available for %s.\n", action);
}

#if BUDO_HAVE_SDL_MIXER
static void budo_audio_clear_sound(budo_sound_t *sound) {
    if (!sound) {
        return;
    }
    memset(sound, 0, sizeof(*sound));
}

static void budo_audio_clear_music(budo_music_t *music) {
    if (!music) {
        return;
    }
    memset(music, 0, sizeof(*music));
}
#endif

int budo_audio_init(int frequency, uint16_t format, int channels, int chunk_size) {
#if !BUDO_HAVE_SDL_MIXER
    budo_audio_warn_no_mixer("audio init");
    (void)frequency;
    (void)format;
    (void)channels;
    (void)chunk_size;
    return -1;
#else
    if (frequency <= 0) {
        frequency = 44100;
    }
    if (format == 0) {
        format = AUDIO_S16SYS;
    }
    if (channels <= 0) {
        channels = 2;
    }
    if (chunk_size <= 0) {
        chunk_size = 2048;
    }

    if ((SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) == 0) {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
            fprintf(stderr, "SDL audio init failed: %s\n", SDL_GetError());
            return -1;
        }
    }

    int mix_flags = MIX_INIT_MP3 | MIX_INIT_OGG | MIX_INIT_FLAC | MIX_INIT_MOD;
    int mix_inited = Mix_Init(mix_flags);
    if ((mix_inited & mix_flags) != mix_flags) {
        fprintf(stderr, "SDL_mixer init warning: %s\n", Mix_GetError());
    }

    if (Mix_OpenAudio(frequency, (Uint16)format, channels, chunk_size) != 0) {
        fprintf(stderr, "SDL_mixer open audio failed: %s\n", Mix_GetError());
        return -1;
    }

    return 0;
#endif
}

void budo_audio_shutdown(void) {
#if !BUDO_HAVE_SDL_MIXER
    budo_audio_warn_no_mixer("audio shutdown");
#else
    Mix_CloseAudio();
    Mix_Quit();
    if ((SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) != 0) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
#endif
}

int budo_sound_load(budo_sound_t *sound, const char *path) {
#if !BUDO_HAVE_SDL_MIXER
    budo_audio_warn_no_mixer("sound load");
    (void)sound;
    (void)path;
    return -1;
#else
    if (!sound || !path) {
        return -1;
    }

    budo_audio_clear_sound(sound);

    Mix_Chunk *chunk = Mix_LoadWAV(path);
    if (!chunk) {
        fprintf(stderr, "Failed to load sound '%s': %s\n", path, Mix_GetError());
        return -1;
    }

    sound->chunk = chunk;
    return 0;
#endif
}

void budo_sound_destroy(budo_sound_t *sound) {
#if !BUDO_HAVE_SDL_MIXER
    budo_audio_warn_no_mixer("sound destroy");
    (void)sound;
#else
    if (!sound) {
        return;
    }
    if (sound->chunk) {
        Mix_FreeChunk((Mix_Chunk *)sound->chunk);
    }
    budo_audio_clear_sound(sound);
#endif
}

int budo_sound_play(const budo_sound_t *sound, int loops) {
    return budo_sound_play_channel(sound, -1, loops);
}

int budo_sound_play_channel(const budo_sound_t *sound, int channel, int loops) {
#if !BUDO_HAVE_SDL_MIXER
    budo_audio_warn_no_mixer("sound play");
    (void)sound;
    (void)channel;
    (void)loops;
    return -1;
#else
    if (!sound || !sound->chunk) {
        return -1;
    }

    int result = Mix_PlayChannel(channel, (Mix_Chunk *)sound->chunk, loops);
    if (result == -1) {
        fprintf(stderr, "Failed to play sound: %s\n", Mix_GetError());
    }
    return result;
#endif
}

void budo_sound_set_volume(budo_sound_t *sound, int volume) {
#if !BUDO_HAVE_SDL_MIXER
    budo_audio_warn_no_mixer("sound volume");
    (void)sound;
    (void)volume;
#else
    if (!sound || !sound->chunk) {
        return;
    }
    Mix_VolumeChunk((Mix_Chunk *)sound->chunk, volume);
#endif
}

void budo_sound_stop_channel(int channel) {
#if !BUDO_HAVE_SDL_MIXER
    budo_audio_warn_no_mixer("sound stop");
    (void)channel;
#else
    Mix_HaltChannel(channel);
#endif
}

int budo_music_load(budo_music_t *music, const char *path) {
#if !BUDO_HAVE_SDL_MIXER
    budo_audio_warn_no_mixer("music load");
    (void)music;
    (void)path;
    return -1;
#else
    if (!music || !path) {
        return -1;
    }

    budo_audio_clear_music(music);

    Mix_Music *track = Mix_LoadMUS(path);
    if (!track) {
        fprintf(stderr, "Failed to load music '%s': %s\n", path, Mix_GetError());
        return -1;
    }

    music->music = track;
    return 0;
#endif
}

void budo_music_destroy(budo_music_t *music) {
#if !BUDO_HAVE_SDL_MIXER
    budo_audio_warn_no_mixer("music destroy");
    (void)music;
#else
    if (!music) {
        return;
    }
    if (music->music) {
        Mix_FreeMusic((Mix_Music *)music->music);
    }
    budo_audio_clear_music(music);
#endif
}

int budo_music_play(const budo_music_t *music, int loops) {
#if !BUDO_HAVE_SDL_MIXER
    budo_audio_warn_no_mixer("music play");
    (void)music;
    (void)loops;
    return -1;
#else
    if (!music || !music->music) {
        return -1;
    }

    if (Mix_PlayMusic((Mix_Music *)music->music, loops) != 0) {
        fprintf(stderr, "Failed to play music: %s\n", Mix_GetError());
        return -1;
    }

    return 0;
#endif
}

void budo_music_stop(void) {
#if !BUDO_HAVE_SDL_MIXER
    budo_audio_warn_no_mixer("music stop");
#else
    Mix_HaltMusic();
#endif
}

void budo_music_pause(void) {
#if !BUDO_HAVE_SDL_MIXER
    budo_audio_warn_no_mixer("music pause");
#else
    Mix_PauseMusic();
#endif
}

void budo_music_resume(void) {
#if !BUDO_HAVE_SDL_MIXER
    budo_audio_warn_no_mixer("music resume");
#else
    Mix_ResumeMusic();
#endif
}

void budo_music_set_volume(int volume) {
#if !BUDO_HAVE_SDL_MIXER
    budo_audio_warn_no_mixer("music volume");
    (void)volume;
#else
    Mix_VolumeMusic(volume);
#endif
}

int budo_music_is_playing(void) {
#if !BUDO_HAVE_SDL_MIXER
    budo_audio_warn_no_mixer("music query");
    return 0;
#else
    return Mix_PlayingMusic() != 0;
#endif
}
