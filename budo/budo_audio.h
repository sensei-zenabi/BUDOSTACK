#ifndef BUDO_AUDIO_H
#define BUDO_AUDIO_H

#include <stdint.h>

typedef struct {
    void *chunk;
} budo_sound_t;

typedef struct {
    void *music;
} budo_music_t;

/* INITIALIZE SDL AUDIO FOR SOUND EFFECTS AND MUSIC.
*  PASS 0 FOR DEFAULT SETTINGS (44100 HZ, S16, STEREO, 2048 SAMPLES).
*  RETURNS 0 ON SUCCESS, -1 ON FAILURE.
*/
int budo_audio_init(int frequency, uint16_t format, int channels, int chunk_size);
/* SHUT DOWN THE AUDIO SUBSYSTEM AND FREE MIXER STATE. */
void budo_audio_shutdown(void);

/* LOAD A SOUND EFFECT FROM DISK.
*  RETURNS 0 ON SUCCESS, -1 ON FAILURE.
*  CALL budo_sound_destroy WHEN DONE.
*/
int budo_sound_load(budo_sound_t *sound, const char *path);
/* FREE A LOADED SOUND EFFECT AND RESET ITS STATE. */
void budo_sound_destroy(budo_sound_t *sound);
/* PLAY A SOUND EFFECT ON THE FIRST FREE CHANNEL.
*  loops CONTROLS REPEAT COUNT (-1 FOR INFINITE).
*  RETURNS CHANNEL INDEX OR -1 ON FAILURE.
*/
int budo_sound_play(const budo_sound_t *sound, int loops);
/* PLAY A SOUND EFFECT ON A SPECIFIC CHANNEL.
*  USE channel = -1 FOR FIRST FREE CHANNEL.
*/
int budo_sound_play_channel(const budo_sound_t *sound, int channel, int loops);
/* SET THE VOLUME FOR A SOUND EFFECT (0-128). */
void budo_sound_set_volume(budo_sound_t *sound, int volume);
/* STOP A PLAYING CHANNEL (-1 TO HALT ALL). */
void budo_sound_stop_channel(int channel);

/* LOAD A MUSIC TRACK FROM DISK (INCLUDING MOD/S3M MODULES).
*  RETURNS 0 ON SUCCESS, -1 ON FAILURE.
*  CALL budo_music_destroy WHEN DONE.
*/
int budo_music_load(budo_music_t *music, const char *path);
/* FREE A LOADED MUSIC TRACK AND RESET ITS STATE. */
void budo_music_destroy(budo_music_t *music);
/* START PLAYING MUSIC.
*  loops CONTROLS REPEAT COUNT (-1 FOR INFINITE).
*/
int budo_music_play(const budo_music_t *music, int loops);
/* STOP, PAUSE, OR RESUME THE CURRENT MUSIC. */
void budo_music_stop(void);
void budo_music_pause(void);
void budo_music_resume(void);
/* SET THE GLOBAL MUSIC VOLUME (0-128). */
void budo_music_set_volume(int volume);
/* RETURN 1 IF MUSIC IS PLAYING, 0 OTHERWISE. */
int budo_music_is_playing(void);

#endif
