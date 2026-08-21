#include "capicola_params.h"
#include <cmath>

namespace capicola_schwung {

float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

// Upstream StretchCurve: linear gave 2x at noon and dumped all the range into
// the last few percent; exp was too steep at the top. (1-n)^2.5 lands ~5.7x at
// noon and reaches a true freeze.
float StretchCurve(float norm) {
    return std::pow(1.0f - Clamp01(norm), 2.5f);
}

// Upstream: 0..90% of the sweep is ratio 0..4 over the envelope average, the
// last 10% climbs 4..8; the very top hard-mutes auto-triggering via a sentinel
// far above any real ratio.
float ThresholdCurve(float norm) {
    const float n = Clamp01(norm);
    if (n > 0.99f) return 1.0e9f;
    return (n <= 0.9f) ? (n * (4.0f / 0.9f))
                        : (4.0f + (n - 0.9f) * 40.0f);
}

// Upstream declares .Linear(0.1f, 0.001f) -- inverted, so CW is max fidelity.
float QualityCurve(float norm) {
    const float n = Clamp01(norm);
    return 0.1f + n * (0.001f - 0.1f);
}

// Upstream applies a +/-0.2 st detent so a pot wobbling near noon still reads
// as true unity.
float PitchCurve(float semitones) {
    float s = semitones;
    if (s > -0.2f && s < 0.2f) s = 0.0f;
    return std::exp2(s / 12.0f);
}

float SmoothingCurve(float norm) {
    const float n = Clamp01(norm);
    return 5.0e-5f * std::pow(0.125f / 5.0e-5f, n);
}

float FbToneCurve(float norm) {
    const float n = Clamp01(norm);
    return 2.0e-3f * std::pow(0.9f / 2.0e-3f, n);
}

float DriveCurve(float norm) {
    return 0.5f + 3.5f * Clamp01(norm);
}

// Upstream expresses fade in samples at 48 kHz (480..12000 = 10..250 ms). We
// expose ms, which is readable and sample-rate independent.
float FadeMsToSamples(float ms, float sample_rate) {
    return ms * 0.001f * sample_rate;
}

float PitchSemisFromNorm(float norm)  { return -12.0f + Clamp01(norm) * 24.0f; }
float PitchNormFromSemis(float semis) { return Clamp01((semis + 12.0f) / 24.0f); }

float GrainKeyframesFromNorm(float norm)   { return 32.0f + Clamp01(norm) * (4096.0f - 32.0f); }
float GrainNormFromKeyframes(float frames) { return Clamp01((frames - 32.0f) / (4096.0f - 32.0f)); }

ModRouter::ModRouter() : envIn_(0.0f), envOut_(0.0f) {
    for (int i = 0; i < PARAM_COUNT; i++) {
        depth_[i]  = 0.0f;
        source_[i] = SRC_OUTPUT_ENV;   // upstream factory default
    }
}

void ModRouter::SetDepth(int param, float depth) {
    if (param < 0 || param >= PARAM_COUNT) return;
    if (depth < -1.0f) depth = -1.0f;
    if (depth >  1.0f) depth =  1.0f;
    depth_[param] = depth;
}

void ModRouter::SetSource(int param, int source) {
    if (param < 0 || param >= PARAM_COUNT) return;
    if (source < 0 || source >= SRC_COUNT) return;
    source_[param] = source;
}

float ModRouter::Depth(int param) const {
    return (param < 0 || param >= PARAM_COUNT) ? 0.0f : depth_[param];
}

int ModRouter::Source(int param) const {
    return (param < 0 || param >= PARAM_COUNT) ? SRC_OUTPUT_ENV : source_[param];
}

void ModRouter::SetEnvelopes(float in_env, float out_env) {
    envIn_  = Clamp01(in_env);
    envOut_ = Clamp01(out_env);
}

float ModRouter::Apply(int param, float norm) const {
    if (param < 0 || param >= PARAM_COUNT) return norm;
    const float d = depth_[param];
    if (d == 0.0f) return norm;                    // exact bypass
    const float src = (source_[param] == SRC_INPUT_ENV) ? envIn_ : envOut_;
    return Clamp01(norm + src * d);
}

} // namespace capicola_schwung
