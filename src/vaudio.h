#ifndef VLCRLIB_VAUDIO_H
#define VLCRLIB_VAUDIO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

typedef struct Sound {
    unsigned int id;
    void        *player;    /* libvlc_media_player_t* — opaque */
} Sound;

typedef struct Music {
    unsigned int id;
    void        *player;
    void        *media;
    bool         loop;
} Music;

bool  InitAudioDevice(void);
void  CloseAudioDevice(void);

Sound LoadSound(const char *path);
void  UnloadSound(Sound s);
void  PlaySound(Sound s);
void  StopSound(Sound s);
void  SetSoundVolume(Sound s, float v01);
bool  IsSoundPlaying(Sound s);

Music LoadMusicStream(const char *path);
void  UnloadMusicStream(Music m);
void  PlayMusicStream(Music m);
void  StopMusicStream(Music m);
void  PauseMusicStream(Music m);
void  ResumeMusicStream(Music m);
void  UpdateMusicStream(Music m);           /* для loop */
void  SetMusicVolume(Music m, float v01);
bool  IsMusicStreamPlaying(Music m);
float GetMusicTimeLength(Music m);
float GetMusicTimePlayed(Music m);

#ifdef __cplusplus
}
#endif
#endif