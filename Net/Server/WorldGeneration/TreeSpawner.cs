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
            int worldX = chunkX * ClientConfig.CHUNK_SIZE;
            int worldZ = chunkZ * ClientConfig.CHUNK_SIZE;
            
            // Check grid points within chunk
            for (int dx = 0; dx < ClientConfig.CHUNK_SIZE; dx += TREE_GRID_SIZE)
            {
                for (int dz = 0; dz < ClientConfig.CHUNK_SIZE; dz += TREE_GRID_SIZE)
                {
                    int x = worldX + dx;
                    int z = worldZ + dz;
                    
                    // Skip if already checked this position
                    var key = (x / TREE_GRID_SIZE, z / TREE_GRID_SIZE);
                    if (spawnedTrees.Contains(key))
                        continue;
                    
                    spawnedTrees.Add(key);
                    TrySpawnTree(x, z);
                }
            }
        }
        
        private void TrySpawnTree(int x, int z)
        {
            // Get biome and surface data
            var columnData = WorldGen.GetColumnData(x, z);
            int surfaceY = (int)Math.Floor(columnData.SurfaceY);
            
            // Skip if underwater or too high
            if (surfaceY < 2 || surfaceY > 80)
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
            
            // If we got here, we should spawn a tree
            Console.WriteLine($"[TreeSpawner] PASSED density check at ({x}, {z}): roll={roll:F3}, density={density:F3}, biome={columnData.Biome}");
            
            // Choose tree type based on biome
            int treeId = ChooseTreeType(columnData.Biome, surfaceY);
            if (treeId == 0)
            {
                Console.WriteLine($"[TreeSpawner] No tree type for biome {columnData.Biome}");
                return;
            }
            
            var treeDef = PrefabRegistry.Get(treeId);
            if (treeDef == null)
            {
                Console.WriteLine($"[TreeSpawner] ERROR: Tree definition {treeId} not found!");
                return;
            }
            
            Console.WriteLine($"[TreeSpawner] Chose {treeDef.Name}, checking placement...");
            
            // Find a good Y position by checking a range around the surface
            int bestY = FindBestTreeHeight(x, z, surfaceY, treeDef.BlockSize);
            
            if (bestY == -1)
            {
                Console.WriteLine($"[TreeSpawner] Could not find suitable height near surface {surfaceY}");
                return;
            }
            
            // Spawn the tree!
            var treePos = new BlockPos(x, bestY, z);
            byte rotation = (byte)(random.Next(0, 4)); // Random rotation
            
            Console.WriteLine($"[TreeSpawner] Placing {treeDef.Name} at {treePos}");
            
            var result = world.Prefabs.Place(treeId, treePos, rotation, "worldgen");
            
            if (result.Success)
            {
                Console.WriteLine($"[TreeSpawner] ✓ SUCCESS: Spawned {treeDef.Name} at ({x}, {bestY}, {z}) in {columnData.Biome} biome");
            }
            else
            {
                Console.WriteLine($"[TreeSpawner] ✗ FAILED: {result.Error}");
            }
        }
        
        /// <summary>
        /// Find the best Y height for tree placement by checking above and below surface
        /// </summary>
        private int FindBestTreeHeight(int x, int z, int surfaceY, (int x, int y, int z) treeSize)
        {
            // Try positions from surface up to 3 blocks above
            for (int yOffset = 0; yOffset <= 3; yOffset++)
            {
                int testY = surfaceY + yOffset;
                
                if (IsGoodTreePosition(x, testY, z, treeSize))
                {
                    return testY;
                }
            }
            
            // Also try 1-2 blocks below surface (for uneven terrain)
            for (int yOffset = -1; yOffset >= -2; yOffset--)
            {
                int testY = surfaceY + yOffset;
                
                if (IsGoodTreePosition(x, testY, z, treeSize))
                {
                    return testY;
                }
            }
            
            return -1; // No good position found
        }
        
        /// <summary>
        /// Check if a position is good for a tree (most of it in air, base can touch ground)
        /// </summary>
        private bool IsGoodTreePosition(int x, int y, int z, (int x, int y, int z) size)
        {
            int airCount = 0;
            int totalCells = size.x * size.y * size.z;
            
            for (int dx = 0; dx < size.x; dx++)
            {
                for (int dy = 0; dy < size.y; dy++)
                {
                    for (int dz = 0; dz < size.z; dz++)
                    {
                        int checkX = x + dx;
                        int checkY = y + dy;
                        int checkZ = z + dz;
                        
                        float density = WorldGen.SampleDensity(checkX, checkY, checkZ);
                        
                        // Count as "air" if density is below threshold
                        if (density <= 0.5f)
                        {
                            airCount++;
                        }
                        else if (dy > 0)
                        {
                            // Above base layer must be air - strict requirement
                            return false;
                        }
                    }
                }
            }
            
            // At least 75% of the tree volume should be in air
            // (base layer can partially intersect ground)
            float airPercent = (float)airCount / totalCells;
            return airPercent >= 0.75f;
        }
        
        private int ChooseTreeType(WorldGen.Biome biome, int surfaceY)
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
        
        private bool IsSpaceClear(int x, int y, int z, (int x, int y, int z) size)
        {
            // This method is now replaced by IsGoodTreePosition
            // Keeping for compatibility but shouldn't be called
            return IsGoodTreePosition(x, y, z, size);
        }
        
        /// <summary>
        /// Get stats for debugging
        /// </summary>
        public int GetSpawnedTreeCount() => spawnedTrees.Count;
    }
}
