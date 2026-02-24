#include "vk_context.h"
#include "asset_path.h"
#include "log.h"
#include <stdexcept>
#include <fstream>
#include <vector>
#include <algorithm>
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
    size_t sz = (size_t)f.tellg();
    std::vector<uint32_t> buf(sz / 4);
    f.seekg(0);
    f.read(reinterpret_cast<char*>(buf.data()), sz);
    return buf;
}

static VkShaderModule makeModule(VkDevice dev, const std::vector<uint32_t>& code) {
    VkShaderModuleCreateInfo ci{};
    ci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = code.size() * 4;
    ci.pCode    = code.data();
    VkShaderModule m;
    check(vkCreateShaderModule(dev, &ci, nullptr, &m), "shader module");
    return m;
}

// ── MegaBuffer free-list ──────────────────────────────────────────────────────

static void doFreeRange(std::vector<MegaBuffer::Range>& list,
                        uint32_t offset, uint32_t size) {
    list.push_back({offset, size});
    std::sort(list.begin(), list.end(),
              [](const MegaBuffer::Range& a, const MegaBuffer::Range& b){
                  return a.offset < b.offset; });
    for (size_t i = 0; i + 1 < list.size(); ) {
        if (list[i].offset + list[i].size == list[i+1].offset) {
            list[i].size += list[i+1].size;
            list.erase(list.begin() + (ptrdiff_t)(i + 1));
        } else ++i;
    }
}

uint32_t MegaBuffer::allocVerts(uint32_t count) {
    for (auto it = vertRanges.begin(); it != vertRanges.end(); ++it) {
        if (it->size >= count) {
            uint32_t off = it->offset;
            it->offset  += count;
            it->size    -= count;
            if (it->size == 0) vertRanges.erase(it);
            return off;
        }
    }
    throw std::runtime_error("MegaBuffer: vertex space exhausted");
}

uint32_t MegaBuffer::allocInds(uint32_t count) {
    for (auto it = indRanges.begin(); it != indRanges.end(); ++it) {
        if (it->size >= count) {
            uint32_t off = it->offset;
            it->offset  += count;
            it->size    -= count;
            if (it->size == 0) indRanges.erase(it);
            return off;
        }
    }
    throw std::runtime_error("MegaBuffer: index space exhausted");
}

void MegaBuffer::releaseVerts(uint32_t offset, uint32_t count) {
    doFreeRange(vertRanges, offset, count);
}

void MegaBuffer::releaseInds(uint32_t offset, uint32_t count) {
    doFreeRange(indRanges, offset, count);
}

// ── Depth image ───────────────────────────────────────────────────────────────

static void createDepthResources(VkContext& ctx) {
    VkImageCreateInfo imgCI{};
    imgCI.sType       = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imgCI.imageType   = VK_IMAGE_TYPE_2D;
    imgCI.format      = VK_FORMAT_D32_SFLOAT;
    imgCI.extent      = {ctx.swapchain.extent.width, ctx.swapchain.extent.height, 1};
    imgCI.mipLevels   = 1; imgCI.arrayLayers = 1;
    imgCI.samples     = VK_SAMPLE_COUNT_1_BIT;
    imgCI.tiling      = VK_IMAGE_TILING_OPTIMAL;
    imgCI.usage       = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    VmaAllocationCreateInfo aCI{};
    aCI.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    vmaCreateImage(ctx.allocator, &imgCI, &aCI,
                   &ctx.depthImage, &ctx.depthAlloc, nullptr);

    VkImageViewCreateInfo vCI{};
    vCI.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vCI.image            = ctx.depthImage;
    vCI.viewType         = VK_IMAGE_VIEW_TYPE_2D;
    vCI.format           = VK_FORMAT_D32_SFLOAT;
    vCI.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
    check(vkCreateImageView(ctx.device.device, &vCI, nullptr, &ctx.depthImageView),
          "depth view");
}

// ── vk_init ───────────────────────────────────────────────────────────────────

