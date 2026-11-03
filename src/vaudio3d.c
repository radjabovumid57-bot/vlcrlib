#include "vaudio3d.h"

#include <string.h>

#if defined(VLCRLIB_HAS_MINIAUDIO) && VLCRLIB_HAS_MINIAUDIO

#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#define AUDIO3D_MAX_SOUNDS 32

typedef struct Audio3DSlot {
    ma_sound       sound;
    ma_lpf_node    lpf;
    ma_delay_node  delay;
    ma_delay_node  delay2;
    bool           lpfInit;
    bool           delayInit;
    bool           delay2Init;
    bool           used;
    bool           initialized;
    bool           streamed;
    float          baseVolume;
    float          occlusion;
    float          reverbWet;
} Audio3DSlot;

typedef struct ConvolutionReverb {
    ma_node_base base;
    float       *ir;
    ma_uint32    irLength;
    float       *history[2];
    ma_uint32    historySize;
    ma_uint32    position;
    ma_uint32    channels;
    float        wet;
    float        dry;
} ConvolutionReverb;

static void convolutionProcess(ma_node *node, const float **in,
                               ma_uint32 *inCount, float **out,
                               ma_uint32 *outCount) {
    ConvolutionReverb *c = (ConvolutionReverb *)node;
    ma_uint32 frames = *inCount < *outCount ? *inCount : *outCount;
    if (!in || !in[0] || !out || !out[0] || c->irLength == 0) {
        if (out && out[0]) memset(out[0], 0, (size_t)frames * c->channels * sizeof(float));
        *inCount = frames;
        *outCount = frames;
        return;
    }
    for (ma_uint32 n = 0; n < frames; ++n) {
        for (ma_uint32 ch = 0; ch < c->channels; ++ch) {
            float x = in[0][n * c->channels + ch];
            c->history[ch][c->position] = x;
            float acc = x * c->dry;
            ma_uint32 idx = c->position;
            for (ma_uint32 k = 0; k < c->irLength; ++k) {
                acc += c->history[ch][idx] * c->ir[k] * c->wet;
                idx = idx == 0 ? c->irLength - 1 : idx - 1;
            }
            out[0][n * c->channels + ch] = acc;
        }
        c->position = (c->position + 1) % c->historySize;
    }
    *inCount = frames;
    *outCount = frames;
}

static const ma_node_vtable g_convolutionVTable = {
    convolutionProcess, NULL, 1, 1, 0
};

static struct {
    bool          ready;
    ma_engine     engine;
    Audio3DSlot   slots[AUDIO3D_MAX_SOUNDS];
    ConvolutionReverb conv;
    bool          convReady;
} g_audio3d;

static void applyVolume(Audio3DSlot *s) {
    if (!s || !s->initialized) return;
    float gain = s->baseVolume * (1.0f - s->occlusion * 0.75f);
    if (gain < 0.0f) gain = 0.0f;
    ma_sound_set_volume(&s->sound, gain);
    if (s->lpfInit) {
        double cutoff = 20000.0 - (double)s->occlusion * 19000.0;
        if (cutoff < 900.0) cutoff = 900.0;
        ma_lpf_node_config cfg = ma_lpf_node_config_init(
            ma_engine_get_channels(&g_audio3d.engine),
            ma_engine_get_sample_rate(&g_audio3d.engine),
            cutoff, 4);
        ma_lpf_node_reinit(&cfg.lpf, &s->lpf);
    }
}

bool InitAudio3D(void) {
    if (g_audio3d.ready) return true;
    memset(&g_audio3d, 0, sizeof(g_audio3d));
    if (ma_engine_init(NULL, &g_audio3d.engine) != MA_SUCCESS) return false;
    g_audio3d.ready = true;
    return true;
}

