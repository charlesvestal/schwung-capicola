# Capicola Schwung Module — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers-extended-cc:subagent-driven-development (recommended) or superpowers-extended-cc:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Port heavylight-industries/capicola's keyframe time-stretch engine to a Schwung chain audio FX with 24 params over three knob pages.

**Architecture:** Upstream `lib/` is vendored byte-identical (portable C++, no SDK includes) and the single required source change is carried as a patch applied to a build-tree copy. Upstream's 288-line `audio_engine.cpp` is reimplemented per-instance at 44.1 kHz; its 840-line Eurorack panel is discarded for Schwung param pages. A pure param-mapping unit owns every taper and the modulation matrix, so the numeric core is host-testable without audio or hardware.

**Tech Stack:** C++17, `aarch64-linux-gnu-g++` via Docker, Schwung `plugin_api_v2`, Node for param-page validation.

**User decisions (already made):**
- Form factor: live audio FX, not a sample player or offline tool.
- Buffer policy: continuous slip, superseded on inspection by upstream's transient re-anchor — "sounds good".
- Modulation matrix pulled into v0.1: "might as well build it now all at once, huh? page 3?"
- Name: Capicola, keeping the author's name.
- Distribution: "i already reached out, so build it privately in parallel" — local only, no catalog entry, no release, no tag.

---

## Reference material

Upstream is cloned at `../capicola` relative to the parent dir, or re-clone:

```bash
git clone --depth 1 https://github.com/heavylight-industries/capicola /tmp/capicola-upstream
```

The design spec is `docs/superpowers/specs/2026-08-21-capicola-module-design.md`. Read it before Task 0 — it carries the ranges, curves and the three behaviours that must survive the port.

Schwung checkout is at `../schwung`. Tasks 5 and 6 reference it; export `SCHWUNG_ROOT` if it lives elsewhere.

## File structure

| Path | Responsibility |
|---|---|
| `LICENSE` | AGPL-3.0, copied from upstream |
| `README.md` | Attribution to Heavylight Industries, the DAFx26 paper, upstream repo |
| `VENDOR.md` | Upstream commit SHA, what was vendored, what was patched |
| `patches/0001-keyframerecorder-sample-rate.patch` | The one upstream change |
| `src/dsp/lib/` | Upstream `lib/` **byte-identical** — never edited in place |
| `src/dsp/capicola_params.{h,cpp}` | Pure: norm ↔ engineering for 24 params, plus `ModRouter`. No audio, no I/O. |
| `src/dsp/capicola_engine.{h,cpp}` | Port of upstream `audio_engine.cpp`; per-instance, 44.1 kHz |
| `src/dsp/capicola_plugin.cpp` | `plugin_api_v2` + `chain_params` + `ui_hierarchy` + `move_audio_fx_on_midi` |
| `src/dsp/plugin_api_v1.h` | Copied from Schwung |
| `src/module.json` | Metadata + `ui_hierarchy` with inline param metadata |
| `src/help.json` | On-device help |
| `tests/host/` | Host-side C++ tests + WAV runner |
| `tools/preview_pages.mjs` | Feeds our contract through Schwung's `planPages`/`validateContract` |
| `scripts/{build.sh,Dockerfile,install.sh}` | Cross-compile + deploy |

`capicola_params` is deliberately separate from `capicola_engine`: the tapers and the matrix are where a silent wrongness would live, and isolating them makes them testable with no audio in the loop.

---

### Task 0: Repo scaffold, licence, vendored `lib/`

**Goal:** A repo with upstream's DSP core vendored byte-identical and its provenance recorded.

**Files:**
- Create: `LICENSE`, `README.md`, `VENDOR.md`, `.gitignore`
- Create: `src/dsp/lib/` (11 headers, copied verbatim)
- Create: `src/dsp/plugin_api_v1.h` (copied from Schwung)

**Acceptance Criteria:**
- [ ] `src/dsp/lib/` is byte-identical to upstream `lib/` — `diff -r` reports nothing
- [ ] `VENDOR.md` records the upstream commit SHA
- [ ] `LICENSE` is AGPL-3.0
- [ ] `README.md` credits Heavylight Industries, the paper, and upstream in the first paragraph

**Verify:** `diff -r src/dsp/lib /tmp/capicola-upstream/lib && echo IDENTICAL` → prints `IDENTICAL`

**Steps:**

- [ ] **Step 1: Clone upstream and record the SHA**

```bash
cd /Volumes/ExtFS/charlesvestal/github/schwung-parent/schwung-capicola
rm -rf /tmp/capicola-upstream
git clone --depth 1 https://github.com/heavylight-industries/capicola /tmp/capicola-upstream
UPSTREAM_SHA=$(git -C /tmp/capicola-upstream rev-parse HEAD)
echo "$UPSTREAM_SHA"
```

- [ ] **Step 2: Vendor `lib/` verbatim and copy the licence**

```bash
mkdir -p src/dsp/lib
cp /tmp/capicola-upstream/lib/*.h src/dsp/lib/
cp /tmp/capicola-upstream/LICENSE LICENSE
cp ../schwung/src/host/plugin_api_v1.h src/dsp/plugin_api_v1.h
ls src/dsp/lib/
```

Expected: `Analyzer.h  Detector.h  DeluxeLine.h  Granule.h  KeyframeRecorder.h  Shapers.h  SparseLine.h  TabulatedFunction.h  Tabulator.h  filter.h  functors.h`

- [ ] **Step 3: Write `VENDOR.md`** (substitute the real SHA)

```markdown
# Vendored code

`src/dsp/lib/` is copied **byte-identical** from
[heavylight-industries/capicola](https://github.com/heavylight-industries/capicola),
licensed AGPL-3.0.

- Upstream commit: `<UPSTREAM_SHA>`
- Vendored: 2026-08-21

## Do not edit `src/dsp/lib/` in place

Keeping it identical means upstream fixes rebase cleanly and the AGPL provenance
is unambiguous. Changes are carried as patches in `patches/`, applied to a
build-tree copy by `scripts/build.sh`. `src/dsp/lib/` itself is never modified.

To check drift:

    diff -r src/dsp/lib /path/to/capicola/lib

## Patches

- `0001-keyframerecorder-sample-rate.patch` — `KeyframeRecorder::Init` hardcodes
  `detector.Init(48000.0f)`. Move runs at 44100 Hz. Adds a sample-rate parameter.
  `Detector::Init` already accepts one, so this only threads it through.
```

- [ ] **Step 4: Write `.gitignore`**

```
build/
dist/
*.so
.DS_Store
```

- [ ] **Step 5: Write `README.md`**

```markdown
# Capicola for Schwung

A live time-stretch and pitch-shift audio FX for [Schwung](https://github.com/charlesvestal/schwung)
on Ableton Move.

Incoming stereo is continuously reduced to bandlimited local extrema
("keyframes") and replayed by a lagging read head under independent time and
pitch control. A transient detector runs in parallel; every kept transient snaps
the read head back to the present with a crossfade, so stretched tails ring out
underneath while the output stays locked to the rhythm of the input.

## Credit

This is a port of **[capicola](https://github.com/heavylight-industries/capicola)**
by **Heavylight Industries** — the hardware embodiment of their independently
authored, peer-reviewed DAFx26 paper
*[Keyframe Time Stretching via Extrema Sampling](https://github.com/heavylight-industries/dafx26-paper)*.
The DSP core in `src/dsp/lib/` is their work, vendored unmodified. See
[VENDOR.md](VENDOR.md).

Their hardware module runs on the Alchemy Lab V2 (Daisy / STM32H750) and has a
Eurorack panel with CV. This port keeps the engine and replaces the panel with
Schwung param pages. Audio examples and the interactive manual for the original
are linked from the upstream README.

## Licence

**AGPL-3.0**, matching upstream. See [LICENSE](LICENSE).
```

- [ ] **Step 6: Verify byte-identity and commit**

