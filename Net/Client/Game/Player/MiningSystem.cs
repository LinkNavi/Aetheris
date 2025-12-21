using System;
using OpenTK.Mathematics;
using OpenTK.Windowing.GraphicsLibraryFramework;

namespace Aetheris
{
    public class MiningSystem
    {
        private readonly Player player;
        private readonly RaycastHelper raycaster;
        private readonly Action<Vector3, BlockType> onBlockMined;
        private readonly Func<float>? toolSpeedProvider;  // Made nullable

        private Vector3? currentTarget = null;
        private float miningProgress = 0f;
        private BlockType targetBlockType = BlockType.Air;

        private const float MAX_REACH = 20.0f;
        private const float MINING_SPEED_MULT = 1f;
        private const float STICKY_ANGLE_DEGREES = 15f;
        private const float STICKY_POSITION_EPS = 0.6f;
        private const float RESET_DELAY = 0.35f;
        private const float PROGRESS_DECAY_RATE = 1.5f;
        private const float HIT_TICK_INTERVAL = 0.2f;
        private const float INSIDE_EPS = 0.01f;

        private static readonly float[] BlockHardness = new float[]
        {
            0f,    // Air
            2f,    // Stone
            0.8f,  // Dirt
            0.8f,  // Grass
            0.5f,  // Sand
            1.5f,  // Snow
            1.2f,  // Gravel
            1.5f,  // Wood
            0.3f   // Leaves
        };

        private float sinceStoppedMining = 0f;
        private float hitTickAccumulator = 0f;

        public event Action<Vector3> OnBlockHit = delegate { };
        public event Action<Vector3, BlockType> OnBlockBreak = delegate { };
        public event Action<Vector3, BlockType> OnMiningStarted = delegate { };
        public event Action OnMiningStopped = delegate { };

        public MiningSystem(Player player, Game game, Action<Vector3, BlockType> onBlockMined, Func<float>? toolSpeedProvider = null)
        {
            this.player = player ?? throw new ArgumentNullException(nameof(player));
            this.raycaster = new RaycastHelper(game);
            this.onBlockMined = onBlockMined;
            this.toolSpeedProvider = toolSpeedProvider;
        }

        public void Update(float deltaTime, MouseState mouse, bool isWindowFocused)
        {
            if (!isWindowFocused) return;

            Vector3 rayStart = player.GetEyePosition();
            Vector3 rayDir = player.GetForward().Normalized();
            Vector3 rayEnd = rayStart + rayDir * MAX_REACH;

            var hits = raycaster.Raycast(rayStart, rayEnd, raycastAll: false);

            bool lookingAtBlock = false;
            Vector3 lookedBlockPos = Vector3.Zero;
            BlockType lookedBlockType = BlockType.Air;
            Vector3 lookedBlockCenter = Vector3.Zero;
            float lookedDistance = 0f;

            if (hits.Length > 0 && hits[0].Hit)
            {
                var hit = hits[0];
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

            bool targetStillValid = false;
            if (currentTarget.HasValue)
            {
                Vector3 curCenter = currentTarget.Value + new Vector3(0.5f);
                float dist = (curCenter - rayStart).Length;
                if (dist <= MAX_REACH)
                {
                    if (lookingAtBlock)
                    {
                        float centerDist = (lookedBlockCenter - curCenter).Length;
                        if (centerDist <= STICKY_POSITION_EPS)
                        {
                            targetStillValid = true;
                        }
                        else
                        {
                            Vector3 toCenter = (curCenter - rayStart).Normalized();
                            float cosAngle = Vector3.Dot(toCenter, rayDir);
                            float angleDeg = MathF.Acos(Math.Clamp(cosAngle, -1f, 1f)) * (180f / MathF.PI);
                            if (angleDeg <= STICKY_ANGLE_DEGREES)
                                targetStillValid = true;
                        }
                    }
                    else
                    {
                        targetStillValid = true;
                    }
                }
            }

            if (isHolding && (lookingAtBlock || currentTarget.HasValue))
            {
                sinceStoppedMining = 0f;

                if (!currentTarget.HasValue && lookingAtBlock)
                {
                    BeginMining(lookedBlockPos, lookedBlockType);
                }
                else if (!targetStillValid && lookingAtBlock)
                {
                    BeginMining(lookedBlockPos, lookedBlockType);
                }

                if (currentTarget.HasValue)
                {
                    float hardness = GetBlockHardness(targetBlockType);

                    if (hardness <= 0f)
                    {
                        BreakCurrentBlock();
                        return;
                    }

                    float toolMul = toolSpeedProvider?.Invoke() ?? 1f;
                    float progressThisFrame = deltaTime * toolMul / (hardness * MINING_SPEED_MULT);
                    miningProgress = MathF.Min(1f, miningProgress + progressThisFrame);

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
                if (currentTarget.HasValue)
                {
                    sinceStoppedMining += deltaTime;
                    hitTickAccumulator = 0f;

                    miningProgress = MathF.Max(0f, miningProgress - PROGRESS_DECAY_RATE * deltaTime);

                    if (sinceStoppedMining >= RESET_DELAY || miningProgress <= 0f)
                    {
                        StopMining();
                    }
                }
                else
                {
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

        private float GetBlockHardness(BlockType blockType)
        {
            int index = (int)blockType;
            if (index < 0 || index >= BlockHardness.Length)
                return 1f;
            return BlockHardness[index];
        }

        public (bool isMining, BlockType blockType, float progress, Vector3? position) GetMiningInfo()
        {
            return (currentTarget.HasValue, targetBlockType, miningProgress, currentTarget);
        }

        public float GetSwingProgress()
        {
            return miningProgress;
        }

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
