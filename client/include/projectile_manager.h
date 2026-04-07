#pragma once
#include "spell_element.h"
#include "spell_packets.h"
#include <algorithm>
#include <glm/vec3.hpp>
#include <vector>

struct ClientProjectile {
    uint32_t id;
    glm::vec3 pos;
    glm::vec3 dir;
    float speed;
    float radius;
    float lifetime;
    float age;
    SpellElement element;
    std::string spellName;  // add this
    bool dead = false;
};

class ProjectileManager {
public:
 void spawn(const ProjectileSpawnPacket &pkt) {
    _projectiles.push_back({pkt.projectileId,
                            {pkt.originX, pkt.originY, pkt.originZ},
                            {pkt.dirX, pkt.dirY, pkt.dirZ},
                            pkt.speed,
                            pkt.radius,
                            pkt.lifetime,
                            0.f,
                            pkt.element,
                            pkt.spellName,  // add this
                            false});
}

  void update(float dt) {
    for (auto &p : _projectiles) {
      if (p.dead)
        continue;
      p.pos += p.dir * p.speed * dt;
      p.age += dt;
      if (p.age >= p.lifetime)
        p.dead = true;
    }
    // remove dead
    _projectiles.erase(
        std::remove_if(_projectiles.begin(), _projectiles.end(),
                       [](const ClientProjectile &p) { return p.dead; }),
        _projectiles.end());
  }

  void onHit(const ProjectileHitPacket &pkt) {
    for (auto &p : _projectiles)
      if (p.id == pkt.projectileId) {
        p.dead = true;
        break;
      }
  }

  const std::vector<ClientProjectile> &all() const { return _projectiles; }

private:
  std::vector<ClientProjectile> _projectiles;
};
