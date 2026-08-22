// Schwung audio_fx_api_v2 wrapper for the capicola engine.
//
// REALTIME CONTRACT: every entry point here runs on the SPI callback. The only
// allocation is the one-shot engine ring in create_instance (documented in
// README.md). set_param, get_param, on_midi and process_block do not allocate,
// block, or touch the filesystem.

#include "plugin_api_v1.h"
#include "audio_fx_api_v2.h"
#include "capicola_engine.h"
#include "capicola_params.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <new>

using namespace capicola_schwung;

namespace {

constexpr int kBlockMax = 256;

struct Instance {
    Engine engine;

    // Primary params live as NORMS because the matrix modulates pre-taper.
    float primary[PARAM_COUNT];

    // Secondary params, engineering units where the user sees them.
    float smoothing;   // norm
    float fadeMs;      // ms
    float drive;       // norm (0..1 -> 0.5..4.0)
    float character;   // 0..1
    float mix;          // 0..1
    float fbTone;       // norm

    ModRouter mod;

    float scratchInL[kBlockMax], scratchInR[kBlockMax];
    float scratchOutL[kBlockMax], scratchOutR[kBlockMax];
};

const char* kPrimaryNames[PARAM_COUNT] = {
    "pitch", "stretch", "threshold", "grain", "quality", "feedback"
};

int PrimaryIndex(const char* key) {
    for (int i = 0; i < PARAM_COUNT; i++)
        if (strcmp(key, kPrimaryNames[i]) == 0) return i;
    return -1;
}

// "mod_depth_<name>" / "mod_src_<name>" -> param index, or -1.
int ModIndex(const char* key, const char* prefix) {
    const size_t n = strlen(prefix);
    if (strncmp(key, prefix, n) != 0) return -1;
    for (int i = 0; i < PARAM_COUNT; i++)
        if (strcmp(key + n, kPrimaryNames[i]) == 0) return i;
    return -1;
}

void PushAll(Instance* s) {
    // Modulate in NORM space, then taper. Order is load-bearing — see
    // capicola_params.h's header comment and Task 2's test_params.cpp.
    const float pitchNorm = s->mod.Apply(PARAM_PITCH,     s->primary[PARAM_PITCH]);
    const float stretchN  = s->mod.Apply(PARAM_STRETCH,   s->primary[PARAM_STRETCH]);
    const float threshN   = s->mod.Apply(PARAM_THRESHOLD, s->primary[PARAM_THRESHOLD]);
    const float grainN    = s->mod.Apply(PARAM_GRAIN,     s->primary[PARAM_GRAIN]);
    const float qualityN  = s->mod.Apply(PARAM_QUALITY,   s->primary[PARAM_QUALITY]);
    const float fbN       = s->mod.Apply(PARAM_FEEDBACK,  s->primary[PARAM_FEEDBACK]);

    s->engine.SetSlicePitch(PitchCurve(PitchSemisFromNorm(pitchNorm)));
    s->engine.SetSliceStretch(StretchCurve(stretchN));
    s->engine.SetTransientThreshold(ThresholdCurve(threshN));
    s->engine.SetGrainSize(GrainKeyframesFromNorm(grainN));
    s->engine.SetQuality(QualityCurve(qualityN));
    s->engine.SetFeedback(fbN * 1.5f);

    s->engine.SetTkeoCutoff(SmoothingCurve(s->smoothing));
    s->engine.SetFade(FadeMsToSamples(s->fadeMs, kSampleRate));
    s->engine.SetDrive(DriveCurve(s->drive));
    s->engine.SetDriveCharacter(s->character);
    s->engine.SetMix(s->mix);
    s->engine.SetFeedbackCutoff(FbToneCurve(s->fbTone));
}

void* CreateInstance(const char* /*module_dir*/, const char* /*json_defaults*/) {
    Instance* s = new (std::nothrow) Instance();
    if (!s) return nullptr;
    if (!s->engine.Init()) { delete s; return nullptr; }

    s->primary[PARAM_PITCH]     = PitchNormFromSemis(0.0f);
    s->primary[PARAM_STRETCH]   = 0.0f;      // realtime
    s->primary[PARAM_THRESHOLD] = 0.5f;
    s->primary[PARAM_GRAIN]     = GrainNormFromKeyframes(128.0f);
    s->primary[PARAM_QUALITY]   = 1.0f;      // max fidelity
    s->primary[PARAM_FEEDBACK]  = 0.0f;

    s->smoothing = 0.4259f;
    s->fadeMs    = 20.0f;
    s->drive     = 0.1429f;
    s->character = 1.0f;
    s->mix       = 1.0f;
    s->fbTone    = 0.3769f;

    // Default modulation source is Output Env (1), matching upstream: depth
    // alone closes a self-modulating loop through the module's own output.
    for (int i = 0; i < PARAM_COUNT; i++) s->mod.SetSource(i, SRC_OUTPUT_ENV);

    PushAll(s);
    return s;
}

void DestroyInstance(void* inst) { delete static_cast<Instance*>(inst); }

void SetParam(void* inst, const char* key, const char* val) {
    Instance* s = static_cast<Instance*>(inst);
    if (!s || !key || !val) return;

    const int p = PrimaryIndex(key);
    if (p >= 0) {
        const float v = (float)atof(val);
        // Two params are exposed in engineering units; store their norm.
        if (p == PARAM_PITCH)         s->primary[p] = PitchNormFromSemis(v);
        else if (p == PARAM_GRAIN)    s->primary[p] = GrainNormFromKeyframes(v);
        else if (p == PARAM_FEEDBACK) s->primary[p] = Clamp01(v / 1.5f);
        else                          s->primary[p] = Clamp01(v);
        PushAll(s);
        return;
    }

    int m = ModIndex(key, "mod_depth_");
    if (m >= 0) { s->mod.SetDepth(m, (float)atof(val)); PushAll(s); return; }
    m = ModIndex(key, "mod_src_");
    if (m >= 0) { s->mod.SetSource(m, atoi(val)); PushAll(s); return; }

    if      (!strcmp(key, "smoothing")) s->smoothing = Clamp01((float)atof(val));
    else if (!strcmp(key, "fade_ms"))   s->fadeMs    = (float)atof(val);
    else if (!strcmp(key, "drive"))     s->drive     = Clamp01((float)atof(val));
    else if (!strcmp(key, "character")) s->character = Clamp01((float)atof(val));
    else if (!strcmp(key, "mix"))       s->mix       = Clamp01((float)atof(val));
    else if (!strcmp(key, "fb_tone"))   s->fbTone    = Clamp01((float)atof(val));
    else if (!strcmp(key, "slice"))     { s->engine.TriggerSlice(); return; }
    else if (!strcmp(key, "state")) {
        // Fixed-arity blob; see get_param. Order must match exactly. Refuse a
        // partial parse rather than half-restoring.
        float v[24];
        int n = sscanf(val,
            "%f %f %f %f %f %f %f %f %f %f %f %f "
            "%f %f %f %f %f %f %f %f %f %f %f %f",
            &v[0],&v[1],&v[2],&v[3],&v[4],&v[5],&v[6],&v[7],
            &v[8],&v[9],&v[10],&v[11],&v[12],&v[13],&v[14],&v[15],
            &v[16],&v[17],&v[18],&v[19],&v[20],&v[21],&v[22],&v[23]);
        if (n != 24) return;                      // refuse a partial restore
        for (int i = 0; i < PARAM_COUNT; i++) s->primary[i] = Clamp01(v[i]);
        s->smoothing = Clamp01(v[6]);
        s->fadeMs    = v[7];
        s->drive     = Clamp01(v[8]);
        s->character = Clamp01(v[9]);
        s->mix       = Clamp01(v[10]);
        s->fbTone    = Clamp01(v[11]);
        for (int i = 0; i < PARAM_COUNT; i++) {
            s->mod.SetDepth(i, v[12 + i]);
            s->mod.SetSource(i, (int)v[18 + i]);
        }
        PushAll(s);
        return;
    }
    else return;

    PushAll(s);
}

int GetParam(void* inst, const char* key, char* buf, int len) {
    Instance* s = static_cast<Instance*>(inst);
    if (!s || !key || !buf || len <= 0) return 0;

    const int p = PrimaryIndex(key);
    if (p >= 0) {
        float out;
        /* Declared int (semitones): report a whole number. Declaring it float
         * made the host derive the detent from the RANGE and ignore step,
         * giving 0.12 st per detent under an integer display. */
        if      (p == PARAM_PITCH)    return snprintf(buf, len, "%.0f", PitchSemisFromNorm(s->primary[p]));
        else if (false)               out = 0.0f;
        else if (p == PARAM_GRAIN)    out = GrainKeyframesFromNorm(s->primary[p]);
        else if (p == PARAM_FEEDBACK) out = s->primary[p] * 1.5f;
        else                          out = s->primary[p];
        return snprintf(buf, (size_t)len, "%.4f", out);
    }

    int m = ModIndex(key, "mod_depth_");
    if (m >= 0) return snprintf(buf, (size_t)len, "%.4f", s->mod.Depth(m));
    m = ModIndex(key, "mod_src_");
    if (m >= 0) return snprintf(buf, (size_t)len, "%d", s->mod.Source(m));

    if (!strcmp(key, "smoothing")) return snprintf(buf, (size_t)len, "%.4f", s->smoothing);
    if (!strcmp(key, "fade_ms"))   return snprintf(buf, (size_t)len, "%.2f", s->fadeMs);
    if (!strcmp(key, "drive"))     return snprintf(buf, (size_t)len, "%.4f", s->drive);
    if (!strcmp(key, "character")) return snprintf(buf, (size_t)len, "%.4f", s->character);
    if (!strcmp(key, "mix"))       return snprintf(buf, (size_t)len, "%.4f", s->mix);
    if (!strcmp(key, "fb_tone"))   return snprintf(buf, (size_t)len, "%.4f", s->fbTone);

    if (!strcmp(key, "state")) {
        return snprintf(buf, (size_t)len,
            "%.6f %.6f %.6f %.6f %.6f %.6f %.6f %.4f %.6f %.6f %.6f %.6f "
            "%.6f %.6f %.6f %.6f %.6f %.6f %d %d %d %d %d %d",
            s->primary[0], s->primary[1], s->primary[2],
            s->primary[3], s->primary[4], s->primary[5],
            s->smoothing, s->fadeMs, s->drive, s->character, s->mix, s->fbTone,
            s->mod.Depth(0), s->mod.Depth(1), s->mod.Depth(2),
            s->mod.Depth(3), s->mod.Depth(4), s->mod.Depth(5),
            s->mod.Source(0), s->mod.Source(1), s->mod.Source(2),
            s->mod.Source(3), s->mod.Source(4), s->mod.Source(5));
    }

    if (!strcmp(key, "chain_params")) {
        // Authoritative metadata. param_meta.mjs merges as
        // { ...inlineHierarchyMeta, ...chainParamsMeta } — chain_params wins —
        // so this and module.json's inline metadata must agree (Task 5).
        return snprintf(buf, (size_t)len, "%s",
        "["
        "{\"key\":\"pitch\",\"name\":\"Pitch\",\"type\":\"int\",\"min\":-12,\"max\":12,\"step\":1,\"default\":0,\"unit\":\"st\"},"
        "{\"key\":\"stretch\",\"name\":\"Stretch\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0},"
        "{\"key\":\"threshold\",\"name\":\"Threshold\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0.5},"
        "{\"key\":\"grain\",\"name\":\"Grain\",\"type\":\"int\",\"min\":32,\"max\":4096,\"default\":128},"
        "{\"key\":\"quality\",\"name\":\"Quality\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":1},"
        "{\"key\":\"feedback\",\"name\":\"Feedback\",\"type\":\"float\",\"min\":0,\"max\":1.5,\"step\":0.01,\"default\":0},"
        "{\"key\":\"smoothing\",\"name\":\"Smoothing\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0.4259},"
        "{\"key\":\"fade_ms\",\"name\":\"Fade\",\"type\":\"float\",\"min\":10,\"max\":250,\"step\":1,\"default\":20,\"unit\":\"ms\"},"
        "{\"key\":\"drive\",\"name\":\"Drive\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0.1429},"
        "{\"key\":\"character\",\"name\":\"Character\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":1},"
        "{\"key\":\"mix\",\"name\":\"Mix\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":1},"
        "{\"key\":\"fb_tone\",\"name\":\"Feedback Tone\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0.3769},"
        "{\"key\":\"mod_depth_pitch\",\"name\":\"Pitch Depth\",\"type\":\"float\",\"min\":-1,\"max\":1,\"step\":0.01,\"default\":0},"
        "{\"key\":\"mod_depth_stretch\",\"name\":\"Stretch Depth\",\"type\":\"float\",\"min\":-1,\"max\":1,\"step\":0.01,\"default\":0},"
        "{\"key\":\"mod_depth_threshold\",\"name\":\"Threshold Depth\",\"type\":\"float\",\"min\":-1,\"max\":1,\"step\":0.01,\"default\":0},"
        "{\"key\":\"mod_depth_grain\",\"name\":\"Grain Depth\",\"type\":\"float\",\"min\":-1,\"max\":1,\"step\":0.01,\"default\":0},"
        "{\"key\":\"mod_depth_quality\",\"name\":\"Quality Depth\",\"type\":\"float\",\"min\":-1,\"max\":1,\"step\":0.01,\"default\":0},"
        "{\"key\":\"mod_depth_feedback\",\"name\":\"Feedback Depth\",\"type\":\"float\",\"min\":-1,\"max\":1,\"step\":0.01,\"default\":0},"
        "{\"key\":\"mod_src_pitch\",\"name\":\"Pitch\",\"type\":\"enum\",\"options\":[\"Input Env\",\"Output Env\"],\"default\":1},"
        "{\"key\":\"mod_src_stretch\",\"name\":\"Stretch\",\"type\":\"enum\",\"options\":[\"Input Env\",\"Output Env\"],\"default\":1},"
        "{\"key\":\"mod_src_threshold\",\"name\":\"Threshold\",\"type\":\"enum\",\"options\":[\"Input Env\",\"Output Env\"],\"default\":1},"
        "{\"key\":\"mod_src_grain\",\"name\":\"Grain\",\"type\":\"enum\",\"options\":[\"Input Env\",\"Output Env\"],\"default\":1},"
        "{\"key\":\"mod_src_quality\",\"name\":\"Quality\",\"type\":\"enum\",\"options\":[\"Input Env\",\"Output Env\"],\"default\":1},"
        "{\"key\":\"mod_src_feedback\",\"name\":\"Feedback\",\"type\":\"enum\",\"options\":[\"Input Env\",\"Output Env\"],\"default\":1}"
        "]");
    }

    if (!strcmp(key, "ui_hierarchy")) {
        return snprintf(buf, (size_t)len, "%s",
        "{\"modes\":null,\"levels\":{"
        "\"root\":{\"label\":\"Capicola\","
          "\"knobs\":[\"pitch\",\"stretch\",\"threshold\",\"grain\",\"quality\",\"feedback\"],"
          "\"params\":["
            "{\"key\":\"pitch\",\"label\":\"Pitch\"},"
            "{\"key\":\"stretch\",\"label\":\"Stretch\"},"
            "{\"key\":\"threshold\",\"label\":\"Threshold\"},"
            "{\"key\":\"grain\",\"label\":\"Grain\"},"
            "{\"key\":\"quality\",\"label\":\"Quality\"},"
            "{\"key\":\"feedback\",\"label\":\"Feedback\"},"
            "{\"level\":\"secondary\",\"label\":\"Secondary\"},"
            "{\"level\":\"modulation\",\"label\":\"Mod Depth\"},"
            "{\"level\":\"mod_source\",\"label\":\"Mod Source\"}"
          "]},"
        "\"secondary\":{\"label\":\"Secondary\","
          "\"knobs\":[\"smoothing\",\"fade_ms\",\"drive\",\"character\",\"mix\",\"fb_tone\"],"
          "\"params\":["
            "{\"key\":\"smoothing\",\"label\":\"Smoothing\"},"
            "{\"key\":\"fade_ms\",\"label\":\"Fade\"},"
            "{\"key\":\"drive\",\"label\":\"Drive\"},"
            "{\"key\":\"character\",\"label\":\"Character\"},"
            "{\"key\":\"mix\",\"label\":\"Mix\"},"
            "{\"key\":\"fb_tone\",\"label\":\"Feedback Tone\"}"
          "]},"
        "\"modulation\":{\"label\":\"Mod Depth\","
          "\"knobs\":[\"mod_depth_pitch\",\"mod_depth_stretch\",\"mod_depth_threshold\","
                    "\"mod_depth_grain\",\"mod_depth_quality\",\"mod_depth_feedback\"],"
          "\"params\":["
            "{\"key\":\"mod_depth_pitch\",\"label\":\"Pitch Depth\"},"
            "{\"key\":\"mod_depth_stretch\",\"label\":\"Stretch Depth\"},"
            "{\"key\":\"mod_depth_threshold\",\"label\":\"Threshold Depth\"},"
            "{\"key\":\"mod_depth_grain\",\"label\":\"Grain Depth\"},"
            "{\"key\":\"mod_depth_quality\",\"label\":\"Quality Depth\"},"
            "{\"key\":\"mod_depth_feedback\",\"label\":\"Feedback Depth\"}"
          "]},"
        "\"mod_source\":{\"label\":\"Mod Source\","
          "\"knobs\":[\"mod_src_pitch\",\"mod_src_stretch\",\"mod_src_threshold\","
                    "\"mod_src_grain\",\"mod_src_quality\",\"mod_src_feedback\"],"
          "\"params\":["
            "{\"key\":\"mod_src_pitch\",\"label\":\"Pitch\"},"
            "{\"key\":\"mod_src_stretch\",\"label\":\"Stretch\"},"
            "{\"key\":\"mod_src_threshold\",\"label\":\"Threshold\"},"
            "{\"key\":\"mod_src_grain\",\"label\":\"Grain\"},"
            "{\"key\":\"mod_src_quality\",\"label\":\"Quality\"},"
            "{\"key\":\"mod_src_feedback\",\"label\":\"Feedback\"}"
          "]}"
        "}}");
    }

    /* Out-of-band diagnostic: deliberately NOT in chain_params or ui_hierarchy,
     * so it occupies no cell and does not change the page plan. It exists
     * because slice firing was otherwise verifiable only by ear - this makes
     * "did a transient re-anchor happen" answerable over the param channel.
     * Monotonic; counts kept DETECTOR events, so a forced slice (MIDI note or
     * the stereo drift guard) does not advance it. */
    if (!strcmp(key, "transient_count")) return snprintf(buf, (size_t)len, "%u", s->engine.TransientCount());
    if (!strcmp(key, "slice_count"))     return snprintf(buf, (size_t)len, "%u", s->engine.SliceCount());

    if (!strcmp(key, "consumes_line_input")) return snprintf(buf, (size_t)len, "0");

    buf[0] = '\0';
    return 0;
}

