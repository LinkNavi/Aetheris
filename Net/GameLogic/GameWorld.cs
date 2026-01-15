// Net/Shared/GameLogic/GameWorld.cs - Main world container (like Minecraft's World class)
using System;
using System.Collections.Generic;

namespace Aetheris.GameLogic
{
    /// <summary>
    /// The main game world container - like Minecraft's World class.
    /// Both server and client create their own GameWorld instance.
    /// 
    /// USAGE (like a Minecraft mod):
    /// ```
    /// var world = new GameWorld();
    /// 
    /// // Access subsystems
    /// var block = world.GetBlock(pos);
    /// world.MineBlock(pos);
    /// world.PlaceBlock(pos, BlockType.Stone);
    /// 
    /// // Events
    /// world.OnBlockChanged += (pos, oldBlock, newBlock) => {
    ///     // Handle block change
    /// };
    /// ```
    /// </summary>
    public class GameWorld
    {
        // Core systems
        public VoxelGrid Grid { get; }
        public TerrainModifier Modifier { get; }
        public PrefabManager Prefabs { get; }
        public MiningHelper Mining { get; }
        public PlacementHelper Placement { get; }

        // World properties
        public int Seed { get; }
        public string Name { get; }
        public DateTime CreatedTime { get; }

        // Events
        public event Action<BlockPos, BlockData, BlockData>? OnBlockChanged;
        public event Action<TerrainModifyResult>? OnTerrainModified;
        public event Action<PlacedPrefab>? OnPrefabPlaced;
        public event Action<BlockPos>? OnPrefabRemoved;

        // Player tracking (for multiplayer)
        private readonly Dictionary<string, PlayerWorldState> players = new();

        public GameWorld(int seed = 0, string name = "World")
        {
            Seed = seed;
            Name = name;
            CreatedTime = DateTime.UtcNow;

            // Initialize subsystems
            Grid = new VoxelGrid(GenerateProceduralBlock);
            Modifier = new TerrainModifier(Grid, OnModified);
            Prefabs = new PrefabManager(Grid, Modifier);
            Mining = new MiningHelper(Grid, Modifier);
            Placement = new PlacementHelper(Grid, Modifier, Prefabs);

            // Wire up events
            Mining.OnBlockMined += (pos, result) => OnTerrainModified?.Invoke(result);
            Placement.OnBlockPlaced += (preview) => 
            {
                var block = Grid.GetBlock(preview.Position);
                OnBlockChanged?.Invoke(preview.Position, BlockData.Air, block);
            };
        }

        private void OnModified(TerrainModifyResult result)
        {
            OnTerrainModified?.Invoke(result);
        }

        /// <summary>
        /// Procedural block generator - called when no modification exists
        /// This is where WorldGen integrates with the new system.
        /// </summary>
        private BlockData GenerateProceduralBlock(BlockPos pos)
        {
            // Convert block position to world position
            var (wx, wy, wz) = pos.ToWorldOrigin();
            
            // Use existing WorldGen for density
            float density = WorldGen.SampleDensity(wx, wy, wz);
            
            if (density <= 0.5f)
                return BlockData.Air;

            // Get block type from WorldGen
            BlockType type = WorldGen.GetBlockType(wx, wy, wz, density);
            return BlockData.Solid(type);
        }

        #region Block Access (Minecraft-style API)

        /// <summary>Get block at block position</summary>
        public BlockData GetBlock(BlockPos pos) => Grid.GetBlock(pos);

        /// <summary>Get block at world coordinates</summary>
        public BlockData GetBlockAtWorld(int x, int y, int z) => Grid.GetBlockAtWorld(x, y, z);

        /// <summary>Get block at world coordinates (float)</summary>
        public BlockData GetBlockAtWorld(float x, float y, float z) => 
            Grid.GetBlock(BlockPos.FromWorld(x, y, z));

        /// <summary>Check if position is solid</summary>
        public bool IsSolid(BlockPos pos) => Grid.IsSolid(pos);

        /// <summary>Check if position is air</summary>
        public bool IsAir(BlockPos pos) => Grid.IsAir(pos);

        /// <summary>Set block directly (bypasses modifier events)</summary>
        public void SetBlock(BlockPos pos, BlockData block)
        {
            var old = Grid.GetBlock(pos);
            Grid.SetBlock(pos, block);
            OnBlockChanged?.Invoke(pos, old, block);
        }

        #endregion

        #region Mining API

        /// <summary>Mine a single block (instant)</summary>
        public TerrainModifyResult MineBlock(BlockPos pos) => Modifier.MineBlock(pos);

        /// <summary>Mine a region of blocks</summary>
        public TerrainModifyResult MineRegion(BlockPos center, int radius = 0) => 
            Modifier.MineRegion(center, radius);

        /// <summary>Start progressive mining</summary>
        public void StartMining(BlockPos pos, ToolCategory tool = ToolCategory.None, int tier = 0) =>
            Mining.StartMining(pos, tool, tier);

