// Net/Client/Game/Player/BlockPlacementSystem.cs - Final fix for line 223
using System;
using OpenTK.Mathematics;
using OpenTK.Windowing.GraphicsLibraryFramework;

namespace Aetheris
{
    public class BlockPlacementSystem
    {
        private readonly Player player;
        private readonly Game game;
        private readonly Client? client;
        private readonly RaycastHelper raycaster;

        private const float MAX_REACH = 8f;

        private float placeCooldown = 0f;
        private const float PLACE_COOLDOWN_TIME = 0.2f;

        public BlockPlacementSystem(Player player, Game game, Client? client)
        {
            this.player = player ?? throw new ArgumentNullException(nameof(player));
            this.game = game ?? throw new ArgumentNullException(nameof(game));
            this.client = client;
            this.raycaster = new RaycastHelper(game);
        }

        public void UpdateCooldown(float deltaTime)
        {
            placeCooldown = Math.Max(0f, placeCooldown - deltaTime);
        }

        public void Update(float deltaTime, MouseState mouse, bool isWindowFocused)
        {
            if (!isWindowFocused) return;

            UpdateCooldown(deltaTime);

            if (mouse.IsButtonPressed(MouseButton.Right) && placeCooldown <= 0f)
            {
                TryPlaceBlock();
            }
        }

        public bool TryPlace(Vector3 playerPos, Vector3 lookDir, AetherisClient.Rendering.BlockType blockType)
        {
            if (placeCooldown > 0f) return false;

            Vector3 rayStart = player.GetEyePosition();
            Vector3 rayEnd = rayStart + lookDir.Normalized() * MAX_REACH;

            var hits = raycaster.Raycast(rayStart, rayEnd, raycastAll: false);

            int x, y, z;
            bool foundPlacement = false;

            if (hits.Length > 0 && hits[0].Hit)
            {
                var hit = hits[0];
                Vector3 placePos = hit.Point + hit.Normal * 0.6f;

                x = (int)MathF.Floor(placePos.X);
                y = (int)MathF.Floor(placePos.Y);
                z = (int)MathF.Floor(placePos.Z);

                foundPlacement = true;
            }
            else
            {
                var placedBlockHit = RaycastPlacedBlocks(rayStart, lookDir, MAX_REACH);
                if (placedBlockHit.HasValue)
                {
                    var (hitPos, normal) = placedBlockHit.Value;
                    x = hitPos.X + (int)normal.X;
                    y = hitPos.Y + (int)normal.Y;
                    z = hitPos.Z + (int)normal.Z;
                    foundPlacement = true;
                }
                else
                {
                    return false;
                }
            }

            if (!foundPlacement) return false;

            if (!CanPlaceBlockAt(x, y, z)) return false;

            PlaceBlockAt(x, y, z, blockType);
            placeCooldown = PLACE_COOLDOWN_TIME;
            return true;
        }

        private void TryPlaceBlock()
        {
            var selectedItem = player.Inventory.GetSelectedItem();
            if (selectedItem.ItemId == 0) return;

            var itemDef = ItemRegistry.Get(selectedItem.ItemId);
            if (itemDef == null || !itemDef.PlacesBlock.HasValue) return;

            var blockTypeToPlace = itemDef.PlacesBlock.Value;

            if (TryPlace(player.Position, player.GetForward(), blockTypeToPlace))
            {
                player.Inventory.RemoveItem(selectedItem.ItemId, 1);
                Console.WriteLine($"[BlockPlace] Placed {itemDef.Name}");
            }
        }

        private (Vector3i hitPos, Vector3 normal)? RaycastPlacedBlocks(Vector3 start, Vector3 dir, float maxDist)
        {
            float closestDist = float.MaxValue;
            Vector3i? closestBlock = null;
            Vector3 closestNormal = Vector3.Zero;

            foreach (var block in game.PlacedBlocks.GetBlocksInRange(start, maxDist))
            {
                Vector3 blockCenter = new Vector3(block.Position.X + 0.5f, block.Position.Y + 0.5f, block.Position.Z + 0.5f);

                var hit = RayAABBIntersect(start, dir, blockCenter, 0.5f);
                if (hit.HasValue && hit.Value.dist < closestDist && hit.Value.dist > 0.01f)
                {
                    closestDist = hit.Value.dist;
                    closestBlock = block.Position;
                    closestNormal = hit.Value.normal;
                }
            }

            if (closestBlock.HasValue)
            {
                return (closestBlock.Value, closestNormal);
            }
            return null;
        }

