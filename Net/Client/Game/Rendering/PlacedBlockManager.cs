// Net/Client/Game/Rendering/PlacedBlockManager.cs - Fixed to use correct BlockType
using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using OpenTK.Mathematics;
using AetherisClient.Rendering;

namespace Aetheris
{
    public class PlacedBlockManager
    {
        private readonly ConcurrentDictionary<Vector3i, PlacedBlock> placedBlocks = new();
        private readonly ConcurrentDictionary<(int, int, int), HashSet<Vector3i>> blocksByChunk = new();
        
        public class PlacedBlock
        {
            public Vector3i Position { get; set; }
            // FIXED: Use AetherisClient.Rendering.BlockType (for rendering)
            public AetherisClient.Rendering.BlockType BlockType { get; set; }
            public DateTime PlacedTime { get; set; }
            public string PlacedBy { get; set; } = "";
            
            public PlacedBlock(Vector3i position, AetherisClient.Rendering.BlockType blockType)
            {
                Position = position;
                BlockType = blockType;
                PlacedTime = DateTime.UtcNow;
            }
        }
        
        // FIXED: Accept AetherisClient.Rendering.BlockType
        public bool PlaceBlock(int x, int y, int z, AetherisClient.Rendering.BlockType blockType)
        {
            var pos = new Vector3i(x, y, z);
            var block = new PlacedBlock(pos, blockType);
            
            if (!placedBlocks.TryAdd(pos, block))
            {
                return false;
            }
            
            var chunkKey = GetChunkKey(x, y, z);
            var chunkBlocks = blocksByChunk.GetOrAdd(chunkKey, _ => new HashSet<Vector3i>());
            lock (chunkBlocks)
            {
                chunkBlocks.Add(pos);
            }
            
            Console.WriteLine($"[PlacedBlockManager] Placed {blockType} at ({x}, {y}, {z})");
            return true;
        }
        
        public bool RemoveBlock(int x, int y, int z)
        {
            var pos = new Vector3i(x, y, z);
            
            if (!placedBlocks.TryRemove(pos, out var block))
            {
                return false;
            }
            
            var chunkKey = GetChunkKey(x, y, z);
            if (blocksByChunk.TryGetValue(chunkKey, out var chunkBlocks))
            {
                lock (chunkBlocks)
                {
                    chunkBlocks.Remove(pos);
                }
            }
            
            Console.WriteLine($"[PlacedBlockManager] Removed {block.BlockType} at ({x}, {y}, {z})");
            return true;
        }
        
        public bool HasBlockAt(int x, int y, int z)
        {
            return placedBlocks.ContainsKey(new Vector3i(x, y, z));
        }
        
        public PlacedBlock? GetBlockAt(int x, int y, int z)
        {
            placedBlocks.TryGetValue(new Vector3i(x, y, z), out var block);
            return block;
        }
        
        public IEnumerable<PlacedBlock> GetBlocksInChunk(int chunkX, int chunkY, int chunkZ)
        {
            var chunkKey = (chunkX, chunkY, chunkZ);
            
            if (!blocksByChunk.TryGetValue(chunkKey, out var positions))
            {
                yield break;
            }
            
            List<Vector3i> snapshot;
            lock (positions)
            {
                snapshot = new List<Vector3i>(positions);
            }
            
            foreach (var pos in snapshot)
            {
                if (placedBlocks.TryGetValue(pos, out var block))
                {
                    yield return block;
                }
            }
        }
        
        public IEnumerable<PlacedBlock> GetBlocksInRange(Vector3 center, float range)
        {
            int centerChunkX = (int)Math.Floor(center.X / 32);
            int centerChunkY = (int)Math.Floor(center.Y / 96);
            int centerChunkZ = (int)Math.Floor(center.Z / 32);
            
            int chunkRange = (int)Math.Ceiling(range / 32) + 1;
            
            for (int dx = -chunkRange; dx <= chunkRange; dx++)
            {
                for (int dy = -chunkRange; dy <= chunkRange; dy++)
                {
                    for (int dz = -chunkRange; dz <= chunkRange; dz++)
                    {
                        int cx = centerChunkX + dx;
                        int cy = centerChunkY + dy;
                        int cz = centerChunkZ + dz;
                        
                        foreach (var block in GetBlocksInChunk(cx, cy, cz))
                        {
                            float dist = (new Vector3(block.Position.X, block.Position.Y, block.Position.Z) - center).Length;
                            if (dist <= range)
                            {
                                yield return block;
                            }
                        }
                    }
                }
            }
        }
        
        public IEnumerable<PlacedBlock> GetAllBlocks()
        {
            return placedBlocks.Values;
        }
        
        public void Clear()
        {
            placedBlocks.Clear();
            blocksByChunk.Clear();
            Console.WriteLine("[PlacedBlockManager] Cleared all placed blocks");
        }
        
        public (int totalBlocks, int chunks) GetStats()
        {
            return (placedBlocks.Count, blocksByChunk.Count);
        }
        
        private (int, int, int) GetChunkKey(int x, int y, int z)
        {
            return (
                (int)Math.Floor((float)x / 32),
                (int)Math.Floor((float)y / 96),
                (int)Math.Floor((float)z / 32)
            );
        }
    }
    
    public struct Vector3i : IEquatable<Vector3i>
    {
        public int X { get; set; }
        public int Y { get; set; }
        public int Z { get; set; }
        
        public Vector3i(int x, int y, int z)
        {
            X = x;
            Y = y;
            Z = z;
        }
        
        public override bool Equals(object? obj)
        {
            return obj is Vector3i other && Equals(other);
        }
        
        public bool Equals(Vector3i other)
        {
            return X == other.X && Y == other.Y && Z == other.Z;
        }
        
        public override int GetHashCode()
        {
            return HashCode.Combine(X, Y, Z);
        }
        
        public static bool operator ==(Vector3i left, Vector3i right)
        {
            return left.Equals(right);
        }
        
        public static bool operator !=(Vector3i left, Vector3i right)
        {
            return !left.Equals(right);
        }
        
        public override string ToString() => $"({X}, {Y}, {Z})";
    }
}
