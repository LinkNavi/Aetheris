// Net/Shared/ItemSystem.cs - Enhanced item system with all block types
using System;
using System.Collections.Generic;
using OpenTK.Mathematics;

namespace Aetheris
{
    public enum ItemRarity
    {
        Common,
        Uncommon,
        Rare,
        Epic,
        Legendary,
        Mythic
    }

    public enum ItemCategory
    {
        Block,
        Tool,
        Weapon,
        Food,
        Armor,
        Totem,
        Material,
        Consumable,
        Quest,
        Misc
    }

    public enum ToolType
    {
        None,
        Pickaxe,
        Axe,
        Shovel,
        Sword,
        Hoe
    }

    public class ItemDefinition
    {
        public int ItemId { get; set; }
        public string Name { get; set; } = "Unknown Item";
        public string Description { get; set; } = "";
        public ItemRarity Rarity { get; set; } = ItemRarity.Common;
        public ItemCategory Category { get; set; } = ItemCategory.Misc;
        public ToolType ToolType { get; set; } = ToolType.None;
        
        public int MaxStack { get; set; } = 64;
        
        public float Durability { get; set; } = 0;
        public float MiningSpeed { get; set; } = 1f;
        public float AttackDamage { get; set; } = 0f;
        public float AttackSpeed { get; set; } = 1f;
        
        public int HungerRestore { get; set; } = 0;
        public float HealthRestore { get; set; } = 0;
        
        public string ModelPath { get; set; } = "";
        public string IconPath { get; set; } = "";
        public Vector3 HeldScale { get; set; } = Vector3.One;
        public Vector3 HeldRotation { get; set; } = Vector3.Zero;
        public Vector3 HeldOffset { get; set; } = Vector3.Zero;
        
        public AetherisClient.Rendering.BlockType? PlacesBlock { get; set; } = null;
        
        public bool IsCraftable { get; set; } = false;
        public Dictionary<int, int> CraftingRecipe { get; set; } = new();
        
        public Vector4 GetRarityColor()
        {
            return Rarity switch
            {
                ItemRarity.Common => new Vector4(0.8f, 0.8f, 0.8f, 1f),
                ItemRarity.Uncommon => new Vector4(0.3f, 1f, 0.3f, 1f),
                ItemRarity.Rare => new Vector4(0.3f, 0.5f, 1f, 1f),
                ItemRarity.Epic => new Vector4(0.7f, 0.3f, 1f, 1f),
                ItemRarity.Legendary => new Vector4(1f, 0.6f, 0f, 1f),
                ItemRarity.Mythic => new Vector4(1f, 0.2f, 0.2f, 1f),
                _ => Vector4.One
            };
        }
        
        public string GetRarityName()
        {
            return Rarity switch
            {
                ItemRarity.Common => "Common",
                ItemRarity.Uncommon => "Uncommon",
                ItemRarity.Rare => "Rare",
                ItemRarity.Epic => "Epic",
                ItemRarity.Legendary => "Legendary",
                ItemRarity.Mythic => "Mythic",
                _ => "Unknown"
            };
        }
    }

    public static class ItemRegistry
    {
        private static readonly Dictionary<int, ItemDefinition> items = new();
        private static bool initialized = false;

        public static void Initialize()
        {
            if (initialized) return;
            
            RegisterBlocks();
            RegisterTools();
            RegisterWeapons();
            RegisterFood();
            RegisterArmor();
            RegisterTotems();
            RegisterMaterials();
            
            initialized = true;
            Console.WriteLine($"[ItemRegistry] Registered {items.Count} items");
        }

        private static void RegisterBlocks()
        {
            // Block items - ID matches BlockType enum value
            Register(new ItemDefinition
            {
                ItemId = 1,
                Name = "Stone",
                Description = "Basic building material",
                Category = ItemCategory.Block,
                Rarity = ItemRarity.Common,
                MaxStack = 64,
                PlacesBlock = AetherisClient.Rendering.BlockType.Stone
            });

            Register(new ItemDefinition
            {
                ItemId = 2,
                Name = "Dirt",
                Description = "Soft soil block",
                Category = ItemCategory.Block,
                Rarity = ItemRarity.Common,
                MaxStack = 64,
                PlacesBlock = AetherisClient.Rendering.BlockType.Dirt
            });

            Register(new ItemDefinition
            {
                ItemId = 3,
                Name = "Grass Block",
                Description = "Dirt with grass on top",
                Category = ItemCategory.Block,
                Rarity = ItemRarity.Common,
                MaxStack = 64,
                PlacesBlock = AetherisClient.Rendering.BlockType.Grass
            });

            Register(new ItemDefinition
            {
                ItemId = 4,
                Name = "Sand",
                Description = "Fine sand particles",
                Category = ItemCategory.Block,
                Rarity = ItemRarity.Common,
                MaxStack = 64,
                PlacesBlock = AetherisClient.Rendering.BlockType.Sand
            });

            Register(new ItemDefinition
            {
                ItemId = 5,
                Name = "Snow",
                Description = "Frozen water crystals",
                Category = ItemCategory.Block,
                Rarity = ItemRarity.Common,
                MaxStack = 64,
                PlacesBlock = AetherisClient.Rendering.BlockType.Snow
            });

            Register(new ItemDefinition
            {
                ItemId = 6,
                Name = "Gravel",
                Description = "Loose rocky material",
                Category = ItemCategory.Block,
                Rarity = ItemRarity.Common,
                MaxStack = 64,
                PlacesBlock = AetherisClient.Rendering.BlockType.Gravel
            });

            Register(new ItemDefinition
            {
                ItemId = 7,
                Name = "Wood",
                Description = "Natural wood block",
                Category = ItemCategory.Block,
                Rarity = ItemRarity.Common,
                MaxStack = 64,
                PlacesBlock = AetherisClient.Rendering.BlockType.Wood
            });

            Register(new ItemDefinition
            {
                ItemId = 8,
                Name = "Leaves",
                Description = "Foliage block",
                Category = ItemCategory.Block,
                Rarity = ItemRarity.Common,
                MaxStack = 64,
                PlacesBlock = AetherisClient.Rendering.BlockType.Leaves
            });
        }