VkContext vk_init(GLFWwindow* window) {
    VkContext ctx;

    auto inst = vkb::InstanceBuilder{}
        .set_app_name("Aetheris")
        .request_validation_layers(false)
        .require_api_version(1, 3, 0)
        .build();
    if (!inst) throw std::runtime_error(inst.error().message());
    ctx.instance = inst.value();

    check(glfwCreateWindowSurface(ctx.instance.instance, window, nullptr, &ctx.surface),
          "window surface");

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
        .build();
    if (!sc) throw std::runtime_error(sc.error().message());
    ctx.swapchain      = sc.value();
    ctx.swapImages     = ctx.swapchain.get_images().value();
    ctx.swapImageViews = ctx.swapchain.get_image_views().value();

    VmaAllocatorCreateInfo vmaCI{};
    vmaCI.instance         = ctx.instance.instance;
    vmaCI.physicalDevice   = ctx.device.physical_device.physical_device;
    vmaCI.device           = ctx.device.device;
    vmaCI.vulkanApiVersion = VK_API_VERSION_1_3;
    check(vmaCreateAllocator(&vmaCI, &ctx.allocator), "VMA allocator");

    // ── Command pool ──────────────────────────────────────────────────────────
    VkCommandPoolCreateInfo poolCI{};
    poolCI.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolCI.queueFamilyIndex = ctx.graphicsQueueFamily;
    poolCI.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    check(vkCreateCommandPool(ctx.device.device, &poolCI, nullptr, &ctx.commandPool),
          "cmd pool");

    // ── Staging buffer ────────────────────────────────────────────────────────
    {
        VkBufferCreateInfo bCI{};
        bCI.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bCI.size  = ctx.stagingSize;
        bCI.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        VmaAllocationCreateInfo aCI{};
        aCI.usage = VMA_MEMORY_USAGE_CPU_ONLY;
        aCI.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VmaAllocationInfo info{};
        check(vmaCreateBuffer(ctx.allocator, &bCI, &aCI,
                              &ctx.stagingBuffer, &ctx.stagingAlloc, &info),
              "staging buf");
        ctx.stagingMapped = info.pMappedData;
    }

    // ── Upload fence + dedicated command buffer ───────────────────────────────
    {
        VkCommandBufferAllocateInfo aI{};
        aI.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        aI.commandPool        = ctx.commandPool;
        aI.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        aI.commandBufferCount = 1;
        check(vkAllocateCommandBuffers(ctx.device.device, &aI, &ctx.uploadCmd),
              "upload cmd");
        VkFenceCreateInfo fCI{};
        fCI.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fCI.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        check(vkCreateFence(ctx.device.device, &fCI, nullptr, &ctx.uploadFence),
              "upload fence");
    }

    // ── Mega vertex + index buffers ───────────────────────────────────────────
    {
        auto makeGpuBuf = [&](VkDeviceSize size, VkBufferUsageFlags usage,
                               VkBuffer& buf, VmaAllocation& alloc) {
            VkBufferCreateInfo bCI{};
            bCI.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bCI.size  = size;
            bCI.usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            VmaAllocationCreateInfo aCI{};
            aCI.usage = VMA_MEMORY_USAGE_GPU_ONLY;
            check(vmaCreateBuffer(ctx.allocator, &bCI, &aCI, &buf, &alloc, nullptr),
                  "mega buf");
        };
        makeGpuBuf(MEGA_VERTEX_CAP * sizeof(Vertex),
                   VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                   ctx.mega.vertexBuffer, ctx.mega.vertexAlloc);
        makeGpuBuf(MEGA_INDEX_CAP * sizeof(uint32_t),
                   VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                   ctx.mega.indexBuffer, ctx.mega.indexAlloc);
        ctx.mega.vertRanges.push_back({0, MEGA_VERTEX_CAP});
        ctx.mega.indRanges .push_back({0, MEGA_INDEX_CAP});
    }

    // ── Per-frame indirect + per-chunk buffers (CPU_TO_GPU, mapped) ───────────
    for (int i = 0; i < 2; i++) {
        auto makeHostBuf = [&](VkDeviceSize size, VkBufferUsageFlags usage,
                                VkBuffer& buf, VmaAllocation& alloc, void*& mapped) {
            VkBufferCreateInfo bCI{};
            bCI.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bCI.size  = size;
            bCI.usage = usage;
            VmaAllocationCreateInfo aCI{};
            aCI.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
            aCI.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
            VmaAllocationInfo info{};
            check(vmaCreateBuffer(ctx.allocator, &bCI, &aCI, &buf, &alloc, &info),
                  "host buf");
            mapped = info.pMappedData;
        };
        makeHostBuf(VkContext::MAX_DRAW_CHUNKS * sizeof(DrawCmd),
                    VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
                    ctx.indirectBuffer[i], ctx.indirectAlloc[i], ctx.indirectMapped[i]);
        makeHostBuf(VkContext::MAX_DRAW_CHUNKS * sizeof(ChunkDrawData),
                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                    ctx.perChunkBuffer[i], ctx.perChunkAlloc[i], ctx.perChunkMapped[i]);
    }

    createDepthResources(ctx);

    // ── Render pass ───────────────────────────────────────────────────────────
    VkAttachmentDescription colorAtt{};
    colorAtt.format         = ctx.swapchain.image_format;
    colorAtt.samples        = VK_SAMPLE_COUNT_1_BIT;
    colorAtt.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAtt.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    colorAtt.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAtt.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAtt.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription depthAtt{};
    depthAtt.format         = VK_FORMAT_D32_SFLOAT;
    depthAtt.samples        = VK_SAMPLE_COUNT_1_BIT;
    depthAtt.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAtt.storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAtt.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAtt.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAtt.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount    = 1;
    subpass.pColorAttachments       = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency dep{};
    dep.srcSubpass    = VK_SUBPASS_EXTERNAL; dep.dstSubpass = 0;
    dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                      | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.dstStageMask  = dep.srcStageMask;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                      | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkAttachmentDescription atts[] = {colorAtt, depthAtt};
    VkRenderPassCreateInfo rpCI{};
    rpCI.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpCI.attachmentCount = 2; rpCI.pAttachments  = atts;
    rpCI.subpassCount    = 1; rpCI.pSubpasses    = &subpass;
    rpCI.dependencyCount = 1; rpCI.pDependencies = &dep;
    check(vkCreateRenderPass(ctx.device.device, &rpCI, nullptr, &ctx.renderPass),
          "render pass");

    // ── Framebuffers ──────────────────────────────────────────────────────────
    ctx.framebuffers.resize(ctx.swapImageViews.size());
    for (size_t i = 0; i < ctx.swapImageViews.size(); i++) {
        VkImageView fbAtts[] = {ctx.swapImageViews[i], ctx.depthImageView};
        VkFramebufferCreateInfo fbCI{};
        fbCI.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbCI.renderPass      = ctx.renderPass;
        fbCI.attachmentCount = 2; fbCI.pAttachments = fbAtts;
        fbCI.width           = ctx.swapchain.extent.width;
        fbCI.height          = ctx.swapchain.extent.height;
        fbCI.layers          = 1;
        check(vkCreateFramebuffer(ctx.device.device, &fbCI, nullptr,
                                  &ctx.framebuffers[i]), "framebuf");
    }

    // ── Descriptor set layout (storage buffer for per-chunk draw data) ────────
    {
        VkDescriptorSetLayoutBinding binding{};
        binding.binding         = 0;
        binding.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        binding.descriptorCount = 1;
        binding.stageFlags      = VK_SHADER_STAGE_VERTEX_BIT;

        VkDescriptorSetLayoutCreateInfo dsCI{};
        dsCI.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dsCI.bindingCount = 1; dsCI.pBindings = &binding;
        check(vkCreateDescriptorSetLayout(ctx.device.device, &dsCI, nullptr,
                                          &ctx.dsLayout), "ds layout");

        VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2};
        VkDescriptorPoolCreateInfo dpCI{};
        dpCI.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        dpCI.maxSets       = 2;
        dpCI.poolSizeCount = 1; dpCI.pPoolSizes = &poolSize;
        check(vkCreateDescriptorPool(ctx.device.device, &dpCI, nullptr, &ctx.dsPool),
              "ds pool");

        VkDescriptorSetLayout layouts[2] = {ctx.dsLayout, ctx.dsLayout};
        VkDescriptorSetAllocateInfo dsAI{};
        dsAI.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dsAI.descriptorPool     = ctx.dsPool;
        dsAI.descriptorSetCount = 2;
        dsAI.pSetLayouts        = layouts;
        check(vkAllocateDescriptorSets(ctx.device.device, &dsAI, ctx.dsSets),
              "ds alloc");

        for (int i = 0; i < 2; i++) {
            VkDescriptorBufferInfo bufInfo{};
            bufInfo.buffer = ctx.perChunkBuffer[i];
            bufInfo.offset = 0;
            bufInfo.range  = VkContext::MAX_DRAW_CHUNKS * sizeof(ChunkDrawData);
            VkWriteDescriptorSet write{};
            write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet          = ctx.dsSets[i];
            write.dstBinding      = 0;
            write.descriptorCount = 1;
            write.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            write.pBufferInfo     = &bufInfo;
            vkUpdateDescriptorSets(ctx.device.device, 1, &write, 0, nullptr);
        }
    }

    // ── Pipeline ──────────────────────────────────────────────────────────────
    auto vertCode = loadSpv(AssetPath::get("terrain_vert.spv").c_str());
    auto fragCode = loadSpv(AssetPath::get("terrain_frag.spv").c_str());
    Log::info("Shaders loaded");

    VkShaderModule vertMod = makeModule(ctx.device.device, vertCode);
    VkShaderModule fragMod = makeModule(ctx.device.device, fragCode);

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertMod; stages[0].pName = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragMod; stages[1].pName = "main";

    VkVertexInputBindingDescription vBinding{};
    vBinding.binding   = 0;
    vBinding.stride    = sizeof(Vertex);
    vBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

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
    vertexInput.pVertexBindingDescriptions      = &vBinding;
    vertexInput.vertexAttributeDescriptionCount = 2;
    vertexInput.pVertexAttributeDescriptions    = attrs;

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport vp{};
    vp.width    = (float)ctx.swapchain.extent.width;
    vp.height   = (float)ctx.swapchain.extent.height;
    vp.maxDepth = 1.f;
    VkRect2D sc2{};
    sc2.extent = ctx.swapchain.extent;

    VkPipelineViewportStateCreateInfo vs{};
    vs.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vs.viewportCount = 1; vs.pViewports = &vp;
    vs.scissorCount  = 1; vs.pScissors  = &sc2;

    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode    = VK_CULL_MODE_BACK_BIT;
    raster.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth   = 1.f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable  = VK_TRUE;
    ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp   = VK_COMPARE_OP_LESS;

    VkPipelineColorBlendAttachmentState blendAtt{};
    blendAtt.colorWriteMask = 0xF;

    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1; blend.pAttachments = &blendAtt;

    // Push constant: mat4 viewProj + vec4 params
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.size       = sizeof(glm::mat4) + sizeof(glm::vec4);

    VkPipelineLayoutCreateInfo layoutCI{};
    layoutCI.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutCI.setLayoutCount         = 1; layoutCI.pSetLayouts         = &ctx.dsLayout;
    layoutCI.pushConstantRangeCount = 1; layoutCI.pPushConstantRanges = &pushRange;
    check(vkCreatePipelineLayout(ctx.device.device, &layoutCI, nullptr,
                                 &ctx.pipelineLayout), "pipeline layout");

    VkGraphicsPipelineCreateInfo pCI{};
    pCI.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pCI.stageCount          = 2;   pCI.pStages             = stages;
    pCI.pVertexInputState   = &vertexInput;
    pCI.pInputAssemblyState = &ia;
    pCI.pViewportState      = &vs;
    pCI.pRasterizationState = &raster;
    pCI.pMultisampleState   = &ms;
    pCI.pDepthStencilState  = &ds;
    pCI.pColorBlendState    = &blend;
    pCI.layout              = ctx.pipelineLayout;
    pCI.renderPass          = ctx.renderPass;
    check(vkCreateGraphicsPipelines(ctx.device.device, VK_NULL_HANDLE, 1, &pCI,
                                    nullptr, &ctx.pipeline), "pipeline");

    vkDestroyShaderModule(ctx.device.device, vertMod, nullptr);
    vkDestroyShaderModule(ctx.device.device, fragMod, nullptr);

    // ── Command buffers ───────────────────────────────────────────────────────
    ctx.commandBuffers.resize(VkContext::FRAMES_IN_FLIGHT);
    VkCommandBufferAllocateInfo cbAI{};
    cbAI.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbAI.commandPool        = ctx.commandPool;
    cbAI.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbAI.commandBufferCount = VkContext::FRAMES_IN_FLIGHT;
    check(vkAllocateCommandBuffers(ctx.device.device, &cbAI,
                                   ctx.commandBuffers.data()), "cmd bufs");

    // ── Sync objects ──────────────────────────────────────────────────────────
    ctx.imageAvailable.resize(VkContext::FRAMES_IN_FLIGHT);
    ctx.renderFinished.resize(VkContext::FRAMES_IN_FLIGHT);
    ctx.inFlight.resize(VkContext::FRAMES_IN_FLIGHT);
    VkSemaphoreCreateInfo semCI{};
    semCI.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fenCI{};
    fenCI.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenCI.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (int i = 0; i < VkContext::FRAMES_IN_FLIGHT; i++) {
        check(vkCreateSemaphore(ctx.device.device, &semCI, nullptr,
                                &ctx.imageAvailable[i]), "sem");
        check(vkCreateSemaphore(ctx.device.device, &semCI, nullptr,
                                &ctx.renderFinished[i]), "sem");
        check(vkCreateFence(ctx.device.device, &fenCI, nullptr,
                            &ctx.inFlight[i]), "fence");
    }

    Log::info("Vulkan initialised");
    return ctx;
}

