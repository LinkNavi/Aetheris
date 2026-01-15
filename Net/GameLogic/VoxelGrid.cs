// Net/Shared/GameLogic/VoxelGrid.cs - Core voxel grid system for 7DTD-style terrain
using System;
using System.Collections.Concurrent;
using System.Runtime.CompilerServices;

namespace Aetheris.GameLogic
{
    /// <summary>
    /// Grid unit size - terrain is divided into cubic cells of this size.
    /// Similar to 7 Days to Die's voxel system where terrain is grid-based.
    /// </summary>
    public static class GridConfig
    {
        /// <summary>Block size in world units - the atomic unit of terrain modification</summary>
        public const int BLOCK_SIZE = 2;
        
        /// <summary>Chunk size in blocks (not world units)</summary>
        public const int CHUNK_SIZE_BLOCKS = 16;
        
        /// <summary>Chunk size in world units</summary>
        public const int CHUNK_SIZE = CHUNK_SIZE_BLOCKS * BLOCK_SIZE; // 32
        
        /// <summary>Chunk height in blocks</summary>
        public const int CHUNK_HEIGHT_BLOCKS = 48;
        
        /// <summary>Chunk height in world units</summary>
        public const int CHUNK_HEIGHT = CHUNK_HEIGHT_BLOCKS * BLOCK_SIZE; // 96
        
        /// <summary>Mining removes a cubic section of this size</summary>
        public const int MINE_SIZE = BLOCK_SIZE;
        
        /// <summary>Placed blocks are this size</summary>
        public const int PLACE_SIZE = BLOCK_SIZE;
    }

    /// <summary>
    /// Represents a position on the voxel grid (block coordinates, not world coordinates)
    /// </summary>
    public readonly struct BlockPos : IEquatable<BlockPos>
    {
        public readonly int X;
        public readonly int Y;
        public readonly int Z;

        public BlockPos(int x, int y, int z)
        {
            X = x;
            Y = y;
            Z = z;
        }

        /// <summary>Convert world position to block position</summary>
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public static BlockPos FromWorld(float worldX, float worldY, float worldZ)
        {
            return new BlockPos(
                (int)MathF.Floor(worldX / GridConfig.BLOCK_SIZE),
                (int)MathF.Floor(worldY / GridConfig.BLOCK_SIZE),
                (int)MathF.Floor(worldZ / GridConfig.BLOCK_SIZE)
            );
        }

        /// <summary>Convert world position to block position</summary>
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public static BlockPos FromWorld(int worldX, int worldY, int worldZ)
        {
            return new BlockPos(
                worldX / GridConfig.BLOCK_SIZE - (worldX < 0 && worldX % GridConfig.BLOCK_SIZE != 0 ? 1 : 0),
                worldY / GridConfig.BLOCK_SIZE - (worldY < 0 && worldY % GridConfig.BLOCK_SIZE != 0 ? 1 : 0),
                worldZ / GridConfig.BLOCK_SIZE - (worldZ < 0 && worldZ % GridConfig.BLOCK_SIZE != 0 ? 1 : 0)
            );
        }

        /// <summary>Get the world-space origin of this block</summary>
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public (int x, int y, int z) ToWorldOrigin()
        {
            return (X * GridConfig.BLOCK_SIZE, Y * GridConfig.BLOCK_SIZE, Z * GridConfig.BLOCK_SIZE);
        }

        /// <summary>Get the world-space center of this block</summary>
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public (float x, float y, float z) ToWorldCenter()
        {
            float half = GridConfig.BLOCK_SIZE * 0.5f;
            return (X * GridConfig.BLOCK_SIZE + half, Y * GridConfig.BLOCK_SIZE + half, Z * GridConfig.BLOCK_SIZE + half);
        }

        /// <summary>Get chunk coordinates containing this block</summary>
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public (int cx, int cy, int cz) GetChunk()
        {
            int cx = X >= 0 ? X / GridConfig.CHUNK_SIZE_BLOCKS : (X - GridConfig.CHUNK_SIZE_BLOCKS + 1) / GridConfig.CHUNK_SIZE_BLOCKS;
            int cy = Y >= 0 ? Y / GridConfig.CHUNK_HEIGHT_BLOCKS : (Y - GridConfig.CHUNK_HEIGHT_BLOCKS + 1) / GridConfig.CHUNK_HEIGHT_BLOCKS;
            int cz = Z >= 0 ? Z / GridConfig.CHUNK_SIZE_BLOCKS : (Z - GridConfig.CHUNK_SIZE_BLOCKS + 1) / GridConfig.CHUNK_SIZE_BLOCKS;
            return (cx, cy, cz);
        }

        public BlockPos Offset(int dx, int dy, int dz) => new BlockPos(X + dx, Y + dy, Z + dz);
        public BlockPos Up() => new BlockPos(X, Y + 1, Z);
        public BlockPos Down() => new BlockPos(X, Y - 1, Z);
        public BlockPos North() => new BlockPos(X, Y, Z + 1);
        public BlockPos South() => new BlockPos(X, Y, Z - 1);
        public BlockPos East() => new BlockPos(X + 1, Y, Z);
        public BlockPos West() => new BlockPos(X - 1, Y, Z);

        public bool Equals(BlockPos other) => X == other.X && Y == other.Y && Z == other.Z;
        public override bool Equals(object? obj) => obj is BlockPos other && Equals(other);
        public override int GetHashCode() => HashCode.Combine(X, Y, Z);
        public static bool operator ==(BlockPos a, BlockPos b) => a.Equals(b);
        public static bool operator !=(BlockPos a, BlockPos b) => !a.Equals(b);
        public override string ToString() => $"Block({X}, {Y}, {Z})";
    }

