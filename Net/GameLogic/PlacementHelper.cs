// Net/Shared/GameLogic/PlacementHelper.cs - Grid-snapped block and prefab placement
using System;

namespace Aetheris.GameLogic
{
    /// <summary>
    /// Placement mode determines how blocks are placed
    /// </summary>
    public enum PlacementMode
    {
        /// <summary>Place on surface of hit block</summary>
        Surface,
        
        /// <summary>Replace the hit block</summary>
        Replace,
        
        /// <summary>Place in air at cursor position</summary>
        Free
    }

    /// <summary>
    /// Handles block and prefab placement with grid snapping.
    /// 
    /// USAGE:
    /// ```
    /// var placer = new PlacementHelper(grid, modifier, prefabManager);
    /// 
    /// // Get placement preview (for ghost block rendering)
    /// var preview = placer.GetPlacementPreview(rayHit, itemId);
    /// 
    /// if (preview.CanPlace) {
    ///     // Render ghost at preview.Position
    /// }
    /// 
    /// // Actually place when clicked
    /// if (mouseClicked && preview.CanPlace) {
    ///     placer.Place(preview);
    /// }
    /// ```
    /// </summary>
    public class PlacementHelper
    {
        private readonly VoxelGrid grid;
        private readonly TerrainModifier modifier;
        private readonly PrefabManager prefabManager;

        public PlacementMode Mode { get; set; } = PlacementMode.Surface;
        public float MaxPlaceDistance { get; set; } = 8f;
        public float PlacementCooldown { get; set; } = 0.2f;

        private float cooldownRemaining = 0f;

        // Events
        public event Action<PlacementPreview>? OnBlockPlaced;
        public event Action<PlacementPreview>? OnPrefabPlaced;

        public PlacementHelper(VoxelGrid grid, TerrainModifier modifier, PrefabManager prefabManager)
        {
            this.grid = grid;
            this.modifier = modifier;
            this.prefabManager = prefabManager;
        }

        /// <summary>
        /// Update cooldown timer
        /// </summary>
        public void Update(float deltaTime)
        {
            cooldownRemaining = Math.Max(0, cooldownRemaining - deltaTime);
        }

        /// <summary>
        /// Get placement preview for an item at a raycast hit position
        /// </summary>
        public PlacementPreview GetPlacementPreview(RaycastResult hit, int itemId, byte rotation = 0)
        {
            var preview = new PlacementPreview
            {
                ItemId = itemId,
                Rotation = rotation,
                CanPlace = false
            };

            if (!hit.Hit || hit.Distance > MaxPlaceDistance)
                return preview;

            // Check if item places a block or prefab
            var prefab = PrefabRegistry.GetByItemId(itemId);
            if (prefab != null)
            {
                return GetPrefabPlacementPreview(hit, prefab, rotation);
            }

            // Check if item places a basic block
            var blockType = GetBlockTypeForItem(itemId);
            if (blockType == BlockType.Air)
                return preview;

            return GetBlockPlacementPreview(hit, blockType, rotation);
        }

        /// <summary>
        /// Get placement preview for a basic block
        /// </summary>
        private PlacementPreview GetBlockPlacementPreview(RaycastResult hit, BlockType blockType, byte rotation)
        {
            // Calculate placement position based on hit normal
            BlockPos placePos;
            
            if (Mode == PlacementMode.Replace)
            {
                placePos = hit.BlockPosition;
            }
            else // Surface mode
            {
                // Place adjacent to hit block in direction of normal
                var hitBlock = hit.BlockPosition;
                int nx = (int)Math.Round(hit.Normal.x);
                int ny = (int)Math.Round(hit.Normal.y);
                int nz = (int)Math.Round(hit.Normal.z);
                placePos = hitBlock.Offset(nx, ny, nz);
            }

            // Check if can place
            bool canPlace = modifier.CanPlace(placePos) && cooldownRemaining <= 0;

            // Get world position for preview rendering
            var worldCenter = placePos.ToWorldCenter();

            return new PlacementPreview
            {
                ItemId = 0,
                BlockType = blockType,
                Position = placePos,
                WorldPosition = worldCenter,
                Rotation = rotation,
                CanPlace = canPlace,
                IsPrefab = false,
                BlockSize = (1, 1, 1),
                PreviewColor = canPlace ? (0f, 1f, 0f, 0.5f) : (1f, 0f, 0f, 0.5f)
            };
        }

