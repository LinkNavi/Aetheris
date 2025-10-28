using System;
using OpenTK.Graphics.OpenGL4;
using OpenTK.Mathematics;
using OpenTK.Windowing.GraphicsLibraryFramework;

namespace Aetheris
{
    /// <summary>
    /// Handles rendering and interaction for hotbar and inventory UI
    /// </summary>
    public class InventoryUI : IDisposable
    {
        private readonly Inventory inventory;
        private bool isInventoryOpen = false;
        
        // UI state
        private int hoveredSlot = -1;
        private int draggedSlot = -1;
        private bool wasMousePressed = false;

        // Shader and rendering
        private int shaderProgram;
        private int vao;
        private int vbo;
        
        // Hotbar dimensions
        private const float HOTBAR_SLOT_SIZE = 50f;
        private const float HOTBAR_SPACING = 4f;
        private const float HOTBAR_Y_OFFSET = 20f;
        
        // Inventory dimensions
        private const float INV_SLOT_SIZE = 45f;
        private const float INV_SPACING = 4f;
        private const float INV_Y_START = 100f;

        public InventoryUI(Inventory inventory)
        {
            this.inventory = inventory ?? throw new ArgumentNullException(nameof(inventory));
            InitializeShaders();
            InitializeBuffers();
        }

        private void InitializeShaders()
        {
            string vertexShader = @"
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoord;
layout (location = 2) in vec4 aColor;

out vec2 TexCoord;
out vec4 Color;

uniform mat4 projection;

void main()
{
    gl_Position = projection * vec4(aPos, 0.0, 1.0);
    TexCoord = aTexCoord;
    Color = aColor;
}";

            string fragmentShader = @"
#version 330 core
in vec2 TexCoord;
in vec4 Color;

out vec4 FragColor;

void main()
{
    FragColor = Color;
}";

            int vs = GL.CreateShader(ShaderType.VertexShader);
            GL.ShaderSource(vs, vertexShader);
            GL.CompileShader(vs);
            CheckShaderCompilation(vs, "VERTEX");

            int fs = GL.CreateShader(ShaderType.FragmentShader);
            GL.ShaderSource(fs, fragmentShader);
            GL.CompileShader(fs);
            CheckShaderCompilation(fs, "FRAGMENT");

            shaderProgram = GL.CreateProgram();
            GL.AttachShader(shaderProgram, vs);
            GL.AttachShader(shaderProgram, fs);
            GL.LinkProgram(shaderProgram);
            CheckProgramLinking(shaderProgram);

            GL.DeleteShader(vs);
            GL.DeleteShader(fs);
        }

        private void InitializeBuffers()
        {
            vao = GL.GenVertexArray();
            vbo = GL.GenBuffer();

            GL.BindVertexArray(vao);
            GL.BindBuffer(BufferTarget.ArrayBuffer, vbo);

            // Position (2 floats) + TexCoord (2 floats) + Color (4 floats) = 8 floats per vertex
            int stride = 8 * sizeof(float);
            
            GL.VertexAttribPointer(0, 2, VertexAttribPointerType.Float, false, stride, 0);
            GL.EnableVertexAttribArray(0);
            
            GL.VertexAttribPointer(1, 2, VertexAttribPointerType.Float, false, stride, 2 * sizeof(float));
            GL.EnableVertexAttribArray(1);
            
            GL.VertexAttribPointer(2, 4, VertexAttribPointerType.Float, false, stride, 4 * sizeof(float));
            GL.EnableVertexAttribArray(2);

            GL.BindVertexArray(0);
        }

