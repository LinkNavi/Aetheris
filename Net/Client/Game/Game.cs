using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using OpenTK.Graphics.OpenGL4;
using OpenTK.Mathematics;
using OpenTK.Windowing.Common;
using OpenTK.Windowing.Desktop;
using OpenTK.Windowing.GraphicsLibraryFramework;
using AetherisClient.Rendering;
using Aetheris.UI;

namespace Aetheris
{
    public class Game : GameWindow
    {
        // Core systems
        public Renderer Renderer { get; private set; }
        private readonly Dictionary<(int, int, int), Aetheris.Chunk> loadedChunks;
        public Player player;
        private readonly Client? client;
        private readonly ChunkManager chunkManager;
        public PlayerNetworkController? NetworkController { get; private set; }
        
        // Game systems (new unified approach)
        private GameSystems gameSystems = null!;
        
        // UI systems
        private InventoryUI? inventoryUI;
        private HUDRenderer? hudRenderer;
        private ChatSystem? chatSystem;
        private FontRenderer? fontRenderer;
        private TooltipSystem? tooltipSystem;
        private BlockPlacementPreview? blockPreview;
        private CrosshairRenderer? crosshair;
        private EnhancedHotbarRenderer? enhancedHotbar;
        
        // Additional systems
        private MiningSystem? miningSystem;
        private RespawnSystem? respawnSystem;
        private FallDamageTracker? fallDamageTracker;
        private TotemManager? totemManager;
        private EntityRenderer? entityRenderer;
        
        // Placed blocks (accessed by both game and systems)
        public PlacedBlockManager PlacedBlocks => gameSystems?.PlacedBlocks!;
        
        // Configuration
        private int renderDistance = ClientConfig.RENDER_DISTANCE;
        private float chunkUpdateTimer = 0f;
        private const float CHUNK_UPDATE_INTERVAL = 0.5f;
        
        // Logging
        private const string LogFileName = "physics_debug.log";
        private StreamWriter? logWriter;
        private TextWriter? originalConsoleOut;
        private TextWriter? originalConsoleError;
        private TeeTextWriter? teeWriter;

        public Game(Dictionary<(int, int, int), Aetheris.Chunk> loadedChunks, Client? client = null)
       : base(GameWindowSettings.Default, new NativeWindowSettings()
       {
           ClientSize = new Vector2i(1920, 1080),
           Title = "Aetheris Client"
       })
        {
            this.loadedChunks = loadedChunks ?? new Dictionary<(int, int, int), Aetheris.Chunk>();
            this.client = client;
            this.chunkManager = new ChunkManager();

            SetupLogging();

            WorldGen.Initialize();
            Console.WriteLine("[Game] WorldGen initialized");

            Renderer = new Renderer();
            Console.WriteLine("[Game] Renderer initialized");

            entityRenderer = new EntityRenderer();
            Console.WriteLine("[Game] EntityRenderer initialized");

            player = new Player(new Vector3(16, 50, 16));
            Console.WriteLine("[Game] Player initialized at position: {0}", player.Position);

            if (client != null)
            {
                NetworkController = new PlayerNetworkController(player, client);
                Console.WriteLine("[Game] Network controller initialized");
            }
            else
            {
                Console.WriteLine("[Game] Running in single-player mode (no network)");
            }
        }

        private void SetupLogging()
        {
            try
            {
                if (File.Exists(LogFileName))
                    File.Delete(LogFileName);

                var fs = new FileStream(LogFileName, FileMode.Create, FileAccess.Write, FileShare.Read);
                logWriter = new StreamWriter(fs, Encoding.UTF8) { AutoFlush = true };

                originalConsoleOut = Console.Out;
                originalConsoleError = Console.Error;

                teeWriter = new TeeTextWriter(originalConsoleOut, logWriter);

                Console.SetOut(teeWriter);
                Console.SetError(teeWriter);

                Console.WriteLine($"[Logging] Started logging to '{LogFileName}'");
            }
            catch (Exception ex)
            {
                try
                {
                    Console.SetOut(Console.Out);
                    Console.WriteLine("[Logging] Failed to initialize file logging: " + ex.Message);
                }
                catch { }
            }
        }

