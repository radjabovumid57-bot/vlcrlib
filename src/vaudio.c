#include "vaudio.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <math.h>
#include <SDL3/SDL.h>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <direct.h>
#else
#include <unistd.h>
#include <limits.h>
#endif

#ifndef VLCRLIB_NO_VLC
#include <vlc/vlc.h>

static libvlc_instance_t *g_vlc = NULL;
static char g_pluginPath[1024] = {0};

static bool findVlcPlugins(char *outPath, size_t maxLen) {
    const char *base = SDL_GetBasePath();
    char cand[1024];

    if (base && *base) {
        snprintf(cand, sizeof(cand), "%splugins", base);
#ifdef _WIN32
        DWORD attr = GetFileAttributesA(cand);
        if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
            snprintf(outPath, maxLen, "%s", cand);
            return true;
        }
#else
        if (access(cand, 0) == 0) {
            snprintf(outPath, maxLen, "%s", cand);
            return true;
        }
#endif
    }

    const char *fallbacks[] = {
        "plugins",
        "external/vlc-sdk/lib/plugins",
        "../external/vlc-sdk/lib/plugins",
        "../../external/vlc-sdk/lib/plugins"
    };

    for (size_t i = 0; i < sizeof(fallbacks)/sizeof(fallbacks[0]); ++i) {
#ifdef _WIN32
        DWORD attr = GetFileAttributesA(fallbacks[i]);
        if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
            char full[1024];
            if (_fullpath(full, fallbacks[i], sizeof(full))) {
                snprintf(outPath, maxLen, "%s", full);
            } else {
                snprintf(outPath, maxLen, "%s", fallbacks[i]);
            }
            return true;
        }
#else
        if (access(fallbacks[i], 0) == 0) {
            char full[1024];
            if (realpath(fallbacks[i], full)) {
                snprintf(outPath, maxLen, "%s", full);
            } else {
                snprintf(outPath, maxLen, "%s", fallbacks[i]);
            }
            return true;
        }
#endif
    }
    return false;
}

bool InitAudioDevice(void) {
    if (g_vlc) return true;

    const char *args[8];
    int argCount = 0;

    args[argCount++] = "--intf";
    args[argCount++] = "dummy";
    args[argCount++] = "--no-video";
    args[argCount++] = "--quiet";
    args[argCount++] = "--no-osd";

    if (findVlcPlugins(g_pluginPath, sizeof(g_pluginPath))) {
#ifdef _WIN32
        SetEnvironmentVariableA("VLC_PLUGIN_PATH", g_pluginPath);
#else
        setenv("VLC_PLUGIN_PATH", g_pluginPath, 1);
#endif
    }

    /* Plugin discovery is done through VLC_PLUGIN_PATH above. Start with the
       smallest possible instance: on some Windows audio setups the full arg
       list (or --plugin-path) can block inside libvlc_new. */
    g_vlc = libvlc_new(0, NULL);
    if (!g_vlc) {
        const char *minArgs[] = { "--intf", "dummy", "--no-video", "--quiet" };
        g_vlc = libvlc_new(4, minArgs);
    }
    if (!g_vlc)
        g_vlc = libvlc_new(argCount, args);

    if (g_vlc) return true;

    /* SDL audio initialization alone does not provide a player/mixer in this
       build. Do not report success when all Sound/Music operations would be
       no-ops. */
    return false;
}

