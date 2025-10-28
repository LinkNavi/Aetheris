using System;
using OpenTK.Mathematics;
using OpenTK.Windowing.GraphicsLibraryFramework;

namespace Aetheris
{
    /// <summary>
    /// Mining system with raycasting and block hardness
    /// - Smooth progress retention/decay
    /// - Sticky targeting
    /// - Hit tick events for audio/particles
    /// - Tool speed provider hook
    /// </summary>
    public class MiningSystem
    {
        private readonly Player player;
        private readonly RaycastHelper raycaster;
        private readonly Action<Vector3, BlockType> onBlockMined;
        private readonly Func<float> toolSpeedProvider; // returns tool speed multiplier

        // Mining state
        private Vector3? currentTarget = null;          // integer block position
        private float miningProgress = 0f;              // 0..1
        private BlockType targetBlockType = BlockType.Air;

        // Mining config
        private const float MAX_REACH = 10.0f;          // blocks
        private const float MINING_SPEED_MULT = 1f;     // global multiplier
        private const float STICKY_ANGLE_DEGREES = 15f; // how far you can look away and still continue
        private const float STICKY_POSITION_EPS = 0.6f; // how far center can differ and still be same block
        private const float RESET_DELAY = 0.35f;        // seconds before instant reset
        private const float PROGRESS_DECAY_RATE = 1.5f; // how quickly progress decays (per second) when not mining
        private const float HIT_TICK_INTERVAL = 0.2f;   // seconds between OnHit events while mining
        private const float INSIDE_EPS = 0.01f;         // how far along -rayDir to step into block for stable floor()

        // Block hardness (seconds to mine)
        private static readonly float[] BlockHardness = new float[]
        {
            0f,    // Air (instant)
            2f,    // Stone
            0.8f,  // Dirt
            0.8f,  // Grass
            0.5f,  // Sand
            1.5f,  // Snow
            1.2f,  // Gravel
            1.5f,  // Wood
            0.3f   // Leaves
        };

        // Internal timers
        private float sinceStoppedMining = 0f;   // accumulate when not holding left click
        private float hitTickAccumulator = 0f;   // fire OnBlockHit every HIT_TICK_INTERVAL

        // Events for hooking audio/particles/ui
        public event Action<Vector3> OnBlockHit; // called repeatedly while mining (use for tick sound/particles)
        public event Action<Vector3, BlockType> OnBlockBreak; // called when block breaks
        public event Action<Vector3, BlockType> OnMiningStarted;
        public event Action OnMiningStopped;

        public MiningSystem(Player player, Game game, Action<Vector3, BlockType> onBlockMined, Func<float> toolSpeedProvider = null)
        {
            this.player = player ?? throw new ArgumentNullException(nameof(player));
            this.raycaster = new RaycastHelper(game);
            this.onBlockMined = onBlockMined;
            this.toolSpeedProvider = toolSpeedProvider ?? (() => 1f);
        }

