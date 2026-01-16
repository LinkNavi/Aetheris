// Net/Shared/GameLogic/PrefabSystem.cs - GLB-based prefab blocks like 7 Days to Die
using System;
using System.Collections.Generic;
using System.IO;

namespace Aetheris.GameLogic
{
    /// <summary>
    /// Defines a prefab - a placeable structure made from a GLB model
    /// </summary>
    public class PrefabDefinition
    {
        /// <summary>Unique prefab ID</summary>
        public int PrefabId { get; init; }
        
        /// <summary>Display name</summary>
        public string Name { get; init; } = "Unknown Prefab";
        
        /// <summary>Description</summary>
        public string Description { get; init; } = "";
        
        /// <summary>Path to GLB model file</summary>
        public string ModelPath { get; init; } = "";
        
        /// <summary>Size in blocks (how many grid cells this occupies)</summary>
        public (int x, int y, int z) BlockSize { get; init; } = (1, 1, 1);
        
        /// <summary>Collision bounds in world units</summary>
        public (float x, float y, float z) CollisionSize { get; init; } = (2f, 2f, 2f);
        
        /// <summary>Model scale multiplier</summary>
        public float ModelScale { get; init; } = 1f;
        
        /// <summary>Model rotation offset (degrees)</summary>
        public (float x, float y, float z) ModelRotation { get; init; } = (0, 0, 0);
        
        /// <summary>Model position offset from block origin</summary>
        public (float x, float y, float z) ModelOffset { get; init; } = (0, 0, 0);
        
        /// <summary>Block type this prefab creates when placed</summary>
        public BlockType PlacedBlockType { get; init; } = BlockType.Stone;
        
        /// <summary>Item ID that places this prefab (0 = not placeable by players)</summary>
        public int PlacementItemId { get; init; } = 0;
        
        /// <summary>Can be rotated when placing</summary>
        public bool CanRotate { get; init; } = true;
        
        /// <summary>Number of rotation states (usually 4 for 90 degree increments)</summary>
        public int RotationStates { get; init; } = 4;
        
        /// <summary>Category for organization</summary>
        public string Category { get; init; } = "misc";
        
        /// <summary>Tags for filtering</summary>
        public string[] Tags { get; init; } = Array.Empty<string>();
    }

    /// <summary>
    /// A placed prefab instance in the world
    /// </summary>
    public class PlacedPrefab
    {
        public int PrefabId { get; init; }
        public BlockPos Position { get; init; }
        public byte Rotation { get; init; }
        public DateTime PlacedTime { get; init; }
        public string PlacedBy { get; init; } = "";
        
        /// <summary>Get world-space center of this prefab</summary>
        public (float x, float y, float z) GetWorldCenter()
        {
            var def = PrefabRegistry.Get(PrefabId);
            if (def == null) return Position.ToWorldCenter();
            
            var origin = Position.ToWorldOrigin();
            return (
                origin.x + def.BlockSize.x * GridConfig.BLOCK_SIZE * 0.5f,
                origin.y + def.BlockSize.y * GridConfig.BLOCK_SIZE * 0.5f,
                origin.z + def.BlockSize.z * GridConfig.BLOCK_SIZE * 0.5f
            );
        }
    }

    /// <summary>
    /// Registry of all prefab definitions
    /// </summary>
    public static class PrefabRegistry
    {
        private static readonly Dictionary<int, PrefabDefinition> prefabs = new();
        private static readonly Dictionary<int, int> itemToPrefab = new(); // ItemId -> PrefabId
        private static bool initialized = false;

        public static void Initialize()
        {
            if (initialized) return;

            // Register built-in prefabs
            RegisterBuiltInPrefabs();
            
            // Load prefabs from disk
            LoadPrefabsFromDirectory("prefabs");

            initialized = true;
            Console.WriteLine($"[PrefabRegistry] Registered {prefabs.Count} prefabs");
        }

