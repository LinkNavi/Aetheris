// Net/Client/Game/Player/BlockPlacementSystem.cs - Block placement for 3D block models
using System;
using OpenTK.Mathematics;
using OpenTK.Windowing.GraphicsLibraryFramework;
using OpenTK.Graphics.OpenGL4;

namespace Aetheris
{
    public class BlockPlacementSystem
    {
        private readonly Player player;
        private readonly Game game;
        private readonly Client client;
        private readonly RaycastHelper raycaster;
        
        private const float MAX_REACH = 10f;
        
        private float placeCooldown = 0f;
        private const float PLACE_COOLDOWN_TIME = 0.15f;
        
        public BlockPlacementSystem(Player player, Game game, Client client)
        {
            this.player = player ?? throw new ArgumentNullException(nameof(player));
            this.game = game ?? throw new ArgumentNullException(nameof(game));
            this.client = client;
            this.raycaster = new RaycastHelper(game);
        }
        
        public void Update(float deltaTime, MouseState mouse, bool isWindowFocused)
        {
            if (!isWindowFocused) return;
            
            placeCooldown = Math.Max(0f, placeCooldown - deltaTime);
            
            // Right click to place block
            if (mouse.IsButtonPressed(MouseButton.Right) && placeCooldown <= 0f)
            {
                TryPlaceBlock();
            }
        }
        
        private void TryPlaceBlock()
        {
            // Get selected item
            var selectedItem = player.Inventory.GetSelectedItem();
            if (selectedItem.ItemId == 0) return;
            
            // Check if item can place blocks
            var itemDef = ItemRegistry.Get(selectedItem.ItemId);
            if (itemDef == null || !itemDef.PlacesBlock.HasValue) return;
            
            // Store block type from ITEM
            var blockTypeToPlace = itemDef.PlacesBlock.Value;
            
            // Raycast to find placement position
            Vector3 rayStart = player.GetEyePosition();
            Vector3 rayDir = player.GetForward().Normalized();
            Vector3 rayEnd = rayStart + rayDir * MAX_REACH;
            
            var hits = raycaster.Raycast(rayStart, rayEnd, raycastAll: false);
            
            if (hits.Length == 0 || !hits[0].Hit) return;
            
            var hit = hits[0];
            
            // Calculate placement position (offset from hit surface)
            Vector3 placePos = hit.Point + hit.Normal * 0.5f;
            
            // Round to nearest block position
            int x = (int)Math.Round(placePos.X);
            int y = (int)Math.Round(placePos.Y);
            int z = (int)Math.Round(placePos.Z);
            
            // Check if player would intersect with placed block
            if (WouldIntersectPlayer(x, y, z))
            {
                Console.WriteLine("[BlockPlace] Cannot place block - would intersect player");
                return;
            }
            
            // Check if block already exists at this position
            if (game.PlacedBlocks.HasBlockAt(x, y, z))
            {
                Console.WriteLine("[BlockPlace] Block already exists at this position");
                return;
            }
            
            // Place block model
            PlaceBlockAt(x, y, z, blockTypeToPlace);
            
            // Consume item
            if (player.Inventory.RemoveItem(selectedItem.ItemId, 1))
            {
                Console.WriteLine($"[BlockPlace] Placed {itemDef.Name} ({blockTypeToPlace}) at ({x}, {y}, {z})");
            }
            
            placeCooldown = PLACE_COOLDOWN_TIME;
        }
        
        private bool WouldIntersectPlayer(int x, int y, int z)
        {
            // Check if block would be inside player's collision box
            Vector3 blockPos = new Vector3(x, y, z);
            Vector3 playerPos = player.Position;
            
            // Player dimensions (from Player.cs)
            const float PLAYER_WIDTH = 1.2f;
            const float PLAYER_HEIGHT = 3.6f;
            
            // Check distance
            float dx = Math.Abs(blockPos.X - playerPos.X);
            float dy = Math.Abs(blockPos.Y - playerPos.Y);
            float dz = Math.Abs(blockPos.Z - playerPos.Z);
            
            if (dx < (PLAYER_WIDTH / 2f + 0.5f) &&
                dy < (PLAYER_HEIGHT / 2f + 0.5f) &&
                dz < (PLAYER_WIDTH / 2f + 0.5f))
            {
                return true;
            }
            
            return false;
        }
        
        private void PlaceBlockAt(int x, int y, int z, AetherisClient.Rendering.BlockType clientBlockType)
        {
            // CHANGED: Place actual 3D block model instead of modifying WorldGen
            bool placed = game.PlacedBlocks.PlaceBlock(x, y, z, clientBlockType);
            
            if (!placed)
            {
                Console.WriteLine($"[BlockPlace] Block already exists at ({x}, {y}, {z})");
                return;
            }
            
            // Convert block type to byte for network protocol
            byte blockTypeByte = (byte)clientBlockType;
            
            // Send to server
            if (client != null)
            {
                _ = SendBlockPlacement(x, y, z, blockTypeByte);
            }
            
            Console.WriteLine($"[BlockPlace] Placed {clientBlockType} block model at ({x}, {y}, {z})");
        }
        
        private async System.Threading.Tasks.Task SendBlockPlacement(int x, int y, int z, byte blockTypeByte)
        {
            await client.SendBlockPlaceAsync(x, y, z, blockTypeByte);
        }
        
