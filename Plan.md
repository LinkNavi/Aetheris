ECS is the right call for your goals — performant, cache-friendly, scales well with a proc-gen world and lots of entities. I'll factor that into the map. Here's the full dev roadmap:

---

## Aetheris — Dev Roadmap

**Architecture recommendation before you start:** Go ECS. With proc-gen, server-based multiplayer, and a performance-first goal on Vulkan, ECS will save you from major refactors later. Libraries like EnTT (header-only, C++) slot in cleanly without fighting your own engine.

---

### PHASE 0 — Engine Foundation
*Don't touch game content until this is solid.*

- Vulkan renderer bootstrap (instance, device, swapchain, render pass)
- Window management (SDL2 or GLFW for cross-platform Win/Linux)
- ECS core (integrate EnTT or roll a minimal one)
- Asset pipeline — mesh loading (GLTF), texture loading (stb_image)
- Basic input system (keyboard, mouse)
- Math library (GLM)
- Logging and crash handling
- CMake build system with Win/Linux targets

**Exit condition:** A window opens, a triangle renders, input is read.

---

### PHASE 1 — Core Systems
*The skeleton everything else attaches to.*

- First person camera with mouse look
- Player controller — walking, jumping, sprinting
- Stamina meter (governs sprint, dodge, heavy attack)
- Basic collision (AABB to start, swap later)
- Chunk-based world system (proc-gen ready structure, flat world placeholder)
- Basic terrain renderer
- Entity spawner — place an entity in the world with a transform
- Simple day/night cycle

**Exit condition:** You can walk around a flat world in first person.

---

### PHASE 2 — Combat Foundation
*Get hitting things right before adding any content.*

- Attack system with start lag and end lag (frame data driven, not hardcoded)
- Hitbox/hurtbox system tied to animation frames
- Dodge roll with I-frame window
- Parry system with tight window
- Health system — damage, death event
- Basic enemy AI (patrol, aggro, attack)
- Placeholder enemy (use a cube) that fights back
- Death & corpse system — on death spawn Undead Knight entity, place beacon

**Exit condition:** You can fight, dodge, parry, die, and your corpse fights back.

---

### PHASE 3 — Totem & Weapon Systems
*The spine of the entire game.*

- Weapon part system — blade/handle/guard etc. as separate data components
- Material system — each ore defines a stat block and passive trait
- Weapon assembly — combine parts into a finished weapon with derived stats
- Synergy detection — check part group membership, apply bonus if matched
- Totem slot system — 5 slots, equip/unequip, stat application
- Totem skill system — active skills bound to equipped totems
- Shade Totem — disables parry, buffs dodge I-frames and distance
- Attack speed driven by weapon data, not hardcoded animations

**Exit condition:** You can forge a weapon from parts, equip totems, and feel the stat differences.

---

### PHASE 4 — Proc-Gen World
*Build this after combat so you have something to fight in the world.*

- Noise-based terrain generation (FastNoise2 library recommended)
- Biome system — define biome rules, assign per chunk
- Ore spawning per biome and depth
- Structure spawning — place hand-authored structures (dungeons, forges, beacons) into proc-gen world
- Cave system generation (3D noise, separate pass from surface)
- Chunk loading/unloading around player
- World seed system

**Exit condition:** A seeded world generates with terrain, caves, ore, and placeholder structures.

---

### PHASE 5 — Survival Systems
- Hunger & thirst meters with drain rates
- Sanity meter — drain triggers, corruption event at 0%
- Corruption event — extended aggro range, phantom spawns, HUD distortion
- Food/drink items, consume and restore meters
- Sleep mechanic — sanity reset
- Inventory system
- Basic crafting at workbench

**Exit condition:** You can starve, go insane, and manage your survival meters.

---

### PHASE 6 — Forging System
- Forge structure — placed in world, tier defined by structure data
- Smelting UI — insert ore, produce molten material
- Cast system — pour molten into cast to produce a part
- Part storage and assembly UI
- Tier gate — forge rejects ores above its tier
- Repair system for durability

**Exit condition:** Full Tinkers-style forge loop works end to end with placeholder ores.

---

### PHASE 7 — Mobs & AI
- AI state machine (idle, patrol, aggro, attack, flee)
- Wyvern — aerial movement, dive/swoop attack, stun on wall collision
- Headcrawler — fast erratic movement, sanity damage on hit, cave spawner
- Undead Knight — parry/dodge reads player input, spawns lesser undead, scales with player level
- Player death → Undead Knight conversion pipeline
- Loot table system per mob

**Exit condition:** All three core mobs behave correctly in the world.

---

### PHASE 8 — Class Systems
- Summoner egg/core — find in world, hatch, companion AI, permadeath tracking, 2 companion cap
- Summoner temp companions — totem skill summon, time limit, no cap
- Monk discipline meter — fills on hit/parry, spends on sanity clear and heal
- Swordmage infusion — totem skill coats weapon with element, timed duration
- Apothecary brew — field crafting UI from inventory ingredients, splash potion physics
- Cleric revival token — item found in world, single stack, self or other use
- Knight shield bash + counter window

**Exit condition:** All 8 archetypes are playable with their unique mechanics working.

---

### PHASE 9 — Multiplayer
*Do this after single player is stable. Never the other way around.*

- Networking layer (consider ENet or a thin UDP layer — avoid bloated libs)
- Server authority model — server owns world state, clients send inputs
- Player sync — position, animation state, inventory
- Undead Knight loot contention — server arbitrates who gets loot
- Revival token use on other players over network
- PvP/PvE server flag
- Summoner companion sync

**Exit condition:** Two players can connect, fight mobs, die, and loot each other's corpses.

---

### PHASE 10 — Dimensions & Endgame
- Aether structure activation and dimension transition
- Aether physics — reduced gravity, extended dodge
- Aether Warden boss fight
- Gate weapon crafting pipeline
- Final Sentinel boss — damage immunity logic, multi-weapon requirement
- Final Dimension — constant sanity drain, corrupted mob variants
- Final Dungeon
- Aetheric Core win condition — server-wide event

**Exit condition:** Full progression loop is completable start to finish.

---

### PHASE 11 — Polish
- Audio system — SFX, ambient, music
- Particle effects
- Animation polish
- HUD distortion during corruption event
- Performance profiling and optimization pass
- Linux build verification
- Playtesting and balance pass

---

**Rough time estimate solo:** Phases 0–3 alone will take 6–12 months depending on your Vulkan experience. Don't rush them. The rest builds fast once the foundation is right.

**First thing to do today:** Set up your CMake project, get a Vulkan window open, and pull in EnTT and GLM. Everything else branches from that.