void CloseAudioDevice(void) {
    if (g_vlc) {
        libvlc_release(g_vlc);
        g_vlc = NULL;
    }
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

static libvlc_media_player_t *newPlayer(const char *path) {
    if (!g_vlc || !path || !*path) return NULL;

    char resolved[1024];
    const char *openPath = path;

    if (strstr(path, "://")) {
        libvlc_media_t *m = libvlc_media_new_location(g_vlc, path);
        if (!m) return NULL;
        libvlc_media_player_t *p = libvlc_media_player_new_from_media(m);
        libvlc_media_release(m);
        return p;
    }

#ifdef _WIN32
    if (_fullpath(resolved, path, sizeof(resolved))) {
        openPath = resolved;
    }
#else
    if (realpath(path, resolved)) {
        openPath = resolved;
    }
#endif

    libvlc_media_t *m = libvlc_media_new_path(g_vlc, openPath);
    if (!m) return NULL;
    libvlc_media_player_t *p = libvlc_media_player_new_from_media(m);
    libvlc_media_release(m);
    return p;
}

Sound LoadSound(const char *path) {
    Sound s = {0};
    s.player = newPlayer(path);
    if (s.player) s.id = 1;
    return s;
}

void UnloadSound(Sound s) {
    if (s.player && g_vlc) {
        libvlc_media_player_stop((libvlc_media_player_t *)s.player);
        libvlc_media_player_release((libvlc_media_player_t *)s.player);
    }
}

void PlaySound(Sound s) {
    if (!s.player || !g_vlc) return;
    libvlc_media_player_stop((libvlc_media_player_t *)s.player);
    libvlc_media_player_play((libvlc_media_player_t *)s.player);
}

void StopSound(Sound s) {
    if (!s.player || !g_vlc) return;
    libvlc_media_player_stop((libvlc_media_player_t *)s.player);
}

void SetSoundVolume(Sound s, float v01) {
    if (!s.player || !g_vlc) return;
    int v = (int)(v01 * 100.0f);
    if (v < 0) v = 0; if (v > 100) v = 100;
    libvlc_audio_set_volume((libvlc_media_player_t *)s.player, v);
}

bool IsSoundPlaying(Sound s) {
    if (!s.player || !g_vlc) return false;
    return libvlc_media_player_is_playing((libvlc_media_player_t *)s.player) != 0;
}

Music LoadMusicStream(const char *path) {
    Music m = {0};
    m.player = newPlayer(path);
    m.loop   = false;
    if (m.player) m.id = 1;
    return m;
}

void UnloadMusicStream(Music m) {
    if (m.player && g_vlc) {
        libvlc_media_player_stop((libvlc_media_player_t *)m.player);
        libvlc_media_player_release((libvlc_media_player_t *)m.player);
    }
}

void PlayMusicStream(Music m) {
    if (!m.player || !g_vlc) return;
    libvlc_media_player_play((libvlc_media_player_t *)m.player);
}

void StopMusicStream(Music m) {
    if (!m.player || !g_vlc) return;
    libvlc_media_player_stop((libvlc_media_player_t *)m.player);
}

void PauseMusicStream(Music m) {
    if (!m.player || !g_vlc) return;
    libvlc_media_player_pause((libvlc_media_player_t *)m.player);
}

void ResumeMusicStream(Music m) {
    if (!m.player || !g_vlc) return;
    libvlc_media_player_play((libvlc_media_player_t *)m.player);
}

void UpdateMusicStream(Music m) {
    if (!m.player || !m.loop || !g_vlc) return;
    libvlc_state_t st = libvlc_media_player_get_state((libvlc_media_player_t *)m.player);
    if (st == libvlc_Ended) {
        libvlc_media_player_stop((libvlc_media_player_t *)m.player);
        libvlc_media_player_play((libvlc_media_player_t *)m.player);
    }
}

void SetMusicVolume(Music m, float v01) {
    if (!m.player || !g_vlc) return;
    int v = (int)(v01 * 100.0f);
    if (v < 0) v = 0; if (v > 100) v = 100;
    libvlc_audio_set_volume((libvlc_media_player_t *)m.player, v);
}

bool IsMusicStreamPlaying(Music m) {
    if (!m.player || !g_vlc) return false;
    return libvlc_media_player_is_playing((libvlc_media_player_t *)m.player) != 0;
}

float GetMusicTimeLength(Music m) {
    if (!m.player || !g_vlc) return 0.0f;
    libvlc_time_t t = libvlc_media_player_get_length((libvlc_media_player_t *)m.player);
    return (t > 0) ? (float)t / 1000.0f : 0.0f;
}

float GetMusicTimePlayed(Music m) {
    if (!m.player || !g_vlc) return 0.0f;
    libvlc_time_t t = libvlc_media_player_get_time((libvlc_media_player_t *)m.player);
    return (t > 0) ? (float)t / 1000.0f : 0.0f;
}

#else

/* VLCRLIB_NO_VLC: SDL3 WAV playback fallback.
   Supports PCM WAV (mono/stereo, 8/16/32-bit). Compressed formats require VLC. */
typedef struct SdlClip {
    SDL_AudioStream *stream;
    Uint8           *data;
    Uint32           len;
    SDL_AudioSpec    spec;
    float            gain;
    bool             started;
    bool             paused;
    Uint64           startTicks;
} SdlClip;

static bool g_sdlAudio = false;

static double clipBytesPerSecond(const SdlClip *c) {
    if (!c) return 0.0;
    double frame = (double)c->spec.channels * (double)SDL_AUDIO_BYTESIZE(c->spec.format);
    return (double)c->spec.freq * frame;
}

bool InitAudioDevice(void) {
    if (g_sdlAudio) return true;
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        fprintf(stderr, "[vlcrlib] SDL audio init failed: %s\n", SDL_GetError());
        return false;
    }
    g_sdlAudio = true;
    return true;
}

void CloseAudioDevice(void) {
    if (g_sdlAudio) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        g_sdlAudio = false;
    }
}

static SdlClip *loadWavClip(const char *path) {
    if (!g_sdlAudio || !path || !*path) return NULL;
    SDL_AudioSpec spec;
    Uint8 *data = NULL;
    Uint32 len = 0;
    if (!SDL_LoadWAV(path, &spec, &data, &len) || !data || len == 0) {
        fprintf(stderr, "[vlcrlib] SDL_LoadWAV failed for '%s' (only WAV is supported without VLC): %s\n",
                path, SDL_GetError());
        return NULL;
    }
    if (len > (Uint32)INT_MAX) { SDL_free(data); return NULL; }

    SDL_AudioStream *stream =
        SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    if (!stream) {
        fprintf(stderr, "[vlcrlib] SDL_OpenAudioDeviceStream failed: %s\n", SDL_GetError());
        SDL_free(data);
        return NULL;
    }

    SdlClip *c = (SdlClip *)SDL_calloc(1, sizeof(*c));
    if (!c) { SDL_DestroyAudioStream(stream); SDL_free(data); return NULL; }
    c->stream = stream;
    c->data = data;
    c->len = len;
    c->spec = spec;
    c->gain = 0.75f;
    SDL_SetAudioStreamGain(stream, c->gain);
    return c;
}