bool LoadReverbIR(const char *irPath, float wet) {
    if (!g_audio3d.ready || !irPath || g_audio3d.convReady) return false;
    ma_decoder_config dc = ma_decoder_config_init(
        ma_format_f32, 1, ma_engine_get_sample_rate(&g_audio3d.engine));
    ma_decoder dec;
    if (ma_decoder_init_file(irPath, &dc, &dec) != MA_SUCCESS) return false;
    const ma_uint32 maxTaps = 4096;
    float *ir = (float *)calloc(maxTaps, sizeof(float));
    if (!ir) { ma_decoder_uninit(&dec); return false; }
    ma_uint64 total = 0;
    while (total < maxTaps) {
        ma_uint64 got = 0;
        ma_uint32 want = (ma_uint32)(maxTaps - total);
        if (ma_decoder_read_pcm_frames(&dec, ir + total, want, &got) != MA_SUCCESS ||
            got == 0) break;
        total += got;
    }
    ma_decoder_uninit(&dec);
    if (total == 0) { free(ir); return false; }

    ConvolutionReverb *c = &g_audio3d.conv;
    c->ir = ir;
    c->irLength = (ma_uint32)total;
    c->historySize = c->irLength;
    c->position = 0;
    c->channels = ma_engine_get_channels(&g_audio3d.engine);
    c->wet = wet < 0.0f ? 0.0f : (wet > 1.0f ? 1.0f : wet);
    c->dry = 1.0f;
    for (int ch = 0; ch < 2; ++ch)
        c->history[ch] = (float *)calloc(c->historySize, sizeof(float));
    if (!c->history[0] || !c->history[1]) {
        for (int ch = 0; ch < 2; ++ch) free(c->history[ch]);
        free(c->ir);
        memset(c, 0, sizeof(*c));
        return false;
    }
    ma_uint32 inCh[1] = { c->channels };
    ma_uint32 outCh[1] = { c->channels };
    ma_node_config nc = ma_node_config_init();
    nc.vtable = &g_convolutionVTable;
    nc.pInputChannels = inCh;
    nc.pOutputChannels = outCh;
    if (ma_node_init(ma_engine_get_node_graph(&g_audio3d.engine), &nc, NULL,
                     (ma_node *)&c->base) != MA_SUCCESS) {
        for (int ch = 0; ch < 2; ++ch) free(c->history[ch]);
        free(c->ir);
        memset(c, 0, sizeof(*c));
        return false;
    }
    if (ma_node_attach_output_bus((ma_node *)&c->base, 0,
            ma_node_graph_get_endpoint(ma_engine_get_node_graph(&g_audio3d.engine)), 0)
        != MA_SUCCESS) {
        ma_node_uninit((ma_node *)&c->base, NULL);
        for (int ch = 0; ch < 2; ++ch) free(c->history[ch]);
        free(c->ir);
        memset(c, 0, sizeof(*c));
        return false;
    }
    g_audio3d.convReady = true;
    return true;
}

void SetReverbWet(float wet) {
    if (!g_audio3d.convReady) return;
    g_audio3d.conv.wet = wet < 0.0f ? 0.0f : (wet > 1.0f ? 1.0f : wet);
}

void CloseAudio3D(void) {
    if (!g_audio3d.ready) return;
    if (g_audio3d.convReady) {
        ma_node_uninit((ma_node *)&g_audio3d.conv.base, NULL);
        for (int ch = 0; ch < 2; ++ch) free(g_audio3d.conv.history[ch]);
        free(g_audio3d.conv.ir);
        memset(&g_audio3d.conv, 0, sizeof(g_audio3d.conv));
        g_audio3d.convReady = false;
    }
    for (int i = 0; i < AUDIO3D_MAX_SOUNDS; ++i) {
        if (g_audio3d.slots[i].delay2Init)
            ma_delay_node_uninit(&g_audio3d.slots[i].delay2, NULL);
        if (g_audio3d.slots[i].delayInit)
            ma_delay_node_uninit(&g_audio3d.slots[i].delay, NULL);
        if (g_audio3d.slots[i].lpfInit)
            ma_lpf_node_uninit(&g_audio3d.slots[i].lpf, NULL);
        if (g_audio3d.slots[i].initialized)
            ma_sound_uninit(&g_audio3d.slots[i].sound);
        g_audio3d.slots[i].delay2Init = false;
        g_audio3d.slots[i].delayInit = false;
        g_audio3d.slots[i].lpfInit = false;
        g_audio3d.slots[i].initialized = false;
        g_audio3d.slots[i].used = false;
    }
    ma_engine_uninit(&g_audio3d.engine);
    memset(&g_audio3d, 0, sizeof(g_audio3d));
}

