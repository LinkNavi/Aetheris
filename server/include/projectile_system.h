#pragma once
#include <enet/enet.h>
#include <glm/vec3.hpp>
#include <glm/geometric.hpp>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <cmath>
#include "spell_element.h"
#include "spell_packets.h"
#include "net_common.h"
#include "noise_gen.h"

struct ServerProjectile {
    uint32_t     id;
    ENetPeer*    caster;
    glm::vec3    pos;
    glm::vec3    dir;
    float        speed;
    float        radius;
    float        lifetime;
    float        age;
    float        damage;
    SpellElement element;
    bool         dead = false;
};
static glm::vec3 terrainNormal(float wx, float wz) {
    float e = 0.5f; // sample offset
    float hL = sampleSurfaceY(wx - e, wz);
    float hR = sampleSurfaceY(wx + e, wz);
    float hD = sampleSurfaceY(wx, wz - e);
    float hU = sampleSurfaceY(wx, wz + e);
    // Cross product of the two tangent vectors gives the normal
    return glm::normalize(glm::vec3(hL - hR, 2.0f * e, hD - hU));
}
class ProjectileSystem {
public:
    void spawn(const ServerProjectile& p) {
        _projectiles.push_back(p);
    }

    struct HitEvent {
    uint32_t     projectileId;
    ENetPeer*    victim;
    glm::vec3    pos;
    glm::vec3    normal;      // add this
    float        damage;
    SpellElement element;
};

   static bool isInsideTerrain(glm::vec3 pos, float radius) {
    float buffer = std::min(radius * 0.5f, 0.4f); // never more than 0.4
    float surfY  = sampleSurfaceY(pos.x, pos.z);
    if (pos.y < surfY + buffer) return true;
    float r = 0.3f;
    if (pos.y < sampleSurfaceY(pos.x + r, pos.z    ) + buffer) return true;
    if (pos.y < sampleSurfaceY(pos.x - r, pos.z    ) + buffer) return true;
    if (pos.y < sampleSurfaceY(pos.x,     pos.z + r) + buffer) return true;
    if (pos.y < sampleSurfaceY(pos.x,     pos.z - r) + buffer) return true;
    return false;
}

    std::vector<HitEvent> update(
        float dt,
        const std::unordered_map<ENetPeer*, glm::vec3>& positions)
    {
        std::vector<HitEvent> hits;

        for (auto& p : _projectiles) {
            if (p.dead) continue;

            // Sub-step so fast projectiles don't tunnel through thin geometry
            float stepSize  = std::min(p.radius * 0.5f, 0.25f);
            float totalDist = p.speed * dt;
            int   steps     = std::max(1, (int)(totalDist / stepSize));
            float subDt     = dt / (float)steps;

            bool killed = false;
            for (int s = 0; s < steps && !killed; s++) {
                p.pos += p.dir * p.speed * subDt;
                p.age += subDt;

                if (p.age >= p.lifetime) {
                    p.dead = true;
                    killed = true;
                    break;
                }

         if (isInsideTerrain(p.pos, p.radius)) {
    float surfY      = sampleSurfaceY(p.pos.x, p.pos.z);
    float buffer     = std::min(p.radius * 0.5f, 0.4f);
    glm::vec3 hitPos = {p.pos.x, surfY + buffer, p.pos.z};
    glm::vec3 n      = terrainNormal(p.pos.x, p.pos.z);
    hits.push_back({p.id, nullptr, hitPos, n, 0.f, p.element});
    p.dead  = true;
    killed  = true;
    break;
}

                // Player check
                for (auto& [peer, ppos] : positions) {
                    if (peer == p.caster) continue;
                    glm::vec3 playerCenter = ppos + glm::vec3(0.f, 0.9f, 0.f);
                    float dist = glm::length(p.pos - playerCenter);
                    if (dist < p.radius + 0.4f) {
                        hits.push_back({p.id, peer, p.pos,
                glm::normalize(p.pos - playerCenter),
                p.damage, p.element});
                        p.dead = true;
                        killed = true;
                        break;
                    }
                }
            }
        }

        _projectiles.erase(
            std::remove_if(_projectiles.begin(), _projectiles.end(),
                           [](const ServerProjectile& p){ return p.dead; }),
            _projectiles.end());

        return hits;
    }

    void onPlayerRemoved(ENetPeer* peer) {
        for (auto& p : _projectiles)
            if (p.caster == peer) p.dead = true;
    }

private:
    std::vector<ServerProjectile> _projectiles;
    static uint32_t _nextId;
};

inline uint32_t ProjectileSystem::_nextId = 1;
