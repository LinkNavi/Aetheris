// Net/Shared/BlockGrid.cs - Grid-based block system for cube-style terrain modification
using System;
using System.Collections.Concurrent;
using System.Runtime.CompilerServices;

namespace Aetheris
{
    /// <summary>
    /// Tracks grid-based block modifications for 7DTD-style cube terrain editing.
    /// Natural terrain uses smooth marching cubes; player edits are sharp cubes.
    /// 
    /// Grid cells are 2x2x2 world units (matching GridConfig.BLOCK_SIZE).
    /// </summary>
    public static class BlockGrid
    {
        /// <summary>
        /// State of a grid cell
        /// </summary>
        public enum CellState : byte
        {
            /// <summary>Not modified - use procedural terrain</summary>
            Natural = 0,
            /// <summary>Player removed this cell - always air</summary>
            Air = 1,
            /// <summary>Player placed a block here - always solid</summary>
            Solid = 2
        }

        /// <summary>
        /// Data for a modified grid cell
        /// </summary>
        public struct GridCell
        {
            public CellState State;
            public BlockType BlockType;  // Only used when State == Solid
            
            public static GridCell Empty => new GridCell { State = CellState.Natural, BlockType = BlockType.Air };
            public static GridCell Removed => new GridCell { State = CellState.Air, BlockType = BlockType.Air };
            public static GridCell Placed(BlockType type) => new GridCell { State = CellState.Solid, BlockType = type };
        }

        // Grid cell size in world units
        public const int CELL_SIZE = 2;
        
        // Storage for modified cells (grid coordinates -> cell data)
        private static readonly ConcurrentDictionary<(int, int, int), GridCell> modifiedCells = new();
        
        // Lock for batch operations
        private static readonly object modifyLock = new object();

        /// <summary>
        /// Convert world coordinates to grid cell coordinates
        /// </summary>
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public static (int gx, int gy, int gz) WorldToGrid(int worldX, int worldY, int worldZ)
        {
            // Floor division to handle negative coordinates correctly
            int gx = worldX >= 0 ? worldX / CELL_SIZE : (worldX - CELL_SIZE + 1) / CELL_SIZE;
            int gy = worldY >= 0 ? worldY / CELL_SIZE : (worldY - CELL_SIZE + 1) / CELL_SIZE;
            int gz = worldZ >= 0 ? worldZ / CELL_SIZE : (worldZ - CELL_SIZE + 1) / CELL_SIZE;
            return (gx, gy, gz);
        }

        /// <summary>
        /// Convert world coordinates to grid cell coordinates (float version)
        /// </summary>
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public static (int gx, int gy, int gz) WorldToGrid(float worldX, float worldY, float worldZ)
        {
            int gx = (int)MathF.Floor(worldX / CELL_SIZE);
            int gy = (int)MathF.Floor(worldY / CELL_SIZE);
            int gz = (int)MathF.Floor(worldZ / CELL_SIZE);
            return (gx, gy, gz);
        }

        /// <summary>
        /// Get the world-space origin (min corner) of a grid cell
        /// </summary>
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public static (int wx, int wy, int wz) GridToWorldOrigin(int gx, int gy, int gz)
        {
            return (gx * CELL_SIZE, gy * CELL_SIZE, gz * CELL_SIZE);
        }

        /// <summary>
        /// Get the world-space center of a grid cell
        /// </summary>
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public static (float wx, float wy, float wz) GridToWorldCenter(int gx, int gy, int gz)
        {
            float half = CELL_SIZE * 0.5f;
            return (gx * CELL_SIZE + half, gy * CELL_SIZE + half, gz * CELL_SIZE + half);
        }

        /// <summary>
        /// Check if a grid cell has been modified by the player
        /// </summary>
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public static bool IsModified(int gx, int gy, int gz)
        {
            return modifiedCells.ContainsKey((gx, gy, gz));
        }

        /// <summary>
        /// Check if a world position is in a modified grid cell
        /// </summary>
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public static bool IsModifiedWorld(int worldX, int worldY, int worldZ)
        {
            var (gx, gy, gz) = WorldToGrid(worldX, worldY, worldZ);
            return IsModified(gx, gy, gz);
        }

        /// <summary>
        /// Get the state of a grid cell
        /// </summary>
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public static GridCell GetCell(int gx, int gy, int gz)
        {
            if (modifiedCells.TryGetValue((gx, gy, gz), out var cell))
                return cell;
            return GridCell.Empty;
        }

        /// <summary>
        /// Get the state of a grid cell at world coordinates
        /// </summary>
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public static GridCell GetCellWorld(int worldX, int worldY, int worldZ)
        {
            var (gx, gy, gz) = WorldToGrid(worldX, worldY, worldZ);
            return GetCell(gx, gy, gz);
        }

        /// <summary>
        /// Remove a block (set grid cell to air)
        /// </summary>
        public static void RemoveBlock(int worldX, int worldY, int worldZ)
        {
            var (gx, gy, gz) = WorldToGrid(worldX, worldY, worldZ);
            
            lock (modifyLock)
            {
                modifiedCells[(gx, gy, gz)] = GridCell.Removed;
            }
            
            Console.WriteLine($"[BlockGrid] Removed cell at grid ({gx},{gy},{gz}) world ({worldX},{worldY},{worldZ})");
        }

