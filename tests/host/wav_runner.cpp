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
