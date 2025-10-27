using System.Collections.Concurrent;

// Net/Server/InventoryManager.cs
namespace Aetheris
{
    public class ServerInventoryManager
    {
        private readonly ConcurrentDictionary<string, Inventory> playerInventories = new();

        public Inventory GetOrCreateInventory(string playerId)
        {
            return playerInventories.GetOrAdd(playerId, _ => new Inventory());
        }

        public byte[] SerializeInventory(Inventory inv)
        {
            using var ms = new MemoryStream();
            using var writer = new BinaryWriter(ms);

            writer.Write(inv.SelectedHotbarSlot);
            writer.Write(Inventory.TOTAL_SIZE);

            foreach (var slot in inv.Slots)
            {
                writer.Write(slot.ItemId);
                writer.Write(slot.Count);
                writer.Write(slot.Metadata?.Length ?? 0);
                if (slot.Metadata != null)
                    writer.Write(slot.Metadata);
            }

            return ms.ToArray();
        }

        public Inventory DeserializeInventory(byte[] data)
        {
            using var ms = new MemoryStream(data);
            using var reader = new BinaryReader(ms);

            var inv = new Inventory();
            inv.SelectedHotbarSlot = reader.ReadInt32();
            int slotCount = reader.ReadInt32();

            for (int i = 0; i < slotCount; i++)
            {
                inv.Slots[i].ItemId = reader.ReadInt32();
                inv.Slots[i].Count = reader.ReadInt32();
                int metaLen = reader.ReadInt32();
                if (metaLen > 0)
                    inv.Slots[i].Metadata = reader.ReadBytes(metaLen);
            }

            return inv;
        }
    }
}
