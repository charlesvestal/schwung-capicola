# Capicola for Schwung — design

*2026-08-21*

## Summary

A live audio FX for a Schwung chain slot. Incoming stereo is continuously
sparsified into keyframes and replayed by a lagging read head under independent
time and pitch control. A transient detector runs in parallel; every kept
transient snaps the read head back to the present with a crossfade, so stretched
tails ring out underneath while the output stays locked to the input's rhythm.

This is a port of [capicola](https://github.com/heavylight-industries/capicola)
by Heavylight Industries — the hardware embodiment of the independently authored,
peer-reviewed DAFx26 paper *Keyframe Time Stretching via Extrema Sampling*.
Upstream targets the Alchemy Lab V2 (Daisy / STM32H750). We keep the DSP core and
replace the Eurorack panel with Schwung param pages.

## Why this is not schwung-stretch

`schwung-stretch` is an offline tool wrapping Bungee: file in, file out, high
quality, transparent. Capicola is the opposite trade — realtime, sample-by-sample,
no block latency beyond the OLA crossfade, and deliberately a creative effect
rather than transparent restoration. The paper measures ~34–61 Cortex-M7
cycles/sample against ~850 for a phase vocoder. Time rate τ and pitch rate σ are
decoupled and freely modulatable while running, which is the entire point and
the thing the offline tool cannot do.

## Repository

New repo `schwung-capicola`, sibling to the other external modules.

```
schwung-capicola/
  LICENSE                AGPL-3.0
  README.md              attribution up front
  src/
    module.json          id "capicola", audio_fx, chainable, api_version 2
    ui.js                minimal; Schwung param pages do the work
    help.json
    dsp/
      lib/                       capicola lib/ VERBATIM (upstream-tracked)
      capicola_engine.{h,cpp}    port of upstream audio_engine.cpp
      capicola_plugin.cpp        plugin_api_v2 + chain_params + ui_hierarchy
  scripts/{build.sh,install.sh,Dockerfile}
  docs/superpowers/specs/
```

`lib/` stays byte-identical to upstream so their fixes rebase cleanly and the
AGPL provenance is unambiguous. Every Schwung-specific line lives outside it.

### Required upstream patches

Two, both one-liners, both worth offering back upstream:

1. `KeyframeRecorder::Init` hardcodes `detector.Init(48000.0f)`. Needs a
   sample-rate argument — Move runs at 44100 Hz.
2. `Keyframe` uses `__attribute__((packed, aligned(4)))` with a
   `static_assert(sizeof(Keyframe) == 12)`. Expected to hold under the ARM64
   cross-compiler; confirm at build time rather than assume.

If either patch cannot be kept minimal, carry it as a patch file applied at
build time rather than editing `lib/` in place.

## Engine port

Upstream `audio_engine.cpp` is 288 lines and is the only real adaptation work.
Upstream `src/main.cpp` (840 lines of Alchemy Lab panel UI) is discarded entirely.

| Drop | Replace with |
|---|---|
| `daisy_seed.h`, `DSY_SDRAM_BSS` | Heap allocation in `create_instance` |
| `daisy::CpuLoadMeter` | Nothing |
| `static sparse_l` / `sparse_r` globals | Per-instance members |
| 48000 Hz constants | 44100 — fade 960→882 samples, drift guard 48000→44100 |
| float in / float out | int16 interleaved ↔ float deinterleaved at the boundary |

Upstream's globals exist because `DSY_SDRAM_BSS` cannot apply to a class member.
Schwung is multi-instance (4 slots + 8 Master FX positions), so they must become
per-instance members. `KeyframeRecorder` is trivially constructible by design,
which makes this safe.

### Ring size

Drops from 2²¹ to **2¹⁸ keyframes** per channel.

Upstream's 2²¹ is ~25 MB/channel — 50 MB per instance, sized for a Eurorack
freeze that holds ~90 s. 2¹⁸ is 3.1 MB/channel (~12 s of audio at the paper's
M/N ≈ 0.5 for musical material), so 6.3 MB per instance. The worst case Schwung
allows is 12 concurrent instances (4 slots + 8 Master FX positions) — 75 MB at
the reduced size against 600 MB at upstream's. Move has 1.85 GB with ~1.28 GB
available, so even upstream's size would fit in practice; there is simply no
reason to spend it. It is a template parameter — one number.

`Analyzer`'s raw input ring is 8 samples, so `SparseLine` is effectively the
whole footprint.

`Granule::kSafetyKeyframes` is derived as `bufsz - (bufsz >> 3)`, so it tracks
the change automatically.

### Known realtime violation

`create_instance` runs on the SPI callback (see `plugin_api_v1.h` and
`docs/REALTIME_SAFETY.md` rule 4). The ~6 MB allocation there is a genuine
violation. It is one-shot at module load, `dlopen` already happens on that
thread, and it matches what the rest of the fleet does — but it is real and gets
documented in the module's own docs rather than papered over. No allocation
occurs in `render_block`, `set_param` or `on_midi`.

## Controls

Twenty-four parameters over four knob pages. Ranges and tapers are lifted from
upstream so the feel is the tested one.

The modulation matrix is two pages, not one: `ui_hierarchy` splits `modulation`
(the six bipolar depths) and `mod_source` (the six source selectors) into
sibling levels off `root`, rather than nesting the selectors as continuation
`params[]` entries under one `modulation` level. Schwung's page planner
(`page_plan.mjs`) appends a level's non-knob `params[]` keys onto extra pages
after its authored `knobs[]`, and the six `mod_src_*` selectors straddle that
overflow boundary — two land on the depth page, four get an ugly page of their
own (`"Modula/Mod Sources"`), for an 8/4 split. Declaring them as their own
level instead yields four even pages of six, verified by
`tools/preview_pages.mjs` against Schwung's real planner and validator (0
findings either way — this is a UX call, not a correctness one).

