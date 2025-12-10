// Additional Gameplay Features to enable the gameplay loop
using OpenTK.Mathematics;
// ============================================================================
// 1. RESPAWN SYSTEM
// ============================================================================

namespace Aetheris
{
    public class RespawnSystem
    {
        private readonly Player player;
        private readonly PlayerStats stats;
        private readonly Action<string, ChatMessageType> addChatMessage;
        
        private bool isRespawning = false;
        private float respawnTimer = 0f;
        private const float RESPAWN_DELAY = 3f;
        private Vector3 spawnPoint;
        
        public bool IsPlayerDead => stats.IsDead;
        public float RespawnProgress => respawnTimer / RESPAWN_DELAY;
        
        public RespawnSystem(Player player, PlayerStats stats, Vector3 spawnPoint,
                            Action<string, ChatMessageType> chatCallback)
        {
            this.player = player;
            this.stats = stats;
            this.spawnPoint = spawnPoint;
            this.addChatMessage = chatCallback;
        }
        
        public void Update(float deltaTime)
        {
            if (stats.IsDead && !isRespawning)
            {
                // Just died
                isRespawning = true;
                respawnTimer = 0f;
                addChatMessage?.Invoke("You died! Respawning in 3 seconds...", ChatMessageType.Error);
            }
            
            if (isRespawning)
            {
                respawnTimer += deltaTime;
                
                if (respawnTimer >= RESPAWN_DELAY)
                {
                    Respawn();
                }
            }
        }
        
        private void Respawn()
        {
            stats.Reset();
            player.SetPosition(spawnPoint);
            player.SetVelocity(Vector3.Zero);
            
            isRespawning = false;
            respawnTimer = 0f;
            
            addChatMessage?.Invoke("You have respawned.", ChatMessageType.Success);
        }
        
        public void SetSpawnPoint(Vector3 position)
        {
            spawnPoint = position;
        }
    }
}

// ============================================================================
// 2. ITEM TOOLTIPS
// ============================================================================

namespace Aetheris
{
    public class ItemTooltip
    {
        public string Name { get; set; } = "";
        public string Description { get; set; } = "";
        public string[] Details { get; set; } = Array.Empty<string>();
        
        public static readonly Dictionary<int, ItemTooltip> Tooltips = new()
        {
            [1] = new ItemTooltip 
            { 
                Name = "Stone", 
                Description = "Basic building material",
                Details = new[] { "Common", "Used in crafting" }
            },
            [2] = new ItemTooltip 
            { 
                Name = "Dirt", 
                Description = "Soft soil block",
                Details = new[] { "Common", "Easy to dig" }
            },
            [3] = new ItemTooltip 
            { 
                Name = "Grass Block", 
                Description = "Dirt with grass on top",
                Details = new[] { "Common", "Spreads to nearby dirt" }
            },
            [100] = new ItemTooltip 
            { 
                Name = "Bread", 
                Description = "Restores 20 hunger",
                Details = new[] { "Food", "Right-click to eat" }
            },
            [101] = new ItemTooltip 
            { 
                Name = "Apple", 
                Description = "Restores 10 hunger, 2 health",
                Details = new[] { "Food", "Nutritious" }
            },
        };
        
        public static ItemTooltip GetTooltip(int itemId)
        {
            return Tooltips.TryGetValue(itemId, out var tooltip) 
                ? tooltip 
                : new ItemTooltip { Name = $"Item {itemId}", Description = "Unknown item" };
        }
    }
}

// ============================================================================
// 3. ARMOR CALCULATION
// ============================================================================

namespace Aetheris
{
    public class ArmorCalculator
    {
        // Armor slot indices
        public const int HELMET = 0;
        public const int CHESTPLATE = 1;
        public const int LEGGINGS = 2;
        public const int BOOTS = 3;
        
        // Armor items (item IDs 200-299)
        public static readonly Dictionary<int, (int armorPoints, string name)> ArmorItems = new()
        {
            // Leather armor (200-203)
            [200] = (2, "Leather Helmet"),
            [201] = (3, "Leather Chestplate"),
            [202] = (2, "Leather Leggings"),
            [203] = (1, "Leather Boots"),
            
            // Iron armor (210-213)
            [210] = (4, "Iron Helmet"),
            [211] = (6, "Iron Chestplate"),
            [212] = (5, "Iron Leggings"),
            [213] = (2, "Iron Boots"),
            
            // Diamond armor (220-223)
            [220] = (6, "Diamond Helmet"),
            [221] = (8, "Diamond Chestplate"),
            [222] = (6, "Diamond Leggings"),
            [223] = (3, "Diamond Boots"),
        };
        
