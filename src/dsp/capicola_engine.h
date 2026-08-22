#pragma once
// Port of upstream src/audio/audio_engine.cpp for Schwung.
//
// Differences from upstream, all forced by the target:
//   - per-instance, not DSY_SDRAM_BSS globals (Schwung runs up to 12 instances)
//   - 44100 Hz, not 48000
//   - ring 2^18 keyframes/channel (3.1 MB), not 2^21 (25 MB)
//   - no CpuLoadMeter
//
// Everything else — the feedback path, the stereo drift guardrail, the
// dry/wet blend, the output follower — is upstream's, kept deliberately close
// so their changes stay readable against ours.

#include <cstddef>
#include "Detector.h"
#include "filter.h"
#include "Shapers.h"

namespace capicola_schwung {

// 2^18 keyframes x 12 B = 3.1 MB per channel, 6.3 MB per instance, ~12 s of
// audio at the paper's M/N ~ 0.5 for musical material. Upstream's 2^21 is sized
// for a 90 s Eurorack freeze; at 12 concurrent Schwung instances that would be
// 600 MB. Must stay a power of two — SparseLine indexes by (& (bufsz-1)).
static constexpr int kRingFrames = 262144;

static constexpr float kSampleRate       = 44100.0f;
static constexpr float kFadeSamples      = 882.0f;    // 20 ms @ 44.1k
static constexpr double kMaxDriftSamples = 44100.0;   // 1 s @ 44.1k
static constexpr float kFbFilterFc       = 0.02f;     // ~480 Hz
static constexpr float kFbSatGain        = 0.5f;      // cancels sinc's 2x gain
static constexpr float kTkeoFc           = 0.0014f;
static constexpr float kEnvGain          = 200.0f;
static constexpr std::size_t kMaxBlock   = 256;       // Move renders 128

class Engine {
public:
    Engine();
    ~Engine();

    // Allocates the two keyframe rings. Returns false if allocation failed —
    // the caller must not render on a failed instance.
    bool Init();

    void ProcessBlock(const float* in_l, const float* in_r,
                      float* out_l, float* out_r, std::size_t n);

    // Engineering units, exactly as upstream's setters take them.
    void SetSlicePitch(float ratio);        // playback ratio, not semitones
    void SetSliceStretch(float stretch);    // 1.0 realtime .. 0.0 frozen
    void SetTransientThreshold(float ratio);
    void SetGrainSize(float keyframes);
    void SetQuality(float epsilon);
    void SetFeedback(float amt);
    void SetFeedbackCutoff(float fc);
    void SetTkeoCutoff(float fc);
    void SetFade(float samples);
    void SetDrive(float d);
    void SetDriveCharacter(float c);
    void SetMix(float wet01);

    void TriggerSlice();

    // Follower state — the modulation matrix reads these each block.
    float EnvNormIn()  const { return envNormIn; }
    float EnvNormOut() const { return envNormOut; }
    bool  InGate()     const { return inGate; }
    bool  OutGate()    const { return outGate; }
    unsigned TransientCount() const;

private:
    struct Impl;
    Impl* impl;          // hides the templated recorders from the header

    capicola::StateVariable fbSvfL;
    capicola::StateVariable fbSvfR;
    capicola::Detector      outDet;
    capicola::Shapers       shapers;

    float mix;
    float fbAmt;
    float fbL[kMaxBlock];
    float fbR[kMaxBlock];

    bool  inGate;
    bool  outGate;
    float envNormIn;
    float envNormOut;
};

} // namespace capicola_schwung
