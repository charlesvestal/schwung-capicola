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
