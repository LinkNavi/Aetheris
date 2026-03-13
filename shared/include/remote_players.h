#pragma once
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <unordered_map>
#include <string>
#include <vector>
#include <cstring>
#include <cstdio>
#include <imgui.h>
#include "mp_packets.h"
#include "log.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/euler_angles.hpp>

#include <tiny_gltf.h>

struct RemotePlayerState {
    uint32_t    id = 0;
    std::string username;
    glm::vec3   pos{0.f};
    glm::vec3   prevPos{0.f};
    glm::vec3   renderPos{0.f};
    float       yaw   = 0.f;
    float       pitch = 0.f;
    float       interpT = 0.f;
    bool        active = false;
};

struct PlayerVertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 uv;
};

struct PlayerModelGPU {
    VkBuffer      vertBuf   = VK_NULL_HANDLE;
    VmaAllocation vertAlloc = nullptr;
    VkBuffer      idxBuf    = VK_NULL_HANDLE;
    VmaAllocation idxAlloc  = nullptr;
    uint32_t      indexCount = 0;

    VkImage       atlasImage     = VK_NULL_HANDLE;
    VkImageView   atlasImageView = VK_NULL_HANDLE;
    VmaAllocation atlasAlloc     = nullptr;
    VkSampler     atlasSampler   = VK_NULL_HANDLE;

    VkDescriptorSetLayout dsLayout = VK_NULL_HANDLE;
    VkDescriptorPool      dsPool   = VK_NULL_HANDLE;
    VkDescriptorSet       dsSet    = VK_NULL_HANDLE;

    VkPipeline       pipeline       = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;

    bool loaded = false;
};

class RemotePlayerRenderer {
public:
    std::unordered_map<uint32_t, RemotePlayerState> players;
    PlayerModelGPU model;
    uint32_t localPlayerId = 0;
    float    modelHeight = 4.9f;
    float    modelScale  = 0.6f;

    void onSpawn(const PlayerSpawnPacket& pkt) {
        if (pkt.playerId == localPlayerId) return;
        auto& p     = players[pkt.playerId];
        p.id        = pkt.playerId;
        p.username  = pkt.username;
        p.pos       = {pkt.x, pkt.y, pkt.z};
        p.prevPos   = p.pos;
        p.renderPos = p.pos;
        p.yaw       = pkt.yaw;
        p.active    = true;
    }

    void onDespawn(uint32_t playerId) { players.erase(playerId); }

    void onPosSync(const PlayerPosSyncPacket& pkt) {
        for (const auto& entry : pkt.players) {
            if (entry.playerId == localPlayerId) continue;
            auto it = players.find(entry.playerId);
            if (it == players.end()) continue;
            auto& p    = it->second;
            p.prevPos  = p.renderPos;
            p.pos      = {entry.x, entry.y, entry.z};
            p.yaw      = entry.yaw;
            p.pitch    = entry.pitch;
            p.interpT  = 0.f;
        }
    }

    void update(float dt) {
        for (auto& [id, p] : players) {
            p.interpT = std::min(p.interpT + dt * 10.f, 1.f);
            p.renderPos = glm::mix(p.prevPos, p.pos, p.interpT);
        }
    }

