// Net/GameLogic/NetworkHelpers.cs - Shared network utilities
using System;
using System.Collections.Generic;
using System.Linq;
using OpenTK.Mathematics;

namespace Aetheris.GameLogic
{
    /// <summary>
    /// Helper methods for chunk invalidation and network operations
    /// Shared between client and server to ensure consistency
    /// </summary>
    public static class NetworkHelpers
    {
        /// <summary>
        /// Get all chunks that should be invalidated when a block is modified
        /// </summary>
        public static List<(int cx, int cy, int cz)> GetAffectedChunks(int blockX, int blockY, int blockZ, int chunkSize = 32, int chunkSizeY = 96)
        {
            int cx = blockX / chunkSize;
            int cy = blockY / chunkSizeY;
            int cz = blockZ / chunkSize;
            
            var chunks = new HashSet<(int, int, int)>();
            
            // Add main chunk
            chunks.Add((cx, cy, cz));
            
            // Check if we're near chunk boundaries
            int localX = blockX - (cx * chunkSize);
            int localY = blockY - (cy * chunkSizeY);
            int localZ = blockZ - (cz * chunkSize);
            
            // Add neighbor chunks if near boundary (within 1 block)
            if (localX <= 0) chunks.Add((cx - 1, cy, cz));
            if (localX >= chunkSize - 1) chunks.Add((cx + 1, cy, cz));
            if (localY <= 0) chunks.Add((cx, cy - 1, cz));
            if (localY >= chunkSizeY - 1) chunks.Add((cx, cy + 1, cz));
            if (localZ <= 0) chunks.Add((cx, cy, cz - 1));
            if (localZ >= chunkSize - 1) chunks.Add((cx, cy, cz + 1));
            
            return chunks.ToList();
        }
        
        /// <summary>
        /// Get chunks affected by a mining operation with radius
        /// </summary>
        public static List<(int cx, int cy, int cz)> GetAffectedChunksRadius(int blockX, int blockY, int blockZ, float radius, int chunkSize = 32, int chunkSizeY = 96)
        {
            int affectRadius = (int)Math.Ceiling(radius);
            var chunks = new HashSet<(int, int, int)>();
            
            for (int dx = -affectRadius; dx <= affectRadius; dx++)
            {
                for (int dy = -affectRadius; dy <= affectRadius; dy++)
                {
                    for (int dz = -affectRadius; dz <= affectRadius; dz++)
                    {
                        int wx = blockX + dx;
                        int wy = blockY + dy;
                        int wz = blockZ + dz;
                        
                        int cx = wx / chunkSize;
                        int cy = wy / chunkSizeY;
                        int cz = wz / chunkSize;
                        
                        chunks.Add((cx, cy, cz));
                    }
                }
            }
            
            return chunks.ToList();
        }
        
        /// <summary>
        /// Calculate distance priority for chunk loading
        /// </summary>
        public static float CalculateChunkPriority(int chunkX, int chunkY, int chunkZ, Vector3 playerPosition, int chunkSize = 32, int chunkSizeY = 96)
        {
            // Calculate chunk center in world coordinates
            float centerX = chunkX * chunkSize + chunkSize * 0.5f;
            float centerY = chunkY * chunkSizeY + chunkSizeY * 0.5f;
            float centerZ = chunkZ * chunkSize + chunkSize * 0.5f;
            
            // Calculate distance to player
            float dx = centerX - playerPosition.X;
            float dy = centerY - playerPosition.Y;
            float dz = centerZ - playerPosition.Z;
            
            // Weight Y distance less (players typically move horizontally)
            float distance = MathF.Sqrt(dx * dx + dy * dy * 0.25f + dz * dz);
            
            return distance;
        }
        
        /// <summary>
        /// Check if a chunk should be loaded based on distance
        /// </summary>
        public static bool ShouldLoadChunk(int chunkX, int chunkY, int chunkZ, Vector3 playerPosition, int renderDistance, int chunkSize = 32, int chunkSizeY = 96)
        {
            int playerCx = (int)Math.Floor(playerPosition.X / chunkSize);
            int playerCy = (int)Math.Floor(playerPosition.Y / chunkSizeY);
            int playerCz = (int)Math.Floor(playerPosition.Z / chunkSize);
            
            int dx = chunkX - playerCx;
            int dy = chunkY - playerCy;
            int dz = chunkZ - playerCz;
            
            // Horizontal distance check
            float horizontalDist = MathF.Sqrt(dx * dx + dz * dz);
            if (horizontalDist > renderDistance)
                return false;
            
            // Vertical distance check (more lenient)
            if (Math.Abs(dy) > 3)
                return false;
            
            // Y-height check (don't load chunks too far above/below player)
            int chunkCenterY = chunkY * chunkSizeY + chunkSizeY / 2;
            int playerBlockY = (int)playerPosition.Y;
            int yDistance = Math.Abs(chunkCenterY - playerBlockY);
            
            if (yDistance > 150)
                return false;
            
            return true;
        }
        
