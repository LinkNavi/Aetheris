// UI/OpenGLInventoryUI.cs - Full OpenGL inventory with drag-and-drop and animations
using System;
using System.Collections.Generic;
using OpenTK.Graphics.OpenGL4;
using OpenTK.Mathematics;
using OpenTK.Windowing.GraphicsLibraryFramework;
using Aetheris.GameLogic;
using Aetheris.UI;

namespace Aetheris.UI
{
    public class OpenGLInventoryUI : IDisposable
    {
        private readonly Inventory inventory;
        private readonly FontRenderer? fontRenderer;
        private int shaderProgram;
        private int vao;
        private int vbo;

        private bool isOpen = false;
        private int hoveredSlot = -1;
        private int draggedSlot = -1;
        private ItemStack draggedItem = ItemStack.Empty;
        private Vector2i lastWindowSize = new Vector2i(1920, 1080);

        // Animation states
        private float openProgress = 0f;
        private float[] slotHighlightAnim = new float[100]; // Support up to 100 slots
        private float draggedItemAlpha = 1f;
        private Vector2 draggedItemPosition = Vector2.Zero;

        // Layout constants
        private const float WINDOW_WIDTH = 900f;
        private const float WINDOW_HEIGHT = 700f;
        private const float SLOT_SIZE = 70f;
        private const float SLOT_SPACING = 6f;
        private const float PADDING = 20f;
        private const float SECTION_SPACING = 30f;
        private const float CORNER_RADIUS = 8f;

        // Colors - Dark theme matching HUD
        private static readonly Vector4 BG_DARK = new Vector4(0.08f, 0.08f, 0.1f, 0.98f); // Increased alpha
        private static readonly Vector4 BG_MEDIUM = new Vector4(0.12f, 0.12f, 0.15f, 0.98f); // Increased alpha
        private static readonly Vector4 BORDER_COLOR = new Vector4(0.25f, 0.25f, 0.3f, 1f);
        private static readonly Vector4 ACCENT_COLOR = new Vector4(0.4f, 0.6f, 1f, 1f);
        private static readonly Vector4 SLOT_BG = new Vector4(0.15f, 0.15f, 0.18f, 0.98f); // Increased alpha
        private static readonly Vector4 SLOT_HOVER = new Vector4(0.2f, 0.2f, 0.25f, 0.98f); // Increased alpha
        private static readonly Vector4 SLOT_SELECTED = new Vector4(0.3f, 0.28f, 0.2f, 0.98f); // Increased alpha
        private static readonly Vector4 SECTION_TITLE_COLOR = new Vector4(0.7f, 0.8f, 1f, 1f);

        public OpenGLInventoryUI(Inventory inventory, FontRenderer? fontRenderer)
        {
            this.inventory = inventory ?? throw new ArgumentNullException(nameof(inventory));
            this.fontRenderer = fontRenderer;
            
            CreateShaderAndBuffers();
            Console.WriteLine("[OpenGLInventoryUI] Initialized");
        }

