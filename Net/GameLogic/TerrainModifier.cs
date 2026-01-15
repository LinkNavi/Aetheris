// Net/Shared/GameLogic/TerrainModifier.cs - Grid-based terrain modification (7DTD style)
using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;

namespace Aetheris.GameLogic
{
    /// <summary>
    /// Result of a terrain modification operation
    /// </summary>
    public struct TerrainModifyResult
    {
        public bool Success;
        public BlockPos[] AffectedBlocks;
        public (int cx, int cy, int cz)[] AffectedChunks;
        public int BlocksRemoved;
        public int BlocksAdded;
        public BlockType[] DroppedTypes;
        public int[] DroppedCounts;
    }

    /// <summary>
    /// How terrain should morph when blocks are removed
    /// </summary>
    public enum TerrainMorphMode
    {
        /// <summary>Simple removal - block becomes air</summary>
        Simple,
        
        /// <summary>Smooth edges - adjacent blocks get slope shapes</summary>
        Smooth,
        
        /// <summary>Collapse - blocks above fall down</summary>
        Collapse
    }

    /// <summary>
    /// Handles all terrain modification - mining, placing, explosions
    /// This is the core system both server and client use for terrain changes.
    /// 
    /// USAGE (like a Minecraft mod):
    /// ```
    /// var modifier = new TerrainModifier(world);
    /// 
    /// // Mine a block
    /// var result = modifier.MineBlock(blockPos);
    /// 
    /// // Place a block
    /// modifier.PlaceBlock(blockPos, BlockType.Stone);
    /// 
    /// // Create explosion crater
    /// modifier.Explode(center, radius: 3);
    /// ```
    /// </summary>
    public class TerrainModifier
    {
        private readonly VoxelGrid grid;
        private readonly Action<TerrainModifyResult>? onModified;

        public TerrainMorphMode MorphMode { get; set; } = TerrainMorphMode.Simple;

        public TerrainModifier(VoxelGrid grid, Action<TerrainModifyResult>? onModified = null)
        {
            this.grid = grid;
            this.onModified = onModified;
        }

        /// <summary>
        /// Mine a single block at the given position.
        /// The block is removed as a cube matching the grid size.
        /// </summary>
        public TerrainModifyResult MineBlock(BlockPos pos)
        {
            var block = grid.GetBlock(pos);
            
            if (block.IsAir)
            {
                return new TerrainModifyResult { Success = false };
            }

            var def = BlockRegistry.GetOrDefault(block.Type);
            
            // Remove the block
            grid.SetBlock(pos, BlockData.Air);

            // Get affected chunks
            var chunk = pos.GetChunk();
            var affectedChunks = GetAffectedChunks(pos);

            // Apply morphing to neighbors if enabled
            if (MorphMode == TerrainMorphMode.Smooth)
            {
                ApplySmoothMorphing(pos);
            }

            var result = new TerrainModifyResult
            {
                Success = true,
                AffectedBlocks = new[] { pos },
                AffectedChunks = affectedChunks,
                BlocksRemoved = 1,
                BlocksAdded = 0,
                DroppedTypes = def.DropItemId > 0 ? new[] { block.Type } : Array.Empty<BlockType>(),
                DroppedCounts = def.DropItemId > 0 ? new[] { def.DropCount } : Array.Empty<int>()
            };

            onModified?.Invoke(result);
            return result;
        }

        /// <summary>
        /// Mine a cubic region of blocks.
        /// Size is in block units (not world units).
        /// </summary>
        public TerrainModifyResult MineRegion(BlockPos center, int radius = 0)
        {
            var bounds = new BlockBounds(center, radius);
            var affected = new List<BlockPos>();
            var chunks = new HashSet<(int, int, int)>();
            var drops = new Dictionary<BlockType, int>();
            int removed = 0;

            for (int x = bounds.Min.X; x <= bounds.Max.X; x++)
            {
                for (int y = bounds.Min.Y; y <= bounds.Max.Y; y++)
                {
                    for (int z = bounds.Min.Z; z <= bounds.Max.Z; z++)
                    {
                        var pos = new BlockPos(x, y, z);
                        var block = grid.GetBlock(pos);

                        if (!block.IsAir)
                        {
                            grid.SetBlock(pos, BlockData.Air);
                            affected.Add(pos);
                            removed++;

                            var def = BlockRegistry.GetOrDefault(block.Type);
                            if (def.DropItemId > 0)
                            {
                                if (!drops.ContainsKey(block.Type))
                                    drops[block.Type] = 0;
                                drops[block.Type] += def.DropCount;
                            }

                            var chunk = pos.GetChunk();
                            chunks.Add(chunk);
                        }
                    }
                }
            }

            // Add neighbor chunks for boundary updates
            foreach (var pos in affected)
            {
                foreach (var c in GetAffectedChunks(pos))
                    chunks.Add(c);
            }

            if (MorphMode == TerrainMorphMode.Smooth)
            {
                foreach (var pos in affected)
                    ApplySmoothMorphing(pos);
            }

            var result = new TerrainModifyResult
            {
                Success = removed > 0,
                AffectedBlocks = affected.ToArray(),
                AffectedChunks = chunks.ToArray(),
                BlocksRemoved = removed,
                BlocksAdded = 0,
                DroppedTypes = drops.Keys.ToArray(),
                DroppedCounts = drops.Values.ToArray()
            };

            onModified?.Invoke(result);
            return result;
        }

