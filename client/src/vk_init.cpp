#include "vk_context.h"
#include <stdexcept>
#include <fstream>
#include <vector>
#include <cstring>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

static void check(VkResult r, const char* msg) {
    if (r != VK_SUCCESS) throw std::runtime_error(msg);
}

static std::vector<uint32_t> loadSpv(const char* path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) throw std::runtime_error(std::string("Cannot open shader: ") + path);
    size_t size = f.tellg();
    std::vector<uint32_t> buf(size / 4);
    f.seekg(0);
    f.read(reinterpret_cast<char*>(buf.data()), size);
    return buf;
}

static VkShaderModule makeModule(VkDevice dev, const std::vector<uint32_t>& code) {
    VkShaderModuleCreateInfo ci{};
    ci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = code.size() * 4;
    ci.pCode    = code.data();
    VkShaderModule mod;
    if (vkCreateShaderModule(dev, &ci, nullptr, &mod) != VK_SUCCESS)
        throw std::runtime_error("Failed to create shader module");
    return mod;
}

// Reuses the persistent staging buffer — no alloc/free per chunk
static void uploadBuffer(VkContext& ctx,
                         VkBufferUsageFlags usage,
                         const void* data, VkDeviceSize size,
                         VkBuffer& outBuffer, VmaAllocation& outAlloc) {
    // Grow staging buffer if data is larger than current capacity (rare)
    if (size > ctx.stagingSize) {
        vmaDestroyBuffer(ctx.allocator, ctx.stagingBuffer, ctx.stagingAlloc);
        ctx.stagingSize = size * 2;
        VkBufferCreateInfo sCI{};
        sCI.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        sCI.size  = ctx.stagingSize;
        sCI.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        VmaAllocationCreateInfo sACI{};
        sACI.usage = VMA_MEMORY_USAGE_CPU_ONLY;
        sACI.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VmaAllocationInfo info{};
        vmaCreateBuffer(ctx.allocator, &sCI, &sACI,
                        &ctx.stagingBuffer, &ctx.stagingAlloc, &info);
        ctx.stagingMapped = info.pMappedData;
    }

    memcpy(ctx.stagingMapped, data, size);

    VkBufferCreateInfo bufCI{};
    bufCI.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufCI.size  = size;
    bufCI.usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VmaAllocationCreateInfo bufAllocCI{};
    bufAllocCI.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    vmaCreateBuffer(ctx.allocator, &bufCI, &bufAllocCI, &outBuffer, &outAlloc, nullptr);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool        = ctx.commandPool;
    allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(ctx.device.device, &allocInfo, &cmd);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    VkBufferCopy copy{0, 0, size};
    vkCmdCopyBuffer(cmd, ctx.stagingBuffer, outBuffer, 1, &copy);
    vkEndCommandBuffer(cmd);

    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers    = &cmd;
    vkQueueSubmit(ctx.graphicsQueue, 1, &submit, VK_NULL_HANDLE);
    vkQueueWaitIdle(ctx.graphicsQueue);
    vkFreeCommandBuffers(ctx.device.device, ctx.commandPool, 1, &cmd);
}

static void createDepthResources(VkContext& ctx) {
    VkImageCreateInfo imgCI{};
    imgCI.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imgCI.imageType     = VK_IMAGE_TYPE_2D;
    imgCI.format        = VK_FORMAT_D32_SFLOAT;
    imgCI.extent        = {ctx.swapchain.extent.width, ctx.swapchain.extent.height, 1};
    imgCI.mipLevels     = 1;
    imgCI.arrayLayers   = 1;
    imgCI.samples       = VK_SAMPLE_COUNT_1_BIT;
    imgCI.tiling        = VK_IMAGE_TILING_OPTIMAL;
    imgCI.usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

    VmaAllocationCreateInfo allocCI{};
    allocCI.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    vmaCreateImage(ctx.allocator, &imgCI, &allocCI,
                   &ctx.depthImage, &ctx.depthAlloc, nullptr);

    VkImageViewCreateInfo viewCI{};
    viewCI.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewCI.image                           = ctx.depthImage;
    viewCI.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    viewCI.format                          = VK_FORMAT_D32_SFLOAT;
    viewCI.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewCI.subresourceRange.levelCount     = 1;
    viewCI.subresourceRange.layerCount     = 1;
    check(vkCreateImageView(ctx.device.device, &viewCI, nullptr, &ctx.depthImageView),
          "depth image view");
}

