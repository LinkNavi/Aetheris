#pragma once
#include "TextEditor.h"
#include "inventory.h"
#include "log.h"
#include "net_common.h"
#include "spell_packets.h"
#include <enet/enet.h>
#include <imgui.h>
#include <string>
#include <vector>

// ── AetherScript language definition ─────────────────────────────────────────
static const TextEditor::Language* AetherScriptLanguage() {
    static TextEditor::Language lang;
    static bool built = false;
    if (built) return &lang;
    built = true;

    lang.name              = "AetherScript";
    lang.caseSensitive     = true;
    lang.singleLineComment = "//";
    lang.hasDoubleQuotedStrings = true;
    lang.stringEscape      = '\\';

    for (auto* kw : {
        "spell","rune","fn","return","if","else",
        "for","while","break","continue","let",
        "vec3","fail","log","true","false","null"
    }) lang.keywords.insert(kw);

    // Primitives shown in declaration color
    for (auto* id : {
        "aoe_damage","aoe_heal","projectile","apply_status",
        "distance","normalize","length","get_aim",
        "get_caster_pos","get_caster_health"
    }) lang.declarations.insert(id);

    lang.isPunctuation = [](ImWchar c) -> bool {
        return c == '(' || c == ')' || c == '{' || c == '}' ||
               c == '[' || c == ']' || c == ';' || c == ':' ||
               c == ',' || c == '.' || c == '+' || c == '-' ||
               c == '*' || c == '/' || c == '%' || c == '=' ||
               c == '!' || c == '<' || c == '>' || c == '&' ||
               c == '|';
    };

    return &lang;
}

// ── SpellEditorUI ─────────────────────────────────────────────────────────────
class SpellEditorUI {
public:
    bool open = false;

    SpellEditorUI() {
        _editor.SetLanguage(AetherScriptLanguage());
        _editor.SetShowLineNumbersEnabled(true);
        _editor.SetShowMatchingBrackets(true);
        _editor.SetCompletePairedGlyphs(true);
        _editor.SetInsertSpacesOnTabs(true);
        _editor.SetTabSize(4);
        _buildAutocomplete();
    }

    void onCompileAck(CInventory& cinv, const SpellCompileAckPacket& pkt) {
        _lastError    = pkt.success ? "" : pkt.error;
        _lastMana     = pkt.baseMana;
        _lastCastTime = pkt.castTime;
        _compiling    = false;

        _editor.ClearMarkers();

        if (pkt.success) {
            int idx = cinv.inv.findSpellInBook(pkt.spellName);
            if (idx < 0) idx = cinv.inv.firstEmptyBookSlot();
            if (idx >= 0) {
                cinv.inv.spellBook[idx].name        = pkt.spellName;
                cinv.inv.spellBook[idx].displayName = pkt.spellName;
                cinv.inv.spellBook[idx].source      = _editor.GetText();
                cinv.inv.spellBook[idx].baseMana    = pkt.baseMana;
                cinv.inv.spellBook[idx].castTime    = pkt.castTime;
                _selectedSpell = idx;
            }
            _trie.insert(pkt.spellName);
        } else {
            // Parse "line N" from error and place markers (AddMarker is 0-based)
            const std::string& e = _lastError;
            size_t p = 0;
            while ((p = e.find("line ", p)) != std::string::npos) {
                p += 5;
                int lineNo = 0;
                while (p < e.size() && std::isdigit((unsigned char)e[p]))
                    lineNo = lineNo * 10 + (e[p++] - '0');
                if (lineNo > 0)
                    _editor.AddMarker(lineNo - 1,
                        IM_COL32(255,80,80,255),
                        IM_COL32(255,80,80,255),
                        "Error", _lastError);
            }
        }
    }

    // Call whenever the server sends an updated spell name list
    void onServerSpellList(const std::vector<std::string>& spellNames) {
        _serverSpellNames = spellNames;
        _buildAutocomplete();
    }

