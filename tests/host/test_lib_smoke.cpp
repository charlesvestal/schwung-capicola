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