static Sound3D loadSoundInternal(const char *path, ma_uint32 flags,
                                 bool spatial) {
    Sound3D out = {0};
    if (!g_audio3d.ready || !path || !*path) return out;
    for (int i = 1; i < AUDIO3D_MAX_SOUNDS; ++i) {
        if (g_audio3d.slots[i].used) continue;
        Audio3DSlot *s = &g_audio3d.slots[i];
        if (ma_sound_init_from_file(&g_audio3d.engine, path,
                                    flags, NULL, NULL,
                                    &s->sound) != MA_SUCCESS)
            return out;
        ma_sound_set_spatialization_enabled(&s->sound,
                                            spatial ? MA_TRUE : MA_FALSE);
        ma_sound_set_attenuation_model(&s->sound,
                                       ma_attenuation_model_inverse);
        ma_sound_set_rolloff(&s->sound, 1.0f);
        ma_sound_set_min_distance(&s->sound, 0.5f);
        ma_sound_set_max_distance(&s->sound, 80.0f);

        ma_uint32 channels = ma_engine_get_channels(&g_audio3d.engine);
        ma_uint32 sampleRate = ma_engine_get_sample_rate(&g_audio3d.engine);
        ma_node_graph *graph = ma_engine_get_node_graph(&g_audio3d.engine);
        ma_node *endpoint = ma_node_graph_get_endpoint(graph);

        ma_lpf_node_config lc = ma_lpf_node_config_init(channels, sampleRate,
                                                        20000.0, 4);
        s->lpfInit = ma_lpf_node_init(graph, &lc, NULL, &s->lpf) == MA_SUCCESS;

        ma_delay_node_config dc = ma_delay_node_config_init(
            channels, sampleRate, sampleRate / 25, 0.38f);
        s->delayInit = ma_delay_node_init(graph, &dc, NULL, &s->delay) == MA_SUCCESS;
        ma_delay_node_config dc2 = ma_delay_node_config_init(
            channels, sampleRate, sampleRate / 13, 0.42f);
        s->delay2Init = ma_delay_node_init(graph, &dc2, NULL, &s->delay2) == MA_SUCCESS;
        if (s->delayInit) {
            ma_delay_node_set_wet(&s->delay, 0.0f);
            ma_delay_node_set_dry(&s->delay, 1.0f);
            ma_delay_node_set_decay(&s->delay, 0.38f);
        }
        if (s->delay2Init) {
            ma_delay_node_set_wet(&s->delay2, 0.0f);
            ma_delay_node_set_dry(&s->delay2, 1.0f);
            ma_delay_node_set_decay(&s->delay2, 0.42f);
        }

        ma_node *tail = g_audio3d.convReady
            ? (ma_node *)&g_audio3d.conv.base : endpoint;
        if (s->delay2Init) {
            ma_node_attach_output_bus((ma_node *)&s->delay2, 0, tail, 0);
            tail = (ma_node *)&s->delay2;
        }
        if (s->delayInit) {
            ma_node_attach_output_bus((ma_node *)&s->delay, 0, tail, 0);
            tail = (ma_node *)&s->delay;
        }
        if (s->lpfInit) {
            ma_node_attach_output_bus((ma_node *)&s->lpf, 0, tail, 0);
            tail = (ma_node *)&s->lpf;
        }
        ma_node_attach_output_bus((ma_node *)&s->sound, 0, tail, 0);

        s->used = true;
        s->initialized = true;
        s->streamed = (flags & MA_SOUND_FLAG_STREAM) != 0;
        s->baseVolume = 1.0f;
        s->occlusion = 0.0f;
        s->reverbWet = 0.0f;
        out.id = (unsigned int)i;
        return out;
    }
    return out;
}

Sound3D LoadSound3D(const char *path) {
    return loadSoundInternal(path,
                             MA_SOUND_FLAG_DECODE, true);
}

Sound3D LoadMusic3D(const char *path) {
    return loadSoundInternal(path,
                             MA_SOUND_FLAG_STREAM, false);
}

void UnloadSound3D(Sound3D sound) {
    if (!g_audio3d.ready || sound.id == 0 || sound.id >= AUDIO3D_MAX_SOUNDS)
        return;
    Audio3DSlot *s = &g_audio3d.slots[sound.id];
    if (s->delay2Init) ma_delay_node_uninit(&s->delay2, NULL);
    if (s->delayInit) ma_delay_node_uninit(&s->delay, NULL);
    if (s->lpfInit) ma_lpf_node_uninit(&s->lpf, NULL);
    if (s->initialized) ma_sound_uninit(&s->sound);
    s->delay2Init = false;
    s->delayInit = false;
    s->lpfInit = false;
    s->initialized = false;
    s->used = false;
}

