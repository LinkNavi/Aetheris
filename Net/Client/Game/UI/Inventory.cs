// Net/Client/Game/UI/Inventory.cs - Gothic vampire hunter themed inventory
using System;
using System.Collections.Generic;
using System.Linq;
using OpenTK.Graphics.OpenGL4;
using OpenTK.Mathematics;
using OpenTK.Windowing.GraphicsLibraryFramework;
using Aetheris.UI;

namespace Aetheris
{
    /// <summary>
    /// -inspired inventory with ornate gothic design
    /// Features: Leather-bound book aesthetic, crimson accents, ornate frames
    /// </summary>
    public class InventoryUIRedo: IDisposable
    {
        private readonly Inventory inventory;
        private readonly ITextRenderer? textRenderer;
        private bool isInventoryOpen = false;

        private float inventoryOpenProgress = 0f;
        private const float ANIMATION_SPEED = 10f;

        private int hoveredSlot = -1;
        private int draggedSlot = -1;
        private bool wasMousePressed = false;

        private readonly Dictionary<int, float> slotPulseTimers = new();
        private readonly Dictionary<int, float> slotScales = new();
        
        private float pageFlipAnimation = 0f;

        private int shaderProgram;
        private int vao, vbo;

        // Gothic color palette
        private static readonly Vector4 BLOOD_RED = new Vector4(0.8f, 0.1f, 0.1f, 1f);
        private static readonly Vector4 DARK_CRIMSON = new Vector4(0.5f, 0.05f, 0.05f, 0.9f);
        private static readonly Vector4 GOLD_ACCENT = new Vector4(0.9f, 0.7f, 0.3f, 1f);
        private static readonly Vector4 SILVER = new Vector4(0.75f, 0.75f, 0.8f, 1f);
        private static readonly Vector4 LEATHER_BG = new Vector4(0.15f, 0.1f, 0.08f, 0.98f);
        private static readonly Vector4 PARCHMENT = new Vector4(0.92f, 0.87f, 0.75f, 1f);
        private static readonly Vector4 INK_BLACK = new Vector4(0.1f, 0.08f, 0.07f, 1f);

        // Layout constants
        private const float SLOT_SIZE = 52f;
        private const float SLOT_SPACING = 8f;

        public InventoryUIRedo	(Inventory inventory, ITextRenderer? textRenderer = null)
        {
            this.inventory = inventory ?? throw new ArgumentNullException(nameof(inventory));
            this.textRenderer = textRenderer;
            InitializeShaders();
            InitializeBuffers();
        }

        public int GetHoveredSlot() => hoveredSlot;
        public bool IsInventoryOpen() => isInventoryOpen;
        
