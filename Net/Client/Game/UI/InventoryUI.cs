using System;
using ImGuiNET;
using OpenTK.Windowing.GraphicsLibraryFramework;
using Aetheris.GameLogic;
using SysVector2 = System.Numerics.Vector2;
using SysVector4 = System.Numerics.Vector4;

namespace Aetheris.UI
{
    public class ImGuiInventoryUI
    {
        private Inventory inventory;
        private bool isOpen = false;
        private int hoveredSlot = -1;
        private int draggedSlot = -1;
        private ItemStack draggedItem = ItemStack.Empty;

        public ImGuiInventoryUI(Inventory inventory)
        {
            this.inventory = inventory;
        }

        public void Update(KeyboardState keyboard, MouseState mouse, OpenTK.Mathematics.Vector2i windowSize, float deltaTime)
        {
            // Inventory is updated in Render() with ImGui
        }

        public void Render(OpenTK.Mathematics.Vector2i windowSize)
        {
            if (!isOpen) return;

            // Center the inventory window
            float windowWidth = 900f;
            float windowHeight = 700f;
            ImGui.SetNextWindowPos(new SysVector2((windowSize.X - windowWidth) / 2, (windowSize.Y - windowHeight) / 2));
            ImGui.SetNextWindowSize(new SysVector2(windowWidth, windowHeight));
            
            ImGui.PushStyleVar(ImGuiStyleVar.WindowRounding, 10f);
            ImGui.PushStyleVar(ImGuiStyleVar.WindowPadding, new SysVector2(20, 20));
            ImGui.PushStyleColor(ImGuiCol.WindowBg, new SysVector4(0.12f, 0.12f, 0.14f, 0.95f));
            ImGui.PushStyleColor(ImGuiCol.TitleBg, new SysVector4(0.2f, 0.2f, 0.25f, 1.0f));
            ImGui.PushStyleColor(ImGuiCol.TitleBgActive, new SysVector4(0.25f, 0.25f, 0.3f, 1.0f));

            if (ImGui.Begin("Inventory", ref isOpen, ImGuiWindowFlags.NoCollapse | ImGuiWindowFlags.NoResize))
            {
                // Hotbar section
                ImGui.PushStyleColor(ImGuiCol.Text, new SysVector4(1.0f, 0.9f, 0.6f, 1.0f));
                ImGui.Text("HOTBAR");
                ImGui.PopStyleColor();
                ImGui.Separator();
                ImGui.Spacing();

                RenderInventoryGrid(0, Inventory.HOTBAR_SIZE, 9, "hotbar", true);

                ImGui.Spacing();
                ImGui.Spacing();

                // Main inventory section
                ImGui.PushStyleColor(ImGuiCol.Text, new SysVector4(0.8f, 1.0f, 0.8f, 1.0f));
                ImGui.Text("MAIN INVENTORY");
                ImGui.PopStyleColor();
                ImGui.Separator();
                ImGui.Spacing();

                RenderInventoryGrid(Inventory.HOTBAR_SIZE, Inventory.MAIN_SIZE, 9, "main", false);

                ImGui.Spacing();
                ImGui.Spacing();

                // Armor and Totems side by side
                ImGui.BeginGroup();
                {
                    // Armor section
                    ImGui.PushStyleColor(ImGuiCol.Text, new SysVector4(0.6f, 0.8f, 1.0f, 1.0f));
                    ImGui.Text("ARMOR");
                    ImGui.PopStyleColor();
                    ImGui.Separator();
                    ImGui.Spacing();

                    string[] armorLabels = { "Head", "Chest", "Legs", "Feet" };
                    RenderInventoryGrid(Inventory.HOTBAR_SIZE + Inventory.MAIN_SIZE, Inventory.ARMOR_SIZE, 4, "armor", false, armorLabels);
                }
                ImGui.EndGroup();

                ImGui.SameLine();
                ImGui.Spacing();
                ImGui.SameLine();

                ImGui.BeginGroup();
                {
                    // Totems section
                    ImGui.PushStyleColor(ImGuiCol.Text, new SysVector4(1.0f, 0.8f, 1.0f, 1.0f));
                    ImGui.Text("TOTEMS");
                    ImGui.PopStyleColor();
                    ImGui.Separator();
                    ImGui.Spacing();

                    string[] totemLabels = { "Totem 1", "Totem 2", "Totem 3", "Totem 4", "Totem 5" };
                    RenderInventoryGrid(Inventory.HOTBAR_SIZE + Inventory.MAIN_SIZE + Inventory.ARMOR_SIZE, Inventory.TOTEM_SIZE, 5, "totems", false, totemLabels);
                }
                ImGui.EndGroup();

                // Close hint
                ImGui.Spacing();
                ImGui.Spacing();
                ImGui.PushStyleColor(ImGuiCol.Text, new SysVector4(0.7f, 0.7f, 0.7f, 1.0f));
                ImGui.TextWrapped("Press E to close inventory");
                ImGui.PopStyleColor();
            }
            ImGui.End();

            ImGui.PopStyleColor(3);
            ImGui.PopStyleVar(2);
        }

