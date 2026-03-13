#include "input.h"
#include <imgui_impl_glfw.h>
#include <cstring>

Input* Input::_instance = nullptr;

// Store ImGui's original callbacks so we can chain
static GLFWkeyfun     s_prevKeyCallback    = nullptr;
static GLFWcursorposfun s_prevCursorCallback = nullptr;

Input::Input(GLFWwindow* window) : _win(window) {
    _instance = this;
    std::memset(_keys,     0, sizeof(_keys));
    std::memset(_prevKeys, 0, sizeof(_prevKeys));

    // Save ImGui's callbacks before overwriting (set by ImGui_ImplGlfw_Init)
    s_prevKeyCallback    = glfwSetKeyCallback(window, keyCallback);
    s_prevCursorCallback = glfwSetCursorPosCallback(window, cursorCallback);

    captureCursor(true);
}

void Input::beginFrame() {
    std::memcpy(_prevKeys, _keys, sizeof(_keys));
    _delta = {};
    glfwPollEvents();
}

void Input::captureCursor(bool capture) {
    _captured   = capture;
    _firstMouse = true;
    glfwSetInputMode(_win, GLFW_CURSOR,
        capture ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
}

void Input::keyCallback(GLFWwindow* w, int key, int scancode, int action, int mods) {
    // Forward to ImGui first
    if (s_prevKeyCallback)
        s_prevKeyCallback(w, key, scancode, action, mods);

    if (!_instance || key < 0 || key > GLFW_KEY_LAST) return;
    if (action == GLFW_PRESS)   _instance->_keys[key] = true;
    if (action == GLFW_RELEASE) _instance->_keys[key] = false;
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
        _instance->captureCursor(!_instance->_captured);
}

void Input::cursorCallback(GLFWwindow* w, double x, double y) {
    // Forward to ImGui first
    if (s_prevCursorCallback)
        s_prevCursorCallback(w, x, y);

    if (!_instance) return;
    glm::vec2 pos((float)x, (float)y);
    if (_instance->_firstMouse) {
        _instance->_lastPos   = pos;
        _instance->_firstMouse = false;
        return;
    }
    if (_instance->_captured)
        _instance->_delta += pos - _instance->_lastPos;
    _instance->_lastPos = pos;
}