        /// <summary>
        /// Place a block at the given position.
        /// Returns false if position is already occupied.
        /// </summary>
        public TerrainModifyResult PlaceBlock(BlockPos pos, BlockType type, byte rotation = 0)
        {
            var existing = grid.GetBlock(pos);
            
            if (!existing.IsAir)
            {
                return new TerrainModifyResult { Success = false };
            }

            grid.SetBlock(pos, new BlockData
            {
                Type = type,
                Damage = 0,
                Rotation = rotation,
                Flags = 0
            });

            var result = new TerrainModifyResult
            {
                Success = true,
                AffectedBlocks = new[] { pos },
                AffectedChunks = GetAffectedChunks(pos),
                BlocksRemoved = 0,
                BlocksAdded = 1,
                DroppedTypes = Array.Empty<BlockType>(),
                DroppedCounts = Array.Empty<int>()
            };

            onModified?.Invoke(result);
            return result;
        }

        /// <summary>
        /// Create an explosion that removes blocks in a cubic pattern.
        /// Unlike spherical explosions, this creates clean cubic craters like 7DTD.
        /// </summary>
        public TerrainModifyResult Explode(BlockPos center, int radius)
        {
            return MineRegion(center, radius);
        }

        /// <summary>
        /// Fill a region with a block type
        /// </summary>
        public TerrainModifyResult Fill(BlockBounds bounds, BlockType type)
        {
            var affected = new List<BlockPos>();
            var chunks = new HashSet<(int, int, int)>();
            int added = 0;

            for (int x = bounds.Min.X; x <= bounds.Max.X; x++)
            {
                for (int y = bounds.Min.Y; y <= bounds.Max.Y; y++)
                {
                    for (int z = bounds.Min.Z; z <= bounds.Max.Z; z++)
                    {
                        var pos = new BlockPos(x, y, z);
                        var existing = grid.GetBlock(pos);

                        if (existing.IsAir)
                        {
                            grid.SetBlock(pos, BlockData.Solid(type));
                            affected.Add(pos);
                            added++;

                            var chunk = pos.GetChunk();
                            chunks.Add(chunk);
                        }
                    }
                }
            }

            foreach (var pos in affected)
            {
                foreach (var c in GetAffectedChunks(pos))
                    chunks.Add(c);
            }

            var result = new TerrainModifyResult
            {
                Success = added > 0,
                AffectedBlocks = affected.ToArray(),
                AffectedChunks = chunks.ToArray(),
                BlocksRemoved = 0,
                BlocksAdded = added,
                DroppedTypes = Array.Empty<BlockType>(),
                DroppedCounts = Array.Empty<int>()
            };

            onModified?.Invoke(result);
            return result;
        }

        /// <summary>
        /// Replace all blocks of one type with another in a region
        /// </summary>
        public TerrainModifyResult Replace(BlockBounds bounds, BlockType from, BlockType to)
        {
            var affected = new List<BlockPos>();
            var chunks = new HashSet<(int, int, int)>();
            int replaced = 0;

            for (int x = bounds.Min.X; x <= bounds.Max.X; x++)
            {
                for (int y = bounds.Min.Y; y <= bounds.Max.Y; y++)
                {
                    for (int z = bounds.Min.Z; z <= bounds.Max.Z; z++)
                    {
                        var pos = new BlockPos(x, y, z);
                        var block = grid.GetBlock(pos);

                        if (block.Type == from)
                        {
                            grid.SetBlock(pos, BlockData.Solid(to));
                            affected.Add(pos);
                            replaced++;

                            chunks.Add(pos.GetChunk());
                        }
                    }
                }
            }

            foreach (var pos in affected)
            {
                foreach (var c in GetAffectedChunks(pos))
                    chunks.Add(c);
            }

            var result = new TerrainModifyResult
            {
                Success = replaced > 0,
                AffectedBlocks = affected.ToArray(),
                AffectedChunks = chunks.ToArray(),
                BlocksRemoved = replaced,
                BlocksAdded = replaced,
                DroppedTypes = Array.Empty<BlockType>(),
                DroppedCounts = Array.Empty<int>()
            };

            onModified?.Invoke(result);
            return result;
        }

