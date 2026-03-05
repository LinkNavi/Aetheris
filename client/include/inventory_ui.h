#pragma once
#include <imgui.h>
#include <enet/enet.h>
#include "inventory.h"
#include "inv_packets.h"
#include "net_common.h"

struct ClientChestMirror {
    uint32_t  uid  = 0;
    glm::vec3 pos{};
    Inventory inv;
    bool      open = false;
};

// ── InventoryUI ───────────────────────────────────────────────────────────────
// Hotbar is always visible (drawn even when inventory is closed).
// Tab cycles Combat → Items → Blocks.
// 1-8 selects active hotbar slot.
// All mutations go to server via InventoryMoveReqPacket.
class InventoryUI {
public:
    static constexpr float SLOT_SZ  = 48.f;
    static constexpr float SLOT_PAD = 4.f;

    // ── Per-frame draw ────────────────────────────────────────────────────────
    // Call inside ImGui frame every frame.
    // Returns true if any overlay window has focus (suppress game input).
    bool draw(CInventory& cinv, ClientChestMirror* chest, ENetPeer* server) {
        _server = server;
        _cinv   = &cinv;
        _chest  = chest;

        drawHotbar(cinv);                          // always visible

        if (cinv.open) {
            drawInventoryWindow(cinv, chest);
            if (chest && chest->open)
                drawChestWindow(chest);
        }

        return cinv.open || (chest && chest->open);
    }

    // ── Server ack application ────────────────────────────────────────────────
    void applyAck(CInventory& cinv, const InventoryMoveAckPacket& ack,
                  ClientChestMirror* chest) {
        cinv.inv = ack.playerInv;
        if (ack.hasSecondary && chest && chest->uid == ack.secondaryUID)
            chest->inv = ack.secondaryInv;
        // Re-sync weapon slot from active combat hotbar slot
        syncWeaponFromHotbar(cinv);
    }

    void applyState(CInventory& cinv, const InventoryStatePacket& pkt) {
        cinv.inv = pkt.inv;
        syncWeaponFromHotbar(cinv);
    }

    void applyChestState(ClientChestMirror& mirror, const ChestStatePacket& pkt) {
        mirror.uid  = pkt.chestUID;
        mirror.pos  = pkt.pos;
        mirror.inv  = pkt.inv;
        mirror.open = true;
    }

    // ── Input handling (call before ImGui frame) ──────────────────────────────
    // Returns true if a hotbar slot changed (caller may want to update viewmodel).
    bool handleInput(CInventory& cinv, bool tabPressed,
                     int numKeyPressed /* 0 = none, 1-8 = slot */) {
        bool changed = false;

        if (tabPressed) {
            int next = ((int)cinv.hotbarMode + 1) % HOTBAR_MODE_COUNT;
            cinv.hotbarMode = (HotbarMode)next;
            // Keep active index, clamp just in case
            cinv.hotbarActive = std::min(cinv.hotbarActive, HOTBAR_SIZE - 1);
            syncWeaponFromHotbar(cinv);
            changed = true;
        }

        if (numKeyPressed >= 1 && numKeyPressed <= HOTBAR_SIZE) {
            cinv.hotbarActive = numKeyPressed - 1;
            syncWeaponFromHotbar(cinv);
            changed = true;
        }

        return changed;
    }

    // Returns the currently active hotbar item (used by combat/use system)
    const ItemStack& activeHotbarItem(const CInventory& cinv) const {
        return cinv.inv.hotbarSlot(cinv.hotbarMode, cinv.hotbarActive);
    }

private:
    ENetPeer*          _server = nullptr;
    CInventory*        _cinv   = nullptr;
    ClientChestMirror* _chest  = nullptr;

    static constexpr const char* DRAG_TYPE = "INV_SLOT";

    struct DragPayload {
        InvOwner   owner;
        uint32_t   uid;
        SlotRegion region;
        uint8_t    index; // flat index within region
    };

