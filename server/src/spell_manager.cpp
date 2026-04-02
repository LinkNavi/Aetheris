#include "spell_manager.h"
#include <algorithm>
#include <fstream>
#include <sstream>

// ── Helpers
// ───────────────────────────────────────────────────────────────────

SpellDef *SpellManager::findSpell(const std::string &name) {
  auto it = _spells.find(name);
  return it != _spells.end() ? &it->second : nullptr;
}

CastState &SpellManager::stateFor(ENetPeer *peer) { return _castStates[peer]; }

bool SpellManager::hasStaffEquipped(const Inventory &inv) const {
  const ItemStack &wpn = inv.weaponSlot();
  if (!wpn.empty()) {
    const ItemDef &def = getItemDef(wpn.id);
    if (def.type == ItemType::Weapon)
      return true; // staff check
  }
  // also accept offhand magic item
  const ItemStack &off = inv.offhandSlot();
  if (!off.empty() && getItemDef(off.id).type == ItemType::Magic)
    return true;
  return false;
}

// ── Load spells
// ───────────────────────────────────────────────────────────────

void SpellManager::loadSpellsFromDir(const std::string &dir) {
  namespace fs = std::filesystem;
  if (!fs::exists(dir)) {
    Log::warn("Spell dir not found: " + dir);
    return;
  }
  for (auto &entry : fs::directory_iterator(dir)) {
    if (entry.path().extension() != ".aes")
      continue;
    std::ifstream f(entry.path());
    if (!f)
      continue;
    std::ostringstream ss;
    ss << f.rdbuf();
    std::string name = entry.path().stem().string();
    loadSpellSource(name, ss.str());
  }
}

bool SpellManager::loadSpellSource(const std::string &name,
                                   const std::string &src) {
  SpellDef def;
  def.name = name;

  // Register all pending natives into each new script
  for (auto &[n, fn] : _pendingNatives)
    def.script.vm.registerNative(n, fn);

  if (!def.script.loadSource(src)) {
    Log::err("Spell compile error [" + name + "]: " + def.script.lastError);
    return false;
  }

  // Parse spell metadata from comments/header fields:
  // Lines like:  // mana: 8   // cast_time: 0.3   // requires_staff: true
  std::istringstream lines(src);
  std::string line;
  while (std::getline(lines, line)) {
    auto strip = [](std::string s) {
      s.erase(0, s.find_first_not_of(" \t/"));
      return s;
    };
    std::string l = strip(line);
    auto parseFloat = [&](const std::string &key, float &out) {
      if (l.rfind(key, 0) == 0) {
        try {
          out = std::stof(l.substr(key.size()));
        } catch (...) {
        }
      }
    };
    parseFloat("mana:", def.baseMana);
    parseFloat("cast_time:", def.baseTime);
    parseFloat("time_scale:", def.timeScale);
    if (l.rfind("requires_staff:", 0) == 0) {
      std::string val = l.substr(15);
      val.erase(0, val.find_first_not_of(" \t"));
      def.requireStaff = (val == "true");
    }
  }

  _spells[name] = std::move(def);
  Log::info("Loaded spell: " + name +
            " (mana=" + std::to_string((int)_spells[name].baseMana) +
            " cast_time=" + std::to_string(_spells[name].baseTime) + "s)");
  return true;
}

void SpellManager::registerNative(const std::string &name,
                                  Aether::NativeFn fn) {
  _pendingNatives.push_back({name, fn});
  // Also register into already-loaded spells
  for (auto &[n, def] : _spells)
    def.script.vm.registerNative(name, fn);
}

// ── beginCharge ──────────────────────────────────────────────────────────────

bool SpellManager::beginCharge(ENetPeer *peer, const std::string &spellName,
                               float aimX, float aimY, float aimZ,
                               uint32_t targetId, const PlayerStats &stats,
                               const Inventory &inv) {
  SpellDef *def = findSpell(spellName);
  if (!def) {
    Log::warn("Unknown spell: " + spellName);
    return false;
  }
  if (stats.dead || stats.mana < def->baseMana)
    return false;
  if (def->requireStaff && !hasStaffEquipped(inv))
    return false;
  if (isOnCooldown(peer, spellName))
    return false;

  CastState &cs = stateFor(peer);
  if (!cs.isIdle())
    return false; // already casting

  cs.reset();
  cs.phase = CastState::Phase::Charging;
  cs.spellName = spellName;
  cs.manaCommitted = 0.f;
  cs.maxCharge = std::min(stats.mana, 200.f);
  cs.aimX = aimX;
  cs.aimY = aimY;
  cs.aimZ = aimZ;
  cs.targetId = targetId;
  return true;
}

// ── tickCharge ───────────────────────────────────────────────────────────────

void SpellManager::tickCharge(ENetPeer *peer, float dt, PlayerStats &stats) {
  CastState &cs = stateFor(peer);
  if (!cs.isCharging())
    return;

  float drained = cs.chargeRate * dt;
  // clamp so we don't drain more than available
  drained = std::min(drained, stats.mana);
  drained = std::min(drained, cs.maxCharge - cs.manaCommitted);

  cs.manaCommitted += drained;
  stats.mana -= drained;
  stats.clamp();

  // auto-commit when cap or mana floor hit — use committed amount not current
  // mana
  if (cs.manaCommitted >= cs.maxCharge || stats.mana <= 0.f)
    commitCast(peer, stats);
}

