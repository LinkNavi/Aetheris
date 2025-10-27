
using System;
using OpenTK.Graphics.OpenGL4;
using OpenTK.Mathematics;
using OpenTK.Windowing.GraphicsLibraryFramework;
using Aetheris.UI;

namespace Aetheris
{
    public class SimpleInventoryRenderer
    {
        private readonly Inventory inventory;
        private readonly int shaderProgram;
        private readonly int vao;
        private readonly int vbo;
        private Aetheris.UI.ITextRenderer? textRenderer;
        
        private Vector2 panelPosition;
        private Vector2 panelSize;
        private const int COLS = 9;
        private const float SLOT_SIZE = 72f;
        private const float SPACING = 8f;
        private const float PADDING = 20f;
        
        private int? hoveredSlot = null;
        private int? heldItemSlot = null;
        private ItemStack heldStack;
        
        public SimpleInventoryRenderer(Inventory inventory, int shaderProgram, int vao, int vbo)
        {
            this.inventory = inventory;
            this.shaderProgram = shaderProgram;
            this.vao = vao;
            this.vbo = vbo;
            
            int rows = (int)Math.Ceiling(Inventory.TOTAL_SIZE / (float)COLS);
            panelSize = new Vector2(
                COLS * SLOT_SIZE + (COLS - 1) * SPACING + PADDING * 2,
                rows * SLOT_SIZE + (rows - 1) * SPACING + PADDING * 2
            );
            
            Console.WriteLine($"[InventoryRenderer] Created with shader={shaderProgram}, vao={vao}, vbo={vbo}");
            Console.WriteLine($"[InventoryRenderer] Panel size: {panelSize}");
        }
        
        public void SetTextRenderer(Aetheris.UI.ITextRenderer renderer)
        {
            textRenderer = renderer;
        }
        
        public void Update(MouseState mouse, int screenWidth, int screenHeight)
        {
            panelPosition = new Vector2(
                (screenWidth - panelSize.X) / 2f,
                (screenHeight - panelSize.Y) / 2f
            );
            
            hoveredSlot = null;
            Vector2 mousePos = new Vector2(mouse.X, mouse.Y);
            
            // Check slots with title bar offset
            float yOffset = 50f;
            
            for (int i = 0; i < Inventory.TOTAL_SIZE; i++)
            {
                Vector2 slotPos = GetSlotPosition(i, yOffset);
                if (mousePos.X >= slotPos.X && mousePos.X <= slotPos.X + SLOT_SIZE &&
                    mousePos.Y >= slotPos.Y && mousePos.Y <= slotPos.Y + SLOT_SIZE)
                {
                    hoveredSlot = i;
                    break;
                }
            }
            
            if (mouse.IsButtonPressed(MouseButton.Left) && hoveredSlot.HasValue)
            {
                HandleSlotClick(hoveredSlot.Value);
            }
        }
        
        private void HandleSlotClick(int slotIndex)
        {
            var slot = inventory.Slots[slotIndex];
            
            if (!heldItemSlot.HasValue)
            {
                if (slot.ItemId != 0)
                {
                    heldStack = slot;
                    heldItemSlot = slotIndex;
                    inventory.Slots[slotIndex] = default;
                    Console.WriteLine($"[Inventory] Picked up ID:{heldStack.ItemId} x{heldStack.Count}");
                }
                return;
            }
            
            if (slot.ItemId == 0)
            {
                inventory.Slots[slotIndex] = heldStack;
                heldItemSlot = null;
                Console.WriteLine($"[Inventory] Placed item in slot {slotIndex}");
            }
            else if (slot.ItemId == heldStack.ItemId)
            {
                int canAdd = Math.Min(64 - slot.Count, heldStack.Count);
                inventory.Slots[slotIndex].Count += canAdd;
                heldStack.Count -= canAdd;
                
                if (heldStack.Count <= 0)
                {
                    heldItemSlot = null;
                    Console.WriteLine($"[Inventory] Merged items, done holding");
                }
                else
                {
                    Console.WriteLine($"[Inventory] Merged, still holding {heldStack.Count}");
                }
            }
            else
            {
                var temp = slot;
                inventory.Slots[slotIndex] = heldStack;
                heldStack = temp;
                Console.WriteLine($"[Inventory] Swapped items");
            }
        }
        
        private Vector2 GetSlotPosition(int index)
        {
            int col = index % COLS;
            int row = index / COLS;
            
            return new Vector2(
                panelPosition.X + PADDING + col * (SLOT_SIZE + SPACING),
                panelPosition.Y + PADDING + row * (SLOT_SIZE + SPACING)
            );
        }
        