    bool loadModel(VkDevice device, VmaAllocator allocator,
                   VkCommandPool pool, VkQueue queue,
                   VkRenderPass renderPass, VkExtent2D extent,
                   const char* glbPath,
                   const char* vertSpvPath, const char* fragSpvPath) {

        tinygltf::Model gltf;
        tinygltf::TinyGLTF loader;
        std::string err, warn;
        if (!loader.LoadBinaryFromFile(&gltf, &err, &warn, glbPath)) {
            Log::err("Player GLB failed: " + err); return false;
        }

        // ── Per-material diffuse textures ─────────────────────────────────
        struct MatTex { std::vector<uint8_t> rgba; int w=0, h=0; glm::vec4 col{1}; };
        std::vector<MatTex> mats(gltf.materials.size());
        for (int mi = 0; mi < (int)gltf.materials.size(); mi++) {
            auto& mat = gltf.materials[mi]; auto& mt = mats[mi];
            if (mat.pbrMetallicRoughness.baseColorFactor.size()==4) {
                auto& f = mat.pbrMetallicRoughness.baseColorFactor;
                mt.col = {(float)f[0],(float)f[1],(float)f[2],(float)f[3]};
            }
            if (mat.pbrMetallicRoughness.baseColorTexture.index >= 0) {
                int src = gltf.textures[mat.pbrMetallicRoughness.baseColorTexture.index].source;
                if (src >= 0 && src < (int)gltf.images.size()) {
                    auto& img = gltf.images[src];
                    if (!img.image.empty()) {
                        mt.w = img.width; mt.h = img.height;
                        if (img.component == 4) { mt.rgba = img.image; }
                        else if (img.component == 3) {
                            mt.rgba.resize(img.width*img.height*4);
                            for (int p=0;p<img.width*img.height;p++) {
                                mt.rgba[p*4]=img.image[p*3]; mt.rgba[p*4+1]=img.image[p*3+1];
                                mt.rgba[p*4+2]=img.image[p*3+2]; mt.rgba[p*4+3]=255;
                            }
                        }
                    }
                }
            }
            if (mt.rgba.empty()) {
                mt.w=1; mt.h=1; mt.rgba.resize(4);
                mt.rgba[0]=(uint8_t)(mt.col.r*255); mt.rgba[1]=(uint8_t)(mt.col.g*255);
                mt.rgba[2]=(uint8_t)(mt.col.b*255); mt.rgba[3]=(uint8_t)(mt.col.a*255);
            }
        }

        // ── Bake atlas: tiles in a row ────────────────────────────────────
        int numMats = (int)mats.size();
        int tile = 512, atlasW = tile*numMats, atlasH = tile;
        std::vector<uint8_t> atlas(atlasW*atlasH*4, 0);
        for (int mi=0; mi<numMats; mi++) {
            auto& mt = mats[mi]; int ox = mi*tile;
            for (int y=0; y<tile; y++) for (int x=0; x<tile; x++) {
                int sx = mt.w>1 ? x%mt.w : 0, sy = mt.h>1 ? y%mt.h : 0;
                int si = (sy*mt.w+sx)*4, di = (y*atlasW+ox+x)*4;
                memcpy(&atlas[di], &mt.rgba[si], 4);
            }
        }

        // ── Extract verts, remap UVs ──────────────────────────────────────
        std::vector<PlayerVertex> verts; std::vector<uint32_t> inds;
        float yMin=1e9f, yMax=-1e9f;
        for (auto& mesh : gltf.meshes) for (auto& prim : mesh.primitives) {
            if (prim.mode != TINYGLTF_MODE_TRIANGLES) continue;
            int mi = prim.material >= 0 ? prim.material : 0;
            float uOff = (float)mi/(float)numMats, uScl = 1.f/(float)numMats;

            auto posIt = prim.attributes.find("POSITION");
            if (posIt == prim.attributes.end()) continue;
            auto& pa = gltf.accessors[posIt->second];
            auto& pv = gltf.bufferViews[pa.bufferView];
            const uint8_t* pr = gltf.buffers[pv.buffer].data.data()+pv.byteOffset+pa.byteOffset;
            size_t ps = pv.byteStride ? pv.byteStride : 12;

            const uint8_t* nr=nullptr; size_t ns=12;
            auto ni = prim.attributes.find("NORMAL");
            if (ni!=prim.attributes.end()) {
                auto& a=gltf.accessors[ni->second]; auto& v=gltf.bufferViews[a.bufferView];
                nr=gltf.buffers[v.buffer].data.data()+v.byteOffset+a.byteOffset;
                ns=v.byteStride?v.byteStride:12;
            }
            const uint8_t* ur=nullptr; size_t us=8;
            auto ui = prim.attributes.find("TEXCOORD_0");
            if (ui!=prim.attributes.end()) {
                auto& a=gltf.accessors[ui->second]; auto& v=gltf.bufferViews[a.bufferView];
                ur=gltf.buffers[v.buffer].data.data()+v.byteOffset+a.byteOffset;
                us=v.byteStride?v.byteStride:8;
            }

            uint32_t base = (uint32_t)verts.size();
            for (size_t i=0; i<pa.count; i++) {
                PlayerVertex v{};
                auto* pp=(const float*)(pr+i*ps);
                v.pos={pp[0],pp[1],pp[2]};
                if (v.pos.y<yMin) yMin=v.pos.y; if (v.pos.y>yMax) yMax=v.pos.y;
                if (nr) { auto* nn=(const float*)(nr+i*ns); v.normal={nn[0],nn[1],nn[2]}; }
                if (ur) {
                    auto* uu=(const float*)(ur+i*us);
                    float u=uu[0]-std::floor(uu[0]), vv=uu[1]-std::floor(uu[1]);
                    v.uv={uOff+u*uScl, vv};
                } else { v.uv={uOff+0.5f*uScl, 0.5f}; }
                verts.push_back(v);
            }
            if (prim.indices>=0) {
                auto& a=gltf.accessors[prim.indices]; auto& bvw=gltf.bufferViews[a.bufferView];
                const uint8_t* r=gltf.buffers[bvw.buffer].data.data()+bvw.byteOffset+a.byteOffset;
                for (size_t i=0;i<a.count;i++) {
                    uint32_t idx;
                    switch(a.componentType){
                        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: idx=r[i]; break;
                        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: idx=((const uint16_t*)r)[i]; break;
                        default: idx=((const uint32_t*)r)[i]; break;
                    }
                    inds.push_back(base+idx);
                }
            } else { for(size_t i=0;i<pa.count;i++) inds.push_back(base+(uint32_t)i); }
        }
        modelHeight = yMax - yMin;
        Log::info("Player model: "+std::to_string(verts.size())+" verts, h="+std::to_string(modelHeight));

        model.indexCount = (uint32_t)inds.size();
        uploadBuf(device,allocator,pool,queue,verts.data(),verts.size()*sizeof(PlayerVertex),
                  VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,model.vertBuf,model.vertAlloc);
        uploadBuf(device,allocator,pool,queue,inds.data(),inds.size()*sizeof(uint32_t),
                  VK_BUFFER_USAGE_INDEX_BUFFER_BIT,model.idxBuf,model.idxAlloc);
        uploadTex(device,allocator,pool,queue,atlas.data(),atlasW,atlasH);
        createDS(device);
        createPipeline(device,renderPass,extent,vertSpvPath,fragSpvPath);
        model.loaded = true;
        return true;
    }