        public void Update(KeyboardState keyboard, MouseState mouse, Vector2i windowSize)
        {
            // Toggle inventory with 'E' key
            if (keyboard.IsKeyPressed(Keys.E))
            {
                isInventoryOpen = !isInventoryOpen;
                Console.WriteLine($"[InventoryUI] Inventory {(isInventoryOpen ? "opened" : "closed")}");
            }

            // Hotbar selection with number keys
            if (!isInventoryOpen)
            {
                for (int i = 0; i < Inventory.HOTBAR_SIZE; i++)
                {
                    Keys key = Keys.D1 + i;
                    if (keyboard.IsKeyPressed(key))
                    {
                        inventory.SelectedHotbarSlot = i;
                        Console.WriteLine($"[InventoryUI] Selected hotbar slot {i}");
                    }
                }

                // Mouse wheel for hotbar selection
                float scroll = mouse.ScrollDelta.Y;
                if (Math.Abs(scroll) > 0.1f)
                {
                    inventory.SelectedHotbarSlot -= (int)Math.Sign(scroll);
                    if (inventory.SelectedHotbarSlot < 0)
                        inventory.SelectedHotbarSlot = Inventory.HOTBAR_SIZE - 1;
                    if (inventory.SelectedHotbarSlot >= Inventory.HOTBAR_SIZE)
                        inventory.SelectedHotbarSlot = 0;
                }
            }

            // Handle inventory interactions
            if (isInventoryOpen)
            {
                UpdateInventoryInteraction(mouse, windowSize);
            }
        }

        private void UpdateInventoryInteraction(MouseState mouse, Vector2i windowSize)
        {
            Vector2 mousePos = new Vector2(mouse.X, windowSize.Y - mouse.Y); // Flip Y
            hoveredSlot = GetSlotAtPosition(mousePos, windowSize);

            bool isMousePressed = mouse.IsButtonDown(MouseButton.Left);

            // Handle slot clicking and dragging
            if (isMousePressed && !wasMousePressed && hoveredSlot >= 0)
            {
                draggedSlot = hoveredSlot;
            }
            else if (!isMousePressed && wasMousePressed && draggedSlot >= 0)
            {
                // Released mouse - swap slots
                if (hoveredSlot >= 0 && hoveredSlot != draggedSlot)
                {
                    SwapSlots(draggedSlot, hoveredSlot);
                }
                draggedSlot = -1;
            }

            wasMousePressed = isMousePressed;
        }

        private int GetSlotAtPosition(Vector2 mousePos, Vector2i windowSize)
        {
            float centerX = windowSize.X / 2f;
            
            // Check hotbar
            float hotbarWidth = Inventory.HOTBAR_SIZE * (HOTBAR_SLOT_SIZE + HOTBAR_SPACING) - HOTBAR_SPACING;
            float hotbarStartX = centerX - hotbarWidth / 2f;
            
            for (int i = 0; i < Inventory.HOTBAR_SIZE; i++)
            {
                float x = hotbarStartX + i * (HOTBAR_SLOT_SIZE + HOTBAR_SPACING);
                float y = HOTBAR_Y_OFFSET;
                
                if (IsPointInRect(mousePos, x, y, HOTBAR_SLOT_SIZE, HOTBAR_SLOT_SIZE))
                    return i;
            }

            // Check inventory (if open)
            if (isInventoryOpen)
            {
                float invWidth = 9 * (INV_SLOT_SIZE + INV_SPACING) - INV_SPACING;
                float invStartX = centerX - invWidth / 2f;
                
                for (int row = 0; row < 3; row++)
                {
                    for (int col = 0; col < 9; col++)
                    {
                        int slot = Inventory.HOTBAR_SIZE + row * 9 + col;
                        float x = invStartX + col * (INV_SLOT_SIZE + INV_SPACING);
                        float y = INV_Y_START + row * (INV_SLOT_SIZE + INV_SPACING);
                        
                        if (IsPointInRect(mousePos, x, y, INV_SLOT_SIZE, INV_SLOT_SIZE))
                            return slot;
                    }
                }
            }

            return -1;
        }

        private bool IsPointInRect(Vector2 point, float x, float y, float w, float h)
        {
            return point.X >= x && point.X <= x + w && point.Y >= y && point.Y <= y + h;
        }

