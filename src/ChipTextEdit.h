#pragma once
#include "imgui.h"
#include <string>
#include <vector>

// A single-line text input that embeds chat-link "chips" as ATOMIC inline elements, matching the
// vanilla GW2 chatbox: the caret moves over a chip as one unit, one backspace/delete adjacent to it
// removes the whole chip, and surrounding text stays editable. Built on stb_textedit (the same
// engine ImGui's InputText uses). Adapted from Pie UI's widget; its chatbox keyboard-capture
// plumbing was dropped because Say Again's editor lives in the Nexus Options window, which already
// owns focus. Say Again's multi-line editor owns one instance per line (stored via unique_ptr).
namespace ChipUI {

// True when a chip box was focused within the last ~60ms (any instance). The addon's Nexus WndProc
// reads this to reinforce keyboard-capture flags so keystrokes don't leak to the game while typing:
// the widget's per-frame WantTextInput flickers false at frame start, which a WndProc check can miss.
bool ChipInputActive();

struct ChipCell {
    bool         isChip = false;
    unsigned int cp = 0;      // Unicode codepoint (text cell)
    std::string  code;        // "[&base64]" (chip cell) — the canonical send form
    // Per-frame render cache (filled at the top of Render):
    float        width = 0.0f;
    std::string  name;        // resolved bracketed label, e.g. "[Copper Ore]"
    ImU32        color = 0;
};

struct ChipTextEdit {
    // GW2 truncates any chat line past this many characters; input is capped here.
    static constexpr int kMaxChars = 199;

    std::vector<ChipCell> cells;
    void*  m_stb     = nullptr;   // opaque ::STB_TexteditState (kept out of the header)
    bool   focused   = false;
    bool   focusNext = false;
    float  scrollX   = 0.0f;

    ChipTextEdit() = default;
    ~ChipTextEdit();
    ChipTextEdit(const ChipTextEdit&)            = delete;   // owns a raw stb pointer
    ChipTextEdit& operator=(const ChipTextEdit&) = delete;

    // Render at the current ImGui cursor for `width` px. Returns true if Enter was pressed.
    // textColor tints the plain typed text + caret (0 = theme default). Chips keep their own colour.
    bool        Render(const char* id, float width, ImU32 borderCol, ImU32 textColor = 0);
    void        InsertChip(const std::string& code);   // at the caret, as one atomic cell
    void        SetText(const std::string& utf8);      // replace all (parses [&..] into chips)
    std::string GetText() const;                       // codes + text, in order (the send form)
    int         SendLength() const;
    bool        Empty() const { return cells.empty(); }
    void        Clear();
    void        Focus()           { focusNext = true; }
    void        Blur()            { focused = false; focusNext = false; }
    bool        IsFocused() const { return focused; }

    // --- multi-line split support (Say Again editor) ---
    int                   CaretCell() const;   // current caret cell index (clamped)
    std::vector<ChipCell> SplitOffTail();      // remove + return cells from caret to end
    void                  SetCells(std::vector<ChipCell> newCells, int cursor);
};

} // namespace ChipUI