static void freeClip(SdlClip *c) {
    if (!c) return;
    SDL_DestroyAudioStream(c->stream);
    SDL_free(c->data);
    SDL_free(c);
}

static void startClip(SdlClip *c) {
    if (!c) return;
    SDL_ClearAudioStream(c->stream);
    SDL_PutAudioStreamData(c->stream, c->data, (int)c->len);
    SDL_ResumeAudioStreamDevice(c->stream);
    c->started = true;
    c->paused = false;
    c->startTicks = SDL_GetTicks();
}

static bool clipQueued(const SdlClip *c) {
    return c && SDL_GetAudioStreamQueued(c->stream) > 0;
}

Sound LoadSound(const char *path) {
    Sound s = {0};
    s.player = loadWavClip(path);
    s.id = s.player ? 1u : 0u;
    return s;
}
void UnloadSound(Sound s) { freeClip((SdlClip *)s.player); }
void PlaySound(Sound s) { startClip((SdlClip *)s.player); }
void StopSound(Sound s) {
    SdlClip *c = (SdlClip *)s.player;
    if (!c) return;
    SDL_ClearAudioStream(c->stream);
    SDL_PauseAudioStreamDevice(c->stream);
    c->started = false;
}
void SetSoundVolume(Sound s, float v01) {
    SdlClip *c = (SdlClip *)s.player;
    if (!c) return;
    c->gain = v01 < 0.0f ? 0.0f : (v01 > 1.0f ? 1.0f : v01);
    SDL_SetAudioStreamGain(c->stream, c->gain);
}
bool IsSoundPlaying(Sound s) {
    SdlClip *c = (SdlClip *)s.player;
    return c && c->started && !c->paused && clipQueued(c);
}

Music LoadMusicStream(const char *path) {
    Music m = {0};
    m.player = loadWavClip(path);
    m.id = m.player ? 1u : 0u;
    m.loop = true;
    return m;
}
void UnloadMusicStream(Music m) { freeClip((SdlClip *)m.player); }
void PlayMusicStream(Music m) { startClip((SdlClip *)m.player); }
void StopMusicStream(Music m) {
    SdlClip *c = (SdlClip *)m.player;
    if (!c) return;
    SDL_ClearAudioStream(c->stream);
    SDL_PauseAudioStreamDevice(c->stream);
    c->started = false;
}
void PauseMusicStream(Music m) {
    SdlClip *c = (SdlClip *)m.player;
    if (!c) return;
    SDL_PauseAudioStreamDevice(c->stream);
    c->paused = true;
}
void ResumeMusicStream(Music m) {
    SdlClip *c = (SdlClip *)m.player;
    if (!c) return;
    SDL_ResumeAudioStreamDevice(c->stream);
    c->paused = false;
}
void UpdateMusicStream(Music m) {
    SdlClip *c = (SdlClip *)m.player;
    if (!c || !c->started || c->paused) return;
    if (!clipQueued(c) && m.loop) {
        SDL_PutAudioStreamData(c->stream, c->data, (int)c->len);
        SDL_ResumeAudioStreamDevice(c->stream);
        c->startTicks = SDL_GetTicks();
    }
}
void SetMusicVolume(Music m, float v01) {
    SdlClip *c = (SdlClip *)m.player;
    if (!c) return;
    c->gain = v01 < 0.0f ? 0.0f : (v01 > 1.0f ? 1.0f : v01);
    SDL_SetAudioStreamGain(c->stream, c->gain);
}
bool IsMusicStreamPlaying(Music m) {
    SdlClip *c = (SdlClip *)m.player;
    return c && c->started && !c->paused;
}
float GetMusicTimeLength(Music m) {
    SdlClip *c = (SdlClip *)m.player;
    double bps = clipBytesPerSecond(c);
    return (bps > 0.0) ? (float)((double)c->len / bps) : 0.0f;
}
float GetMusicTimePlayed(Music m) {
    SdlClip *c = (SdlClip *)m.player;
    if (!c || !c->started) return 0.0f;
    double bps = clipBytesPerSecond(c);
    if (bps <= 0.0) return 0.0f;
    double queued = (double)SDL_GetAudioStreamQueued(c->stream);
    double played = (double)c->len / bps - queued / bps;
    if (played < 0.0) played = 0.0;
    double length = (double)c->len / bps;
    if (m.loop && length > 0.0) played = fmod(played, length);
    return (float)played;
}

#endif
