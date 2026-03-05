#pragma once
#include <entt/entt.hpp>
#include <glm/vec3.hpp>
#include <glm/geometric.hpp>
#include "inventory.h"
#include "player.h"   // CTransform

// Manages world chest entities.
// Call update() each frame to handle interaction range checks.
class ChestSystem {
public:
    explicit ChestSystem(entt::registry& reg) : _reg(reg) {}

    // Spawn a chest at world position, optionally pre-filled.
    entt::entity spawnChest(glm::vec3 pos, Inventory prefill = {}) {
        auto e = _reg.create();
        _reg.emplace<CTransform>(e, pos);
        CChest chest{};
        chest.inv = prefill;
        _reg.emplace<CChest>(e, chest);
        return e;
    }

    // Call each frame. Opens nearest chest when player presses E,
    // closes if player walks away. Returns the currently open chest entity
    // (entt::null if none).
    entt::entity update(entt::entity player, bool interactPressed) {
        if (!_reg.valid(player)) return entt::null;
        auto& pTF = _reg.get<CTransform>(player);

        // If we have an open chest, check if player walked away
        if (_reg.valid(_openChest)) {
            auto& chest = _reg.get<CChest>(_openChest);
            auto& cTF   = _reg.get<CTransform>(_openChest);
            float dist  = glm::length(pTF.pos - cTF.pos);
            if (dist > chest.interactRange * 1.5f) {
              //  chest.open  = false;
                _openChest  = entt::null;
            }
        }

        // Interact pressed — find nearest chest in range
        if (interactPressed && !_reg.valid(_openChest)) {
            entt::entity nearest = entt::null;
            float        bestDist = 1e9f;

            _reg.view<CTransform, CChest>().each(
            [&](entt::entity e, CTransform& tf, CChest& chest) {
                float d = glm::length(pTF.pos - tf.pos);
                if (d < chest.interactRange && d < bestDist) {
                    bestDist = d;
                    nearest  = e;
                }
            });

            if (_reg.valid(nearest)) {
     //           _reg.get<CChest>(nearest).open = true;
                _openChest = nearest;
            }
        }

        // Close if E pressed while already open (toggle)
        if (interactPressed && _reg.valid(_openChest)) {
            // only toggle on second press — handled in drawChest's &chestOpen
        }

        return _openChest;
    }

    entt::entity openChest() const { return _openChest; }

    void closeChest() {
        if (_reg.valid(_openChest)) {
    //        _reg.get<CChest>(_openChest).open = false;
            _openChest = entt::null;
        }
    }

private:
    entt::registry& _reg;
    entt::entity    _openChest = entt::null;
};
