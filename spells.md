# Aetheris Magic System v2 — Spells & Runes

## Overview

Two distinct but related magic systems sharing the same scripting language (AetherScript) and primitive set:

- **Spells** — active, player-cast, immediate effect. Costs mana on cast.
- **Runes** — passive, placed in the world, trigger on conditions. Costs mana to place + optional upkeep.

Both are written in AetherScript. The only difference is the entry point: spells use `cast()`, runes use `trigger:` + `on_trigger()`.

Normal players use preset spells/runes. Advanced players write custom ones. Custom scripting is never required to play.

---

## Spells

### What They Are
Things the player actively does: shoot a fireball, teleport, heal themselves, shield an ally. Cast from the hotbar (Combat mode) using a Staff or other magic weapon.

### Script Structure
```c
spell fireball {
    cast(caster, target_pos) {
        vec3 dir = normalize(target_pos - caster.pos);
        projectile(caster.pos, dir, speed: 22.0, radius: 0.3);
        on_hit(p) {
            aoe_damage(p.pos, radius: 2.5, damage: 35.0, type: FIRE);
            apply_status(p, BURNING, duration: 3.0);
        }
    }
}
```

```c
spell blink_strike {
    cast(caster, target) {
        if (target == null) { fail("No target"); }
        if (distance(caster.pos, target.pos) > 15.0) { fail("Too far"); }
        blink(caster, target.pos + vec3(0.0, 0.0, -1.2));
        aoe_damage(target.pos, radius: 1.0, damage: 45.0, type: VOID);
        apply_status(caster, INVINCIBLE, duration: 0.2);
    }
}
```

```c
spell heal_aura {
    cast(caster, caster) {
        aoe_heal(caster.pos, radius: 4.0, amount: 30.0);
        apply_status(caster, HASTED, duration: 2.0);
    }
}
```

### Casting Flow
1. Player has Staff equipped (or low-cost spell with any weapon)
2. Player aims + presses cast key
3. Client sends `SpellCastReqPacket` (spell name, aim pos, target entity id)
4. Server validates: equipped weapon, enough mana, not stunned/dead
5. Server runs script in VM
6. Server applies all effects, sends stat deltas to affected players

### Mana Cost
Computed statically from AST at load time. Rejected at compile if over 200.

| Cost Range | Category | Staff Required |
|---|---|---|
| 0 – 30 | Minor | No |
| 31 – 80 | Standard | No |
| 81 – 120 | Major | No |
| 121 – 200 | Grand | Yes |
| 200+ | Rejected | — |

---

## Runes

### What They Are
Magic circles, sigils, glyphs — placed on surfaces (floors, walls, ceilings) and activated by triggers. They persist in the world until destroyed, expired, or the player removes them. Players place them with a **Rune Chisel** item (or a staff with a rune mode).

Think:
- A fire trap on the floor of a dungeon corridor
- A healing circle that pulses when allies stand in it
- An alarm glyph on a door that fires when enemies pass
- A death rune on yourself that explodes when you die
- A barrier rune on a wall that absorbs projectiles passing through

### Script Structure
```c
rune fire_trap {
    trigger: entity_enter(radius: 1.5, faction: ENEMY)
    cooldown: 3.0
    max_triggers: 3        // disappears after 3 activations. -1 = infinite
    lifetime: 60.0         // seconds before it expires. -1 = permanent
    upkeep: 0.0            // mana/sec drained from placer while active

    on_trigger(rune, entity) {
        aoe_damage(rune.pos, radius: 2.5, damage: 30.0, type: FIRE);
        apply_status(entity, BURNING, duration: 2.0);
    }
}
```

```c
rune heal_circle {
    trigger: timed(interval: 2.0)
    lifetime: 30.0
    upkeep: 1.5            // costs 1.5 mana/sec from placer

    on_trigger(rune, null) {
        entity[] nearby = entities_in_sphere(rune.pos, 3.0);
        for (i = 0; i < length(nearby); i++) {
            if (nearby[i].faction == ALLY) {
                aoe_heal(nearby[i].pos, radius: 0.1, amount: 8.0);
            }
        }
    }
}
```

```c
rune death_burst {
    trigger: on_death(attached_entity)
    max_triggers: 1
    lifetime: -1           // lasts until entity dies

    on_trigger(rune, entity) {
        aoe_damage(rune.pos, radius: 4.0, damage: 60.0, type: VOID);
        push_all(rune.pos, radius: 5.0, force: 14.0);
    }
}
```