    bool draw(CInventory& cinv, ENetPeer* server) {
        if (!open) return false;

        ImGuiIO& io = ImGui::GetIO();
        float sw = io.DisplaySize.x;
        float sh = io.DisplaySize.y;

        ImGui::SetNextWindowPos({0,0});
        ImGui::SetNextWindowSize({sw,sh});
        ImGui::SetNextWindowBgAlpha(0.85f);
        ImGui::Begin("##spellEditorOverlay", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoSavedSettings);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled({0,0}, {sw,36.f}, IM_COL32(15,10,25,255));
        dl->AddText(ImGui::GetFont(), 20.f, {16.f,8.f},
                    IM_COL32(160,100,255,255), "SPELL EDITOR");
        dl->AddText(ImGui::GetFont(), 13.f, {180.f,12.f},
                    IM_COL32(100,80,140,200), "AetherScript");

        ImGui::SetCursorPos({sw-40.f, 6.f});
        if (ImGui::Button("X##closeSpellEd", {30.f,24.f})) {
            open = false;
            ImGui::End();
            return false;
        }

        float panelH = sh - 40.f;
        float leftW  = 220.f;
        float rightW = 260.f;
        float midW   = sw - leftW - rightW - 12.f;

        ImGui::SetCursorPos({4.f, 44.f});
        ImGui::BeginChild("##spellList", {leftW, panelH-8.f}, true,
                          ImGuiWindowFlags_NoScrollbar);
        _drawSpellList(cinv);
        ImGui::EndChild();

        ImGui::SetCursorPos({leftW+8.f, 44.f});
        ImGui::BeginChild("##spellEditor", {midW, panelH-8.f}, true);
        _drawEditor(cinv, server, midW, panelH-8.f);
        ImGui::EndChild();

        ImGui::SetCursorPos({leftW+midW+12.f, 44.f});
        ImGui::BeginChild("##spellOptions", {rightW-4.f, panelH-8.f}, true);
        _drawOptions(cinv, server);
        ImGui::EndChild();

        ImGui::End();
        return true;
    }

    void update(float /*dt*/) {}

private:
    TextEditor       _editor;
    TextEditor::Trie _trie;

    char  _nameBuf[64]   = {};
    bool  _compiling     = false;
    std::string _lastError;
    float _lastMana      = 0.f;
    float _lastCastTime  = 0.f;
    int   _selectedSpell = -1;

    std::vector<std::string> _serverSpellNames;

    // ── Autocomplete ──────────────────────────────────────────────────────────
    void _buildAutocomplete() {
        _trie.clear();

        for (auto* kw : {
            "spell","rune","fn","return","if","else",
            "for","while","break","continue","let",
            "vec3","fail","log","true","false","null"
        }) _trie.insert(kw);

        for (auto* id : {
            "aoe_damage","aoe_heal","projectile","apply_status",
            "distance","normalize","length","get_aim",
            "get_caster_pos","get_caster_health"
        }) _trie.insert(id);

        for (auto& n : _serverSpellNames)
            _trie.insert(n);

        _editor.IterateIdentifiers([this](const std::string& id) {
            _trie.insert(id);
        });

        TextEditor::AutoCompleteConfig cfg;
        cfg.callback = [this](TextEditor::AutoCompleteState& state) {
            _trie.findSuggestions(state.suggestions, state.searchTerm);
        };
        _editor.SetAutoCompleteConfig(&cfg);

        _editor.SetChangeCallback([this]() {
            _buildAutocomplete();
        }, 500);
    }

    // ── Spellbook list ────────────────────────────────────────────────────────
    void _drawSpellList(CInventory& cinv) {
        ImGui::TextColored({0.6f,0.4f,1.f,1.f}, "SPELLBOOK");
        ImGui::Separator();

        if (ImGui::Button("+ New Spell", {-1.f,28.f})) {
            _selectedSpell = -1;
            memset(_nameBuf, 0, sizeof(_nameBuf));
            _lastError.clear();
            _lastMana = 0.f;
            _editor.ClearMarkers();
            _editor.SetText(
                "// mana: 20\n"
                "// cast_time: 0.5\n\n"
                "spell my_spell() {\n"
                "    aoe_damage(radius: 3.0, damage: 0.5);\n"
                "}\n");
            strncpy(_nameBuf, "my_spell", sizeof(_nameBuf)-1);
            _buildAutocomplete();
        }

        ImGui::Spacing();

        for (int i = 0; i < SPELL_BOOK_SIZE; i++) {
            const SpellEntry& sp = cinv.inv.spellBook[i];
            if (sp.empty()) continue;

            bool isActive   = false;
            int  activeSlot = -1;
            for (int s = 0; s < SPELL_SLOTS; s++) {
                if (cinv.inv.activeSpells[s] == i) {
                    isActive = true; activeSlot = s; break;
                }
            }

            bool selected = (_selectedSpell == i);
            if (selected)
                ImGui::PushStyleColor(ImGuiCol_Header, {0.3f,0.15f,0.5f,1.f});

            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 p = ImGui::GetCursorScreenPos();
            dl->AddCircleFilled({p.x+8.f,p.y+10.f}, 5.f,
                isActive ? IM_COL32(80,255,120,255) : IM_COL32(80,80,80,200), 8);
            ImGui::Dummy({0.f,0.f});
            ImGui::SameLine(18.f);

            char label[96];
            if (isActive)
                snprintf(label,sizeof(label),"%s  [%d]##sp%d",
                         sp.displayName.c_str(),activeSlot+1,i);
            else
                snprintf(label,sizeof(label),"%s##sp%d",
                         sp.displayName.c_str(),i);

            if (ImGui::Selectable(label, selected,
                                  ImGuiSelectableFlags_None, {-1.f,20.f})) {
                _selectedSpell = i;
                _editor.SetText(sp.source);
                strncpy(_nameBuf, sp.name.c_str(), sizeof(_nameBuf)-1);
                _lastError.clear();
                _lastMana     = sp.baseMana;
                _lastCastTime = sp.castTime;
                _editor.ClearMarkers();
                _buildAutocomplete();
            }

            if (selected) ImGui::PopStyleColor();
        }
    }

