#pragma once
#include "keybinds.h"
#include "input.h"

inline bool Keybinds::isDown(Action a, const Input& input) const {
    int k = get(a);
    return k != GLFW_KEY_UNKNOWN && input.keyDown(k);
}

inline bool Keybinds::isHeld(Action a, const Input& input) const {
    int k = get(a);
    return k != GLFW_KEY_UNKNOWN && input.key(k);
}

inline bool Keybinds::isUp(Action a, const Input& input) const {
    int k = get(a);
    return k != GLFW_KEY_UNKNOWN && input.keyUp(k);
}
