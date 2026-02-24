#pragma once
#include <cmath>
#include <glm/vec3.hpp>
#include "config.h"

struct DayNight {
    float time = 0.25f; // start at dawn (0=midnight, 0.25=dawn, 0.5=noon, 0.75=dusk)

    void update(float dt) {
        time += dt / Config::DAY_LENGTH_SECONDS;
        if (time > 1.f) time -= 1.f;
    }

    // 0=night, 1=noon
    float sunIntensity() const {
        float s = std::sin(time * 6.2831853f - 1.5707963f); // peaks at 0.5 (noon)
        return s < 0.f ? 0.f : s;
    }

    // Sun direction for lighting (world space)
    glm::vec3 sunDir() const {
        float angle = time * 6.2831853f;
        return glm::normalize(glm::vec3{std::cos(angle), std::sin(angle), 0.3f});
    }

    // Sky clear color — lerps between night (dark blue) and day (light blue)
    glm::vec3 skyColor() const {
        float t = sunIntensity();
        glm::vec3 night{0.02f, 0.02f, 0.08f};
        glm::vec3 day  {0.40f, 0.65f, 0.90f};
        // Sunrise/sunset tint
        float edgeness = 1.f - std::abs(t - 0.5f) * 2.f; // peaks at t=0.5
        glm::vec3 sunset{0.80f, 0.35f, 0.10f};
        glm::vec3 base = night + (day - night) * t;
        return base + sunset * (edgeness * edgeness * 0.3f * t);
    }
};