        private void CreateShaderAndBuffers()
        {
            string vertexShaderSource = @"
                #version 330 core
                layout (location = 0) in vec2 aPos;
                layout (location = 1) in vec4 aColor;
                out vec4 vertexColor;
                uniform mat4 projection;
                void main()
                {
                    gl_Position = projection * vec4(aPos, 0.0, 1.0);
                    vertexColor = aColor;
                }
            ";

            string fragmentShaderSource = @"
                #version 330 core
                in vec4 vertexColor;
                out vec4 FragColor;
                void main()
                {
                    FragColor = vertexColor;
                }
            ";

            int vertexShader = GL.CreateShader(ShaderType.VertexShader);
            GL.ShaderSource(vertexShader, vertexShaderSource);
            GL.CompileShader(vertexShader);
            
            GL.GetShader(vertexShader, ShaderParameter.CompileStatus, out int vsStatus);
            if (vsStatus == 0)
            {
                string error = GL.GetShaderInfoLog(vertexShader);
                Console.WriteLine($"[OpenGLInventoryUI] Vertex shader error: {error}");
            }

            int fragmentShader = GL.CreateShader(ShaderType.FragmentShader);
            GL.ShaderSource(fragmentShader, fragmentShaderSource);
            GL.CompileShader(fragmentShader);
            
            GL.GetShader(fragmentShader, ShaderParameter.CompileStatus, out int fsStatus);
            if (fsStatus == 0)
            {
                string error = GL.GetShaderInfoLog(fragmentShader);
                Console.WriteLine($"[OpenGLInventoryUI] Fragment shader error: {error}");
            }

            shaderProgram = GL.CreateProgram();
            GL.AttachShader(shaderProgram, vertexShader);
            GL.AttachShader(shaderProgram, fragmentShader);
            GL.LinkProgram(shaderProgram);
            
            GL.GetProgram(shaderProgram, GetProgramParameterName.LinkStatus, out int linkStatus);
            if (linkStatus == 0)
            {
                string error = GL.GetProgramInfoLog(shaderProgram);
                Console.WriteLine($"[OpenGLInventoryUI] Shader link error: {error}");
            }
            else
            {
                Console.WriteLine("[OpenGLInventoryUI] Shaders compiled and linked successfully");
            }

            GL.DeleteShader(vertexShader);
            GL.DeleteShader(fragmentShader);

            vao = GL.GenVertexArray();
            vbo = GL.GenBuffer();

            GL.BindVertexArray(vao);
            GL.BindBuffer(BufferTarget.ArrayBuffer, vbo);

            GL.VertexAttribPointer(0, 2, VertexAttribPointerType.Float, false, 6 * sizeof(float), 0);
            GL.EnableVertexAttribArray(0);

            GL.VertexAttribPointer(1, 4, VertexAttribPointerType.Float, false, 6 * sizeof(float), 2 * sizeof(float));
            GL.EnableVertexAttribArray(1);

            GL.BindVertexArray(0);
        }

        public void Update(KeyboardState keyboard, MouseState mouse, Vector2i windowSize, float deltaTime)
        {
            // Animate window open/close
            float targetProgress = isOpen ? 1f : 0f;
            
            // Instant close, smooth open
            if (!isOpen && openProgress > 0)
            {
                openProgress = 0f; // Instant close
            }
            else
            {
                openProgress = MathHelper.Lerp(openProgress, targetProgress, deltaTime * 12f);
            }

            if (!isOpen && openProgress < 0.01f)
                return;

            // Reset hover state
            hoveredSlot = -1;

            // Update slot animations
            for (int i = 0; i < slotHighlightAnim.Length; i++)
            {
                float target = (i == hoveredSlot || i == inventory.SelectedHotbarSlot) ? 1f : 0f;
                slotHighlightAnim[i] = MathHelper.Lerp(slotHighlightAnim[i], target, deltaTime * 15f);
            }

            // Calculate window position
            float windowX = (windowSize.X - WINDOW_WIDTH) / 2;
            float windowY = (windowSize.Y - WINDOW_HEIGHT) / 2;

            // Scale animation effect
            float scale = 0.9f + openProgress * 0.1f;
            windowX += WINDOW_WIDTH * (1f - scale) / 2f;
            windowY += WINDOW_HEIGHT * (1f - scale) / 2f;

            Vector2 mousePos = new Vector2(mouse.X, mouse.Y);

            // Check hover and clicks
            if (isOpen)
            {
                CheckSlotInteractions(mousePos, windowX, windowY, mouse);
            }

            // Update dragged item position
            if (draggedSlot != -1)
            {
                draggedItemPosition = mousePos;
                draggedItemAlpha = 0.7f + (float)Math.Sin(deltaTime * 10f) * 0.15f;
            }
        }

