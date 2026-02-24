#include "input.h"
#include <cstring>

Input* Input::_instance = nullptr;

Input::Input(GLFWwindow* window) : _win(window) {
    _instance = this;
    std::memset(_keys,     0, sizeof(_keys));
    std::memset(_prevKeys, 0, sizeof(_prevKeys));
    glfwSetKeyCallback(window,         keyCallback);
    glfwSetCursorPosCallback(window,   cursorCallback);
    captureCursor(true);
}

void Input::beginFrame() {
    std::memcpy(_prevKeys, _keys, sizeof(_keys));
    _delta = {};
    glfwPollEvents();
}

void Input::captureCursor(bool capture) {
    _captured = capture;
    _firstMouse = true;
    glfwSetInputMode(_win, GLFW_CURSOR,
        capture ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
}

void Input::keyCallback(GLFWwindow* w, int key, int /*sc*/, int action, int /*mods*/) {
    if (!_instance) return;
    if (key < 0 || key > GLFW_KEY_LAST) return;
    if (action == GLFW_PRESS)   _instance->_keys[key] = true;
    if (action == GLFW_RELEASE) _instance->_keys[key] = false;

    // Escape releases cursor
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
        _instance->captureCursor(!_instance->_captured);
}

void Input::cursorCallback(GLFWwindow*, double x, double y) {
    if (!_instance || !_instance->_captured) return;
    glm::vec2 pos((float)x, (float)y);
    if (_instance->_firstMouse) {
        _instance->_lastPos = pos;
        _instance->_firstMouse = false;
    }
    _instance->_delta += pos - _instance->_lastPos;
    _instance->_lastPos = pos;
}