    // When switching to Combat mode or changing active slot,
    // push active combat hotbar item into weapon equip slot.
    void syncWeaponFromHotbar(CInventory& cinv) {
        if (cinv.hotbarMode != HotbarMode::Combat) return;
        const ItemStack& active = cinv.inv.hotbarSlot(HotbarMode::Combat,
                                                       cinv.hotbarActive);
        const ItemDef& def = getItemDef(active.id);
        if (active.empty() || def.type == ItemType::Weapon)
            cinv.inv.weaponSlot() = active;
    }

    // ── Hotbar bar (always visible at bottom of screen) ───────────────────────
    void drawHotbar(CInventory& cinv) {
        ImGuiIO& io = ImGui::GetIO();
        float barW  = HOTBAR_SIZE * (SLOT_SZ + SLOT_PAD) + SLOT_PAD + 120.f;
        float barH  = SLOT_SZ + 36.f;
        float x     = (io.DisplaySize.x - barW) * 0.5f;
        float y     = io.DisplaySize.y - barH - 8.f;

        ImGui::SetNextWindowPos({x, y}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({barW, barH}, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.75f);
        ImGui::Begin("##hotbar", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoNav |
            ImGuiWindowFlags_NoMove       | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoBringToFrontOnFocus);

        // Mode label + cycle button
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 6.f);
        ImGui::PushStyleColor(ImGuiCol_Button, modeColor(cinv.hotbarMode));
        if (ImGui::Button(HOTBAR_MODE_NAMES[(int)cinv.hotbarMode], {90.f, SLOT_SZ})) {
            int next = ((int)cinv.hotbarMode + 1) % HOTBAR_MODE_COUNT;
            cinv.hotbarMode = (HotbarMode)next;
            syncWeaponFromHotbar(cinv);
        }
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Tab / click to cycle mode");

        ImGui::SameLine(0, SLOT_PAD * 2);

        // Slots
        for (int i = 0; i < HOTBAR_SIZE; i++) {
            if (i > 0) ImGui::SameLine(0, SLOT_PAD);
            bool active = (i == cinv.hotbarActive);
            const ItemStack& s = cinv.inv.hotbarSlot(cinv.hotbarMode, i);

            // Active slot highlight
            if (active) {
                ImGui::PushStyleColor(ImGuiCol_Button,
                    {0.55f, 0.75f, 0.25f, 1.f});
            }

            uint8_t flatIdx = (uint8_t)cinv.inv.hotbarFlatIndex(cinv.hotbarMode, i);
            drawSlot(s, InvOwner::Player, 0, SlotRegion::Hotbar, flatIdx,
                     /*label=*/std::to_string(i + 1).c_str());

            if (active) ImGui::PopStyleColor();

            // Click to select
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemActive()) {
                cinv.hotbarActive = i;
                syncWeaponFromHotbar(cinv);
            }
        }

        // Key hint row
        ImGui::SetCursorPosX(120.f + SLOT_PAD * 2);
        for (int i = 0; i < HOTBAR_SIZE; i++) {
            if (i > 0) ImGui::SameLine(0, SLOT_PAD);
            ImGui::SetCursorPosX(120.f + SLOT_PAD * 2 +
                                  i * (SLOT_SZ + SLOT_PAD));
            ImGui::TextDisabled("%d", i + 1);
        }

