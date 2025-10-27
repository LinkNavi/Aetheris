
using System.Collections.Generic;
using OpenTK.Mathematics;

namespace Aetheris.UI
{
    public class InventoryPanel : Panel
    {
        public Aetheris.Inventory Inventory { get; }
        public List<InventorySlot> Slots { get; } = new List<InventorySlot>();

        public InventoryPanel(Aetheris.Inventory inv, Vector2 slotSize, float spacing)
        {
            Inventory = inv;
            int cols = Aetheris.Inventory.HOTBAR_SIZE; // show hotbar width or choose 9
            int rows = (int)System.Math.Ceiling(Aetheris.Inventory.TOTAL_SIZE / (float)cols);

            float panelWidth = cols * slotSize.X + (cols - 1) * spacing + 40;
            float panelHeight = rows * slotSize.Y + (rows - 1) * spacing + 60;

            Size = new Vector2(panelWidth, panelHeight);
            CornerRadius = 8f;
            BackgroundColor = new Vector4(0.05f, 0.05f, 0.07f, 0.95f);

            // create slot placeholders; positions will be set in LayoutSlots()
            for (int i = 0; i < Aetheris.Inventory.TOTAL_SIZE; i++)
            {
                var s = new InventorySlot(inv, i) { Size = slotSize, Position = Vector2.Zero };
                Slots.Add(s);
            }
        }

        public void LayoutSlots(Vector2 panelPosition, Vector2 slotSize, float spacing)
        {
            Position = panelPosition;
            float startX = Position.X + 20f;
            float startY = Position.Y + 20f;

            int cols = Aetheris.Inventory.HOTBAR_SIZE;
            for (int i = 0; i < Slots.Count; i++)
            {
                int x = i % cols;
                int y = i / cols;
                Slots[i].Position = new Vector2(startX + x * (slotSize.X + spacing),
                                                startY + y * (slotSize.Y + spacing));
                Slots[i].Size = slotSize;
            }
        }
    }
}