        /// <summary>
        /// Get chunks that should be unloaded (outside render distance)
        /// </summary>
        public static List<(int cx, int cy, int cz)> GetChunksToUnload(
            IEnumerable<(int cx, int cy, int cz)> loadedChunks,
            Vector3 playerPosition,
            int renderDistance,
            int chunkSize = 32,
            int chunkSizeY = 96)
        {
            var toUnload = new List<(int cx, int cy, int cz)>();
            int unloadDist = renderDistance + 2;
            
            int playerCx = (int)Math.Floor(playerPosition.X / chunkSize);
            int playerCy = (int)Math.Floor(playerPosition.Y / chunkSizeY);
            int playerCz = (int)Math.Floor(playerPosition.Z / chunkSize);
            
            foreach (var (cx, cy, cz) in loadedChunks)
            {
                int dx = cx - playerCx;
                int dy = cy - playerCy;
                int dz = cz - playerCz;
                
                float dist = MathF.Sqrt(dx * dx + dz * dz);
                
                if (dist > unloadDist || Math.Abs(dy) > 3)
                {
                    toUnload.Add((cx, cy, cz));
                }
            }
            
            return toUnload;
        }
        
        /// <summary>
        /// Convert world position to block position
        /// </summary>
        public static (int x, int y, int z) WorldToBlock(Vector3 worldPos)
        {
            return (
                (int)Math.Floor(worldPos.X),
                (int)Math.Floor(worldPos.Y),
                (int)Math.Floor(worldPos.Z)
            );
        }
        
        /// <summary>
        /// Convert world position to chunk coordinates
        /// </summary>
        public static (int cx, int cy, int cz) WorldToChunk(Vector3 worldPos, int chunkSize = 32, int chunkSizeY = 96)
        {
            return (
                (int)Math.Floor(worldPos.X / chunkSize),
                (int)Math.Floor(worldPos.Y / chunkSizeY),
                (int)Math.Floor(worldPos.Z / chunkSize)
            );
        }
        
        /// <summary>
        /// Convert block position to chunk coordinates
        /// </summary>
        public static (int cx, int cy, int cz) BlockToChunk(int x, int y, int z, int chunkSize = 32, int chunkSizeY = 96)
        {
            return (
                x / chunkSize,
                y / chunkSizeY,
                z / chunkSize
            );
        }
    }
    
    /// <summary>
    /// Extension methods for GameWorld to simplify network operations
    /// </summary>
    public static class GameWorldNetworkExtensions
    {
        /// <summary>
        /// Apply a block modification and return affected chunks
        /// </summary>
        public static (bool success, List<(int cx, int cy, int cz)> affectedChunks) ModifyBlockWithChunks(
            this GameWorld world,
            int x, int y, int z,
            bool isMine,
            BlockType blockType = BlockType.Air,
            byte rotation = 0)
        {
            var pos = new BlockPos(x, y, z);
            TerrainModifyResult result;
            
            if (isMine)
            {
                result = world.MineBlock(pos);
            }
            else
            {
                result = world.PlaceBlock(pos, blockType, rotation);
            }
            
            if (result.Success)
            {
                return (true, result.AffectedChunks.ToList());
            }
            
            return (false, new List<(int cx, int cy, int cz)>());
        }
        
        /// <summary>
        /// Check if a block can be placed at position (for validation)
        /// </summary>
        public static bool CanPlaceBlockAt(this GameWorld world, int x, int y, int z)
        {
            var pos = new BlockPos(x, y, z);
            return world.CanPlace(pos);
        }
        
        /// <summary>
        /// Check if a block can be mined at position (for validation)
        /// </summary>
        public static bool CanMineBlockAt(this GameWorld world, int x, int y, int z)
        {
            var pos = new BlockPos(x, y, z);
            return world.Modifier.CanMine(pos);
        }
    }
}