        /// <summary>
        /// Check if a block can be placed at position
        /// </summary>
        public bool CanPlace(BlockPos pos)
        {
            return grid.IsAir(pos);
        }

        /// <summary>
        /// Check if a block can be mined at position
        /// </summary>
        public bool CanMine(BlockPos pos)
        {
            var block = grid.GetBlock(pos);
            if (block.IsAir) return false;
            
            var def = BlockRegistry.Get(block.Type);
            return def != null && def.Hardness > 0;
        }

        /// <summary>
        /// Get mining time for a block
        /// </summary>
        public float GetMiningTime(BlockPos pos, ToolCategory tool, int toolTier)
        {
            var block = grid.GetBlock(pos);
            if (block.IsAir) return 0f;

            var def = BlockRegistry.GetOrDefault(block.Type);
            return def.GetMiningTime(tool, toolTier);
        }

        /// <summary>
        /// Damage a block (for progressive mining)
        /// Returns true if block was destroyed
        /// </summary>
        public bool DamageBlock(BlockPos pos, int damage)
        {
            var block = grid.GetBlock(pos);
            if (block.IsAir) return false;

            block.Damage = (byte)Math.Min(255, block.Damage + damage);
            
            if (block.IsDestroyed)
            {
                MineBlock(pos);
                return true;
            }

            grid.SetBlock(pos, block);
            return false;
        }

        /// <summary>
        /// Apply smooth morphing to neighbors of a removed block.
        /// This creates smooth terrain transitions like 7 Days to Die.
        /// </summary>
        private void ApplySmoothMorphing(BlockPos removedPos)
        {
            // Check each neighbor
            BlockPos[] neighbors = {
                removedPos.Up(), removedPos.Down(),
                removedPos.North(), removedPos.South(),
                removedPos.East(), removedPos.West()
            };

            foreach (var neighbor in neighbors)
            {
                var block = grid.GetBlock(neighbor);
                if (block.IsAir) continue;

                // Count exposed faces
                int exposedFaces = 0;
                var dirs = new[] {
                    neighbor.Up(), neighbor.Down(),
                    neighbor.North(), neighbor.South(),
                    neighbor.East(), neighbor.West()
                };

                foreach (var dir in dirs)
                {
                    if (grid.IsAir(dir))
                        exposedFaces++;
                }

                // If block is heavily exposed, could convert to slope
                // This is where you'd implement slope/corner detection for smooth terrain
                // For now, we just leave it as is - the marching cubes will handle visual smoothing
            }
        }

        /// <summary>
        /// Get all chunks that need to be updated when a block at pos changes.
        /// Includes neighboring chunks if block is on chunk boundary.
        /// </summary>
        private (int, int, int)[] GetAffectedChunks(BlockPos pos)
        {
            var chunks = new HashSet<(int, int, int)>();
            var main = pos.GetChunk();
            chunks.Add(main);

            // Check if we're near chunk boundaries
            int localX = pos.X - main.cx * GridConfig.CHUNK_SIZE_BLOCKS;
            int localY = pos.Y - main.cy * GridConfig.CHUNK_HEIGHT_BLOCKS;
            int localZ = pos.Z - main.cz * GridConfig.CHUNK_SIZE_BLOCKS;

            // Add neighbor chunks if near boundary (within 1 block)
            if (localX <= 0) chunks.Add((main.cx - 1, main.cy, main.cz));
            if (localX >= GridConfig.CHUNK_SIZE_BLOCKS - 1) chunks.Add((main.cx + 1, main.cy, main.cz));
            if (localY <= 0) chunks.Add((main.cx, main.cy - 1, main.cz));
            if (localY >= GridConfig.CHUNK_HEIGHT_BLOCKS - 1) chunks.Add((main.cx, main.cy + 1, main.cz));
            if (localZ <= 0) chunks.Add((main.cx, main.cy, main.cz - 1));
            if (localZ >= GridConfig.CHUNK_SIZE_BLOCKS - 1) chunks.Add((main.cx, main.cy, main.cz + 1));

            return chunks.ToArray();
        }
    }
}
