// Net/Client/Game/Player/SimpleVoxelPlacer.cs
using System;
using System.Collections.Generic;
using OpenTK.Mathematics;
using OpenTK.Windowing.GraphicsLibraryFramework;

namespace Aetheris
{
    public class SimpleVoxelPlacer
    {
        private readonly Player player;
        private readonly Game game;
        private readonly Client client;
        private readonly RaycastHelper raycaster;
        
        public readonly Dictionary<Vector3i, VoxelCube> placedCubes = new();
        
        private const float MAX_REACH = 10f;
        private float placeCooldown = 0f;
        
        public SimpleVoxelPlacer(Player player, Game game, Client client)
        {
            this.player = player;
            this.game = game;
            this.client = client;
            this.raycaster = new RaycastHelper(game);
        }
        
        public void Update(float deltaTime, MouseState mouse, bool focused)
        {
            if (!focused) return;
            placeCooldown = Math.Max(0f, placeCooldown - deltaTime);
            
            if (mouse.IsButtonPressed(MouseButton.Right) && placeCooldown <= 0f)
                TryPlaceBlock();
        }
        
        private void TryPlaceBlock()
        {
            var item = player.Inventory.GetSelectedItem();
            if (item.ItemId == 0) return;
            
            var itemDef = ItemRegistry.Get(item.ItemId);
            if (itemDef?.PlacesBlock == null) return;
            
            var blockType = itemDef.PlacesBlock.Value;
            
            Vector3 rayStart = player.GetEyePosition();
            Vector3 rayDir = player.GetForward().Normalized();
            Vector3 rayEnd = rayStart + rayDir * MAX_REACH;
            
            var hits = raycaster.Raycast(rayStart, rayEnd, false);
            if (hits.Length == 0 || !hits[0].Hit) return;
            
            var hit = hits[0];
            Vector3 placePos = hit.Point + hit.Normal * 0.5f;
            
            int x = (int)Math.Round(placePos.X);
            int y = (int)Math.Round(placePos.Y);
            int z = (int)Math.Round(placePos.Z);
            
            Vector3i pos = new(x, y, z);
            
            if (WouldIntersectPlayer(x, y, z) || placedCubes.ContainsKey(pos))
                return;
            
            placedCubes[pos] = new VoxelCube { Position = pos, BlockType = blockType };
            
            client?.SendBlockPlaceAsync(x, y, z, (byte)blockType);
            player.Inventory.RemoveItem(item.ItemId, 1);
            
            placeCooldown = 0.15f;
        }
        
        private bool WouldIntersectPlayer(int x, int y, int z)
        {
            Vector3 blockCenter = new(x, y, z);
            Vector3 playerPos = player.Position;
            
            float dx = Math.Abs(blockCenter.X - playerPos.X);
            float dy = Math.Abs(blockCenter.Y - playerPos.Y);
            float dz = Math.Abs(blockCenter.Z - playerPos.Z);
            
            return dx < 1.1f && dy < 2.3f && dz < 1.1f;
        }
        
        public void AddCube(int x, int y, int z, AetherisClient.Rendering.BlockType blockType)
        {
            placedCubes[new Vector3i(x, y, z)] = new VoxelCube 
            { 
                Position = new(x, y, z), 
                BlockType = blockType 
            };
        }
        
        public bool CheckCollision(Vector3 pos, float radius)
        {
            foreach (var cube in placedCubes.Values)
            {
                Vector3 center = new(cube.Position.X, cube.Position.Y, cube.Position.Z);
                if ((pos - center).Length < 0.5f + radius)
                    return true;
            }
            return false;
        }
    }
    
    public struct VoxelCube
    {
        public Vector3i Position;
        public AetherisClient.Rendering.BlockType BlockType;
    }
}