void ProcessBlock(void* inst, int16_t* out_lr, int frames) {
    Instance* s = static_cast<Instance*>(inst);
    if (!s || !out_lr) return;
    if (frames > kBlockMax) frames = kBlockMax;
    if (frames < 0) frames = 0;

    for (int i = 0; i < frames; i++) {
        s->scratchInL[i] = out_lr[i * 2 + 0] / 32768.0f;
        s->scratchInR[i] = out_lr[i * 2 + 1] / 32768.0f;
    }

    s->engine.ProcessBlock(s->scratchInL, s->scratchInR,
                           s->scratchOutL, s->scratchOutR, (size_t)frames);

    // Feed the followers back into the matrix, then re-push. Once per block =
    // 344 Hz at 128 frames / 44.1 kHz, ample for envelope modulation.
    s->mod.SetEnvelopes(s->engine.EnvNormIn(), s->engine.EnvNormOut());
    PushAll(s);

    for (int i = 0; i < frames; i++) {
        float a = s->scratchOutL[i], b = s->scratchOutR[i];
        if (!std::isfinite(a)) a = 0.0f;
        if (!std::isfinite(b)) b = 0.0f;
        if (a >  1.0f) a =  1.0f; if (a < -1.0f) a = -1.0f;
        if (b >  1.0f) b =  1.0f; if (b < -1.0f) b = -1.0f;
        out_lr[i * 2 + 0] = (int16_t)(a * 32767.0f);
        out_lr[i * 2 + 1] = (int16_t)(b * 32767.0f);
    }
}