        private (float dist, Vector3 normal)? RayAABBIntersect(Vector3 origin, Vector3 dir, Vector3 center, float halfSize)
        {
            Vector3 min = center - new Vector3(halfSize);
            Vector3 max = center + new Vector3(halfSize);

            float tmin = float.NegativeInfinity;
            float tmax = float.PositiveInfinity;
            Vector3 normal = Vector3.Zero;

            for (int i = 0; i < 3; i++)
            {
                float o = i == 0 ? origin.X : (i == 1 ? origin.Y : origin.Z);
                float d = i == 0 ? dir.X : (i == 1 ? dir.Y : dir.Z);
                float minV = i == 0 ? min.X : (i == 1 ? min.Y : min.Z);
                float maxV = i == 0 ? max.X : (i == 1 ? max.Y : max.Z);

                if (MathF.Abs(d) < 0.0001f)
                {
                    if (o < minV || o > maxV) return null;
                }
                else
                {
                    float t1 = (minV - o) / d;
                    float t2 = (maxV - o) / d;

                    if (t1 > t2) (t1, t2) = (t2, t1);

                    if (t1 > tmin)
                    {
                        tmin = t1;
                        normal = i == 0 ? new Vector3(-MathF.Sign(d), 0, 0) :
                                 i == 1 ? new Vector3(0, -MathF.Sign(d), 0) :
                                          new Vector3(0, 0, -MathF.Sign(d));
                    }

                    tmax = MathF.Min(tmax, t2);

                    if (tmin > tmax) return null;
                }
            }

            if (tmin < 0) return null;
            return (tmin, normal);
        }

        private bool CanPlaceBlockAt(int x, int y, int z)
        {
            if (WouldIntersectPlayer(x, y, z))
            {
                return false;
            }

            if (game.PlacedBlocks.HasBlockAt(x, y, z))
            {
                return false;
            }

            float density = WorldGen.SampleDensity(x, y, z);
            if (density > 0.5f)
            {
                return false;
            }

            return true;
        }

        private bool WouldIntersectPlayer(int x, int y, int z)
        {
            Vector3 blockMin = new Vector3(x, y, z);
            Vector3 blockMax = new Vector3(x + 1, y + 1, z + 1);

            Vector3 playerPos = player.Position;
            const float PLAYER_WIDTH = 1.2f;
            const float PLAYER_HEIGHT = 3.6f;
            float hw = PLAYER_WIDTH / 2f;

            Vector3 playerMin = new Vector3(playerPos.X - hw, playerPos.Y, playerPos.Z - hw);
            Vector3 playerMax = new Vector3(playerPos.X + hw, playerPos.Y + PLAYER_HEIGHT, playerPos.Z + hw);

            return blockMin.X < playerMax.X && blockMax.X > playerMin.X &&
                   blockMin.Y < playerMax.Y && blockMax.Y > playerMin.Y &&
                   blockMin.Z < playerMax.Z && blockMax.Z > playerMin.Z;
        }

        private void PlaceBlockAt(int x, int y, int z, AetherisClient.Rendering.BlockType clientBlockType)
        {
            // Place using client BlockType (for rendering)
            bool placed = game.PlacedBlocks.PlaceBlock(x, y, z, clientBlockType);

            if (!placed) return;

            // Convert to network BlockType for server
            Aetheris.BlockType networkBlockType = (Aetheris.BlockType)((int)clientBlockType);

            if (client != null)
            {
                _ = client.SendBlockPlaceAsync(x, y, z, networkBlockType);
            }
        }

        public (bool canPlace, Vector3 position, Vector3 normal) GetPlacementPreview()
        {
            var selectedItem = player.Inventory.GetSelectedItem();
            if (selectedItem.ItemId == 0) return (false, Vector3.Zero, Vector3.Zero);

            var itemDef = ItemRegistry.Get(selectedItem.ItemId);
            if (itemDef == null || !itemDef.PlacesBlock.HasValue)
                return (false, Vector3.Zero, Vector3.Zero);

            Vector3 rayStart = player.GetEyePosition();
            Vector3 rayDir = player.GetForward().Normalized();
            Vector3 rayEnd = rayStart + rayDir * MAX_REACH;

            var hits = raycaster.Raycast(rayStart, rayEnd, raycastAll: false);

            int x, y, z;
            Vector3 normal = Vector3.Zero;

            if (hits.Length > 0 && hits[0].Hit)
            {
                var hit = hits[0];
                Vector3 placePos = hit.Point + hit.Normal * 0.6f;

                x = (int)MathF.Floor(placePos.X);
                y = (int)MathF.Floor(placePos.Y);
                z = (int)MathF.Floor(placePos.Z);
                normal = hit.Normal;
            }
            else
            {
                var placedBlockHit = RaycastPlacedBlocks(rayStart, rayDir, MAX_REACH);
                if (placedBlockHit.HasValue)
                {
                    var (hitPos, n) = placedBlockHit.Value;
                    x = hitPos.X + (int)n.X;
                    y = hitPos.Y + (int)n.Y;
                    z = hitPos.Z + (int)n.Z;
                    normal = n;
                }
                else
                {
                    return (false, Vector3.Zero, Vector3.Zero);
                }
            }

            bool canPlace = CanPlaceBlockAt(x, y, z);

            return (canPlace, new Vector3(x + 0.5f, y + 0.5f, z + 0.5f), normal);
        }
    }
}
