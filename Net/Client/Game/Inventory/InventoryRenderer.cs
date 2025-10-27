// File: Net/Client/Game/Inventory/EnhancedInventoryUI.cs
using System;
using OpenTK.Graphics.OpenGL4;
using OpenTK.Mathematics;
using OpenTK.Windowing.GraphicsLibraryFramework;
using Aetheris.UI;

namespace Aetheris
{
    /// <summary>
    /// Complete redesign of inventory UI - separate screen with polished UX
    /// Features:
    /// - Full-screen overlay with semi-transparent background
    /// - Organized sections (hotbar, inventory, trash)
    /// - Smooth hover effects and animations
    /// - Right-click to split stacks
    /// - Drag and drop support
    /// - Item tooltips on hover
    /// </summary>
    public class EnhancedInventoryUI
    {
        private readonly Inventory inventory;
        private readonly int shaderProgram;
        private readonly int vao;
        private readonly int vbo;
        private ITextRenderer? textRenderer;
        
        // Layout constants
        private const float SLOT_SIZE = 64f;
        private const float SLOT_SPACING = 4f;
        private const float SECTION_SPACING = 32f;
        private const float PANEL_PADDING = 24f;
        private const float TITLE_HEIGHT = 48f;
        
        private const int INVENTORY_COLS = 9;
        private const int INVENTORY_ROWS = 3;
        private const int HOTBAR_SLOTS = 9;
        
        // Colors (darker, more refined palette)
        private readonly Vector4 BG_OVERLAY = new Vector4(0.05f, 0.05f, 0.08f, 0.85f);
        private readonly Vector4 PANEL_BG = new Vector4(0.12f, 0.14f, 0.18f, 0.98f);
        private readonly Vector4 SLOT_BG = new Vector4(0.08f, 0.09f, 0.12f, 1f);
        private readonly Vector4 SLOT_BORDER = new Vector4(0.25f, 0.28f, 0.35f, 1f);
        private readonly Vector4 SLOT_HOVER = new Vector4(0.4f, 0.45f, 0.55f, 1f);
        private readonly Vector4 SLOT_SELECTED = new Vector4(0.3f, 0.5f, 0.7f, 1f);
        private readonly Vector4 HOTBAR_HIGHLIGHT = new Vector4(0.9f, 0.75f, 0.3f, 1f);
        private readonly Vector4 TEXT_PRIMARY = new Vector4(0.95f, 0.97f, 1f, 1f);
        private readonly Vector4 TEXT_SECONDARY = new Vector4(0.7f, 0.75f, 0.8f, 1f);
        private readonly Vector4 TEXT_HINT = new Vector4(0.5f, 0.55f, 0.6f, 1f);
        
        // State
        private Vector2 panelPosition;
        private Vector2 panelSize;
        private int? hoveredSlot = null;
        private int? heldItemSlot = null;
        private ItemStack heldStack;
        private float hoverTime = 0f;
        private const float TOOLTIP_DELAY = 0.3f;
        
        // Mouse state tracking for reliable click detection
        private bool wasLeftPressed = false;
        private bool wasRightPressed = false;
        
        // Animation
        private float openAnimation = 0f;
        private const float ANIM_SPEED = 8f;
        
        public EnhancedInventoryUI(Inventory inventory, int shaderProgram, int vao, int vbo)
        {
            this.inventory = inventory;
            this.shaderProgram = shaderProgram;
            this.vao = vao;
            this.vbo = vbo;
            
            // Calculate panel size
            float inventoryWidth = INVENTORY_COLS * SLOT_SIZE + (INVENTORY_COLS - 1) * SLOT_SPACING;
            float hotbarWidth = HOTBAR_SLOTS * SLOT_SIZE + (HOTBAR_SLOTS - 1) * SLOT_SPACING;
            
            panelSize = new Vector2(
                Math.Max(inventoryWidth, hotbarWidth) + PANEL_PADDING * 2,
                TITLE_HEIGHT + 
                (INVENTORY_ROWS * SLOT_SIZE + (INVENTORY_ROWS - 1) * SLOT_SPACING) + 
                SECTION_SPACING +
                SLOT_SIZE + // Hotbar row
                PANEL_PADDING * 2
            );
            
            Console.WriteLine($"[EnhancedInventoryUI] Panel size: {panelSize}");
        }
        
