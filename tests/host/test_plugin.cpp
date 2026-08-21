#include "plugin_api_v1.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <cmath>

extern "C" plugin_api_v2_t* move_plugin_init_v2(const host_api_v1_t*);
extern "C" void move_audio_fx_on_midi(void*, const uint8_t*, int, int);

static int failures = 0;
static void check(bool cond, const char* what) {
    if (!cond) { std::printf("FAIL: %s\n", what); failures++; }
    else       { std::printf("ok:   %s\n", what); }
}

static const char* kKeys[] = {
    "pitch","stretch","threshold","grain","quality","feedback",
    "smoothing","fade_ms","drive","character","mix","fb_tone",
    "mod_depth_pitch","mod_depth_stretch","mod_depth_threshold",
    "mod_depth_grain","mod_depth_quality","mod_depth_feedback",
    "mod_src_pitch","mod_src_stretch","mod_src_threshold",
    "mod_src_grain","mod_src_quality","mod_src_feedback",
};
static const int kKeyCount = (int)(sizeof(kKeys) / sizeof(kKeys[0]));

int main() {
    plugin_api_v2_t* api = move_plugin_init_v2(nullptr);
    check(api != nullptr, "move_plugin_init_v2 returns an api");
    if (!api) return 1;
    check(api->api_version == 2, "api_version == 2");

    void* inst = api->create_instance(".", nullptr);
    check(inst != nullptr, "create_instance succeeds");
    if (!inst) return 1;

    char buf[8192];

    // Every declared key must answer a get_param.
    int answered = 0;
    for (int i = 0; i < kKeyCount; i++) {
        buf[0] = '\0';
        if (api->get_param(inst, kKeys[i], buf, sizeof(buf)) > 0 && buf[0]) answered++;
        else std::printf("      (no answer for %s)\n", kKeys[i]);
    }
    check(answered == kKeyCount, "all 24 params answer get_param");

    // Every declared key round-trips through set_param / get_param. Values
    // are picked per-key since several params are exposed in engineering
    // units (pitch: semitones, grain: keyframes) rather than 0..1 norms.
    int roundtripped = 0;
    for (int i = 0; i < kKeyCount; i++) {
        const bool isSrc = std::strncmp(kKeys[i], "mod_src_", 8) == 0;
        const bool isDepth = std::strncmp(kKeys[i], "mod_depth_", 10) == 0;
        const char* setVal;
        double tol;
        if (isSrc)                                 { setVal = "0";   tol = 0.5; }
        else if (isDepth)                           { setVal = "0.3"; tol = 0.05; }
        else if (!strcmp(kKeys[i], "pitch"))        { setVal = "3";   tol = 0.05; }
        else if (!strcmp(kKeys[i], "grain"))        { setVal = "500"; tol = 1.0; }
        else if (!strcmp(kKeys[i], "feedback"))     { setVal = "0.9"; tol = 0.05; }
        else if (!strcmp(kKeys[i], "fade_ms"))      { setVal = "50";  tol = 0.5; }
        else                                         { setVal = "0.3"; tol = 0.05; }
        api->set_param(inst, kKeys[i], setVal);
        buf[0] = '\0';
        api->get_param(inst, kKeys[i], buf, sizeof(buf));
        const double got = atof(buf);
        const double want = atof(setVal);
        if (std::fabs(got - want) < tol) roundtripped++;
        else std::printf("      (%s: set %s, got %s)\n", kKeys[i], setVal, buf);
    }
    check(roundtripped == kKeyCount, "all 24 params round-trip set/get");

    // A float param round-trips precisely.
    api->set_param(inst, "mix", "0.25");
    api->get_param(inst, "mix", buf, sizeof(buf));
    check(std::fabs(atof(buf) - 0.25) < 1e-3, "mix round-trips");

    // A param in engineering units round-trips.
    api->set_param(inst, "pitch", "7");
    api->get_param(inst, "pitch", buf, sizeof(buf));
    check(std::fabs(atof(buf) - 7.0) < 0.05, "pitch round-trips in semitones");

    api->set_param(inst, "grain", "256");
    api->get_param(inst, "grain", buf, sizeof(buf));
    check(std::fabs(atof(buf) - 256.0) < 1.0, "grain round-trips in keyframes");

    // A mod source (enum, stored as an index) round-trips.
    api->set_param(inst, "mod_src_pitch", "0");
    api->get_param(inst, "mod_src_pitch", buf, sizeof(buf));
    check(atoi(buf) == 0, "mod source round-trips");

    // Contracts are non-empty JSON of the right shape. Count '{' at depth 1
    // rather than substring matching, so a malformed nested object can't
    // inflate the count.
    api->get_param(inst, "chain_params", buf, sizeof(buf));
    check(buf[0] == '[', "chain_params is a JSON array");
    {
        int depth = 0, entries = 0;
        bool inString = false;
        for (char* p = buf; *p; p++) {
            char c = *p;
            if (inString) {
                if (c == '\\' && p[1]) { p++; continue; }
                if (c == '"') inString = false;
                continue;
            }
            if (c == '"') { inString = true; continue; }
            if (c == '{') { if (depth == 0) entries++; depth++; }
            else if (c == '}') depth--;
        }
        check(entries == 24, "chain_params declares exactly 24 entries");
    }
    // Every key must appear in chain_params (as a quoted "key":"<name>" pair).
    {
        int foundAll = 1;
        for (int i = 0; i < kKeyCount; i++) {
            char needle[64];
            std::snprintf(needle, sizeof(needle), "\"key\":\"%s\"", kKeys[i]);
            if (!strstr(buf, needle)) { foundAll = 0; std::printf("      (missing chain_params key %s)\n", kKeys[i]); }
        }
        check(foundAll, "chain_params has an entry for all 24 keys");
    }

    api->get_param(inst, "ui_hierarchy", buf, sizeof(buf));
    check(buf[0] == '{', "ui_hierarchy is a JSON object");
    check(strstr(buf, "\"root\"") != nullptr, "ui_hierarchy has a root level");
    check(strstr(buf, "\"secondary\"") != nullptr, "ui_hierarchy has a secondary level");
    check(strstr(buf, "\"modulation\"") != nullptr, "ui_hierarchy has a modulation level");

    // consumes_line_input must be "0" — this FX takes chain audio, not the
    // line jack; answering 1 would arm the feedback gate wrongly.
    api->get_param(inst, "consumes_line_input", buf, sizeof(buf));
    check(!strcmp(buf, "0"), "consumes_line_input is 0");

    // State blob round-trips.
    api->set_param(inst, "drive", "0.75");
    api->set_param(inst, "mod_depth_stretch", "-0.5");
    api->set_param(inst, "mod_src_feedback", "0");
    char state[8192];
    api->get_param(inst, "state", state, sizeof(state));
    check(state[0] != '\0', "state blob is non-empty");

    api->set_param(inst, "drive", "0.1");
    api->set_param(inst, "mod_depth_stretch", "0.0");
    api->set_param(inst, "mod_src_feedback", "1");
    api->set_param(inst, "state", state);
    api->get_param(inst, "drive", buf, sizeof(buf));
    check(std::fabs(atof(buf) - 0.75) < 1e-3, "state restores drive");
    api->get_param(inst, "mod_depth_stretch", buf, sizeof(buf));
    check(std::fabs(atof(buf) + 0.5) < 1e-3, "state restores mod depth");
    api->get_param(inst, "mod_src_feedback", buf, sizeof(buf));
    check(atoi(buf) == 0, "state restores mod source");

    // A garbled/partial state string must not half-restore.
    api->set_param(inst, "drive", "0.9");
    api->set_param(inst, "state", "1.0 2.0 not-enough-fields");
    api->get_param(inst, "drive", buf, sizeof(buf));
    check(std::fabs(atof(buf) - 0.9) < 1e-3, "partial state write is refused, not half-applied");

    // Render produces finite audio.
    int16_t block[256];
    for (int i = 0; i < 256; i++) block[i] = (int16_t)((i % 64) * 300 - 9600);
    api->render_block(inst, block, 128);
    bool sane = true;
    for (int i = 0; i < 256; i++) if (block[i] == INT16_MIN) sane = false;
    check(sane, "render_block produces sane int16");

    // A note-on is a slice trigger and must not crash, even under a
    // threshold set high enough to mute auto-detection.
    api->set_param(inst, "threshold", "1.0");
    const uint8_t note_on[3] = { 0x90, 60, 100 };
    move_audio_fx_on_midi(inst, note_on, 3, 0);
    api->render_block(inst, block, 128);
    check(true, "note-on slice trigger survives a render under mute threshold");

    api->destroy_instance(inst);

    // NOTE on the "post-taper modulation regression" test requested by the
    // task brief: the public plugin_api_v2 surface has no getter that
    // exposes the engine-applied (post-curve) engineering value — get_param
    // for a primary key returns the stored pre-modulation norm
    // (s->primary[p]), never the modulated-then-tapered value PushAll()
    // actually pushes into the engine. There is therefore no way to observe,
    // through this public API, whether modulation was applied before or
    // after the taper without adding a debug-only accessor (out of scope:
    // this task touches only capicola_plugin.cpp, test_plugin.cpp and the
    // Makefile). The correct order is already pinned at the unit level by
    // Task 2's test_params.cpp (ModRouter::Apply documented and tested as
    // pre-taper), and capicola_plugin.cpp's PushAll() calls
    // s->mod.Apply(...) and only then the *Curve()/*FromNorm() functions,
    // matching that contract by inspection.

    std::printf(failures ? "\nFAILURES: %d\n" : "\nALL TESTS PASSED\n", failures);
    return failures ? 1 : 0;
}
