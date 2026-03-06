#pragma once
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include "gltf_loader.h"
#include "view_model_anim.h"

// GPU buffers for one loaded GLB mesh
struct ViewModelMesh {
    VkBuffer      vertBuf   = VK_NULL_HANDLE;
    VmaAllocation vertAlloc = nullptr;
    VkBuffer      idxBuf    = VK_NULL_HANDLE;
    VmaAllocation idxAlloc  = nullptr;
    uint32_t      indexCount = 0;
};

// Position/rotation/scale of the weapon in view space.
// Tweak per weapon to sit correctly in the player's hand.
struct ViewModelTransform {
    glm::vec3 offset   = { 0.25f, -0.28f, -0.45f };
    glm::vec3 rotation = { 0.f,   0.f,    0.f };
    glm::vec3 scale    = { 1.f,   1.f,    1.f };
    glm::vec3 meshCenter = { 0.f, 0.f, 0.f }; // auto-centering offset
};

// Owns the viewmodel pipeline, all loaded meshes, and the animation system.
struct ViewModelRenderer {
    // Pipeline objects
    VkPipeline            pipeline       = VK_NULL_HANDLE;
    VkPipelineLayout      pipelineLayout = VK_NULL_HANDLE;

    // Loaded meshes — index returned by loadMesh()
    std::vector<ViewModelMesh> meshes;

    // Which mesh is currently equipped (-1 = nothing / fists)
    int activeMesh = -1;

    // Per-weapon hand transforms (base pose, edited via transform panel)
    std::vector<ViewModelTransform> transforms;

    // ── Animation system ───────────────────────────────────────────────────
    AnimationPlayer      anim;
    ViewModelAnimEditor  animEditor;

    // ── UI toggle (] key) ──────────────────────────────────────────────────
    // Both the transform debug panel and the animation editor share this flag.
    bool uiVisible = false;

    void setActiveMesh(int idx) { activeMesh = idx; }

    // Call once per frame with delta time to advance animation
    void update(float dt) {
        anim.update(dt);

        // Advance animation editor preview if it's playing
        // (editor manages its own previewTime internally via ImGui::GetIO().DeltaTime)
    }

    // Trigger attack animations from combat input
    void triggerLightAttack() { anim.play(AnimSlot::LightAttack); }
    void triggerHeavyAttack() { anim.play(AnimSlot::HeavyAttack); }

    // ── Lifecycle ─────────────────────────────────────────────────────────
    void init(VkDevice device, VmaAllocator allocator,
              VkRenderPass renderPass, VkExtent2D extent,
              const char* vertSpv, const char* fragSpv);

    void destroy(VkDevice device, VmaAllocator allocator);

    // Upload a GLB to GPU. Returns mesh index or -1 on failure.
    int loadMesh(VkDevice device, VmaAllocator allocator,
                 VkCommandPool pool, VkQueue queue,
                 const GltfModel& model,
                 ViewModelTransform transform = {});

    // Record draw commands. Call after terrain draw, still inside render pass.
    // proj: same projection matrix used for the scene.
    void draw(VkCommandBuffer cmd, const glm::mat4& proj) const;

    // Draw ImGui panels (transform debug + animation editor).
    // Only shown when uiVisible == true.
    void drawDebugUI();
};
