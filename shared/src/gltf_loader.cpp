#include "gltf_loader.h"
#include "log.h"

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#include <tiny_gltf.h>

GltfModel loadGlb(const char* path) {
    GltfModel out;
    tinygltf::Model    model;
    tinygltf::TinyGLTF loader;
    std::string err, warn;

    bool ok = loader.LoadBinaryFromFile(&model, &err, &warn, path);
    if (!warn.empty()) Log::warn(std::string("glTF: ") + warn);
    if (!ok) { Log::err(std::string("glTF load failed: ") + err); return out; }

    for (auto& mesh : model.meshes) {
        for (auto& prim : mesh.primitives) {
            if (prim.mode != TINYGLTF_MODE_TRIANGLES) continue;
            GltfMesh gm;
            gm.name = mesh.name;

            // ── Positions ─────────────────────────────────────────────────
            auto posIt = prim.attributes.find("POSITION");
            if (posIt == prim.attributes.end()) continue;

            auto getAcc = [&](int idx) -> const tinygltf::Accessor& {
                return model.accessors[idx];
            };
            auto getView = [&](int idx) -> const tinygltf::BufferView& {
                return model.bufferViews[idx];
            };
            auto getRaw = [&](const tinygltf::Accessor& acc) -> const uint8_t* {
                const auto& view = getView(acc.bufferView);
                return model.buffers[view.buffer].data.data()
                     + view.byteOffset + acc.byteOffset;
            };
            auto getStride = [&](const tinygltf::Accessor& acc, size_t elemSize) -> size_t {
                const auto& view = getView(acc.bufferView);
                return view.byteStride ? view.byteStride : elemSize;
            };

            const auto& posAcc = getAcc(posIt->second);
            const uint8_t* posRaw = getRaw(posAcc);
            size_t posStride = getStride(posAcc, sizeof(float) * 3);

            gm.vertices.resize(posAcc.count);
            for (size_t i = 0; i < posAcc.count; i++) {
                const float* p = reinterpret_cast<const float*>(posRaw + i * posStride);
                gm.vertices[i].pos = {p[0], p[1], p[2]};
            }

            // ── Normals ───────────────────────────────────────────────────
            auto normIt = prim.attributes.find("NORMAL");
            if (normIt != prim.attributes.end()) {
                const auto& acc = getAcc(normIt->second);
                const uint8_t* raw = getRaw(acc);
                size_t stride = getStride(acc, sizeof(float) * 3);
                for (size_t i = 0; i < acc.count && i < gm.vertices.size(); i++) {
                    const float* n = reinterpret_cast<const float*>(raw + i * stride);
                    gm.vertices[i].normal = {n[0], n[1], n[2]};
                }
            }

            // ── UVs ───────────────────────────────────────────────────────
            auto uvIt = prim.attributes.find("TEXCOORD_0");
            if (uvIt != prim.attributes.end()) {
                const auto& acc = getAcc(uvIt->second);
                const uint8_t* raw = getRaw(acc);
                size_t stride = getStride(acc, sizeof(float) * 2);
                for (size_t i = 0; i < acc.count && i < gm.vertices.size(); i++) {
                    const float* u = reinterpret_cast<const float*>(raw + i * stride);
                    gm.vertices[i].uv = {u[0], u[1]};
                }
            }

            // ── Indices ───────────────────────────────────────────────────
            if (prim.indices >= 0) {
                const auto& acc = getAcc(prim.indices);
                const uint8_t* raw = getRaw(acc);
                gm.indices.resize(acc.count);
                for (size_t i = 0; i < acc.count; i++) {
                    switch (acc.componentType) {
                    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                        gm.indices[i] = raw[i]; break;
                    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
                        gm.indices[i] = reinterpret_cast<const uint16_t*>(raw)[i]; break;
                    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
                        gm.indices[i] = reinterpret_cast<const uint32_t*>(raw)[i]; break;
                    default: break;
                    }
                }
            } else {
                gm.indices.resize(gm.vertices.size());
                for (size_t i = 0; i < gm.vertices.size(); i++)
                    gm.indices[i] = (uint32_t)i;
            }

            out.meshes.push_back(std::move(gm));
        }
    }

    out.valid = !out.meshes.empty();
    if (out.valid)
        Log::info(std::string("Loaded GLB: ") + path +
                  " (" + std::to_string(out.meshes.size()) + " meshes)");
    else
        Log::warn(std::string("GLB has no usable meshes: ") + path);
    return out;
}
