// Net/Client/Game/Systems.cs - Updated with GameLogic integration
using System;
using OpenTK.Mathematics;
using AetherisClient.Rendering;
using Aetheris.GameLogic;

namespace Aetheris
{
    public class GameSystems : IDisposable
    {
        public Inventory Inventory { get; private set; } = null!;
        public PlayerStats Stats { get; private set; } = null!;
        public PlacedBlockManager PlacedBlocks { get; private set; } = null!;
        public BlockRenderer BlockRenderer { get; private set; } = null!;
        public BlockPlacementSystem? PlacementSystem { get; private set; }
        public BlockBreakingSystem BreakingSystem { get; private set; } = null!;
        public CraftingManager Crafting { get; private set; } = null!;
        
public PrefabMiningSystem? PrefabMining { get; private set; }
        private bool initialized;
        private Player? player;
        private Game? game;
        private Client? client;
        private GameWorld? clientWorld;
        
        public void Initialize(Player player, Game game, Client? client, GameWorld? clientWorld = null)
        {
            if (initialized) return;
            
            this.player = player;
            this.game = game;
            this.client = client;
            this.clientWorld = clientWorld;
            
            // Initialize registries
            ItemRegistry.Initialize();
            CraftingRegistry.Initialize();
            BlockRegistry.Initialize(); // NEW: Initialize GameLogic block registry
            
            // Create core systems
            Inventory = new Inventory();
            Stats = new PlayerStats(100f, 100f);
            PlacedBlocks = new PlacedBlockManager();
            BlockRenderer = new BlockRenderer();
            
            // Create gameplay systems with GameLogic integration
            PlacementSystem = new BlockPlacementSystem(player, game, client);
            BreakingSystem = new BlockBreakingSystem(PlacedBlocks, Inventory);
            Crafting = new CraftingManager(Inventory);
            PrefabMining = new PrefabMiningSystem(
    player, 
    game, 
    clientWorld, 
    client,
    (itemId, count) => {
        bool added = Inventory.AddItem(itemId, count);
        if (added)
        {
            var itemDef = ItemRegistry.Get(itemId);
            string itemName = itemDef?.Name ?? $"Item {itemId}";
            Console.WriteLine($"[PrefabMining] Collected {count}x {itemName}");
        }
    }
);
            // Wire up events
            Stats.OnDeath += OnPlayerDeath;
            
            // Give starter items
            GiveStarterItems();
            
            initialized = true;
            Console.WriteLine("[GameSystems] All systems initialized with GameLogic integration");
        }
        
        private void GiveStarterItems()
        {
            Inventory.AddItem(7, 20);   // 20 wood
            Inventory.AddItem(1, 10);   // 10 stone
            Inventory.AddItem(2, 10);   // 10 dirt
            Inventory.AddItem(50, 1);   // Wooden pickaxe
            Inventory.AddItem(100, 5);  // 5 bread
            Console.WriteLine("[GameSystems] Starter items added");
        }
        
        private void OnPlayerDeath()
        {
            Console.WriteLine("[GameSystems] Player died! Respawning...");
            Stats.Respawn();
        }
        
        public void Update(float deltaTime, Vector3 playerPos, Vector3 lookDir, 
            bool placePressed, bool breakHeld, int selectedSlot)
        {
            // Update stats (hunger drain, regen)
            Stats.Update(deltaTime);
            
            PlacementSystem?.UpdateCooldown(deltaTime);
            
            var selectedItem = Inventory.GetSlot(selectedSlot);
            int toolId = selectedItem.ItemId;
            
            // Handle placement
            if (placePressed && selectedItem.ItemId > 0)
            {
                var itemDef = ItemRegistry.Get(selectedItem.ItemId);
                if (itemDef?.PlacesBlock != null && PlacementSystem != null)
                {
                    // Use the new GameLogic-based placement system
                    if (PlacementSystem.TryPlace(playerPos, lookDir, itemDef.PlacesBlock.Value))
                    {
                        Inventory.RemoveItem(selectedItem.ItemId, 1);
                    }
                }
                else if (itemDef?.Category == ItemCategory.Food)
                {
                    Stats.UseFood(selectedItem.ItemId, Inventory);
                }
            }
            
            // Handle breaking (still uses old system for placed blocks)
            if (breakHeld)
            {
                var hit = BreakingSystem.RaycastPlacedBlocks(playerPos, lookDir, 5f);
                if (hit.HasValue)
                {
                    if (BreakingSystem.TargetBlock != hit.Value.pos)
                        BreakingSystem.StartBreaking(hit.Value.pos, toolId);
                    BreakingSystem.Update(deltaTime, true, toolId);
                }
                else
                {
                    BreakingSystem.StopBreaking();
                }
            }
            else
            {
                BreakingSystem.StopBreaking();
            }
        }
        
        public void Render(Matrix4 view, Matrix4 projection, Vector3 cameraPos)
        {
            if (BlockRenderer == null || PlacedBlocks == null) return;
            
            int atlasTexture = AetherisClient.Rendering.AtlasManager.IsLoaded
                ? AetherisClient.Rendering.AtlasManager.AtlasTextureId
                : 0;
            
            BlockRenderer.RenderBlocks(PlacedBlocks, cameraPos, view, projection, 
                atlasTexture, 0.003f, 100f);
        }
        
        public void Dispose()
        {
            BlockRenderer?.Dispose();
        }
    }
}
