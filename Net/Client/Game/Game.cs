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
        public Renderer Renderer { get; private set; }
        private readonly Dictionary<(int, int, int), Aetheris.Chunk> loadedChunks;
        public Player player;
        private readonly Client? client;
        private readonly ChunkManager chunkManager;
        public PlayerNetworkController? NetworkController { get; private set; }

        private int renderDistance = ClientConfig.RENDER_DISTANCE;
        private float chunkUpdateTimer = 0f;
        private const float CHUNK_UPDATE_INTERVAL = 0.5f;

        // UI / Inventory
        private UIManager? uiManager;
        private Aetheris.Inventory? inventory;
        private InventoryPanel? inventoryPanel;
        private bool inventoryVisible = false;

        // simple held stack for pick/drop
        private Aetheris.ItemStack heldStack;
        private bool holdingItem = false;

        private MiningSystem? miningSystem;

        // Logging
        private const string LogFileName = "physics_debug.log";
        private StreamWriter? logWriter;
        private TextWriter? originalConsoleOut;
        private TextWriter? originalConsoleError;
        private TeeTextWriter? teeWriter;

        private EntityRenderer? entityRenderer;
        private PlayerNetworkController? networkController; // duplicate reference used by rendering code

        public Game(Dictionary<(int, int, int), Aetheris.Chunk> loadedChunks, Client? client = null)
            : base(GameWindowSettings.Default, new NativeWindowSettings()
            {
                ClientSize = new Vector2i(1920, 1080),
                Title = "Aetheris Client"
            })
        {
            this.loadedChunks = loadedChunks ?? new Dictionary<(int, int, int), Aetheris.Chunk>();
            this.client = client;

            // initialize chunk manager (was commented out previously)
            chunkManager = new ChunkManager();

            SetupLogging();

            // Initialize WorldGen FIRST (needed for player collision)
            WorldGen.Initialize();
            Console.WriteLine("[Game] WorldGen initialized");

            // Create Renderer
            Renderer = new Renderer();
            Console.WriteLine("[Game] Renderer initialized");

            // Create EntityRenderer
            entityRenderer = new EntityRenderer();
            Console.WriteLine("[Game] EntityRenderer initialized");

            // Create player (MUST be after WorldGen.Initialize())
            player = new Player(new Vector3(16, 50, 16));
            Console.WriteLine("[Game] Player initialized at position: {0}", player.Position);

            // Create network controller (MUST be after player is created)
            if (client != null)
            {
                NetworkController = new PlayerNetworkController(player, client);
                networkController = NetworkController; // Keep both references in sync
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

            // GL state
            GL.ClearColor(0.15f, 0.18f, 0.2f, 1.0f);
            GL.Enable(EnableCap.DepthTest);
            GL.DepthFunc(DepthFunction.Less);
            GL.Enable(EnableCap.CullFace);
            GL.CullFace(TriangleFace.Back);
            GL.FrontFace(FrontFaceDirection.Ccw);

            // start grabbed by default
            CursorState = CursorState.Grabbed;

            // mining system
            miningSystem = new MiningSystem(player, this, OnBlockMined);

            // Load atlas (fallbacks)
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
                if (File.Exists(path))
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

            // --- UI setup (minimal helper shaders/buffers created here if you don't already have them) ---
            int uiShader = CreateSimpleUIShader();
            int uiVao = CreateQuadVao();
            int uiVbo = CreateQuadVbo();

            uiManager = new UIManager(this, uiShader, uiVao, uiVbo);

            // assign text renderer if Renderer exposes one
            if (Renderer != null)
            {
                try
                {
                    // If Renderer provides a TextRenderer instance, use it; otherwise user must set it later.
                    Aetheris.UI.FontRenderer fontRenderer = new FontRenderer("assets/font.ttf", 48);
                }
                catch
                {
                    Console.WriteLine("[Game] WARNING: Renderer.TextRenderer not found; UI text will be missing.");
                }
            }

            // create inventory and fill a bit for testing
            inventory = new Aetheris.Inventory();
            inventory.AddItem(1, 32);
            inventory.AddItem(2, 16);
            inventory.SelectedHotbarSlot = 0;

            // panel + layout
            var slotSize = new Vector2(72, 72);
            float spacing = 8f;
            inventoryPanel = new InventoryPanel(inventory, slotSize, spacing);

            // center the panel on screen
            var panelPos = new Vector2((Size.X - inventoryPanel.Size.X) / 2f, (Size.Y - inventoryPanel.Size.Y) / 2f);
            inventoryPanel.LayoutSlots(panelPos, slotSize, spacing);

            // add panel + slots to UI manager (panel renders itself; slots receive input)
            uiManager.AddElement(inventoryPanel);
            foreach (var s in inventoryPanel.Slots)
            {
                s.OnSlotClicked = (idx) => OnInventorySlotClicked(idx);
                uiManager.AddElement(s);
            }

            // start hidden
            inventoryPanel.Visible = false;
            foreach (var s in inventoryPanel.Slots) s.Visible = false;
            inventoryVisible = false;

            // Load pre-fetched chunks into renderer and chunkManager
            foreach (var kv in loadedChunks)
            {
                var coord = kv.Key;
                var chunk = kv.Value;

                var chunkCoord = new ChunkCoord(coord.Item1, coord.Item2, coord.Item3);

                // Generate mesh for rendering
                var meshFloats = MarchingCubes.GenerateMesh(chunk, chunkCoord, chunkManager, 0.5f);

                // Generate collision mesh for physics
                chunk.GenerateCollisionMesh(meshFloats);

                Console.WriteLine($"[Game] Loading chunk {coord} with {meshFloats.Length / 7} vertices");
                Renderer.LoadMeshForChunk(coord.Item1, coord.Item2, coord.Item3, meshFloats);
            }

            Console.WriteLine($"[Game] Loaded {loadedChunks.Count} chunks");
        }

        private readonly Queue<Action> pendingMainThreadActions = new Queue<Action>();
        private readonly object mainThreadLock = new object();

        private void OnBlockMined(Vector3 blockPos, BlockType blockType)
        {
            Console.WriteLine($"[Client] Mined {blockType} at {blockPos}");

            int x = (int)blockPos.X;
            int y = (int)blockPos.Y;
            int z = (int)blockPos.Z;

            // Send to server via TCP (reliable)
            if (client != null)
            {
                _ = client.SendBlockBreakAsync(x, y, z);
            }

            // The server will broadcast the block break back to us via TCP
            // and we'll reload chunks with the server's authoritative state
        }

        public void RegenerateMeshForBlock(Vector3 blockPos)
        {
            int blockX = (int)blockPos.X;
            int blockY = (int)blockPos.Y;
            int blockZ = (int)blockPos.Z;

            float miningRadius = 5.0f;
            int affectRadius = (int)Math.Ceiling(miningRadius);

            HashSet<(int, int, int)> chunksToUpdate = new HashSet<(int, int, int)>();

            for (int dx = -affectRadius; dx <= affectRadius; dx++)
            {
                for (int dy = -affectRadius; dy <= affectRadius; dy++)
                {
                    for (int dz = -affectRadius; dz <= affectRadius; dz++)
                    {
                        int wx = blockX + dx;
                        int wy = blockY + dy;
                        int wz = blockZ + dz;

                        int cx = (int)Math.Floor((float)wx / ClientConfig.CHUNK_SIZE);
                        int cy = (int)Math.Floor((float)wy / ClientConfig.CHUNK_SIZE_Y);
                        int cz = (int)Math.Floor((float)wz / ClientConfig.CHUNK_SIZE);

                        chunksToUpdate.Add((cx, cy, cz));
                    }
                }
            }

            Console.WriteLine($"[Client] Re-requesting {chunksToUpdate.Count} chunks from server after block break");

            foreach (var (cx, cy, cz) in chunksToUpdate)
            {
                loadedChunks.Remove((cx, cy, cz));
                if (client != null)
                {
                    client.ForceReloadChunk(cx, cy, cz);
                }
            }
        }

        protected override void OnUpdateFrame(FrameEventArgs e)
        {
            base.OnUpdateFrame(e);

            lock (mainThreadLock)
            {
                while (pendingMainThreadActions.Count > 0)
                {
                    var action = pendingMainThreadActions.Dequeue();
                    try
                    {
                        action?.Invoke();
                    }
                    catch (Exception ex)
                    {
                        Console.WriteLine($"[Game] Error executing pending action: {ex.Message}");
                    }
                }
            }

            float delta = (float)e.Time;

            // Process pending mesh uploads
            Renderer.ProcessPendingUploads();

            if (IsKeyDown(Keys.Escape))
                Close();

            // Toggle inventory (single-press)
            if (KeyboardState.IsKeyPressed(Keys.I))
            {
                ToggleInventory();
            }

            // Update UI manager every frame so UI interactions work
            uiManager?.Update(MouseState, KeyboardState, delta);

            // When inventory is open we skip player input/mining, but keep chunk/network updates running
            if (!inventoryVisible)
            {
                if (NetworkController != null)
                    NetworkController.Update(e, KeyboardState, MouseState);
                else
                    player.Update(e, KeyboardState, MouseState);

                miningSystem?.Update(delta, MouseState, IsFocused);
            }

            // Chunk loading updates (kept running whether inventory is open or not)
            if (chunkUpdateTimer == 0f)
            {
                Vector3 playerChunk = player.GetPlayersChunk();
                client?.UpdateLoadedChunks(playerChunk, renderDistance);
            }

            chunkUpdateTimer += delta;
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
            }
            if (KeyboardState.IsKeyPressed(Keys.Minus) || KeyboardState.IsKeyPressed(Keys.KeyPadSubtract))
            {
                renderDistance = Math.Max(renderDistance - 1, 1);
                Console.WriteLine($"[Game] Render distance: {renderDistance}");
            }
        }

        protected override void OnRenderFrame(FrameEventArgs e)
        {
            base.OnRenderFrame(e);
            GL.Clear(ClearBufferMask.ColorBufferBit | ClearBufferMask.DepthBufferBit);

            // Debug: Raycast
            if (KeyboardState.IsKeyPressed(Keys.R))
            {
                Vector3 forward = player.GetForward();
                for (float dist = 0; dist < 50; dist += 0.5f)
                {
                    Vector3 pos = player.Position + forward * dist;
                    int x = (int)pos.X, y = (int)pos.Y, z = (int)pos.Z;
                    var blockType = WorldGen.GetBlockType(x, y, z);
                    if (blockType != BlockType.Air)
                    {
                        Console.WriteLine($"Looking at: {blockType} at ({x},{y},{z}), distance={dist:F1}");
                        break;
                    }
                }
            }

            // Debug: Biome info
            if (KeyboardState.IsKeyPressed(Keys.B))
            {
                int px = (int)player.Position.X;
                int pz = (int)player.Position.Z;
                WorldGen.PrintBiomeAt(px, pz);
                Console.WriteLine($"Player at: {player.Position}");
            }

            var projection = Matrix4.CreatePerspectiveFieldOfView(
                MathHelper.DegreesToRadians(60f),
                Size.X / (float)Size.Y,
                0.1f,
                1000f);
            var view = player.GetViewMatrix();

            // === RENDER TERRAIN (sets up shader and keeps it active) ===
            Renderer.Render(projection, view, player.Position);

            // === RENDER OTHER PLAYERS (shader still active) ===

            if (entityRenderer != null && networkController != null)
            {
                var remotePlayers = networkController.RemotePlayers;
                if (remotePlayers != null && remotePlayers.Count > 0)
                {
                    entityRenderer.RenderPlayers(
                        (Dictionary<string, RemotePlayer>)remotePlayers,
                        Renderer.psxEffects,
                        player.Position,
                        Renderer.UsePSXEffects
                    );
                }
            }


            // === CLEANUP 3D ===
            GL.BindVertexArray(0);
            GL.UseProgram(0);

            // === RENDER UI (orthographic 2D overlay) ===
            if (uiManager != null)
            {
                var ortho = Matrix4.CreateOrthographicOffCenter(0f, Size.X, Size.Y, 0f, -1f, 1f);
                uiManager.Render(ortho);

                // Held-item ghost (text fallback)
                if (holdingItem && uiManager.TextRenderer != null)
                {
                    var mp = MouseState.Position;
                    string info = $"ID:{heldStack.ItemId} x{heldStack.Count}";
                    uiManager.TextRenderer.DrawText(info, new Vector2(mp.X + 8, mp.Y + 8), 0.9f, new Vector4(1, 1, 1, 1));
                }
            }

            SwapBuffers();
        }

        private bool networkcontrollerExists() => networkController != null;
        private object? networkcontrollerRemotePlayers() => networkController?.RemotePlayers;

        protected override void OnUnload()
        {
            base.OnUnload();

            Renderer.Dispose();

            // Restore console
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

        // ---------------------------
        // Inventory / UI helpers
        // ---------------------------
        private void ToggleInventory()
        {
            inventoryVisible = !inventoryVisible;
            if (inventoryPanel != null)
            {
                inventoryPanel.Visible = inventoryVisible;
                foreach (var s in inventoryPanel.Slots) s.Visible = inventoryVisible;
            }

            CursorState = inventoryVisible ? CursorState.Normal : CursorState.Grabbed;
            Console.WriteLine(inventoryVisible ? "[Game] Inventory opened" : "[Game] Inventory closed");
        }

        private void OnInventorySlotClicked(int index)
        {
            if (inventory == null) return;

            var slot = inventory.Slots[index];

            // If not holding anything, pick up stack
            if (!holdingItem)
            {
                if (slot.ItemId != 0)
                {
                    heldStack = slot;
                    holdingItem = true;
                    inventory.Slots[index] = default;
                    Console.WriteLine($"Picked up ID:{heldStack.ItemId} x{heldStack.Count} from slot {index}");
                }
                return;
            }

            // Holding an item - try place / merge / swap
            if (slot.ItemId == 0)
            {
                inventory.Slots[index] = heldStack;
                holdingItem = false;
                Console.WriteLine($"Placed ID:{inventory.Slots[index].ItemId} x{inventory.Slots[index].Count} into slot {index}");
                return;
            }

            if (slot.ItemId == heldStack.ItemId)
            {
                // merge up to 64
                int canTake = Math.Min(64 - slot.Count, heldStack.Count);
                inventory.Slots[index].Count += canTake;
                heldStack.Count -= canTake;
                if (heldStack.Count <= 0)
                {
                    holdingItem = false;
                    Console.WriteLine($"Merged into slot {index}; finished holding");
                }
                else
                {
                    Console.WriteLine($"Merged into slot {index}; remaining held: {heldStack.Count}");
                }
                return;
            }

            // swap
            var tmp = slot;
            inventory.Slots[index] = heldStack;
            heldStack = tmp;
            Console.WriteLine($"Swapped held with slot {index}");
        }

        // Minimal UI shader + buffer helpers (handy if you don't already have them)
        private int CreateSimpleUIShader()
        {
            string vs = @"#version 330 core
            layout(location=0) in vec2 aPos;
            layout(location=1) in vec4 aColor;
            uniform mat4 projection;
            out vec4 vColor;
            void main() {
                vColor = aColor;
                gl_Position = projection * vec4(aPos, 0.0, 1.0);
            }";

            string fs = @"#version 330 core
            in vec4 vColor;
            out vec4 FragColor;
            void main(){ FragColor = vColor; }";

            int vsId = GL.CreateShader(ShaderType.VertexShader);
            GL.ShaderSource(vsId, vs);
            GL.CompileShader(vsId);
            CheckShaderCompile(vsId, "UI vertex shader");

            int fsId = GL.CreateShader(ShaderType.FragmentShader);
            GL.ShaderSource(fsId, fs);
            GL.CompileShader(fsId);
            CheckShaderCompile(fsId, "UI fragment shader");

            int program = GL.CreateProgram();
            GL.AttachShader(program, vsId);
            GL.AttachShader(program, fsId);
            GL.LinkProgram(program);

            GL.DeleteShader(vsId);
            GL.DeleteShader(fsId);

            return program;
        }

        private int CreateQuadVao()
        {
            int vao = GL.GenVertexArray();
            GL.BindVertexArray(vao);

            // Setup attributes layout (pos: vec2, color: vec4)
            int vbo = GL.GenBuffer();
            GL.BindBuffer(BufferTarget.ArrayBuffer, vbo);
            GL.EnableVertexAttribArray(0);
            GL.VertexAttribPointer(0, 2, VertexAttribPointerType.Float, false, 6 * sizeof(float), 0);
            GL.EnableVertexAttribArray(1);
            GL.VertexAttribPointer(1, 4, VertexAttribPointerType.Float, false, 6 * sizeof(float), 2 * sizeof(float));

            GL.BindVertexArray(0);
            GL.BindBuffer(BufferTarget.ArrayBuffer, 0);
            return vao;
        }

        private int CreateQuadVbo()
        {
            int vbo = GL.GenBuffer();
            return vbo;
        }

        private void CheckShaderCompile(int shader, string name)
        {
            GL.GetShader(shader, ShaderParameter.CompileStatus, out int status);
            if (status == (int)All.False)
            {
                string log = GL.GetShaderInfoLog(shader);
                Console.WriteLine($"[Shader] Error compiling {name}: {log}");
            }
        }

        // ---------------------------
        // Logging helper tee writer
        // ---------------------------
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
