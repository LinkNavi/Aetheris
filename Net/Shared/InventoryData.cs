// Net/Shared/InventoryData.cs - Enhanced with armor and totem slots
namespace Aetheris
{
    public struct ItemStack
    {
        public int ItemId;
        public int Count;
        public byte[]? Metadata;
        
        public ItemStack(int itemId, int count, byte[]? metadata = null)
        {
            ItemId = itemId;
            Count = count;
            Metadata = metadata;
        }
        
        public static ItemStack Empty => new ItemStack(0, 0, null);
    }

    public class Inventory
    {
        public const int HOTBAR_SIZE = 9;
        public const int MAIN_SIZE = 27;
        public const int ARMOR_SIZE = 4;   // Head, Chest, Legs, Feet
        public const int TOTEM_SIZE = 5;   // 5 totem slots
        public const int TOTAL_SIZE = HOTBAR_SIZE + MAIN_SIZE + ARMOR_SIZE + TOTEM_SIZE;

        // Slot layout:
        // [0-8]     = Hotbar (9 slots)
        // [9-35]    = Main inventory (27 slots)
        // [36-39]   = Armor (4 slots)
        // [40-44]   = Totems (5 slots)

        private ItemStack[] slots;
        public int SelectedHotbarSlot { get; set; } = 0;

        public Inventory()
        {
            slots = new ItemStack[TOTAL_SIZE];
            for (int i = 0; i < TOTAL_SIZE; i++)
            {
                slots[i] = ItemStack.Empty;
            }
        }

        // Safe slot access
        public ItemStack GetSlot(int index)
        {
            if (index < 0 || index >= TOTAL_SIZE) return ItemStack.Empty;
            return slots[index];
        }

        public void SetSlot(int index, ItemStack item)
        {
            if (index < 0 || index >= TOTAL_SIZE) return;
            slots[index] = item;
        }

        public ItemStack[] Slots => slots;

        // Armor slot helpers
        public static int GetArmorSlotIndex(int armorSlot) => HOTBAR_SIZE + MAIN_SIZE + armorSlot;
        
        public ItemStack GetArmorSlot(int armorSlot) => GetSlot(GetArmorSlotIndex(armorSlot));
        public void SetArmorSlot(int armorSlot, ItemStack item) => SetSlot(GetArmorSlotIndex(armorSlot), item);
        
        // Totem slot helpers
        public static int GetTotemSlotIndex(int totemSlot) => HOTBAR_SIZE + MAIN_SIZE + ARMOR_SIZE + totemSlot;
        
        public ItemStack GetTotemSlot(int totemSlot) => GetSlot(GetTotemSlotIndex(totemSlot));
        public void SetTotemSlot(int totemSlot, ItemStack item) => SetSlot(GetTotemSlotIndex(totemSlot), item);

        public bool AddItem(int itemId, int count)
        {
            // Try stacking first (only in hotbar and main inventory)
            for (int i = 0; i < HOTBAR_SIZE + MAIN_SIZE; i++)
            {
                if (slots[i].ItemId == itemId && slots[i].Count < 64)
                {
                    int space = 64 - slots[i].Count;
                    int toAdd = Math.Min(space, count);
                    slots[i].Count += toAdd;
                    count -= toAdd;
                    if (count == 0) return true;
                }
            }

            // Find empty slot
            for (int i = 0; i < HOTBAR_SIZE + MAIN_SIZE; i++)
            {
                if (slots[i].ItemId == 0)
                {
                    slots[i] = new ItemStack(itemId, count);
                    return true;
                }
            }

            return false; // Inventory full
        }

        public bool RemoveItem(int itemId, int count)
        {
            int remaining = count;
            
            // Remove from hotbar and main inventory
            for (int i = 0; i < HOTBAR_SIZE + MAIN_SIZE && remaining > 0; i++)
            {
                if (slots[i].ItemId == itemId)
                {
                    int toRemove = Math.Min(slots[i].Count, remaining);
                    slots[i].Count -= toRemove;
                    remaining -= toRemove;
                    
                    if (slots[i].Count <= 0)
                    {
                        slots[i] = ItemStack.Empty;
                    }
                }
            }
            
            return remaining == 0;
        }

        public int CountItem(int itemId)
        {
            int total = 0;
            for (int i = 0; i < HOTBAR_SIZE + MAIN_SIZE; i++)
            {
                if (slots[i].ItemId == itemId)
                {
                    total += slots[i].Count;
                }
            }
            return total;
        }

        public bool HasItem(int itemId)
        {
            return CountItem(itemId) > 0;
        }

        public ItemStack GetSelectedItem()
        {
            return GetSlot(SelectedHotbarSlot);
        }
    }
}