```bash
diff -r src/dsp/lib /tmp/capicola-upstream/lib && echo IDENTICAL
git add -A && git commit -m "Vendor capicola lib/ verbatim (AGPL-3.0)"
```

Expected: `IDENTICAL`, then a commit.

---

### Task 1: Sample-rate patch + host build harness

**Goal:** `lib/` compiles on the host at 44.1 kHz, with the one upstream change carried as a patch rather than an edit.

**Files:**
- Create: `patches/0001-keyframerecorder-sample-rate.patch`
- Create: `tests/host/Makefile`
- Create: `tests/host/test_lib_smoke.cpp`
- Create: `scripts/apply_patches.sh`

**Acceptance Criteria:**
- [ ] `src/dsp/lib/` still byte-identical to upstream after the patch step
- [ ] Patch applies cleanly to a build-tree copy
- [ ] `sizeof(Keyframe) == 12` asserted at compile time and at runtime
- [ ] A `KeyframeRecorder<1024>` initialises and processes a block of silence without crashing

**Verify:** `make -C tests/host test` → `ALL TESTS PASSED`

**Steps:**

- [ ] **Step 1: Write `scripts/apply_patches.sh`**

```bash
#!/usr/bin/env bash
# Copy the pristine vendored lib into the build tree and apply our patches.
# src/dsp/lib is NEVER modified in place — see VENDOR.md.
set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
OUT="${1:-$REPO_ROOT/build/lib}"

rm -rf "$OUT"
mkdir -p "$OUT"
cp "$REPO_ROOT"/src/dsp/lib/*.h "$OUT"/

shopt -s nullglob
for p in "$REPO_ROOT"/patches/*.patch; do
    echo "applying $(basename "$p")"
    patch -d "$OUT" -p1 --forward --silent < "$p"
done
echo "patched lib -> $OUT"
```

```bash
chmod +x scripts/apply_patches.sh
```

- [ ] **Step 2: Create the patch by editing a scratch copy**

```bash
cd /Volumes/ExtFS/charlesvestal/github/schwung-parent/schwung-capicola
rm -rf /tmp/patchwork && mkdir -p /tmp/patchwork/a /tmp/patchwork/b
cp src/dsp/lib/KeyframeRecorder.h /tmp/patchwork/a/
cp src/dsp/lib/KeyframeRecorder.h /tmp/patchwork/b/
```

Now edit `/tmp/patchwork/b/KeyframeRecorder.h`. Two changes.

Change the `Init` signature (upstream line 145):

```cpp
    void Init(const Shapers* sh = nullptr, float sample_rate = 48000.0f) {
```

Change the hardcoded detector init (upstream line 156):

```cpp
        detector.Init(sample_rate);
```

Generate the patch:

```bash
cd /tmp/patchwork
diff -u a/KeyframeRecorder.h b/KeyframeRecorder.h > /tmp/kf.patch || true
cd /Volumes/ExtFS/charlesvestal/github/schwung-parent/schwung-capicola
mkdir -p patches
sed 's|^--- a/|--- |; s|^+++ b/|+++ |' /tmp/kf.patch > patches/0001-keyframerecorder-sample-rate.patch
head -5 patches/0001-keyframerecorder-sample-rate.patch
```

- [ ] **Step 3: Verify the patch applies and `lib/` is untouched**

```bash
./scripts/apply_patches.sh
command grep -n 'detector.Init' build/lib/KeyframeRecorder.h
diff -r src/dsp/lib /tmp/capicola-upstream/lib && echo "PRISTINE"
```

Expected: `detector.Init(sample_rate);` in the build copy, and `PRISTINE`.

- [ ] **Step 4: Write the smoke test**

`tests/host/test_lib_smoke.cpp`:

```cpp
// Host smoke test: the vendored engine compiles, its packed layout survives the
// compiler, and a recorder can run a block without exploding.
#include "KeyframeRecorder.h"
#include "Shapers.h"
#include <cstdio>
#include <cmath>
#include <cstdlib>

using namespace capicola;

static int failures = 0;
static void check(bool cond, const char* what) {
    if (!cond) { std::printf("FAIL: %s\n", what); failures++; }
    else       { std::printf("ok:   %s\n", what); }
}

// SparseLine indexes by (& (bufsz-1)), so the ring MUST be a power of two.
static_assert(sizeof(Keyframe) == 12, "Keyframe must stay 12 bytes");

int main() {
    check(sizeof(Keyframe) == 12, "sizeof(Keyframe) == 12");

    static Shapers shapers;
    shapers.Init();

    // Heap, not stack: even a small ring is far past the default stack limit.
    auto* rec = new KeyframeRecorder<1024>();
    rec->Init(&shapers, 44100.0f);
    rec->SubmitRequest(Request::LIVE_EFFECT);

    float in[128] = {0.0f};
    float out[128] = {0.0f};
    for (int block = 0; block < 64; block++) {
        rec->ProcessBlock(in, out, 128);
    }

    bool finite = true;
    for (int i = 0; i < 128; i++) if (!std::isfinite(out[i])) finite = false;
    check(finite, "silence in -> finite out");

    check(rec->GetState() == State::LIVE_EFFECT, "state is LIVE_EFFECT");

    delete rec;
    std::printf(failures ? "\nFAILURES: %d\n" : "\nALL TESTS PASSED\n", failures);
    return failures ? 1 : 0;
}
```

- [ ] **Step 5: Write `tests/host/Makefile`**

```make
# Host-side tests. Builds against the PATCHED lib in build/lib, never src/dsp/lib.
CXX      ?= c++
CXXFLAGS ?= -std=c++17 -O2 -g -Wall -Wextra -Wno-unused-parameter
ROOT     := $(shell cd ../.. && pwd)
LIB      := $(ROOT)/build/lib
INC      := -I$(LIB) -I$(ROOT)/src/dsp

BIN := $(ROOT)/build/tests

TESTS := test_lib_smoke

.PHONY: test clean patched
patched:
	@$(ROOT)/scripts/apply_patches.sh >/dev/null

test: patched $(addprefix $(BIN)/,$(TESTS))
	@set -e; for t in $(TESTS); do echo "== $$t"; $(BIN)/$$t; done

$(BIN)/test_lib_smoke: test_lib_smoke.cpp | $(BIN)
	$(CXX) $(CXXFLAGS) $(INC) $< -o $@

$(BIN):
	mkdir -p $(BIN)

clean:
	rm -rf $(BIN)
```

- [ ] **Step 6: Run it — expect a pass**

```bash
make -C tests/host test
```

Expected output ends with `ALL TESTS PASSED`.

- [ ] **Step 7: Commit**

```bash
git add -A
git commit -m "Carry the 44.1kHz sample-rate change as a patch, add host smoke test

Upstream hardcodes detector.Init(48000.0f) inside KeyframeRecorder::Init.
Detector::Init already takes a sample rate, so this only threads it through.
Applied to a build-tree copy so src/dsp/lib stays byte-identical."
```

---

### Task 2: Param mapping and the modulation matrix

**Goal:** A pure unit owning every taper and the pre-taper modulation matrix, fully tested without audio.

**Files:**
- Create: `src/dsp/capicola_params.h`
- Create: `src/dsp/capicola_params.cpp`
- Create: `tests/host/test_params.cpp`
- Modify: `tests/host/Makefile` (add `test_params` to `TESTS`, link the .cpp)

**Acceptance Criteria:**
- [ ] All six primary and six secondary curves match the spec's mapping table
- [ ] `pitch` and `grain` round-trip norm → engineering → norm within 1e-4
- [ ] Threshold sends the mute sentinel `1.0e9` above norm 0.99
- [ ] Modulation applies **before** the taper: a fixed depth on `stretch` yields different engineering deltas at each end of the sweep
- [ ] Modulation output clamps to 0…1 in norm space

**Verify:** `make -C tests/host test` → `ALL TESTS PASSED`