        private void RenderInventoryGrid(int startSlot, int slotCount, int columns, string id, bool showNumbers, string[]? labels = null)
        {
            float slotSize = 70f;
            float spacing = 6f;

            ImGui.PushStyleVar(ImGuiStyleVar.ItemSpacing, new SysVector2(spacing, spacing));

            for (int i = 0; i < slotCount; i++)
            {
                if (i > 0 && i % columns != 0)
                    ImGui.SameLine();

                int slotIndex = startSlot + i;
                var item = inventory.GetSlot(slotIndex);
                bool isSelected = slotIndex == inventory.SelectedHotbarSlot && showNumbers;

                ImGui.PushID($"{id}_{i}");

                // Slot button styling
                if (isSelected)
                {
                    ImGui.PushStyleColor(ImGuiCol.Button, new SysVector4(0.4f, 0.35f, 0.2f, 1.0f));
                    ImGui.PushStyleColor(ImGuiCol.ButtonHovered, new SysVector4(0.5f, 0.45f, 0.3f, 1.0f));
                    ImGui.PushStyleColor(ImGuiCol.ButtonActive, new SysVector4(0.6f, 0.55f, 0.4f, 1.0f));
                    ImGui.PushStyleColor(ImGuiCol.Border, new SysVector4(1.0f, 0.8f, 0.3f, 1.0f));
                    ImGui.PushStyleVar(ImGuiStyleVar.FrameBorderSize, 3f);
                }
                else
                {
                    ImGui.PushStyleColor(ImGuiCol.Button, new SysVector4(0.18f, 0.18f, 0.2f, 1.0f));
                    ImGui.PushStyleColor(ImGuiCol.ButtonHovered, new SysVector4(0.25f, 0.25f, 0.28f, 1.0f));
                    ImGui.PushStyleColor(ImGuiCol.ButtonActive, new SysVector4(0.3f, 0.3f, 0.35f, 1.0f));
                    ImGui.PushStyleColor(ImGuiCol.Border, new SysVector4(0.4f, 0.4f, 0.45f, 0.8f));
                    ImGui.PushStyleVar(ImGuiStyleVar.FrameBorderSize, 2f);
                }

                bool clicked = ImGui.Button($"##slot{id}{i}", new SysVector2(slotSize, slotSize));

                ImGui.PopStyleVar();
                ImGui.PopStyleColor(4);

                // Handle click for swapping items
                if (clicked)
                {
                    HandleSlotClick(slotIndex);
                }

                // Check if hovered
                if (ImGui.IsItemHovered())
                {
                    hoveredSlot = slotIndex;
                }

                var drawList = ImGui.GetWindowDrawList();
                var pos = ImGui.GetItemRectMin();

                // Draw slot number or label
                if (showNumbers)
                {
                    drawList.AddText(new SysVector2(pos.X + 5, pos.Y + 5),
                        ImGui.ColorConvertFloat4ToU32(new SysVector4(1, 1, 1, 0.7f)),
                        (i + 1).ToString());
                }
                else if (labels != null && i < labels.Length)
                {
                    var labelSize = ImGui.CalcTextSize(labels[i]);
                    drawList.AddText(new SysVector2(pos.X + 5, pos.Y + 5),
                        ImGui.ColorConvertFloat4ToU32(new SysVector4(0.8f, 0.8f, 0.9f, 0.8f)),
                        labels[i]);
                }

                // Draw item
                if (item.ItemId > 0)
                {
                    var itemDef = ItemRegistry.Get(item.ItemId);
                    string name = itemDef?.Name ?? "???";

                    // Shorten name if too long
                    if (name.Length > 10)
                    {
                        name = name.Substring(0, 9) + "...";
                    }

                    // Center item name
                    var textSize = ImGui.CalcTextSize(name);
                    float textX = pos.X + (slotSize - textSize.X) / 2;
                    float textY = pos.Y + (slotSize - textSize.Y) / 2;

                    // Background for text readability
                    drawList.AddRectFilled(
                        new SysVector2(textX - 3, textY - 2),
                        new SysVector2(textX + textSize.X + 3, textY + textSize.Y + 2),
                        ImGui.ColorConvertFloat4ToU32(new SysVector4(0, 0, 0, 0.6f)),
                        3f);

                    drawList.AddText(new SysVector2(textX, textY),
                        ImGui.ColorConvertFloat4ToU32(new SysVector4(1, 1, 1, 1)),
                        name);

                    // Item count
                    if (item.Count > 1)
                    {
                        var countStr = item.Count.ToString();
                        var countSize = ImGui.CalcTextSize(countStr);
                        float countX = pos.X + slotSize - countSize.X - 5;
                        float countY = pos.Y + slotSize - countSize.Y - 5;

                        // Background for count
                        drawList.AddRectFilled(
                            new SysVector2(countX - 3, countY - 2),
                            new SysVector2(countX + countSize.X + 3, countY + countSize.Y + 2),
                            ImGui.ColorConvertFloat4ToU32(new SysVector4(0, 0, 0, 0.7f)),
                            3f);

                        drawList.AddText(new SysVector2(countX, countY),
                            ImGui.ColorConvertFloat4ToU32(new SysVector4(1, 1, 0.5f, 1)),
                            countStr);
                    }
                }

                ImGui.PopID();
            }

            ImGui.PopStyleVar();
        }