        public void SetTextRenderer(ITextRenderer renderer)
        {
            textRenderer = renderer;
        }
        
        public void Update(MouseState mouse, int screenWidth, int screenHeight, float deltaTime)
        {
            // Animate opening
            openAnimation = Math.Min(1f, openAnimation + ANIM_SPEED * deltaTime);
            
            // Center panel (no animation offset for now to debug)
            panelPosition = new Vector2(
                (screenWidth - panelSize.X) / 2f,
                (screenHeight - panelSize.Y) / 2f
            );
            
            // Mouse interaction - direct position, no transformation needed
            Vector2 mousePos = new Vector2(mouse.X, mouse.Y);
            int? previousHovered = hoveredSlot;
            hoveredSlot = GetSlotAtPosition(mousePos);
            
            // Debug first few frames
            if (openAnimation < 0.5f)
            {
                Console.WriteLine($"[Inventory] Mouse: ({mouse.X}, {mouse.Y}), Panel: ({panelPosition.X}, {panelPosition.Y}), Hovered: {hoveredSlot}");
            }
            
            // Reset hover timer if slot changed
            if (hoveredSlot != previousHovered)
            {
                hoverTime = 0f;
            }
            else if (hoveredSlot.HasValue)
            {
                hoverTime += deltaTime;
            }
            
            // Handle clicks with proper edge detection
            bool leftPressed = mouse.IsButtonDown(MouseButton.Left);
            bool rightPressed = mouse.IsButtonDown(MouseButton.Right);
            
            // Detect button press (edge trigger, not held)
            bool leftClicked = leftPressed && !wasLeftPressed;
            bool rightClicked = rightPressed && !wasRightPressed;
            
            wasLeftPressed = leftPressed;
            wasRightPressed = rightPressed;
            
            if (leftClicked)
            {
                Console.WriteLine($"[Inventory] Left click at slot {hoveredSlot}");
                HandleLeftClick();
            }
            
            if (rightClicked)
            {
                Console.WriteLine($"[Inventory] Right click at slot {hoveredSlot}");
                HandleRightClick();
            }
        }
        
        private int? GetSlotAtPosition(Vector2 mousePos)
        {
            // Check inventory slots
            for (int i = 0; i < INVENTORY_ROWS * INVENTORY_COLS; i++)
            {
                Vector2 slotPos = GetInventorySlotPosition(i);
                if (IsPointInRect(mousePos, slotPos, SLOT_SIZE, SLOT_SIZE))
                {
                    return i;
                }
            }
            
            // Check hotbar slots
            for (int i = 0; i < HOTBAR_SLOTS; i++)
            {
                Vector2 slotPos = GetHotbarSlotPosition(i);
                if (IsPointInRect(mousePos, slotPos, SLOT_SIZE, SLOT_SIZE))
                {
                    return INVENTORY_ROWS * INVENTORY_COLS + i;
                }
            }
            
            return null;
        }
        
        private Vector2 GetInventorySlotPosition(int index)
        {
            int col = index % INVENTORY_COLS;
            int row = index / INVENTORY_COLS;
            
            return new Vector2(
                panelPosition.X + PANEL_PADDING + col * (SLOT_SIZE + SLOT_SPACING),
                panelPosition.Y + TITLE_HEIGHT + PANEL_PADDING + row * (SLOT_SIZE + SLOT_SPACING)
            );
        }
        
        private Vector2 GetHotbarSlotPosition(int index)
        {
            float inventoryHeight = INVENTORY_ROWS * SLOT_SIZE + (INVENTORY_ROWS - 1) * SLOT_SPACING;
            float hotbarY = panelPosition.Y + TITLE_HEIGHT + PANEL_PADDING + inventoryHeight + SECTION_SPACING;
            
            return new Vector2(
                panelPosition.X + PANEL_PADDING + index * (SLOT_SIZE + SLOT_SPACING),
                hotbarY
            );
        }
        
