// Net/Client/Game/Player/BlockBreakingSystem.cs - Fixed type conversions
using System;
using OpenTK.Mathematics;

namespace Aetheris
{
    public class BlockBreakingSystem
    {
        private readonly PlacedBlockManager blockManager;
        private readonly Inventory inventory;
        
        private Vector3i? targetBlock;
        private float breakProgress;
        private float breakTime;
        private float lastBreakTime;
        private const float BREAK_COOLDOWN = 0.05f;
        
        public Vector3i? TargetBlock => targetBlock;
        public float BreakProgress => targetBlock.HasValue ? breakProgress / breakTime : 0f;
        public bool IsBreaking => targetBlock.HasValue && breakProgress > 0;
        
        public BlockBreakingSystem(PlacedBlockManager blockManager, Inventory inventory)
        {
            this.blockManager = blockManager;
            this.inventory = inventory;
        }
        
        public void StartBreaking(Vector3i blockPos, int toolId = 0)
        {
            var block = blockManager.GetBlockAt(blockPos.X, blockPos.Y, blockPos.Z);
            if (block == null) return;
            
            targetBlock = blockPos;
            breakProgress = 0f;
            // FIXED: Convert from rendering type to network type for GetBreakTime
            Aetheris.BlockType networkBlockType = (Aetheris.BlockType)((int)block.BlockType);
            breakTime = GetBreakTime(networkBlockType, toolId);
        }
        
        public void StopBreaking()
        {
            targetBlock = null;
            breakProgress = 0f;
        }
        
        public bool Update(float deltaTime, bool holdingBreak, int toolId = 0)
        {
            if (!holdingBreak || !targetBlock.HasValue)
            {
                StopBreaking();
                return false;
            }
            
            lastBreakTime += deltaTime;
            if (lastBreakTime < BREAK_COOLDOWN) return false;
            lastBreakTime = 0f;
            
            breakProgress += deltaTime;
            
            if (breakProgress >= breakTime)
            {
                BreakBlock(targetBlock.Value, toolId);
                StopBreaking();
                return true;
            }
            
            return false;
        }
        
        private void BreakBlock(Vector3i pos, int toolId)
        {
            var block = blockManager.GetBlockAt(pos.X, pos.Y, pos.Z);
            if (block == null) return;
            
            // FIXED: Convert from rendering type to network type
            Aetheris.BlockType networkBlockType = (Aetheris.BlockType)((int)block.BlockType);
            
            int dropId = GetDropForBlock(networkBlockType, toolId);
            int dropCount = GetDropCount(networkBlockType, toolId);
            
            if (blockManager.RemoveBlock(pos.X, pos.Y, pos.Z))
            {
                if (dropId > 0 && dropCount > 0)
                {
                    inventory.AddItem(dropId, dropCount);
                    Console.WriteLine($"[Breaking] Dropped {dropCount}x item {dropId}");
                }
            }
        }
        
        private float GetBreakTime(Aetheris.BlockType type, int toolId)
        {
            float baseTime = type switch
            {
                Aetheris.BlockType.Dirt or Aetheris.BlockType.Sand or Aetheris.BlockType.Gravel => 0.5f,
                Aetheris.BlockType.Grass => 0.6f,
                Aetheris.BlockType.Wood => 1.5f,
                Aetheris.BlockType.Leaves => 0.2f,
                Aetheris.BlockType.Stone => 2.0f,
                Aetheris.BlockType.Snow => 0.3f,
                _ => 1.0f
            };
            
            float toolMultiplier = GetToolMultiplier(type, toolId);
            return baseTime / toolMultiplier;
        }
        
        private float GetToolMultiplier(Aetheris.BlockType type, int toolId)
        {
            bool isPickaxeBlock = type == Aetheris.BlockType.Stone || type == Aetheris.BlockType.Gravel;
            bool isShovelBlock = type == Aetheris.BlockType.Dirt || type == Aetheris.BlockType.Sand || 
                                 type == Aetheris.BlockType.Grass || type == Aetheris.BlockType.Snow;
            bool isAxeBlock = type == Aetheris.BlockType.Wood;
            
            return toolId switch
            {
                50 => isPickaxeBlock ? 2f : 1f,  // Wooden Pickaxe
                51 => isPickaxeBlock ? 4f : 1f,  // Stone Pickaxe
                52 => isPickaxeBlock ? 6f : 1f,  // Iron Pickaxe
                53 => isPickaxeBlock ? 8f : 1f,  // Diamond Pickaxe
                54 => isAxeBlock ? 2f : 1f,      // Wooden Axe
                55 => isShovelBlock ? 2f : 1f,   // Wooden Shovel
                _ => 1f
            };
        }
        
        private int GetDropForBlock(Aetheris.BlockType type, int toolId)
        {
            return type switch
            {
                Aetheris.BlockType.Stone => 1,   // Stone item
                Aetheris.BlockType.Dirt => 2,    // Dirt item
                Aetheris.BlockType.Grass => 2,   // Drops dirt
                Aetheris.BlockType.Sand => 4,    // Sand item
                Aetheris.BlockType.Snow => 5,    // Snow item
                Aetheris.BlockType.Gravel => 6,  // Gravel item
                Aetheris.BlockType.Wood => 7,    // Wood item
                Aetheris.BlockType.Leaves => 0,  // No drop
                _ => 0
            };
        }
        
        private int GetDropCount(Aetheris.BlockType type, int toolId)
        {
            return type == Aetheris.BlockType.Leaves ? 0 : 1;
        }
        
        public (Vector3i pos, float distance)? RaycastPlacedBlocks(Vector3 origin, Vector3 direction, float maxDist)
        {
            Vector3i? closest = null;
            float closestDist = maxDist;
            
            foreach (var block in blockManager.GetBlocksInRange(origin, maxDist))
            {
                var min = new Vector3(block.Position.X, block.Position.Y, block.Position.Z);
                var max = min + Vector3.One;
                
                if (RayAABBIntersect(origin, direction, min, max, out float t) && t < closestDist && t > 0.1f)
                {
                    closestDist = t;
                    closest = block.Position;
                }
            }
            
            return closest.HasValue ? (closest.Value, closestDist) : null;
        }
        
        private bool RayAABBIntersect(Vector3 origin, Vector3 dir, Vector3 min, Vector3 max, out float t)
        {
            t = 0;
            float tmax = float.MaxValue;
            
            for (int i = 0; i < 3; i++)
            {
                float o = i == 0 ? origin.X : (i == 1 ? origin.Y : origin.Z);
                float d = i == 0 ? dir.X : (i == 1 ? dir.Y : dir.Z);
                float bmin = i == 0 ? min.X : (i == 1 ? min.Y : min.Z);
                float bmax = i == 0 ? max.X : (i == 1 ? max.Y : max.Z);
                
                if (Math.Abs(d) < 1e-6f)
                {
                    if (o < bmin || o > bmax) return false;
                }
                else
                {
                    float t1 = (bmin - o) / d;
                    float t2 = (bmax - o) / d;
                    if (t1 > t2) (t1, t2) = (t2, t1);
                    t = Math.Max(t, t1);
                    tmax = Math.Min(tmax, t2);
                    if (t > tmax) return false;
                }
            }
            return true;
        }
    }
}
