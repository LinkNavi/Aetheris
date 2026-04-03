#pragma once
#include "aether_lexer.h"
#include "inventory.h"
#include "log.h"
#include "net_common.h"
#include "spell_packets.h"
#include <array>
#include <chrono>
#include <enet/enet.h>
#include <imgui.h>
#include <string>
#include <vector>

// ── Token color map
// ───────────────────────────────────────────────────────────
static inline ImU32 tokenColor(Aether::TokenType t) {
  using T = Aether::TokenType;
  switch (t) {
  case T::KwSpell:
  case T::KwRune:
  case T::KwFn:
  case T::KwReturn:
  case T::KwIf:
  case T::KwElse:
  case T::KwFor:
  case T::KwWhile:
  case T::KwBreak:
  case T::KwContinue:
  case T::KwLet:
  case T::KwVec3:
  case T::KwFail:
  case T::KwLog:
    return IM_COL32(200, 120, 255, 255); // purple — keywords

  case T::KwTrue:
  case T::KwFalse:
  case T::KwNull:
    return IM_COL32(255, 160, 80, 255); // orange — literals

  case T::Number:
    return IM_COL32(180, 220, 100, 255); // green — numbers

  case T::String:
    return IM_COL32(230, 180, 100, 255); // yellow — strings

  case T::Identifier:
    return IM_COL32(180, 210, 255, 255); // light blue — identifiers

  case T::Plus:
  case T::Minus:
  case T::Star:
  case T::Slash:
  case T::Percent:
  case T::Eq:
  case T::EqEq:
  case T::BangEq:
  case T::Lt:
  case T::Gt:
  case T::LtEq:
  case T::GtEq:
  case T::And:
  case T::Or:
  case T::Bang:
  case T::PlusEq:
  case T::MinusEq:
  case T::StarEq:
  case T::SlashEq:
    return IM_COL32(255, 200, 100, 255); // yellow — operators

  case T::LParen:
  case T::RParen:
  case T::LBrace:
  case T::RBrace:
  case T::LBracket:
  case T::RBracket:
    return IM_COL32(200, 200, 200, 255); // white — brackets

  case T::Colon:
  case T::Comma:
  case T::Dot:
  case T::Semicolon:
    return IM_COL32(150, 150, 150, 255); // grey — punctuation

  case T::Error:
    return IM_COL32(255, 80, 80, 255); // red — errors

  default:
    return IM_COL32(210, 210, 210, 255);
  }
}

// ── Highlighted line
// ──────────────────────────────────────────────────────────
struct HlSpan {
  int col; // byte offset in line
  int len;
  ImU32 color;
};

struct HlLine {
  std::vector<HlSpan> spans;
};

// ── SpellEditorUI
// ─────────────────────────────────────────────────────────────
class SpellEditorUI {
public:
  bool open = false;

  // Called when server sends compile ack
  void onCompileAck(CInventory &cinv, const SpellCompileAckPacket &pkt) {
    _lastError = pkt.success ? "" : pkt.error;
    _lastMana = pkt.baseMana;
    _lastCastTime = pkt.castTime;
    _compiling = false;

    if (pkt.success) {
      // Add or update in spellbook
      int idx = cinv.inv.findSpellInBook(pkt.spellName);
      if (idx < 0)
        idx = cinv.inv.firstEmptyBookSlot();
      if (idx >= 0) {
        cinv.inv.spellBook[idx].name = pkt.spellName;
        cinv.inv.spellBook[idx].displayName = pkt.spellName;
        cinv.inv.spellBook[idx].source = _editorBuf;
        cinv.inv.spellBook[idx].baseMana = pkt.baseMana;
        cinv.inv.spellBook[idx].castTime = pkt.castTime;
        _selectedSpell = idx;
      }
    }
    // Rebuild highlight
    _rebuildHighlight();
    _rebuildErrorLines();
  }

