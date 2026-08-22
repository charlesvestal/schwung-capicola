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

## Implementation notes

### Memory

Each instance holds two keyframe rings (one per channel) of 2^18 frames x
12 bytes = 3.1 MB per channel, 6.3 MB per instance — about 12 seconds of
audio at the paper's typical keep ratio for musical material. Upstream uses
2^21 frames (25 MB/channel, 50 MB/instance), sized for a Eurorack module that
freezes up to ~90 seconds on dedicated SDRAM. Schwung allows up to 12
concurrent instances (4 chain slots + 8 Master FX positions); at upstream's
ring size that ceiling would be 600 MB. Move has ~1.28 GB available, so even
upstream's size would technically fit, but there's no reason to spend it —
the ring size is a single template parameter.

### Known realtime violation

`create_instance` runs on the SPI audio callback thread — see
`src/dsp/audio_fx_api_v2.h` / `src/dsp/plugin_api_v1.h` and Schwung's
`docs/REALTIME_SAFETY.md` rule 4, which requires every plugin entry point
(including `create_instance`) to be realtime-safe. The ~6.3 MB keyframe-ring
allocation in `create_instance` is a genuine violation of that contract: it
calls into the allocator from a SCHED_FIFO 90 thread. This is documented
rather than hidden. Mitigating factors: it is one-shot, at module load time,
on the same thread that already `dlopen()`s the plugin (a pre-existing
violation of the same kind); it matches what the rest of the module fleet
does. It is still real, and a device under load could in principle glitch on
the frame a Capicola instance is created. `set_param`, `get_param`, `on_midi`
and `process_block` perform no allocation, no blocking calls and no
filesystem access.

Measured on hardware, `create_instance` originally took **14.3 ms** against
the SPI callback's ~900 µs budget — `Engine::Init()` allocated `Impl`
value-initialized (`new (std::nothrow) Impl()`), which zero-fills the whole
6.3 MB block, immediately followed by `KeyframeRecorder::Init` overwriting
every byte of it again (`sparse.Init()` → `Clear()` →
`buffer.fill(Keyframe{0.0f, 1.0f})`, plus every scalar control field set
explicitly). Switching to default-initialization (`new (std::nothrow) Impl`,
no parens) removes that redundant zero-fill pass; `KeyframeRecorder` was
already made trivially constructible upstream for exactly this reason (see
its header comment). This does not eliminate the violation — the ring
allocation itself still happens on the SPI thread — it removes a pass of
work that was strictly wasted. On this dev machine, single-shot
`create_instance` calls (which is what actually happens once per module
load, unlike a warm-allocator loop) dropped from a ~1.2–1.9 ms range to a
~0.77–1.0 ms range; the ARM device's 14.3 ms baseline is expected to shrink
by a similar proportion, not to disappear.

### Differences from upstream

| | Upstream | Here |
|---|---|---|
| Sample rate | 48 kHz | 44.1 kHz |
| Ring | 2^21 keyframes/ch | 2^18 keyframes/ch |
| Instances | 2 SDRAM globals | per-instance |
| Panel | Eurorack, 4 pages + CV | Schwung param pages, 4 pages |
| Mod sources | input env / output env / CV IN | input env / output env |
| Presets | QSPI slot | Schwung slot autosave + user presets |

Modulation depth and routing are ported in full. Upstream's CV IN source is
dropped — Move has no such jack, and Schwung's chain LFOs and modulation
routing already reach these parameters from outside, which is the same
capability by another route.

### Vendored DSP core

`src/dsp/lib/` is copied byte-identical from upstream and must never be
edited in place. Any fix goes in `patches/`, applied to a build-tree copy —
see [VENDOR.md](VENDOR.md) for the mechanics.