```c
rune alarm {
    trigger: entity_enter(radius: 2.0, faction: ENEMY)
    cooldown: 10.0
    lifetime: -1

    on_trigger(rune, entity) {
        // notify placer — future: add notify_player() primitive
        apply_status(entity, MARKED, duration: 8.0);
    }
}
```

```c
rune projectile_absorb {
    trigger: projectile_pass(radius: 1.5, faction: ENEMY)
    max_triggers: 5
    lifetime: 20.0

    on_trigger(rune, projectile) {
        destroy_entity(projectile);
        aoe_heal(rune.pos, radius: 3.0, amount: 5.0);
    }
}
```

### Placement
- Player targets a surface (floor, wall, ceiling) within range (~4 units)
- Rune appears as a glowing circle decal on that surface
- Placement mana cost is deducted immediately
- If upkeep > 0, mana drains each second from placer; if placer runs out of mana, the rune deactivates (but doesn't disappear — reactivates when mana recovers)
- Max active runes per player: **5** (upgradeable via Totems)

### Trigger Types

| Trigger | Description | Arguments |
|---|---|---|
| `entity_enter` | Entity walks into radius | `radius`, `faction` (ENEMY/ALLY/ANY) |
| `entity_exit` | Entity leaves radius | `radius`, `faction` |
| `timed` | Fires on interval | `interval` (seconds) |
| `on_death` | Attached entity dies | `attached_entity` |
| `on_interact` | Player presses E on rune | — |
| `on_damage_taken` | Attached entity takes damage | `attached_entity`, optional `min_damage` |
| `projectile_pass` | A projectile enters radius | `radius`, `faction` |
| `on_cast` | A spell is cast nearby | `radius`, optional `spell_name` |
| `daylight` | Sun rises | — |
| `darkness` | Sun sets | — |

### Mana Cost (Placement)

Placement cost = `base_trigger_cost + on_trigger_body_cost × expected_triggers_estimate`

| Trigger Type | Base Placement Cost |
|---|---|
| `entity_enter` | 15 |
| `timed` | 20 |
| `on_death` | 10 |
| `on_interact` | 8 |
| `projectile_pass` | 18 |
| `on_damage_taken` | 12 |
| `on_cast` | 14 |

`max_triggers` and `lifetime` reduce cost: a single-use short-lived rune is cheaper than a permanent infinite one.

**Cost formula:**
```
placement_cost = base_trigger_cost
              + body_cost × min(max_triggers, 5)
              + (lifetime == -1 ? 10 : lifetime * 0.1)
              + upkeep_per_sec * 5
```

---

## Shared Primitives

Both spells and runes use the same built-in functions.

### Geometry
```c
vec3  normalize(vec3)
float distance(vec3, vec3)
float length(vec3)
vec3  lerp(vec3, vec3, float)
float dot(vec3, vec3)
vec3  rotate(vec3, axis: vec3, angle: float)
```

### Queries
```c
entity[]  entities_in_sphere(vec3 pos, float radius)
entity    nearest_enemy(vec3 pos, float radius)
entity    nearest_ally(vec3 pos, float radius)
bool      has_line_of_sight(vec3 from, vec3 to)
bool      is_on_surface(vec3 pos)
```

### Combat
```c
void projectile(vec3 origin, vec3 dir, float speed, float radius)
void aoe_damage(vec3 pos, float radius, float damage, DamageType type)
void aoe_heal(vec3 pos, float radius, float amount)
void beam(vec3 origin, vec3 dir, float length, float radius, float dps)
void chain(entity origin, int max_targets, float jump_range, float damage)
void push(entity target, vec3 dir, float force)
void push_all(vec3 pos, float radius, float force)
void pull(entity target, vec3 pos, float force)
```

### Movement
```c
void blink(entity e, vec3 destination)
void launch(entity e, vec3 velocity)
```

### World
```c
void place_entity(vec3 pos, EntityType type, float lifetime)
void destroy_entity(entity e)
void summon(vec3 pos, EntityType type, float duration)
```

### Status
```c
void apply_status(entity e, StatusType type, float duration)
void remove_status(entity e, StatusType type)
bool has_status(entity e, StatusType type)
```

### Spell Callbacks (spells only)
```c
void on_hit(entity p, callback)
void on_expire(callback)
```

### Rune Callbacks (runes only)
```c
void remove_rune(entity rune)      // self-destructs the rune
void move_rune(entity rune, vec3)  // repositions (limited use)
```

### Control
```c
void fail(string reason)
void log(string msg)
```

---

## Damage & Status Types

**Damage:** `FIRE`, `FROST`, `LIGHTNING`, `VOID`, `PHYSICAL`, `PURE`

**Status:** `BURNING`, `FROZEN`, `POISONED`, `SLOWED`, `STUNNED`, `INVINCIBLE`, `INVISIBLE`, `HASTED`, `MARKED`, `BLEEDING`

**Entity Types:** `FROST_WALL_TILE`, `FIRE_PILLAR`, `DECOY`, `TURRET`, `RUNE_LIGHT`

**Factions:** `ENEMY`, `ALLY`, `ANY`, `SELF`

---

## Preset Spell/Rune List (Built-in, No Scripting Needed)

### Preset Spells
| Name | Cost | Description |
|---|---|---|
| Firebolt | 8 | Basic fire projectile |
| Frostbolt | 8 | Slows on hit |
| Chain Lightning | 38 | Jumps between 4 enemies |
| Blink | 22 | Short range teleport |
| Blink Strike | 45 | Teleport + burst damage |
| Heal | 20 | Heal self or nearby ally |
| Void Rift | 95 | Large AoE void damage + pull |
| Mass Blink | 130 | Teleport self + nearby allies (staff req) |
| Frost Wall | 50 | Places 5 frost tiles in a line |
| Shield Burst | 35 | Brief invincibility + push nearby enemies |

### Preset Runes
| Name | Cost | Trigger | Description |
|---|---|---|---|
| Fire Trap | 28 | entity_enter | Burns enemies who step on it |
| Frost Glyph | 25 | entity_enter | Freezes enemies briefly |
| Heal Circle | 40 | timed(2s) | Pulses heals for allies, 30s lifetime |
| Death Burst | 35 | on_death | Explodes when attached entity dies |
| Alarm | 18 | entity_enter | Marks enemies who pass |
| Projectile Ward | 42 | projectile_pass | Destroys enemy projectiles |
| Lightning Ring | 55 | entity_enter | Chain lightning to all nearby enemies |
| Void Anchor | 80 | timed(1s) | Pulls all nearby enemies inward (staff req) |

---

## Compiler / VM Architecture

```
.aes file
    │
    ▼
[Lexer] → tokens
    │
    ▼
[Parser] → AST
    │
    ├──► [Static Cost Analyser]
    │         ├── spell: mana_cost (flat)
    │         └── rune:  placement_cost + upkeep_per_sec
    │
    └──► [Bytecode Emitter] → SpellBytecode / RuneBytecode
                │
                ▼
          [Stack VM]
          - max 512 instructions
          - max 2ms execution time
          - max 8 loop iterations (hard cap)
          - no heap allocation during execution
          - calls into validated C++ dispatch table
```

---

## Integration Points (Existing Codebase)

| System | Hook |
|---|---|
| `PlayerStats::mana` | Deducted on cast/placement |
| `StatsManager::spendMana()` | Called server-side for both spells and runes |
| `ItemID::WpnStaff` | Required for grand spells (>120 mana) |
| `CombatSystem` | Spell damage/knockback feeds into existing hit resolution |
| `CTransform` | Rune world position stored here |
| `entt::registry` | Runes are entities with a `CRune` component |
| `ChunkManager` | Rune decals rendered as world entities |
| New: `SpellCastReqPacket` | Client → server |
| New: `RunePlaceReqPacket` | Client → server |
| New: `RuneStatePacket` | Server → client (placement, trigger, removal) |

---

## Security / Sandboxing

- No file I/O, no network, no access outside spell API
- Max 512 instructions per execution
- Max 8 loop iterations at runtime (hard capped, not just estimated)
- 2ms execution timeout — abort + no mana spent if exceeded
- Custom scripts only executed server-side
- All primitive calls go through a validated C++ dispatch table
- Rune upkeep prevents infinite passive mana drain exploits
- Max 5 runes per player prevents world spam

---

## Totem Synergies (Future)

Some existing totems interact with the magic system:

| Totem | Effect |
|---|---|
| `TotFocus` | Spell mana costs reduced by 10% |
| `TotEndure` | Rune upkeep reduced by 25% |
| `TotMark` | Marked enemies take +15% spell damage |
| `TotBarrier` | Placed runes have +50% more max_triggers |
| `TotAnchor` | Max active runes increased from 5 to 8 |
| `TotVoid` | Void damage spells/runes chain to one extra target |