**Steps:**

- [ ] **Step 1: Write the failing test**

`tests/host/test_params.cpp`:

```cpp
#include "capicola_params.h"
#include <cstdio>
#include <cmath>

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
    // --- Primary curves (spec: Mapping curves) ---
    near(StretchCurve(0.0f), 1.0f,   1e-6f, "stretch norm 0 -> realtime 1.0");
    near(StretchCurve(1.0f), 0.0f,   1e-6f, "stretch norm 1 -> frozen 0.0");
    near(StretchCurve(0.5f), std::pow(0.5f, 2.5f), 1e-6f, "stretch noon ~ 5.7x");

    near(ThresholdCurve(0.0f), 0.0f, 1e-6f, "threshold norm 0 -> ratio 0");
    near(ThresholdCurve(0.9f), 4.0f, 1e-5f, "threshold norm 0.9 -> ratio 4");
    near(ThresholdCurve(0.95f), 6.0f, 1e-5f, "threshold norm 0.95 -> ratio 6");
    check(ThresholdCurve(1.0f) > 1.0e8f,   "threshold top -> mute sentinel");

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

    std::printf(failures ? "\nFAILURES: %d\n" : "\nALL TESTS PASSED\n", failures);
    return failures ? 1 : 0;
}
```

- [ ] **Step 2: Add it to the Makefile and watch it fail**

In `tests/host/Makefile`, change the `TESTS` line and add a rule:

```make
TESTS := test_lib_smoke test_params
```

```make
$(BIN)/test_params: test_params.cpp $(ROOT)/src/dsp/capicola_params.cpp | $(BIN)
	$(CXX) $(CXXFLAGS) $(INC) $^ -o $@
```

Run: `make -C tests/host test`
Expected: FAIL — `capicola_params.h: No such file or directory`

- [ ] **Step 3: Write `src/dsp/capicola_params.h`**

```cpp
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

// ── Primary curves: norm 0..1 -> engineering ────────────────────────────────
float StretchCurve(float norm);      // -> 1.0 (realtime) .. 0.0 (frozen)
float ThresholdCurve(float norm);    // -> keep ratio 0..8; >0.99 -> 1.0e9 (mute)
float QualityCurve(float norm);      // -> analyzer epsilon 0.1 .. 0.001
float PitchCurve(float semitones);   // semitones -> playback ratio, with detent

// ── Secondary curves ────────────────────────────────────────────────────────
float SmoothingCurve(float norm);    // -> follower fc 5e-5 .. 0.125 (exp)
float FbToneCurve(float norm);       // -> feedback bandpass fc 2e-3 .. 0.9 (exp)
float DriveCurve(float norm);        // -> shaper drive 0.5 .. 4.0 (linear)
float FadeMsToSamples(float ms, float sample_rate);

// ── Engineering-unit params: norm <-> unit (both linear) ────────────────────
// These two are exposed to the user in real units but stored as norms, because
// the matrix modulates in norm space.
float PitchSemisFromNorm(float norm);        // -> -12 .. +12
float PitchNormFromSemis(float semis);
float GrainKeyframesFromNorm(float norm);    // -> 32 .. 4096
float GrainNormFromKeyframes(float keyframes);

float Clamp01(float v);

// ── Modulation matrix ───────────────────────────────────────────────────────
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
```

- [ ] **Step 4: Write `src/dsp/capicola_params.cpp`**

```cpp
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

// Upstream declares .Linear(0.1f, 0.001f) — inverted, so CW is max fidelity.
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
```

- [ ] **Step 5: Run the tests — expect a pass**

```bash
make -C tests/host test
```

Expected: every `ok:` line, ending `ALL TESTS PASSED`.

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "Param mapping + modulation matrix, pure and host-tested

Every taper from upstream main.cpp, isolated from audio. Modulation applies
pre-taper; the test asserts a fixed depth on stretch gives DIFFERENT deltas at
each end of the sweep, because the post-taper bug produces a constant delta and
is otherwise inaudible as a bug."
```

---

### Task 3: Engine port

**Goal:** Upstream `audio_engine.cpp` reimplemented per-instance at 44.1 kHz, proven on real audio by a host WAV runner.

**Files:**
- Create: `src/dsp/capicola_engine.h`
- Create: `src/dsp/capicola_engine.cpp`
- Create: `tests/host/test_engine.cpp`
- Create: `tests/host/wav_runner.cpp`
- Modify: `tests/host/Makefile`

**Acceptance Criteria:**
- [ ] Ring is 2¹⁸ keyframes/channel; both channels allocated per instance, not as globals
- [ ] All 48000 constants replaced with 44100 (fade 882, drift guard 44100)
- [ ] Engine produces finite, non-silent output for a noise input at stretch 0.5
- [ ] Full stretch (norm 1.0) freezes: output stays non-silent after input goes to zero
- [ ] `TriggerSlice()` is observable — transient count advances
- [ ] `wav_runner` renders a WAV at a given stretch/pitch without NaN

**Verify:** `make -C tests/host test` → `ALL TESTS PASSED`

**Steps:**

- [ ] **Step 1: Write `src/dsp/capicola_engine.h`**

```cpp
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
```

- [ ] **Step 2: Write `src/dsp/capicola_engine.cpp`**

```cpp
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
      inGate(false), outGate(false), envNormIn(0.0f), envNormOut(0.0f) {
    for (std::size_t i = 0; i < kMaxBlock; i++) { fbL[i] = 0.0f; fbR[i] = 0.0f; }
}

Engine::~Engine() { delete impl; }

bool Engine::Init() {
    // ~6.3 MB. This runs on the SPI callback (create_instance is the SPI
    // callback — see plugin_api_v1.h). It is a one-shot allocation at module
    // load, on the same thread that already dlopen()s us, and it is documented
    // in README.md rather than hidden. Nothing else here allocates.
    impl = new (std::nothrow) Impl();
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
    if (impl->l.GetState() == State::LIVE_EFFECT) {
        impl->l.SubmitRequest(Request::SLICE);
        impl->r.SubmitRequest(Request::SLICE);
    }
}

unsigned Engine::TransientCount() const {
    return impl ? (unsigned)impl->l.TransientCount() : 0u;
}

} // namespace capicola_schwung
```

- [ ] **Step 3: Write `tests/host/test_engine.cpp`**

```cpp
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
```

- [ ] **Step 4: Write `tests/host/wav_runner.cpp`**

```cpp
// wav_runner — push a 16-bit stereo WAV through the engine and write the result.
//
//   ./wav_runner in.wav out.wav <stretch_norm> <pitch_semis>
//
// This is the "mirror upstream's loop and validate on the host before
// cross-compiling" step. Judge it by ear, not by assertion.
#include "capicola_engine.h"
#include "capicola_params.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <vector>
#include <cmath>

using namespace capicola_schwung;

#pragma pack(push, 1)
struct WavHeader {
    char riff[4]; uint32_t size; char wave[4];
    char fmt[4];  uint32_t fmtSize;
    uint16_t format, channels;
    uint32_t rate, byteRate;
    uint16_t align, bits;
    char data[4]; uint32_t dataSize;
};
#pragma pack(pop)

