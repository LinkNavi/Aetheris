using System;
using System.Collections.Generic;
using OpenTK.Mathematics;

namespace Aetheris
{
    public static class CubeBlockMeshGenerator
    {
        public static float[] GenerateCubeMesh(int worldX, int worldY, int worldZ, BlockType blockType)
        {
            var vertices = new List<float>();
            
            float x = worldX + 0.5f;
            float y = worldY + 0.5f;
            float z = worldZ + 0.5f;
            float size = 0.5f;
            
            // Only render faces exposed to air
            bool renderTop = !IsSolidAt(worldX, worldY + 1, worldZ);
            bool renderBottom = !IsSolidAt(worldX, worldY - 1, worldZ);
            bool renderNorth = !IsSolidAt(worldX, worldY, worldZ + 1);
            bool renderSouth = !IsSolidAt(worldX, worldY, worldZ - 1);
            bool renderEast = !IsSolidAt(worldX + 1, worldY, worldZ);
            bool renderWest = !IsSolidAt(worldX - 1, worldY, worldZ);
            
            // Top face
            if (renderTop)
            {
                AddQuad(vertices, 
                    new Vector3(x - size, y + size, z - size),
                    new Vector3(x + size, y + size, z - size),
                    new Vector3(x + size, y + size, z + size),
                    new Vector3(x - size, y + size, z + size),
                    new Vector3(0, 1, 0), blockType);
            }
            
            // Bottom face
            if (renderBottom)
            {
                AddQuad(vertices,
                    new Vector3(x - size, y - size, z - size),
                    new Vector3(x - size, y - size, z + size),
                    new Vector3(x + size, y - size, z + size),
                    new Vector3(x + size, y - size, z - size),
                    new Vector3(0, -1, 0), blockType);
            }
            
            // North face (+Z)
            if (renderNorth)
            {
                AddQuad(vertices,
                    new Vector3(x - size, y - size, z + size),
                    new Vector3(x - size, y + size, z + size),
                    new Vector3(x + size, y + size, z + size),
                    new Vector3(x + size, y - size, z + size),
                    new Vector3(0, 0, 1), blockType);
            }
            
            // South face (-Z)
            if (renderSouth)
            {
                AddQuad(vertices,
                    new Vector3(x + size, y - size, z - size),
                    new Vector3(x + size, y + size, z - size),
                    new Vector3(x - size, y + size, z - size),
                    new Vector3(x - size, y - size, z - size),
                    new Vector3(0, 0, -1), blockType);
            }
            
            // East face (+X)
            if (renderEast)
            {
                AddQuad(vertices,
                    new Vector3(x + size, y - size, z + size),
                    new Vector3(x + size, y + size, z + size),
                    new Vector3(x + size, y + size, z - size),
                    new Vector3(x + size, y - size, z - size),
                    new Vector3(1, 0, 0), blockType);
            }
            
            // West face (-X)
            if (renderWest)
            {
                AddQuad(vertices,
                    new Vector3(x - size, y - size, z - size),
                    new Vector3(x - size, y + size, z - size),
                    new Vector3(x - size, y + size, z + size),
                    new Vector3(x - size, y - size, z + size),
                    new Vector3(-1, 0, 0), blockType);
            }
            
            return vertices.ToArray();
        }
        
        private static void AddQuad(List<float> vertices, Vector3 v0, Vector3 v1, Vector3 v2, Vector3 v3, Vector3 normal, BlockType blockType)
        {
            AddVertex(vertices, v0, normal, blockType);
            AddVertex(vertices, v1, normal, blockType);
            AddVertex(vertices, v2, normal, blockType);
            
            AddVertex(vertices, v0, normal, blockType);
            AddVertex(vertices, v2, normal, blockType);
            AddVertex(vertices, v3, normal, blockType);
        }
        
        private static void AddVertex(List<float> vertices, Vector3 pos, Vector3 normal, BlockType blockType)
        {
            vertices.Add(pos.X);
            vertices.Add(pos.Y);
            vertices.Add(pos.Z);
            vertices.Add(normal.X);
            vertices.Add(normal.Y);
            vertices.Add(normal.Z);
            vertices.Add((float)blockType);
        }
        
        private static bool IsSolidAt(int x, int y, int z)
        {
            float density = WorldGen.SampleDensity(x, y, z);
            return density > 0.5f;
        }
    }
}
