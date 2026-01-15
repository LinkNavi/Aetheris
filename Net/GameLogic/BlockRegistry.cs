// Net/Shared/GameLogic/BlockRegistry.cs - Block definitions and properties
using System;
using System.Collections.Generic;

namespace Aetheris.GameLogic
{
    /// <summary>
    /// Block material determines tool effectiveness and sound
    /// </summary>
    public enum BlockMaterial
    {
        Air,
        Stone,
        Earth,
        Sand,
        Wood,
        Leaves,
        Metal,
        Glass
    }

    /// <summary>
    /// Tool type for mining speed calculations
    /// </summary>
    public enum ToolCategory
    {
        None,
        Pickaxe,
        Shovel,
        Axe,
        Hammer
    }

    /// <summary>
    /// Block shape for mesh generation
    /// </summary>
    public enum BlockShape
    {
        Cube,           // Standard cube block
        Slope,          // Ramp/slope for smooth terrain
        Corner,         // Corner piece
        InnerCorner,    // Inner corner
        Prefab          // GLB model prefab
    }

    /// <summary>
    /// Properties for a block type - like Minecraft's Block class
    /// </summary>
    public class BlockDefinition
    {
        public BlockType Type { get; init; }
        public string Name { get; init; } = "Unknown";
        public string Description { get; init; } = "";
        
        // Physical properties
        public BlockMaterial Material { get; init; } = BlockMaterial.Stone;
        public float Hardness { get; init; } = 1.0f;        // Time to mine (seconds)
        public float BlastResistance { get; init; } = 1.0f; // Explosion resistance
        public bool IsTransparent { get; init; } = false;
        public bool IsSolid { get; init; } = true;
        public bool HasCollision { get; init; } = true;
        
        // Mining
        public ToolCategory PreferredTool { get; init; } = ToolCategory.None;
        public int MinToolTier { get; init; } = 0;          // 0=hand, 1=wood, 2=stone, 3=iron, 4=diamond
        public int DropItemId { get; init; } = 0;           // Item dropped when mined
        public int DropCount { get; init; } = 1;
        
        // Rendering
        public BlockShape Shape { get; init; } = BlockShape.Cube;
        public string? PrefabPath { get; init; } = null;    // GLB model path for prefab blocks
        public int TextureTop { get; init; } = 0;           // Atlas tile index
        public int TextureSide { get; init; } = 0;
        public int TextureBottom { get; init; } = 0;
        
        // Light
        public int LightEmission { get; init; } = 0;        // 0-15 light level
        public int LightFilter { get; init; } = 15;         // How much light is blocked
        
        /// <summary>Calculate actual mining time based on tool</summary>
        public float GetMiningTime(ToolCategory tool, int toolTier)
        {
            if (Hardness <= 0) return 0f; // Instant break
            
            float time = Hardness;
            
            // Check if correct tool
            if (tool == PreferredTool && toolTier >= MinToolTier)
            {
                // Tool tier multiplier: 1=1.5x, 2=3x, 3=5x, 4=8x
                float multiplier = toolTier switch
                {
                    0 => 1.0f,
                    1 => 1.5f,
                    2 => 3.0f,
                    3 => 5.0f,
                    4 => 8.0f,
                    _ => 10.0f
                };
                time /= multiplier;
            }
            else if (tool != PreferredTool && toolTier < MinToolTier)
            {
                // Wrong tool or too low tier - much slower
                time *= 3.0f;
            }
            
            return time;
        }
    }

    /// <summary>
    /// Central registry of all block definitions - like Minecraft's Blocks class
    /// </summary>
    public static class BlockRegistry
    {
        private static readonly Dictionary<BlockType, BlockDefinition> blocks = new();
        private static bool initialized = false;