        private bool IsPointInRect(Vector2 point, Vector2 rectPos, float width, float height)
        {
            return point.X >= rectPos.X && point.X <= rectPos.X + width &&
                   point.Y >= rectPos.Y && point.Y <= rectPos.Y + height;
        }
        
        private void HandleLeftClick()
        {
            if (!hoveredSlot.HasValue) return;
            
            int slotIndex = hoveredSlot.Value;
            var slot = inventory.Slots[slotIndex];
            
            if (!heldItemSlot.HasValue)
            {
                // Pick up item
                if (slot.ItemId != 0)
                {
                    heldStack = slot;
                    heldItemSlot = slotIndex;
                    inventory.Slots[slotIndex] = default;
                    Console.WriteLine($"[Inventory] Picked up {GetItemName(heldStack.ItemId)} x{heldStack.Count}");
                }
            }
            else
            {
                // Place or merge item
                if (slot.ItemId == 0)
                {
                    // Place in empty slot
                    inventory.Slots[slotIndex] = heldStack;
                    heldItemSlot = null;
                    Console.WriteLine($"[Inventory] Placed in slot {slotIndex}");
                }
                else if (slot.ItemId == heldStack.ItemId)
                {
                    // Merge stacks
                    int canAdd = Math.Min(64 - slot.Count, heldStack.Count);
                    inventory.Slots[slotIndex].Count += canAdd;
                    heldStack.Count -= canAdd;
                    
                    if (heldStack.Count <= 0)
                    {
                        heldItemSlot = null;
                    }
                }
                else
                {
                    // Swap items
                    var temp = slot;
                    inventory.Slots[slotIndex] = heldStack;
                    heldStack = temp;
                    Console.WriteLine($"[Inventory] Swapped items");
                }
            }
        }
        
        private void HandleRightClick()
        {
            if (!hoveredSlot.HasValue) return;
            
            int slotIndex = hoveredSlot.Value;
            var slot = inventory.Slots[slotIndex];
            
            if (!heldItemSlot.HasValue)
            {
                // Pick up half the stack
                if (slot.ItemId != 0 && slot.Count > 1)
                {
                    int halfCount = (slot.Count + 1) / 2; // Round up
                    heldStack = new ItemStack { ItemId = slot.ItemId, Count = halfCount };
                    heldItemSlot = slotIndex;
                    inventory.Slots[slotIndex].Count -= halfCount;
                    Console.WriteLine($"[Inventory] Picked up half stack ({halfCount}/{slot.Count})");
                }
            }
            else
            {
                // Place one item
                if (slot.ItemId == 0 || slot.ItemId == heldStack.ItemId)
                {
                    if (slot.Count < 64)
                    {
                        if (slot.ItemId == 0)
                        {
                            inventory.Slots[slotIndex] = new ItemStack { ItemId = heldStack.ItemId, Count = 1 };
                        }
                        else
                        {
                            inventory.Slots[slotIndex].Count++;
                        }
                        
                        heldStack.Count--;
                        if (heldStack.Count <= 0)
                        {
                            heldItemSlot = null;
                        }
                    }
                }
            }
        }
        