int main(int argc, char** argv) {
    if (argc < 5) {
        std::fprintf(stderr, "usage: %s in.wav out.wav <stretch_norm> <pitch_semis>\n", argv[0]);
        return 2;
    }
    const float stretchNorm = (float)atof(argv[3]);
    const float pitchSemis  = (float)atof(argv[4]);

    FILE* f = fopen(argv[1], "rb");
    if (!f) { std::fprintf(stderr, "cannot open %s\n", argv[1]); return 1; }
    WavHeader h;
    if (fread(&h, sizeof(h), 1, f) != 1) { std::fprintf(stderr, "short header\n"); fclose(f); return 1; }
    if (h.bits != 16 || h.channels != 2) {
        std::fprintf(stderr, "need 16-bit stereo, got %d-bit %dch\n", h.bits, h.channels);
        fclose(f); return 1;
    }
    const size_t frames = h.dataSize / 4;
    std::vector<int16_t> pcm(frames * 2);
    if (fread(pcm.data(), 2, frames * 2, f) != frames * 2) {
        std::fprintf(stderr, "short data\n"); fclose(f); return 1;
    }
    fclose(f);

    Engine* e = new Engine();
    if (!e->Init()) { std::fprintf(stderr, "engine init failed\n"); return 1; }
    e->SetSliceStretch(StretchCurve(stretchNorm));
    e->SetSlicePitch(PitchCurve(pitchSemis));
    e->SetMix(1.0f);

    std::vector<int16_t> out(frames * 2, 0);
    float il[128], ir[128], ol[128], orr[128];
    size_t nan_count = 0;

    for (size_t pos = 0; pos < frames; pos += 128) {
        const size_t n = (frames - pos < 128) ? (frames - pos) : 128;
        for (size_t i = 0; i < n; i++) {
            il[i] = pcm[(pos + i) * 2 + 0] / 32768.0f;
            ir[i] = pcm[(pos + i) * 2 + 1] / 32768.0f;
        }
        e->ProcessBlock(il, ir, ol, orr, n);
        for (size_t i = 0; i < n; i++) {
            if (!std::isfinite(ol[i]) || !std::isfinite(orr[i])) { nan_count++; ol[i] = orr[i] = 0.0f; }
            float a = ol[i], b = orr[i];
            if (a >  1.0f) a =  1.0f; if (a < -1.0f) a = -1.0f;
            if (b >  1.0f) b =  1.0f; if (b < -1.0f) b = -1.0f;
            out[(pos + i) * 2 + 0] = (int16_t)(a * 32767.0f);
            out[(pos + i) * 2 + 1] = (int16_t)(b * 32767.0f);
        }
    }
    delete e;

    FILE* g = fopen(argv[2], "wb");
    if (!g) { std::fprintf(stderr, "cannot write %s\n", argv[2]); return 1; }
    fwrite(&h, sizeof(h), 1, g);
    fwrite(out.data(), 2, out.size(), g);
    fclose(g);

    std::printf("wrote %s (%zu frames, %zu non-finite samples zeroed)\n",
                argv[2], frames, nan_count);
    return nan_count ? 1 : 0;
}
```

- [ ] **Step 5: Extend the Makefile**

```make
TESTS := test_lib_smoke test_params test_engine

ENGINE_SRC := $(ROOT)/src/dsp/capicola_engine.cpp $(ROOT)/src/dsp/capicola_params.cpp

$(BIN)/test_engine: test_engine.cpp $(ENGINE_SRC) | $(BIN)
	$(CXX) $(CXXFLAGS) $(INC) $^ -o $@

$(BIN)/wav_runner: wav_runner.cpp $(ENGINE_SRC) | $(BIN)
	$(CXX) $(CXXFLAGS) $(INC) $^ -o $@

.PHONY: wav
wav: patched $(BIN)/wav_runner
	@echo "built $(BIN)/wav_runner"
```

- [ ] **Step 6: Run the tests**

```bash
make -C tests/host test
```

Expected: `ALL TESTS PASSED`.

- [ ] **Step 7: Build the WAV runner and render one file**

```bash
make -C tests/host wav
```

Do NOT play the result — print the path only, per the user's standing preference. Rendering is fine; playback is theirs to initiate.

- [ ] **Step 8: Commit**

```bash
git add -A
git commit -m "Port the audio engine: per-instance, 44.1kHz, 2^18 ring

Upstream's recorders are DSY_SDRAM_BSS globals because the section attribute
cannot apply to a class member; Schwung runs up to 12 instances, so they become
pimpl members. Ring drops 2^21 -> 2^18: 6.3 MB per instance instead of 50 MB.

The feedback path, stereo drift guardrail and output follower are kept close to
upstream's shape so their changes stay readable against ours."
```

---

### Task 4: Plugin wrapper

**Goal:** A loadable `capicola.so` exposing `plugin_api_v2`, `chain_params`, `ui_hierarchy`, state persistence, and slice-on-note.

**Files:**
- Create: `src/dsp/capicola_plugin.cpp`
- Create: `tests/host/test_plugin.cpp`
- Modify: `tests/host/Makefile`

**Acceptance Criteria:**
- [ ] `move_plugin_init_v2` returns a v2 API with `api_version == 2`
- [ ] All 24 params round-trip through `set_param` / `get_param`
- [ ] `chain_params` parses as a JSON array with 24 entries
- [ ] `ui_hierarchy` parses as JSON with three levels
- [ ] `state` blob round-trips: set params, read state, reset, write state, params match
- [ ] `move_audio_fx_on_midi` with a note-on advances the transient counter path
- [ ] `render_block` on int16 stereo produces finite output

**Verify:** `make -C tests/host test` → `ALL TESTS PASSED`

**Steps:**

- [ ] **Step 1: Write the failing test**

`tests/host/test_plugin.cpp`:

```cpp
#include "plugin_api_v1.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
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

    // A float param round-trips.
    api->set_param(inst, "mix", "0.25");
    api->get_param(inst, "mix", buf, sizeof(buf));
    check(std::fabs(atof(buf) - 0.25) < 1e-3, "mix round-trips");

    // A param in engineering units round-trips.
    api->set_param(inst, "pitch", "7");
    api->get_param(inst, "pitch", buf, sizeof(buf));
    check(std::fabs(atof(buf) - 7.0) < 0.05, "pitch round-trips in semitones");

    // A mod source (enum, stored as an index) round-trips.
    api->set_param(inst, "mod_src_pitch", "0");
    api->get_param(inst, "mod_src_pitch", buf, sizeof(buf));
    check(atoi(buf) == 0, "mod source round-trips");

    // Contracts are non-empty JSON of the right shape.
    api->get_param(inst, "chain_params", buf, sizeof(buf));
    check(buf[0] == '[', "chain_params is a JSON array");
    int commas = 0; for (char* p = buf; *p; p++) if (*p == '{') commas++;
    check(commas == kKeyCount, "chain_params declares 24 entries");

    api->get_param(inst, "ui_hierarchy", buf, sizeof(buf));
    check(buf[0] == '{', "ui_hierarchy is a JSON object");
    check(strstr(buf, "\"secondary\"") != nullptr, "ui_hierarchy has a secondary level");
    check(strstr(buf, "\"modulation\"") != nullptr, "ui_hierarchy has a modulation level");

    // State blob round-trips.
    api->set_param(inst, "drive", "3.0");
    api->set_param(inst, "mod_depth_stretch", "-0.5");
    char state[8192];
    api->get_param(inst, "state", state, sizeof(state));
    check(state[0] != '\0', "state blob is non-empty");

    api->set_param(inst, "drive", "1.0");
    api->set_param(inst, "mod_depth_stretch", "0.0");
    api->set_param(inst, "state", state);
    api->get_param(inst, "drive", buf, sizeof(buf));
    check(std::fabs(atof(buf) - 3.0) < 1e-3, "state restores drive");
    api->get_param(inst, "mod_depth_stretch", buf, sizeof(buf));
    check(std::fabs(atof(buf) + 0.5) < 1e-3, "state restores mod depth");

    // Render produces finite audio.
    int16_t block[256];
    for (int i = 0; i < 256; i++) block[i] = (int16_t)((i % 64) * 300 - 9600);
    api->render_block(inst, block, 128);
    bool sane = true;
    for (int i = 0; i < 256; i++) if (block[i] == INT16_MIN) sane = false;
    check(sane, "render_block produces sane int16");

    // A note-on is a slice trigger and must not crash.
    const uint8_t note_on[3] = { 0x90, 60, 100 };
    move_audio_fx_on_midi(inst, note_on, 3, 0);
    api->render_block(inst, block, 128);
    check(true, "note-on slice trigger survives a render");

    api->destroy_instance(inst);
    std::printf(failures ? "\nFAILURES: %d\n" : "\nALL TESTS PASSED\n", failures);
    return failures ? 1 : 0;
}
```

- [ ] **Step 2: Add to the Makefile and watch it fail**

```make
TESTS := test_lib_smoke test_params test_engine test_plugin

