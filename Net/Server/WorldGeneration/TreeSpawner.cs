// Net/Server/WorldGeneration/TreeSpawner.cs
using System;
using System.Collections.Generic;
using Aetheris.GameLogic;

namespace Aetheris
{
    /// <summary>
    /// Spawns trees on the world surface during world generation
    /// </summary>
    public class TreeSpawner
    {
        private readonly GameWorld world;
        private readonly Random random;
        private readonly HashSet<(int, int)> spawnedTrees = new();
        
        // Tree density per biome (0-1, chance per suitable spot)
        private const float PLAINS_TREE_DENSITY = 0.02f;
        private const float FOREST_TREE_DENSITY = 1.0f;
        private const float DESERT_TREE_DENSITY = 0.001f;
        private const float MOUNTAIN_TREE_DENSITY = 0.05f;
        
        // Grid spacing (check every N blocks)
        private const int TREE_GRID_SIZE = 4;
        
        public TreeSpawner(GameWorld world, int seed)
        {
            this.world = world;
            this.random = new Random(seed + 12345);
        }
        
     /// <summary>
/// Attempt to spawn trees in a chunk
/// </summary>
public void SpawnTreesInChunk(int chunkX, int chunkZ)
{
    // Convert chunk coordinates to world coordinates
    int worldX = chunkX * ClientConfig.CHUNK_SIZE;
    int worldZ = chunkZ * ClientConfig.CHUNK_SIZE;
    
    // Check grid points within chunk (in WORLD coordinates)
    for (int dx = 0; dx < ClientConfig.CHUNK_SIZE; dx += TREE_GRID_SIZE)
    {
        for (int dz = 0; dz < ClientConfig.CHUNK_SIZE; dz += TREE_GRID_SIZE)
        {
            int wx = worldX + dx;
            int wz = worldZ + dz;
            
            // Skip if already checked this position
            var key = (wx / TREE_GRID_SIZE, wz / TREE_GRID_SIZE);
            if (spawnedTrees.Contains(key))
                continue;
            
            spawnedTrees.Add(key);
            TrySpawnTree(wx, wz);  // Pass WORLD coordinates
        }
    }
}

private void TrySpawnTree(int worldX, int worldZ)
{
    // Get biome and surface data
    var columnData = WorldGen.GetColumnData(worldX, worldZ);
    int surfaceWorldY = (int)Math.Floor(columnData.SurfaceY);
    
    // Skip if underwater or too high
    if (surfaceWorldY < 2 || surfaceWorldY > 80)
    {
        return;
    }
    
    // Get tree density for this biome
    float density = columnData.Biome switch
    {
        WorldGen.Biome.Plains => PLAINS_TREE_DENSITY,
        WorldGen.Biome.Forest => FOREST_TREE_DENSITY,
        WorldGen.Biome.Desert => DESERT_TREE_DENSITY,
        WorldGen.Biome.Mountains => MOUNTAIN_TREE_DENSITY,
        _ => 0f
    };
    
    // Random check against density
    double roll = random.NextDouble();
    if (roll > density)
    {
        return;
    }
    
    // Choose tree type based on biome
    int treeId = ChooseTreeType(columnData.Biome, surfaceWorldY);
    if (treeId == 0)
    {
        return;
    }
    
    var treeDef = PrefabRegistry.Get(treeId);
    if (treeDef == null)
    {
        Console.WriteLine($"[TreeSpawner] ERROR: Tree definition {treeId} not found!");
        return;
    }
    
    // Convert world position to block position
    var treeBlockPos = BlockPos.FromWorld(worldX, surfaceWorldY, worldZ);
    
    // Check if we can place the tree here
    if (!CanPlaceTreeAt(treeBlockPos, treeDef))
    {
        return;
    }
    
    // Spawn the tree!
    byte rotation = (byte)(random.Next(0, 4));
    
    var result = world.Prefabs.Place(treeId, treeBlockPos, rotation, "worldgen");
    
    if (result.Success)
    {
        Console.WriteLine($"[TreeSpawner] ✓ Spawned {treeDef.Name} at block {treeBlockPos} (world {worldX},{surfaceWorldY},{worldZ})");
    }
    else
    {
        Console.WriteLine($"[TreeSpawner] ✗ FAILED at {treeBlockPos}: {result.Error}");
    }
}

private bool CanPlaceTreeAt(BlockPos pos, PrefabDefinition treeDef)
{
    // Check if most of the tree's volume would be in air
    int airCount = 0;
    int totalCells = treeDef.BlockSize.x * treeDef.BlockSize.y * treeDef.BlockSize.z;
    
    for (int dx = 0; dx < treeDef.BlockSize.x; dx++)
    {
        for (int dy = 0; dy < treeDef.BlockSize.y; dy++)
        {
            for (int dz = 0; dz < treeDef.BlockSize.z; dz++)
            {
                var checkPos = pos.Offset(dx, dy, dz);
                var (wx, wy, wz) = checkPos.ToWorldOrigin();
                
                float density = WorldGen.SampleDensity(wx, wy, wz);
                
                if (density <= 0.5f)
                {
                    airCount++;
                }
                else if (dy > 0)
                {
                    // Above base layer must be air
                    return false;
                }
            }
        }
    }
    
    // At least 75% should be air
    return (float)airCount / totalCells >= 0.75f;
}        private int ChooseTreeType(WorldGen.Biome biome, int surfaceY)
        {
            return biome switch
            {
                WorldGen.Biome.Plains => random.NextDouble() > 0.3 ? 2000 : 2000,  // Oak or bush
                WorldGen.Biome.Forest => random.NextDouble() > 0.5 ? 2000 : 2000,  // Oak or pine
                WorldGen.Biome.Desert => 0,  // No trees in desert
                WorldGen.Biome.Mountains => surfaceY > 50 ? 2001 : 2000,  // Pine at high altitude
                _ => 0
            };
        }
        
      
        /// <summary>
        /// Get stats for debugging
        /// </summary>
        public int GetSpawnedTreeCount() => spawnedTrees.Count;
    }
}
