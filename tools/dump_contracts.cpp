// tools/dump_contracts.cpp
//
// Tiny host-side dumper used by tools/preview_pages.mjs to pull the
// authoritative "chain_params" / "ui_hierarchy" contracts straight out of the
// compiled plugin (never from a copy that can drift from capicola_plugin.cpp).
//
// Usage: dump_contracts <key>
//   prints the get_param(inst, key, ...) result for a fresh instance to
//   stdout, nothing else.
//
// Not part of the shipped module; not linked into tests/host. Built ad hoc by
// tools/preview_pages.mjs into build/dump_contracts.

#include "plugin_api_v1.h"
#include "audio_fx_api_v2.h"
#include <cstdio>
#include <cstring>

extern "C" audio_fx_api_v2_t* move_audio_fx_init_v2(const host_api_v1_t* host);

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: %s <key>\n", argv[0]);
        return 2;
    }

    audio_fx_api_v2_t* api = move_audio_fx_init_v2(nullptr);
    if (!api || !api->create_instance || !api->get_param) {
        std::fprintf(stderr, "move_audio_fx_init_v2 did not return a usable v2 api\n");
        return 1;
    }

    void* inst = api->create_instance(".", nullptr);
    if (!inst) {
        std::fprintf(stderr, "create_instance failed\n");
        return 1;
    }

    char buf[8192];
    buf[0] = '\0';
    int n = api->get_param(inst, argv[1], buf, sizeof(buf));
    (void)n;
    std::fputs(buf, stdout);

    if (api->destroy_instance) api->destroy_instance(inst);
    return 0;
}