  // Main draw — call inside ImGui frame every frame when open
  // Returns true if UI captured input (suppress game input)
  bool draw(CInventory &cinv, ENetPeer *server) {
    if (!open)
      return false;

    ImGuiIO &io = ImGui::GetIO();
    float sw = io.DisplaySize.x;
    float sh = io.DisplaySize.y;

    // Fullscreen overlay background
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize({sw, sh});
    ImGui::SetNextWindowBgAlpha(0.85f);
    ImGui::Begin("##spellEditorOverlay", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoBringToFrontOnFocus |
                     ImGuiWindowFlags_NoSavedSettings);

    // Title bar
    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled({0, 0}, {sw, 36.f}, IM_COL32(15, 10, 25, 255));
    dl->AddText(ImGui::GetFont(), 20.f, {16.f, 8.f},
                IM_COL32(160, 100, 255, 255), "SPELL EDITOR");
    dl->AddText(ImGui::GetFont(), 13.f, {180.f, 12.f},
                IM_COL32(100, 80, 140, 200), "AetherScript");

    // Close button
    float cx = sw - 40.f;
    ImGui::SetCursorPos({cx, 6.f});
    if (ImGui::Button("X##closeSpellEd", {30.f, 24.f})) {
      open = false;
      ImGui::End();
      return false;
    }

    ImGui::SetCursorPos({0.f, 40.f});

    float panelH = sh - 40.f;
    float leftW = 220.f;
    float rightW = 260.f;
    float midW = sw - leftW - rightW - 12.f;

    // ── Left: spellbook list ──────────────────────────────────────────────
    ImGui::SetCursorPos({4.f, 44.f});
    ImGui::BeginChild("##spellList", {leftW, panelH - 8.f}, true,
                      ImGuiWindowFlags_NoScrollbar);
    drawSpellList(cinv);
    ImGui::EndChild();

    // ── Center: editor ────────────────────────────────────────────────────
    ImGui::SetCursorPos({leftW + 8.f, 44.f});
    ImGui::BeginChild("##spellEditor", {midW, panelH - 8.f}, true);
    drawEditor(cinv, server, midW, panelH - 8.f);
    ImGui::EndChild();

    // ── Right: options ────────────────────────────────────────────────────
    ImGui::SetCursorPos({leftW + midW + 12.f, 44.f});
    ImGui::BeginChild("##spellOptions", {rightW - 4.f, panelH - 8.f}, true);
    drawOptions(cinv, server);
    ImGui::EndChild();

    ImGui::End();
    return true;
  }

  // Tick debounce timer
  void update(float dt) {
    if (_debounceTimer > 0.f) {
      _debounceTimer -= dt;
      if (_debounceTimer <= 0.f) {
        _rebuildHighlight();
        _rebuildErrorLines();
      }
    }
  }

private:
  // Editor state
  char _editorBuf[8192] = {};
  char _nameBuf[64] = {};
  bool _compiling = false;
  std::string _lastError;
  float _lastMana = 0.f;
  float _lastCastTime = 0.f;
  int _selectedSpell = -1;
  float _debounceTimer = 0.f;

  // Syntax highlight cache
  std::vector<HlLine> _hlLines;
  std::vector<int> _errorLines; // 1-based line numbers with errors