        private void SwapSlots(int slotA, int slotB)
        {
            var temp = inventory.Slots[slotA];
            inventory.Slots[slotA] = inventory.Slots[slotB];
            inventory.Slots[slotB] = temp;
            Console.WriteLine($"[InventoryUI] Swapped slots {slotA} and {slotB}");
        }

        public void Render(Vector2i windowSize)
        {
            // Save state
            bool depthTestEnabled = GL.IsEnabled(EnableCap.DepthTest);
            bool blendEnabled = GL.IsEnabled(EnableCap.Blend);
            
            GL.Disable(EnableCap.DepthTest);
            GL.Enable(EnableCap.Blend);
            GL.BlendFunc(BlendingFactor.SrcAlpha, BlendingFactor.OneMinusSrcAlpha);

            GL.UseProgram(shaderProgram);
            
            // Orthographic projection
            var projection = Matrix4.CreateOrthographicOffCenter(0, windowSize.X, 0, windowSize.Y, -1, 1);
            int projLoc = GL.GetUniformLocation(shaderProgram, "projection");
            GL.UniformMatrix4(projLoc, false, ref projection);

            GL.BindVertexArray(vao);

            // Render hotbar
            RenderHotbar(windowSize);

            // Render inventory if open
            if (isInventoryOpen)
            {
                RenderInventory(windowSize);
            }

            GL.BindVertexArray(0);
            GL.UseProgram(0);

            // Restore state
            if (depthTestEnabled) GL.Enable(EnableCap.DepthTest);
            if (!blendEnabled) GL.Disable(EnableCap.Blend);
        }

        private void RenderHotbar(Vector2i windowSize)
        {
            float centerX = windowSize.X / 2f;
            float hotbarWidth = Inventory.HOTBAR_SIZE * (HOTBAR_SLOT_SIZE + HOTBAR_SPACING) - HOTBAR_SPACING;
            float startX = centerX - hotbarWidth / 2f;

            for (int i = 0; i < Inventory.HOTBAR_SIZE; i++)
            {
                float x = startX + i * (HOTBAR_SLOT_SIZE + HOTBAR_SPACING);
                float y = HOTBAR_Y_OFFSET;
                
                bool isSelected = i == inventory.SelectedHotbarSlot;
                bool isHovered = hoveredSlot == i;
                
                RenderSlot(x, y, HOTBAR_SLOT_SIZE, inventory.Slots[i], isSelected, isHovered);
            }
        }

        private void RenderInventory(Vector2i windowSize)
        {
            float centerX = windowSize.X / 2f;
            float invWidth = 9 * (INV_SLOT_SIZE + INV_SPACING) - INV_SPACING;
            float startX = centerX - invWidth / 2f;

            // Draw background panel
            float panelWidth = invWidth + 20f;
            float panelHeight = 3 * (INV_SLOT_SIZE + INV_SPACING) + 40f;
            DrawRect(startX - 10f, INV_Y_START - 10f, panelWidth, panelHeight, 
                     new Vector4(0.1f, 0.1f, 0.1f, 0.9f));

            // Draw inventory slots
            for (int row = 0; row < 3; row++)
            {
                for (int col = 0; col < 9; col++)
                {
                    int slot = Inventory.HOTBAR_SIZE + row * 9 + col;
                    float x = startX + col * (INV_SLOT_SIZE + INV_SPACING);
                    float y = INV_Y_START + row * (INV_SLOT_SIZE + INV_SPACING);
                    
                    bool isHovered = hoveredSlot == slot;
                    RenderSlot(x, y, INV_SLOT_SIZE, inventory.Slots[slot], false, isHovered);
                }
            }
        }