        private static void RegisterBuiltInPrefabs()
        {
         

   Register(new PrefabDefinition
    {
        PrefabId = 2000,
        Name = "Oak Tree",
        Description = "Natural oak tree",
        ModelPath = "assets/models/oaktree.glb",
        BlockSize = (2, 4, 2),  // Takes up 2x4x2 grid cells
        CollisionSize = (4f, 8f, 4f),  // World units for collision
        PlacedBlockType = BlockType.Wood,
        PlacementItemId = 0,  // Can't be placed by players - world gen only
        CanRotate = true,
        RotationStates = 4,
        Category = "nature",
        Tags = new[] { "tree", "wood", "nature", "mineable" },
        ModelScale = 1.5f,
        ModelOffset = (0f, 0f, 0f)
    });
        }

        private static void LoadPrefabsFromDirectory(string path)
        {
            if (!Directory.Exists(path)) return;

            foreach (var file in Directory.GetFiles(path, "*.json"))
            {
                try
                {
                    // TODO: Load prefab definition from JSON
                    // var json = File.ReadAllText(file);
                    // var def = JsonSerializer.Deserialize<PrefabDefinition>(json);
                    // Register(def);
                }
                catch (Exception ex)
                {
                    Console.WriteLine($"[PrefabRegistry] Error loading {file}: {ex.Message}");
                }
            }
        }

        public static void Register(PrefabDefinition def)
        {
            prefabs[def.PrefabId] = def;
            
            if (def.PlacementItemId > 0)
            {
                itemToPrefab[def.PlacementItemId] = def.PrefabId;
            }
        }

        public static PrefabDefinition? Get(int prefabId)
        {
            return prefabs.TryGetValue(prefabId, out var def) ? def : null;
        }

        public static PrefabDefinition? GetByItemId(int itemId)
        {
            if (itemToPrefab.TryGetValue(itemId, out var prefabId))
                return Get(prefabId);
            return null;
        }

        public static IEnumerable<PrefabDefinition> GetAll() => prefabs.Values;

        public static IEnumerable<PrefabDefinition> GetByCategory(string category)
        {
            foreach (var def in prefabs.Values)
            {
                if (def.Category == category)
                    yield return def;
            }
        }

        public static IEnumerable<PrefabDefinition> GetByTag(string tag)
        {
            foreach (var def in prefabs.Values)
            {
                if (Array.IndexOf(def.Tags, tag) >= 0)
                    yield return def;
            }
        }
    }

    /// <summary>
    /// Manages placed prefabs in the world
    /// </summary>
    public class PrefabManager
    {
        private readonly Dictionary<BlockPos, PlacedPrefab> prefabs = new();
        private readonly VoxelGrid grid;
        private readonly TerrainModifier modifier;

        public PrefabManager(VoxelGrid grid, TerrainModifier modifier)
        {
            this.grid = grid;
            this.modifier = modifier;
        }

        /// <summary>
        /// Place a prefab at the given position
        /// </summary>
        public PlacePrefabResult Place(int prefabId, BlockPos position, byte rotation = 0, string placedBy = "")
        {
            var def = PrefabRegistry.Get(prefabId);
            if (def == null)
            {
                return new PlacePrefabResult { Success = false, Error = "Unknown prefab" };
            }

            // Check if space is clear
            if (!CanPlace(prefabId, position, rotation))
            {
                return new PlacePrefabResult { Success = false, Error = "Space not clear" };
            }

            // Create the placed prefab
            var placed = new PlacedPrefab
            {
                PrefabId = prefabId,
                Position = position,
                Rotation = rotation,
                PlacedTime = DateTime.UtcNow,
                PlacedBy = placedBy
            };

            // Occupy grid cells
            var occupiedBlocks = new List<BlockPos>();
            for (int dx = 0; dx < def.BlockSize.x; dx++)
            {
                for (int dy = 0; dy < def.BlockSize.y; dy++)
                {
                    for (int dz = 0; dz < def.BlockSize.z; dz++)
                    {
                        var blockPos = position.Offset(dx, dy, dz);
                        grid.SetBlock(blockPos, BlockData.Solid(def.PlacedBlockType));
                        occupiedBlocks.Add(blockPos);
                    }
                }
            }

            // Store prefab
            prefabs[position] = placed;

            return new PlacePrefabResult
            {
                Success = true,
                Prefab = placed,
                OccupiedBlocks = occupiedBlocks.ToArray()
            };
        }