        private void CheckSlotInteractions(Vector2 mousePos, float windowX, float windowY, MouseState mouse)
        {
            float currentY = windowY + PADDING + 40; // After title

            // Hotbar section
            currentY += 30; // Section title
            CheckSectionSlots(mousePos, windowX + PADDING, currentY, 0, Inventory.HOTBAR_SIZE, 9, mouse);
            currentY += GetSectionHeight(Inventory.HOTBAR_SIZE, 9) + SECTION_SPACING;

            // Main inventory
            currentY += 30;
            CheckSectionSlots(mousePos, windowX + PADDING, currentY, Inventory.HOTBAR_SIZE, Inventory.MAIN_SIZE, 9, mouse);
            currentY += GetSectionHeight(Inventory.MAIN_SIZE, 9) + SECTION_SPACING;

            // Armor and Totems (side by side)
            currentY += 30;
            float armorX = windowX + PADDING;
            float totemX = windowX + PADDING + 400; // Offset for second column

            CheckSectionSlots(mousePos, armorX, currentY, 
                Inventory.HOTBAR_SIZE + Inventory.MAIN_SIZE, Inventory.ARMOR_SIZE, 4, mouse);
            
            CheckSectionSlots(mousePos, totemX, currentY, 
                Inventory.HOTBAR_SIZE + Inventory.MAIN_SIZE + Inventory.ARMOR_SIZE, Inventory.TOTEM_SIZE, 5, mouse);
        }

        private void CheckSectionSlots(Vector2 mousePos, float startX, float startY, int slotStart, int slotCount, int columns, MouseState mouse)
        {
            for (int i = 0; i < slotCount; i++)
            {
                int col = i % columns;
                int row = i / columns;
                
                float slotX = startX + col * (SLOT_SIZE + SLOT_SPACING);
                float slotY = startY + row * (SLOT_SIZE + SLOT_SPACING);

                if (mousePos.X >= slotX && mousePos.X <= slotX + SLOT_SIZE &&
                    mousePos.Y >= slotY && mousePos.Y <= slotY + SLOT_SIZE)
                {
                    int slotIndex = slotStart + i;
                    hoveredSlot = slotIndex;

                    if (mouse.IsButtonPressed(MouseButton.Left))
                    {
                        HandleSlotClick(slotIndex);
                    }
                }
            }
        }

        private void HandleSlotClick(int slotIndex)
        {
            if (draggedSlot == -1)
            {
                // Start dragging
                var item = inventory.GetSlot(slotIndex);
                if (item.ItemId > 0)
                {
                    draggedSlot = slotIndex;
                    draggedItem = item;
                    Console.WriteLine($"[OpenGLInventoryUI] Started dragging from slot {slotIndex}");
                }
            }
            else
            {
                // Drop item
                if (draggedSlot == slotIndex)
                {
                    // Same slot - cancel
                    draggedSlot = -1;
                    draggedItem = ItemStack.Empty;
                    Console.WriteLine($"[OpenGLInventoryUI] Cancelled drag");
                }
                else
                {
                    // Swap items
                    var targetItem = inventory.GetSlot(slotIndex);
                    inventory.SetSlot(slotIndex, draggedItem);
                    inventory.SetSlot(draggedSlot, targetItem);
                    
                    Console.WriteLine($"[OpenGLInventoryUI] Swapped: slot {draggedSlot} <-> slot {slotIndex}");
                    
                    draggedSlot = -1;
                    draggedItem = ItemStack.Empty;
                }
            }
        }

        private float GetSectionHeight(int slotCount, int columns)
        {
            int rows = (int)Math.Ceiling((float)slotCount / columns);
            return rows * (SLOT_SIZE + SLOT_SPACING) - SLOT_SPACING;
        }