void PlaySound3D(Sound3D sound, Vector3 position) {
    if (!g_audio3d.ready || sound.id == 0 || sound.id >= AUDIO3D_MAX_SOUNDS)
        return;
    Audio3DSlot *s = &g_audio3d.slots[sound.id];
    if (!s->initialized) return;
    ma_sound_set_position(&s->sound, position.x, position.y, position.z);
    applyVolume(s);
    ma_sound_seek_to_pcm_frame(&s->sound, 0);
    ma_sound_start(&s->sound);
}

void StopSound3D(Sound3D sound) {
    if (!g_audio3d.ready || sound.id == 0 || sound.id >= AUDIO3D_MAX_SOUNDS)
        return;
    if (g_audio3d.slots[sound.id].initialized)
        ma_sound_stop(&g_audio3d.slots[sound.id].sound);
}

void PauseSound3D(Sound3D sound) {
    if (!g_audio3d.ready || sound.id == 0 || sound.id >= AUDIO3D_MAX_SOUNDS)
        return;
    if (g_audio3d.slots[sound.id].initialized)
        ma_sound_stop(&g_audio3d.slots[sound.id].sound);
}

void ResumeSound3D(Sound3D sound) {
    if (!g_audio3d.ready || sound.id == 0 || sound.id >= AUDIO3D_MAX_SOUNDS)
        return;
    if (g_audio3d.slots[sound.id].initialized)
        ma_sound_start(&g_audio3d.slots[sound.id].sound);
}

void SeekSound3D(Sound3D sound, float seconds) {
    if (!g_audio3d.ready || sound.id == 0 || sound.id >= AUDIO3D_MAX_SOUNDS)
        return;
    if (seconds < 0.0f) seconds = 0.0f;
    if (g_audio3d.slots[sound.id].initialized)
        ma_sound_seek_to_second(&g_audio3d.slots[sound.id].sound, seconds);
}

float GetSound3DDuration(Sound3D sound) {
    if (!g_audio3d.ready || sound.id == 0 || sound.id >= AUDIO3D_MAX_SOUNDS)
        return 0.0f;
    float len = 0.0f;
    if (g_audio3d.slots[sound.id].initialized &&
        ma_sound_get_length_in_seconds(&g_audio3d.slots[sound.id].sound,
                                       &len) == MA_SUCCESS)
        return len;
    return 0.0f;
}

float GetSound3DTime(Sound3D sound) {
    if (!g_audio3d.ready || sound.id == 0 || sound.id >= AUDIO3D_MAX_SOUNDS)
        return 0.0f;
    float cur = 0.0f;
    if (g_audio3d.slots[sound.id].initialized &&
        ma_sound_get_cursor_in_seconds(&g_audio3d.slots[sound.id].sound,
                                       &cur) == MA_SUCCESS)
        return cur;
    return 0.0f;
}

bool IsSound3DPlaying(Sound3D sound) {
    if (!g_audio3d.ready || sound.id == 0 || sound.id >= AUDIO3D_MAX_SOUNDS)
        return false;
    return g_audio3d.slots[sound.id].initialized &&
           ma_sound_is_playing(&g_audio3d.slots[sound.id].sound);
}

void SetSound3DPosition(Sound3D sound, Vector3 position) {
    if (!g_audio3d.ready || sound.id == 0 || sound.id >= AUDIO3D_MAX_SOUNDS)
        return;
    if (g_audio3d.slots[sound.id].initialized)
        ma_sound_set_position(&g_audio3d.slots[sound.id].sound,
                              position.x, position.y, position.z);
}

void SetSound3DVolume(Sound3D sound, float volume01) {
    if (!g_audio3d.ready || sound.id == 0 || sound.id >= AUDIO3D_MAX_SOUNDS)
        return;
    Audio3DSlot *s = &g_audio3d.slots[sound.id];
    if (!s->initialized) return;
    s->baseVolume = volume01 < 0.0f ? 0.0f : volume01;
    applyVolume(s);
}

void SetSound3DOcclusion(Sound3D sound, float occlusion01) {
    if (!g_audio3d.ready || sound.id == 0 || sound.id >= AUDIO3D_MAX_SOUNDS)
        return;
    Audio3DSlot *s = &g_audio3d.slots[sound.id];
    if (!s->initialized) return;
    s->occlusion = occlusion01 < 0.0f ? 0.0f :
                   (occlusion01 > 1.0f ? 1.0f : occlusion01);
    applyVolume(s);
}

