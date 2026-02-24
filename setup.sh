#!/bin/bash
set -e

# ── Deps ──────────────────────────────────────────────────────────────────────
sudo apt-get update -qq
sudo apt-get install -y libenet-dev

# EnTT — header only, drop into thirdparty
if [ ! -f thirdparty/entt/entt.hpp ]; then
    mkdir -p thirdparty/entt
    curl -L https://github.com/skypjack/entt/releases/download/v3.13.2/entt-v3.13.2-include.zip \
         -o /tmp/entt.zip
    unzip -q /tmp/entt.zip -d /tmp/entt_out
    cp /tmp/entt_out/single_include/entt/entt.hpp thirdparty/entt/entt.hpp
    echo "EnTT installed"
fi

# ── Move misplaced shared files to correct locations ──────────────────────────

# chunk_manager belongs in server/
mv shared/include/chunk_manager.h server/include/chunk_manager.h 2>/dev/null || true

# ── Add missing client files ──────────────────────────────────────────────────
# player.h — should be in client/include
if [ ! -f client/include/player.h ]; then
    echo "WARNING: client/include/player.h is missing — paste it from the Claude output"
fi

# ── Clean up empty/stub files that will conflict ─────────────────────────────
# renderer.h/cpp — superseded by vk_context.h/vk_init.cpp
rm -f client/include/renderer.h
rm -f client/src/renderer.cpp
rm -f client/src/mesh_buffer.cpp

# Empty stubs that meson will try to compile
> server/src/network.cpp
> server/src/world.cpp
> client/src/network.cpp

echo ""
echo "Structure fixed. Summary of what's where:"
echo "  shared/include/  — chunk.h, config.h, net_common.h, packets.h, noise_gen.h, marching_cubes.h"
echo "  server/include/  — chunk_manager.h, network.h, world.h"
echo "  client/include/  — vk_context.h, window.h, input.h, camera.h, player.h"
echo ""
echo "Next: add EnTT include path to meson.build, then: meson setup build && ninja -C build"