    /// <summary>
    /// Axis-aligned bounding box in block coordinates
    /// </summary>
    public readonly struct BlockBounds
    {
        public readonly BlockPos Min;
        public readonly BlockPos Max;

        public BlockBounds(BlockPos min, BlockPos max)
        {
            Min = min;
            Max = max;
        }

        public BlockBounds(BlockPos center, int radius)
        {
            Min = center.Offset(-radius, -radius, -radius);
            Max = center.Offset(radius, radius, radius);
        }

        public bool Contains(BlockPos pos)
        {
            return pos.X >= Min.X && pos.X <= Max.X &&
                   pos.Y >= Min.Y && pos.Y <= Max.Y &&
                   pos.Z >= Min.Z && pos.Z <= Max.Z;
        }

        public int Volume => (Max.X - Min.X + 1) * (Max.Y - Min.Y + 1) * (Max.Z - Min.Z + 1);
    }

    /// <summary>
    /// Block data stored in the grid
    /// </summary>
    public struct BlockData
    {
        public BlockType Type;
        public byte Damage;      // 0-255 damage level (255 = destroyed)
        public byte Rotation;    // 0-23 rotation states
        public byte Flags;       // Bit flags for various states

        public static BlockData Air => new BlockData { Type = BlockType.Air, Damage = 0, Rotation = 0, Flags = 0 };
        public static BlockData Solid(BlockType type) => new BlockData { Type = type, Damage = 0, Rotation = 0, Flags = 0 };

        public bool IsAir => Type == BlockType.Air;
        public bool IsSolid => Type != BlockType.Air;
        public bool IsDestroyed => Damage >= 255;
    }

    /// <summary>
    /// Central voxel grid storage - thread-safe block access
    /// </summary>
    public class VoxelGrid
    {
        private readonly ConcurrentDictionary<BlockPos, BlockData> modifiedBlocks = new();
        private readonly Func<BlockPos, BlockData>? proceduralGenerator;

        public VoxelGrid(Func<BlockPos, BlockData>? generator = null)
        {
            proceduralGenerator = generator;
        }

        /// <summary>Get block at position (checks modifications first, then procedural)</summary>
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public BlockData GetBlock(BlockPos pos)
        {
            if (modifiedBlocks.TryGetValue(pos, out var block))
                return block;
            
            return proceduralGenerator?.Invoke(pos) ?? BlockData.Air;
        }

        /// <summary>Get block at world coordinates</summary>
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public BlockData GetBlockAtWorld(int worldX, int worldY, int worldZ)
        {
            return GetBlock(BlockPos.FromWorld(worldX, worldY, worldZ));
        }

        /// <summary>Set block at position</summary>
        public void SetBlock(BlockPos pos, BlockData block)
        {
            modifiedBlocks[pos] = block;
        }

        /// <summary>Set block at world coordinates</summary>
        public void SetBlockAtWorld(int worldX, int worldY, int worldZ, BlockData block)
        {
            SetBlock(BlockPos.FromWorld(worldX, worldY, worldZ), block);
        }

        /// <summary>Remove block (set to air)</summary>
        public void RemoveBlock(BlockPos pos)
        {
            modifiedBlocks[pos] = BlockData.Air;
        }

        /// <summary>Check if block is solid</summary>
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public bool IsSolid(BlockPos pos)
        {
            return GetBlock(pos).IsSolid;
        }

        /// <summary>Check if block is air</summary>
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public bool IsAir(BlockPos pos)
        {
            return GetBlock(pos).IsAir;
        }

        /// <summary>Get all modified blocks in a region</summary>
       // public ConcurrentDictionary<BlockPos, BlockData>.Enumerator GetModifiedBlocksEnumerator()
       // {
         //   return modifiedBlocks.GetEnumerator();
       // }

        /// <summary>Clear a specific modification (revert to procedural)</summary>
        public bool ClearModification(BlockPos pos)
        {
            return modifiedBlocks.TryRemove(pos, out _);
        }

        /// <summary>Get total number of modified blocks</summary>
        public int ModifiedBlockCount => modifiedBlocks.Count;
    }
}
