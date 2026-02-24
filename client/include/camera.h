#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "config.h"

class Camera {
public:
    glm::vec3 position{0.f, 20.f, 0.f};
    float yaw   = -90.f; // degrees, facing -Z
    float pitch = 0.f;

    glm::vec3 forward() const {
        return glm::normalize(glm::vec3{
            cos(glm::radians(yaw)) * cos(glm::radians(pitch)),
            sin(glm::radians(pitch)),
            sin(glm::radians(yaw)) * cos(glm::radians(pitch))
        });
    }

    glm::vec3 right() const {
        return glm::normalize(glm::cross(forward(), glm::vec3{0,1,0}));
    }

    void applyMouse(glm::vec2 delta) {
        yaw   += delta.x * Config::MOUSE_SENS;
        pitch -= delta.y * Config::MOUSE_SENS;
        if (pitch >  89.f) pitch =  89.f;
        if (pitch < -89.f) pitch = -89.f;
    }

    glm::mat4 view() const {
        return glm::lookAt(position, position + forward(), glm::vec3{0,1,0});
    }

    glm::mat4 proj(float aspect) const {
        glm::mat4 p = glm::perspective(glm::radians(70.f), aspect, 0.05f, 1000.f);
        p[1][1] *= -1; // Vulkan Y flip
        return p;
    }

    glm::mat4 viewProj(float aspect) const {
        return proj(aspect) * view();
    }
};