        ImGui::End();
    }

    // ── Main inventory window ─────────────────────────────────────────────────
    void drawInventoryWindow(CInventory& cinv, ClientChestMirror* chest) {
        ImGui::SetNextWindowSize({600, 520}, ImGuiCond_Once);
        ImGui::SetNextWindowPos({40, 40},   ImGuiCond_Once);
        ImGui::Begin("Inventory", &cinv.open, ImGuiWindowFlags_NoCollapse);

        // ── Equip section ─────────────────────────────────────────────────────
        ImGui::Text("Equipped");
        ImGui::Separator();

        // Weapon
        ImGui::Text("Weapon "); ImGui::SameLine();
        drawSlot(cinv.inv.weaponSlot(),
                 InvOwner::Player, 0, SlotRegion::Equip, EQUIP_WEAPON);

        ImGui::SameLine(0, 24.f);

        // Offhand
        ImGui::Text("Offhand"); ImGui::SameLine();
        drawSlot(cinv.inv.offhandSlot(),
                 InvOwner::Player, 0, SlotRegion::Equip, EQUIP_OFFHAND);

        ImGui::SameLine(0, 24.f);

        // Totems
        ImGui::Text("Totems ");
        for (int t = 0; t < TOTEM_SLOTS; t++) {
            ImGui::SameLine(0, SLOT_PAD);
            drawSlot(cinv.inv.totemSlot(t),
                     InvOwner::Player, 0, SlotRegion::Equip,
                     (uint8_t)(EQUIP_TOTEM_0 + t));
        }

        ImGui::Spacing();
        ImGui::Spacing();

        // ── All 3 hotbar rows ─────────────────────────────────────────────────
        ImGui::Text("Hotbars");
        ImGui::Separator();

        for (int m = 0; m < HOTBAR_MODE_COUNT; m++) {
            HotbarMode mode = (HotbarMode)m;
            bool isCurrent  = (cinv.hotbarMode == mode);

            ImGui::PushStyleColor(ImGuiCol_Text,
                isCurrent ? ImVec4{0.4f,0.9f,0.4f,1.f}
                           : ImVec4{0.6f,0.6f,0.6f,1.f});
            ImGui::Text("%-8s", HOTBAR_MODE_NAMES[m]);
            ImGui::PopStyleColor();
            ImGui::SameLine(0, SLOT_PAD * 2);

            for (int i = 0; i < HOTBAR_SIZE; i++) {
                if (i > 0) ImGui::SameLine(0, SLOT_PAD);
                bool active = isCurrent && (i == cinv.hotbarActive);
                if (active) ImGui::PushStyleColor(ImGuiCol_Button,
                                {0.55f,0.75f,0.25f,1.f});

                uint8_t flat = (uint8_t)cinv.inv.hotbarFlatIndex(mode, i);
                drawSlot(cinv.inv.hotbarSlot(mode, i),
                         InvOwner::Player, 0, SlotRegion::Hotbar, flat);

                if (active) ImGui::PopStyleColor();
            }
        }

        ImGui::Spacing();
        ImGui::Spacing();

        // ── Bag grid ──────────────────────────────────────────────────────────
        ImGui::Text("Bag");
        ImGui::Separator();
        drawGrid(InvOwner::Player, 0, cinv.inv);

        ImGui::End();
    }

    // ── Chest window ──────────────────────────────────────────────────────────
    void drawChestWindow(ClientChestMirror* chest) {
        if (!chest) return;
        ImGui::SetNextWindowSize({440, 320}, ImGuiCond_Once);
        ImGui::SetNextWindowPos({660, 40},   ImGuiCond_Once);
        bool open = chest->open;
        ImGui::Begin("Chest", &open, ImGuiWindowFlags_NoCollapse);
        ImGui::Text("Chest Contents");
        ImGui::Separator();
        drawGrid(InvOwner::Chest, chest->uid, chest->inv);
        ImGui::End();

        if (!open) {
            ChestCloseReqPacket req{chest->uid};
            Net::sendReliable(_server, req.serialize());
            chest->open  = false;
            _cinv->open  = false;
        }
    }

    // ── Draw helpers ──────────────────────────────────────────────────────────

    void drawGrid(InvOwner owner, uint32_t uid, const Inventory& inv) {
        // Use a const ref and cast away for display — slots are read for display,
        // actual mutation goes through server.
        for (int row = 0; row < INV_ROWS; row++) {
            for (int col = 0; col < INV_COLS; col++) {
                if (col > 0) ImGui::SameLine(0, SLOT_PAD);
                int idx = row * INV_COLS + col;
                drawSlot(inv.grid[idx], owner, uid, SlotRegion::Grid, (uint8_t)idx);
            }
        }
    }

    void drawSlot(const ItemStack& slot, InvOwner owner, uint32_t uid,
                  SlotRegion region, uint8_t index,
                  const char* keyHint = nullptr) {
        ImVec2 sz{SLOT_SZ, SLOT_SZ};
        ImGui::PushID((int)owner * 1000000 + (int)uid * 10000 +
                      (int)region * 1000 + index);

        bool empty = slot.empty();
        ImVec4 bg  = empty ? ImVec4{0.13f,0.13f,0.13f,1.f}
                           : ImVec4{0.22f,0.22f,0.28f,1.f};
        ImGui::PushStyleColor(ImGuiCol_Button,        bg);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.32f,0.32f,0.38f,1.f});
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  {0.42f,0.42f,0.48f,1.f});

        ImGui::Button("##s", sz);

        if (!empty && ImGui::IsItemHovered()) {
            const ItemDef& def = getItemDef(slot.id);
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(def.name.data());
            if (def.maxStack > 1) ImGui::Text("x%d", slot.count);
            ImGui::TextDisabled("%s", def.description.data());
            ImGui::EndTooltip();
        }

        // Draw item abbreviation + count on top of button
        if (!empty) {
            ImVec2 p   = ImGui::GetItemRectMin();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ItemDef& def = getItemDef(slot.id);
            char abbr[4] = {};
            for (int i = 0; i < 3 && def.name[i]; i++) abbr[i] = def.name[i];
            dl->AddText({p.x + 3, p.y + 3},
                        IM_COL32(230, 230, 230, 255), abbr);
            if (def.maxStack > 1) {
                char cnt[8]; snprintf(cnt, sizeof(cnt), "%d", slot.count);
                dl->AddText({p.x + 3, p.y + SLOT_SZ - 14},
                            IM_COL32(255, 220, 80, 255), cnt);
            }
        }

        // Key hint (hotbar numbers)
        if (keyHint) {
            ImVec2 p   = ImGui::GetItemRectMin();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddText({p.x + SLOT_SZ - 12, p.y + SLOT_SZ - 14},
                        IM_COL32(180, 180, 180, 180), keyHint);
        }

        ImGui::PopStyleColor(3);

        // Drag source
        if (!empty && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            DragPayload pay{owner, uid, region, index};
            ImGui::SetDragDropPayload(DRAG_TYPE, &pay, sizeof(pay));
            ImGui::TextUnformatted(getItemDef(slot.id).name.data());
            ImGui::EndDragDropSource();
        }

        // Drop target → send move request, never mutate locally
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* p =
                    ImGui::AcceptDragDropPayload(DRAG_TYPE)) {
                const DragPayload& src =
                    *static_cast<const DragPayload*>(p->Data);
                sendMoveReq(src, {owner, uid, region, index});
            }
            ImGui::EndDragDropTarget();
        }

        ImGui::PopID();
    }

    void sendMoveReq(const DragPayload& src, const DragPayload& dst) {
        if (!_server) return;
        InventoryMoveReqPacket req;
        req.src = {src.owner, src.uid, src.region, src.index};
        req.dst = {dst.owner, dst.uid, dst.region, dst.index};
        Net::sendReliable(_server, req.serialize());
    }

    // ── Mode colour accent ────────────────────────────────────────────────────
    ImVec4 modeColor(HotbarMode mode) {
        switch (mode) {
        case HotbarMode::Combat: return {0.55f, 0.15f, 0.15f, 0.90f};
        case HotbarMode::Items:  return {0.15f, 0.45f, 0.55f, 0.90f};
        case HotbarMode::Blocks: return {0.40f, 0.30f, 0.15f, 0.90f};
        default:                 return {0.25f, 0.25f, 0.25f, 0.90f};
        }
    }
};