// ── Upload ────────────────────────────────────────────────────────────────────

void vk_upload_chunk(VkContext& ctx, const ChunkMesh& mesh) {
    if (mesh.vertices.empty()) return;
    PendingUpload u;
    u.coord    = mesh.coord;
    u.vertices = mesh.vertices;
    u.indices  = mesh.indices;
    ctx.uploadQueue.push_back(std::move(u));
}

static void flushUploads(VkContext& ctx) {
    if (ctx.uploadQueue.empty()) return;

    vkWaitForFences(ctx.device.device, 1, &ctx.uploadFence, VK_TRUE, UINT64_MAX);
    vkResetFences  (ctx.device.device, 1, &ctx.uploadFence);
    vkResetCommandBuffer(ctx.uploadCmd, 0);

    VkCommandBufferBeginInfo bI{};
    bI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bI.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(ctx.uploadCmd, &bI);

    uint8_t*     staging = static_cast<uint8_t*>(ctx.stagingMapped);
    VkDeviceSize cursor  = 0;

    for (auto& u : ctx.uploadQueue) {
        auto existing = ctx.chunks.find(u.coord);
        if (existing != ctx.chunks.end()) {
            ctx.mega.releaseVerts(existing->second.vertexOffset,
                                  existing->second.vertexCount);
            ctx.mega.releaseInds (existing->second.indexOffset,
                                  existing->second.indexCount);
            ctx.chunks.erase(existing);
        }

        uint32_t vc = (uint32_t)u.vertices.size();
        uint32_t ic = (uint32_t)u.indices.size();

        GpuChunk gpu{};
        gpu.vertexCount  = vc;
        gpu.indexCount   = ic;
        gpu.vertexOffset = ctx.mega.allocVerts(vc);
        gpu.indexOffset  = ctx.mega.allocInds (ic);

        VkDeviceSize vSize = vc * sizeof(Vertex);
        VkDeviceSize iSize = ic * sizeof(uint32_t);

        memcpy(staging + cursor,         u.vertices.data(), vSize);
        memcpy(staging + cursor + vSize, u.indices.data(),  iSize);

        VkBufferCopy vc2{cursor,         gpu.vertexOffset * sizeof(Vertex),    vSize};
        VkBufferCopy ic2{cursor + vSize, gpu.indexOffset  * sizeof(uint32_t),  iSize};
        vkCmdCopyBuffer(ctx.uploadCmd, ctx.stagingBuffer, ctx.mega.vertexBuffer, 1, &vc2);
        vkCmdCopyBuffer(ctx.uploadCmd, ctx.stagingBuffer, ctx.mega.indexBuffer,  1, &ic2);

        cursor += vSize + iSize;
        ctx.chunks[u.coord] = gpu;

        if (cursor + 4 * 1024 * 1024 > ctx.stagingSize) break;
    }
    ctx.uploadQueue.clear();

    vkEndCommandBuffer(ctx.uploadCmd);

    VkSubmitInfo sI{};
    sI.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    sI.commandBufferCount = 1;
    sI.pCommandBuffers    = &ctx.uploadCmd;
    vkQueueSubmit(ctx.graphicsQueue, 1, &sI, ctx.uploadFence);
    // Fence is checked at start of next flushUploads — no stall here
}