        public void Render(Matrix4 projection, MouseState mouse)
        {
            float alpha = openAnimation;
            
            // Set projection for text renderer
            textRenderer?.SetProjection(projection);
            
            // Use shader
            GL.UseProgram(shaderProgram);
            int projLoc = GL.GetUniformLocation(shaderProgram, "projection");
            GL.UniformMatrix4(projLoc, false, ref projection);
            
            // 1. Draw full-screen overlay
            DrawRect(0, 0, 10000, 10000, BG_OVERLAY * new Vector4(1, 1, 1, alpha));
            
            // 2. Draw main panel with shadow
            DrawRectWithShadow(panelPosition.X, panelPosition.Y, panelSize.X, panelSize.Y, 
                              PANEL_BG * new Vector4(1, 1, 1, alpha), 8f);
            
            // 3. Draw title bar
            DrawRect(panelPosition.X, panelPosition.Y, panelSize.X, TITLE_HEIGHT, 
                    new Vector4(0.2f, 0.24f, 0.3f, alpha));
            
            // Title text
            if (textRenderer != null)
            {
                textRenderer.DrawText("INVENTORY", 
                    new Vector2(panelPosition.X + PANEL_PADDING, panelPosition.Y + 12), 
                    1.2f, TEXT_PRIMARY * new Vector4(1, 1, 1, alpha));
                
                // Close hint
                string hint = "[ESC] or [I] to close";
                var hintSize = textRenderer.MeasureText(hint, 0.55f);
                textRenderer.DrawText(hint, 
                    new Vector2(panelPosition.X + panelSize.X - hintSize.X - PANEL_PADDING, 
                               panelPosition.Y + 16), 
                    0.55f, TEXT_HINT * new Vector4(1, 1, 1, alpha));
            }
            
            // 4. Draw section labels
            if (textRenderer != null)
            {
                float labelY = panelPosition.Y + TITLE_HEIGHT + PANEL_PADDING - 20;
                textRenderer.DrawText("Storage", 
                    new Vector2(panelPosition.X + PANEL_PADDING, labelY), 
                    0.7f, TEXT_SECONDARY * new Vector4(1, 1, 1, alpha));
                
                float inventoryHeight = INVENTORY_ROWS * SLOT_SIZE + (INVENTORY_ROWS - 1) * SLOT_SPACING;
                float hotbarLabelY = labelY + inventoryHeight + SECTION_SPACING - 20;
                textRenderer.DrawText("Hotbar", 
                    new Vector2(panelPosition.X + PANEL_PADDING, hotbarLabelY), 
                    0.7f, TEXT_SECONDARY * new Vector4(1, 1, 1, alpha));
            }
            
            // 5. Draw inventory slots
            for (int i = 0; i < INVENTORY_ROWS * INVENTORY_COLS; i++)
            {
                DrawSlot(i, false, alpha);
            }
            
            // 6. Draw hotbar slots
            for (int i = 0; i < HOTBAR_SLOTS; i++)
            {
                DrawSlot(INVENTORY_ROWS * INVENTORY_COLS + i, true, alpha);
            }
            
            // 7. Draw item text/counts
            if (textRenderer != null)
            {
                for (int i = 0; i < Inventory.TOTAL_SIZE; i++)
                {
                    DrawSlotContent(i, alpha);
                }
            }
            
            // 8. Draw tooltip
            if (hoveredSlot.HasValue && hoverTime >= TOOLTIP_DELAY && textRenderer != null)
            {
                DrawTooltip(hoveredSlot.Value, mouse, alpha);
            }
            
            // 9. Draw held item following cursor
            if (heldItemSlot.HasValue && textRenderer != null)
            {
                DrawHeldItem(mouse, alpha);
            }
        }
        
        private void DrawSlot(int index, bool isHotbar, float alpha)
        {
            Vector2 pos = isHotbar ? 
                GetHotbarSlotPosition(index - INVENTORY_ROWS * INVENTORY_COLS) : 
                GetInventorySlotPosition(index);
            
            bool isHovered = hoveredSlot == index;
            bool isHeld = heldItemSlot == index;
            bool isSelected = isHotbar && (index - INVENTORY_ROWS * INVENTORY_COLS) == inventory.SelectedHotbarSlot;
            
            if (isHeld) return; // Don't draw slot we're holding
            
            // Background
            Vector4 bgColor = SLOT_BG * new Vector4(1, 1, 1, alpha);
            DrawRect(pos.X, pos.Y, SLOT_SIZE, SLOT_SIZE, bgColor);
            
            // Border with hover effect
            Vector4 borderColor = isHovered ? SLOT_HOVER : SLOT_BORDER;
            if (isSelected) borderColor = HOTBAR_HIGHLIGHT;
            
            float borderWidth = isSelected ? 3f : 2f;
            DrawBorder(pos.X, pos.Y, SLOT_SIZE, SLOT_SIZE, borderWidth, 
                      borderColor * new Vector4(1, 1, 1, alpha));
            
            // Glow effect on hover - MUCH MORE VISIBLE
            if (isHovered)
            {
                DrawRect(pos.X + 2, pos.Y + 2, SLOT_SIZE - 4, SLOT_SIZE - 4, 
                        new Vector4(1, 1, 1, 0.15f * alpha));
                
                // Extra bright border on hover
                DrawBorder(pos.X - 1, pos.Y - 1, SLOT_SIZE + 2, SLOT_SIZE + 2, 2f,
                          new Vector4(1, 1, 1, 0.5f * alpha));
            }
        }
        
