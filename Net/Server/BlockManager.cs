// Net/Server/ServerPlacedBlockManager.cs - Server-side block placement tracking

using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Numerics;

namespace Aetheris
{
    /// <summary>
    /// Server-side manager for placed blocks (for authoritative state)
    /// </summary>
    public class ServerPlacedBlockManager
    {
        private readonly ConcurrentDictionary<Vector3Int, PlacedBlockData> placedBlocks = new();
        
        public class PlacedBlockData
        {
            public Vector3Int Position { get; set; }
            public byte BlockType { get; set; }
            public DateTime PlacedTime { get; set; }
            public string PlacedBy { get; set; } = "";
            
            public PlacedBlockData(Vector3Int position, byte blockType, string placedBy = "")
            {
                Position = position;
                BlockType = blockType;
                PlacedTime = DateTime.UtcNow;
                PlacedBy = placedBy;
            }
        }
        
        public bool PlaceBlock(int x, int y, int z, byte blockType, string placedBy = "")
        {
            var pos = new Vector3Int(x, y, z);
            var block = new PlacedBlockData(pos, blockType, placedBy);
            
            bool added = placedBlocks.TryAdd(pos, block);
            
            if (added)
            {
                Console.WriteLine($"[ServerBlocks] Placed block type {blockType} at ({x}, {y}, {z}) by {placedBy}");
            }
            
            return added;
        }
        
        public bool RemoveBlock(int x, int y, int z)
        {
            var pos = new Vector3Int(x, y, z);
            bool removed = placedBlocks.TryRemove(pos, out var block);
            
            if (removed)
            {
                Console.WriteLine($"[ServerBlocks] Removed block type {block.BlockType} at ({x}, {y}, {z})");
            }
            
            return removed;
        }
        
        public bool HasBlockAt(int x, int y, int z)
        {
            return placedBlocks.ContainsKey(new Vector3Int(x, y, z));
        }
        
        public PlacedBlockData? GetBlockAt(int x, int y, int z)
        {
            placedBlocks.TryGetValue(new Vector3Int(x, y, z), out var block);
            return block;
        }
        
        public int GetBlockCount() => placedBlocks.Count;
        
        public void Clear()
        {
            placedBlocks.Clear();
            Console.WriteLine("[ServerBlocks] Cleared all placed blocks");
        }
    }
    
    public struct Vector3Int : IEquatable<Vector3Int>
    {
        public int X { get; set; }
        public int Y { get; set; }
        public int Z { get; set; }
        
        public Vector3Int(int x, int y, int z)
        {
            X = x;
            Y = y;
            Z = z;
        }
        
        public override bool Equals(object? obj)
        {
            return obj is Vector3Int other && Equals(other);
        }
        
        public bool Equals(Vector3Int other)
        {
            return X == other.X && Y == other.Y && Z == other.Z;
        }
        
        public override int GetHashCode()
        {
            return HashCode.Combine(X, Y, Z);
        }
        
        public static bool operator ==(Vector3Int left, Vector3Int right)
        {
            return left.Equals(right);
        }
        
        public static bool operator !=(Vector3Int left, Vector3Int right)
        {
            return !left.Equals(right);
        }
        
        public override string ToString() => $"({X}, {Y}, {Z})";
    }
}
