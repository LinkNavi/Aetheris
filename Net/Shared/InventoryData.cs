// Net/Shared/InventoryData.cs
namespace Aetheris
{
    public struct ItemStack
    {
        public int ItemId;
        public int Count;
        public byte[] Metadata; // For tools durability, etc.
    }

    public class Inventory
    {
        public const int HOTBAR_SIZE = 9;
        public const int MAIN_SIZE = 27;
        public const int TOTAL_SIZE = HOTBAR_SIZE + MAIN_SIZE;

        public ItemStack[] Slots { get; private set; }
        public int SelectedHotbarSlot { get; set; } = 0;

        public Inventory()
        {
            Slots = new ItemStack[TOTAL_SIZE];
        }

        public bool AddItem(int itemId, int count)
        {
            // Try stacking first
            for (int i = 0; i < TOTAL_SIZE; i++)
            {
                if (Slots[i].ItemId == itemId && Slots[i].Count < 64)
                {
                    int space = 64 - Slots[i].Count;
                    int toAdd = Math.Min(space, count);
                    Slots[i].Count += toAdd;
                    count -= toAdd;
                    if (count == 0) return true;
                }
            }

            // Find empty slot
            for (int i = 0; i < TOTAL_SIZE; i++)
            {
                if (Slots[i].ItemId == 0)
                {
                    Slots[i].ItemId = itemId;
                    Slots[i].Count = count;
                    return true;
                }
            }

            return false; // Inventory full
        }


    }
}