// ── commitCast ───────────────────────────────────────────────────────────────

bool SpellManager::commitCast(ENetPeer *peer, PlayerStats &stats) {
  CastState &cs = stateFor(peer);
  if (!cs.isCharging())
    return false;

  SpellDef *def = findSpell(cs.spellName);
  if (!def) {
    cs.reset();
    return false;
  }

  // check committed amount, not stats.mana (which may be 0 after drain)
  if (cs.manaCommitted < def->baseMana) {
    stats.mana += cs.manaCommitted; // refund
    stats.clamp();
    cs.reset();
    return false;
  }

  cs.castTimeTotal =
      CastState::calcCastTime(cs.manaCommitted, def->baseTime, def->timeScale);
  cs.castTimeElapsed = 0.f;
  cs.interruptDC = CastState::calcInterruptDC(cs.manaCommitted);
  cs.phase = CastState::Phase::WindUp;
  return true;
}

// ── cancelCast ───────────────────────────────────────────────────────────────

void SpellManager::cancelCast(ENetPeer *peer) {
  CastState &cs = stateFor(peer);
  if (cs.isIdle())
    return;

  // Refund mana if still charging (no cost for cancelling before commit)
  if (cs.isCharging()) {
    // mana was already drained in tickCharge — find stats and refund
    // (caller is responsible for refunding via onDamageTaken or explicit
    // cancel)
  }
  // Wind-up cancel: mana already spent, no refund
  cs.reset();
}

// ── onDamageTaken
// ─────────────────────────────────────────────────────────────

bool SpellManager::onDamageTaken(ENetPeer *peer, float damage,
                                 PlayerStats &stats) {
  CastState &cs = stateFor(peer);
  if (!cs.isWindingUp())
    return false;

  if (damage >= cs.interruptDC) {
    Log::info("Cast interrupted! damage=" + std::to_string(damage) +
              " DC=" + std::to_string(cs.interruptDC));
    // Mana already spent — no refund on interrupt
    cs.reset();
    return true;
  }
  return false;
}

// ── update ───────────────────────────────────────────────────────────────────

std::vector<SpellManager::FireEvent> SpellManager::update(float dt) {
  tickCooldowns(dt);

  std::vector<FireEvent> fired;

 for (auto& [peer, cs] : _castStates) {
    if (!cs.isWindingUp()) continue;
    cs.castTimeElapsed += dt;
    if (cs.castTimeElapsed < cs.castTimeTotal) continue;

    FireEvent ev;
    ev.peer      = peer;
    ev.spellName = cs.spellName;
    ev.manaSpent = cs.manaCommitted;
    ev.potency   = CastState::calcPotency(cs.manaCommitted);
    ev.aimX      = cs.aimX;
    ev.aimY      = cs.aimY;
    ev.aimZ      = cs.aimZ;
    ev.targetId  = cs.targetId;

    // set budget BEFORE running script
    _firingBudget = ev.potency;
    _isFiring     = true;
_firingPeer   = peer;
_firingBudget = ev.potency;
_isFiring     = true;
_firingState  = &cs;

SpellDef* def = findSpell(cs.spellName);
if (def && def->script.hasFn(cs.spellName)) {
    def->script.call(cs.spellName, {});
}

_isFiring    = false;
_firingState = nullptr;
_firingPeer  = nullptr;

applyCooldown(peer, cs.spellName, cs.manaCommitted);
cs.reset();
fired.push_back(ev);
}

  return fired;
}

// ── Cooldowns
// ─────────────────────────────────────────────────────────────────

void SpellManager::tickCooldowns(float dt) {
  for (auto &[peer, cds] : _cooldowns) {
    for (auto &[spell, remaining] : cds) {
      remaining -= dt;
    }
  }
}

void SpellManager::applyCooldown(ENetPeer *peer, const std::string &spell,
                                 float mana) {
  _cooldowns[peer][spell] = calcCooldown(mana);
}

bool SpellManager::isOnCooldown(ENetPeer *peer,
                                const std::string &spell) const {
  auto pit = _cooldowns.find(peer);
  if (pit == _cooldowns.end())
    return false;
  auto sit = pit->second.find(spell);
  if (sit == pit->second.end())
    return false;
  return sit->second > 0.f;
}

float SpellManager::getCooldown(ENetPeer *peer,
                                const std::string &spell) const {
  auto pit = _cooldowns.find(peer);
  if (pit == _cooldowns.end())
    return 0.f;
  auto sit = pit->second.find(spell);
  if (sit == pit->second.end())
    return 0.f;
  return std::max(0.f, sit->second);
}

void SpellManager::onPlayerRemoved(ENetPeer *peer) {
  _castStates.erase(peer);
  _cooldowns.erase(peer);
}

// ── Queries
// ───────────────────────────────────────────────────────────────────

const CastState *SpellManager::getCastState(ENetPeer *peer) const {
  auto it = _castStates.find(peer);
  return it != _castStates.end() ? &it->second : nullptr;
}

bool SpellManager::hasspell(const std::string &name) const {
  return _spells.count(name) > 0;
}