        protected override void OnLoad()
        {
            base.OnLoad();
            GL.ClearColor(0.15f, 0.18f, 0.2f, 1.0f);
            GL.Enable(EnableCap.DepthTest);
            GL.DepthFunc(DepthFunction.Less);
            GL.Enable(EnableCap.CullFace);
            GL.CullFace(TriangleFace.Back);
            GL.FrontFace(FrontFaceDirection.Ccw);
            CursorState = CursorState.Grabbed;
            
            // Initialize item and crafting registries
            ItemRegistry.Initialize();
            
            // Initialize game systems
            gameSystems = new GameSystems();
            gameSystems.Initialize(player, this, client);
            
            // Link player inventory to systems
            player.Inventory = gameSystems.Inventory;
            
            // Initialize mining system
            miningSystem = new MiningSystem(player, this, OnBlockMined);

            // Initialize font renderer
            fontRenderer = new FontRenderer("assets/fonts/Roboto-Regular.ttf", 48);
            fontRenderer.SetProjection(Matrix4.CreateOrthographicOffCenter(0, Size.X, Size.Y, 0, -1, 1));

            // Initialize UI systems
            tooltipSystem = new TooltipSystem(fontRenderer);
            inventoryUI = new InventoryUI(player.Inventory, fontRenderer);
            hudRenderer = new HUDRenderer();
            chatSystem = new ChatSystem(fontRenderer);
            blockPreview = new BlockPlacementPreview();
            crosshair = new CrosshairRenderer();
            enhancedHotbar = new EnhancedHotbarRenderer(player.Inventory, fontRenderer);

            // Initialize gameplay systems
            respawnSystem = new RespawnSystem(
                player,
                gameSystems.Stats,
                player.Position,
                (msg, type) => chatSystem?.AddMessage(msg, type)
            );

            fallDamageTracker = new FallDamageTracker();
            totemManager = new TotemManager(player.Inventory, gameSystems.Stats);

            Console.WriteLine("[Game] All systems initialized");

            // Load texture atlas
            string[] atlasPaths = new[]
            {
                "textures/atlas.png",
                "../textures/atlas.png",
                "../../textures/atlas.png",
                "atlas.png"
            };

            bool atlasLoaded = false;
            foreach (var path in atlasPaths)
            {
                if (System.IO.File.Exists(path))
                {
                    Console.WriteLine($"[Game] Found atlas at: {path}");
                    Renderer.LoadTextureAtlas(path);
                    atlasLoaded = true;
                    break;
                }
            }

            if (!atlasLoaded)
            {
                Console.WriteLine("[Game] No atlas.png found - using procedural fallback");
                Renderer.CreateProceduralAtlas();
            }

            // Load initial chunks
            foreach (var kv in loadedChunks)
            {
                var coord = kv.Key;
                var chunk = kv.Value;

                var chunkCoord = new ChunkCoord(coord.Item1, coord.Item2, coord.Item3);
                var meshFloats = MarchingCubes.GenerateMesh(chunk, chunkCoord, chunkManager, 0.5f);
                chunk.GenerateCollisionMesh(meshFloats);

                Console.WriteLine($"[Game] Loading chunk {coord} with {meshFloats.Length / 7} vertices");
                Renderer.LoadMeshForChunk(coord.Item1, coord.Item2, coord.Item3, meshFloats);
            }

            Console.WriteLine($"[Game] Loaded {loadedChunks.Count} chunks");

            chatSystem?.AddMessage("Welcome to Aetheris!", ChatMessageType.System);
            chatSystem?.AddMessage("Press T to chat, E for inventory, F for stats", ChatMessageType.System);
        }

