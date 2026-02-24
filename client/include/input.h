#pragma once
#include <GLFW/glfw3.h>
#include <glm/vec2.hpp>

class Input {
public:
    explicit Input(GLFWwindow* window);

    void beginFrame();

    bool key(int glfwKey)        const { return _keys[glfwKey]; }
    bool keyDown(int glfwKey)    const { return _keys[glfwKey] && !_prevKeys[glfwKey]; }
    bool keyPressed(int glfwKey) const { return _keys[glfwKey] && !_prevKeys[glfwKey]; }

    glm::vec2 mouseDelta() const { return _delta; }

    void captureCursor(bool capture);
    bool cursorCaptured() const { return _captured; }

private:
    GLFWwindow* _win;
    bool _keys    [GLFW_KEY_LAST + 1]{};
    bool _prevKeys[GLFW_KEY_LAST + 1]{};
    glm::vec2 _lastPos{};
    glm::vec2 _delta{};
    bool _captured   = false;
    bool _firstMouse = true;

    static void  keyCallback   (GLFWwindow*, int, int, int, int);
    static void  cursorCallback(GLFWwindow*, double, double);
    static Input* _instance;
};
