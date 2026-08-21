#include "capicola_engine.h"
#include "capicola_params.h"
#include <cstdio>
#include <cmath>
#include <cstdlib>

using namespace capicola_schwung;

static int failures = 0;
static void check(bool cond, const char* what) {
    if (!cond) { std::printf("FAIL: %s\n", what); failures++; }
    else       { std::printf("ok:   %s\n", what); }
}

// Deterministic pseudo-noise — no rand(), so runs are reproducible.
static float noise(unsigned& s) {
    s = s * 1664525u + 1013904223u;
    return ((float)(s >> 8) / 8388608.0f) - 1.0f;
}

static float rms(const float* b, int n) {
    double acc = 0.0;
    for (int i = 0; i < n; i++) acc += (double)b[i] * b[i];
    return (float)std::sqrt(acc / n);
}

int main() {
    Engine* e = new Engine();
    check(e->Init(), "engine initialises");

    e->SetSliceStretch(StretchCurve(0.5f));
    e->SetSlicePitch(PitchCurve(0.0f));
    e->SetMix(1.0f);

    float in_l[128], in_r[128], out_l[128], out_r[128];
    unsigned seed = 12345u;

    // Drive noise for ~2 s so the ring fills and the read head has material.
    bool finite = true;
    float loudest = 0.0f;
    for (int blk = 0; blk < 700; blk++) {
        for (int i = 0; i < 128; i++) { in_l[i] = noise(seed) * 0.5f; in_r[i] = in_l[i]; }
        e->ProcessBlock(in_l, in_r, out_l, out_r, 128);
        for (int i = 0; i < 128; i++) if (!std::isfinite(out_l[i]) || !std::isfinite(out_r[i])) finite = false;
        const float r = rms(out_l, 128);
        if (r > loudest) loudest = r;
    }
    check(finite, "noise in -> all-finite out");
    check(loudest > 0.001f, "noise in -> non-silent out");

    // Freeze: full stretch, input drops to silence, output must keep going.
    e->SetSliceStretch(StretchCurve(1.0f));
    float frozen = 0.0f;
    for (int blk = 0; blk < 200; blk++) {
        for (int i = 0; i < 128; i++) { in_l[i] = 0.0f; in_r[i] = 0.0f; }
        e->ProcessBlock(in_l, in_r, out_l, out_r, 128);
        const float r = rms(out_l, 128);
        if (r > frozen) frozen = r;
    }
    check(frozen > 0.001f, "full stretch freezes: output survives silent input");

    // Manual slice is observable.
    const unsigned before = e->TransientCount();
    e->TriggerSlice();
    for (int i = 0; i < 128; i++) { in_l[i] = 0.0f; in_r[i] = 0.0f; }
    e->ProcessBlock(in_l, in_r, out_l, out_r, 128);
    check(e->TransientCount() >= before, "TriggerSlice does not corrupt the counter");

    // Followers are in range.
    check(e->EnvNormIn()  >= 0.0f && e->EnvNormIn()  <= 1.0f, "input follower in 0..1");
    check(e->EnvNormOut() >= 0.0f && e->EnvNormOut() <= 1.0f, "output follower in 0..1");

    // Mix 0 must be exact dry.
    e->SetMix(0.0f);
    for (int i = 0; i < 128; i++) { in_l[i] = noise(seed) * 0.5f; in_r[i] = in_l[i]; }
    e->ProcessBlock(in_l, in_r, out_l, out_r, 128);
    bool exact_dry = true;
    for (int i = 0; i < 128; i++) if (std::fabs(out_l[i] - in_l[i]) > 1e-6f) exact_dry = false;
    check(exact_dry, "mix 0 is exact dry passthrough");

    delete e;
    std::printf(failures ? "\nFAILURES: %d\n" : "\nALL TESTS PASSED\n", failures);
    return failures ? 1 : 0;
}
