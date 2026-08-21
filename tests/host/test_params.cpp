#include "capicola_params.h"
#include <cstdio>
#include <cmath>
#include <initializer_list>

using namespace capicola_schwung;

static int failures = 0;
static void near(float got, float want, float tol, const char* what) {
    if (std::fabs(got - want) > tol) {
        std::printf("FAIL: %s (got %.6f, want %.6f)\n", what, got, want);
        failures++;
    } else {
        std::printf("ok:   %s\n", what);
    }
}
static void check(bool cond, const char* what) {
    if (!cond) { std::printf("FAIL: %s\n", what); failures++; }
    else       { std::printf("ok:   %s\n", what); }
}

int main() {
    // --- Primary curves ---
    near(StretchCurve(0.0f), 1.0f,   1e-6f, "stretch norm 0 -> realtime 1.0");
    near(StretchCurve(1.0f), 0.0f,   1e-6f, "stretch norm 1 -> frozen 0.0");
    near(StretchCurve(0.5f), std::pow(0.5f, 2.5f), 1e-6f, "stretch noon ~ 5.7x");

    near(ThresholdCurve(0.0f), 0.0f, 1e-6f, "threshold norm 0 -> ratio 0");
    near(ThresholdCurve(0.9f), 4.0f, 1e-5f, "threshold norm 0.9 -> ratio 4");
    near(ThresholdCurve(0.95f), 6.0f, 1e-5f, "threshold norm 0.95 -> ratio 6");
    check(ThresholdCurve(1.0f) > 1.0e8f,   "threshold top -> mute sentinel");
    check(ThresholdCurve(0.995f) > 1.0e8f, "threshold above 0.99 -> mute sentinel");

    near(QualityCurve(0.0f), 0.1f,   1e-6f, "quality CCW -> eps 0.1 (crunchy)");
    near(QualityCurve(1.0f), 0.001f, 1e-6f, "quality CW  -> eps 0.001 (fidelity)");

    near(PitchCurve(0.0f),  1.0f, 1e-6f, "pitch 0 st -> ratio 1.0");
    near(PitchCurve(12.0f), 2.0f, 1e-5f, "pitch +12 st -> ratio 2.0");
    near(PitchCurve(0.1f),  1.0f, 1e-6f, "pitch inside detent snaps to unity");

    // --- Secondary curves ---
    near(SmoothingCurve(0.4259f), 0.0014f, 1e-4f, "smoothing default -> fc 0.0014");
    near(FbToneCurve(0.3769f),    0.02f,   1e-3f, "fb tone default -> fc 0.02");
    near(DriveCurve(0.0f), 0.5f, 1e-6f, "drive min 0.5");
    near(DriveCurve(1.0f), 4.0f, 1e-6f, "drive max 4.0");
    near(FadeMsToSamples(20.0f, 44100.0f), 882.0f, 0.5f, "20 ms -> 882 samples @44.1k");

    // --- Engineering-unit round trips (the matrix needs the inverse) ---
    for (float n : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
        near(PitchNormFromSemis(PitchSemisFromNorm(n)), n, 1e-4f, "pitch norm round trip");
        near(GrainNormFromKeyframes(GrainKeyframesFromNorm(n)), n, 1e-4f, "grain norm round trip");
    }
    near(GrainKeyframesFromNorm(0.0f), 32.0f,   1e-3f, "grain norm 0 -> 32 keyframes");
    near(GrainKeyframesFromNorm(1.0f), 4096.0f, 1e-3f, "grain norm 1 -> 4096 keyframes");

    // --- Modulation is applied PRE-taper ---
    // The bug this catches: applying mod after the taper gives the SAME
    // engineering delta at both ends of a (1-x)^2.5 sweep. Pre-taper, the
    // deltas differ because the curve's slope differs.
    {
        ModRouter mod;
        mod.SetDepth(PARAM_STRETCH, 0.1f);
        mod.SetSource(PARAM_STRETCH, SRC_INPUT_ENV);
        mod.SetEnvelopes(/*in=*/1.0f, /*out=*/0.0f);

        const float lowBase  = 0.1f;
        const float highBase = 0.8f;
        const float dLow  = StretchCurve(mod.Apply(PARAM_STRETCH, lowBase))
                          - StretchCurve(lowBase);
        const float dHigh = StretchCurve(mod.Apply(PARAM_STRETCH, highBase))
                          - StretchCurve(highBase);
        check(std::fabs(std::fabs(dLow) - std::fabs(dHigh)) > 1e-3f,
              "stretch mod delta differs across the sweep (pre-taper)");
    }

    // --- Modulation clamps in norm space ---
    {
        ModRouter mod;
        mod.SetDepth(PARAM_PITCH, 1.0f);
        mod.SetSource(PARAM_PITCH, SRC_INPUT_ENV);
        mod.SetEnvelopes(1.0f, 0.0f);
        near(mod.Apply(PARAM_PITCH, 0.9f), 1.0f, 1e-6f, "mod clamps at 1.0");

        mod.SetDepth(PARAM_PITCH, -1.0f);
        near(mod.Apply(PARAM_PITCH, 0.1f), 0.0f, 1e-6f, "mod clamps at 0.0");
    }

    // --- Source selection routes the right envelope ---
    {
        ModRouter mod;
        mod.SetDepth(PARAM_FEEDBACK, 0.5f);
        mod.SetSource(PARAM_FEEDBACK, SRC_OUTPUT_ENV);
        mod.SetEnvelopes(/*in=*/1.0f, /*out=*/0.0f);
        near(mod.Apply(PARAM_FEEDBACK, 0.5f), 0.5f, 1e-6f, "output-env source ignores input env");
        mod.SetEnvelopes(0.0f, 1.0f);
        near(mod.Apply(PARAM_FEEDBACK, 0.5f), 1.0f, 1e-6f, "output-env source follows output env");
    }

    // --- Zero depth is exactly bypass ---
    {
        ModRouter mod;
        mod.SetEnvelopes(1.0f, 1.0f);
        near(mod.Apply(PARAM_QUALITY, 0.37f), 0.37f, 1e-6f, "zero depth is bypass");
    }

    // --- Default source and index bounds-checking ---
    {
        ModRouter mod;
        check(mod.Source(PARAM_STRETCH) == SRC_OUTPUT_ENV, "default source is output env");
        // Out-of-range accessors must degrade safely, not crash or read OOB.
        near(mod.Depth(-1), 0.0f, 1e-6f, "Depth() out of range returns 0");
        near(mod.Depth(PARAM_COUNT), 0.0f, 1e-6f, "Depth() out of range (high) returns 0");
        check(mod.Source(-1) == SRC_OUTPUT_ENV, "Source() out of range returns default");
        near(mod.Apply(-1, 0.42f), 0.42f, 1e-6f, "Apply() out of range is a no-op passthrough");
        mod.SetDepth(-1, 1.0f);       // must not crash
        mod.SetSource(PARAM_COUNT, SRC_INPUT_ENV); // must not crash
        mod.SetDepth(PARAM_PITCH, 5.0f);
        near(mod.Depth(PARAM_PITCH), 1.0f, 1e-6f, "SetDepth clamps above range to +1");
        mod.SetDepth(PARAM_PITCH, -5.0f);
        near(mod.Depth(PARAM_PITCH), -1.0f, 1e-6f, "SetDepth clamps below range to -1");
    }

    std::printf(failures ? "\nFAILURES: %d\n" : "\nALL TESTS PASSED\n", failures);
    return failures ? 1 : 0;
}