        private static void RegisterTools()
        {
            Register(new ItemDefinition
            {
                ItemId = 50,
                Name = "Wooden Pickaxe",
                Description = "Basic mining tool",
                Category = ItemCategory.Tool,
                ToolType = ToolType.Pickaxe,
                Rarity = ItemRarity.Common,
                MaxStack = 1,
                Durability = 60,
                MiningSpeed = 1.5f,
                AttackDamage = 2f,
                HeldOffset = new Vector3(0.3f, -0.2f, 0.5f),
                HeldRotation = new Vector3(-45f, 0f, 0f)
            });

            Register(new ItemDefinition
            {
                ItemId = 51,
                Name = "Stone Pickaxe",
                Description = "Improved mining tool",
                Category = ItemCategory.Tool,
                ToolType = ToolType.Pickaxe,
                Rarity = ItemRarity.Common,
                MaxStack = 1,
                Durability = 132,
                MiningSpeed = 2.5f,
                AttackDamage = 3f
            });

            Register(new ItemDefinition
            {
                ItemId = 52,
                Name = "Iron Pickaxe",
                Description = "Durable mining tool",
                Category = ItemCategory.Tool,
                ToolType = ToolType.Pickaxe,
                Rarity = ItemRarity.Uncommon,
                MaxStack = 1,
                Durability = 251,
                MiningSpeed = 4f,
                AttackDamage = 4f
            });

            Register(new ItemDefinition
            {
                ItemId = 53,
                Name = "Diamond Pickaxe",
                Description = "Superior mining tool",
                Category = ItemCategory.Tool,
                ToolType = ToolType.Pickaxe,
                Rarity = ItemRarity.Rare,
                MaxStack = 1,
                Durability = 1562,
                MiningSpeed = 6f,
                AttackDamage = 5f
            });

            Register(new ItemDefinition
            {
                ItemId = 54,
                Name = "Wooden Axe",
                Description = "Basic chopping tool",
                Category = ItemCategory.Tool,
                ToolType = ToolType.Axe,
                Rarity = ItemRarity.Common,
                MaxStack = 1,
                Durability = 60,
                MiningSpeed = 2f,
                AttackDamage = 4f
            });

            Register(new ItemDefinition
            {
                ItemId = 55,
                Name = "Wooden Shovel",
                Description = "Basic digging tool",
                Category = ItemCategory.Tool,
                ToolType = ToolType.Shovel,
                Rarity = ItemRarity.Common,
                MaxStack = 1,
                Durability = 60,
                MiningSpeed = 2f,
                AttackDamage = 1f
            });
        }

        private static void RegisterWeapons()
        {
            Register(new ItemDefinition
            {
                ItemId = 70,
                Name = "Wooden Sword",
                Description = "Basic weapon",
                Category = ItemCategory.Weapon,
                ToolType = ToolType.Sword,
                Rarity = ItemRarity.Common,
                MaxStack = 1,
                Durability = 60,
                AttackDamage = 4f,
                AttackSpeed = 1.6f
            });

            Register(new ItemDefinition
            {
                ItemId = 71,
                Name = "Stone Sword",
                Description = "Sturdy weapon",
                Category = ItemCategory.Weapon,
                ToolType = ToolType.Sword,
                Rarity = ItemRarity.Common,
                MaxStack = 1,
                Durability = 132,
                AttackDamage = 5f,
                AttackSpeed = 1.6f
            });

            Register(new ItemDefinition
            {
                ItemId = 72,
                Name = "Iron Sword",
                Description = "Strong weapon",
                Category = ItemCategory.Weapon,
                ToolType = ToolType.Sword,
                Rarity = ItemRarity.Uncommon,
                MaxStack = 1,
                Durability = 251,
                AttackDamage = 6f,
                AttackSpeed = 1.6f
            });

            Register(new ItemDefinition
            {
                ItemId = 73,
                Name = "Diamond Sword",
                Description = "Legendary blade",
                Category = ItemCategory.Weapon,
                ToolType = ToolType.Sword,
                Rarity = ItemRarity.Rare,
                MaxStack = 1,
                Durability = 1562,
                AttackDamage = 7f,
                AttackSpeed = 1.6f
            });
        }