        private void DrawSlotContent(int index, float alpha)
        {
            var slot = inventory.Slots[index];
            if (slot.ItemId == 0 || heldItemSlot == index) return;
            
            Vector2 pos = index >= INVENTORY_ROWS * INVENTORY_COLS ? 
                GetHotbarSlotPosition(index - INVENTORY_ROWS * INVENTORY_COLS) : 
                GetInventorySlotPosition(index);
            
            // Item name (small, top)
            string itemName = GetItemName(slot.ItemId);
            textRenderer!.DrawText(itemName, 
                new Vector2(pos.X + 4, pos.Y + 4), 
                0.5f, TEXT_PRIMARY * new Vector4(1, 1, 1, alpha * 0.9f));
            
            // Count (large, bottom-right)
            if (slot.Count > 1)
            {
                string count = slot.Count.ToString();
                var countSize = textRenderer.MeasureText(count, 1.0f);
                
                // Shadow
                textRenderer.DrawText(count, 
                    new Vector2(pos.X + SLOT_SIZE - countSize.X - 5, pos.Y + SLOT_SIZE - countSize.Y - 3),
                    1.0f, new Vector4(0, 0, 0, 0.7f * alpha));
                
                // Count
                textRenderer.DrawText(count, 
                    new Vector2(pos.X + SLOT_SIZE - countSize.X - 6, pos.Y + SLOT_SIZE - countSize.Y - 4),
                    1.0f, TEXT_PRIMARY * new Vector4(1, 1, 1, alpha));
            }
        }
        
        private void DrawTooltip(int slotIndex, MouseState mouse, float alpha)
        {
            var slot = inventory.Slots[slotIndex];
            if (slot.ItemId == 0) return;
            
            string itemName = GetItemName(slot.ItemId);
            string itemDesc = GetItemDescription(slot.ItemId);
            
            // Measure tooltip size
            var nameSize = textRenderer!.MeasureText(itemName, 0.8f);
            var descSize = textRenderer.MeasureText(itemDesc, 0.6f);
            
            float tooltipWidth = Math.Max(nameSize.X, descSize.X) + 24;
            float tooltipHeight = nameSize.Y + descSize.Y + 32;
            
            // Position near mouse but avoid edges
            Vector2 tooltipPos = new Vector2(mouse.X + 12, mouse.Y + 12);
            
            // Keep on screen
            if (tooltipPos.X + tooltipWidth > panelPosition.X + panelSize.X)
                tooltipPos.X = mouse.X - tooltipWidth - 12;
            if (tooltipPos.Y + tooltipHeight > panelPosition.Y + panelSize.Y)
                tooltipPos.Y = mouse.Y - tooltipHeight - 12;
            
            // Draw tooltip background with shadow
            DrawRectWithShadow(tooltipPos.X, tooltipPos.Y, tooltipWidth, tooltipHeight,
                              new Vector4(0.15f, 0.17f, 0.22f, 0.95f * alpha), 4f);
            
            // Draw border
            DrawBorder(tooltipPos.X, tooltipPos.Y, tooltipWidth, tooltipHeight, 1f,
                      new Vector4(0.4f, 0.45f, 0.55f, alpha));
            
            // Draw text
            textRenderer.DrawText(itemName, 
                new Vector2(tooltipPos.X + 12, tooltipPos.Y + 8), 
                0.8f, new Vector4(1f, 0.95f, 0.6f, alpha));
            
            textRenderer.DrawText(itemDesc, 
                new Vector2(tooltipPos.X + 12, tooltipPos.Y + 8 + nameSize.Y + 4), 
                0.6f, TEXT_SECONDARY * new Vector4(1, 1, 1, alpha));
        }
        