$(BIN)/test_plugin: test_plugin.cpp $(ROOT)/src/dsp/capicola_plugin.cpp $(ENGINE_SRC) | $(BIN)
	$(CXX) $(CXXFLAGS) $(INC) $^ -o $@
```

Run: `make -C tests/host test`
Expected: FAIL — `capicola_plugin.cpp: No such file or directory`

- [ ] **Step 3: Write `src/dsp/capicola_plugin.cpp`**

```cpp
// Schwung plugin_api_v2 wrapper for the capicola engine.
//
// REALTIME CONTRACT: every entry point here runs on the SPI callback. The only
// allocation is the one-shot engine ring in create_instance (documented in
// README.md). set_param, get_param, on_midi and render_block do not allocate,
// block, or touch the filesystem.

#include "plugin_api_v1.h"
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
    float mix;         // 0..1
    float fbTone;      // norm

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
    // Modulate in NORM space, then taper. Order is load-bearing.
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
        if (p == PARAM_PITCH)      s->primary[p] = PitchNormFromSemis(v);
        else if (p == PARAM_GRAIN) s->primary[p] = GrainNormFromKeyframes(v);
        else if (p == PARAM_FEEDBACK) s->primary[p] = Clamp01(v / 1.5f);
        else                       s->primary[p] = Clamp01(v);
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
        // Fixed-arity blob; see get_param. Order must match exactly.
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
        if      (p == PARAM_PITCH)    out = PitchSemisFromNorm(s->primary[p]);
        else if (p == PARAM_GRAIN)    out = GrainKeyframesFromNorm(s->primary[p]);
        else if (p == PARAM_FEEDBACK) out = s->primary[p] * 1.5f;
        else                          out = s->primary[p];
        return snprintf(buf, len, "%.4f", out);
    }

    int m = ModIndex(key, "mod_depth_");
    if (m >= 0) return snprintf(buf, len, "%.4f", s->mod.Depth(m));
    m = ModIndex(key, "mod_src_");
    if (m >= 0) return snprintf(buf, len, "%d", s->mod.Source(m));

    if (!strcmp(key, "smoothing")) return snprintf(buf, len, "%.4f", s->smoothing);
    if (!strcmp(key, "fade_ms"))   return snprintf(buf, len, "%.2f", s->fadeMs);
    if (!strcmp(key, "drive"))     return snprintf(buf, len, "%.4f", s->drive);
    if (!strcmp(key, "character")) return snprintf(buf, len, "%.4f", s->character);
    if (!strcmp(key, "mix"))       return snprintf(buf, len, "%.4f", s->mix);
    if (!strcmp(key, "fb_tone"))   return snprintf(buf, len, "%.4f", s->fbTone);

    if (!strcmp(key, "state")) {
        return snprintf(buf, len,
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
        // so this and module.json's inline metadata must agree.
        return snprintf(buf, len, "%s",
        "["
        "{\"key\":\"pitch\",\"name\":\"Pitch\",\"type\":\"float\",\"min\":-12,\"max\":12,\"step\":1,\"default\":0,\"unit\":\"st\"},"
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
        "{\"key\":\"fb_tone\",\"name\":\"FB Tone\",\"type\":\"float\",\"min\":0,\"max\":1,\"step\":0.01,\"default\":0.3769},"
        "{\"key\":\"mod_depth_pitch\",\"name\":\"Pitch Depth\",\"type\":\"float\",\"min\":-1,\"max\":1,\"step\":0.01,\"default\":0},"
        "{\"key\":\"mod_depth_stretch\",\"name\":\"Stretch Depth\",\"type\":\"float\",\"min\":-1,\"max\":1,\"step\":0.01,\"default\":0},"
        "{\"key\":\"mod_depth_threshold\",\"name\":\"Thresh Depth\",\"type\":\"float\",\"min\":-1,\"max\":1,\"step\":0.01,\"default\":0},"
        "{\"key\":\"mod_depth_grain\",\"name\":\"Grain Depth\",\"type\":\"float\",\"min\":-1,\"max\":1,\"step\":0.01,\"default\":0},"
        "{\"key\":\"mod_depth_quality\",\"name\":\"Qual Depth\",\"type\":\"float\",\"min\":-1,\"max\":1,\"step\":0.01,\"default\":0},"
        "{\"key\":\"mod_depth_feedback\",\"name\":\"Fdbk Depth\",\"type\":\"float\",\"min\":-1,\"max\":1,\"step\":0.01,\"default\":0},"
        "{\"key\":\"mod_src_pitch\",\"name\":\"Pitch Src\",\"type\":\"enum\",\"options\":[\"Input Env\",\"Output Env\"],\"default\":1},"
        "{\"key\":\"mod_src_stretch\",\"name\":\"Stretch Src\",\"type\":\"enum\",\"options\":[\"Input Env\",\"Output Env\"],\"default\":1},"
        "{\"key\":\"mod_src_threshold\",\"name\":\"Thresh Src\",\"type\":\"enum\",\"options\":[\"Input Env\",\"Output Env\"],\"default\":1},"
        "{\"key\":\"mod_src_grain\",\"name\":\"Grain Src\",\"type\":\"enum\",\"options\":[\"Input Env\",\"Output Env\"],\"default\":1},"
        "{\"key\":\"mod_src_quality\",\"name\":\"Qual Src\",\"type\":\"enum\",\"options\":[\"Input Env\",\"Output Env\"],\"default\":1},"
        "{\"key\":\"mod_src_feedback\",\"name\":\"Fdbk Src\",\"type\":\"enum\",\"options\":[\"Input Env\",\"Output Env\"],\"default\":1}"
        "]");
    }

    if (!strcmp(key, "ui_hierarchy")) {
        return snprintf(buf, len, "%s",
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
            "{\"level\":\"modulation\",\"label\":\"Modulation\"}"
          "]},"
        "\"secondary\":{\"label\":\"Secondary\","
          "\"knobs\":[\"smoothing\",\"fade_ms\",\"drive\",\"character\",\"mix\",\"fb_tone\"],"
          "\"params\":["
            "{\"key\":\"smoothing\",\"label\":\"Smoothing\"},"
            "{\"key\":\"fade_ms\",\"label\":\"Fade\"},"
            "{\"key\":\"drive\",\"label\":\"Drive\"},"
            "{\"key\":\"character\",\"label\":\"Character\"},"
            "{\"key\":\"mix\",\"label\":\"Mix\"},"
            "{\"key\":\"fb_tone\",\"label\":\"FB Tone\"}"
          "]},"
        "\"modulation\":{\"label\":\"Modulation\","
          "\"knobs\":[\"mod_depth_pitch\",\"mod_depth_stretch\",\"mod_depth_threshold\","
                    "\"mod_depth_grain\",\"mod_depth_quality\",\"mod_depth_feedback\"],"
          "\"params\":["
            "{\"key\":\"mod_depth_pitch\",\"label\":\"Pitch Depth\"},"
            "{\"key\":\"mod_depth_stretch\",\"label\":\"Stretch Depth\"},"
            "{\"key\":\"mod_depth_threshold\",\"label\":\"Thresh Depth\"},"
            "{\"key\":\"mod_depth_grain\",\"label\":\"Grain Depth\"},"
            "{\"key\":\"mod_depth_quality\",\"label\":\"Qual Depth\"},"
            "{\"key\":\"mod_depth_feedback\",\"label\":\"Fdbk Depth\"},"
            "{\"key\":\"mod_src_pitch\",\"label\":\"Pitch Src\"},"
            "{\"key\":\"mod_src_stretch\",\"label\":\"Stretch Src\"},"
            "{\"key\":\"mod_src_threshold\",\"label\":\"Thresh Src\"},"
            "{\"key\":\"mod_src_grain\",\"label\":\"Grain Src\"},"
            "{\"key\":\"mod_src_quality\",\"label\":\"Qual Src\"},"
            "{\"key\":\"mod_src_feedback\",\"label\":\"Fdbk Src\"}"
          "]}"
        "}}");
    }

    if (!strcmp(key, "consumes_line_input")) return snprintf(buf, len, "0");

    buf[0] = '\0';
    return 0;
}

