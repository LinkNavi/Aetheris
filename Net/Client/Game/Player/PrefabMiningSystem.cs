// Net/Client/Game/Player/PrefabMiningSystem.cs
using System;
using OpenTK.Mathematics;
using Aetheris.GameLogic;

namespace Aetheris
{
    /// <summary>
    /// Handles mining prefabs (trees, ores, etc.) to get resources
    /// </summary>
    public class PrefabMiningSystem
    {
        private readonly Player player;
        private readonly GameWorld clientWorld;
        private readonly Client? client;
        private readonly RaycastHelper raycaster;
        private readonly Action<int, int> onItemDropped;  // itemId, count
        
        private PlacedPrefab? currentTarget = null;
        private float miningProgress = 0f;
        private float miningTime = 3f;
        private float timeSinceStart = 0f;
        
        // Mining config
        public float ProgressDecayRate { get; set; } = 2.0f;
        public float ResetDelay { get; set; } = 0.5f;
        private float timeSinceStopped = 0f;
        
        public bool IsMining => currentTarget != null;
        public float Progress => currentTarget != null ? miningProgress / miningTime : 0f;
        
        public PrefabMiningSystem(
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
                if (currentTarget != null)
                {
                    timeSinceStopped += deltaTime;
                    miningProgress = Math.Max(0, miningProgress - ProgressDecayRate * deltaTime);
                    
                    if (timeSinceStopped >= ResetDelay || miningProgress <= 0f)
                    {
                        StopMining();
                    }
                }
                return;
            }
            
            timeSinceStopped = 0f;
            
            // Raycast to find prefab
            Vector3 rayStart = player.GetEyePosition();
            Vector3 rayDir = player.GetForward().Normalized();
            Vector3 rayEnd = rayStart + rayDir * 8f;
            
            var hits = raycaster.Raycast(rayStart, rayEnd, false);
            
            if (hits.Length == 0 || !hits[0].Hit)
            {
                if (currentTarget != null)
                {
                    timeSinceStopped += deltaTime;
                    if (timeSinceStopped >= ResetDelay)
                    {
                        StopMining();
                    }
                }
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
            
            // Check if this is a mineable prefab
            var prefab = clientWorld.GetPrefabAt(blockPos);
            if (prefab == null)
            {
                StopMining();
                return;
            }
            
            var prefabDef = PrefabRegistry.Get(prefab.PrefabId);
            if (prefabDef == null || !IsMineablePrefab(prefabDef))
            {
                StopMining();
                return;
            }
            
            // Start or continue mining
            if (currentTarget == null || !AreSamePrefab(currentTarget, prefab))
            {
                StartMining(prefab, prefabDef);
            }
            
            // Update progress
            float toolMultiplier = GetToolMultiplier(prefabDef);
            timeSinceStart += deltaTime;
            miningProgress += deltaTime * toolMultiplier;
            
            if (miningProgress >= miningTime)
            {
                MinePrefab(prefab, prefabDef);
                StopMining();
            }
        }
        
        private void StartMining(PlacedPrefab prefab, PrefabDefinition prefabDef)
        {
            currentTarget = prefab;
            miningProgress = 0f;
            timeSinceStart = 0f;
            timeSinceStopped = 0f;
            miningTime = GetMiningTime(prefabDef);
            Console.WriteLine($"[PrefabMining] Started mining {prefabDef.Name}");
        }
        
        private void StopMining()
        {
            currentTarget = null;
            miningProgress = 0f;
            timeSinceStart = 0f;
        }
        
        private void MinePrefab(PlacedPrefab prefab, PrefabDefinition prefabDef)
        {
            Console.WriteLine($"[PrefabMining] Mined {prefabDef.Name}!");
            
            // Get drops from prefab definition tags
            var drops = GetPrefabDrops(prefabDef);
            
            foreach (var (itemId, count) in drops)
            {
                onItemDropped?.Invoke(itemId, count);
                Console.WriteLine($"[PrefabMining] Dropped {count}x item {itemId}");
            }
            
            // Remove prefab from world (send to server)
            if (client != null)
            {
                // Mine the base block to trigger prefab removal on server
                var pos = prefab.Position;
                _ = client.MineBlockAsync(pos.X, pos.Y, pos.Z);
            }
            else
            {
                // Single-player: remove directly
                clientWorld.RemovePrefab(prefab.Position);
            }
        }
        