        private void OnBlockMined(Vector3 blockPos, BlockType blockType)
        {
            Console.WriteLine($"[Client] Mined {blockType} at {blockPos}");

            int x = (int)blockPos.X;
            int y = (int)blockPos.Y;
            int z = (int)blockPos.Z;

            // Check if this is a placed block
            if (PlacedBlocks.HasBlockAt(x, y, z))
            {
                PlacedBlocks.RemoveBlock(x, y, z);
                Console.WriteLine($"[Client] Removed placed block at ({x}, {y}, {z})");

                var placedBlock = PlacedBlocks.GetBlockAt(x, y, z);
                if (placedBlock != null)
                {
                    blockType = (BlockType)((int)placedBlock.BlockType);
                }
            }

            int itemId = BlockTypeToItemId(blockType);
            if (itemId > 0)
            {
                bool added = player.Inventory.AddItem(itemId, 1);
                if (added)
                {
                    Console.WriteLine($"[Client] Added {blockType} to inventory");
                    chatSystem?.AddMessage($"Mined {blockType}", ChatMessageType.System);
                }
                else
                {
                    Console.WriteLine($"[Client] Inventory full, couldn't add {blockType}");
                    chatSystem?.AddMessage("Inventory full!", ChatMessageType.Error);
                }
            }

            // Send to server
            if (client != null)
            {
                _ = client.SendBlockBreakAsync(x, y, z);
            }
        }

        private int BlockTypeToItemId(BlockType blockType)
        {
            return blockType switch
            {
                BlockType.Stone => 1,
                BlockType.Dirt => 2,
                BlockType.Grass => 3,
                BlockType.Sand => 4,
                BlockType.Snow => 5,
                BlockType.Gravel => 6,
                BlockType.Wood => 7,
                BlockType.Leaves => 8,
                _ => 0
            };
        }

        protected override void OnUpdateFrame(FrameEventArgs e)
        {
            base.OnUpdateFrame(e);

            float deltaTime = (float)e.Time;

            Renderer.ProcessPendingUploads();

            if (IsKeyDown(Keys.Escape))
                Close();

            // Update chat system
            chatSystem?.Update(KeyboardState, deltaTime);

            // Update inventory UI
            bool inventoryOpen = false;
            if (inventoryUI != null)
            {
                inventoryUI.Update(KeyboardState, MouseState, Size, deltaTime);
                inventoryOpen = inventoryUI.IsInventoryOpen();
            }

            bool chatOpen = chatSystem?.IsChatOpen() ?? false;

            // Update tooltip
            bool showTooltip = inventoryUI != null && inventoryUI.IsInventoryOpen() && inventoryUI.GetHoveredSlot() >= 0;
            tooltipSystem?.Update(deltaTime, showTooltip);

            // Gameplay updates (only when not in UI)
            if (!inventoryOpen && !chatOpen)
            {
                if (CursorState != CursorState.Grabbed)
                {
                    CursorState = CursorState.Grabbed;
                }

                // Player movement
                if (NetworkController != null)
                {
                    NetworkController.Update(e, KeyboardState, MouseState);
                }
                else
                {
                    player.Update(e, KeyboardState, MouseState);
                }

                // Mining
                miningSystem?.Update(deltaTime, MouseState, IsFocused);

                // Block placement (handled by game systems)
                if (MouseState.IsButtonPressed(MouseButton.Right))
                {
                    gameSystems?.Update(
                        deltaTime,
                        player.Position,
                        player.GetForward(),
                        true,  // place pressed
                        false,
                        player.Inventory.SelectedHotbarSlot
                    );
                }
            }
            else
            {
                if (CursorState != CursorState.Normal)
                {
                    CursorState = CursorState.Normal;
                }
            }

            // Update gameplay systems
            respawnSystem?.Update(deltaTime);
            fallDamageTracker?.Update(player, gameSystems.Stats);
            totemManager?.ApplyTotemEffects();

            // Update armor from equipped items
            float totalArmor = ArmorCalculator.CalculateTotalArmor(player.Inventory);
            gameSystems.Stats.Armor = totalArmor;

            // Update stats
            gameSystems.Stats.Update(deltaTime);
            hudRenderer?.Update(gameSystems.Stats, deltaTime);

            // Chunk loading
            if (chunkUpdateTimer == 0f)
            {
                Vector3 playerChunk = player.GetPlayersChunk();
                client?.UpdateLoadedChunks(playerChunk, renderDistance);
            }

            chunkUpdateTimer += deltaTime;
            if (chunkUpdateTimer >= CHUNK_UPDATE_INTERVAL)
            {
                chunkUpdateTimer = 0f;
                Vector3 playerChunk = player.GetPlayersChunk();
                client?.UpdateLoadedChunks(playerChunk, renderDistance);
            }

            // Render distance controls
            if (KeyboardState.IsKeyPressed(Keys.Equal) || KeyboardState.IsKeyPressed(Keys.KeyPadAdd))
            {
                renderDistance = Math.Min(renderDistance + 1, 999);
                Console.WriteLine($"[Game] Render distance: {renderDistance}");
                chatSystem?.AddMessage($"Render distance: {renderDistance}", ChatMessageType.System);
            }
            if (KeyboardState.IsKeyPressed(Keys.Minus) || KeyboardState.IsKeyPressed(Keys.KeyPadSubtract))
            {
                renderDistance = Math.Max(renderDistance - 1, 1);
                Console.WriteLine($"[Game] Render distance: {renderDistance}");
                chatSystem?.AddMessage($"Render distance: {renderDistance}", ChatMessageType.System);
            }

            // Stats display
            if (KeyboardState.IsKeyPressed(Keys.F))
            {
                ShowStats();
            }

            // Debug heal/damage
            if (KeyboardState.IsKeyPressed(Keys.H))
            {
                gameSystems.Stats.Heal(20f);
                hudRenderer?.OnHealed();
                chatSystem?.AddMessage("Healed +20 HP", ChatMessageType.Success);
            }

            if (KeyboardState.IsKeyPressed(Keys.J))
            {
                gameSystems.Stats.TakeDamage(10f);
                hudRenderer?.OnDamageTaken();
                chatSystem?.AddMessage("Took 10 damage", ChatMessageType.Error);
            }
        }