        private void DrawHeldItem(MouseState mouse, float alpha)
        {
            Vector2 pos = new Vector2(mouse.X - SLOT_SIZE / 2, mouse.Y - SLOT_SIZE / 2);
            
            // Draw translucent slot background
            DrawRect(pos.X, pos.Y, SLOT_SIZE, SLOT_SIZE, 
                    SLOT_BG * new Vector4(1, 1, 1, 0.8f * alpha));
            DrawBorder(pos.X, pos.Y, SLOT_SIZE, SLOT_SIZE, 2f,
                      SLOT_SELECTED * new Vector4(1, 1, 1, alpha));
            
            // Draw item info
            string itemName = GetItemName(heldStack.ItemId);
            textRenderer!.DrawText(itemName, 
                new Vector2(pos.X + 4, pos.Y + 4), 
                0.5f, TEXT_PRIMARY * new Vector4(1, 1, 1, alpha));
            
            if (heldStack.Count > 1)
            {
                string count = heldStack.Count.ToString();
                var countSize = textRenderer.MeasureText(count, 1.0f);
                textRenderer.DrawText(count, 
                    new Vector2(pos.X + SLOT_SIZE - countSize.X - 6, pos.Y + SLOT_SIZE - countSize.Y - 4),
                    1.0f, TEXT_PRIMARY * new Vector4(1, 1, 1, alpha));
            }
        }
        
        private void DrawRect(float x, float y, float w, float h, Vector4 color)
        {
            float[] vertices = new float[]
            {
                x,     y,     color.X, color.Y, color.Z, color.W,
                x + w, y,     color.X, color.Y, color.Z, color.W,
                x + w, y + h, color.X, color.Y, color.Z, color.W,
                
                x,     y,     color.X, color.Y, color.Z, color.W,
                x + w, y + h, color.X, color.Y, color.Z, color.W,
                x,     y + h, color.X, color.Y, color.Z, color.W,
            };
            
            GL.BindVertexArray(vao);
            GL.BindBuffer(BufferTarget.ArrayBuffer, vbo);
            GL.BufferData(BufferTarget.ArrayBuffer, vertices.Length * sizeof(float), 
                         vertices, BufferUsageHint.DynamicDraw);
            GL.DrawArrays(PrimitiveType.Triangles, 0, 6);
        }
        
        private void DrawBorder(float x, float y, float w, float h, float thickness, Vector4 color)
        {
            DrawRect(x, y, w, thickness, color);
            DrawRect(x, y + h - thickness, w, thickness, color);
            DrawRect(x, y, thickness, h, color);
            DrawRect(x + w - thickness, y, thickness, h, color);
        }
        
        private void DrawRectWithShadow(float x, float y, float w, float h, Vector4 color, float cornerRadius)
        {
            // Simple shadow (offset rect)
            float shadowOffset = 8f;
            DrawRect(x + shadowOffset, y + shadowOffset, w, h, 
                    new Vector4(0, 0, 0, 0.3f * color.W));
            
            // Main rect
            DrawRect(x, y, w, h, color);
        }
        
        private string GetItemName(int itemId)
        {
            return itemId switch
            {
                1 => "Stone",
                2 => "Dirt",
                3 => "Grass",
                4 => "Sand",
                5 => "Snow",
                6 => "Gravel",
                7 => "Wood",
                8 => "Leaves",
                _ => $"Item {itemId}"
            };
        }
        
        private string GetItemDescription(int itemId)
        {
            return itemId switch
            {
                1 => "Basic building block",
                2 => "Soft ground material",
                3 => "Surface layer block",
                4 => "Granular material",
                5 => "Frozen water crystals",
                6 => "Loose rock pieces",
                7 => "Tree material",
                8 => "Foliage block",
                _ => "Unknown item"
            };
        }
        
        public void OnOpen()
        {
            openAnimation = 0f;
            hoverTime = 0f;
            wasLeftPressed = false;
            wasRightPressed = false;
            Console.WriteLine($"[EnhancedInventoryUI] Opened - panel will be at ({panelSize.X}x{panelSize.Y})");
        }
        
        public void OnClose()
        {
            // Drop held item back to original slot if we have one
            if (heldItemSlot.HasValue)
            {
                inventory.Slots[heldItemSlot.Value] = heldStack;
                heldItemSlot = null;
            }
        }
    }
}
