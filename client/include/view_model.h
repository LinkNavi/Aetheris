#pragma once
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <unordered_map>
#include "gltf_loader.h"
#include "view_model_anim.h"
#include "item.h"

struct ViewModelMesh {
    VkBuffer      vertBuf    = VK_NULL_HANDLE;
    VmaAllocation vertAlloc  = nullptr;
    VkBuffer      idxBuf     = VK_NULL_HANDLE;
    VmaAllocation idxAlloc   = nullptr;
    uint32_t      indexCount = 0;
};

struct ViewModelTransform {
    glm::vec3 offset     = { 0.25f, -0.28f, -0.45f };
    glm::vec3 rotation   = { 0.f,    0.f,    0.f };
    glm::vec3 scale      = { 1.f,    1.f,    1.f };
    glm::vec3 meshCenter = { 0.f,    0.f,    0.f };
};

// One entry per registered item type
struct ItemViewModel {
    int                meshIdx          = -1;
    ViewModelTransform mainTransform;    // right hand
    ViewModelTransform offhandTransform; // left hand
};

enum class DebugHandSlot { MainHand = 0, OffHand = 1 };

struct ViewModelRenderer {
    // ── Vulkan ────────────────────────────────────────────────────────────
    VkPipeline            pipeline       = VK_NULL_HANDLE;
    VkPipelineLayout      pipelineLayout = VK_NULL_HANDLE;

    std::vector<ViewModelMesh>      meshes;
    std::vector<ViewModelTransform> transforms; // base transform per mesh slot

    // ── Item registry ─────────────────────────────────────────────────────
    // Key: (int)ItemID
    std::unordered_map<int, ItemViewModel> itemRegistry;

    // Special mesh slots
    int armMeshIdx     = -1; // arm.glb — always visible
    int activeMeshIdx  = -1; // current main-hand item mesh (-1 = fists)
    int offhandMeshIdx = -1; // current offhand item mesh  (-1 = nothing)

    // Live transforms for the active slots (copied from registry on swap,
    // then editable in debug UI without touching the registry)
    ViewModelTransform activeMainTransform;
    ViewModelTransform activeOffhandTransform;

    // Last seen item IDs — detect changes cheaply
    ItemID lastMainHandId = ItemID::None;
    ItemID lastOffhandId  = ItemID::None;

    // ── Animation ─────────────────────────────────────────────────────────
    AnimationPlayer     anim;        // main hand
    AnimationPlayer     offhandAnim; // offhand (idle only by default)
    ViewModelAnimEditor animEditor;

    // ── Debug UI ──────────────────────────────────────────────────────────
    bool          uiVisible = false;
    DebugHandSlot debugSlot = DebugHandSlot::MainHand;

    // ── Lifecycle ──────────────────────────────────────────────────────────
    void init(VkDevice device, VmaAllocator allocator,
              VkRenderPass renderPass, VkExtent2D extent,
              const char* vertSpv, const char* fragSpv);

    void destroy(VkDevice device, VmaAllocator allocator);

    // Upload a GLB to GPU, returns mesh index.
    int loadMesh(VkDevice device, VmaAllocator allocator,
                 VkCommandPool pool, VkQueue queue,
                 const GltfModel& model,
                 ViewModelTransform transform = {});

    // Register an already-loaded mesh as the view model for an item.
    // offhandTransform: leave default-constructed and it will mirror mainTransform.
    void registerItemMesh(ItemID id, int meshIdx,
                          ViewModelTransform mainTransform,
                          ViewModelTransform offhandTransform = {});

    // Call every frame with whatever is currently held in each hand.
    // Swaps meshes only when the ID changes.
    void syncEquipped(ItemID mainHandId, ItemID offhandId);

    // ── Per-frame ──────────────────────────────────────────────────────────
    void update(float dt) { anim.update(dt); offhandAnim.update(dt); }

    void triggerLightAttack() { anim.play(AnimSlot::LightAttack); }
    void triggerHeavyAttack() { anim.play(AnimSlot::HeavyAttack); }

    void draw(VkCommandBuffer cmd, const glm::mat4& proj, VkExtent2D extent) const;
    void drawDebugUI();

private:
    void drawMesh(VkCommandBuffer cmd, const glm::mat4& proj,
                  int meshIdx, const ViewModelTransform& t,
                  const AnimationPlayer& ap, bool mirrorX) const;

    void drawTransformEditor(ViewModelTransform& t, const AnimationPlayer& ap,
                             ItemID itemId, bool isOffhand);
};