        /// <summary>
        /// Get placement preview for a prefab
        /// </summary>
        private PlacementPreview GetPrefabPlacementPreview(RaycastResult hit, PrefabDefinition prefab, byte rotation)
        {
            // Calculate placement position
            BlockPos placePos;
            
            var hitBlock = hit.BlockPosition;
            int nx = (int)Math.Round(hit.Normal.x);
            int ny = (int)Math.Round(hit.Normal.y);
            int nz = (int)Math.Round(hit.Normal.z);
            placePos = hitBlock.Offset(nx, ny, nz);

            // Check if can place
            bool canPlace = prefabManager.CanPlace(prefab.PrefabId, placePos, rotation) && cooldownRemaining <= 0;

            // Get world position for preview rendering
            var origin = placePos.ToWorldOrigin();
            var worldCenter = (
                origin.x + prefab.BlockSize.x * GridConfig.BLOCK_SIZE * 0.5f,
                origin.y + prefab.BlockSize.y * GridConfig.BLOCK_SIZE * 0.5f,
                origin.z + prefab.BlockSize.z * GridConfig.BLOCK_SIZE * 0.5f
            );

            return new PlacementPreview
            {
                ItemId = prefab.PlacementItemId,
                BlockType = prefab.PlacedBlockType,
                Position = placePos,
                WorldPosition = worldCenter,
                Rotation = rotation,
                CanPlace = canPlace,
                IsPrefab = true,
                PrefabId = prefab.PrefabId,
                PrefabModelPath = prefab.ModelPath,
                BlockSize = prefab.BlockSize,
                PreviewColor = canPlace ? (0f, 1f, 0f, 0.5f) : (1f, 0f, 0f, 0.5f)
            };
        }

        /// <summary>
        /// Execute placement from a preview
        /// </summary>
        public PlacementResult Place(PlacementPreview preview, string placedBy = "")
        {
            if (!preview.CanPlace || cooldownRemaining > 0)
            {
                return new PlacementResult { Success = false, Error = "Cannot place" };
            }

            if (preview.IsPrefab)
            {
                var result = prefabManager.Place(preview.PrefabId, preview.Position, preview.Rotation, placedBy);
                
                if (result.Success)
                {
                    cooldownRemaining = PlacementCooldown;
                    OnPrefabPlaced?.Invoke(preview);
                }

                return new PlacementResult
                {
                    Success = result.Success,
                    Error = result.Error,
                    Position = preview.Position,
                    BlocksModified = result.OccupiedBlocks?.Length ?? 0
                };
            }
            else
            {
                var result = modifier.PlaceBlock(preview.Position, preview.BlockType, preview.Rotation);
                
                if (result.Success)
                {
                    cooldownRemaining = PlacementCooldown;
                    OnBlockPlaced?.Invoke(preview);
                }

                return new PlacementResult
                {
                    Success = result.Success,
                    Position = preview.Position,
                    BlocksModified = result.BlocksAdded
                };
            }
        }

        /// <summary>
        /// Get the block type an item places
        /// </summary>
        private BlockType GetBlockTypeForItem(int itemId)
        {
            // Item IDs 1-8 correspond to block types
            return itemId switch
            {
                1 => BlockType.Stone,
                2 => BlockType.Dirt,
                3 => BlockType.Grass,
                4 => BlockType.Sand,
                5 => BlockType.Snow,
                6 => BlockType.Gravel,
                7 => BlockType.Wood,
                8 => BlockType.Leaves,
                _ => BlockType.Air
            };
        }

        /// <summary>
        /// Rotate placement (for prefabs that support rotation)
        /// </summary>
        public byte GetNextRotation(byte current, int prefabId)
        {
            var prefab = PrefabRegistry.Get(prefabId);
            if (prefab == null || !prefab.CanRotate)
                return 0;

            return (byte)((current + 1) % prefab.RotationStates);
        }
    }

    /// <summary>
    /// Preview of where a block/prefab will be placed
    /// </summary>
    public struct PlacementPreview
    {
        public int ItemId;
        public BlockType BlockType;
        public BlockPos Position;
        public (float x, float y, float z) WorldPosition;
        public byte Rotation;
        public bool CanPlace;
        public bool IsPrefab;
        public int PrefabId;
        public string? PrefabModelPath;
        public (int x, int y, int z) BlockSize;
        public (float r, float g, float b, float a) PreviewColor;
    }

    /// <summary>
    /// Result of a placement operation
    /// </summary>
    public struct PlacementResult
    {
        public bool Success;
        public string? Error;
        public BlockPos Position;
        public int BlocksModified;
    }

    /// <summary>
    /// Simple raycast result for placement calculations
    /// </summary>
    public struct RaycastResult
    {
        public bool Hit;
        public float Distance;
        public BlockPos BlockPosition;
        public (float x, float y, float z) HitPoint;
        public (float x, float y, float z) Normal;
        public BlockType HitBlockType;

        public static RaycastResult Miss => new RaycastResult { Hit = false };
    }
}