        private static void RegisterFood()
        {
            Register(new ItemDefinition
            {
                ItemId = 100,
                Name = "Bread",
                Description = "Restores 20 hunger",
                Category = ItemCategory.Food,
                Rarity = ItemRarity.Common,
                MaxStack = 16,
                HungerRestore = 20
            });

            Register(new ItemDefinition
            {
                ItemId = 101,
                Name = "Apple",
                Description = "Restores 10 hunger, 2 health",
                Category = ItemCategory.Food,
                Rarity = ItemRarity.Common,
                MaxStack = 16,
                HungerRestore = 10,
                HealthRestore = 2f
            });

            Register(new ItemDefinition
            {
                ItemId = 102,
                Name = "Cooked Meat",
                Description = "Hearty meal",
                Category = ItemCategory.Food,
                Rarity = ItemRarity.Uncommon,
                MaxStack = 16,
                HungerRestore = 40,
                HealthRestore = 5f
            });

            Register(new ItemDefinition
            {
                ItemId = 103,
                Name = "Golden Apple",
                Description = "Magical fruit",
                Category = ItemCategory.Food,
                Rarity = ItemRarity.Epic,
                MaxStack = 8,
                HungerRestore = 20,
                HealthRestore = 20f
            });
        }

        private static void RegisterArmor()
        {
            Register(new ItemDefinition
            {
                ItemId = 200,
                Name = "Leather Helmet",
                Description = "+2 Armor",
                Category = ItemCategory.Armor,
                Rarity = ItemRarity.Common,
                MaxStack = 1,
                Durability = 56
            });

            Register(new ItemDefinition
            {
                ItemId = 201,
                Name = "Leather Chestplate",
                Description = "+3 Armor",
                Category = ItemCategory.Armor,
                Rarity = ItemRarity.Common,
                MaxStack = 1,
                Durability = 81
            });

            Register(new ItemDefinition
            {
                ItemId = 210,
                Name = "Iron Helmet",
                Description = "+4 Armor",
                Category = ItemCategory.Armor,
                Rarity = ItemRarity.Uncommon,
                MaxStack = 1,
                Durability = 166
            });

            Register(new ItemDefinition
            {
                ItemId = 220,
                Name = "Diamond Helmet",
                Description = "+6 Armor",
                Category = ItemCategory.Armor,
                Rarity = ItemRarity.Rare,
                MaxStack = 1,
                Durability = 364
            });
        }

        private static void RegisterTotems()
        {
            Register(new ItemDefinition
            {
                ItemId = 300,
                Name = "Totem of Vitality",
                Description = "+20 Max Health",
                Category = ItemCategory.Totem,
                Rarity = ItemRarity.Epic,
                MaxStack = 1
            });

            Register(new ItemDefinition
            {
                ItemId = 301,
                Name = "Totem of Protection",
                Description = "+15 Max Armor",
                Category = ItemCategory.Totem,
                Rarity = ItemRarity.Epic,
                MaxStack = 1
            });

            Register(new ItemDefinition
            {
                ItemId = 302,
                Name = "Totem of Swiftness",
                Description = "+50% Movement Speed",
                Category = ItemCategory.Totem,
                Rarity = ItemRarity.Epic,
                MaxStack = 1
            });
        }

        private static void RegisterMaterials()
        {
            Register(new ItemDefinition
            {
                ItemId = 400,
                Name = "Iron Ingot",
                Description = "Refined iron",
                Category = ItemCategory.Material,
                Rarity = ItemRarity.Uncommon,
                MaxStack = 64
            });

            Register(new ItemDefinition
            {
                ItemId = 401,
                Name = "Diamond",
                Description = "Precious gemstone",
                Category = ItemCategory.Material,
                Rarity = ItemRarity.Rare,
                MaxStack = 64
            });

            Register(new ItemDefinition
            {
                ItemId = 402,
                Name = "Coal",
                Description = "Fuel source",
                Category = ItemCategory.Material,
                Rarity = ItemRarity.Common,
                MaxStack = 64
            });

            Register(new ItemDefinition
            {
                ItemId = 403,
                Name = "Copper Ingot",
                Description = "Conductive metal",
                Category = ItemCategory.Material,
                Rarity = ItemRarity.Common,
                MaxStack = 64
            });
        }

        private static void Register(ItemDefinition item)
        {
            items[item.ItemId] = item;
        }

        public static ItemDefinition? Get(int itemId)
        {
            return items.TryGetValue(itemId, out var item) ? item : null;
        }

        public static bool Exists(int itemId)
        {
            return items.ContainsKey(itemId);
        }

        public static IEnumerable<ItemDefinition> GetAll()
        {
            return items.Values;
        }

        public static IEnumerable<ItemDefinition> GetByCategory(ItemCategory category)
        {
            foreach (var item in items.Values)
            {
                if (item.Category == category)
                    yield return item;
            }
        }
    }

   }