        private void HandleSlotClick(int slotIndex)
        {
            if (draggedSlot == -1)
            {
                // Start dragging
                var item = inventory.GetSlot(slotIndex);
                if (item.ItemId > 0)
                {
                    draggedSlot = slotIndex;
                    draggedItem = item;
                }
            }
            else
            {
                // Drop item
                if (draggedSlot == slotIndex)
                {
                    // Clicked same slot, cancel drag
                    draggedSlot = -1;
                    draggedItem = ItemStack.Empty;
                }
                else
                {
                    // Swap items
                    var targetItem = inventory.GetSlot(slotIndex);
                    inventory.SetSlot(slotIndex, draggedItem);
                    inventory.SetSlot(draggedSlot, targetItem);

                    draggedSlot = -1;
                    draggedItem = ItemStack.Empty;
                }
            }
        }

        public void ToggleInventory()
        {
            isOpen = !isOpen;
            
            // Reset drag state when closing
            if (!isOpen)
            {
                draggedSlot = -1;
                draggedItem = ItemStack.Empty;
                hoveredSlot = -1;
            }
        }

        public bool IsInventoryOpen() => isOpen;

        public int GetHoveredSlot() => hoveredSlot;

        public void Dispose()
        {
            // Nothing to dispose for ImGui version
        }
    }
}