    // ── Editor center panel ───────────────────────────────────────────────────
    void _drawEditor(CInventory& /*cinv*/, ENetPeer* server, float w, float h) {
        ImGui::Text("Name:");
        ImGui::SameLine();
        ImGui::PushItemWidth(w - 120.f);
        ImGui::InputText("##spellName", _nameBuf, sizeof(_nameBuf));
        ImGui::PopItemWidth();
        ImGui::SameLine();

        bool canCompile = (_nameBuf[0] != 0) && !_compiling && server;
        if (!canCompile) ImGui::BeginDisabled();
        ImGui::PushStyleColor(ImGuiCol_Button,        {0.3f,0.1f,0.6f,1.f});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.45f,0.15f,0.8f,1.f});
        if (ImGui::Button(_compiling ? "Compiling..." : "Compile", {-1.f,0.f}))
            _sendCompile(server);
        ImGui::PopStyleColor(2);
        if (!canCompile) ImGui::EndDisabled();

        ImGui::Separator();

        if (!_lastError.empty()) {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, {0.3f,0.05f,0.05f,1.f});
            ImGui::BeginChild("##errBanner", {-1.f,48.f}, false);
            ImGui::TextColored({1.f,0.4f,0.4f,1.f}, "ERROR");
            ImGui::SameLine();
            ImGui::TextWrapped("%s", _lastError.c_str());
            ImGui::EndChild();
            ImGui::PopStyleColor();
        } else if (_lastMana > 0.f) {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, {0.05f,0.2f,0.08f,1.f});
            ImGui::BeginChild("##okBanner", {-1.f,28.f}, false);
            ImGui::TextColored({0.4f,1.f,0.5f,1.f}, "OK");
            ImGui::SameLine();
            ImGui::Text("Mana: %.0f  Cast: %.2fs", _lastMana, _lastCastTime);
            ImGui::EndChild();
            ImGui::PopStyleColor();
        }

        float edH = h - ImGui::GetCursorPosY() - 8.f;
        edH = std::max(edH, 100.f);
        _editor.Render("##aetherCode", {-1.f, edH});
    }

    // ── Options right panel ───────────────────────────────────────────────────
    void _drawOptions(CInventory& cinv, ENetPeer* server) {
        ImGui::TextColored({0.6f,0.4f,1.f,1.f}, "SPELL INFO");
        ImGui::Separator();

        if (_selectedSpell >= 0 && _selectedSpell < SPELL_BOOK_SIZE &&
            !cinv.inv.spellBook[_selectedSpell].empty()) {

            const SpellEntry& sp = cinv.inv.spellBook[_selectedSpell];
            ImGui::Text("Name:  %s", sp.displayName.c_str());
            ImGui::Text("Mana:  %.0f", sp.baseMana);
            ImGui::Text("Cast:  %.2fs", sp.castTime);

            ImGui::Spacing(); ImGui::Separator();
            ImGui::TextColored({0.6f,0.4f,1.f,1.f}, "ACTIVE SLOTS");
            ImGui::Spacing();

            for (int s = 0; s < SPELL_SLOTS; s++) {
                bool isHere = (cinv.inv.activeSpells[s] == _selectedSpell);
                char btnLabel[32];
                int oc = cinv.inv.activeSpells[s];
                if (oc >= 0 && oc < SPELL_BOOK_SIZE &&
                    !cinv.inv.spellBook[oc].empty() && oc != _selectedSpell)
                    snprintf(btnLabel,sizeof(btnLabel),"Slot %d: %s",s+1,
                             cinv.inv.spellBook[oc].name.c_str());
                else if (isHere)
                    snprintf(btnLabel,sizeof(btnLabel),"Slot %d: [this]",s+1);
                else
                    snprintf(btnLabel,sizeof(btnLabel),"Slot %d: empty",s+1);

                if (isHere)
                    ImGui::PushStyleColor(ImGuiCol_Button,{0.2f,0.5f,0.2f,1.f});
                if (ImGui::Button(btnLabel,{-1.f,26.f})) {
                    if (isHere) {
                        cinv.inv.activeSpells[s] = -1;
                    } else {
                        for (int os = 0; os < SPELL_SLOTS; os++)
                            if (cinv.inv.activeSpells[os] == _selectedSpell)
                                cinv.inv.activeSpells[os] = -1;
                        cinv.inv.activeSpells[s] = _selectedSpell;
                    }
                    if (server) _sendLoadout(cinv, server);
                }
                if (isHere) ImGui::PopStyleColor();
            }

            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
            ImGui::TextColored({0.6f,0.4f,1.f,1.f}, "PRIMITIVES");
            ImGui::Spacing();
            static const char* REFS[] = {
                "aoe_damage(radius, damage)",
                "aoe_heal(radius, amount)",
                "projectile(damage, speed)",
                "apply_status(e, type, dur)",
                "distance(v3, v3) -> float",
                "normalize(v3) -> v3",
                "length(v3) -> float",
                "vec3(x, y, z)",
                "fail(\"reason\")",
                "log(\"msg\")",
            };
            for (auto* r : REFS) {
                ImGui::PushStyleColor(ImGuiCol_Text,{0.6f,0.8f,0.6f,1.f});
                ImGui::TextUnformatted(r);
                ImGui::PopStyleColor();
            }

            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Button,       {0.5f,0.1f,0.1f,1.f});
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,{0.7f,0.15f,0.15f,1.f});
            if (ImGui::Button("Delete Spell",{-1.f,30.f})) {
                if (server) {
                    SpellDeleteReqPacket pkt;
                    pkt.spellName = sp.name;
                    Net::sendReliable(server, pkt.serialize());
                }
                for (int s = 0; s < SPELL_SLOTS; s++)
                    if (cinv.inv.activeSpells[s] == _selectedSpell)
                        cinv.inv.activeSpells[s] = -1;
                cinv.inv.spellBook[_selectedSpell].clear();
                _selectedSpell = -1;
                memset(_nameBuf, 0, sizeof(_nameBuf));
                _lastError.clear();
                _lastMana = 0.f;
                _editor.ClearText();
                _editor.ClearMarkers();
            }
            ImGui::PopStyleColor(2);

        } else {
            ImGui::TextDisabled("No spell selected.");
            ImGui::Spacing();
            ImGui::TextDisabled("Create or select a spell\nfrom the left panel.");
            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
            ImGui::TextColored({0.6f,0.4f,1.f,1.f}, "QUICK START");
            ImGui::Spacing();
            ImGui::TextWrapped(
                "1. Click '+ New Spell'\n"
                "2. Edit name + source\n"
                "3. Click Compile\n"
                "4. Assign to a slot (1-5)\n"
                "5. Hold R/T/Y/Z/X to cast");
        }
    }

    // ── Network ───────────────────────────────────────────────────────────────
    void _sendCompile(ENetPeer* server) {
        _compiling = true;
        _lastError.clear();
        _editor.ClearMarkers();

        SpellCompileReqPacket pkt;
        pkt.spellName = _nameBuf;
        pkt.source    = _editor.GetText();
        Log::info("compile req: " + pkt.spellName);
        Net::sendReliable(server, pkt.serialize());
    }

    void _sendLoadout(const CInventory& cinv, ENetPeer* server) {
        SpellLoadoutSetPacket pkt;
        for (int s = 0; s < SPELL_SLOTS; s++) {
            int idx = cinv.inv.activeSpells[s];
            pkt.slots[s] = (idx >= 0 && idx < SPELL_BOOK_SIZE &&
                            !cinv.inv.spellBook[idx].empty())
                         ? cinv.inv.spellBook[idx].name : "";
        }
        Net::sendReliable(server, pkt.serialize());
    }
};