Where the engineering unit reads well it is exposed directly. Where upstream's
taper is load-bearing and the raw unit is meaningless on a 128×64 screen, the
param stays 0..1 with the curve inside the DSP and a `display_format` showing the
real value — same feel, readable screen.

### Primary page

| Knob | Key | Type | Range | Notes |
|---|---|---|---|---|
| 1 | `pitch` | float | −12…+12 st | ±0.2 st detent snaps to true 0 |
| 2 | `stretch` | float | 0…1 | `(1−x)^2.5` taper → ×1 (realtime) … freeze. ~5.7× at noon |
| 3 | `threshold` | float | 0…1 | Piecewise → keep-ratio 0…8. `x ≤ 0.9` maps 0…4; above maps 4…8. `x > 0.99` sends the mute sentinel (`1.0e9`) |
| 4 | `grain` | int | 32…4096 | Adaptive splice leash, in keyframes |
| 5 | `quality` | float | 0…1 | → analyzer ε 0.1 (crunchy) … 0.001 (fidelity). CW = fidelity |
| 6 | `feedback` | float | 0…1.5 | Above 1.0 deliberately unstable; ±1 clamp guards runaway |

### Secondary page

| Knob | Key | Type | Range | Default |
|---|---|---|---|---|
| 1 | `smoothing` | float | 0…1 → fc 5e-5…0.125 exp | 0.4259 (fc 0.0014) |
| 2 | `fade_ms` | float | **10…250 ms** | 20 ms |
| 3 | `drive` | float | 0.5…4.0 | 1.0 |
| 4 | `character` | float | 0…1 (quake ← clean → sinc) | 1.0 (sinc) |
| 5 | `mix` | float | 0…1 | 1.0 (full wet) |
| 6 | `fb_tone` | float | 0…1 → fc 2e-3…0.9 exp | 0.3769 (fc 0.02, ~480 Hz) |

Upstream expresses fade in samples (480…12000 @ 48 kHz). We expose **ms**, which
is both readable and sample-rate independent, converting at the boundary.

### Mod Depth page

Upstream's 6×6 matrix: each primary knob gets a bipolar depth and a source
selector. The six depths are knobs 1–6 of the `modulation` level.

| Knob | Key | Type | Range | Default |
|---|---|---|---|---|
| 1–6 | `mod_depth_<n>` | float | −1…+1 | 0 (off) |

`<n>` is the primary key it modulates: `pitch`, `stretch`, `threshold`, `grain`,
`quality`, `feedback`.

### Mod Source page

The six source selectors get their own sibling level, `mod_source`, one enum
per knob, divable to the enum picker.

| Knob | Key | Type | Range | Default |
|---|---|---|---|---|
| 1–6 | `mod_src_<n>` | enum | `Input Env` / `Output Env` | `Output Env` |

Upstream's third source is the CV IN jack. Move has no such jack and needs none —
the chain host's LFOs and modulation routing can already target these params from
outside, which is the same capability by another route. Dropping it leaves two
sources and no loss.

Default source is the **output** follower, matching upstream: depth alone closes
a loop through the module's own output envelope, which is what makes it
self-modulating out of the box.

**Modulation is applied in normalized space, before the taper** — `norm +
source × depth`, clamped to 0…1, then through that param's range mapping. This is
not incidental: applying it after the taper would make depth mean something
different at each end of a `(1−x)^2.5` sweep. Consequence for the port: the six
primary params are stored internally as norms and converted at the param
boundary, even the two exposed in engineering units (`pitch` in semitones,
`grain` in keyframes — both linear, so the inverse is trivial).

Applied once per `render_block` (344 Hz at 128 frames / 44.1 kHz), which is
ample for envelope-follower modulation.

Known limitation: the Schwung knob grid shows the *base* value, so a modulated
param's cell will not animate. The chain host's modulation marks track its own
LFOs, not a module's internal matrix. Upstream has the same split — its ring
shows knob position while the LED animates intensity separately.

### Mapping curves