    void draw(VkCommandBuffer cmd, const glm::mat4& viewProj) const {
        if (!model.loaded || players.empty()) return;
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, model.pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                model.pipelineLayout, 0, 1, &model.dsSet, 0, nullptr);
        VkDeviceSize zero=0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &model.vertBuf, &zero);
        vkCmdBindIndexBuffer(cmd, model.idxBuf, 0, VK_INDEX_TYPE_UINT32);
        for (const auto& [id,p] : players) {
            if (!p.active) continue;
            glm::mat4 m(1.f);
            m = glm::translate(m, p.renderPos);
            m = glm::rotate(m, glm::radians(-p.yaw+90.f), {0,1,0});
            m = glm::scale(m, glm::vec3(modelScale));
            glm::mat4 mvp = viewProj * m;
            vkCmdPushConstants(cmd, model.pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
                               0, sizeof(glm::mat4), &mvp);
            vkCmdDrawIndexed(cmd, model.indexCount, 1, 0, 0, 0);
        }
    }

    void drawNametags(const glm::mat4& viewProj, int sw, int sh) const {
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        float tagY = modelHeight * modelScale + 0.3f;
        for (const auto& [id,p] : players) {
            if (!p.active) continue;
            glm::vec4 clip = viewProj * glm::vec4(p.renderPos + glm::vec3(0,tagY,0), 1.f);
            if (clip.w <= 0.01f) continue;
            glm::vec3 ndc = glm::vec3(clip)/clip.w;
            float sx = (ndc.x*0.5f+0.5f)*sw, sy = (1.f-(ndc.y*0.5f+0.5f))*sh;
            if (sx<-100||sx>sw+100||sy<-50||sy>sh+50) continue;
            const char* name = p.username.c_str();
            ImVec2 tsz = ImGui::CalcTextSize(name);
            float tx=sx-tsz.x*0.5f, ty=sy-tsz.y;
            dl->AddRectFilled({tx-4,ty-2},{tx+tsz.x+4,ty+tsz.y+2},IM_COL32(0,0,0,140),3.f);
            dl->AddText({tx,ty},IM_COL32(220,230,240,255),name);
        }
    }

    void destroy(VkDevice device, VmaAllocator allocator) {
        if (model.pipeline)       vkDestroyPipeline(device, model.pipeline, nullptr);
        if (model.pipelineLayout) vkDestroyPipelineLayout(device, model.pipelineLayout, nullptr);
        if (model.dsPool)         vkDestroyDescriptorPool(device, model.dsPool, nullptr);
        if (model.dsLayout)       vkDestroyDescriptorSetLayout(device, model.dsLayout, nullptr);
        if (model.atlasSampler)   vkDestroySampler(device, model.atlasSampler, nullptr);
        if (model.atlasImageView) vkDestroyImageView(device, model.atlasImageView, nullptr);
        if (model.atlasImage)     vmaDestroyImage(allocator, model.atlasImage, model.atlasAlloc);
        if (model.vertBuf)        vmaDestroyBuffer(allocator, model.vertBuf, model.vertAlloc);
        if (model.idxBuf)         vmaDestroyBuffer(allocator, model.idxBuf, model.idxAlloc);
        model = {};
    }