        public static void Initialize()
        {
            if (initialized) return;

            // Air
            Register(new BlockDefinition
            {
                Type = BlockType.Air,
                Name = "Air",
                Material = BlockMaterial.Air,
                Hardness = 0,
                IsSolid = false,
                HasCollision = false,
                IsTransparent = true
            });

            // Stone
            Register(new BlockDefinition
            {
                Type = BlockType.Stone,
                Name = "Stone",
                Material = BlockMaterial.Stone,
                Hardness = 1.5f,
                PreferredTool = ToolCategory.Pickaxe,
                MinToolTier = 0,
                DropItemId = 1,
                TextureTop = 0, TextureSide = 0, TextureBottom = 0
            });

            // Dirt
            Register(new BlockDefinition
            {
                Type = BlockType.Dirt,
                Name = "Dirt",
                Material = BlockMaterial.Earth,
                Hardness = 0.5f,
                PreferredTool = ToolCategory.Shovel,
                DropItemId = 2,
                TextureTop = 1, TextureSide = 1, TextureBottom = 1
            });

            // Grass
            Register(new BlockDefinition
            {
                Type = BlockType.Grass,
                Name = "Grass Block",
                Material = BlockMaterial.Earth,
                Hardness = 0.6f,
                PreferredTool = ToolCategory.Shovel,
                DropItemId = 2, // Drops dirt
                TextureTop = 2, TextureSide = 2, TextureBottom = 1
            });

            // Sand
            Register(new BlockDefinition
            {
                Type = BlockType.Sand,
                Name = "Sand",
                Material = BlockMaterial.Sand,
                Hardness = 0.5f,
                PreferredTool = ToolCategory.Shovel,
                DropItemId = 4,
                TextureTop = 3, TextureSide = 3, TextureBottom = 3
            });

            // Snow
            Register(new BlockDefinition
            {
                Type = BlockType.Snow,
                Name = "Snow",
                Material = BlockMaterial.Sand,
                Hardness = 0.2f,
                PreferredTool = ToolCategory.Shovel,
                DropItemId = 5,
                TextureTop = 4, TextureSide = 4, TextureBottom = 4
            });

            // Gravel
            Register(new BlockDefinition
            {
                Type = BlockType.Gravel,
                Name = "Gravel",
                Material = BlockMaterial.Sand,
                Hardness = 0.6f,
                PreferredTool = ToolCategory.Shovel,
                DropItemId = 6,
                TextureTop = 5, TextureSide = 5, TextureBottom = 5
            });

            // Wood
            Register(new BlockDefinition
            {
                Type = BlockType.Wood,
                Name = "Wood",
                Material = BlockMaterial.Wood,
                Hardness = 2.0f,
                PreferredTool = ToolCategory.Axe,
                DropItemId = 7,
                TextureTop = 6, TextureSide = 6, TextureBottom = 6
            });

            // Leaves
            Register(new BlockDefinition
            {
                Type = BlockType.Leaves,
                Name = "Leaves",
                Material = BlockMaterial.Leaves,
                Hardness = 0.2f,
                PreferredTool = ToolCategory.None,
                DropItemId = 0, // No drop by default
                DropCount = 0,
                IsTransparent = true,
                LightFilter = 1,
                TextureTop = 7, TextureSide = 7, TextureBottom = 7
            });

            initialized = true;
            Console.WriteLine($"[BlockRegistry] Registered {blocks.Count} block types");
        }

        private static void Register(BlockDefinition def)
        {
            blocks[def.Type] = def;
        }

        public static BlockDefinition? Get(BlockType type)
        {
            return blocks.TryGetValue(type, out var def) ? def : null;
        }

        public static BlockDefinition GetOrDefault(BlockType type)
        {
            return blocks.TryGetValue(type, out var def) ? def : blocks[BlockType.Stone];
        }

        public static IEnumerable<BlockDefinition> GetAll() => blocks.Values;

        public static bool IsMineable(BlockType type)
        {
            var def = Get(type);
            return def != null && def.Hardness > 0 && def.IsSolid;
        }

        public static bool IsSolid(BlockType type)
        {
            var def = Get(type);
            return def?.IsSolid ?? false;
        }

        public static bool HasCollision(BlockType type)
        {
            var def = Get(type);
            return def?.HasCollision ?? true;
        }
    }
}