  // ── Spellbook list ────────────────────────────────────────────────────────
  void drawSpellList(CInventory &cinv) {
    ImGui::TextColored({0.6f, 0.4f, 1.f, 1.f}, "SPELLBOOK");
    ImGui::Separator();

    // New spell button
    if (ImGui::Button("+ New Spell", {-1.f, 28.f})) {
      _selectedSpell = -1;
      memset(_editorBuf, 0, sizeof(_editorBuf));
      memset(_nameBuf, 0, sizeof(_nameBuf));
      _lastError.clear();
      // Insert template
      const char *tmpl = "// mana: 20\n"
                         "// cast_time: 0.5\n\n"
                         "spell my_spell() {\n"
                         "    aoe_damage(radius: 3.0, damage: 0.5);\n"
                         "}\n";
      strncpy(_editorBuf, tmpl, sizeof(_editorBuf) - 1);
      strncpy(_nameBuf, "my_spell", sizeof(_nameBuf) - 1);
      _rebuildHighlight();
    }

    ImGui::Spacing();

    // List all spells in book
    for (int i = 0; i < SPELL_BOOK_SIZE; i++) {
      const SpellEntry &sp = cinv.inv.spellBook[i];
      if (sp.empty())
        continue;

      // Check if active in any slot
      bool isActive = false;
      int activeSlot = -1;
      for (int s = 0; s < SPELL_SLOTS; s++) {
        if (cinv.inv.activeSpells[s] == i) {
          isActive = true;
          activeSlot = s;
          break;
        }
      }

      // Highlight selected
      bool selected = (_selectedSpell == i);
      if (selected)
        ImGui::PushStyleColor(ImGuiCol_Header, {0.3f, 0.15f, 0.5f, 1.f});

      // Active indicator
      ImU32 dotCol =
          isActive ? IM_COL32(80, 255, 120, 255) : IM_COL32(80, 80, 80, 200);
      ImDrawList *dl = ImGui::GetWindowDrawList();
      ImVec2 p = ImGui::GetCursorScreenPos();
      dl->AddCircleFilled({p.x + 8.f, p.y + 10.f}, 5.f, dotCol, 8);
      ImGui::Dummy({0.f, 0.f});
      ImGui::SameLine(18.f);

      char label[96];
      if (isActive)
        snprintf(label, sizeof(label), "%s  [%d]##sp%d", sp.displayName.c_str(),
                 activeSlot + 1, i);
      else
        snprintf(label, sizeof(label), "%s##sp%d", sp.displayName.c_str(), i);

      if (ImGui::Selectable(label, selected, ImGuiSelectableFlags_None,
                            {-1.f, 20.f})) {
        _selectedSpell = i;
        strncpy(_editorBuf, sp.source.c_str(), sizeof(_editorBuf) - 1);
        strncpy(_nameBuf, sp.name.c_str(), sizeof(_nameBuf) - 1);
        _lastError.clear();
        _lastMana = sp.baseMana;
        _lastCastTime = sp.castTime;
        _rebuildHighlight();
      }

      if (selected)
        ImGui::PopStyleColor();
    }
  }

  // ── Editor center panel ───────────────────────────────────────────────────
  void drawEditor(CInventory &cinv, ENetPeer *server, float w, float h) {
    // Name row
    ImGui::Text("Name:");
    ImGui::SameLine();
    ImGui::PushItemWidth(w - 120.f);
    ImGui::InputText("##spellName", _nameBuf, sizeof(_nameBuf));
    ImGui::PopItemWidth();
    ImGui::SameLine();

    // Compile button
    bool canCompile = (_nameBuf[0] != 0) && !_compiling && server;
    if (!canCompile)
      ImGui::BeginDisabled();
    ImGui::PushStyleColor(ImGuiCol_Button, {0.3f, 0.1f, 0.6f, 1.f});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.45f, 0.15f, 0.8f, 1.f});
    if (ImGui::Button(_compiling ? "Compiling..." : "Compile", {-1.f, 0.f})) {
      sendCompile(server);
    }
    ImGui::PopStyleColor(2);
    if (!canCompile)
      ImGui::EndDisabled();

    ImGui::Separator();

    // Error banner
    if (!_lastError.empty()) {
      ImGui::PushStyleColor(ImGuiCol_ChildBg, {0.3f, 0.05f, 0.05f, 1.f});
      ImGui::BeginChild("##errBanner", {-1.f, 48.f}, false);
      ImGui::TextColored({1.f, 0.4f, 0.4f, 1.f}, "ERROR");
      ImGui::SameLine();
      ImGui::TextWrapped("%s", _lastError.c_str());
      ImGui::EndChild();
      ImGui::PopStyleColor();
    } else if (_lastMana > 0.f) {
      ImGui::PushStyleColor(ImGuiCol_ChildBg, {0.05f, 0.2f, 0.08f, 1.f});
      ImGui::BeginChild("##okBanner", {-1.f, 28.f}, false);
      ImGui::TextColored({0.4f, 1.f, 0.5f, 1.f}, "OK");
      ImGui::SameLine();
      ImGui::Text("Mana: %.0f  Cast: %.2fs", _lastMana, _lastCastTime);
      ImGui::EndChild();
      ImGui::PopStyleColor();
    }

    // ── Highlighted text editor ───────────────────────────────────────────
    float edH = h - (_lastError.empty() && _lastMana <= 0.f ? 60.f : 96.f);
    edH = std::max(edH, 100.f);

   ImGui::PushStyleColor(ImGuiCol_FrameBg, {0.08f, 0.05f, 0.12f, 1.f});