        public void Render(Vector2i windowSize)
        {
            if (openProgress < 0.01f)
            {
                // Successfully skipped rendering (closed)
                return;
            }

            Console.WriteLine($"[OpenGLInventoryUI] Rendering with openProgress={openProgress:F3}, isOpen={isOpen}");

            // Store window size for use in draw functions
            lastWindowSize = windowSize;

            // Save GL state
            GL.GetInteger(GetPName.Blend, out int blendEnabled);
            GL.GetInteger(GetPName.DepthTest, out int depthEnabled);
            GL.GetInteger(GetPName.CurrentProgram, out int oldProgram);

            // CRITICAL: Force correct GL state for 2D rendering
            GL.Enable(EnableCap.Blend);
            GL.BlendFunc(BlendingFactor.SrcAlpha, BlendingFactor.OneMinusSrcAlpha);
            GL.Disable(EnableCap.DepthTest);
            GL.DepthMask(false); // Don't write to depth buffer
            GL.Disable(EnableCap.CullFace); // Don't cull faces for 2D

            var projection = Matrix4.CreateOrthographicOffCenter(0, windowSize.X, windowSize.Y, 0, -1, 1);

            GL.UseProgram(shaderProgram);
            int projLoc = GL.GetUniformLocation(shaderProgram, "projection");
            GL.UniformMatrix4(projLoc, false, ref projection);

            GL.BindVertexArray(vao);

            // Set font projection
            if (fontRenderer != null)
            {
                fontRenderer.SetProjection(projection);
            }

            // Calculate window position with animation
            float windowX = (windowSize.X - WINDOW_WIDTH) / 2;
            float windowY = (windowSize.Y - WINDOW_HEIGHT) / 2;

            float scale = 0.9f + openProgress * 0.1f;
            windowX += WINDOW_WIDTH * (1f - scale) / 2f;
            windowY += WINDOW_HEIGHT * (1f - scale) / 2f;

            // Semi-transparent background overlay (DISABLED - blocks tooltips)
            // Vector4 overlayColor = new Vector4(0, 0, 0, 0.6f * openProgress);
            // DrawRect(0, 0, windowSize.X, windowSize.Y, overlayColor);

            // Main window background - draw multiple layers for visibility
            Vector4 bgColor = BG_DARK;
            bgColor.W *= openProgress;
            
            // Draw a solid backing first
            Vector4 solidBg = new Vector4(0.05f, 0.05f, 0.08f, 1f * openProgress);
            DrawRect(windowX, windowY, WINDOW_WIDTH, WINDOW_HEIGHT, solidBg);
            
            // Then the styled background
            DrawRoundedRect(windowX, windowY, WINDOW_WIDTH, WINDOW_HEIGHT, 12f, bgColor);

            // Window border
            Vector4 borderColor = BORDER_COLOR;
            borderColor.W *= openProgress;
            DrawRoundedRectOutline(windowX, windowY, WINDOW_WIDTH, WINDOW_HEIGHT, 12f, 3f, borderColor);

            // Title bar
            if (fontRenderer != null)
            {
                Vector4 titleColor = new Vector4(1f, 1f, 1f, openProgress);
                fontRenderer.DrawText("INVENTORY", new Vector2(windowX + PADDING, windowY + PADDING), 0.6f, titleColor);
                
                // Close hint
                Vector4 hintColor = new Vector4(0.6f, 0.6f, 0.7f, openProgress);
                string hint = "Press E to close";
                Vector2 hintSize = fontRenderer.MeasureText(hint, 0.35f);
                fontRenderer.DrawText(hint, 
                    new Vector2(windowX + WINDOW_WIDTH - hintSize.X - PADDING, windowY + PADDING + 5), 
                    0.35f, hintColor);
            }

            float currentY = windowY + PADDING + 40; // After title

            // Render sections
            currentY = RenderHotbarSection(windowX, currentY);
            currentY = RenderMainInventorySection(windowX, currentY);
            RenderArmorAndTotemsSection(windowX, currentY);

            // Render dragged item (on top of everything)
            if (draggedSlot != -1 && draggedItem.ItemId > 0)
            {
                RenderDraggedItem();
            }

            GL.BindVertexArray(0);
            GL.UseProgram(oldProgram);

            // Restore GL state
            if (blendEnabled == 0) GL.Disable(EnableCap.Blend);
            if (depthEnabled != 0) GL.Enable(EnableCap.DepthTest);
            GL.DepthMask(true); // Restore depth writes
        }

