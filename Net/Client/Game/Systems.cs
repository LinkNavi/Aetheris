// Net/Client/Game/GameSystems.cs - Initializes and manages all game systems
using System;
using OpenTK.Mathematics;
using AetherisClient.Rendering;

namespace Aetheris
{
    public class GameSystems : IDisposable
    {
        public Inventory Inventory { get; private set; }
        public PlayerStats Stats { get; private set; }
        public PlacedBlockManager PlacedBlocks { get; private set; }
        public BlockRenderer BlockRenderer { get; private set; }
        public BlockPlacementSystem PlacementSystem { get; private set; }
        public BlockBreakingSystem BreakingSystem { get; private set; }
        public CraftingManager Crafting { get; private set; }
        
        private bool initialized;
        
        public void Initialize()
        {
            if (initialized) return;
            
            // Initialize registries
            ItemRegistry.Initialize();
            CraftingRegistry.Initialize();
            
            // Create core systems
            Inventory = new Inventory(40);
            Stats = new PlayerStats(100f, 100f);
            PlacedBlocks = new PlacedBlockManager();
            BlockRenderer = new BlockRenderer();
            
            // Create gameplay systems
            PlacementSystem = new BlockPlacementSystem(PlacedBlocks, Inventory);
            BreakingSystem = new BlockBreakingSystem(PlacedBlocks, Inventory);
            Crafting = new CraftingManager(Inventory);
            
            // Wire up events
            Stats.OnDeath += OnPlayerDeath;
            
            // Give starter items
            GiveStarterItems();
            
            initialized = true;
            Console.WriteLine("[GameSystems] All systems initialized");
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
            
            PlacementSystem.UpdateCooldown(deltaTime);
            
            var selectedItem = Inventory.GetSlot(selectedSlot);
            int toolId = selectedItem?.ItemId ?? 0;
            
            // Handle placement
            if (placePressed && selectedItem != null)
            {
                var itemDef = ItemRegistry.Get(selectedItem.ItemId);
                if (itemDef?.PlacesBlock != null)
                {
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
            
            // Handle breaking
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
            var blocks = PlacedBlocks.GetBlocksInRange(cameraPos, 100f);
            BlockRenderer.Render(blocks, view, projection, cameraPos);
        }
        
        public void Dispose()
        {
            BlockRenderer?.Dispose();
        }
    }
}