ImGui::PushStyleColor(ImGuiCol_Text, {0.85f, 0.85f, 0.85f, 1.f});
    ImVec2 editorPos = ImGui::GetCursorScreenPos();

    bool changed = ImGui::InputTextMultiline("##code", _editorBuf,
                                             sizeof(_editorBuf), {-1.f, edH},
                                             ImGuiInputTextFlags_AllowTabInput);
    ImGui::PopStyleColor(2);

   if (changed) {
    _rebuildHighlight();
    _rebuildErrorLines();
}

    // Draw highlighted text on top
    //drawHighlight(editorPos, w, edH);

    // Error line underlines
   // drawErrorUnderlines(editorPos, w);
  }

  // ── Options right panel ───────────────────────────────────────────────────
  void drawOptions(CInventory &cinv, ENetPeer *server) {
    ImGui::TextColored({0.6f, 0.4f, 1.f, 1.f}, "SPELL INFO");
    ImGui::Separator();

    if (_selectedSpell >= 0 && _selectedSpell < SPELL_BOOK_SIZE &&
        !cinv.inv.spellBook[_selectedSpell].empty()) {

      const SpellEntry &sp = cinv.inv.spellBook[_selectedSpell];
      ImGui::Text("Name:  %s", sp.displayName.c_str());
      ImGui::Text("Mana:  %.0f", sp.baseMana);
      ImGui::Text("Cast:  %.2fs", sp.castTime);

      ImGui::Spacing();
      ImGui::Separator();
      ImGui::TextColored({0.6f, 0.4f, 1.f, 1.f}, "ACTIVE SLOTS");
      ImGui::Spacing();

      // 5 slot buttons
      for (int s = 0; s < SPELL_SLOTS; s++) {
        bool isHere = (cinv.inv.activeSpells[s] == _selectedSpell);

        char btnLabel[32];
        if (cinv.inv.activeSpells[s] >= 0 &&
            cinv.inv.activeSpells[s] < SPELL_BOOK_SIZE &&
            !cinv.inv.spellBook[cinv.inv.activeSpells[s]].empty() &&
            cinv.inv.activeSpells[s] != _selectedSpell) {
          // Slot occupied by another spell
          snprintf(btnLabel, sizeof(btnLabel), "Slot %d: %s", s + 1,
                   cinv.inv.spellBook[cinv.inv.activeSpells[s]].name.c_str());
        } else if (isHere) {
          snprintf(btnLabel, sizeof(btnLabel), "Slot %d: [this]", s + 1);
        } else {
          snprintf(btnLabel, sizeof(btnLabel), "Slot %d: empty", s + 1);
        }

        if (isHere)
          ImGui::PushStyleColor(ImGuiCol_Button, {0.2f, 0.5f, 0.2f, 1.f});

        if (ImGui::Button(btnLabel, {-1.f, 26.f})) {
          if (isHere) {
            cinv.inv.activeSpells[s] = -1;
          } else {
            // Remove from any current slot first
            for (int os = 0; os < SPELL_SLOTS; os++)
              if (cinv.inv.activeSpells[os] == _selectedSpell)
                cinv.inv.activeSpells[os] = -1;
            cinv.inv.activeSpells[s] = _selectedSpell;
          }
          // Send loadout update
          if (server)
            sendLoadout(cinv, server);
        }

        if (isHere)
          ImGui::PopStyleColor();
      }

      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();

      // Reference card
      ImGui::TextColored({0.6f, 0.4f, 1.f, 1.f}, "PRIMITIVES");
      ImGui::Spacing();
      static const char *REFS[] = {
          "aoe_damage(radius, damage)", "aoe_heal(radius, amount)",
          "projectile(damage, speed)",  "apply_status(e, type, dur)",
          "distance(v3, v3) -> float",  "normalize(v3) -> v3",
          "length(v3) -> float",        "vec3(x, y, z)",
          "fail(\"reason\")",           "log(\"msg\")",
      };
      for (auto *r : REFS) {
        ImGui::PushStyleColor(ImGuiCol_Text, {0.6f, 0.8f, 0.6f, 1.f});
        ImGui::TextUnformatted(r);
        ImGui::PopStyleColor();
      }

      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();

      // Delete button
      ImGui::PushStyleColor(ImGuiCol_Button, {0.5f, 0.1f, 0.1f, 1.f});
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.7f, 0.15f, 0.15f, 1.f});
      if (ImGui::Button("Delete Spell", {-1.f, 30.f})) {
        if (server) {
          SpellDeleteReqPacket pkt;
          pkt.spellName = sp.name;
          Net::sendReliable(server, pkt.serialize());
        }
        // Remove from active slots
        for (int s = 0; s < SPELL_SLOTS; s++)
          if (cinv.inv.activeSpells[s] == _selectedSpell)
            cinv.inv.activeSpells[s] = -1;
        cinv.inv.spellBook[_selectedSpell].clear();
        _selectedSpell = -1;
        memset(_editorBuf, 0, sizeof(_editorBuf));
        _lastError.clear();
        _lastMana = 0.f;
        _hlLines.clear();
      }
      ImGui::PopStyleColor(2);

    } else {
      ImGui::TextDisabled("No spell selected.");
      ImGui::Spacing();
      ImGui::TextDisabled("Create or select a spell\nfrom the left panel.");
      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();

      ImGui::TextColored({0.6f, 0.4f, 1.f, 1.f}, "QUICK START");
      ImGui::Spacing();
      ImGui::TextWrapped("1. Click '+ New Spell'\n"
                         "2. Edit name + source\n"
                         "3. Click Compile\n"
                         "4. Assign to a slot (1-5)\n"
                         "5. Hold R/T/Y/Z/X to cast");
    }
  }

  // ── Highlight rendering ───────────────────────────────────────────────────
  void _rebuildHighlight() {
    _hlLines.clear();
    std::string src(_editorBuf);
    if (src.empty())
      return;

    // Split into lines
    std::vector<std::string> lines;
    std::string cur;
    for (char c : src) {
      if (c == '\n') {
        lines.push_back(cur);
        cur.clear();
      } else
        cur += c;
    }
    lines.push_back(cur);

    // Lex the whole source — tokens have line numbers
    Aether::Lexer lexer(src);
    auto tokens = lexer.tokenize();

    _hlLines.resize(lines.size());

    // Map tokens back to per-line byte offsets
    // We need to find where each token starts in its line.
    // Rebuild line start offsets.
    std::vector<int> lineStart;
    lineStart.push_back(0);
    for (int i = 0; i < (int)src.size(); i++)
      if (src[i] == '\n')
        lineStart.push_back(i + 1);

    for (auto &tok : tokens) {
      if (tok.type == Aether::TokenType::Eof)
        break;
      int lineIdx = tok.line - 1; // 1-based → 0-based
      if (lineIdx < 0 || lineIdx >= (int)_hlLines.size())
        continue;
      if (tok.value.empty())
        continue;

      // Find column: search for token value in source starting at line start
      int ls = (lineIdx < (int)lineStart.size()) ? lineStart[lineIdx] : 0;
      // Simple scan: find first occurrence of token text at or after ls
      std::string_view sv(src.c_str() + ls);
      auto pos = sv.find(tok.value);
      if (pos == std::string_view::npos)
        continue;

      HlSpan span;
      span.col = (int)pos;
      span.len = (int)tok.value.size();
      span.color = tokenColor(tok.type);
      _hlLines[lineIdx].spans.push_back(span);
    }
  }

  void _rebuildErrorLines() {
    _errorLines.clear();
    if (_lastError.empty())
      return;
    // Parse "line N" or "(line N)" from error string
    const std::string &e = _lastError;
    size_t p = 0;
    while ((p = e.find("line ", p)) != std::string::npos) {
      p += 5;
      int lineNo = 0;
      while (p < e.size() && std::isdigit(e[p]))
        lineNo = lineNo * 10 + (e[p++] - '0');
      if (lineNo > 0)
        _errorLines.push_back(lineNo);
    }
  }

  void drawHighlight(ImVec2 editorPos, float w, float h) {
    if (_hlLines.empty())
      return;

    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImFont *font = ImGui::GetFont();
    float fontSize = ImGui::GetFontSize();
    float lineH = ImGui::GetTextLineHeightWithSpacing();

    float scrollY = ImGui::GetScrollY();

    float startY = editorPos.y - scrollY;
    // Find visible line range
    int firstLine = std::max(0, (int)(scrollY / lineH));
    int lastLine =
        std::min((int)_hlLines.size() - 1, firstLine + (int)(h / lineH) + 2);

    // Clip rect
    dl->PushClipRect(editorPos, {editorPos.x + w, editorPos.y + h}, true);

    for (int li = firstLine; li <= lastLine; li++) {
      float ly = startY + li * lineH;
      if (ly > editorPos.y + h)
        break;

      const HlLine &hl = _hlLines[li];
      // Get the raw line text to measure prefix widths
      // We render each span at its column offset
      for (const HlSpan &sp : hl.spans) {
        // Measure pixel offset of span.col characters
        // Use monospace assumption: each char = fontSize * 0.6
        float charW = font->CalcTextSizeA(fontSize, FLT_MAX, 0.f, "M").x;
        float px = editorPos.x + sp.col * charW + 4.f;

        // Extract the actual substring
        // Find line in _editorBuf
        const char *buf = _editorBuf;
        int lineStart = 0;
        for (int row = 0; row < li && *buf; buf++) {
          if (*buf == '\n') {
            row++;
            lineStart = (int)(buf - _editorBuf + 1);
          }
        }
        // buf now points to start of line li
        const char *linePtr = _editorBuf + lineStart;
        int lineLen = 0;
        while (linePtr[lineLen] && linePtr[lineLen] != '\n')
          lineLen++;

        if (sp.col >= lineLen)
          continue;
        int drawLen = std::min(sp.len, lineLen - sp.col);
        if (drawLen <= 0)
          continue;

        dl->AddText(font, fontSize, {px, ly}, sp.color, linePtr + sp.col,
                    linePtr + sp.col + drawLen);
      }

      // If no spans on this line, draw plain text
      if (hl.spans.empty()) {
        const char *buf = _editorBuf;
        int lineStart = 0;
        for (int row = 0; row < li && *buf; buf++) {
          if (*buf == '\n') {
            row++;
            lineStart = (int)(buf - _editorBuf + 1);
          }
        }
        const char *linePtr = _editorBuf + lineStart;
        int lineLen = 0;
        while (linePtr[lineLen] && linePtr[lineLen] != '\n')
          lineLen++;
        if (lineLen > 0)
          dl->AddText(font, fontSize, {editorPos.x + 4.f, ly},
                      IM_COL32(210, 210, 210, 255), linePtr, linePtr + lineLen);
      }
    }
    dl->PopClipRect();
  }

  void drawErrorUnderlines(ImVec2 editorPos, float w) {
    if (_errorLines.empty())
      return;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    float lineH = ImGui::GetTextLineHeightWithSpacing();

    float scrollY = ImGui::GetScrollY();

    for (int errLine : _errorLines) {
      int li = errLine - 1; // 0-based
      float ly = editorPos.y - scrollY + li * lineH + lineH - 3.f;
      dl->AddLine({editorPos.x + 4.f, ly}, {editorPos.x + w - 4.f, ly},
                  IM_COL32(255, 60, 60, 200), 1.5f);
    }
  }

  // ── Network ───────────────────────────────────────────────────────────────
  void sendCompile(ENetPeer *server) {
    _compiling = true;
    Log::info("attempting to send comile req");
    _lastError.clear();

    SpellCompileReqPacket pkt;
    pkt.spellName = _nameBuf;
    pkt.source = _editorBuf;

	  Log::info("req info:");
	  Log::info(pkt.spellName);
	  Log::info(pkt.source);
    Net::sendReliable(server, pkt.serialize());
  }

  void sendLoadout(const CInventory &cinv, ENetPeer *server) {
    SpellLoadoutSetPacket pkt;
    for (int s = 0; s < SPELL_SLOTS; s++) {
      int idx = cinv.inv.activeSpells[s];
      if (idx >= 0 && idx < SPELL_BOOK_SIZE && !cinv.inv.spellBook[idx].empty())
        pkt.slots[s] = cinv.inv.spellBook[idx].name;
      else
        pkt.slots[s] = "";
    }
    Net::sendReliable(server, pkt.serialize());
  }
};
