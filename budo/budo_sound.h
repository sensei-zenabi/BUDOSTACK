#ifndef BUDO_SOUND_H
#define BUDO_SOUND_H

#ifdef __cplusplus
extern "C" {
#endif

void budo_sound_beep(int count, int delay_ms);
int budo_sound_init(int sample_rate);
void budo_sound_shutdown(void);
int budo_sound_play_tone(int frequency_hz, int duration_ms, int volume);

#ifdef __cplusplus
}
#endif

#endif