        private float RenderHotbarSection(float windowX, float currentY)
        {
            // Section title
            if (fontRenderer != null)
            {
                Vector4 titleColor = SECTION_TITLE_COLOR;
                titleColor.W *= openProgress;
                fontRenderer.DrawText("HOTBAR", new Vector2(windowX + PADDING, currentY), 0.45f, titleColor);
            }
            currentY += 30;

            // IMPORTANT: Reset shader and projection after font rendering
            GL.UseProgram(shaderProgram);
            
            // Render slots
            RenderSlotGrid(windowX + PADDING, currentY, 0, Inventory.HOTBAR_SIZE, 9, true);
            
            return currentY + GetSectionHeight(Inventory.HOTBAR_SIZE, 9) + SECTION_SPACING;
        }

        private float RenderMainInventorySection(float windowX, float currentY)
        {
            // Section title
            if (fontRenderer != null)
            {
                Vector4 titleColor = new Vector4(0.7f, 1f, 0.7f, openProgress);
                fontRenderer.DrawText("MAIN INVENTORY", new Vector2(windowX + PADDING, currentY), 0.45f, titleColor);
            }
            currentY += 30;

            // IMPORTANT: Reset shader after font rendering
            GL.UseProgram(shaderProgram);
            
            // Render slots
            RenderSlotGrid(windowX + PADDING, currentY, Inventory.HOTBAR_SIZE, Inventory.MAIN_SIZE, 9, false);
            
            return currentY + GetSectionHeight(Inventory.MAIN_SIZE, 9) + SECTION_SPACING;
        }

        private void RenderArmorAndTotemsSection(float windowX, float currentY)
        {
            float leftColumnX = windowX + PADDING;
            float rightColumnX = windowX + PADDING + 400;

            // Armor section
            if (fontRenderer != null)
            {
                Vector4 armorColor = new Vector4(0.6f, 0.8f, 1f, openProgress);
                fontRenderer.DrawText("ARMOR", new Vector2(leftColumnX, currentY), 0.45f, armorColor);
            }
            
            // IMPORTANT: Reset shader after font rendering
            GL.UseProgram(shaderProgram);
            
            string[] armorLabels = { "Head", "Chest", "Legs", "Feet" };
            RenderSlotGrid(leftColumnX, currentY + 30, 
                Inventory.HOTBAR_SIZE + Inventory.MAIN_SIZE, Inventory.ARMOR_SIZE, 4, false, armorLabels);

            // Totems section
            if (fontRenderer != null)
            {
                Vector4 totemColor = new Vector4(1f, 0.8f, 1f, openProgress);
                fontRenderer.DrawText("TOTEMS", new Vector2(rightColumnX, currentY), 0.45f, totemColor);
            }
            
            // IMPORTANT: Reset shader after font rendering
            GL.UseProgram(shaderProgram);
            
            string[] totemLabels = { "Totem 1", "Totem 2", "Totem 3", "Totem 4", "Totem 5" };
            RenderSlotGrid(rightColumnX, currentY + 30, 
                Inventory.HOTBAR_SIZE + Inventory.MAIN_SIZE + Inventory.ARMOR_SIZE, Inventory.TOTEM_SIZE, 5, false, totemLabels);
        }

