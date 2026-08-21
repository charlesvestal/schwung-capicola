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

    // Freeze, checked differentially: two fresh engines fed the IDENTICAL noise
    // sequence, then silence. A max-over-blocks check is vacuous here — the
    // first few silence blocks still carry the tail of the noise that was
    // still working through the read head, in BOTH settings, so it can't tell
    // frozen from realtime. What must differ is the steady state once that
    // tail has drained: frozen holds material indefinitely, realtime empties.
    // We read the FINAL block's RMS (steady state, not peak) and require
    // frozen to still be loud while realtime has gone to silence.
    Engine* e_frozen = new Engine();
    Engine* e_realtime = new Engine();
    check(e_frozen->Init() && e_realtime->Init(), "differential engines initialise");
    e_frozen->SetSliceStretch(StretchCurve(1.0f));      // frozen
    e_realtime->SetSliceStretch(StretchCurve(0.0f));    // realtime
    e_frozen->SetSlicePitch(PitchCurve(0.0f));
    e_realtime->SetSlicePitch(PitchCurve(0.0f));
    e_frozen->SetMix(1.0f);
    e_realtime->SetMix(1.0f);

    unsigned dseed = 777u;
    float dl[128], dr[128], fol[128], forr[128], rol[128], rorr[128];
    for (int blk = 0; blk < 700; blk++) {
        unsigned s = dseed;   // identical noise into both engines this block
        for (int i = 0; i < 128; i++) { dl[i] = noise(s) * 0.5f; dr[i] = dl[i]; }
        dseed = s;
        e_frozen->ProcessBlock(dl, dr, fol, forr, 128);
        e_realtime->ProcessBlock(dl, dr, rol, rorr, 128);
    }

    float frozen_tail = 0.0f, realtime_tail = 0.0f;
    for (int blk = 0; blk < 200; blk++) {
        for (int i = 0; i < 128; i++) { dl[i] = 0.0f; dr[i] = 0.0f; }
        e_frozen->ProcessBlock(dl, dr, fol, forr, 128);
        e_realtime->ProcessBlock(dl, dr, rol, rorr, 128);
        if (blk == 199) {   // steady state: last block only, not the max
            frozen_tail   = rms(fol, 128);
            realtime_tail = rms(rol, 128);
        }
    }
    check(frozen_tail > 0.01f, "full stretch freezes: tail stays non-silent");
    check(realtime_tail < 0.001f, "realtime stretch: tail decays to silence");
    check(frozen_tail > 10.0f * realtime_tail,
          "freeze holds material realtime has already consumed (frozen >> realtime)");
    delete e_frozen;
    delete e_realtime;

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