        public static float CalculateTotalArmor(Inventory inventory)
        {
            float totalArmor = 0f;
            
            for (int i = 0; i < 4; i++)
            {
                var armorItem = inventory.GetArmorSlot(i);
                if (ArmorItems.TryGetValue(armorItem.ItemId, out var armorData))
                {
                    totalArmor += armorData.armorPoints;
                }
            }
            
            return totalArmor;
        }
        
        public static bool IsArmorItem(int itemId) => ArmorItems.ContainsKey(itemId);
        
        public static int GetArmorSlotForItem(int itemId)
        {
            if (!IsArmorItem(itemId)) return -1;
            
            // Determine slot based on item ID pattern
            int lastDigit = itemId % 10;
            return lastDigit switch
            {
                0 => HELMET,
                1 => CHESTPLATE,
                2 => LEGGINGS,
                3 => BOOTS,
                _ => -1
            };
        }
    }
}

// ============================================================================
// 4. TOTEM SYSTEM
// ============================================================================

namespace Aetheris
{
    public enum TotemEffect
    {
        None,
        HealthBoost,
        ArmorBoost,
        SpeedBoost,
        RegenBoost,
        MiningSpeed
    }
    
    public class TotemData
    {
        public int ItemId { get; set; }
        public string Name { get; set; } = "";
        public TotemEffect Effect { get; set; }
        public float EffectStrength { get; set; }
        
        public static readonly Dictionary<int, TotemData> Totems = new()
        {
            // Totems are item IDs 300-399
            [300] = new TotemData 
            { 
                ItemId = 300, 
                Name = "Totem of Vitality", 
                Effect = TotemEffect.HealthBoost, 
                EffectStrength = 20f 
            },
            [301] = new TotemData 
            { 
                ItemId = 301, 
                Name = "Totem of Protection", 
                Effect = TotemEffect.ArmorBoost, 
                EffectStrength = 15f 
            },
            [302] = new TotemData 
            { 
                ItemId = 302, 
                Name = "Totem of Swiftness", 
                Effect = TotemEffect.SpeedBoost, 
                EffectStrength = 1.5f 
            },
            [303] = new TotemData 
            { 
                ItemId = 303, 
                Name = "Totem of Regeneration", 
                Effect = TotemEffect.RegenBoost, 
                EffectStrength = 2f 
            },
            [304] = new TotemData 
            { 
                ItemId = 304, 
                Name = "Totem of Mining", 
                Effect = TotemEffect.MiningSpeed, 
                EffectStrength = 2f 
            },
        };
        
        public static bool IsTotem(int itemId) => Totems.ContainsKey(itemId);
    }
    
    public class TotemManager
    {
        private readonly Inventory inventory;
        private readonly PlayerStats stats;
        
        public TotemManager(Inventory inventory, PlayerStats stats)
        {
            this.inventory = inventory;
            this.stats = stats;
        }
        
        public void ApplyTotemEffects()
        {
            // Reset bonuses
            stats.MaxHealth = 100f;
            stats.MaxArmor = 100f;
            stats.HealthRegenRate = 1f;
            
            // Apply active totem effects
            for (int i = 0; i < 5; i++)
            {
                var totemItem = inventory.GetTotemSlot(i);
                if (TotemData.Totems.TryGetValue(totemItem.ItemId, out var totem))
                {
                    ApplyTotemEffect(totem);
                }
            }
        }
        
        private void ApplyTotemEffect(TotemData totem)
        {
            switch (totem.Effect)
            {
                case TotemEffect.HealthBoost:
                    stats.MaxHealth += totem.EffectStrength;
                    break;
                    
                case TotemEffect.ArmorBoost:
                    stats.MaxArmor += totem.EffectStrength;
                    break;
                    
                case TotemEffect.RegenBoost:
                    stats.HealthRegenRate += totem.EffectStrength;
                    break;
                    
                // SpeedBoost and MiningSpeed would affect player/mining system
            }
        }
        
        public float GetMiningSpeedMultiplier()
        {
            float multiplier = 1f;
            
            for (int i = 0; i < 5; i++)
            {
                var totemItem = inventory.GetTotemSlot(i);
                if (TotemData.Totems.TryGetValue(totemItem.ItemId, out var totem))
                {
                    if (totem.Effect == TotemEffect.MiningSpeed)
                    {
                        multiplier *= totem.EffectStrength;
                    }
                }
            }
            
            return multiplier;
        }
    }
}