        /// <summary>
        /// Remove a prefab at the given position
        /// </summary>
        public bool Remove(BlockPos position)
        {
            if (!prefabs.TryGetValue(position, out var placed))
                return false;

            var def = PrefabRegistry.Get(placed.PrefabId);
            if (def == null)
                return false;

            // Clear grid cells
            for (int dx = 0; dx < def.BlockSize.x; dx++)
            {
                for (int dy = 0; dy < def.BlockSize.y; dy++)
                {
                    for (int dz = 0; dz < def.BlockSize.z; dz++)
                    {
                        var blockPos = position.Offset(dx, dy, dz);
                        grid.SetBlock(blockPos, BlockData.Air);
                    }
                }
            }

            prefabs.Remove(position);
            return true;
        }

        /// <summary>
        /// Check if a prefab can be placed at position
        /// </summary>
        public bool CanPlace(int prefabId, BlockPos position, byte rotation = 0)
        {
            var def = PrefabRegistry.Get(prefabId);
            if (def == null) return false;

            // Check all blocks in the prefab's footprint
            for (int dx = 0; dx < def.BlockSize.x; dx++)
            {
                for (int dy = 0; dy < def.BlockSize.y; dy++)
                {
                    for (int dz = 0; dz < def.BlockSize.z; dz++)
                    {
                        var blockPos = position.Offset(dx, dy, dz);
                        if (!grid.IsAir(blockPos))
                            return false;
                    }
                }
            }

            // Check for support (needs at least one solid block below or adjacent)
            bool hasSupport = false;
            var below = position.Down();
            if (grid.IsSolid(below))
                hasSupport = true;

            // Also check adjacent blocks for wall-mounted prefabs
            if (!hasSupport)
            {
                var adjacent = new[] { position.North(), position.South(), position.East(), position.West() };
                foreach (var adj in adjacent)
                {
                    if (grid.IsSolid(adj))
                    {
                        hasSupport = true;
                        break;
                    }
                }
            }

            return true;
        }

        /// <summary>
        /// Get prefab at position (if any)
        /// </summary>
        public PlacedPrefab? GetAt(BlockPos position)
        {
            return prefabs.TryGetValue(position, out var placed) ? placed : null;
        }

 /// <summary>
/// Get all prefabs in range (thread-safe)
/// </summary>
public IEnumerable<PlacedPrefab> GetInRange(BlockPos center, int radius)
{
    // Create a snapshot to avoid concurrent modification
    PlacedPrefab[] snapshot;
    lock (prefabs)
    {
        snapshot = prefabs.Values.ToArray();
    }
    
    foreach (var prefab in snapshot)
    {
        int dx = prefab.Position.X - center.X;
        int dy = prefab.Position.Y - center.Y;
        int dz = prefab.Position.Z - center.Z;
        
        if (Math.Abs(dx) <= radius && Math.Abs(dy) <= radius && Math.Abs(dz) <= radius)
        {
            yield return prefab;
        }
    }
}

        /// <summary>
        /// Get all placed prefabs
        /// </summary>
        public IEnumerable<PlacedPrefab> GetAll() => prefabs.Values;
    }

    public struct PlacePrefabResult
    {
        public bool Success;
        public string? Error;
        public PlacedPrefab? Prefab;
        public BlockPos[]? OccupiedBlocks;
    }
}