        private void RenderSlotGrid(float startX, float startY, int slotStart, int slotCount, int columns, bool showNumbers, string[]? labels = null)
        {
            for (int i = 0; i < slotCount; i++)
            {
                int col = i % columns;
                int row = i / columns;
                
                float slotX = startX + col * (SLOT_SIZE + SLOT_SPACING);
                float slotY = startY + row * (SLOT_SIZE + SLOT_SPACING);

                int slotIndex = slotStart + i;
                RenderSlot(slotX, slotY, slotIndex, showNumbers ? (i + 1) : -1, labels != null && i < labels.Length ? labels[i] : null);
            }
        }

        private void RenderSlot(float x, float y, int slotIndex, int number = -1, string? label = null)
        {
            var item = inventory.GetSlot(slotIndex);
            bool isHovered = slotIndex == hoveredSlot;
            bool isSelected = slotIndex == inventory.SelectedHotbarSlot && number > 0;
            bool isDragging = slotIndex == draggedSlot;
            
            float highlightAnim = slotIndex < slotHighlightAnim.Length ? slotHighlightAnim[slotIndex] : 0f;

            // Glow effect when hovered/selected
            if (highlightAnim > 0.01f)
            {
                float glowSize = 4f * highlightAnim;
                Vector4 glowColor = ACCENT_COLOR;
                glowColor.W = 0.3f * highlightAnim * openProgress;
                DrawRoundedRect(x - glowSize, y - glowSize, 
                    SLOT_SIZE + glowSize * 2, SLOT_SIZE + glowSize * 2, 
                    CORNER_RADIUS + 2, glowColor);
            }

            // Slot background
            Vector4 bgColor = SLOT_BG;
            if (isSelected)
                bgColor = SLOT_SELECTED;
            else if (isHovered)
                bgColor = Vector4.Lerp(SLOT_BG, SLOT_HOVER, highlightAnim);
            
            if (isDragging)
                bgColor.W *= 0.5f;
            
            bgColor.W *= openProgress;
            DrawRoundedRect(x, y, SLOT_SIZE, SLOT_SIZE, CORNER_RADIUS, bgColor);

            // Slot border
            Vector4 borderColor = isSelected ? ACCENT_COLOR : BORDER_COLOR;
            float borderThickness = isSelected ? 3f : 2f;
            borderColor.W *= openProgress;
            DrawRoundedRectOutline(x, y, SLOT_SIZE, SLOT_SIZE, CORNER_RADIUS, borderThickness, borderColor);

            // Slot number or label
            if (fontRenderer != null)
            {
                if (number > 0)
                {
                    Vector4 numColor = new Vector4(0.7f, 0.7f, 0.8f, 0.8f * openProgress);
                    fontRenderer.DrawText(number.ToString(), new Vector2(x + 6, y + 6), 0.35f, numColor);
                }
                else if (label != null)
                {
                    Vector4 labelColor = new Vector4(0.6f, 0.6f, 0.7f, 0.7f * openProgress);
                    fontRenderer.DrawText(label, new Vector2(x + 6, y + 6), 0.3f, labelColor);
                }
            }

            // Item (if not being dragged)
            if (item.ItemId > 0 && !isDragging && fontRenderer != null)
            {
                RenderItemInSlot(x, y, item);
            }
        }

