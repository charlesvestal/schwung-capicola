#include "capicola_engine.h"
#include "KeyframeRecorder.h"
#include <cmath>
#include <new>

namespace capicola_schwung {

using namespace capicola;

// The recorders are templated on ring size and enormous; keeping them behind a
// pimpl stops every translation unit that includes the header from
// instantiating them.
struct Engine::Impl {
    KeyframeRecorder<kRingFrames> l;
    KeyframeRecorder<kRingFrames> r;
};

static inline float EnvNorm(float env) {
    float e = env * kEnvGain;
    if (e < 0.0f) e = 0.0f; else if (e > 1.0f) e = 1.0f;
    return std::sqrt(e);
}

static inline float Clamp01f(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

Engine::Engine()
    : impl(nullptr), mix(1.0f), fbAmt(0.0f),
      inGate(false), outGate(false), envNormIn(0.0f), envNormOut(0.0f),
      sliceCount(0) {
    for (std::size_t i = 0; i < kMaxBlock; i++) { fbL[i] = 0.0f; fbR[i] = 0.0f; }
}

Engine::~Engine() { delete impl; }

bool Engine::Init() {
    // ~6.3 MB. This runs on the SPI callback (create_instance is the SPI
    // callback — see plugin_api_v1.h). It is a one-shot allocation at module
    // load, on the same thread that already dlopen()s us, and it is documented
    // in README.md rather than hidden. Nothing else here allocates.
    //
    // Default-initialize (no parens): value-initializing `Impl()` zero-fills
    // the whole 6.3 MB block, which KeyframeRecorder::Init immediately
    // overwrites member-by-member (sparse.Init() -> Clear() ->
    // buffer.fill(...), plus every scalar control field set explicitly below)
    // — the zero-fill upstream deliberately made unnecessary by keeping
    // KeyframeRecorder trivially constructible (see its header comment).
    impl = new (std::nothrow) Impl;
    if (!impl) return false;

    shapers.Init();

    impl->l.Init(&shapers, kSampleRate);
    impl->r.Init(&shapers, kSampleRate);

    for (KeyframeRecorder<kRingFrames>* rec : {&impl->l, &impl->r}) {
        rec->SetThreshold(0.001f);
        rec->SetGrainPitch(1.0f);
        rec->SetGrainLeash(128);
        rec->SetGrainFade(kFadeSamples);
    }

    fbSvfL.Init();
    fbSvfR.Init();
    fbSvfL.SetControls(kFbFilterFc, 0.01f);
    fbSvfR.SetControls(kFbFilterFc, 0.01f);

    outDet.Init(kSampleRate);
    SetTkeoCutoff(kTkeoFc);

    impl->l.SubmitRequest(Request::LIVE_EFFECT);
    impl->r.SubmitRequest(Request::LIVE_EFFECT);
    return true;
}

void Engine::ProcessBlock(const float* in_l, const float* in_r,
                          float* out_l, float* out_r, std::size_t n) {
    if (!impl) {
        for (std::size_t i = 0; i < n; i++) { out_l[i] = in_l[i]; out_r[i] = in_r[i]; }
        return;
    }
    const std::size_t m = (n < kMaxBlock) ? n : kMaxBlock;

    // Feedback: last block's wet -> sinc saturator -> SVF bandpass -> input.
    // Hard +/-1 clamp on the injection is the runaway guard; the knob is
    // allowed above 1.0 on purpose.
    const float* src_l = in_l;
    const float* src_r = in_r;
    float fin_l[kMaxBlock], fin_r[kMaxBlock];
    if (fbAmt > 0.0f) {
        for (std::size_t i = 0; i < m; i++) {
            fbSvfL.Tick(kFbSatGain * shapers.ReadSinc(fbL[i]));
            fbSvfR.Tick(kFbSatGain * shapers.ReadSinc(fbR[i]));
            float inj_l = fbSvfL.GetBandpass() * fbAmt;
            float inj_r = fbSvfR.GetBandpass() * fbAmt;
            if (inj_l >  1.0f) inj_l =  1.0f; else if (inj_l < -1.0f) inj_l = -1.0f;
            if (inj_r >  1.0f) inj_r =  1.0f; else if (inj_r < -1.0f) inj_r = -1.0f;
            fin_l[i] = in_l[i] + inj_l;
            fin_r[i] = in_r[i] + inj_r;
        }
        src_l = fin_l;
        src_r = fin_r;
    }

    impl->l.ProcessBlock(src_l, out_l, m);
    impl->r.ProcessBlock(src_r, out_r, m);

    // Stereo re-alignment guardrail: only on the block where exactly one
    // channel auto-fired. Past the guardrail, force the quiet channel to slice
    // so it re-anchors to the same delayed tap.
    const bool fired_l = impl->l.FiredThisBlock();
    const bool fired_r = impl->r.FiredThisBlock();
    if (fired_l != fired_r) {
        const double lag_l = impl->l.GridLag();
        const double lag_r = impl->r.GridLag();
        const double drift = (lag_l > lag_r) ? (lag_l - lag_r) : (lag_r - lag_l);
        if (drift > kMaxDriftSamples)
            (fired_l ? impl->r : impl->l).SubmitRequest(Request::SLICE);
    }

    // Stash the wet output for next block's feedback, BEFORE the blend — the
    // tap sits pre-mix so turning mix down doesn't starve the loop.
    for (std::size_t i = 0; i < m; i++) { fbL[i] = out_l[i]; fbR[i] = out_r[i]; }

    const float wet = mix;
    const float dry = 1.0f - wet;
    for (std::size_t i = 0; i < m; i++) {
        out_l[i] = wet * out_l[i] + dry * in_l[i];
        out_r[i] = wet * out_r[i] + dry * in_r[i];
    }

    // Output follower on the post-mix mono sum, fed at 2x (no halving): TKEO is
    // quadratic, so this lifts the envelope 4x to sit level with the per-channel
    // input followers.
    for (std::size_t i = 0; i < m; i++) outDet.Analyze(out_l[i] + out_r[i]);

    const float env_l = impl->l.TkeoEnvelope();
    const float env_r = impl->r.TkeoEnvelope();
    envNormIn  = EnvNorm((env_l > env_r) ? env_l : env_r);
    envNormOut = EnvNorm(outDet.Envelope());
    inGate     = impl->l.DetectorGate() || impl->r.DetectorGate();
    outGate    = outDet.Gate();
}

void Engine::SetSlicePitch(float ratio) {
    if (!impl) return;
    impl->l.SetGrainPitch(ratio);
    impl->r.SetGrainPitch(ratio);
}
void Engine::SetSliceStretch(float s) {
    if (!impl) return;
    impl->l.SetGrainStretch(s);
    impl->r.SetGrainStretch(s);
}
void Engine::SetTransientThreshold(float t) {
    if (!impl) return;
    impl->l.SetTransientThreshold(t);
    impl->r.SetTransientThreshold(t);
    outDet.SetThreshold(t);
}
void Engine::SetGrainSize(float keyframes) {
    if (!impl) return;
    const int d = (int)(keyframes + 0.5f);
    impl->l.SetGrainLeash(d);
    impl->r.SetGrainLeash(d);
}
void Engine::SetQuality(float eps) {
    if (!impl) return;
    impl->l.SetThreshold(eps);
    impl->r.SetThreshold(eps);
}
void Engine::SetFeedback(float amt) { fbAmt = (amt < 0.0f) ? 0.0f : amt; }
void Engine::SetFeedbackCutoff(float fc) {
    fbSvfL.SetControls(fc, 0.01f);
    fbSvfR.SetControls(fc, 0.01f);
}
void Engine::SetTkeoCutoff(float fc) {
    if (impl) { impl->l.SetTkeoCutoff(fc); impl->r.SetTkeoCutoff(fc); }
    outDet.SetCutoff(fc);
}
void Engine::SetFade(float samples) {
    if (!impl) return;
    impl->l.SetGrainFade(samples);
    impl->r.SetGrainFade(samples);
}
void Engine::SetDrive(float d) {
    if (!impl) return;
    impl->l.SetDistortDrive(d);
    impl->r.SetDistortDrive(d);
}
void Engine::SetDriveCharacter(float c) {
    if (!impl) return;
    impl->l.SetDistortCharacter(c);
    impl->r.SetDistortCharacter(c);
}
void Engine::SetMix(float wet01) { mix = Clamp01f(wet01); }

void Engine::TriggerSlice() {
    if (!impl) return;
    sliceCount++;
    if (impl->l.GetState() == State::LIVE_EFFECT) {
        impl->l.SubmitRequest(Request::SLICE);
        impl->r.SubmitRequest(Request::SLICE);
    }
}

unsigned Engine::TransientCount() const {
    return impl ? (unsigned)impl->l.TransientCount() : 0u;
}

} // namespace capicola_schwung
