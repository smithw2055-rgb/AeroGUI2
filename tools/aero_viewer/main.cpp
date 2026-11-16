#include <cstdio>

#include "aero/app/AeroAppHost.hpp"

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    aero::AeroAppHost host;
    (void)host.InitializeRuntime();
    std::printf("aero-viewer v3.0 (Phase-0 skeleton)\n");
    return 0;
}