private:
    static void uploadBuf(VkDevice dev, VmaAllocator alloc, VkCommandPool pool, VkQueue q,
                          const void* data, VkDeviceSize size, VkBufferUsageFlags usage,
                          VkBuffer& buf, VmaAllocation& al) {
        VkBuffer stg; VmaAllocation sa;
        { VkBufferCreateInfo b{}; b.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
          b.size=size; b.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
          VmaAllocationCreateInfo a{}; a.usage=VMA_MEMORY_USAGE_CPU_ONLY;
          a.flags=VMA_ALLOCATION_CREATE_MAPPED_BIT; VmaAllocationInfo i{};
          vmaCreateBuffer(alloc,&b,&a,&stg,&sa,&i); memcpy(i.pMappedData,data,size); }
        { VkBufferCreateInfo b{}; b.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
          b.size=size; b.usage=usage|VK_BUFFER_USAGE_TRANSFER_DST_BIT;
          VmaAllocationCreateInfo a{}; a.usage=VMA_MEMORY_USAGE_GPU_ONLY;
          vmaCreateBuffer(alloc,&b,&a,&buf,&al,nullptr); }
        auto cmd=beginOT(dev,pool); VkBufferCopy r{0,0,size};
        vkCmdCopyBuffer(cmd,stg,buf,1,&r); endOT(dev,pool,q,cmd);
        vmaDestroyBuffer(alloc,stg,sa);
    }

    void uploadTex(VkDevice dev, VmaAllocator alloc, VkCommandPool pool, VkQueue q,
                   const uint8_t* px, int w, int h) {
        VkDeviceSize sz=w*h*4; VkBuffer stg; VmaAllocation sa;
        { VkBufferCreateInfo b{}; b.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
          b.size=sz; b.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
          VmaAllocationCreateInfo a{}; a.usage=VMA_MEMORY_USAGE_CPU_ONLY;
          a.flags=VMA_ALLOCATION_CREATE_MAPPED_BIT; VmaAllocationInfo i{};
          vmaCreateBuffer(alloc,&b,&a,&stg,&sa,&i); memcpy(i.pMappedData,px,sz); }

        VkImageCreateInfo ic{}; ic.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ic.imageType=VK_IMAGE_TYPE_2D; ic.format=VK_FORMAT_R8G8B8A8_SRGB;
        ic.extent={(uint32_t)w,(uint32_t)h,1}; ic.mipLevels=1; ic.arrayLayers=1;
        ic.samples=VK_SAMPLE_COUNT_1_BIT; ic.tiling=VK_IMAGE_TILING_OPTIMAL;
        ic.usage=VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;
        VmaAllocationCreateInfo ac{}; ac.usage=VMA_MEMORY_USAGE_GPU_ONLY;
        vmaCreateImage(alloc,&ic,&ac,&model.atlasImage,&model.atlasAlloc,nullptr);

        auto cmd=beginOT(dev,pool);
        VkImageMemoryBarrier br{}; br.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        br.image=model.atlasImage; br.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        br.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED; br.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        br.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0,0,nullptr,0,nullptr,1,&br);
        VkBufferImageCopy rg{}; rg.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
        rg.imageExtent={(uint32_t)w,(uint32_t)h,1};
        vkCmdCopyBufferToImage(cmd,stg,model.atlasImage,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&rg);
        br.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        br.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        br.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT; br.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0,0,nullptr,0,nullptr,1,&br);
        endOT(dev,pool,q,cmd); vmaDestroyBuffer(alloc,stg,sa);

        VkImageViewCreateInfo vc{}; vc.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vc.image=model.atlasImage; vc.viewType=VK_IMAGE_VIEW_TYPE_2D;
        vc.format=VK_FORMAT_R8G8B8A8_SRGB; vc.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        vkCreateImageView(dev,&vc,nullptr,&model.atlasImageView);

        VkSamplerCreateInfo sc{}; sc.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sc.magFilter=VK_FILTER_LINEAR; sc.minFilter=VK_FILTER_LINEAR;
        sc.addressModeU=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sc.addressModeV=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        vkCreateSampler(dev,&sc,nullptr,&model.atlasSampler);
    }

    void createDS(VkDevice dev) {
        VkDescriptorSetLayoutBinding b{}; b.binding=0;
        b.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b.descriptorCount=1; b.stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo ci{}; ci.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        ci.bindingCount=1; ci.pBindings=&b;
        vkCreateDescriptorSetLayout(dev,&ci,nullptr,&model.dsLayout);

        VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1};
        VkDescriptorPoolCreateInfo dp{}; dp.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        dp.maxSets=1; dp.poolSizeCount=1; dp.pPoolSizes=&ps;
        vkCreateDescriptorPool(dev,&dp,nullptr,&model.dsPool);

        VkDescriptorSetAllocateInfo ai{}; ai.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool=model.dsPool; ai.descriptorSetCount=1; ai.pSetLayouts=&model.dsLayout;
        vkAllocateDescriptorSets(dev,&ai,&model.dsSet);

        VkDescriptorImageInfo ii{}; ii.sampler=model.atlasSampler;
        ii.imageView=model.atlasImageView; ii.imageLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkWriteDescriptorSet w{}; w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstSet=model.dsSet; w.dstBinding=0; w.descriptorCount=1;
        w.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w.pImageInfo=&ii;
        vkUpdateDescriptorSets(dev,1,&w,0,nullptr);
    }

    void createPipeline(VkDevice dev, VkRenderPass rp, VkExtent2D ext,
                        const char* vs, const char* fs) {
        auto loadSpv=[](const char* p)->std::vector<uint32_t>{
            FILE* f=fopen(p,"rb"); if(!f) return {};
            fseek(f,0,SEEK_END); size_t sz=ftell(f); fseek(f,0,SEEK_SET);
            std::vector<uint32_t> b(sz/4); fread(b.data(),1,sz,f); fclose(f); return b; };
        auto makeMod=[&](const std::vector<uint32_t>& c)->VkShaderModule{
            VkShaderModuleCreateInfo ci{}; ci.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            ci.codeSize=c.size()*4; ci.pCode=c.data(); VkShaderModule m;
            vkCreateShaderModule(dev,&ci,nullptr,&m); return m; };

        auto vc=loadSpv(vs), fc=loadSpv(fs);
        VkShaderModule vm=makeMod(vc), fm=makeMod(fc);

        VkPushConstantRange pcr{}; pcr.stageFlags=VK_SHADER_STAGE_VERTEX_BIT; pcr.size=sizeof(glm::mat4);
        VkPipelineLayoutCreateInfo li{}; li.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        li.setLayoutCount=1; li.pSetLayouts=&model.dsLayout;
        li.pushConstantRangeCount=1; li.pPushConstantRanges=&pcr;
        vkCreatePipelineLayout(dev,&li,nullptr,&model.pipelineLayout);

        VkPipelineShaderStageCreateInfo st[2]{};
        st[0].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        st[0].stage=VK_SHADER_STAGE_VERTEX_BIT; st[0].module=vm; st[0].pName="main";
        st[1].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        st[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT; st[1].module=fm; st[1].pName="main";

        VkVertexInputBindingDescription bd{}; bd.stride=sizeof(PlayerVertex);
        VkVertexInputAttributeDescription at[3]{};
        at[0]={0,0,VK_FORMAT_R32G32B32_SFLOAT,offsetof(PlayerVertex,pos)};
        at[1]={1,0,VK_FORMAT_R32G32B32_SFLOAT,offsetof(PlayerVertex,normal)};
        at[2]={2,0,VK_FORMAT_R32G32_SFLOAT,   offsetof(PlayerVertex,uv)};
        VkPipelineVertexInputStateCreateInfo vi{}; vi.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vi.vertexBindingDescriptionCount=1; vi.pVertexBindingDescriptions=&bd;
        vi.vertexAttributeDescriptionCount=3; vi.pVertexAttributeDescriptions=at;

        VkPipelineInputAssemblyStateCreateInfo ia{}; ia.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkViewport vp{0,0,(float)ext.width,(float)ext.height,0,1};
        VkRect2D sc{{0,0},ext};
        VkPipelineViewportStateCreateInfo vps{}; vps.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vps.viewportCount=1; vps.pViewports=&vp; vps.scissorCount=1; vps.pScissors=&sc;
        VkPipelineRasterizationStateCreateInfo rs{}; rs.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.polygonMode=VK_POLYGON_MODE_FILL; rs.cullMode=VK_CULL_MODE_BACK_BIT;
        rs.frontFace=VK_FRONT_FACE_COUNTER_CLOCKWISE; rs.lineWidth=1.f;
        VkPipelineMultisampleStateCreateInfo ms{}; ms.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo ds{}; ds.sType=VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds.depthTestEnable=VK_TRUE; ds.depthWriteEnable=VK_TRUE; ds.depthCompareOp=VK_COMPARE_OP_LESS;
        VkPipelineColorBlendAttachmentState ba{}; ba.colorWriteMask=0xF;
        VkPipelineColorBlendStateCreateInfo bl{}; bl.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        bl.attachmentCount=1; bl.pAttachments=&ba;

        VkGraphicsPipelineCreateInfo pi{}; pi.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pi.stageCount=2; pi.pStages=st; pi.pVertexInputState=&vi; pi.pInputAssemblyState=&ia;
        pi.pViewportState=&vps; pi.pRasterizationState=&rs; pi.pMultisampleState=&ms;
        pi.pDepthStencilState=&ds; pi.pColorBlendState=&bl;
        pi.layout=model.pipelineLayout; pi.renderPass=rp;
        vkCreateGraphicsPipelines(dev,VK_NULL_HANDLE,1,&pi,nullptr,&model.pipeline);
        vkDestroyShaderModule(dev,vm,nullptr); vkDestroyShaderModule(dev,fm,nullptr);
    }

    static VkCommandBuffer beginOT(VkDevice d, VkCommandPool p) {
        VkCommandBufferAllocateInfo a{}; a.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        a.commandPool=p; a.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY; a.commandBufferCount=1;
        VkCommandBuffer c; vkAllocateCommandBuffers(d,&a,&c);
        VkCommandBufferBeginInfo b{}; b.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        b.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT; vkBeginCommandBuffer(c,&b); return c; }
    static void endOT(VkDevice d, VkCommandPool p, VkQueue q, VkCommandBuffer c) {
        vkEndCommandBuffer(c); VkSubmitInfo s{}; s.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;
        s.commandBufferCount=1; s.pCommandBuffers=&c;
        vkQueueSubmit(q,1,&s,VK_NULL_HANDLE); vkQueueWaitIdle(q);
        vkFreeCommandBuffers(d,p,1,&c); }
};
