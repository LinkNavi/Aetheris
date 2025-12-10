// Net/Client/Game/Inventory/InventoryUI.cs - Enhanced with animations, armor, totems
using System;
using System.Collections.Generic;
using OpenTK.Graphics.OpenGL4;
using OpenTK.Mathematics;
using OpenTK.Windowing.GraphicsLibraryFramework;

namespace Aetheris
{
    /// <summary>
    /// Enhanced inventory UI with animations, armor slots, and totem slots
    /// </summary>
    public class InventoryUI : IDisposable
    {
        private readonly Inventory inventory;
        private bool isInventoryOpen = false;

        // Animation state
        private float inventoryOpenProgress = 0f;
        private const float ANIMATION_SPEED = 8f;

        // UI state
        private int hoveredSlot = -1;
        private int draggedSlot = -1;
        private bool wasMousePressed = false;
        private Vector2 dragOffset = Vector2.Zero;

        // Slot animations
        private readonly Dictionary<int, float> slotPulseTimers = new();
        private readonly Dictionary<int, float> slotScales = new();

        // Shader and rendering
        private int shaderProgram;
        private int vao, vbo;

        // Layout constants
        private const float HOTBAR_SLOT_SIZE = 50f;
        private const float HOTBAR_SPACING = 4f;
        private const float HOTBAR_Y_OFFSET = 20f;

        private const float INV_SLOT_SIZE = 45f;
        private const float INV_SPACING = 4f;
        private const float INV_Y_START = 120f;

        private const float ARMOR_SLOT_SIZE = 50f;
        private const float ARMOR_SPACING = 6f;

        private const float TOTEM_SLOT_SIZE = 40f;
        private const float TOTEM_SPACING = 4f;

        // Slot type enum for rendering
        private enum SlotType { Hotbar, Inventory, Armor, Totem }

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

            int vs = CompileShader(ShaderType.VertexShader, vertexShader);
            int fs = CompileShader(ShaderType.FragmentShader, fragmentShader);

            shaderProgram = GL.CreateProgram();
            GL.AttachShader(shaderProgram, vs);
            GL.AttachShader(shaderProgram, fs);
            GL.LinkProgram(shaderProgram);

            GL.DeleteShader(vs);
            GL.DeleteShader(fs);
        }

        private void InitializeBuffers()
        {
            vao = GL.GenVertexArray();
            vbo = GL.GenBuffer();

            GL.BindVertexArray(vao);
            GL.BindBuffer(BufferTarget.ArrayBuffer, vbo);

            int stride = 8 * sizeof(float);

            GL.VertexAttribPointer(0, 2, VertexAttribPointerType.Float, false, stride, 0);
            GL.EnableVertexAttribArray(0);

            GL.VertexAttribPointer(1, 2, VertexAttribPointerType.Float, false, stride, 2 * sizeof(float));
            GL.EnableVertexAttribArray(1);

            GL.VertexAttribPointer(2, 4, VertexAttribPointerType.Float, false, stride, 4 * sizeof(float));
            GL.EnableVertexAttribArray(2);

            GL.BindVertexArray(0);
        }

        public void Update(KeyboardState keyboard, MouseState mouse, Vector2i windowSize, float deltaTime)
        {
            // Toggle inventory
            if (keyboard.IsKeyPressed(Keys.E))
            {
                isInventoryOpen = !isInventoryOpen;
                Console.WriteLine($"[InventoryUI] Inventory {(isInventoryOpen ? "opened" : "closed")}");
            }

            // Animate inventory panel
            float targetProgress = isInventoryOpen ? 1f : 0f;
            inventoryOpenProgress = Lerp(inventoryOpenProgress, targetProgress, deltaTime * ANIMATION_SPEED);

            // Hotbar selection
            if (!isInventoryOpen)
            {
                for (int i = 0; i < Inventory.HOTBAR_SIZE; i++)
                {
                    Keys key = Keys.D1 + i;
                    if (keyboard.IsKeyPressed(key))
                    {
                        inventory.SelectedHotbarSlot = i;
                        AnimateSlotPulse(i);
                    }
                }

                float scroll = mouse.ScrollDelta.Y;
                if (Math.Abs(scroll) > 0.1f)
                {
                    int oldSlot = inventory.SelectedHotbarSlot;
                    inventory.SelectedHotbarSlot -= (int)Math.Sign(scroll);
                    if (inventory.SelectedHotbarSlot < 0)
                        inventory.SelectedHotbarSlot = Inventory.HOTBAR_SIZE - 1;
                    if (inventory.SelectedHotbarSlot >= Inventory.HOTBAR_SIZE)
                        inventory.SelectedHotbarSlot = 0;

                    if (oldSlot != inventory.SelectedHotbarSlot)
                        AnimateSlotPulse(inventory.SelectedHotbarSlot);
                }
            }

            // Handle inventory interactions
            if (isInventoryOpen && inventoryOpenProgress > 0.5f)
            {
                UpdateInventoryInteraction(mouse, windowSize);
            }

            // Update animations
            UpdateAnimations(deltaTime);
        }