        private bool IsMineablePrefab(PrefabDefinition def)
        {
            // Check if prefab has "mineable" tag
            foreach (var tag in def.Tags)
            {
                if (tag == "mineable" || tag == "tree" || tag == "ore")
                    return true;
            }
            return false;
        }
        
        private float GetMiningTime(PrefabDefinition def)
        {
            // Base time from prefab category
            float baseTime = 3f;
            
            if (HasTag(def, "tree"))
            {
                // Trees take longer based on size
                int volume = def.BlockSize.x * def.BlockSize.y * def.BlockSize.z;
                baseTime = 2f + (volume * 0.3f);
            }
            else if (HasTag(def, "ore"))
            {
                // Ores take time based on rarity
                if (HasTag(def, "common")) baseTime = 2f;
                else if (HasTag(def, "uncommon")) baseTime = 3f;
                else if (HasTag(def, "rare")) baseTime = 4f;
                else if (HasTag(def, "epic")) baseTime = 5f;
                else baseTime = 3f;
            }
            
            return baseTime;
        }
        
        private float GetToolMultiplier(PrefabDefinition def)
        {
            var selectedItem = player.Inventory.GetSelectedItem();
            if (selectedItem.ItemId == 0)
                return 1f;
            
            var itemDef = ItemRegistry.Get(selectedItem.ItemId);
            if (itemDef == null)
                return 1f;
            
            // Trees prefer axes
            if (HasTag(def, "tree"))
            {
                if (itemDef.ToolType == ToolType.Axe)
                    return itemDef.MiningSpeed;
                else if (itemDef.Category == ItemCategory.Tool)
                    return itemDef.MiningSpeed * 0.5f;
            }
            // Ores prefer pickaxes
            else if (HasTag(def, "ore"))
            {
                if (itemDef.ToolType == ToolType.Pickaxe)
                    return itemDef.MiningSpeed;
                else if (itemDef.Category == ItemCategory.Tool)
                    return itemDef.MiningSpeed * 0.3f;
            }
            
            return 1f;
        }
        
        private (int itemId, int count)[] GetPrefabDrops(PrefabDefinition def)
        {
            var drops = new System.Collections.Generic.List<(int, int)>();
            
            if (HasTag(def, "tree"))
            {
                // Trees drop wood
                int volume = def.BlockSize.x * def.BlockSize.y * def.BlockSize.z;
                int woodCount = 3 + (volume / 2);
                drops.Add((7, woodCount)); // Wood item
            }
            else if (HasTag(def, "ore"))
            {
                // Parse ore type from tags
                int oreItemId = GetOreItemId(def);
                int oreCount = GetOreCount(def);
                if (oreItemId > 0)
                {
                    drops.Add((oreItemId, oreCount));
                }
            }
            
            return drops.ToArray();
        }
        
        private int GetOreItemId(PrefabDefinition def)
        {
            // Extract ore type from tags like "ore_iron", "ore_diamond"
            foreach (var tag in def.Tags)
            {
                if (tag.StartsWith("ore_"))
                {
                    string oreType = tag.Substring(4);
                    return oreType switch
                    {
                        "coal" => 402,      // Coal
                        "iron" => 400,      // Iron ingot
                        "copper" => 403,    // Copper ingot
                        "diamond" => 401,   // Diamond
                        _ => 0
                    };
                }
            }
            return 0;
        }
        
        private int GetOreCount(PrefabDefinition def)
        {
            if (HasTag(def, "common")) return 1;
            if (HasTag(def, "uncommon")) return 2;
            if (HasTag(def, "rare")) return 3;
            if (HasTag(def, "epic")) return 5;
            return 1;
        }
        
        private bool HasTag(PrefabDefinition def, string tag)
        {
            foreach (var t in def.Tags)
            {
                if (t == tag)
                    return true;
            }
            return false;
        }
        
        private bool AreSamePrefab(PlacedPrefab a, PlacedPrefab b)
        {
            return a.Position == b.Position && a.PrefabId == b.PrefabId;
        }
        
        public (bool isMining, float progress, string name) GetMiningInfo()
        {
            if (currentTarget == null)
                return (false, 0f, "");
            
            var def = PrefabRegistry.Get(currentTarget.PrefabId);
            string name = def?.Name ?? "Unknown";
            
            return (true, miningProgress / miningTime, name);
        }
    }
}