        public void ToggleInventory()
        {
            isInventoryOpen = !isInventoryOpen;
            pageFlipAnimation = 0f;
            Console.WriteLine($"[Inventory] Toggled to: {isInventoryOpen}");
            
            if (!isInventoryOpen)
            {
                hoveredSlot = -1;
                draggedSlot = -1;
                wasMousePressed = false;
            }
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
            // Animation
            float targetProgress = isInventoryOpen ? 1f : 0f;
            inventoryOpenProgress = Lerp(inventoryOpenProgress, targetProgress, deltaTime * ANIMATION_SPEED);
            
            // Page flip animation
            if (isInventoryOpen && pageFlipAnimation < 1f)
            {
                pageFlipAnimation += deltaTime * 5f;
            }
            
            // Hotbar selection (only when inventory closed)
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
            
            // Inventory interaction
            if (isInventoryOpen && inventoryOpenProgress > 0.5f)
            {
                UpdateInventoryInteraction(mouse, windowSize);
            }
            
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
                    slotScales[slot] = 1f + (float)Math.Sin(t * Math.PI) * 0.12f;
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
            float centerY = windowSize.Y / 2f;
            
            // Hotbar at bottom
            float hotbarWidth = Inventory.HOTBAR_SIZE * (SLOT_SIZE + SLOT_SPACING);
            float hotbarStartX = centerX - hotbarWidth / 2f;
            float hotbarY = 80f;
            
            for (int i = 0; i < Inventory.HOTBAR_SIZE; i++)
            {
                float x = hotbarStartX + i * (SLOT_SIZE + SLOT_SPACING);
                if (IsPointInRect(mousePos, x, hotbarY, SLOT_SIZE, SLOT_SIZE))
                    return i;
            }
            
            if (inventoryOpenProgress < 0.5f) return -1;
            
            // Main inventory grid (3 rows x 9 cols)
            float invWidth = 9 * (SLOT_SIZE + SLOT_SPACING);
            float invStartX = centerX - invWidth / 2f;
            float invStartY = centerY - 100f;
            
            for (int row = 0; row < 3; row++)
            {
                for (int col = 0; col < 9; col++)
                {
                    int slot = Inventory.HOTBAR_SIZE + row * 9 + col;
                    float x = invStartX + col * (SLOT_SIZE + SLOT_SPACING);
                    float y = invStartY + row * (SLOT_SIZE + SLOT_SPACING);
                    
                    if (IsPointInRect(mousePos, x, y, SLOT_SIZE, SLOT_SIZE))
                        return slot;
                }
            }
            
            // Armor slots (left side)
            float armorX = centerX - invWidth / 2f - 100f;
            float armorY = centerY - 80f;
            
            for (int i = 0; i < 4; i++)
            {
                float y = armorY + i * (SLOT_SIZE + SLOT_SPACING);
                if (IsPointInRect(mousePos, armorX, y, SLOT_SIZE, SLOT_SIZE))
                    return Inventory.GetArmorSlotIndex(i);
            }
            
            // Totem slots (right side)
            float totemX = centerX + invWidth / 2f + 48f;
            float totemY = centerY - 80f;
            
            for (int i = 0; i < 5; i++)
            {
                float y = totemY + i * (SLOT_SIZE + SLOT_SPACING - 2f);
                if (IsPointInRect(mousePos, totemX, y, SLOT_SIZE, SLOT_SIZE))
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

            // Validation for armor slots
            if (slotB >= Inventory.GetArmorSlotIndex(0) &&
                slotB < Inventory.GetArmorSlotIndex(0) + Inventory.ARMOR_SIZE)
            {
                if (!ArmorCalculator.IsArmorItem(itemA.ItemId))
                {
                    AnimateSlotPulse(slotA);
                    return;
                }
            }
            
            // Validation for totem slots
            if (slotB >= Inventory.GetTotemSlotIndex(0) &&
                slotB < Inventory.GetTotemSlotIndex(0) + Inventory.TOTEM_SIZE)
            {
                if (!TotemData.IsTotem(itemA.ItemId))
                {
                    AnimateSlotPulse(slotA);
                    return;
                }
            }

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

            if (textRenderer != null)
            {
                textRenderer.SetProjection(projection);
            }

            GL.BindVertexArray(vao);

            // Always render hotbar
            RenderGothicHotbar(windowSize);

            // Render full inventory when open
            if (inventoryOpenProgress > 0.01f)
            {
                RenderGothicInventory(windowSize);
            }

            GL.BindVertexArray(0);
            GL.UseProgram(0);

            if (depthTestEnabled) GL.Enable(EnableCap.DepthTest);
            if (!blendEnabled) GL.Disable(EnableCap.Blend);
        }

        private void RenderGothicHotbar(Vector2i windowSize)
        {
            float centerX = windowSize.X / 2f;
            float hotbarWidth = Inventory.HOTBAR_SIZE * (SLOT_SIZE + SLOT_SPACING) + 40f;
            float hotbarX = centerX - hotbarWidth / 2f;
            float hotbarY = 60f;
            
            // Gothic panel background
            DrawGothicPanel(hotbarX, hotbarY, hotbarWidth, 80f);
            
            // Hotbar slots
            float slotStartX = hotbarX + 20f;
            float slotY = hotbarY + 14f;
            
            for (int i = 0; i < Inventory.HOTBAR_SIZE; i++)
            {
                float x = slotStartX + i * (SLOT_SIZE + SLOT_SPACING);
                bool isSelected = i == inventory.SelectedHotbarSlot;
                bool isHovered = hoveredSlot == i;
                float scale = slotScales.ContainsKey(i) ? slotScales[i] : 1f;

                RenderGothicSlot(x, slotY, SLOT_SIZE, inventory.GetSlot(i), 
                               isSelected, isHovered, scale, i, 1f);
            }
        }

        private void RenderGothicInventory(Vector2i windowSize)
        {
            float centerX = windowSize.X / 2f;
            float centerY = windowSize.Y / 2f;
            
            float alpha = inventoryOpenProgress;
            
            // Page flip effect
            float flipScale = Math.Min(1f, pageFlipAnimation * 1.5f);
            
            // Leather-bound book background
            float bookWidth = 750f;
            float bookHeight = 500f;
            float bookX = centerX - bookWidth / 2f;
            float bookY = centerY - bookHeight / 2f;
            
            // Book shadow
            DrawRect(bookX + 8f, bookY + 8f, bookWidth, bookHeight, 
                    new Vector4(0f, 0f, 0f, 0.6f * alpha));
            
            // Leather cover
            Vector4 leatherColor = LEATHER_BG;
            leatherColor.W *= alpha;
            DrawRect(bookX, bookY, bookWidth, bookHeight, leatherColor);
            
            // Ornate border with gold trim
            DrawOrnateBookBorder(bookX, bookY, bookWidth, bookHeight, alpha);
            
            // Parchment pages
            float pageInset = 15f;
            Vector4 parchmentColor = PARCHMENT;
            parchmentColor.W *= alpha * flipScale;
            DrawRect(bookX + pageInset, bookY + pageInset, 
                    bookWidth - pageInset * 2, bookHeight - pageInset * 2, parchmentColor);
            
            if (flipScale < 0.3f) return; // Don't render contents during flip
            
            // Title at top
            if (textRenderer != null)
            {
                Vector4 titleColor = INK_BLACK;
                titleColor.W *= alpha;
                Vector2 titlePos = new Vector2(centerX - 80f, bookY + bookHeight - 40f);
                textRenderer.DrawText("⚔ INVENTORY ⚔", titlePos, 1.2f, titleColor);
            }
            
            // Main inventory grid
            float invWidth = 9 * (SLOT_SIZE + SLOT_SPACING);
            float invStartX = centerX - invWidth / 2f;
            float invStartY = centerY - 80f;
            
            for (int row = 0; row < 3; row++)
            {
                for (int col = 0; col < 9; col++)
                {
                    int slot = Inventory.HOTBAR_SIZE + row * 9 + col;
                    float x = invStartX + col * (SLOT_SIZE + SLOT_SPACING);
                    float y = invStartY + row * (SLOT_SIZE + SLOT_SPACING);
                    
                    bool isHovered = hoveredSlot == slot;
                    float scale = slotScales.ContainsKey(slot) ? slotScales[slot] : 1f;
                    RenderGothicSlot(x, y, SLOT_SIZE, inventory.GetSlot(slot), 
                                   false, isHovered, scale, slot, alpha);
                }
            }
            
            // Armor slots (left page)
            RenderArmorSection(centerX - invWidth / 2f - 100f, centerY, alpha);
            
            // Totem slots (right page)
            RenderTotemSection(centerX + invWidth / 2f + 48f, centerY, alpha);
        }

        private void RenderArmorSection(float x, float y, float alpha)
        {
            // Section title
            if (textRenderer != null)
            {
                Vector4 titleColor = INK_BLACK;
                titleColor.W *= alpha;
                textRenderer.DrawText("ARMOR", new Vector2(x, y + 120f), 0.9f, titleColor);
            }
            
            y -= 80f;
            
            string[] armorLabels = { "HEAD", "CHEST", "LEGS", "FEET" };
            
            for (int i = 0; i < 4; i++)
            {
                int slot = Inventory.GetArmorSlotIndex(i);
                float slotY = y + i * (SLOT_SIZE + SLOT_SPACING);
                
                bool isHovered = hoveredSlot == slot;
                float scale = slotScales.ContainsKey(slot) ? slotScales[slot] : 1f;
                
                RenderGothicSlot(x, slotY, SLOT_SIZE, inventory.GetSlot(slot), 
                               false, isHovered, scale, slot, alpha);
                
                // Label
                if (textRenderer != null && inventory.GetSlot(slot).ItemId == 0)
                {
                    Vector4 labelColor = new Vector4(0.4f, 0.3f, 0.25f, alpha);
                    textRenderer.DrawText(armorLabels[i], new Vector2(x - 48f, slotY + 18f), 0.6f, labelColor);
                }
            }
        }

        private void RenderTotemSection(float x, float y, float alpha)
        {
            // Section title
            if (textRenderer != null)
            {
                Vector4 titleColor = INK_BLACK;
                titleColor.W *= alpha;
                textRenderer.DrawText("RELICS", new Vector2(x - 8f, y + 120f), 0.9f, titleColor);
            }
            
            y -= 80f;
            
            for (int i = 0; i < 5; i++)
            {
                int slot = Inventory.GetTotemSlotIndex(i);
                float slotY = y + i * (SLOT_SIZE + SLOT_SPACING - 2f);
                
                bool isHovered = hoveredSlot == slot;
                float scale = slotScales.ContainsKey(slot) ? slotScales[slot] : 1f;
                
                RenderGothicSlot(x, slotY, SLOT_SIZE, inventory.GetSlot(slot), 
                               false, isHovered, scale, slot, alpha);
            }
        }

        private void RenderGothicSlot(float x, float y, float size, ItemStack item, 
                                     bool selected, bool hovered, float scale, int slotIndex, float alpha)
        {
            float scaledSize = size * scale;
            float offset = (size - scaledSize) / 2f;
            x += offset;
            y += offset;

            // Slot background
            Vector4 bgColor = selected ? DARK_CRIMSON : new Vector4(0.12f, 0.1f, 0.08f, 0.9f * alpha);
            if (hovered) bgColor = new Vector4(0.3f, 0.15f, 0.1f, 0.9f * alpha);
            
            DrawRect(x, y, scaledSize, scaledSize, bgColor);

            // Ornate border
            Vector4 borderColor = selected ? GOLD_ACCENT : SILVER;
            borderColor.W *= alpha;
            DrawRectOutline(x, y, scaledSize, scaledSize, 2f, borderColor);
            
            // Inner decorative frame
            DrawRectOutline(x + 3f, y + 3f, scaledSize - 6f, scaledSize - 6f, 1f, 
                          new Vector4(0.4f, 0.3f, 0.2f, alpha * 0.6f));
            
            // Corner decorations
            float cornerSize = 6f;
            DrawCornerDecoration(x, y, cornerSize, borderColor);
            DrawCornerDecoration(x + scaledSize, y, cornerSize, borderColor);
            DrawCornerDecoration(x, y + scaledSize, cornerSize, borderColor);
            DrawCornerDecoration(x + scaledSize, y + scaledSize, cornerSize, borderColor);

            if (item.ItemId > 0)
            {
                var itemDef = ItemRegistry.Get(item.ItemId);
                if (itemDef != null)
                {
                    // Item color representation
                    Vector4 itemColor = itemDef.GetRarityColor();
                    itemColor.W *= alpha;
                    float itemSize = scaledSize * 0.55f;
                    float itemX = x + (scaledSize - itemSize) / 2f;
                    float itemY = y + (scaledSize - itemSize) / 2f;
                    DrawRect(itemX, itemY, itemSize, itemSize, itemColor);
                    
                    if (textRenderer != null)
                    {
                        // Item count
                        if (item.Count > 1)
                        {
                            string countText = item.Count > 99 ? "99+" : item.Count.ToString();
                            Vector2 countPos = new Vector2(x + scaledSize - 16f, y + 6f);
                            Vector4 textColor = new Vector4(1f, 1f, 1f, alpha);
                            textRenderer.DrawText(countText, countPos, 0.7f, textColor);
                        }
                        
                        // Slot number for hotbar
                        if (slotIndex < 9)
                        {
                            string numText = (slotIndex + 1).ToString();
                            Vector2 numPos = new Vector2(x + 4f, y + scaledSize - 16f);
                            Vector4 numColor = new Vector4(0.9f, 0.8f, 0.6f, alpha * 0.8f);
                            textRenderer.DrawText(numText, numPos, 0.6f, numColor);
                        }
                        
                        // Item name on hover
                        if (hovered)
                        {
                            Vector2 namePos = new Vector2(x, y + scaledSize + 10f);
                            Vector4 nameColor = itemDef.GetRarityColor();
                            nameColor.W = alpha;
                            textRenderer.DrawText(itemDef.Name, namePos, 0.85f, nameColor);
                        }
                    }
                }
            }
        }

        private void DrawGothicPanel(float x, float y, float w, float h)
        {
            // Dark background
            DrawRect(x, y, w, h, LEATHER_BG);
            
            // Ornate border
            DrawRectOutline(x, y, w, h, 3f, ORNATE_BORDER);
            DrawRectOutline(x - 1f, y - 1f, w + 2f, h + 2f, 1f, GOLD_ACCENT);
            
            // Corner decorations
            float cornerSize = 10f;
            DrawCornerDecoration(x, y, cornerSize, GOLD_ACCENT);
            DrawCornerDecoration(x + w, y, cornerSize, GOLD_ACCENT);
            DrawCornerDecoration(x, y + h, cornerSize, GOLD_ACCENT);
            DrawCornerDecoration(x + w, y + h, cornerSize, GOLD_ACCENT);
        }

        private void DrawOrnateBookBorder(float x, float y, float w, float h, float alpha)
        {
            Vector4 goldColor = GOLD_ACCENT;
            goldColor.W *= alpha;
            
            // Multiple border layers for depth
            for (int i = 0; i < 3; i++)
            {
                float offset = i * 2f;
                DrawRectOutline(x + offset, y + offset, w - offset * 2, h - offset * 2, 2f, 
                              new Vector4(ORNATE_BORDER.X, ORNATE_BORDER.Y, ORNATE_BORDER.Z, alpha));
            }
            
            // Gold accent trim
            DrawRectOutline(x + 6f, y + 6f, w - 12f, h - 12f, 1f, goldColor);
            
            // Corner embellishments
            float cornerSize = 25f;
            DrawCornerDecoration(x + 10f, y + 10f, cornerSize, goldColor);
            DrawCornerDecoration(x + w - 10f, y + 10f, cornerSize, goldColor);
            DrawCornerDecoration(x + 10f, y + h - 10f, cornerSize, goldColor);
            DrawCornerDecoration(x + w - 10f, y + h - 10f, cornerSize, goldColor);
        }

        private void DrawCornerDecoration(float x, float y, float size, Vector4 color)
        {
            DrawLine(x, y, x + size, y, 1f, color);
            DrawLine(x, y, x, y + size, 1f, color);
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
        
        private void DrawLine(float x1, float y1, float x2, float y2, float thickness, Vector4 color)
        {
            float dx = x2 - x1;
            float dy = y2 - y1;
            float length = MathF.Sqrt(dx * dx + dy * dy);
            if (length < 0.1f) return;
            
            float angle = MathF.Atan2(dy, dx);
            float halfThickness = thickness * 0.5f;
            float perpX = -MathF.Sin(angle) * halfThickness;
            float perpY = MathF.Cos(angle) * halfThickness;
            
            float[] vertices = new float[]
            {
                x1 + perpX, y1 + perpY, 0, 0, color.X, color.Y, color.Z, color.W,
                x1 - perpX, y1 - perpY, 0, 1, color.X, color.Y, color.Z, color.W,
                x2 - perpX, y2 - perpY, 1, 1, color.X, color.Y, color.Z, color.W,
                
                x1 + perpX, y1 + perpY, 0, 0, color.X, color.Y, color.Z, color.W,
                x2 - perpX, y2 - perpY, 1, 1, color.X, color.Y, color.Z, color.W,
                x2 + perpX, y2 + perpY, 1, 0, color.X, color.Y, color.Z, color.W,
            };
            
            GL.BindBuffer(BufferTarget.ArrayBuffer, vbo);
            GL.BufferData(BufferTarget.ArrayBuffer, vertices.Length * sizeof(float), vertices, BufferUsageHint.DynamicDraw);
            GL.DrawArrays(PrimitiveType.Triangles, 0, 6);
        }

        private float Lerp(float a, float b, float t) => a + (b - a) * Math.Clamp(t, 0f, 1f);

        private int CompileShader(ShaderType type, string source)
        {
            int shader = GL.CreateShader(type);
            GL.ShaderSource(shader, source);
            GL.CompileShader(shader);

            GL.GetShader(shader, ShaderParameter.CompileStatus, out int success);
            if (success == 0)
            {
                string info = GL.GetShaderInfoLog(shader);
                Console.WriteLine($"[Inventory] Shader compilation failed: {info}");
            }

            return shader;
        }

        public void Dispose()
        {
            GL.DeleteVertexArray(vao);
            GL.DeleteBuffer(vbo);
            GL.DeleteProgram(shaderProgram);
        }

    
    // Use existing ORNATE_BORDER color
    private static readonly Vector4 ORNATE_BORDER = new Vector4(0.4f, 0.25f, 0.15f, 1f);
}}
