#pragma once
#include <GLFW/glfw3.h>
#include <fstream>
#include <string>
#include <unordered_map>

enum class Action : uint8_t {
  // Movement
  MoveForward = 0,
  MoveBack,
  MoveLeft,
  MoveRight,
  Jump,
  Sprint,
  Crouch,
  // Combat
  LightAttack,
  HeavyAttack,
  Parry,
  Dodge,
  // Spells
  CycleSpell,
  SpellEditor, // open spell editor
  CastSpell,
  // UI
  Inventory,
  Chat,
  Interact,
  DebugMenu,
  Respawn,
  COUNT
};

static constexpr const char *ACTION_NAMES[] = {
    "Move Forward", "Move Back",   "Move Left",    "Move Right",   "Jump",
    "Sprint",       "Crouch",      "Light Attack", "Heavy Attack", "Parry",
    "Dodge",        "Cycle Spell", "Spell Editor", "Cast Spell","Inventory",    "Chat",
    "Interact",     "Debug Menu",  "Respawn",
};

struct Keybinds {
  std::unordered_map<Action, int> binds;

  // defaults
  Keybinds() {
    binds[Action::MoveForward] = GLFW_KEY_W;
    binds[Action::MoveBack] = GLFW_KEY_S;
    binds[Action::MoveLeft] = GLFW_KEY_A;
    binds[Action::MoveRight] = GLFW_KEY_D;
    binds[Action::Jump] = GLFW_KEY_SPACE;
    binds[Action::Sprint] = GLFW_KEY_LEFT_SHIFT;
    binds[Action::Crouch] = GLFW_KEY_LEFT_CONTROL;
    binds[Action::LightAttack] = GLFW_KEY_F;
    binds[Action::HeavyAttack] = GLFW_KEY_G;
    binds[Action::Parry] = GLFW_KEY_Q;
    binds[Action::Dodge] = GLFW_KEY_LEFT_CONTROL;
    binds[Action::CycleSpell] = GLFW_KEY_C;
    binds[Action::SpellEditor] = GLFW_KEY_K;
    binds[Action::CastSpell] = GLFW_KEY_R;
    binds[Action::Inventory] = GLFW_KEY_I;
    binds[Action::Chat] = GLFW_KEY_ENTER;
    binds[Action::Interact] = GLFW_KEY_E;
    binds[Action::DebugMenu] = GLFW_KEY_F3;
    binds[Action::Respawn] = GLFW_KEY_P;
  }

  int get(Action a) const {
    auto it = binds.find(a);
    return it != binds.end() ? it->second : GLFW_KEY_UNKNOWN;
  }

  void set(Action a, int key) { binds[a] = key; }

  bool isDown(Action a, const class Input &input) const;
  bool isHeld(Action a, const class Input &input) const;
  bool isUp(Action a,
            const class Input &input) const; // held last frame, not this frame

  void save(const char *path = "keybinds.cfg") const {
    std::ofstream f(path);
    if (!f)
      return;
    for (auto &[action, key] : binds)
      f << (int)action << " " << key << "\n";
  }

  void load(const char *path = "keybinds.cfg") {
    std::ifstream f(path);
    if (!f)
      return;
    int action, key;
    while (f >> action >> key)
      binds[(Action)action] = key;
  }
};
