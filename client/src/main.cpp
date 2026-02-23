#include "window.h"
#include "vk_context.h"
#include "noise_gen.h"
#include "marching_cubes.h"

int main() {
    Window window(1280, 720, "Aetheris");
    VkContext ctx = vk_init(window.handle());

    // Generate and upload a 3x3x3 grid of chunks
    for (int x = -1; x <= 1; x++)
    for (int y = -1; y <= 1; y++)
    for (int z = -1; z <= 1; z++) {
        ChunkData data = generateChunk({x, y, z});
        ChunkMesh mesh = marchChunk(data);
        vk_upload_chunk(ctx, mesh);
    }

    while (!window.shouldClose()) {
        window.poll();
        vk_draw(ctx);
    }

    vk_destroy(ctx);
}