        private void RenderItemInSlot(float x, float y, ItemStack item)
        {
            if (fontRenderer == null) return;

            var itemDef = ItemRegistry.Get(item.ItemId);
            string name = itemDef?.Name ?? "???";

            if (name.Length > 9)
                name = name.Substring(0, 8) + "..";

            // Item name centered
            float nameScale = 0.33f;
            Vector2 nameSize = fontRenderer.MeasureText(name, nameScale);
            float nameX = x + (SLOT_SIZE - nameSize.X) / 2;
            float nameY = y + (SLOT_SIZE - nameSize.Y) / 2;

            // Text shadow
            Vector4 shadowColor = new Vector4(0, 0, 0, 0.8f * openProgress);
            fontRenderer.DrawText(name, new Vector2(nameX + 1, nameY + 1), nameScale, shadowColor);
            
            Vector4 textColor = new Vector4(1, 1, 1, openProgress);
            fontRenderer.DrawText(name, new Vector2(nameX, nameY), nameScale, textColor);

            // Item count badge
            if (item.Count > 1)
            {
                string countText = item.Count.ToString();
                float countScale = 0.4f;
                Vector2 countSize = fontRenderer.MeasureText(countText, countScale);
                float countX = x + SLOT_SIZE - countSize.X - 6;
                float countY = y + SLOT_SIZE - countSize.Y - 6;

                // Badge background
                float badgePadding = 3;
                Vector4 badgeBg = new Vector4(0, 0, 0, 0.85f * openProgress);
                DrawRoundedRect(countX - badgePadding, countY - badgePadding,
                    countSize.X + badgePadding * 2, countSize.Y + badgePadding * 2,
                    3f, badgeBg);

                Vector4 countColor = new Vector4(1, 0.9f, 0.5f, openProgress);
                fontRenderer.DrawText(countText, new Vector2(countX, countY), countScale, countColor);
            }

            // Rarity dot
            if (itemDef != null)
            {
                Vector4 rarityColor = itemDef.GetRarityColor();
                rarityColor.W *= openProgress;
                DrawCircle(x + SLOT_SIZE - 8, y + 8, 3f, rarityColor);
            }
        }

        private void RenderDraggedItem()
        {
            if (fontRenderer == null || draggedItem.ItemId == 0) return;

            float x = draggedItemPosition.X - SLOT_SIZE / 2;
            float y = draggedItemPosition.Y - SLOT_SIZE / 2;

            // Glowing dragged slot
            Vector4 glowColor = new Vector4(0.5f, 0.7f, 1f, 0.4f * draggedItemAlpha);
            DrawRoundedRect(x - 4, y - 4, SLOT_SIZE + 8, SLOT_SIZE + 8, CORNER_RADIUS + 2, glowColor);

            // Slot background
            Vector4 bgColor = new Vector4(0.2f, 0.2f, 0.25f, 0.9f * draggedItemAlpha);
            DrawRoundedRect(x, y, SLOT_SIZE, SLOT_SIZE, CORNER_RADIUS, bgColor);

            Vector4 borderColor = new Vector4(1f, 1f, 1f, draggedItemAlpha);
            DrawRoundedRectOutline(x, y, SLOT_SIZE, SLOT_SIZE, CORNER_RADIUS, 3f, borderColor);

            // Render item with alpha
            float oldProgress = openProgress;
            openProgress = draggedItemAlpha;
            RenderItemInSlot(x, y, draggedItem);
            openProgress = oldProgress;
        }

        // Drawing primitives
        private void DrawRect(float x, float y, float width, float height, Vector4 color)
        {
            // CRITICAL: Ensure our shader is active before drawing
            GL.UseProgram(shaderProgram);
            
            // CRITICAL: Set projection matrix every time (FontRenderer changes it)
            var projection = Matrix4.CreateOrthographicOffCenter(0, lastWindowSize.X, lastWindowSize.Y, 0, -1, 1);
            int projLoc = GL.GetUniformLocation(shaderProgram, "projection");
            GL.UniformMatrix4(projLoc, false, ref projection);
            
            float[] vertices = {
                x, y, color.X, color.Y, color.Z, color.W,
                x + width, y, color.X, color.Y, color.Z, color.W,
                x + width, y + height, color.X, color.Y, color.Z, color.W,
                
                x, y, color.X, color.Y, color.Z, color.W,
                x + width, y + height, color.X, color.Y, color.Z, color.W,
                x, y + height, color.X, color.Y, color.Z, color.W,
            };

            GL.BindVertexArray(vao);
            GL.BindBuffer(BufferTarget.ArrayBuffer, vbo);
            GL.BufferData(BufferTarget.ArrayBuffer, vertices.Length * sizeof(float), vertices, BufferUsageHint.DynamicDraw);
            GL.DrawArrays(PrimitiveType.Triangles, 0, 6);
        }