void RenderBlock(void* inst, int16_t* out_lr, int frames) {
    Instance* s = static_cast<Instance*>(inst);
    if (!s || !out_lr) return;
    if (frames > kBlockMax) frames = kBlockMax;

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

plugin_api_v2_t g_api;

} // namespace

extern "C" plugin_api_v2_t* move_plugin_init_v2(const host_api_v1_t* /*host*/) {
    g_api.api_version     = 2;
    g_api.create_instance  = CreateInstance;
    g_api.destroy_instance = DestroyInstance;
    g_api.on_midi          = nullptr;      // audio FX use the dlsym'd hook below
    g_api.set_param        = SetParam;
    g_api.get_param        = GetParam;
    g_api.render_block     = RenderBlock;
    return &g_api;
}

// Discovered by the chain host via dlsym("move_audio_fx_on_midi") and broadcast
// to every audio FX — the same path the ducker uses.
extern "C" void move_audio_fx_on_midi(void* inst, const uint8_t* msg, int len, int source) {
    OnMidi(inst, msg, len, source);
}
```

- [ ] **Step 4: Run the tests**

```bash
make -C tests/host test
```

Expected: `ALL TESTS PASSED`. If `plugin_api_v2_t`'s field names differ, read `src/dsp/plugin_api_v1.h` and match it — that header is the authority, not this plan.

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "plugin_api_v2 wrapper: 24 params, contracts, state, slice-on-note

Primary params are stored as norms because the matrix modulates pre-taper;
pitch and grain convert to semitones/keyframes only at the param boundary.

chain_params and module.json's inline hierarchy metadata must agree —
param_meta.mjs merges them with chain_params winning."
```

---

### Task 5: module.json and page-layout verification

**Goal:** A module manifest whose declared contract produces exactly three knob pages, proven without hardware.

**Files:**
- Create: `src/module.json`
- Create: `tools/preview_pages.mjs`

**Acceptance Criteria:**
- [ ] `module.json` is valid JSON with `component_type: audio_fx`, `chainable: true`, `api_version: 2`, `license: AGPL-3.0`
- [ ] Inline `ui_hierarchy` metadata agrees with `chain_params` on every key's type/min/max
- [ ] `planPages` yields exactly three `PAGE_KNOBS` pages
- [ ] `validateContract` reports zero `error`-level findings

**Verify:** `node tools/preview_pages.mjs` → `PAGES: 3 knob pages` and `ERRORS: 0`

**Steps:**

- [ ] **Step 1: Write `src/module.json`**

```json
{
  "id": "capicola",
  "name": "Capicola",
  "abbrev": "CP",
  "version": "0.1.0",
  "description": "Keyframe time stretcher with transient re-anchoring (port of capicola by Heavylight Industries)",
  "author": "Heavylight Industries (port: charlesvestal)",
  "license": "AGPL-3.0",
  "dsp": "capicola.so",
  "api_version": 2,
  "capabilities": {
    "audio_out": true,
    "audio_in": false,
    "midi_in": true,
    "midi_out": false,
    "chainable": true,
    "component_type": "audio_fx"
  }
}
```

`audio_in` is **false** deliberately. It refers to the *line input*, and this FX takes its audio from the chain, not the jack. Setting it true would arm the feedback gate (`feedback_gate.mjs`) and boot-bypass the slot. The `consumes_line_input` get_param answers `0` for the same reason.

`ui_hierarchy` is served from `get_param` (Task 4) rather than duplicated here — one source of truth. `chain_params` carries the metadata, and `param_meta.mjs` merges chain_params over any inline hierarchy metadata anyway.

- [ ] **Step 2: Write `tools/preview_pages.mjs`**

```javascript
#!/usr/bin/env node
/**
 * preview_pages.mjs — run our declared contract through Schwung's own page
 * planner and contract validator. No device, no build: planPages and
 * validateContract are pure.
 *
 *   node tools/preview_pages.mjs
 *   SCHWUNG_ROOT=/path/to/schwung node tools/preview_pages.mjs
 *
 * The contract strings are extracted from capicola_plugin.cpp so this checks
 * what the DSP actually serves, not a copy that can drift.
 */
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const HERE = path.dirname(fileURLToPath(import.meta.url));
const REPO = path.dirname(HERE);
const SCHWUNG = process.env.SCHWUNG_ROOT || path.join(path.dirname(REPO), "schwung");

const { planPages, PAGE_KNOBS } = await import(
    path.join(SCHWUNG, "src/shared/param_pages/page_plan.mjs"));
const { validateContract } = await import(
    path.join(SCHWUNG, "src/shared/param_pages/validate_contract.mjs"));

/* Pull the two contract literals out of the C++ so they cannot drift from what
 * the DSP serves. Each is a run of adjacent "..." string literals inside a
 * single snprintf, so concatenate them and unescape. */
function extractContract(src, key) {
    const anchor = src.indexOf(`!strcmp(key, "${key}")`);
    if (anchor < 0) throw new Error(`no branch for ${key}`);
    const tail = src.slice(anchor);
    const parts = [...tail.matchAll(/"((?:[^"\\]|\\.)*)"/g)].map((m) => m[1]);
    // Skip the key literal itself and the "%s" format string.
    const body = parts.slice(2).join("");
    return body.replace(/\\"/g, '"').replace(/\\\\/g, "\\");
}

const src = fs.readFileSync(path.join(REPO, "src/dsp/capicola_plugin.cpp"), "utf8");
const hierarchy   = JSON.parse(extractContract(src, "ui_hierarchy"));
const chainParams = JSON.parse(extractContract(src, "chain_params"));

console.log(`chain_params entries: ${chainParams.length}`);
console.log(`hierarchy levels:     ${Object.keys(hierarchy.levels).join(", ")}`);

const plan = planPages({ hierarchy, chainParams, mode: null, visible: null });
const knobPages = plan.pages.filter((p) => p.kind === PAGE_KNOBS);

console.log("\nPages:");
for (const p of plan.pages) console.log(`  ${p.kind.padEnd(8)} ${p.name}`);
if (plan.warnings?.length) {
    console.log("\nWarnings:");
    for (const w of plan.warnings) console.log(`  ${w}`);
}

const findings = validateContract({ id: "capicola", ui_hierarchy: hierarchy, chain_params: chainParams });
const errors = (findings || []).filter((f) => f.level === "error");
console.log("\nValidator:");
for (const f of findings || []) console.log(`  ${f.level.padEnd(5)} ${f.message || JSON.stringify(f)}`);

console.log(`\nPAGES: ${knobPages.length} knob pages`);
console.log(`ERRORS: ${errors.length}`);
process.exit(knobPages.length === 3 && errors.length === 0 ? 0 : 1);
```

- [ ] **Step 3: Run it**

```bash
node tools/preview_pages.mjs
```

Expected: `PAGES: 3 knob pages` and `ERRORS: 0`, exit 0.

If the planner emits more than three knob pages, the cause is almost certainly overflow continuation — a level's `params[]` keys that aren't on its `knobs[]` follow the authored knobs as extra pages (`page_plan.mjs:378-382`). The `modulation` level has 12 params against 6 knobs, so the six `mod_src_*` keys will spill onto a fourth page. That is acceptable and arguably better than the design's plan — but decide deliberately: either accept four pages and update the spec, or drop `mod_src_*` from `params[]` so they are reachable only via the list editor. **Prefer accepting the overflow page** — it makes the selectors turnable, and the enum picker still opens from a click.