        /// <summary>
        /// Call every frame with deltaTime and mouse state
        /// </summary>
        public void Update(float deltaTime, MouseState mouse, bool isWindowFocused)
        {
            if (!isWindowFocused) return;

            // Raycast for what we're looking at (use player's real eye position)
            Vector3 rayStart = player.GetEyePosition();
            Vector3 rayDir = player.GetForward().Normalized();
            Vector3 rayEnd = rayStart + rayDir * MAX_REACH;

            var hits = raycaster.Raycast(rayStart, rayEnd, raycastAll: false);

            // default looked values
            bool lookingAtBlock = false;
            Vector3 lookedBlockPos = Vector3.Zero;
            BlockType lookedBlockType = BlockType.Air;
            Vector3 lookedBlockCenter = Vector3.Zero;
            float lookedDistance = 0f;

            if (hits.Length > 0 && hits[0].Hit)
            {
                var hit = hits[0];
                // step slightly BACK along the ray to ensure we're inside the block (stable regardless of triangle winding)
                Vector3 insidePoint = hit.Point - rayDir * INSIDE_EPS;

                lookedBlockPos = new Vector3(
                    MathF.Floor(insidePoint.X),
                    MathF.Floor(insidePoint.Y),
                    MathF.Floor(insidePoint.Z)
                );

                lookedBlockCenter = lookedBlockPos + new Vector3(0.5f);
                lookedBlockType = hit.BlockType;
                lookedDistance = hit.Distance;
                lookingAtBlock = true;
            }

            bool isHolding = mouse.IsButtonDown(MouseButton.Left);

            // If we're already targeting a block, decide if we should stay "sticky" to it
            bool targetStillValid = false;
            if (currentTarget.HasValue)
            {
                Vector3 curCenter = currentTarget.Value + new Vector3(0.5f);
                // Check distance from player (don't allow mining beyond reach)
                float dist = (curCenter - rayStart).Length;
                if (dist <= MAX_REACH)
                {
                    // If we're still looking at roughly the same block center, or looking within sticky angle, allow continuation
                    if (lookingAtBlock)
                    {
                        float centerDist = (lookedBlockCenter - curCenter).Length;
                        if (centerDist <= STICKY_POSITION_EPS)
                        {
                            targetStillValid = true;
                        }
                        else
                        {
                            // angle check: if our view direction is still aiming at the block center within STICKY_ANGLE
                            Vector3 toCenter = (curCenter - rayStart).Normalized();
                            float cosAngle = Vector3.Dot(toCenter, rayDir);
                            float angleDeg = MathF.Acos(Math.Clamp(cosAngle, -1f, 1f)) * (180f / MathF.PI);
                            if (angleDeg <= STICKY_ANGLE_DEGREES)
                                targetStillValid = true;
                        }
                    }
                    else
                    {
                        // not looking at anything but we can still allow a short grace if player was mining
                        targetStillValid = true;
                    }
                }
            }

            // MAIN: Start/continue/stop mining (handles both looking and not-looking cases)
            if (isHolding && (lookingAtBlock || currentTarget.HasValue))
            {
                // reset stop timer while holding
                sinceStoppedMining = 0f;

                // If we have no target yet, start on looked block
                if (!currentTarget.HasValue && lookingAtBlock)
                {
                    BeginMining(lookedBlockPos, lookedBlockType);
                }
                else if (!targetStillValid && lookingAtBlock)
                {
                    // switched to a different far-away block; restart mining
                    BeginMining(lookedBlockPos, lookedBlockType);
                }

                // If still have a target, progress it
                if (currentTarget.HasValue)
                {
                    float hardness = GetBlockHardness(targetBlockType);

                    // instant break if hardness <= 0
                    if (hardness <= 0f)
                    {
                        BreakCurrentBlock();
                        return;
                    }

                    float toolMul = toolSpeedProvider?.Invoke() ?? 1f;
                    // progress rate = (deltaTime * toolMul) / (hardness * global)
                    float progressThisFrame = deltaTime * toolMul / (hardness * MINING_SPEED_MULT);
                    miningProgress = MathF.Min(1f, miningProgress + progressThisFrame);

                    // Hit tick (sound/particles) every HIT_TICK_INTERVAL seconds of mining
                    hitTickAccumulator += deltaTime;
                    if (hitTickAccumulator >= HIT_TICK_INTERVAL)
                    {
                        hitTickAccumulator = 0f;
                        OnBlockHit?.Invoke(currentTarget.Value);
                    }

                    if (miningProgress >= 1f)
                    {
                        BreakCurrentBlock();
                    }
                }
            }
            else
            {
                // Not holding left click -> decay progress before full reset
                if (currentTarget.HasValue)
                {
                    sinceStoppedMining += deltaTime;
                    hitTickAccumulator = 0f; // stop hit ticks while not mining

                    // decay progress
                    miningProgress = MathF.Max(0f, miningProgress - PROGRESS_DECAY_RATE * deltaTime);

                    if (sinceStoppedMining >= RESET_DELAY || miningProgress <= 0f)
                    {
                        // fully reset after delay or when progress gone
                        StopMining();
                    }
                }
                else
                {
                    // nothing targeted: keep clear state
                    ResetMiningState();
                }
            }
        }

        private void BeginMining(Vector3 blockPos, BlockType blockType)
        {
            currentTarget = blockPos;
            targetBlockType = blockType;
            miningProgress = 0f;
            sinceStoppedMining = 0f;
            hitTickAccumulator = 0f;
            OnMiningStarted?.Invoke(blockPos, blockType);
        }

        private void BreakCurrentBlock()
        {
            if (!currentTarget.HasValue) return;
            Vector3 pos = currentTarget.Value;
            OnBlockBreak?.Invoke(pos, targetBlockType);
            onBlockMined?.Invoke(pos, targetBlockType);
            ResetMiningState();
        }

        private void StopMining()
        {
            // Called when player stops mining but we keep state for a bit; now we actually stop
            ResetMiningState();
            OnMiningStopped?.Invoke();
        }

        private void ResetMiningState()
        {
            currentTarget = null;
            miningProgress = 0f;
            targetBlockType = BlockType.Air;
            sinceStoppedMining = 0f;
            hitTickAccumulator = 0f;
        }

        /// <summary>
        /// Get how long it takes to mine a block (in seconds)
        /// </summary>
        private float GetBlockHardness(BlockType blockType)
        {
            int index = (int)blockType;
            if (index < 0 || index >= BlockHardness.Length)
                return 1f;
            return BlockHardness[index];
        }

        /// <summary>
        /// External API: query current mining info for UI display
        /// progress is in 0..1
        /// </summary>
        public (bool isMining, BlockType blockType, float progress, Vector3? position) GetMiningInfo()
        {
            return (currentTarget.HasValue, targetBlockType, miningProgress, currentTarget);
        }

        /// <summary>
        /// Get what the player is currently looking at (for crosshair/UI)
        /// </summary>
        public (bool lookingAt, BlockType blockType, Vector3 position, float distance) GetLookTarget()
        {
            Vector3 rayStart = player.GetEyePosition();
            Vector3 rayDir = player.GetForward().Normalized();
            Vector3 rayEnd = rayStart + rayDir * MAX_REACH;

            var hits = raycaster.Raycast(rayStart, rayEnd, raycastAll: false);

            if (hits.Length > 0 && hits[0].Hit)
            {
                var hit = hits[0];
                Vector3 insidePoint = hit.Point - rayDir * INSIDE_EPS;
                Vector3 blockPos = new Vector3(
                    MathF.Floor(insidePoint.X),
                    MathF.Floor(insidePoint.Y),
                    MathF.Floor(insidePoint.Z)
                );

                return (true, hit.BlockType, blockPos, hit.Distance);
            }

            return (false, BlockType.Air, Vector3.Zero, 0f);
        }
    }
}