        private void UpdateAnimations(float deltaTime)
        {
            var keysToRemove = new List<int>();

            foreach (var slot in slotPulseTimers.Keys.ToList())
            {
                slotPulseTimers[slot] -= deltaTime;

                if (slotPulseTimers[slot] <= 0f)
                {
                    keysToRemove.Add(slot);
                    slotScales[slot] = 1f;
                }
                else
                {
                    float t = slotPulseTimers[slot] / 0.3f;
                    slotScales[slot] = 1f + (float)Math.Sin(t * Math.PI) * 0.15f;
                }
            }

            foreach (var key in keysToRemove)
            {
                slotPulseTimers.Remove(key);
            }
        }

        private void AnimateSlotPulse(int slot)
        {
            slotPulseTimers[slot] = 0.3f;
        }

        private void UpdateInventoryInteraction(MouseState mouse, Vector2i windowSize)
        {
            Vector2 mousePos = new Vector2(mouse.X, windowSize.Y - mouse.Y);
            hoveredSlot = GetSlotAtPosition(mousePos, windowSize);

            bool isMousePressed = mouse.IsButtonDown(MouseButton.Left);

            if (isMousePressed && !wasMousePressed && hoveredSlot >= 0)
            {
                draggedSlot = hoveredSlot;
                AnimateSlotPulse(hoveredSlot);
            }
            else if (!isMousePressed && wasMousePressed && draggedSlot >= 0)
            {
                if (hoveredSlot >= 0 && hoveredSlot != draggedSlot)
                {
                    SwapSlots(draggedSlot, hoveredSlot);
                    AnimateSlotPulse(hoveredSlot);
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

            if (inventoryOpenProgress < 0.5f) return -1;

            // Check main inventory
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

            // Check armor slots (left side)
            float armorStartX = invStartX - ARMOR_SLOT_SIZE - 40f;
            float armorStartY = INV_Y_START;

            for (int i = 0; i < 4; i++)
            {
                float x = armorStartX;
                float y = armorStartY + i * (ARMOR_SLOT_SIZE + ARMOR_SPACING);

                if (IsPointInRect(mousePos, x, y, ARMOR_SLOT_SIZE, ARMOR_SLOT_SIZE))
                    return Inventory.GetArmorSlotIndex(i);
            }

            // Check totem slots (right side)
            float totemStartX = invStartX + invWidth + 40f;
            float totemStartY = INV_Y_START;

            for (int i = 0; i < 5; i++)
            {
                float x = totemStartX;
                float y = totemStartY + i * (TOTEM_SLOT_SIZE + TOTEM_SPACING);

                if (IsPointInRect(mousePos, x, y, TOTEM_SLOT_SIZE, TOTEM_SLOT_SIZE))
                    return Inventory.GetTotemSlotIndex(i);
            }

            return -1;
        }

        private bool IsPointInRect(Vector2 point, float x, float y, float w, float h)
        {
            return point.X >= x && point.X <= x + w && point.Y >= y && point.Y <= y + h;
        }

        private void SwapSlots(int slotA, int slotB)
        {
            var itemA = inventory.GetSlot(slotA);
            var itemB = inventory.GetSlot(slotB);

            // Check if trying to place into armor slot
            if (slotB >= Inventory.GetArmorSlotIndex(0) &&
                slotB < Inventory.GetArmorSlotIndex(0) + Inventory.ARMOR_SIZE)
            {
                if (!ArmorCalculator.IsArmorItem(itemA.ItemId))
                {
                    // Not armor - reject
                    AnimateSlotPulse(slotA);
                    return;
                }

                int requiredSlot = slotB - Inventory.GetArmorSlotIndex(0);
                int itemSlot = ArmorCalculator.GetArmorSlotForItem(itemA.ItemId);

                if (itemSlot != requiredSlot)
                {
                    // Wrong armor slot
                    AnimateSlotPulse(slotA);
                    return;
                }
            }

            // Check if trying to place into totem slot
            if (slotB >= Inventory.GetTotemSlotIndex(0) &&
                slotB < Inventory.GetTotemSlotIndex(0) + Inventory.TOTEM_SIZE)
            {
                if (!TotemData.IsTotem(itemA.ItemId))
                {
                    // Not a totem - reject
                    AnimateSlotPulse(slotA);
                    return;
                }
            }

            // Valid swap
            inventory.SetSlot(slotA, itemB);
            inventory.SetSlot(slotB, itemA);
            AnimateSlotPulse(slotB);
        }

        public void Render(Vector2i windowSize)
        {
            bool depthTestEnabled = GL.IsEnabled(EnableCap.DepthTest);
            bool blendEnabled = GL.IsEnabled(EnableCap.Blend);

            GL.Disable(EnableCap.DepthTest);
            GL.Enable(EnableCap.Blend);
            GL.BlendFunc(BlendingFactor.SrcAlpha, BlendingFactor.OneMinusSrcAlpha);

            GL.UseProgram(shaderProgram);

            var projection = Matrix4.CreateOrthographicOffCenter(0, windowSize.X, 0, windowSize.Y, -1, 1);
            int projLoc = GL.GetUniformLocation(shaderProgram, "projection");
            GL.UniformMatrix4(projLoc, false, ref projection);

            GL.BindVertexArray(vao);

            // Always render hotbar
            RenderHotbar(windowSize);

            // Render full inventory if open (with animation)
            if (inventoryOpenProgress > 0.01f)
            {
                RenderFullInventory(windowSize);
            }

            GL.BindVertexArray(0);
            GL.UseProgram(0);

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
                float scale = slotScales.ContainsKey(i) ? slotScales[i] : 1f;

                RenderSlot(x, y, HOTBAR_SLOT_SIZE, inventory.GetSlot(i), isSelected, isHovered, scale, SlotType.Hotbar);
            }
        }

        private void RenderFullInventory(Vector2i windowSize)
        {
            float centerX = windowSize.X / 2f;
            float invWidth = 9 * (INV_SLOT_SIZE + INV_SPACING) - INV_SPACING;
            float startX = centerX - invWidth / 2f;

            float alpha = inventoryOpenProgress;

            // Background panel
            float panelWidth = invWidth + 300f;
            float panelHeight = 3 * (INV_SLOT_SIZE + INV_SPACING) + 80f;
            float panelY = INV_Y_START - 20f;

            DrawRect(startX - 150f, panelY, panelWidth, panelHeight,
                     new Vector4(0.1f, 0.1f, 0.1f, 0.9f * alpha));

            // Main inventory slots
            for (int row = 0; row < 3; row++)
            {
                for (int col = 0; col < 9; col++)
                {
                    int slot = Inventory.HOTBAR_SIZE + row * 9 + col;
                    float x = startX + col * (INV_SLOT_SIZE + INV_SPACING);
                    float y = INV_Y_START + row * (INV_SLOT_SIZE + INV_SPACING);

                    bool isHovered = hoveredSlot == slot;
                    float scale = slotScales.ContainsKey(slot) ? slotScales[slot] : 1f;
                    RenderSlot(x, y, INV_SLOT_SIZE, inventory.GetSlot(slot), false, isHovered, scale, SlotType.Inventory, alpha);
                }
            }

            // Armor slots (left side with icons)
            float armorStartX = startX - ARMOR_SLOT_SIZE - 40f;
            float armorStartY = INV_Y_START;
            string[] armorLabels = { "HEAD", "CHEST", "LEGS", "FEET" };

            for (int i = 0; i < 4; i++)
            {
                int slot = Inventory.GetArmorSlotIndex(i);
                float x = armorStartX;
                float y = armorStartY + i * (ARMOR_SLOT_SIZE + ARMOR_SPACING);

                bool isHovered = hoveredSlot == slot;
                float scale = slotScales.ContainsKey(slot) ? slotScales[slot] : 1f;
                RenderSlot(x, y, ARMOR_SLOT_SIZE, inventory.GetSlot(slot), false, isHovered, scale, SlotType.Armor, alpha);
            }

            // Totem slots (right side)
            float totemStartX = startX + invWidth + 40f;
            float totemStartY = INV_Y_START;

            for (int i = 0; i < 5; i++)
            {
                int slot = Inventory.GetTotemSlotIndex(i);
                float x = totemStartX;
                float y = totemStartY + i * (TOTEM_SLOT_SIZE + TOTEM_SPACING);

                bool isHovered = hoveredSlot == slot;
                float scale = slotScales.ContainsKey(slot) ? slotScales[slot] : 1f;
                RenderSlot(x, y, TOTEM_SLOT_SIZE, inventory.GetSlot(slot), false, isHovered, scale, SlotType.Totem, alpha);
            }
        }

        private void RenderSlot(float x, float y, float size, ItemStack item, bool selected, bool hovered, float scale, SlotType slotType, float alpha = 1f)
        {
            // Apply scale from center
            float scaledSize = size * scale;
            float offset = (size - scaledSize) / 2f;
            x += offset;
            y += offset;

            // Background color based on slot type
            Vector4 bgColor = slotType switch
            {
                SlotType.Armor => new Vector4(0.3f, 0.25f, 0.2f, 0.8f * alpha),
                SlotType.Totem => new Vector4(0.2f, 0.25f, 0.35f, 0.8f * alpha),
                _ => new Vector4(0.2f, 0.2f, 0.2f, 0.8f * alpha)
            };

            if (selected) bgColor += new Vector4(0.2f, 0.2f, 0.2f, 0f);
            if (hovered) bgColor += new Vector4(0.1f, 0.1f, 0.1f, 0f);

            DrawRect(x, y, scaledSize, scaledSize, bgColor);

            // Border
            Vector4 borderColor = selected
                ? new Vector4(1f, 1f, 1f, alpha)
                : new Vector4(0.5f, 0.5f, 0.5f, 0.8f * alpha);
            DrawRectOutline(x, y, scaledSize, scaledSize, 2f, borderColor);

            // Item
            if (item.ItemId > 0)
            {
                Vector4 itemColor = GetItemColor(item.ItemId);
                itemColor.W *= alpha;
                float itemSize = scaledSize * 0.6f;
                float itemX = x + (scaledSize - itemSize) / 2f;
                float itemY = y + (scaledSize - itemSize) / 2f;
                DrawRect(itemX, itemY, itemSize, itemSize, itemColor);

                if (item.Count > 1)
                {
                    DrawRect(x + scaledSize - 10f, y + 2f, 8f, 8f, new Vector4(1f, 1f, 0f, alpha));
                }
            }
        }

        private Vector4 GetItemColor(int itemId)
        {
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
            DrawRect(x, y + h - thickness, w, thickness, color);
            DrawRect(x, y, w, thickness, color);
            DrawRect(x, y, thickness, h, color);
            DrawRect(x + w - thickness, y, thickness, h, color);
        }

        private float Lerp(float a, float b, float t) => a + (b - a) * Math.Clamp(t, 0f, 1f);

        public bool IsInventoryOpen() => isInventoryOpen;

        private int CompileShader(ShaderType type, string source)
        {
            int shader = GL.CreateShader(type);
            GL.ShaderSource(shader, source);
            GL.CompileShader(shader);

            GL.GetShader(shader, ShaderParameter.CompileStatus, out int success);
            if (success == 0)
            {
                string info = GL.GetShaderInfoLog(shader);
                Console.WriteLine($"[InventoryUI] Shader compilation failed: {info}");
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