void OnMidi(void* inst, const uint8_t* msg, int len, int /*source*/) {
    Instance* s = static_cast<Instance*>(inst);
    if (!s || !msg || len < 3) return;
    // Note-on with velocity: upstream's B2 — force a slice on both channels,
    // bypassing the threshold mute.
    if ((msg[0] & 0xF0) == 0x90 && msg[2] > 0) s->engine.TriggerSlice();
}

audio_fx_api_v2_t g_api;

} // namespace

extern "C" audio_fx_api_v2_t* move_audio_fx_init_v2(const host_api_v1_t* /*host*/) {
    g_api.api_version      = AUDIO_FX_API_VERSION_2;
    g_api.create_instance  = CreateInstance;
    g_api.destroy_instance = DestroyInstance;
    g_api.process_block    = ProcessBlock;
    g_api.set_param        = SetParam;
    g_api.get_param        = GetParam;
    g_api.on_midi           = OnMidi;
    return &g_api;
}

// ALSO discovered by the chain host via dlsym("move_audio_fx_on_midi") and
// broadcast to every audio FX — the same path the ducker uses. Kept alongside
// the struct member above; the chain host's FX MIDI broadcast (chain_host.c)
// looks this symbol up independently of the on_midi function pointer.
extern "C" void move_audio_fx_on_midi(void* inst, const uint8_t* msg, int len, int source) {
    OnMidi(inst, msg, len, source);
}