        /// <summary>
        /// Place a block (set grid cell to solid)
        /// </summary>
        public static void PlaceBlock(int worldX, int worldY, int worldZ, BlockType blockType)
        {
            var (gx, gy, gz) = WorldToGrid(worldX, worldY, worldZ);
            
            lock (modifyLock)
            {
                modifiedCells[(gx, gy, gz)] = GridCell.Placed(blockType);
            }
            
            Console.WriteLine($"[BlockGrid] Placed {blockType} at grid ({gx},{gy},{gz}) world ({worldX},{worldY},{worldZ})");
        }

        /// <summary>
        /// Clear a modification (revert to natural terrain)
        /// </summary>
        public static bool ClearModification(int gx, int gy, int gz)
        {
            return modifiedCells.TryRemove((gx, gy, gz), out _);
        }

        /// <summary>
        /// Get density for marching cubes sampling.
        /// Returns null if cell is natural (use procedural), otherwise returns grid-based density.
        /// 
        /// CRITICAL: For modified cells, returns the same density for all points within the cell.
        /// This ensures marching cubes creates flat faces (no interpolation).
        /// </summary>
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public static float? GetDensityOverride(int worldX, int worldY, int worldZ)
        {
            var (gx, gy, gz) = WorldToGrid(worldX, worldY, worldZ);
            
            if (!modifiedCells.TryGetValue((gx, gy, gz), out var cell))
                return null;  // Natural terrain - use procedural
            
            return cell.State switch
            {
                CellState.Air => 0f,      // Fully empty - below iso threshold
                CellState.Solid => 1f,    // Fully solid - above iso threshold
                _ => null                  // Natural - use procedural
            };
        }

        /// <summary>
        /// Get block type for a modified cell.
        /// Returns null if cell is natural or air.
        /// </summary>
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public static BlockType? GetBlockTypeOverride(int worldX, int worldY, int worldZ)
        {
            var (gx, gy, gz) = WorldToGrid(worldX, worldY, worldZ);
            
            if (!modifiedCells.TryGetValue((gx, gy, gz), out var cell))
                return null;
            
            if (cell.State == CellState.Solid)
                return cell.BlockType;
            
            return null;
        }

        /// <summary>
        /// Get all modified cells in a chunk (for mesh regeneration)
        /// </summary>
        public static IEnumerable<(int gx, int gy, int gz, GridCell cell)> GetModifiedCellsInChunk(
            int chunkX, int chunkY, int chunkZ, 
            int chunkSizeX = 32, int chunkSizeY = 96, int chunkSizeZ = 32)
        {
            // Calculate grid cell range for this chunk
            int minGx = (chunkX * chunkSizeX) / CELL_SIZE;
            int minGy = (chunkY * chunkSizeY) / CELL_SIZE;
            int minGz = (chunkZ * chunkSizeZ) / CELL_SIZE;
            int maxGx = ((chunkX + 1) * chunkSizeX) / CELL_SIZE;
            int maxGy = ((chunkY + 1) * chunkSizeY) / CELL_SIZE;
            int maxGz = ((chunkZ + 1) * chunkSizeZ) / CELL_SIZE;

            foreach (var kvp in modifiedCells)
            {
                var (gx, gy, gz) = kvp.Key;
                if (gx >= minGx && gx < maxGx &&
                    gy >= minGy && gy < maxGy &&
                    gz >= minGz && gz < maxGz)
                {
                    yield return (gx, gy, gz, kvp.Value);
                }
            }
        }

        /// <summary>
        /// Get chunks affected by a grid cell modification
        /// </summary>
        public static List<(int cx, int cy, int cz)> GetAffectedChunks(
            int gx, int gy, int gz,
            int chunkSizeX = 32, int chunkSizeY = 96, int chunkSizeZ = 32)
        {
            var chunks = new HashSet<(int, int, int)>();
            
            // Get world bounds of the grid cell
            var (wx, wy, wz) = GridToWorldOrigin(gx, gy, gz);
            
            // Check all corners of the grid cell (they might span chunk boundaries)
            for (int dx = 0; dx <= CELL_SIZE; dx += CELL_SIZE)
            {
                for (int dy = 0; dy <= CELL_SIZE; dy += CELL_SIZE)
                {
                    for (int dz = 0; dz <= CELL_SIZE; dz += CELL_SIZE)
                    {
                        int cx = (wx + dx) / chunkSizeX;
                        int cy = (wy + dy) / chunkSizeY;
                        int cz = (wz + dz) / chunkSizeZ;
                        
                        // Handle negative coordinates
                        if (wx + dx < 0) cx = (wx + dx - chunkSizeX + 1) / chunkSizeX;
                        if (wy + dy < 0) cy = (wy + dy - chunkSizeY + 1) / chunkSizeY;
                        if (wz + dz < 0) cz = (wz + dz - chunkSizeZ + 1) / chunkSizeZ;
                        
                        chunks.Add((cx, cy, cz));
                    }
                }
            }
            
            return chunks.ToList();
        }

        /// <summary>
        /// Clear all modifications (for world reset)
        /// </summary>
        public static void ClearAll()
        {
            modifiedCells.Clear();
            Console.WriteLine("[BlockGrid] Cleared all modifications");
        }

        /// <summary>
        /// Get statistics
        /// </summary>
        public static (int totalModified, int airCells, int solidCells) GetStats()
        {
            int air = 0, solid = 0;
            foreach (var cell in modifiedCells.Values)
            {
                if (cell.State == CellState.Air) air++;
                else if (cell.State == CellState.Solid) solid++;
            }
            return (modifiedCells.Count, air, solid);
        }
    }
}