        /// <summary>Update mining progress</summary>
        public MiningTickResult UpdateMining(float deltaTime, bool holding, BlockPos? target = null) =>
            Mining.UpdateMining(deltaTime, holding, target);

        /// <summary>Stop mining</summary>
        public void StopMining() => Mining.StopMining();

        /// <summary>Get mining preview</summary>
        public MiningPreview GetMiningPreview(BlockPos pos, ToolCategory tool = ToolCategory.None, int tier = 0) =>
            Mining.GetMiningPreview(pos, tool, tier);

        #endregion

        #region Placement API

        /// <summary>Place a block</summary>
        public TerrainModifyResult PlaceBlock(BlockPos pos, BlockType type, byte rotation = 0) =>
            Modifier.PlaceBlock(pos, type, rotation);

        /// <summary>Place a prefab</summary>
        public PlacePrefabResult PlacePrefab(int prefabId, BlockPos pos, byte rotation = 0, string by = "") =>
            Prefabs.Place(prefabId, pos, rotation, by);

        /// <summary>Get placement preview</summary>
        public PlacementPreview GetPlacementPreview(RaycastResult hit, int itemId, byte rotation = 0) =>
            Placement.GetPlacementPreview(hit, itemId, rotation);

        /// <summary>Execute placement from preview</summary>
        public PlacementResult ExecutePlacement(PlacementPreview preview, string by = "") =>
            Placement.Place(preview, by);

        /// <summary>Check if can place at position</summary>
        public bool CanPlace(BlockPos pos) => Modifier.CanPlace(pos);

        #endregion

        #region Prefab API

        /// <summary>Get prefab at position</summary>
        public PlacedPrefab? GetPrefabAt(BlockPos pos) => Prefabs.GetAt(pos);

        /// <summary>Remove prefab at position</summary>
        public bool RemovePrefab(BlockPos pos) => Prefabs.Remove(pos);

        /// <summary>Get all prefabs in range</summary>
        public IEnumerable<PlacedPrefab> GetPrefabsInRange(BlockPos center, int radius) =>
            Prefabs.GetInRange(center, radius);

        #endregion

        #region Chunk Management

        /// <summary>Get chunks affected by a position change</summary>
        public (int cx, int cy, int cz)[] GetAffectedChunks(BlockPos pos)
        {
            var chunks = new HashSet<(int, int, int)>();
            var main = pos.GetChunk();
            chunks.Add(main);

            // Add neighbors if near boundary
            int localX = pos.X % GridConfig.CHUNK_SIZE_BLOCKS;
            int localY = pos.Y % GridConfig.CHUNK_HEIGHT_BLOCKS;
            int localZ = pos.Z % GridConfig.CHUNK_SIZE_BLOCKS;

            if (localX <= 0) chunks.Add((main.cx - 1, main.cy, main.cz));
            if (localX >= GridConfig.CHUNK_SIZE_BLOCKS - 1) chunks.Add((main.cx + 1, main.cy, main.cz));
            if (localY <= 0) chunks.Add((main.cx, main.cy - 1, main.cz));
            if (localY >= GridConfig.CHUNK_HEIGHT_BLOCKS - 1) chunks.Add((main.cx, main.cy + 1, main.cz));
            if (localZ <= 0) chunks.Add((main.cx, main.cy, main.cz - 1));
            if (localZ >= GridConfig.CHUNK_SIZE_BLOCKS - 1) chunks.Add((main.cx, main.cy, main.cz + 1));

            return chunks.ToArray();
        }

        #endregion

        #region Player Management

        /// <summary>Register a player in the world</summary>
        public void AddPlayer(string playerId, PlayerWorldState state)
        {
            players[playerId] = state;
        }

        /// <summary>Remove a player from the world</summary>
        public void RemovePlayer(string playerId)
        {
            players.Remove(playerId);
        }

        /// <summary>Get player state</summary>
        public PlayerWorldState? GetPlayer(string playerId)
        {
            return players.TryGetValue(playerId, out var state) ? state : null;
        }

        /// <summary>Get all players</summary>
        public IEnumerable<PlayerWorldState> GetAllPlayers() => players.Values;

        #endregion

        #region Update

        /// <summary>Update world (call every frame)</summary>
        public void Update(float deltaTime)
        {
            Placement.Update(deltaTime);
        }

        #endregion
    }

    /// <summary>
    /// Player state within the world
    /// </summary>
    public class PlayerWorldState
    {
        public string PlayerId { get; set; } = "";
        public (float x, float y, float z) Position { get; set; }
        public (float yaw, float pitch) Rotation { get; set; }
        public bool IsOnline { get; set; }
        public DateTime LastUpdate { get; set; }

        public BlockPos GetBlockPosition() => BlockPos.FromWorld(Position.x, Position.y, Position.z);
    }
}