        private void ShowStats()
        {
            chatSystem?.AddMessage("=== Player Stats ===", ChatMessageType.System);
            chatSystem?.AddMessage($"Health: {gameSystems.Stats.Health:F1}/{gameSystems.Stats.MaxHealth:F1}", ChatMessageType.System);
            chatSystem?.AddMessage($"Armor: {gameSystems.Stats.Armor:F1}/{gameSystems.Stats.MaxArmor:F1}", ChatMessageType.System);
            chatSystem?.AddMessage($"Hunger: {gameSystems.Stats.Hunger:F1}/{gameSystems.Stats.MaxHunger:F1}", ChatMessageType.System);
            chatSystem?.AddMessage($"Position: {player.Position}", ChatMessageType.System);
        }

        protected override void OnTextInput(TextInputEventArgs e)
        {
            base.OnTextInput(e);

            if (chatSystem != null && chatSystem.IsChatOpen())
            {
                chatSystem.HandleTextInput((char)e.Unicode);
            }
        }

        protected override void OnKeyDown(KeyboardKeyEventArgs e)
        {
            base.OnKeyDown(e);

            if (chatSystem != null && chatSystem.IsChatOpen())
            {
                chatSystem.HandleKeyPress(e.Key);
            }
        }

        protected override void OnRenderFrame(FrameEventArgs e)
        {
            base.OnRenderFrame(e);
            GL.Clear(ClearBufferMask.ColorBufferBit | ClearBufferMask.DepthBufferBit);

            var projection = Matrix4.CreatePerspectiveFieldOfView(
                OpenTK.Mathematics.MathHelper.DegreesToRadians(60f),
                Size.X / (float)Size.Y,
                0.1f,
                1000f);
            var view = player.GetViewMatrix();
            
            // Render placed blocks (from game systems)
            gameSystems?.Render(view, projection, player.Position);
            
            // Render terrain
            Renderer.Render(projection, view, player.Position);

            // Render remote players
            if (entityRenderer != null && NetworkController != null)
            {
                var remotePlayers = NetworkController.RemotePlayers;
                if (remotePlayers != null && remotePlayers.Count > 0)
                {
                    entityRenderer.RenderPlayers(
                        remotePlayers as Dictionary<string, RemotePlayer>,
                        Renderer.psxEffects,
                        player.Position,
                        Renderer.UsePSXEffects
                    );
                }
            }

            // Render block placement preview
            if (inventoryUI != null && gameSystems.PlacementSystem != null && blockPreview != null &&
                !inventoryUI.IsInventoryOpen() && !(chatSystem?.IsChatOpen() ?? false))
            {
                var selectedItem = player.Inventory.GetSelectedItem();
                if (selectedItem.ItemId > 0)
                {
                    var itemDef = ItemRegistry.Get(selectedItem.ItemId);
                    if (itemDef?.PlacesBlock.HasValue == true)
                    {
                        var preview = gameSystems.PlacementSystem.GetPlacementPreview();
                        if (preview.canPlace)
                        {
                            blockPreview.Render(preview.position, preview.canPlace, view, projection);
                        }
                    }
                }
            }

            GL.BindVertexArray(0);
            GL.UseProgram(0);

            // Render UI
            hudRenderer?.Render(gameSystems.Stats, Size);
            enhancedHotbar?.Render(Size);
            crosshair?.Render(Size);
            inventoryUI?.Render(Size);

            // Render tooltips
            if (inventoryUI != null && tooltipSystem != null && inventoryUI.GetHoveredSlot() >= 0)
            {
                var item = player.Inventory.GetSlot(inventoryUI.GetHoveredSlot());
                if (item.ItemId > 0)
                {
                    Vector2 mousePos = new Vector2(MouseState.X, MouseState.Y);
                    tooltipSystem.RenderItemTooltip(item.ItemId, mousePos, Size);
                }
            }

            chatSystem?.Render(Size);

            SwapBuffers();
        }