        /// <summary>
        /// Get preview of where block would be placed
        /// </summary>
        public (bool canPlace, Vector3 position, Vector3 normal) GetPlacementPreview()
        {
            Vector3 rayStart = player.GetEyePosition();
            Vector3 rayDir = player.GetForward().Normalized();
            Vector3 rayEnd = rayStart + rayDir * MAX_REACH;
            
            var hits = raycaster.Raycast(rayStart, rayEnd, raycastAll: false);
            
            if (hits.Length == 0 || !hits[0].Hit)
                return (false, Vector3.Zero, Vector3.Zero);
            
            var hit = hits[0];
            Vector3 placePos = hit.Point + hit.Normal * 0.5f;
            
            int x = (int)Math.Round(placePos.X);
            int y = (int)Math.Round(placePos.Y);
            int z = (int)Math.Round(placePos.Z);
            
            bool canPlace = !WouldIntersectPlayer(x, y, z) && !game.PlacedBlocks.HasBlockAt(x, y, z);
            
            return (canPlace, new Vector3(x, y, z), hit.Normal);
        }
    }
    
    /// <summary>
    /// Visual indicator for block placement preview
    /// </summary>
    public class BlockPlacementPreview : IDisposable
    {
        private int vao, vbo;
        private int shaderProgram;
        
        public BlockPlacementPreview()
        {
            InitializePreviewMesh();
            InitializeShader();
        }
        
        private void InitializePreviewMesh()
        {
            // Create wireframe cube
            float size = 0.5f;
            float[] vertices = {
                // 8 corners of cube
                -size, -size, -size,
                 size, -size, -size,
                 size,  size, -size,
                -size,  size, -size,
                -size, -size,  size,
                 size, -size,  size,
                 size,  size,  size,
                -size,  size,  size
            };
            
            uint[] indices = {
                0, 1, 1, 2, 2, 3, 3, 0,
                4, 5, 5, 6, 6, 7, 7, 4,
                0, 4, 1, 5, 2, 6, 3, 7
            };
            
            vao = GL.GenVertexArray();
            vbo = GL.GenBuffer();
            int ebo = GL.GenBuffer();
            
            GL.BindVertexArray(vao);
            
            GL.BindBuffer(BufferTarget.ArrayBuffer, vbo);
            GL.BufferData(BufferTarget.ArrayBuffer, vertices.Length * sizeof(float), 
                vertices, BufferUsageHint.StaticDraw);
            
            GL.BindBuffer(BufferTarget.ElementArrayBuffer, ebo);
            GL.BufferData(BufferTarget.ElementArrayBuffer, indices.Length * sizeof(uint), 
                indices, BufferUsageHint.StaticDraw);
            
            GL.EnableVertexAttribArray(0);
            GL.VertexAttribPointer(0, 3, VertexAttribPointerType.Float, false, 3 * sizeof(float), 0);
            
            GL.BindVertexArray(0);
        }
        
        private void InitializeShader()
        {
            string vertexShader = @"
#version 330 core
layout (location = 0) in vec3 aPos;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;

void main()
{
    gl_Position = uProjection * uView * uModel * vec4(aPos, 1.0);
}";

            string fragmentShader = @"
#version 330 core
out vec4 FragColor;

uniform vec4 uColor;

void main()
{
    FragColor = uColor;
}";

            int vs = CompileShader(ShaderType.VertexShader, vertexShader);
            int fs = CompileShader(ShaderType.FragmentShader, fragmentShader);

            shaderProgram = GL.CreateProgram();
            GL.AttachShader(shaderProgram, vs);
            GL.AttachShader(shaderProgram, fs);
            GL.LinkProgram(shaderProgram);

            GL.DeleteShader(vs);
            GL.DeleteShader(fs);
        }
        
        public void Render(Vector3 position, bool canPlace, Matrix4 view, Matrix4 projection)
        {
            GL.UseProgram(shaderProgram);
            
            var model = Matrix4.CreateTranslation(position);
            
            GL.UniformMatrix4(GL.GetUniformLocation(shaderProgram, "uModel"), false, ref model);
            GL.UniformMatrix4(GL.GetUniformLocation(shaderProgram, "uView"), false, ref view);
            GL.UniformMatrix4(GL.GetUniformLocation(shaderProgram, "uProjection"), false, ref projection);
            
            Vector4 color = canPlace 
                ? new Vector4(0f, 1f, 0f, 0.5f) 
                : new Vector4(1f, 0f, 0f, 0.5f);
            GL.Uniform4(GL.GetUniformLocation(shaderProgram, "uColor"), ref color);
            
            GL.Disable(EnableCap.DepthTest);
            GL.Enable(EnableCap.Blend);
            GL.BlendFunc(BlendingFactor.SrcAlpha, BlendingFactor.OneMinusSrcAlpha);
            
            GL.BindVertexArray(vao);
            GL.DrawElements(PrimitiveType.Lines, 24, DrawElementsType.UnsignedInt, IntPtr.Zero);
            GL.BindVertexArray(0);
            
            GL.Enable(EnableCap.DepthTest);
        }
        
        private int CompileShader(ShaderType type, string source)
        {
            int shader = GL.CreateShader(type);
            GL.ShaderSource(shader, source);
            GL.CompileShader(shader);
            
            GL.GetShader(shader, ShaderParameter.CompileStatus, out int success);
            if (success == 0)
            {
                string info = GL.GetShaderInfoLog(shader);
                Console.WriteLine($"[BlockPreview] Shader compilation failed: {info}");
            }
            
            return shader;
        }
        
        public void Dispose()
        {
            GL.DeleteVertexArray(vao);
            GL.DeleteBuffer(vbo);
            GL.DeleteProgram(shaderProgram);
        }
    }
}