        private void DrawRoundedRect(float x, float y, float width, float height, float radius, Vector4 color)
        {
            // Draw main rectangle
            DrawRect(x, y, width, height, color);
            
            // Optional: Add corner highlights for rounded effect
            if (radius > 2f)
            {
                Vector4 cornerColor = new Vector4(color.X * 1.1f, color.Y * 1.1f, color.Z * 1.1f, color.W * 0.3f);
                
                // Top-left corner
                DrawRect(x, y, radius, radius, cornerColor);
                // Top-right corner  
                DrawRect(x + width - radius, y, radius, radius, cornerColor);
                // Bottom-left corner
                DrawRect(x, y + height - radius, radius, radius, cornerColor);
                // Bottom-right corner
                DrawRect(x + width - radius, y + height - radius, radius, radius, cornerColor);
            }
        }

        private void DrawRoundedRectOutline(float x, float y, float width, float height, float radius, float thickness, Vector4 color)
        {
            // Top
            DrawRect(x, y, width, thickness, color);
            // Bottom
            DrawRect(x, y + height - thickness, width, thickness, color);
            // Left
            DrawRect(x, y, thickness, height, color);
            // Right
            DrawRect(x + width - thickness, y, thickness, height, color);
        }

        private void DrawCircle(float centerX, float centerY, float radius, Vector4 color)
        {
            // CRITICAL: Ensure our shader is active
            GL.UseProgram(shaderProgram);
            
            int segments = 12;
            float[] vertices = new float[segments * 3 * 6];
            int idx = 0;

            for (int i = 0; i < segments; i++)
            {
                float angle1 = (float)(i * 2 * Math.PI / segments);
                float angle2 = (float)((i + 1) * 2 * Math.PI / segments);

                vertices[idx++] = centerX;
                vertices[idx++] = centerY;
                vertices[idx++] = color.X;
                vertices[idx++] = color.Y;
                vertices[idx++] = color.Z;
                vertices[idx++] = color.W;

                vertices[idx++] = centerX + (float)Math.Cos(angle1) * radius;
                vertices[idx++] = centerY + (float)Math.Sin(angle1) * radius;
                vertices[idx++] = color.X;
                vertices[idx++] = color.Y;
                vertices[idx++] = color.Z;
                vertices[idx++] = color.W;

                vertices[idx++] = centerX + (float)Math.Cos(angle2) * radius;
                vertices[idx++] = centerY + (float)Math.Sin(angle2) * radius;
                vertices[idx++] = color.X;
                vertices[idx++] = color.Y;
                vertices[idx++] = color.Z;
                vertices[idx++] = color.W;
            }

            GL.BindVertexArray(vao);
            GL.BindBuffer(BufferTarget.ArrayBuffer, vbo);
            GL.BufferData(BufferTarget.ArrayBuffer, vertices.Length * sizeof(float), vertices, BufferUsageHint.DynamicDraw);
            GL.DrawArrays(PrimitiveType.Triangles, 0, segments * 3);
        }

        public void ToggleInventory()
        {
            isOpen = !isOpen;
            
            if (!isOpen)
            {
                // Reset drag state
                draggedSlot = -1;
                draggedItem = ItemStack.Empty;
                hoveredSlot = -1;
                Console.WriteLine("[OpenGLInventoryUI] Inventory closed");
            }
            else
            {
                Console.WriteLine("[OpenGLInventoryUI] Inventory opened");
            }
        }

        public bool IsInventoryOpen() => isOpen;

        public int GetHoveredSlot() => hoveredSlot;

        public void Dispose()
        {
            if (vao != 0) GL.DeleteVertexArray(vao);
            if (vbo != 0) GL.DeleteBuffer(vbo);
            if (shaderProgram != 0) GL.DeleteProgram(shaderProgram);
            
            Console.WriteLine("[OpenGLInventoryUI] Disposed");
        }
    }
}