        private void RenderSlot(float x, float y, float size, ItemStack item, bool selected, bool hovered)
        {
            // Background
            Vector4 bgColor = new Vector4(0.2f, 0.2f, 0.2f, 0.8f);
            if (selected) bgColor = new Vector4(0.4f, 0.4f, 0.5f, 0.9f);
            if (hovered) bgColor += new Vector4(0.1f, 0.1f, 0.1f, 0f);
            
            DrawRect(x, y, size, size, bgColor);
            
            // Border
            Vector4 borderColor = selected ? new Vector4(1f, 1f, 1f, 1f) : new Vector4(0.5f, 0.5f, 0.5f, 0.8f);
            DrawRectOutline(x, y, size, size, 2f, borderColor);
            
            // Item representation (simple colored square for now)
            if (item.ItemId > 0)
            {
                Vector4 itemColor = GetItemColor(item.ItemId);
                float itemSize = size * 0.6f;
                float itemX = x + (size - itemSize) / 2f;
                float itemY = y + (size - itemSize) / 2f;
                DrawRect(itemX, itemY, itemSize, itemSize, itemColor);
                
                // Draw count if > 1
                if (item.Count > 1)
                {
                    // Note: Text rendering would require a proper font system
                    // For now, just draw a small indicator
                    DrawRect(x + size - 8f, y + 2f, 6f, 6f, new Vector4(1f, 1f, 0f, 1f));
                }
            }
        }

        private Vector4 GetItemColor(int itemId)
        {
            // Map item IDs to colors (expand this based on your item types)
            return itemId switch
            {
                1 => new Vector4(0.5f, 0.5f, 0.5f, 1f),  // Stone
                2 => new Vector4(0.6f, 0.4f, 0.2f, 1f),  // Dirt
                3 => new Vector4(0.3f, 0.8f, 0.3f, 1f),  // Grass
                4 => new Vector4(0.9f, 0.9f, 0.6f, 1f),  // Sand
                5 => new Vector4(1.0f, 1.0f, 1.0f, 1f),  // Snow
                _ => new Vector4(0.8f, 0.3f, 0.8f, 1f),  // Unknown
            };
        }

        private void DrawRect(float x, float y, float w, float h, Vector4 color)
        {
            float[] vertices = new float[]
            {
                // Pos          // TexCoord  // Color
                x,     y,       0, 0,        color.X, color.Y, color.Z, color.W,
                x + w, y,       1, 0,        color.X, color.Y, color.Z, color.W,
                x + w, y + h,   1, 1,        color.X, color.Y, color.Z, color.W,
                
                x,     y,       0, 0,        color.X, color.Y, color.Z, color.W,
                x + w, y + h,   1, 1,        color.X, color.Y, color.Z, color.W,
                x,     y + h,   0, 1,        color.X, color.Y, color.Z, color.W,
            };

            GL.BindBuffer(BufferTarget.ArrayBuffer, vbo);
            GL.BufferData(BufferTarget.ArrayBuffer, vertices.Length * sizeof(float), vertices, BufferUsageHint.DynamicDraw);
            GL.DrawArrays(PrimitiveType.Triangles, 0, 6);
        }

        private void DrawRectOutline(float x, float y, float w, float h, float thickness, Vector4 color)
        {
            // Top
            DrawRect(x, y + h - thickness, w, thickness, color);
            // Bottom
            DrawRect(x, y, w, thickness, color);
            // Left
            DrawRect(x, y, thickness, h, color);
            // Right
            DrawRect(x + w - thickness, y, thickness, h, color);
        }

        public bool IsInventoryOpen() => isInventoryOpen;

        private void CheckShaderCompilation(int shader, string type)
        {
            GL.GetShader(shader, ShaderParameter.CompileStatus, out int success);
            if (success == 0)
            {
                string info = GL.GetShaderInfoLog(shader);
                Console.WriteLine($"[InventoryUI] {type} shader compilation failed: {info}");
            }
        }

        private void CheckProgramLinking(int program)
        {
            GL.GetProgram(program, GetProgramParameterName.LinkStatus, out int success);
            if (success == 0)
            {
                string info = GL.GetProgramInfoLog(program);
                Console.WriteLine($"[InventoryUI] Shader program linking failed: {info}");
            }
        }

        public void Dispose()
        {
            GL.DeleteVertexArray(vao);
            GL.DeleteBuffer(vbo);
            GL.DeleteProgram(shaderProgram);
        }
    }
}
