// Updated InventorySlot.cs with improved contrast (lighter bg) and optional debug log in Render

using System;
using OpenTK.Mathematics;

namespace Aetheris.UI
{
    public class InventorySlot : UIElement
    {
        public int Index { get; }
        public Aetheris.Inventory Inventory { get; }
        public Action<int>? OnSlotClicked; // Game will assign

        public InventorySlot(Aetheris.Inventory inventory, int index)
        {
            Inventory = inventory ?? throw new ArgumentNullException(nameof(inventory));
            Index = index;
            Size = new Vector2(72, 72);
        }

        public override void Render()
        {
            if (Manager == null) return;

            // Optional debug (can remove later)
            // Console.WriteLine($"[Debug] Rendering InventorySlot {Index} at pos={Position}, size={Size}, visible={Visible}");

            var bg = new Vector4(0.2f, 0.2f, 0.25f, 0.95f);  // Fixed: Lighter slot background for better visibility
            var border = IsHovered ? new Vector4(0.75f, 0.85f, 1f, 1f) : new Vector4(0.25f, 0.28f, 0.33f, 1f);
            Manager.DrawRect(Position.X, Position.Y, Size.X, Size.Y, bg, 6f);
            Manager.DrawBorder(Position.X, Position.Y, Size.X, Size.Y, 2f, border);

            var stack = Inventory.Slots[Index];
            if (stack.ItemId != 0 && Manager.TextRenderer != null)
            {
                // Draw item name (placeholder). Replace with sprite draw if you have atlas.
                string name = $"ID:{stack.ItemId}";
                Manager.TextRenderer.DrawText(name, new Vector2(Position.X + 8, Position.Y + 8), 0.7f);

                if (stack.Count > 1)
                {
                    string count = stack.Count.ToString();
                    var m = Manager.TextRenderer.MeasureText(count, 0.7f);
                    Manager.TextRenderer.DrawText(count, new Vector2(Position.X + Size.X - m.X - 8, Position.Y + Size.Y - m.Y - 6), 0.7f);
                }
            }

            // highlight selected hotbar slot if this index matches player's selected hotbar (if applicable)
            // Game will set Inventory.SelectedHotbarSlot; but we can visually hint it:
            if (Index == Inventory.SelectedHotbarSlot)
            {
                Manager.DrawBorder(Position.X - 4, Position.Y - 4, Size.X + 8, Size.Y + 8, 3f, new Vector4(0.9f, 0.8f, 0.2f, 1f));
            }
        }

        public override void OnClick()
        {
            OnSlotClicked?.Invoke(Index);
        }

        public override bool CanFocus => true;
    }
}
