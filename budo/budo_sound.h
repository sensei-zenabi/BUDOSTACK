#ifndef BUDO_SOUND_H
#define BUDO_SOUND_H

int budo_sound_play(int channel, const char *path, int volume);
int budo_sound_stop(int channel);

#endif
