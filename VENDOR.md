# Vendored code

`src/dsp/lib/` is copied **byte-identical** from
[heavylight-industries/capicola](https://github.com/heavylight-industries/capicola),
licensed AGPL-3.0.

- Upstream commit: `f0fb61cfa7111067b4ec1a642d1b16a0910adb3b`
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

(The patch itself is created in Task 1 — this just documents it ahead of time.)
