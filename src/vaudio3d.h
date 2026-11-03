#ifndef VLCRLIB_VAUDIO3D_H
#define VLCRLIB_VAUDIO3D_H

#include "vmath.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Positional audio backed by miniaudio's software mixer. This is independent
   from the legacy VLC-based vaudio API. */
typedef struct Sound3D { unsigned int id; } Sound3D;

bool InitAudio3D(void);
void CloseAudio3D(void);

Sound3D LoadSound3D(const char *path);
/* Streamed (not fully decoded) source for music/soundtracks. */
Sound3D LoadMusic3D(const char *path);
void    UnloadSound3D(Sound3D sound);
void    PlaySound3D(Sound3D sound, Vector3 position);
void    StopSound3D(Sound3D sound);
void    PauseSound3D(Sound3D sound);
void    ResumeSound3D(Sound3D sound);
void    SeekSound3D(Sound3D sound, float seconds);
float   GetSound3DDuration(Sound3D sound);
float   GetSound3DTime(Sound3D sound);
bool    IsSound3DPlaying(Sound3D sound);
void    SetSound3DPosition(Sound3D sound, Vector3 position);
void    SetSound3DVolume(Sound3D sound, float volume01);
void    SetSound3DLooping(Sound3D sound, bool looping);
/* 0 = clear line of sight, 1 = fully occluded (attenuates up to -18 dB). */
void    SetSound3DOcclusion(Sound3D sound, float occlusion01);
/* Wet amount of the per-sound feedback-delay reverb zone (0..1). */
void    SetSound3DReverb(Sound3D sound, float wet01);
void    SetSound3DVelocity(Sound3D sound, Vector3 velocity);

/* Global convolution reverb bus. Load the IR before creating sounds. */
bool LoadReverbIR(const char *irPath, float wet);
void SetReverbWet(float wet);

void SetListener3D(Vector3 position, Vector3 forward, Vector3 up);
void SetListener3DVelocity(Vector3 velocity);

#ifdef __cplusplus
}
#endif

#endif
