// Net/Client/Game/Player/TreeMiningSystem.cs
using System;
using OpenTK.Mathematics;
using Aetheris.GameLogic;

namespace Aetheris
{
    /// <summary>
    /// Handles mining tree prefabs to get wood
    /// </summary>
    public class TreeMiningSystem
    {
        private readonly Player player;
        private readonly GameWorld clientWorld;
        private readonly Client? client;
        private readonly RaycastHelper raycaster;
        private readonly Action<int, int> onItemDropped;  // itemId, count
        
        private BlockPos? currentTarget = null;
        private float miningProgress = 0f;
        private float miningTime = 3f;  // Base time to chop a tree
        
        public TreeMiningSystem(
            Player player, 
            Game game, 
            GameWorld clientWorld,
            Client? client,
            Action<int, int> onItemDropped)
        {
            this.player = player;
            this.clientWorld = clientWorld;
            this.client = client;
            this.raycaster = new RaycastHelper(game);
            this.onItemDropped = onItemDropped;
        }
        
        public void Update(float deltaTime, bool isHoldingMine)
        {
            if (!isHoldingMine)
            {
                if (currentTarget.HasValue)
                {
                    StopMining();
                }
                return;
            }
            
            // Raycast to find tree
            Vector3 rayStart = player.GetEyePosition();
            Vector3 rayDir = player.GetForward().Normalized();
            Vector3 rayEnd = rayStart + rayDir * 8f;
            
            var hits = raycaster.Raycast(rayStart, rayEnd, false);
            
            if (hits.Length == 0 || !hits[0].Hit)
            {
                StopMining();
                return;
            }
            
            var hit = hits[0];
            Vector3 hitPoint = hit.Point;
            
            // Convert to block position
            var blockPos = new BlockPos(
                (int)Math.Floor(hitPoint.X),
                (int)Math.Floor(hitPoint.Y),
                (int)Math.Floor(hitPoint.Z)
            );
            
            // Check if this is a tree prefab
            var prefab = clientWorld.GetPrefabAt(blockPos);
            if (prefab == null)
            {
                StopMining();
                return;
            }
            
            var prefabDef = PrefabRegistry.Get(prefab.PrefabId);
            if (prefabDef == null || !IsTreePrefab(prefabDef))
            {
                StopMining();
                return;
            }
            
            // Start or continue mining
            if (!currentTarget.HasValue || currentTarget.Value != prefab.Position)
            {
                StartMining(prefab.Position, prefabDef);
            }
            
            // Update progress
            float toolMultiplier = GetToolMultiplier();
            miningProgress += deltaTime * toolMultiplier;
            
            if (miningProgress >= miningTime)
            {
                ChopTree(prefab, prefabDef);
                StopMining();
            }
        }
        
        private void StartMining(BlockPos pos, PrefabDefinition prefabDef)
        {
            currentTarget = pos;
            miningProgress = 0f;
            miningTime = GetMiningTime(prefabDef);
            Console.WriteLine($"[TreeMining] Started chopping {prefabDef.Name}");
        }
        
        private void StopMining()
        {
            currentTarget = null;
            miningProgress = 0f;
        }
        
        private void ChopTree(PlacedPrefab prefab, PrefabDefinition prefabDef)
        {
            Console.WriteLine($"[TreeMining] Chopped {prefabDef.Name}!");
            
            // Calculate wood drops based on tree size
            int woodCount = CalculateWoodDrop(prefabDef);
            
            // Drop items
            onItemDropped?.Invoke(7, woodCount);  // Wood item (ID 7)
            
            // Remove tree from world (send to server)
            if (client != null)
            {
                // Mine the base block of the tree to remove it
                var pos = prefab.Position;
                _ = client.MineBlockAsync(pos.X, pos.Y, pos.Z);
            }
            else
            {
                // Single-player: remove directly
                clientWorld.RemovePrefab(prefab.Position);
            }
        }
        
        private bool IsTreePrefab(PrefabDefinition def)
        {
            // Check if it's tagged as a tree
            foreach (var tag in def.Tags)
            {
                if (tag == "tree" || tag == "mineable")
                    return true;
            }
            return false;
        }
        
        private float GetMiningTime(PrefabDefinition def)
        {
            // Base time depends on tree size
            int volume = def.BlockSize.x * def.BlockSize.y * def.BlockSize.z;
            return 2f + (volume * 0.3f);  // Larger trees take longer
        }
        
        private float GetToolMultiplier()
        {
            var selectedItem = player.Inventory.GetSelectedItem();
            if (selectedItem.ItemId == 0)
                return 1f;
            
            var itemDef = ItemRegistry.Get(selectedItem.ItemId);
            if (itemDef == null)
                return 1f;
            
            // Axes are fastest
            if (itemDef.ToolType == ToolType.Axe)
                return itemDef.MiningSpeed;
            
            // Any tool is better than hand
            if (itemDef.Category == ItemCategory.Tool)
                return itemDef.MiningSpeed * 0.5f;
            
            return 1f;
        }
        
        private int CalculateWoodDrop(PrefabDefinition def)
        {
            // Drop more wood for bigger trees
            int volume = def.BlockSize.x * def.BlockSize.y * def.BlockSize.z;
            int baseWood = 3;
            int bonusWood = volume / 2;
            return baseWood + bonusWood;
        }
        
        public (bool isMining, float progress) GetMiningInfo()
        {
            return (currentTarget.HasValue, currentTarget.HasValue ? miningProgress / miningTime : 0f);
        }
    }
}
