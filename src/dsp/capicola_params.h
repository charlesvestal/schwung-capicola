#pragma once
// Pure parameter mapping for the Schwung capicola port.
//
// Every taper from upstream main.cpp lives here, isolated from audio so it can
// be tested without a device. Nothing in this file allocates, blocks, or does
// I/O — it is safe to call from render_block.
//
// CRITICAL: modulation is applied in NORMALIZED space, BEFORE the taper.
// Upstream does `norm + source * depth` then maps through the range. Applying
// it after the taper would make a fixed depth mean a constant engineering
// delta at both ends of a (1-x)^2.5 sweep, which is wrong and inaudible as a
// bug. See test_params.cpp.

namespace capicola_schwung {

// The six modulatable primary params, in knob order.
enum ParamId {
    PARAM_PITCH = 0,
    PARAM_STRETCH,
    PARAM_THRESHOLD,
    PARAM_GRAIN,
    PARAM_QUALITY,
    PARAM_FEEDBACK,
    PARAM_COUNT
};

// Modulation sources. Upstream's third source is the CV IN jack; Move has none
// and needs none, since the chain host's LFOs reach these params from outside.
enum ModSource {
    SRC_INPUT_ENV = 0,
    SRC_OUTPUT_ENV,
    SRC_COUNT
};

// -- Primary curves: norm 0..1 -> engineering --------------------------------
float StretchCurve(float norm);      // -> 1.0 (realtime) .. 0.0 (frozen)
float ThresholdCurve(float norm);    // -> keep ratio 0..8; >0.99 -> 1.0e9 (mute)
float QualityCurve(float norm);      // -> analyzer epsilon 0.1 .. 0.001
float PitchCurve(float semitones);   // semitones -> playback ratio, with detent

// -- Secondary curves ---------------------------------------------------------
float SmoothingCurve(float norm);    // -> follower fc 5e-5 .. 0.125 (exp)
float FbToneCurve(float norm);       // -> feedback bandpass fc 2e-3 .. 0.9 (exp)
float DriveCurve(float norm);        // -> shaper drive 0.5 .. 4.0 (linear)
float FadeMsToSamples(float ms, float sample_rate);

// -- Engineering-unit params: norm <-> unit (both linear) --------------------
// These two are exposed to the user in real units but stored as norms, because
// the matrix modulates in norm space.
float PitchSemisFromNorm(float norm);        // -> -12 .. +12
float PitchNormFromSemis(float semis);
float GrainKeyframesFromNorm(float norm);    // -> 32 .. 4096
float GrainNormFromKeyframes(float keyframes);

float Clamp01(float v);

// -- Modulation matrix --------------------------------------------------------
// One bipolar depth and one source per primary param. Default source is the
// OUTPUT follower, matching upstream: depth alone closes a loop through the
// module's own output envelope, which is what makes it self-modulating out of
// the box.
class ModRouter {
public:
    ModRouter();

    void SetDepth(int param, float depth);      // -1 .. +1
    void SetSource(int param, int source);
    float Depth(int param) const;
    int   Source(int param) const;

    void SetEnvelopes(float in_env, float out_env);   // both 0..1

    // norm + source*depth, clamped to 0..1. Call BEFORE the taper.
    float Apply(int param, float norm) const;

private:
    float depth_[PARAM_COUNT];
    int   source_[PARAM_COUNT];
    float envIn_;
    float envOut_;
};

} // namespace capicola_schwung