void SetSound3DReverb(Sound3D sound, float wet01) {
    if (!g_audio3d.ready || sound.id == 0 || sound.id >= AUDIO3D_MAX_SOUNDS)
        return;
    Audio3DSlot *s = &g_audio3d.slots[sound.id];
    if (!s->initialized) return;
    s->reverbWet = wet01 < 0.0f ? 0.0f : (wet01 > 1.0f ? 1.0f : wet01);
    if (s->delayInit) {
        ma_delay_node_set_wet(&s->delay, s->reverbWet * 0.5f);
        ma_delay_node_set_dry(&s->delay, 1.0f - s->reverbWet * 0.25f);
    }
    if (s->delay2Init) {
        ma_delay_node_set_wet(&s->delay2, s->reverbWet * 0.35f);
        ma_delay_node_set_dry(&s->delay2, 1.0f);
    }
}

void SetSound3DVelocity(Sound3D sound, Vector3 velocity) {
    if (!g_audio3d.ready || sound.id == 0 || sound.id >= AUDIO3D_MAX_SOUNDS)
        return;
    if (g_audio3d.slots[sound.id].initialized)
        ma_sound_set_velocity(&g_audio3d.slots[sound.id].sound,
                              velocity.x, velocity.y, velocity.z);
}

void SetSound3DLooping(Sound3D sound, bool looping) {
    if (!g_audio3d.ready || sound.id == 0 || sound.id >= AUDIO3D_MAX_SOUNDS)
        return;
    if (g_audio3d.slots[sound.id].initialized)
        ma_sound_set_looping(&g_audio3d.slots[sound.id].sound,
                             looping ? MA_TRUE : MA_FALSE);
}

void SetListener3D(Vector3 position, Vector3 forward, Vector3 up) {
    if (!g_audio3d.ready) return;
    ma_engine_listener_set_position(&g_audio3d.engine, 0,
                                    position.x, position.y, position.z);
    ma_engine_listener_set_direction(&g_audio3d.engine, 0,
                                     forward.x, forward.y, forward.z);
    ma_engine_listener_set_world_up(&g_audio3d.engine, 0,
                                    up.x, up.y, up.z);
}

void SetListener3DVelocity(Vector3 velocity) {
    if (g_audio3d.ready)
        ma_engine_listener_set_velocity(&g_audio3d.engine, 0,
                                        velocity.x, velocity.y, velocity.z);
}

#else

bool InitAudio3D(void) { return false; }
void CloseAudio3D(void) {}
Sound3D LoadSound3D(const char *path) { (void)path; return (Sound3D){0}; }
Sound3D LoadMusic3D(const char *path) { (void)path; return (Sound3D){0}; }
void UnloadSound3D(Sound3D s) { (void)s; }
void PlaySound3D(Sound3D s, Vector3 p) { (void)s; (void)p; }
void StopSound3D(Sound3D s) { (void)s; }
void PauseSound3D(Sound3D s) { (void)s; }
void ResumeSound3D(Sound3D s) { (void)s; }
void SeekSound3D(Sound3D s, float t) { (void)s; (void)t; }
float GetSound3DDuration(Sound3D s) { (void)s; return 0.0f; }
float GetSound3DTime(Sound3D s) { (void)s; return 0.0f; }
bool IsSound3DPlaying(Sound3D s) { (void)s; return false; }
void SetSound3DPosition(Sound3D s, Vector3 p) { (void)s; (void)p; }
void SetSound3DVolume(Sound3D s, float v) { (void)s; (void)v; }
void SetSound3DLooping(Sound3D s, bool l) { (void)s; (void)l; }
void SetSound3DOcclusion(Sound3D s, float o) { (void)s; (void)o; }
void SetSound3DReverb(Sound3D s, float w) { (void)s; (void)w; }
void SetSound3DVelocity(Sound3D s, Vector3 v) { (void)s; (void)v; }
void SetListener3D(Vector3 p, Vector3 f, Vector3 u) { (void)p; (void)f; (void)u; }
void SetListener3DVelocity(Vector3 v) { (void)v; }
bool LoadReverbIR(const char *p, float w) { (void)p; (void)w; return false; }
void SetReverbWet(float w) { (void)w; }

#endif