        protected override void OnUnload()
        {
            base.OnUnload();

            Renderer.Dispose();
            inventoryUI?.Dispose();
            hudRenderer?.Dispose();
            chatSystem?.Dispose();
            entityRenderer?.Dispose();
            gameSystems?.Dispose();

            try
            {
                if (originalConsoleOut != null)
                    Console.SetOut(originalConsoleOut);
                if (originalConsoleError != null)
                    Console.SetError(originalConsoleError);
                logWriter?.Flush();
                logWriter?.Dispose();
                Console.WriteLine("[Game] Cleanup complete");
            }
            catch (Exception ex)
            {
                Console.WriteLine("[Game] Error closing log: " + ex.Message);
            }
        }

        public Vector3 GetPlayerPosition() => player.Position;

        public void RunGame() => Run();

        private class TeeTextWriter : TextWriter
        {
            private readonly TextWriter consoleWriter;
            private readonly StreamWriter fileWriter;
            private readonly object writeLock = new object();

            public TeeTextWriter(TextWriter consoleWriter, StreamWriter fileWriter)
            {
                this.consoleWriter = consoleWriter ?? throw new ArgumentNullException(nameof(consoleWriter));
                this.fileWriter = fileWriter ?? throw new ArgumentNullException(nameof(fileWriter));
            }

            public override Encoding Encoding => Encoding.UTF8;

            public override void WriteLine(string? value)
            {
                lock (writeLock)
                {
                    string line = $"[{DateTime.Now:yyyy-MM-dd HH:mm:ss.fff}] {value}";
                    try { consoleWriter.WriteLine(line); } catch { }
                    try { fileWriter.WriteLine(line); } catch { }
                }
            }

            public override void Write(string? value)
            {
                lock (writeLock)
                {
                    try { consoleWriter.Write(value); } catch { }
                    try { fileWriter.Write(value); } catch { }
                }
            }

            public override void Write(char value)
            {
                lock (writeLock)
                {
                    try { consoleWriter.Write(value); } catch { }
                    try { fileWriter.Write(value); } catch { }
                }
            }

            public override void Flush()
            {
                lock (writeLock)
                {
                    try { consoleWriter.Flush(); } catch { }
                    try { fileWriter.Flush(); } catch { }
                }
            }

            protected override void Dispose(bool disposing)
            {
                if (disposing)
                {
                    try { fileWriter.Flush(); } catch { }
                }
                base.Dispose(disposing);
            }
        }
    }
}