void vk_remove_chunk(VkContext& ctx, ChunkCoord coord) {
    auto it = ctx.chunks.find(coord);
    if (it == ctx.chunks.end()) return;
    vkDeviceWaitIdle(ctx.device.device);
    ctx.mega.releaseVerts(it->second.vertexOffset, it->second.vertexCount);
    ctx.mega.releaseInds (it->second.indexOffset,  it->second.indexCount);
    ctx.chunks.erase(it);
}

// ── Draw ──────────────────────────────────────────────────────────────────────

void vk_draw(VkContext& ctx, const glm::mat4& viewProj,
             float sunIntensity, glm::vec3 skyColor) {

    flushUploads(ctx);

    uint32_t frame = ctx.currentFrame;
    vkWaitForFences(ctx.device.device, 1, &ctx.inFlight[frame], VK_TRUE, UINT64_MAX);
    vkResetFences  (ctx.device.device, 1, &ctx.inFlight[frame]);

    uint32_t imageIndex;
    vkAcquireNextImageKHR(ctx.device.device, ctx.swapchain.swapchain, UINT64_MAX,
                          ctx.imageAvailable[frame], VK_NULL_HANDLE, &imageIndex);

    // ── Frustum planes ────────────────────────────────────────────────────────
    struct Plane { glm::vec3 n; float d; };
    Plane planes[6];
    const glm::mat4& m = viewProj;
    planes[0] = {{ m[0][3]+m[0][0], m[1][3]+m[1][0], m[2][3]+m[2][0] }, m[3][3]+m[3][0] };
    planes[1] = {{ m[0][3]-m[0][0], m[1][3]-m[1][0], m[2][3]-m[2][0] }, m[3][3]-m[3][0] };
    planes[2] = {{ m[0][3]+m[0][1], m[1][3]+m[1][1], m[2][3]+m[2][1] }, m[3][3]+m[3][1] };
    planes[3] = {{ m[0][3]-m[0][1], m[1][3]-m[1][1], m[2][3]-m[2][1] }, m[3][3]-m[3][1] };
    planes[4] = {{ m[0][3]+m[0][2], m[1][3]+m[1][2], m[2][3]+m[2][2] }, m[3][3]+m[3][2] };
    planes[5] = {{ m[0][3]-m[0][2], m[1][3]-m[1][2], m[2][3]-m[2][2] }, m[3][3]-m[3][2] };

    auto chunkVisible = [&](ChunkCoord coord) -> bool {
        float s  = (float)ChunkData::SIZE;
        glm::vec3 mn((float)coord.x * s, (float)coord.y * s, (float)coord.z * s);
        glm::vec3 mx = mn + s;
        for (auto& p : planes) {
            glm::vec3 pv{p.n.x>0?mx.x:mn.x, p.n.y>0?mx.y:mn.y, p.n.z>0?mx.z:mn.z};
            if (glm::dot(p.n, pv) + p.d < 0.f) return false;
        }
        return true;
    };

    // ── Build indirect draw list ──────────────────────────────────────────────
    auto* drawCmds   = static_cast<DrawCmd*>      (ctx.indirectMapped[frame]);
    auto* chunkDatas = static_cast<ChunkDrawData*>(ctx.perChunkMapped[frame]);

    uint32_t drawCount = 0;
    for (auto& [coord, gpu] : ctx.chunks) {
        if (!chunkVisible(coord)) continue;
        if (drawCount >= VkContext::MAX_DRAW_CHUNKS) break;

        float s = (float)ChunkData::SIZE;
        glm::vec3 offset((float)coord.x * s, (float)coord.y * s, (float)coord.z * s);

        chunkDatas[drawCount].model  = glm::translate(glm::mat4(1.f), offset);
        chunkDatas[drawCount].params = glm::vec4(sunIntensity, 0.f, 0.f, 0.f);

        drawCmds[drawCount].indexCount    = gpu.indexCount;
        drawCmds[drawCount].instanceCount = 1;
        drawCmds[drawCount].firstIndex    = gpu.indexOffset;
        drawCmds[drawCount].vertexOffset  = (int32_t)gpu.vertexOffset;
        drawCmds[drawCount].firstInstance = drawCount;

        drawCount++;
    }

    vmaFlushAllocation(ctx.allocator, ctx.perChunkAlloc[frame], 0, VK_WHOLE_SIZE);
    vmaFlushAllocation(ctx.allocator, ctx.indirectAlloc[frame],  0, VK_WHOLE_SIZE);

    // ── Record render commands ────────────────────────────────────────────────
    VkCommandBuffer cmd = ctx.commandBuffers[frame];
    vkResetCommandBuffer(cmd, 0);
    VkCommandBufferBeginInfo bI{};
    bI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cmd, &bI);

    VkClearValue clears[2]{};
    clears[0].color        = {{skyColor.r, skyColor.g, skyColor.b, 1.f}};
    clears[1].depthStencil = {1.f, 0};

    VkRenderPassBeginInfo rpBI{};
    rpBI.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpBI.renderPass        = ctx.renderPass;
    rpBI.framebuffer       = ctx.framebuffers[imageIndex];
    rpBI.renderArea.extent = ctx.swapchain.extent;
    rpBI.clearValueCount   = 2; rpBI.pClearValues = clears;

    vkCmdBeginRenderPass(cmd, &rpBI, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx.pipeline);

    VkDeviceSize zero = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &ctx.mega.vertexBuffer, &zero);
    vkCmdBindIndexBuffer(cmd, ctx.mega.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            ctx.pipelineLayout, 0, 1, &ctx.dsSets[frame], 0, nullptr);

    struct GlobalPC { glm::mat4 viewProj; glm::vec4 params; };
    GlobalPC gpc{viewProj, {sunIntensity, 0.f, 0.f, 0.f}};
    vkCmdPushConstants(cmd, ctx.pipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(GlobalPC), &gpc);

    if (drawCount > 0)
        vkCmdDrawIndexedIndirect(cmd, ctx.indirectBuffer[frame], 0,
                                 drawCount, sizeof(DrawCmd));

    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);

    // ── Submit ────────────────────────────────────────────────────────────────
    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo sI2{};
    sI2.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    sI2.waitSemaphoreCount   = 1; sI2.pWaitSemaphores   = &ctx.imageAvailable[frame];
    sI2.pWaitDstStageMask    = &waitStage;
    sI2.commandBufferCount   = 1; sI2.pCommandBuffers   = &cmd;
    sI2.signalSemaphoreCount = 1; sI2.pSignalSemaphores = &ctx.renderFinished[frame];
    vkQueueSubmit(ctx.graphicsQueue, 1, &sI2, ctx.inFlight[frame]);

    VkPresentInfoKHR pI{};
    pI.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pI.waitSemaphoreCount = 1; pI.pWaitSemaphores = &ctx.renderFinished[frame];
    pI.swapchainCount     = 1; pI.pSwapchains     = &ctx.swapchain.swapchain;
    pI.pImageIndices      = &imageIndex;
    vkQueuePresentKHR(ctx.graphicsQueue, &pI);

    ctx.currentFrame = (frame + 1) % VkContext::FRAMES_IN_FLIGHT;
}

