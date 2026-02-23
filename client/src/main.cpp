#include "window.h"
#include "vk_context.h"

int main() {
    Window window(1280, 720, "Aetheris");
    VkContext ctx = vk_init(window.handle());

    while (!window.shouldClose()) {
        window.poll();
    }

    vk_destroy(ctx);
}
