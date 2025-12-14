// Net/Server/Game/VoxelCubeManager.cs
using System.Collections.Concurrent;
using OpenTK.Mathematics;

namespace Aetheris.Server
{
    public class VoxelCubeManager
    {
        private readonly ConcurrentDictionary<Vector3i, VoxelCubeData> cubes = new();
        
        public bool TryPlaceCube(int x, int y, int z, byte blockType)
        {
            var pos = new Vector3i(x, y, z);
            
            if (cubes.ContainsKey(pos))
                return false;
            
            cubes[pos] = new VoxelCubeData
            {
                Position = pos,
                BlockType = blockType,
                Timestamp = System.DateTime.UtcNow.Ticks
            };
            
            return true;
        }
        
        public bool TryRemoveCube(int x, int y, int z)
        {
            return cubes.TryRemove(new Vector3i(x, y, z), out _);
        }
        
        public VoxelCubeData[] GetAllCubes()
        {
            return cubes.Values.ToArray();
        }
        
        public VoxelCubeData[] GetCubesInRadius(Vector3 center, float radius)
        {
            float radiusSq = radius * radius;
            var result = new List<VoxelCubeData>();
            
            foreach (var cube in cubes.Values)
            {
                Vector3 pos = new(cube.Position.X, cube.Position.Y, cube.Position.Z);
                if ((pos - center).LengthSquared <= radiusSq)
                    result.Add(cube);
            }
            
            return result.ToArray();
        }
        
        public bool CheckCollision(Vector3 position, float radius)
        {
            foreach (var cube in cubes.Values)
            {
                Vector3 center = new(cube.Position.X, cube.Position.Y, cube.Position.Z);
                if ((position - center).Length < 0.5f + radius)
                    return true;
            }
            return false;
        }
    }
    
    public struct VoxelCubeData
    {
        public Vector3i Position;
        public byte BlockType;
        public long Timestamp;
    }
}