// ── Destroy ───────────────────────────────────────────────────────────────────

void vk_destroy(VkContext& ctx) {
    vkDeviceWaitIdle(ctx.device.device);

    vmaDestroyBuffer(ctx.allocator, ctx.mega.vertexBuffer, ctx.mega.vertexAlloc);
    vmaDestroyBuffer(ctx.allocator, ctx.mega.indexBuffer,  ctx.mega.indexAlloc);
    vmaDestroyBuffer(ctx.allocator, ctx.stagingBuffer,     ctx.stagingAlloc);
    vkDestroyFence  (ctx.device.device, ctx.uploadFence, nullptr);

    for (int i = 0; i < 2; i++) {
        vmaDestroyBuffer(ctx.allocator, ctx.indirectBuffer[i], ctx.indirectAlloc[i]);
        vmaDestroyBuffer(ctx.allocator, ctx.perChunkBuffer[i], ctx.perChunkAlloc[i]);
    }

    vkDestroyImageView(ctx.device.device, ctx.depthImageView, nullptr);
    vmaDestroyImage   (ctx.allocator, ctx.depthImage, ctx.depthAlloc);
    vmaDestroyAllocator(ctx.allocator);

    for (int i = 0; i < VkContext::FRAMES_IN_FLIGHT; i++) {
        vkDestroySemaphore(ctx.device.device, ctx.imageAvailable[i], nullptr);
        vkDestroySemaphore(ctx.device.device, ctx.renderFinished[i], nullptr);
        vkDestroyFence    (ctx.device.device, ctx.inFlight[i],       nullptr);
    }

    vkDestroyDescriptorPool     (ctx.device.device, ctx.dsPool,         nullptr);
    vkDestroyDescriptorSetLayout(ctx.device.device, ctx.dsLayout,       nullptr);
    vkDestroyCommandPool        (ctx.device.device, ctx.commandPool,    nullptr);
    vkDestroyPipeline           (ctx.device.device, ctx.pipeline,       nullptr);
    vkDestroyPipelineLayout     (ctx.device.device, ctx.pipelineLayout, nullptr);

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
