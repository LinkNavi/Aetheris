// Test version - draws a simple test rectangle first
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
        
        private bool isVisible = false;
        private Vector2 panelPosition;
        private Vector2 panelSize;
        private const int COLS = 9;
        private const float SLOT_SIZE = 72f;
        private const float SPACING = 8f;
        private const float PADDING = 20f;
        
        private int? hoveredSlot = null;
        private int? heldItemSlot = null;
        private ItemStack heldStack;
        
        private bool firstRender = true;
        
        public bool IsVisible => isVisible;
        
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
        
        public void Toggle()
        {
            isVisible = !isVisible;
            Console.WriteLine($"[SimpleInventory] Toggled to: {isVisible}");
        }
        
        public void Update(MouseState mouse, int screenWidth, int screenHeight)
        {
            if (!isVisible) return;
            
            panelPosition = new Vector2(
                (screenWidth - panelSize.X) / 2f,
                (screenHeight - panelSize.Y) / 2f
            );
            
            hoveredSlot = null;
            Vector2 mousePos = new Vector2(mouse.X, mouse.Y);
            
            for (int i = 0; i < Inventory.TOTAL_SIZE; i++)
            {
                Vector2 slotPos = GetSlotPosition(i);
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
            if (!isVisible) return;
            
            // Set text renderer projection first
            textRenderer?.SetProjection(projection);
            
            // Activate our shader
            GL.UseProgram(shaderProgram);
            int projLoc = GL.GetUniformLocation(shaderProgram, "projection");
            GL.UniformMatrix4(projLoc, false, ref projection);
            
            if (firstRender)
            {
                Console.WriteLine($"[InventoryRenderer] Projection matrix:");
                Console.WriteLine($"  [{projection.M11}, {projection.M12}, {projection.M13}, {projection.M14}]");
                Console.WriteLine($"  [{projection.M21}, {projection.M22}, {projection.M23}, {projection.M24}]");
                Console.WriteLine($"  [{projection.M31}, {projection.M32}, {projection.M33}, {projection.M34}]");
                Console.WriteLine($"  [{projection.M41}, {projection.M42}, {projection.M43}, {projection.M44}]");
                
                // Draw a BIG RED TEST RECTANGLE at top-left corner
                Console.WriteLine($"[InventoryRenderer] Drawing TEST RECTANGLE at (100, 100, 300, 300) - should be BRIGHT RED");
                DrawRect(100f, 100f, 300f, 300f, new Vector4(1f, 0f, 0f, 1f));
                
                // Check GL errors
                OpenTK.Graphics.OpenGL4.ErrorCode err = GL.GetError();
                if (err != OpenTK.Graphics.OpenGL4.ErrorCode.NoError)
                {
                    Console.WriteLine($"[InventoryRenderer] GL ERROR after test rect: {err}");
                }
                
                firstRender = false;
            }
            
            // Draw panel background
            DrawRect(panelPosition.X, panelPosition.Y, panelSize.X, panelSize.Y, 
                     new Vector4(0.05f, 0.05f, 0.07f, 0.95f));
            
            // Draw panel border
            DrawBorder(panelPosition.X, panelPosition.Y, panelSize.X, panelSize.Y, 3f,
                      new Vector4(0.3f, 0.3f, 0.4f, 1f));
            
            // Draw all slots
            for (int i = 0; i < Inventory.TOTAL_SIZE; i++)
            {
                DrawSlotBackground(i);
            }
            
            // Draw text
            if (textRenderer != null)
            {
                for (int i = 0; i < Inventory.TOTAL_SIZE; i++)
                {
                    DrawSlotText(i);
                }
            }
            
            // Draw held item cursor
            if (heldItemSlot.HasValue && textRenderer != null)
            {
                Vector2 mousePos = new Vector2(mouse.X, mouse.Y);
                string info = $"ID:{heldStack.ItemId} x{heldStack.Count}";
                textRenderer.DrawText(info, mousePos + new Vector2(8, 8), 0.8f, 
                                    new Vector4(1, 1, 0.5f, 1));
            }
        }
        
        private void DrawSlotBackground(int index)
        {
            Vector2 pos = GetSlotPosition(index);
            
            if (heldItemSlot.HasValue && heldItemSlot.Value == index)
                return;
            
            GL.UseProgram(shaderProgram);
            
            Vector4 bgColor = new Vector4(0.15f, 0.15f, 0.18f, 0.95f);
            DrawRect(pos.X, pos.Y, SLOT_SIZE, SLOT_SIZE, bgColor);
            
            Vector4 borderColor = (hoveredSlot == index) 
                ? new Vector4(0.7f, 0.8f, 1f, 1f) 
                : new Vector4(0.3f, 0.35f, 0.4f, 1f);
            DrawBorder(pos.X, pos.Y, SLOT_SIZE, SLOT_SIZE, 2f, borderColor);
            
            if (index == inventory.SelectedHotbarSlot)
            {
                DrawBorder(pos.X - 3, pos.Y - 3, SLOT_SIZE + 6, SLOT_SIZE + 6, 3f,
                          new Vector4(1f, 0.8f, 0.2f, 1f));
            }
        }
        
        private void DrawSlotText(int index)
        {
            Vector2 pos = GetSlotPosition(index);
            var slot = inventory.Slots[index];
            
            if (heldItemSlot.HasValue && heldItemSlot.Value == index)
                return;
            
            if (slot.ItemId != 0)
            {
                string itemName = $"ID:{slot.ItemId}";
                textRenderer!.DrawText(itemName, new Vector2(pos.X + 6, pos.Y + 6), 0.7f,
                                    new Vector4(1, 1, 1, 1));
                
                if (slot.Count > 1)
                {
                    string count = slot.Count.ToString();
                    var countSize = textRenderer.MeasureText(count, 0.8f);
                    textRenderer.DrawText(count, 
                        new Vector2(pos.X + SLOT_SIZE - countSize.X - 6, 
                                   pos.Y + SLOT_SIZE - countSize.Y - 4),
                        0.8f, new Vector4(1, 1, 1, 1));
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
