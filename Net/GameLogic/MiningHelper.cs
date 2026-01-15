// Net/Shared/GameLogic/MiningHelper.cs - Progressive mining with tool support
using System;

namespace Aetheris.GameLogic
{
    /// <summary>
    /// Result of a mining operation tick
    /// </summary>
    public struct MiningTickResult
    {
        public bool IsComplete;
        public float Progress;          // 0-1
        public int DamageDealt;
        public BlockPos TargetBlock;
        public TerrainModifyResult? ModifyResult;
    }

    /// <summary>
    /// Handles progressive mining with tool effectiveness.
    /// 
    /// USAGE:
    /// ```
    /// var miner = new MiningHelper(world, modifier);
    /// 
    /// // Start mining
    /// miner.StartMining(targetBlock, ToolCategory.Pickaxe, tierLevel);
    /// 
    /// // Each frame while holding mouse
    /// var result = miner.UpdateMining(deltaTime, holdingMouse);
    /// 
    /// if (result.IsComplete) {
    ///     // Block was mined!
    ///     var drops = result.ModifyResult.Value.DroppedTypes;
    /// }
    /// ```
    /// </summary>
    public class MiningHelper
    {
        private readonly VoxelGrid grid;
        private readonly TerrainModifier modifier;

        // Current mining state
        private BlockPos? currentTarget;
        private float miningProgress;
        private float miningTime;
        private ToolCategory currentTool;
        private int currentToolTier;
        private float timeSinceStart;
        private float timeSinceStopped;

        // Mining config
        public float ProgressDecayRate { get; set; } = 2.0f;     // How fast progress decays when not mining
        public float ResetDelay { get; set; } = 0.5f;            // Time before mining resets entirely
        public float MinMiningTime { get; set; } = 0.1f;         // Minimum time to mine anything

        // Events
        public event Action<BlockPos>? OnMiningStarted;
        public event Action<BlockPos, float>? OnMiningProgress;
        public event Action<BlockPos, TerrainModifyResult>? OnBlockMined;
        public event Action? OnMiningStopped;

        public bool IsMining => currentTarget.HasValue;
        public float Progress => miningProgress;
        public BlockPos? Target => currentTarget;

        public MiningHelper(VoxelGrid grid, TerrainModifier modifier)
        {
            this.grid = grid;
            this.modifier = modifier;
        }

        /// <summary>
        /// Start mining a new block
        /// </summary>
        public void StartMining(BlockPos target, ToolCategory tool = ToolCategory.None, int toolTier = 0)
        {
            var block = grid.GetBlock(target);
            if (block.IsAir) return;

            var def = BlockRegistry.GetOrDefault(block.Type);
            if (def.Hardness <= 0) return; // Can't mine this

            currentTarget = target;
            currentTool = tool;
            currentToolTier = toolTier;
            miningProgress = 0f;
            timeSinceStart = 0f;
            timeSinceStopped = 0f;
            miningTime = Math.Max(MinMiningTime, def.GetMiningTime(tool, toolTier));

            OnMiningStarted?.Invoke(target);
        }

        /// <summary>
        /// Stop mining current block
        /// </summary>
        public void StopMining()
        {
            if (currentTarget.HasValue)
            {
                OnMiningStopped?.Invoke();
            }
            currentTarget = null;
            miningProgress = 0f;
        }

        /// <summary>
        /// Update mining state - call every frame
        /// </summary>
        public MiningTickResult UpdateMining(float deltaTime, bool isHoldingMine, BlockPos? lookingAt = null)
        {
            var result = new MiningTickResult
            {
                IsComplete = false,
                Progress = 0f,
                DamageDealt = 0
            };

            // If not holding mine button, decay progress
            if (!isHoldingMine)
            {
                if (currentTarget.HasValue)
                {
                    timeSinceStopped += deltaTime;
                    miningProgress = Math.Max(0, miningProgress - ProgressDecayRate * deltaTime);

                    if (timeSinceStopped >= ResetDelay || miningProgress <= 0)
                    {
                        StopMining();
                    }
                }
                return result;
            }

            // Reset stopped timer
            timeSinceStopped = 0f;

            // Check if we should switch targets
            if (lookingAt.HasValue)
            {
                if (!currentTarget.HasValue || currentTarget.Value != lookingAt.Value)
                {
                    // New target - switch if we have significant progress, keep mining same block
                    if (!currentTarget.HasValue || miningProgress < 0.3f)
                    {
                        StartMining(lookingAt.Value, currentTool, currentToolTier);
                    }
                }
            }

            // No target
            if (!currentTarget.HasValue)
                return result;

            // Update mining progress
            timeSinceStart += deltaTime;
            float progressThisFrame = deltaTime / miningTime;
            miningProgress = Math.Min(1f, miningProgress + progressThisFrame);

            result.TargetBlock = currentTarget.Value;
            result.Progress = miningProgress;

            OnMiningProgress?.Invoke(currentTarget.Value, miningProgress);

            // Check if mining complete
            if (miningProgress >= 1f)
            {
                result.IsComplete = true;
                
                // Perform the actual terrain modification
                var modResult = modifier.MineBlock(currentTarget.Value);
                result.ModifyResult = modResult;
                
                if (modResult.Success)
                {
                    OnBlockMined?.Invoke(currentTarget.Value, modResult);
                }

                StopMining();
            }

            return result;
        }

        /// <summary>
        /// Change the tool being used (affects mining speed)
        /// </summary>
        public void SetTool(ToolCategory tool, int tier)
        {
            if (currentTool == tool && currentToolTier == tier)
                return;

            currentTool = tool;
            currentToolTier = tier;

            // Recalculate mining time if currently mining
            if (currentTarget.HasValue)
            {
                var block = grid.GetBlock(currentTarget.Value);
                if (!block.IsAir)
                {
                    var def = BlockRegistry.GetOrDefault(block.Type);
                    miningTime = Math.Max(MinMiningTime, def.GetMiningTime(tool, tier));
                }
            }
        }

        /// <summary>
        /// Get info about what would be mined at a position
        /// </summary>
        public MiningPreview GetMiningPreview(BlockPos pos, ToolCategory tool = ToolCategory.None, int toolTier = 0)
        {
            var block = grid.GetBlock(pos);
            
            if (block.IsAir)
            {
                return new MiningPreview { CanMine = false };
            }

            var def = BlockRegistry.GetOrDefault(block.Type);
            
            return new MiningPreview
            {
                CanMine = def.Hardness > 0,
                BlockType = block.Type,
                BlockName = def.Name,
                MiningTime = def.GetMiningTime(tool, toolTier),
                IsCorrectTool = def.PreferredTool == tool,
                RequiredTier = def.MinToolTier,
                HasRequiredTier = toolTier >= def.MinToolTier,
                DropItemId = def.DropItemId,
                DropCount = def.DropCount
            };
        }
    }

    public struct MiningPreview
    {
        public bool CanMine;
        public BlockType BlockType;
        public string BlockName;
        public float MiningTime;
        public bool IsCorrectTool;
        public int RequiredTier;
        public bool HasRequiredTier;
        public int DropItemId;
        public int DropCount;
    }
}