// ============================================================================
// 5. INTEGRATION INTO GAME.CS
// ============================================================================

/*
Add these to Game.cs:

private RespawnSystem? respawnSystem;
private FallDamageTracker? fallDamageTracker;
private TotemManager? totemManager;

// In constructor:
respawnSystem = new RespawnSystem(
    player, 
    PlayerStats, 
    player.Position,
    (msg, type) => chatSystem?.AddMessage(msg, type)
);
fallDamageTracker = new FallDamageTracker();
totemManager = new TotemManager(player.Inventory, PlayerStats);

// In Update():
respawnSystem?.Update((float)e.Time);
fallDamageTracker?.Update(player, PlayerStats);
totemManager?.ApplyTotemEffects();

// Update armor from equipped items
float totalArmor = ArmorCalculator.CalculateTotalArmor(player.Inventory);
PlayerStats.Armor = totalArmor;

// Test commands (add to OnLoad for testing):
player.Inventory.AddItem(200, 1); // Leather helmet
player.Inventory.AddItem(300, 1); // Vitality totem
chatSystem?.AddMessage("Press T to chat, E for inventory", ChatMessageType.System);
*/

// ============================================================================
// 6. INVENTORY SLOT VALIDATION
// ============================================================================

/*
Add to InventoryUI.cs SwapSlots method to validate armor/totem placement:

private void SwapSlots(int slotA, int slotB)
{
    var itemA = inventory.GetSlot(slotA);
    var itemB = inventory.GetSlot(slotB);
    
    // Check if trying to place into armor slot
    if (slotB >= Inventory.GetArmorSlotIndex(0) && 
        slotB < Inventory.GetArmorSlotIndex(0) + Inventory.ARMOR_SIZE)
    {
        if (!ArmorCalculator.IsArmorItem(itemA.ItemId))
        {
            // Not armor - reject
            AnimateSlotPulse(slotA);
            return;
        }
        
        int requiredSlot = slotB - Inventory.GetArmorSlotIndex(0);
        int itemSlot = ArmorCalculator.GetArmorSlotForItem(itemA.ItemId);
        
        if (itemSlot != requiredSlot)
        {
            // Wrong armor slot
            AnimateSlotPulse(slotA);
            return;
        }
    }
    
    // Check if trying to place into totem slot
    if (slotB >= Inventory.GetTotemSlotIndex(0) && 
        slotB < Inventory.GetTotemSlotIndex(0) + Inventory.TOTEM_SIZE)
    {
        if (!TotemData.IsTotem(itemA.ItemId))
        {
            // Not a totem - reject
            AnimateSlotPulse(slotA);
            return;
        }
    }
    
    // Valid swap
    inventory.SetSlot(slotA, itemB);
    inventory.SetSlot(slotB, itemA);
    AnimateSlotPulse(slotB);
}
*/

// ============================================================================
// 7. QUICK START GUIDE
// ============================================================================

/*
COMPLETE SETUP CHECKLIST:

1. ✅ Copy all artifacts to appropriate folders
2. ✅ Update Game.cs with new systems (HUD, Chat, Stats)
3. ✅ Add TextInput event handler for chat
4. ✅ Update inventory to use GetSlot/SetSlot
5. ✅ Add fall damage tracker
6. ✅ Add respawn system
7. ✅ Add totem manager
8. ✅ Add food consumption on right-click
9. ✅ Test commands: /help, /heal, /give, /tp
10. ✅ Give player starting items

TESTING SEQUENCE:

1. Press T -> Type "hello" -> Enter (test chat)
2. Press T -> Type "/help" -> Enter (test commands)
3. Press T -> Type "/give 100 5" -> Enter (get bread)
4. Press E -> Open inventory (test UI)
5. Drag items between slots (test animations)
6. Press T -> Type "/heal" -> Enter (test healing)
7. Jump off cliff (test fall damage)
8. Wait to die (test respawn)
9. Press E -> Equip armor (test armor system)
10. Press T -> Type "/stats" -> Enter (verify everything)

GAMEPLAY LOOP NOW READY:
✅ Mining for resources
✅ Hunger/health management
✅ Armor system
✅ Totem power-ups
✅ Fall damage
✅ Death/respawn
✅ Admin commands for debugging
✅ Chat for communication
✅ Animated UI with feedback
*/