Ported verbatim from upstream `main.cpp`:

```
stretch   : (1 - x)^2.5
threshold : x <= 0.9 ? x * (4/0.9) : 4 + (x - 0.9) * 40   ; x > 0.99 -> 1.0e9
quality   : 0.1 + x * (0.001 - 0.1)                        ; linear, inverted
smoothing : 5.0e-5 * (0.125 / 5.0e-5)^x
fade      : ms -> samples at 44100
fb_tone   : 2.0e-3 * (0.9 / 2.0e-3)^x
drive     : 0.5 + 3.5 * x
pitch     : |st| < 0.2 -> 0 ; then exp2(st / 12)
```

### Manual slice

The module exports `move_audio_fx_on_midi`, which the chain host dlsyms
separately from the v2 API and broadcasts to every audio FX (the ducker
establishes this path). A note-on fires `Request::SLICE` on both channels,
bypassing the threshold mute — upstream's B2.

### Dropped from upstream

CV in/out jacks, LED rings, QSPI preset store, and pot-catch — all hardware
affordances with no Move equivalent.

`ModRouter` lives in upstream's `main.cpp`, not in `lib/`, so it is not part of
the verbatim lift and must be written fresh against the spec above.

Chain state persistence uses Schwung's standard opaque `<prefix>:state` blob, so
slot autosave and user presets work without upstream's QSPI layer.

## Preserved behaviour worth naming

Three upstream properties that must survive the port, because losing any of them
silently would leave something that still makes sound but is not capicola:

1. **Transient re-anchor.** A kept transient punches the idle grain, seats it at
   the delayed tap and crossfades in over `fade`. The refractory is `fade` itself,
   so dense bursts retrigger every crossfade instead of latching.
2. **The keyframe waveshaper cannot alias.** Drive is applied to keyframes before
   interpolation, so the nonlinearity never sees the sample rate. This is the
   sleeper feature of the port and it comes free.
3. **The stereo drift guardrail.** L and R run fully independent chains. On a
   block where exactly one channel auto-fired and the grids have drifted more
   than 1 s, the quiet channel is forced to slice and re-anchor.

## Licence and attribution

Repo is **AGPL-3.0**, matching upstream. Precedent exists in the catalog —
`schwung-surge`, `schwung-obxd` and `schwung-wavewarp` are GPL-3.0. AGPL's §13
network clause is inert for an offline audio module.

`module.json` carries `"license": "AGPL-3.0"`. README, `help.json` and the module
description credit Heavylight Industries, the DAFx26 paper and the upstream repo
up front, not in a footnote.

**The author approved the port on 2026-08-22.** The block on distribution was
their reply, and it has come.

**The remaining gate is hardware.** Nothing is released until the module has
been verified on a real Move (Task 8) — a release of something that has never
made a sound on the target device would be backwards. Until then: no catalog
entry, no release workflow, no tag, no `release.json`. Build and install scripts
target `./scripts/install.sh` against the device directly.

What to confirm with the author before publishing, none of which the licence
decides for us:
- the exact credit wording, and whether they want to be listed as `author` in
  `module.json` and the catalog entry (it currently reads
  `"Heavylight Industries (port: charlesvestal)"`)
- whether keeping the name **Capicola** for a port of their hardware product is
  what they intended by approving, or whether they would rather the Schwung
  build be distinguishable from the Eurorack module
- whether they want the two-line sample-rate patch offered back upstream

## Testing

1. **Host-side WAV runner first.** Build `lib/` natively, push a WAV through
   `KeyframeRecorder` in `LIVE_EFFECT` at several stretch and pitch settings,
   and confirm plausible output before any cross-compile. Mirroring upstream's
   loop verbatim and validating on the host precedes any refactoring.
2. **Pin the mapping table.** Unit-test norm → engineering value against the
   curves above so a taper cannot silently drift from upstream. Include the
   round trip for the two params exposed in engineering units (`pitch`,
   `grain`), since the matrix depends on recovering their norms.
3. **Pin that modulation is pre-taper.** Assert that a fixed depth on `stretch`
   produces different engineering deltas at each end of the sweep — the
   post-taper bug would show as a constant delta and is otherwise invisible.
4. **Pin `sizeof(Keyframe) == 12`** under the cross-compiler, not just the host.
5. **Cross-compile** ARM64 in Docker via `scripts/build.sh`.
6. **On hardware:** load as an audio FX in a slot; verify transient re-anchor
   locks to a drum loop; verify freeze at full stretch; verify slice-on-note;
   verify the default Output Env routing self-modulates once a depth is raised.
   Feedback chaos zone tested **last and at low volume** — ask before running
   anything that makes noise.

## Out of scope

- Catalog entry, release workflow, `release.json`, version tag
- CV/gate equivalents — Move has no such jacks
- Upstream's offline record/playback states (`RECORDING`, `PLAYBACK`); only
  `LIVE_EFFECT` is driven
- Any change to `schwung-stretch`