        public void Render(Matrix4 projection, MouseState mouse)
        {
            // Set text renderer projection first
            textRenderer?.SetProjection(projection);
            
            // Activate our shader
            GL.UseProgram(shaderProgram);
            int projLoc = GL.GetUniformLocation(shaderProgram, "projection");
            GL.UniformMatrix4(projLoc, false, ref projection);
            
            // Draw panel background with slight transparency
            DrawRect(panelPosition.X, panelPosition.Y, panelSize.X, panelSize.Y, 
                     new Vector4(0.12f, 0.14f, 0.18f, 0.98f));
            
            // Draw thick outer border
            DrawBorder(panelPosition.X, panelPosition.Y, panelSize.X, panelSize.Y, 4f,
                      new Vector4(0.4f, 0.45f, 0.55f, 1f));
            
            // Draw title bar background
            DrawRect(panelPosition.X, panelPosition.Y, panelSize.X, 40f, 
                     new Vector4(0.2f, 0.24f, 0.3f, 1f));
            
            // Draw title bar bottom border
            DrawRect(panelPosition.X, panelPosition.Y + 40f, panelSize.X, 2f, 
                     new Vector4(0.5f, 0.55f, 0.65f, 1f));
            
            // Draw title text
            if (textRenderer != null)
            {
                textRenderer.DrawText("INVENTORY", 
                    new Vector2(panelPosition.X + PADDING, panelPosition.Y + 8), 
                    1.2f, new Vector4(0.9f, 0.92f, 0.95f, 1f));
                
                // Draw close hint
                string closeHint = "Press [ESC] or [I] to close";
                var hintSize = textRenderer.MeasureText(closeHint, 0.6f);
                textRenderer.DrawText(closeHint, 
                    new Vector2(panelPosition.X + panelSize.X - hintSize.X - PADDING, 
                               panelPosition.Y + 12), 
                    0.6f, new Vector4(0.6f, 0.65f, 0.7f, 1f));
            }
            
            // Draw all slots (with title bar offset)
            for (int i = 0; i < Inventory.TOTAL_SIZE; i++)
            {
                DrawSlotBackground(i, 50f); // 50f offset for title bar
            }
            
            // Draw text
            if (textRenderer != null)
            {
                for (int i = 0; i < Inventory.TOTAL_SIZE; i++)
                {
                    DrawSlotText(i, 50f);
                }
            }
            
            // Draw held item cursor
            if (heldItemSlot.HasValue && textRenderer != null)
            {
                Vector2 mousePos = new Vector2(mouse.X, mouse.Y);
                
                // Draw shadow/outline for held item
                string info = $"ID:{heldStack.ItemId} x{heldStack.Count}";
                textRenderer.DrawText(info, mousePos + new Vector2(9, 9), 0.9f, 
                                    new Vector4(0, 0, 0, 0.5f));
                textRenderer.DrawText(info, mousePos + new Vector2(8, 8), 0.9f, 
                                    new Vector4(1, 0.95f, 0.5f, 1));
            }
        }
        
        private Vector2 GetSlotPosition(int index, float yOffset = 0f)
        {
            int col = index % COLS;
            int row = index / COLS;
            
            return new Vector2(
                panelPosition.X + PADDING + col * (SLOT_SIZE + SPACING),
                panelPosition.Y + PADDING + yOffset + row * (SLOT_SIZE + SPACING)
            );
        }
        
        private void DrawSlotBackground(int index, float yOffset = 0f)
        {
            Vector2 pos = GetSlotPosition(index, yOffset);
            
            if (heldItemSlot.HasValue && heldItemSlot.Value == index)
                return;
            
            GL.UseProgram(shaderProgram);
            
            // Darker background for slots
            Vector4 bgColor = new Vector4(0.08f, 0.09f, 0.12f, 0.95f);
            DrawRect(pos.X, pos.Y, SLOT_SIZE, SLOT_SIZE, bgColor);
            
            // Highlight on hover
            Vector4 borderColor = (hoveredSlot == index) 
                ? new Vector4(0.8f, 0.85f, 1f, 1f) 
                : new Vector4(0.25f, 0.28f, 0.35f, 1f);
            DrawBorder(pos.X, pos.Y, SLOT_SIZE, SLOT_SIZE, 2f, borderColor);
            
            // Selected hotbar slot indicator
            if (index == inventory.SelectedHotbarSlot)
            {
                DrawBorder(pos.X - 4, pos.Y - 4, SLOT_SIZE + 8, SLOT_SIZE + 8, 3f,
                          new Vector4(1f, 0.85f, 0.3f, 1f));
            }
        }
        
        private void DrawSlotText(int index, float yOffset = 0f)
        {
            Vector2 pos = GetSlotPosition(index, yOffset);
            var slot = inventory.Slots[index];
            
            if (heldItemSlot.HasValue && heldItemSlot.Value == index)
                return;
            
            if (slot.ItemId != 0)
            {
                string itemName = $"ID:{slot.ItemId}";
                textRenderer!.DrawText(itemName, new Vector2(pos.X + 6, pos.Y + 6), 0.7f,
                                    new Vector4(0.95f, 0.97f, 1f, 1));
                
                if (slot.Count > 1)
                {
                    string count = slot.Count.ToString();
                    var countSize = textRenderer.MeasureText(count, 0.9f);
                    
                    // Shadow for count
                    textRenderer.DrawText(count, 
                        new Vector2(pos.X + SLOT_SIZE - countSize.X - 5, 
                                   pos.Y + SLOT_SIZE - countSize.Y - 3),
                        0.9f, new Vector4(0, 0, 0, 0.7f));
                    
                    // Count text
                    textRenderer.DrawText(count, 
                        new Vector2(pos.X + SLOT_SIZE - countSize.X - 6, 
                                   pos.Y + SLOT_SIZE - countSize.Y - 4),
                        0.9f, new Vector4(1, 1, 1, 1));
                }
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
    }
}