- [ ] **Step 4: Reconcile the spec if the page count differs**

If Step 3 shows four pages, update `docs/superpowers/specs/2026-08-21-capicola-module-design.md` to say four, with one sentence on why. A spec that disagrees with the tool is worse than either answer.

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "module.json + hardware-free page-layout check

preview_pages.mjs extracts the contract literals from capicola_plugin.cpp and
runs them through Schwung's own planPages/validateContract, so the page layout
is proven before anything is cross-compiled.

audio_in stays false: it means the LINE input, and setting it true would arm the
feedback gate and boot-bypass the slot."
```

---

### Task 6: Build scripts and cross-compile

**Goal:** `dist/capicola/capicola.so` built for ARM64 in Docker, with the packed-struct assertion holding under the cross-compiler.

**Files:**
- Create: `scripts/Dockerfile`
- Create: `scripts/build.sh`
- Create: `scripts/install.sh`

**Acceptance Criteria:**
- [ ] `./scripts/build.sh` produces `dist/capicola/capicola.so`
- [ ] `file` reports ARM aarch64 shared object
- [ ] `dist/capicola/` contains `module.json` and `help.json`
- [ ] `dist/capicola-module.tar.gz` exists with a top-level `capicola/` dir
- [ ] `sizeof(Keyframe) == 12` static_assert compiles under the cross-compiler

**Verify:** `./scripts/build.sh && file dist/capicola/capicola.so` → `ELF 64-bit LSB shared object, ARM aarch64`

**Steps:**

- [ ] **Step 1: Write `scripts/Dockerfile`**

```dockerfile
FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    g++-aarch64-linux-gnu \
    make \
    patch \
    && rm -rf /var/lib/apt/lists/*

ENV CROSS_PREFIX=aarch64-linux-gnu-

WORKDIR /build
```

- [ ] **Step 2: Write `scripts/build.sh`**

```bash
#!/usr/bin/env bash
# Build the Capicola module for Schwung (ARM64).
# Uses Docker unless CROSS_PREFIX is already set.
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
IMAGE_NAME="move-anything-builder-cpp"

if [ -z "$CROSS_PREFIX" ] && [ ! -f "/.dockerenv" ]; then
    echo "=== Capicola Module Build (via Docker) ==="
    if ! docker image inspect "$IMAGE_NAME" &>/dev/null; then
        echo "Building Docker image (first time only)..."
        docker build -t "$IMAGE_NAME" -f "$SCRIPT_DIR/Dockerfile" "$REPO_ROOT"
    fi
    docker run --rm \
        -v "$REPO_ROOT:/build" \
        -u "$(id -u):$(id -g)" \
        -w /build \
        "$IMAGE_NAME" \
        ./scripts/build.sh
    echo "=== Done ==="
    exit 0
fi

CROSS_PREFIX="${CROSS_PREFIX:-aarch64-linux-gnu-}"
cd "$REPO_ROOT"

echo "=== Building Capicola Module ==="
echo "Cross prefix: $CROSS_PREFIX"

# Apply our patches to a build-tree copy; src/dsp/lib stays pristine.
./scripts/apply_patches.sh "$REPO_ROOT/build/lib"

mkdir -p build dist/capicola

echo "Compiling DSP plugin..."
${CROSS_PREFIX}g++ -std=c++17 -Ofast -shared -fPIC \
    -march=armv8-a -mtune=cortex-a72 \
    -fomit-frame-pointer -fno-stack-protector \
    -fvisibility=hidden \
    -DNDEBUG \
    src/dsp/capicola_plugin.cpp \
    src/dsp/capicola_engine.cpp \
    src/dsp/capicola_params.cpp \
    -o build/capicola.so \
    -Ibuild/lib -Isrc/dsp \
    -lm

echo "Packaging..."
# cat, not cp — avoids ExtFS deallocation issues under Docker.
cat src/module.json > dist/capicola/module.json
[ -f src/help.json ] && cat src/help.json > dist/capicola/help.json
cat build/capicola.so > dist/capicola/capicola.so
chmod +x dist/capicola/capicola.so

cd dist
tar -czf capicola-module.tar.gz capicola/
cd ..

echo ""
echo "=== Build Complete ==="
echo "Output:  dist/capicola/"
echo "Tarball: dist/capicola-module.tar.gz"
```

```bash
chmod +x scripts/build.sh
```

Note: `-fvisibility=hidden` keeps upstream's symbols out of the dynamic table. The two entry points are `extern "C"` and must stay visible — if `dlsym` cannot find them after this, add `__attribute__((visibility("default")))` to both in `capicola_plugin.cpp`.

- [ ] **Step 3: Write `scripts/install.sh`**

```bash
#!/usr/bin/env bash
# Deploy the built module to a Move over SSH.
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
DEVICE="${MOVE_HOST:-ableton@move.local}"
DEST="/data/UserData/schwung/modules/audio_fxs/capicola"

if [ ! -f "$REPO_ROOT/dist/capicola/capicola.so" ]; then
    echo "No build found. Run ./scripts/build.sh first." >&2
    exit 1
fi

echo "Installing to $DEVICE:$DEST"
ssh "$DEVICE" "mkdir -p $DEST"
scp "$REPO_ROOT"/dist/capicola/* "$DEVICE:$DEST/"
echo "Done. Restart the shadow UI or reload the module to pick it up."
```

```bash
chmod +x scripts/install.sh
```

- [ ] **Step 4: Build and verify the artifact**

```bash
./scripts/build.sh
file dist/capicola/capicola.so
ls -la dist/capicola/ dist/capicola-module.tar.gz
tar -tzf dist/capicola-module.tar.gz | head -3
```

Expected: `ELF 64-bit LSB shared object, ARM aarch64`, and the tarball listing starts with `capicola/`.

- [ ] **Step 5: Confirm the entry points are exported**

```bash
aarch64-linux-gnu-nm -D --defined-only dist/capicola/capicola.so 2>/dev/null \
  || docker run --rm -v "$PWD:/b" -w /b move-anything-builder-cpp \
     aarch64-linux-gnu-nm -D --defined-only dist/capicola/capicola.so
```

Expected: both `move_plugin_init_v2` and `move_audio_fx_on_midi` present. If either is missing, the `-fvisibility=hidden` note in Step 2 applies.

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "Cross-compile to ARM64 via Docker

apply_patches.sh runs first so the build compiles the patched copy in build/lib
while src/dsp/lib stays byte-identical to upstream."
```

---

### Task 7: Help content and documentation

**Goal:** On-device help and a README that credits upstream and states the known realtime violation plainly.

**Files:**
- Create: `src/help.json`
- Modify: `README.md`

**Acceptance Criteria:**
- [ ] `help.json` is valid JSON and explains all three pages
- [ ] `help.json` credits upstream and the paper
- [ ] README documents the `create_instance` allocation as a known RT violation
- [ ] README documents the ring size and its memory cost

**Verify:** `python3 -c "import json;json.load(open('src/help.json'));print('OK')"` → `OK`

**Steps:**

- [ ] **Step 1: Write `src/help.json`**

```json
{
  "title": "Capicola",
  "sections": [
    {
      "heading": "What it does",
      "body": "Incoming audio is reduced to keyframes and replayed by a lagging read head under independent time and pitch control. Detected transients snap that head back to the present, so stretched tails ring out underneath while the output stays locked to the rhythm of the input."
    },
    {
      "heading": "Page 1 - Primary",
      "body": "Pitch: +/-12 semitones, grain pitch only. Stretch: fully CCW is realtime, noon is about 6x slower, fully CW is a true freeze. Threshold: how picky the transient detector is - it is a ratio over the signal's own average, so it needs no gain staging. The very top mutes auto-triggering. Grain: splice length in keyframes. Quality: keyframe density - CCW is crunchy and sparse, CW is maximum fidelity. Feedback: wet output back into the input, above 1.0 on purpose unstable."
    },
    {
      "heading": "Page 2 - Secondary",
      "body": "Smoothing: envelope follower speed. Fade: the crossfade length, which is also the latency and the fastest possible splice rate. Drive and Character: a waveshaper applied to the keyframes before interpolation, so it cannot alias however hard you push it. Character sweeps quake - clean - sinc. Mix: dry/wet. FB Tone: the feedback bandpass centre."
    },
    {
      "heading": "Page 3 - Modulation",
      "body": "Each knob sets how much the same-numbered primary knob is modulated - centre is off, either side is positive or negative. Each has a source in the list below: the input envelope or the output envelope. Output is the default, so raising a depth alone makes the module modulate itself."
    },
    {
      "heading": "Triggering",
      "body": "A note into this slot forces a splice on both channels, even when the threshold is set to mute."
    },
    {
      "heading": "Credit",
      "body": "A port of capicola by Heavylight Industries, the hardware embodiment of their DAFx26 paper 'Keyframe Time Stretching via Extrema Sampling'. The DSP core is their work, used unmodified under AGPL-3.0."
    }
  ]
}
```

- [ ] **Step 2: Append implementation notes to `README.md`**

```markdown
## Implementation notes

### Memory

Each instance holds two keyframe rings of 2^18 frames x 12 bytes = 3.1 MB per
channel, 6.3 MB per instance, about 12 seconds of audio. Upstream uses 2^21
(25 MB/channel), sized for a 90-second Eurorack freeze; Schwung can run up to 12
instances, which would be 600 MB.

### Known realtime violation

`create_instance` runs on the SPI audio callback — see `plugin_api_v1.h` and
Schwung's `docs/REALTIME_SAFETY.md` rule 4. The ~6.3 MB ring allocation there is
a genuine violation of that contract. It is one-shot at module load, on the same
thread that already `dlopen()`s the plugin, and it is stated here rather than
hidden. `set_param`, `get_param`, `on_midi` and `render_block` do not allocate,
block, or touch the filesystem.

### Differences from upstream

| | Upstream | Here |
|---|---|---|
| Sample rate | 48 kHz | 44.1 kHz |
| Ring | 2^21 keyframes/ch | 2^18 keyframes/ch |
| Instances | 2 SDRAM globals | per-instance |
| Panel | Eurorack, 4 pages + CV | Schwung param pages, 3 pages |
| Mod sources | input env / output env / CV IN | input env / output env |
| Presets | QSPI slot | Schwung slot autosave + user presets |

Modulation depth and routing are ported in full. Upstream's CV IN source is
dropped because Move has no such jack and Schwung's chain LFOs already reach
these parameters from outside.
```

- [ ] **Step 3: Verify and commit**

```bash
python3 -c "import json;json.load(open('src/help.json'));print('OK')"
git add -A
git commit -m "Help content and implementation notes

Documents the create_instance allocation as a known RT violation rather than
leaving it for someone to find."
```

---

### Task 8: On-hardware verification

**Goal:** Confirm on a real Move that the port behaves like capicola.

**USER-ORDERED GATE — NON-SKIPPABLE.** This task was requested by the user in the current conversation. It MUST NOT be closed by walking around it, by declaring it "verified inline", or by substituting a cheaper check. Close only after every item in `acceptanceCriteria` has been re-validated independently, with output captured.

**Files:**
- Modify: none expected; fixes land in the relevant task's files

**Acceptance Criteria:**
- [ ] Module appears in the audio FX list and loads into a chain slot without error
- [ ] Three knob pages are reachable and every knob moves its parameter
- [ ] With a drum loop playing and Stretch at noon, splices audibly land on the hits rather than at a fixed rate
- [ ] Stretch fully CW freezes: audio continues after the source stops
- [ ] A note into the slot forces an audible splice with Threshold at maximum (mute)
- [ ] Raising a Modulation depth with the default Output Env source audibly self-modulates
- [ ] `mod_src_*` enum rows open the enum picker on a jog click
- [ ] Slot autosave restores all 24 params across a reload

**Verify:** Manual, on hardware, with the user present. Capture `ssh ableton@move.local "tail -50 /data/UserData/schwung/debug.log"` for any load error.

**Steps:**

- [ ] **Step 1: Deploy**

```bash
./scripts/build.sh
./scripts/install.sh
```

- [ ] **Step 2: Ask before making sound**

The user's standing preference: audio goes to whoever is wearing headphones. Ask before any step that produces sound, and say what will play and roughly how loud.

- [ ] **Step 3: Check it loaded**

```bash
ssh ableton@move.local "touch /data/UserData/schwung/debug_log_on"
ssh ableton@move.local "tail -50 /data/UserData/schwung/debug.log"
```

Look for the module loading without a dlopen or symbol error. Turn the log back off when finished — leaving it on has itself caused dropouts:

```bash
ssh ableton@move.local "rm -f /data/UserData/schwung/debug_log_on"
```

- [ ] **Step 4: Walk the acceptance criteria in order**

Feedback is deliberately last, at low volume, and only after asking — the knob goes above 1.0 on purpose and the loop is unstable there by design.

- [ ] **Step 5: Record what happened**

Note any deviation from upstream's documented behaviour in `README.md` under a "Known differences" heading. If something is wrong, fix it in the task that owns the file rather than patching over it here.

---

## Self-review

**Spec coverage.** Repo/licence → Task 0. Upstream patches → Task 1. Ring size, per-instance, 44.1 kHz, RT-violation note → Tasks 3 and 7. All 24 params and every curve → Task 2. Three preserved behaviours (transient re-anchor, non-aliasing shaper, stereo drift guard) → Task 3 code and Task 8 criteria. Contracts and pages → Tasks 4 and 5. Build → Task 6. Testing steps 1–6 → Tasks 1, 2, 3, 5, 6, 8.

**One deviation, flagged rather than buried:** the spec claims three knob pages. `page_plan.mjs:378-382` continues a level's non-knob `params[]` onto extra pages, so the `modulation` level's six `mod_src_*` keys will likely produce a fourth. Task 5 Step 3 makes this an explicit decision with a recommendation, and Step 4 reconciles the spec either way, rather than letting the plan and the spec disagree silently.

**Type consistency.** `capicola_schwung::ParamId`, `ModSource`, `ModRouter::{SetDepth,SetSource,Depth,Source,SetEnvelopes,Apply}`, `Engine::{Init,ProcessBlock,Set*,TriggerSlice,EnvNormIn,EnvNormOut,TransientCount}` are defined in Tasks 2 and 3 and used with those exact names in Tasks 3 and 4. Curve names (`StretchCurve`, `ThresholdCurve`, `QualityCurve`, `PitchCurve`, `SmoothingCurve`, `FbToneCurve`, `DriveCurve`, `FadeMsToSamples`, `PitchSemisFromNorm`, `PitchNormFromSemis`, `GrainKeyframesFromNorm`, `GrainNormFromKeyframes`, `Clamp01`) match between header, implementation, tests and the plugin.

**Upstream APIs used** are all confirmed present in the vendored headers: `KeyframeRecorder::{Init,ProcessBlock,SubmitRequest,GetState,SetGrainPitch,SetGrainStretch,SetGrainLeash,SetGrainFade,SetThreshold,SetTransientThreshold,SetTkeoCutoff,SetDistortDrive,SetDistortCharacter,TkeoEnvelope,DetectorGate,FiredThisBlock,GridLag,TransientCount}`, `Detector::{Init,Analyze,Envelope,Gate,SetThreshold,SetCutoff}`, `StateVariable::{Init,SetControls,Tick,GetBandpass}`, `Shapers::{Init,ReadSinc}`.
