// Net/Client/Game/Player/BlockPlacementSystem.cs - Client-side placement with GameLogic integration
using System;
using OpenTK.Mathematics;
using Aetheris.GameLogic;

namespace Aetheris
{
    /// <summary>
    /// Client-side block placement system that integrates with GameLogic
    /// Handles placement preview, validation, and network communication
    /// </summary>
    public class BlockPlacementSystem
    {
        private readonly Player player;
        private readonly Game game;
        private readonly Client? client;
        private readonly RaycastHelper raycaster;
        
        private float placementCooldown = 0f;
        private const float PLACEMENT_COOLDOWN_TIME = 0.2f;
        private const float MAX_PLACEMENT_DISTANCE = 8f;
        
        public BlockPlacementSystem(Player player, Game game, Client? client)
        {
            this.player = player ?? throw new ArgumentNullException(nameof(player));
            this.game = game ?? throw new ArgumentNullException(nameof(game));
            this.client = client;
            this.raycaster = new RaycastHelper(game);
        }
        
        public void UpdateCooldown(float deltaTime)
        {
            if (placementCooldown > 0)
                placementCooldown -= deltaTime;
        }
        
        /// <summary>
        /// Get placement preview for rendering ghost block
        /// </summary>
        public (Vector3 position, bool canPlace) GetPlacementPreview()
        {
            if (placementCooldown > 0)
                return (Vector3.Zero, false);
            
            Vector3 rayStart = player.GetEyePosition();
            Vector3 rayDir = player.GetForward().Normalized();
            Vector3 rayEnd = rayStart + rayDir * MAX_PLACEMENT_DISTANCE;
            
            var hits = raycaster.Raycast(rayStart, rayEnd, raycastAll: false);
            
            if (hits.Length == 0 || !hits[0].Hit)
                return (Vector3.Zero, false);
            
            var hit = hits[0];
            
            // Calculate placement position (adjacent to hit block)
            Vector3 placementPos = hit.Point + hit.Normal * 0.1f;
            
            // Snap to grid
            int blockX = (int)Math.Floor(placementPos.X);
            int blockY = (int)Math.Floor(placementPos.Y);
            int blockZ = (int)Math.Floor(placementPos.Z);
            
            // Check if position is valid (not inside player)
            bool canPlace = IsValidPlacementPosition(blockX, blockY, blockZ);
            
            return (new Vector3(blockX + 0.5f, blockY + 0.5f, blockZ + 0.5f), canPlace);
        }
        
        /// <summary>
        /// Try to place a block at the current look target
        /// </summary>
        public bool TryPlace(Vector3 playerPos, Vector3 lookDir, AetherisClient.Rendering.BlockType blockType)
        {
            if (placementCooldown > 0)
                return false;
            
            Vector3 rayStart = player.GetEyePosition();
            Vector3 rayDir = player.GetForward().Normalized();
            Vector3 rayEnd = rayStart + rayDir * MAX_PLACEMENT_DISTANCE;
            
            var hits = raycaster.Raycast(rayStart, rayEnd, raycastAll: false);
            
            if (hits.Length == 0 || !hits[0].Hit)
                return false;
            
            var hit = hits[0];
            
            // Calculate placement position
            Vector3 placementPos = hit.Point + hit.Normal * 0.1f;
            
            int blockX = (int)Math.Floor(placementPos.X);
            int blockY = (int)Math.Floor(placementPos.Y);
            int blockZ = (int)Math.Floor(placementPos.Z);
            
            // Validate placement
            if (!IsValidPlacementPosition(blockX, blockY, blockZ))
                return false;
            
            // Convert rendering BlockType to network BlockType
            var networkBlockType = (BlockType)((int)blockType);
            
            // Place locally for immediate feedback (if we have a client world)
            if (client != null)
            {
                // Send to server with prediction
                _ = client.PlaceBlockAsync(blockX, blockY, blockZ, networkBlockType);
                Console.WriteLine($"[Placement] Placed {blockType} at ({blockX}, {blockY}, {blockZ}) with prediction");
            }
            else
            {
                // Single-player: place directly in world
                WorldGen.PlaceSolidBlock(blockX, blockY, blockZ, networkBlockType);
                
                // Invalidate chunks
                game.Renderer.ClearChunkMesh(
                    blockX / ClientConfig.CHUNK_SIZE,
                    blockY / ClientConfig.CHUNK_SIZE_Y,
                    blockZ / ClientConfig.CHUNK_SIZE
                );
                
                Console.WriteLine($"[Placement] Placed {blockType} at ({blockX}, {blockY}, {blockZ})");
            }
            
            placementCooldown = PLACEMENT_COOLDOWN_TIME;
            return true;
        }
        
        /// <summary>
        /// Check if a position is valid for block placement
        /// </summary>
        private bool IsValidPlacementPosition(int x, int y, int z)
        {
            // Check if position is already solid
            if (WorldGen.IsSolid(x, y, z))
                return false;
            
            // Check if placement would intersect with player
            Vector3 blockCenter = new Vector3(x + 0.5f, y + 0.5f, z + 0.5f);
            float distanceToPlayer = Vector3.Distance(blockCenter, player.Position);
            
            if (distanceToPlayer < 1.5f) // Player collision radius
                return false;
            
            return true;
        }
        
        /// <summary>
        /// Get the block that would be placed at current look position
        /// </summary>
        public (bool found, int x, int y, int z) GetTargetPosition()
        {
            var preview = GetPlacementPreview();
            if (!preview.canPlace)
                return (false, 0, 0, 0);
            
            int x = (int)Math.Floor(preview.position.X);
            int y = (int)Math.Floor(preview.position.Y);
            int z = (int)Math.Floor(preview.position.Z);
            
            return (true, x, y, z);
        }
    }
}