VkContext vk_init(GLFWwindow* window) {
    VkContext ctx;

    // Validation layers OFF — significant CPU overhead on low-end hardware
    auto inst = vkb::InstanceBuilder{}
        .set_app_name("Aetheris")
        .request_validation_layers(false)
        .require_api_version(1, 3, 0)
        .build();
    if (!inst) throw std::runtime_error(inst.error().message());
    ctx.instance = inst.value();

    check(glfwCreateWindowSurface(ctx.instance.instance, window, nullptr, &ctx.surface),
          "Failed to create window surface");

    auto phys = vkb::PhysicalDeviceSelector{ctx.instance}
        .set_surface(ctx.surface)
        .set_minimum_version(1, 3)
        .select();
    if (!phys) throw std::runtime_error(phys.error().message());

    auto dev = vkb::DeviceBuilder{phys.value()}.build();
    if (!dev) throw std::runtime_error(dev.error().message());
    ctx.device = dev.value();

    auto gq = ctx.device.get_queue(vkb::QueueType::graphics);
    if (!gq) throw std::runtime_error("No graphics queue");
    ctx.graphicsQueue       = gq.value();
    ctx.graphicsQueueFamily = ctx.device.get_queue_index(vkb::QueueType::graphics).value();

    int w, h;
    glfwGetFramebufferSize(window, &w, &h);

    auto sc = vkb::SwapchainBuilder{ctx.device}
        .set_desired_format({VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
        .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
        .set_desired_extent(w, h)
        .add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
        .build();
    if (!sc) throw std::runtime_error(sc.error().message());
    ctx.swapchain      = sc.value();
    ctx.swapImages     = ctx.swapchain.get_images().value();
    ctx.swapImageViews = ctx.swapchain.get_image_views().value();

    VmaAllocatorCreateInfo vmaInfo{};
    vmaInfo.instance         = ctx.instance.instance;
    vmaInfo.physicalDevice   = ctx.device.physical_device.physical_device;
    vmaInfo.device           = ctx.device.device;
    vmaInfo.vulkanApiVersion = VK_API_VERSION_1_3;
    check(vmaCreateAllocator(&vmaInfo, &ctx.allocator), "Failed to create VMA allocator");

    // Command pool before staging buffer and depth resources
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = ctx.graphicsQueueFamily;
    poolInfo.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    check(vkCreateCommandPool(ctx.device.device, &poolInfo, nullptr, &ctx.commandPool),
          "Failed to create command pool");

    // Persistent staging buffer — reused for every chunk upload
    {
        VkBufferCreateInfo sCI{};
        sCI.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        sCI.size  = ctx.stagingSize;
        sCI.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        VmaAllocationCreateInfo sACI{};
        sACI.usage = VMA_MEMORY_USAGE_CPU_ONLY;
        sACI.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VmaAllocationInfo info{};
        check(vmaCreateBuffer(ctx.allocator, &sCI, &sACI,
                              &ctx.stagingBuffer, &ctx.stagingAlloc, &info),
              "Failed to create staging buffer");
        ctx.stagingMapped = info.pMappedData;
    }

    createDepthResources(ctx);

    // ── Render pass ───────────────────────────────────────────────────────────
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format         = ctx.swapchain.image_format;
    colorAttachment.samples        = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format         = VK_FORMAT_D32_SFLOAT;
    depthAttachment.samples        = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount    = 1;
    subpass.pColorAttachments       = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency dep{};
    dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass    = 0;
    dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                      | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.srcAccessMask = 0;
    dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                      | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                      | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkAttachmentDescription attachments[] = {colorAttachment, depthAttachment};
    VkRenderPassCreateInfo rpci{};
    rpci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpci.attachmentCount = 2;
    rpci.pAttachments    = attachments;
    rpci.subpassCount    = 1;
    rpci.pSubpasses      = &subpass;
    rpci.dependencyCount = 1;
    rpci.pDependencies   = &dep;
    check(vkCreateRenderPass(ctx.device.device, &rpci, nullptr, &ctx.renderPass),
          "Failed to create render pass");

    // ── Framebuffers ──────────────────────────────────────────────────────────
    ctx.framebuffers.resize(ctx.swapImageViews.size());
    for (size_t i = 0; i < ctx.swapImageViews.size(); i++) {
        VkImageView fbAttachments[] = {ctx.swapImageViews[i], ctx.depthImageView};
        VkFramebufferCreateInfo fbci{};
        fbci.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbci.renderPass      = ctx.renderPass;
        fbci.attachmentCount = 2;
        fbci.pAttachments    = fbAttachments;
        fbci.width           = ctx.swapchain.extent.width;
        fbci.height          = ctx.swapchain.extent.height;
        fbci.layers          = 1;
        check(vkCreateFramebuffer(ctx.device.device, &fbci, nullptr, &ctx.framebuffers[i]),
              "Failed to create framebuffer");
    }

    // ── Pipeline ──────────────────────────────────────────────────────────────
    auto vertCode = loadSpv("client/shaders/terrain_vert.spv");
    auto fragCode = loadSpv("client/shaders/terrain_frag.spv");
    VkShaderModule vertMod = makeModule(ctx.device.device, vertCode);
    VkShaderModule fragMod = makeModule(ctx.device.device, fragCode);

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertMod;
    stages[0].pName  = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragMod;
    stages[1].pName  = "main";

    VkVertexInputBindingDescription binding{};
    binding.binding   = 0;
    binding.stride    = sizeof(Vertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrs[2]{};
    attrs[0].binding = 0; attrs[0].location = 0;
    attrs[0].format  = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[0].offset  = offsetof(Vertex, pos);
    attrs[1].binding = 0; attrs[1].location = 1;
    attrs[1].format  = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[1].offset  = offsetof(Vertex, normal);

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount   = 1;
    vertexInput.pVertexBindingDescriptions      = &binding;
    vertexInput.vertexAttributeDescriptionCount = 2;
    vertexInput.pVertexAttributeDescriptions    = attrs;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport viewport{};
    viewport.width    = (float)ctx.swapchain.extent.width;
    viewport.height   = (float)ctx.swapchain.extent.height;
    viewport.maxDepth = 1.0f;
    VkRect2D scissor{};
    scissor.extent = ctx.swapchain.extent;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1; viewportState.pViewports = &viewport;
    viewportState.scissorCount  = 1; viewportState.pScissors  = &scissor;

    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode    = VK_CULL_MODE_BACK_BIT;
    raster.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable  = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp   = VK_COMPARE_OP_LESS;

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments    = &blendAttachment;

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushRange.offset     = 0;
    pushRange.size       = sizeof(glm::mat4);

    VkPipelineLayoutCreateInfo layoutci{};
    layoutci.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutci.pushConstantRangeCount = 1;
    layoutci.pPushConstantRanges    = &pushRange;
    check(vkCreatePipelineLayout(ctx.device.device, &layoutci, nullptr, &ctx.pipelineLayout),
          "Failed to create pipeline layout");

    VkGraphicsPipelineCreateInfo pipelineCI{};
    pipelineCI.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineCI.stageCount          = 2;
    pipelineCI.pStages             = stages;
    pipelineCI.pVertexInputState   = &vertexInput;
    pipelineCI.pInputAssemblyState = &inputAssembly;
    pipelineCI.pViewportState      = &viewportState;
    pipelineCI.pRasterizationState = &raster;
    pipelineCI.pMultisampleState   = &multisample;
    pipelineCI.pDepthStencilState  = &depthStencil;
    pipelineCI.pColorBlendState    = &blend;
    pipelineCI.layout              = ctx.pipelineLayout;
    pipelineCI.renderPass          = ctx.renderPass;
    check(vkCreateGraphicsPipelines(ctx.device.device, VK_NULL_HANDLE, 1,
                                    &pipelineCI, nullptr, &ctx.pipeline),
          "Failed to create pipeline");

    vkDestroyShaderModule(ctx.device.device, vertMod, nullptr);
    vkDestroyShaderModule(ctx.device.device, fragMod, nullptr);

    // ── Command buffers ───────────────────────────────────────────────────────
    ctx.commandBuffers.resize(VkContext::FRAMES_IN_FLIGHT);
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool        = ctx.commandPool;
    allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = VkContext::FRAMES_IN_FLIGHT;
    check(vkAllocateCommandBuffers(ctx.device.device, &allocInfo, ctx.commandBuffers.data()),
          "Failed to allocate command buffers");

    // ── Sync objects ──────────────────────────────────────────────────────────
    ctx.imageAvailable.resize(VkContext::FRAMES_IN_FLIGHT);
    ctx.renderFinished.resize(VkContext::FRAMES_IN_FLIGHT);
    ctx.inFlight.resize(VkContext::FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo semInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (int i = 0; i < VkContext::FRAMES_IN_FLIGHT; i++) {
        check(vkCreateSemaphore(ctx.device.device, &semInfo, nullptr, &ctx.imageAvailable[i]), "semaphore");
        check(vkCreateSemaphore(ctx.device.device, &semInfo, nullptr, &ctx.renderFinished[i]), "semaphore");
        check(vkCreateFence(ctx.device.device, &fenceInfo, nullptr, &ctx.inFlight[i]), "fence");
    }

    return ctx;
}

void vk_upload_chunk(VkContext& ctx, const ChunkMesh& mesh) {
    if (mesh.vertices.empty()) return;

    GpuChunk gpu{};
    gpu.indexCount = static_cast<uint32_t>(mesh.indices.size());

    uploadBuffer(ctx, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        mesh.vertices.data(), mesh.vertices.size() * sizeof(Vertex),
        gpu.vertexBuffer, gpu.vertexAlloc);

    uploadBuffer(ctx, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        mesh.indices.data(), mesh.indices.size() * sizeof(uint32_t),
        gpu.indexBuffer, gpu.indexAlloc);

    ctx.chunks[mesh.coord] = gpu;
}

void vk_draw(VkContext& ctx, const glm::mat4& viewProj) {
    uint32_t frame = ctx.currentFrame;

    vkWaitForFences(ctx.device.device, 1, &ctx.inFlight[frame], VK_TRUE, UINT64_MAX);
    vkResetFences(ctx.device.device, 1, &ctx.inFlight[frame]);

    uint32_t imageIndex;
    vkAcquireNextImageKHR(ctx.device.device, ctx.swapchain.swapchain, UINT64_MAX,
                          ctx.imageAvailable[frame], VK_NULL_HANDLE, &imageIndex);

    VkCommandBuffer cmd = ctx.commandBuffers[frame];
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vkBeginCommandBuffer(cmd, &beginInfo);

    VkClearValue clearValues[2]{};
    clearValues[0].color        = {{0.1f, 0.1f, 0.15f, 1.0f}};
    clearValues[1].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo rpBegin{};
    rpBegin.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpBegin.renderPass        = ctx.renderPass;
    rpBegin.framebuffer       = ctx.framebuffers[imageIndex];
    rpBegin.renderArea.extent = ctx.swapchain.extent;
    rpBegin.clearValueCount   = 2;
    rpBegin.pClearValues      = clearValues;

    vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx.pipeline);

    // ── Frustum culling (Gribb/Hartmann) ─────────────────────────────────────
    struct Plane { glm::vec3 n; float d; };
    Plane planes[6];
    const glm::mat4& m = viewProj;
    planes[0] = {{ m[0][3]+m[0][0], m[1][3]+m[1][0], m[2][3]+m[2][0] }, m[3][3]+m[3][0] }; // left
    planes[1] = {{ m[0][3]-m[0][0], m[1][3]-m[1][0], m[2][3]-m[2][0] }, m[3][3]-m[3][0] }; // right
    planes[2] = {{ m[0][3]+m[0][1], m[1][3]+m[1][1], m[2][3]+m[2][1] }, m[3][3]+m[3][1] }; // bottom
    planes[3] = {{ m[0][3]-m[0][1], m[1][3]-m[1][1], m[2][3]-m[2][1] }, m[3][3]-m[3][1] }; // top
    planes[4] = {{ m[0][3]+m[0][2], m[1][3]+m[1][2], m[2][3]+m[2][2] }, m[3][3]+m[3][2] }; // near
    planes[5] = {{ m[0][3]-m[0][2], m[1][3]-m[1][2], m[2][3]-m[2][2] }, m[3][3]-m[3][2] }; // far

    auto chunkVisible = [&](ChunkCoord coord) -> bool {
        glm::vec3 mn = glm::vec3(coord.x, coord.y, coord.z) * (float)ChunkData::SIZE;
        glm::vec3 mx = mn + (float)ChunkData::SIZE;
        for (auto& p : planes) {
            glm::vec3 pv{
                p.n.x > 0 ? mx.x : mn.x,
                p.n.y > 0 ? mx.y : mn.y,
                p.n.z > 0 ? mx.z : mn.z
            };
            if (glm::dot(p.n, pv) + p.d < 0.f) return false;
        }
        return true;
    };

    for (auto& [coord, gpu] : ctx.chunks) {
        if (!chunkVisible(coord)) continue;

        glm::mat4 model = glm::translate(glm::mat4(1.f),
            glm::vec3(coord.x * ChunkData::SIZE,
                      coord.y * ChunkData::SIZE,
                      coord.z * ChunkData::SIZE));
        glm::mat4 mvp = viewProj * model;

        vkCmdPushConstants(cmd, ctx.pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
                           0, sizeof(glm::mat4), &mvp);

        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &gpu.vertexBuffer, &offset);
        vkCmdBindIndexBuffer(cmd, gpu.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, gpu.indexCount, 1, 0, 0, 0);
    }

    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.waitSemaphoreCount   = 1;
    submit.pWaitSemaphores      = &ctx.imageAvailable[frame];
    submit.pWaitDstStageMask    = &waitStage;
    submit.commandBufferCount   = 1;
    submit.pCommandBuffers      = &cmd;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores    = &ctx.renderFinished[frame];
    vkQueueSubmit(ctx.graphicsQueue, 1, &submit, ctx.inFlight[frame]);

    VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores    = &ctx.renderFinished[frame];
    present.swapchainCount     = 1;
    present.pSwapchains        = &ctx.swapchain.swapchain;
    present.pImageIndices      = &imageIndex;
    vkQueuePresentKHR(ctx.graphicsQueue, &present);

    ctx.currentFrame = (frame + 1) % VkContext::FRAMES_IN_FLIGHT;
}

void vk_destroy(VkContext& ctx) {
    vkDeviceWaitIdle(ctx.device.device);

    for (auto& [coord, gpu] : ctx.chunks) {
        vmaDestroyBuffer(ctx.allocator, gpu.vertexBuffer, gpu.vertexAlloc);
        vmaDestroyBuffer(ctx.allocator, gpu.indexBuffer,  gpu.indexAlloc);
    }

    vmaDestroyBuffer(ctx.allocator, ctx.stagingBuffer, ctx.stagingAlloc);
    vkDestroyImageView(ctx.device.device, ctx.depthImageView, nullptr);
    vmaDestroyImage(ctx.allocator, ctx.depthImage, ctx.depthAlloc);
    vmaDestroyAllocator(ctx.allocator);

    for (int i = 0; i < VkContext::FRAMES_IN_FLIGHT; i++) {
        vkDestroySemaphore(ctx.device.device, ctx.imageAvailable[i], nullptr);
        vkDestroySemaphore(ctx.device.device, ctx.renderFinished[i], nullptr);
        vkDestroyFence(ctx.device.device, ctx.inFlight[i], nullptr);
    }

    vkDestroyCommandPool(ctx.device.device, ctx.commandPool, nullptr);
    vkDestroyPipeline(ctx.device.device, ctx.pipeline, nullptr);
    vkDestroyPipelineLayout(ctx.device.device, ctx.pipelineLayout, nullptr);

    for (auto fb : ctx.framebuffers)
        vkDestroyFramebuffer(ctx.device.device, fb, nullptr);

    vkDestroyRenderPass(ctx.device.device, ctx.renderPass, nullptr);

    for (auto iv : ctx.swapImageViews)
        vkDestroyImageView(ctx.device.device, iv, nullptr);

    vkb::destroy_swapchain(ctx.swapchain);
    vkb::destroy_device(ctx.device);
    vkDestroySurfaceKHR(ctx.instance.instance, ctx.surface, nullptr);
    vkb::destroy_instance(ctx.instance);
}
