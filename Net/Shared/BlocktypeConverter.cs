// Net/Shared/BlockTypeConverter.cs - Convert between rendering and network block types
using System;

namespace Aetheris
{
    /// <summary>
    /// Converts between client rendering BlockType and shared network BlockType
    /// </summary>
    public static class BlockTypeConverter
    {
        /// <summary>
        /// Convert from rendering BlockType to network BlockType
        /// </summary>
        public static BlockType ToNetworkType(AetherisClient.Rendering.BlockType renderingType)
        {
            // They have the same underlying values, so direct cast works
            return (BlockType)((int)renderingType);
        }
        
        /// <summary>
        /// Convert from network BlockType to rendering BlockType
        /// </summary>
        public static AetherisClient.Rendering.BlockType ToRenderingType(BlockType networkType)
        {
            // They have the same underlying values, so direct cast works
            return (AetherisClient.Rendering.BlockType)((int)networkType);
        }
        
        /// <summary>
        /// Check if a block type is solid (has collision)
        /// </summary>
        public static bool IsSolid(BlockType type)
        {
            return type != BlockType.Air;
        }
        
        /// <summary>
        /// Check if a block type is air
        /// </summary>
        public static bool IsAir(BlockType type)
        {
            return type == BlockType.Air;
        }
    }
}
