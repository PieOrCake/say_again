# Chip Editor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace Say Again's add/edit message editor (per-line `InputText` + a faded grey preview row) with a rich editor where chat-link codes render as inline, atomic, rarity-coloured **chips**, editor-only, multi-line preserved.

**Architecture:** Port Pie UI's `ChipTextEdit` widget (a single-line custom input built on `stb_textedit`) into Say Again, stripped of the chatbox keyboard-capture plumbing (unneeded inside the Nexus Options window). Chip names + colours resolve through a new `ChipResolve` adapter backed by `DecoderClient` (the Decoder Ring client added in the prior migration); items are rarity-tinted, other link types get a default link tint. The existing multi-line editor keeps its line-list shape — each line becomes one `ChipTextEdit` instance (owned via `unique_ptr`), and the per-line grey preview row is removed because resolved names now live inline as chips.

**Tech Stack:** C++17, MinGW cross-compile (`x86_64-w64-mingw32-g++`), CMake → `build/SayAgain.dll`, Dear ImGui 1.80 (`lib/imgui`, ships `imstb_textedit.h`), Decoder Ring client (`DecoderClient` / `DecoderRingApi.h`).

## Global Constraints

- Build/verify after every code change: `cd /home/tony/Dev/say_again/build && make`. A task is not done until this succeeds with no new warnings on the touched files.
- This project has **no host unit-test framework**. Verification per task = a clean DLL build plus the stated inspection; the editor's interactive behaviour is verified in-game by the user (Task 3 / Task 4).
- **Editor-only.** Chips appear only in the add/edit message editor. The floating message panel and its tooltips keep showing plain decoded text via `LinkResolve::Display` — do NOT change them.
- **Multi-line preserved.** The editor stays multi-line: one chip box per line, `>` prefix, Enter splits the line at the caret, `x` deletes a line (hidden when only one line remains).
- **Rarity colour is item-only.** Item links (`ChatLinks::LinkType::Item`) are tinted by `DecoderRecord::rarity`; every other link type (waypoints, skills, traits, builds, …) uses a single default link tint. Unresolved / Decoder-Ring-absent → generic placeholder (`[Waypoint]`) + default tint.
- The stored message text (the raw `[&...]` codes) is the send form and must round-trip exactly: `SetText(s)` then `GetText()` reproduces every code verbatim. Chips never mutate the underlying code.
- `ChipTextEdit` owns a raw `stb_textedit` state pointer — it is **non-copyable**; the editor stores `std::vector<std::unique_ptr<ChipTextEdit>>`.
- GW2 caps a chat line at 199 characters; the widget enforces this per line (`kMaxChars = 199`).
- Reference source (copy-and-adapt origin): `/home/tony/Dev/pie_ui/src/widgets/ChipTextEdit.{h,cpp}`. Do not add a dependency on Pie UI; the ported files are self-contained.
- Quoted `#include "X.h"` resolves headers in `src/` (same dir) and `lib/imgui` (a CMake include dir). Do not add include paths.
- Never deploy the DLL; the user deploys. Do not bump the version. No Co-Authored-By trailers. Do not push.

---

### Task 1: ChipResolve adapter (Decoder Ring-backed chip name + colour)

A tiny module that turns one `[&base64]` code into a bracketed display name and an `ImU32` colour. Items are rarity-tinted from Decoder Ring's record; other types use a default link tint; unresolved codes fall back to the generic placeholder. Builds standalone (the widget consumes it in Task 2).

**Files:**
- Create: `/home/tony/Dev/say_again/src/ChipResolve.h`
- Create: `/home/tony/Dev/say_again/src/ChipResolve.cpp`
- Modify: `/home/tony/Dev/say_again/CMakeLists.txt` (SOURCES list)

**Interfaces:**
- Consumes: `ChatLinks::ParseSegments`, `ChatLinks::GenericLabel`, `ChatLinks::Link` (`type`, `raw`, `primary_id`, `wire_byte`); `DecoderClient::Resolve(uint8_t, uint32_t, const char*, DecoderRecord&)`; `DecoderRecord` + `DecoderRarity` (`DR_Junk`..`DR_Legendary`) from `DecoderRingApi.h` (pulled in via `DecoderClient.h`).
- Produces: `void ChipResolve::Resolve(const std::string& code, std::string& outName, ImU32& outColor);`

- [ ] **Step 1: Create `src/ChipResolve.h`**

```cpp
#pragma once
#include <string>
#include "imgui.h"   // ImU32

// Resolve a single "[&base64]" chat-link code to a bracketed display name and a colour, for the
// chip editor. Names come from Decoder Ring (via DecoderClient): item links are tinted by rarity,
// all other link types get a default link tint. When Decoder Ring is absent or the lookup is not
// yet warm, returns the generic placeholder (e.g. "[Waypoint]") with the default tint. outName is
// never left empty for a decodable code.
namespace ChipResolve {
void Resolve(const std::string& code, std::string& outName, ImU32& outColor);
}
```

- [ ] **Step 2: Create `src/ChipResolve.cpp`**

```cpp
#include "ChipResolve.h"
#include "ChatLinks.h"
#include "DecoderClient.h"   // pulls in DecoderRingApi.h (DecoderRecord, DecoderRarity)

namespace ChipResolve {

// Default tint for non-item links (waypoints, skills, traits, builds, …): a light blue echoing
// GW2's in-chat link styling.
static const ImU32 kDefaultChipColor = IM_COL32(150, 200, 255, 255);

// GW2 item-rarity colours, keyed by DecoderRarity.
static ImU32 RarityColor(uint8_t rarity) {
    switch (rarity) {
        case DR_Junk:       return IM_COL32(170, 170, 170, 255);
        case DR_Basic:      return IM_COL32(255, 255, 255, 255);
        case DR_Fine:       return IM_COL32( 98, 164, 243, 255);
        case DR_Masterwork: return IM_COL32( 38, 168,  16, 255);
        case DR_Rare:       return IM_COL32(252, 208,  11, 255);
        case DR_Exotic:     return IM_COL32(255, 164,   5, 255);
        case DR_Ascended:   return IM_COL32(251,   0, 127, 255);
        case DR_Legendary:  return IM_COL32(168,  80, 255, 255);
        default:            return kDefaultChipColor;   // DR_RarityUnknown / unresolved
    }
}

void Resolve(const std::string& code, std::string& outName, ImU32& outColor) {
    outName.clear();
    outColor = kDefaultChipColor;

    auto segs = ChatLinks::ParseSegments(code);
    if (segs.empty() || !segs.front().is_link) {
        outName = ChatLinks::GenericLabel(ChatLinks::LinkType::Unknown);   // "[Link]"
        return;
    }
    const ChatLinks::Link& lk = segs.front().link;

    DecoderRecord rec;
    if (DecoderClient::Resolve(lk.wire_byte, lk.primary_id, lk.raw.c_str(), rec) && rec.name[0]) {
        outName = "[";
        outName += rec.name;
        outName += "]";
        outColor = (lk.type == ChatLinks::LinkType::Item) ? RarityColor(rec.rarity)
                                                          : kDefaultChipColor;
    } else {
        outName  = ChatLinks::GenericLabel(lk.type);   // "[Waypoint]" / "[Item]" / …
        outColor = kDefaultChipColor;
    }
}

} // namespace ChipResolve
```

- [ ] **Step 3: Add the source to `CMakeLists.txt`**

In the `set(SOURCES …)` block, add `src/ChipResolve.cpp` after `src/LinkResolve.cpp`:

```cmake
    src/DecoderClient.cpp
    src/LinkResolve.cpp
    src/ChipResolve.cpp
    ${IMGUI_SOURCES}
```

- [ ] **Step 4: Build**

Run: `cd /home/tony/Dev/say_again/build && make`
Expected: clean build. `ChipResolve` compiles though nothing calls it yet.

- [ ] **Step 5: Commit**

```bash
cd /home/tony/Dev/say_again
git add src/ChipResolve.h src/ChipResolve.cpp CMakeLists.txt
git commit -m "Add ChipResolve: Decoder Ring-backed chip name + rarity colour"
```

---

### Task 2: Port the ChipTextEdit widget

Bring Pie UI's `ChipTextEdit` into Say Again, adapted: chatbox keyboard-capture plumbing removed (the editor lives in the Nexus Options window, which already owns focus), chip resolution routed through `ChipResolve`, segment parsing routed through Say Again's `ChatLinks::ParseSegments`, the widget made explicitly non-copyable, and three small methods added for multi-line split support. Builds; not yet wired into the editor.

**Files:**
- Create: `/home/tony/Dev/say_again/src/ChipTextEdit.h`
- Create: `/home/tony/Dev/say_again/src/ChipTextEdit.cpp`
- Modify: `/home/tony/Dev/say_again/CMakeLists.txt` (SOURCES list)

**Interfaces:**
- Consumes: `ChipResolve::Resolve` (Task 1); `ChatLinks::ParseSegments` + `ChatLinks::Segment`/`Link` (`is_link`, `text`, `link.raw`); ImGui 1.80; `imstb_textedit.h`.
- Produces (namespace `ChipUI`):
  - `struct ChipCell { bool isChip; unsigned int cp; std::string code; float width; std::string name; ImU32 color; };`
  - `struct ChipTextEdit` with: `bool Render(const char* id, float width, ImU32 borderCol, ImU32 textColor = 0);` `void InsertChip(const std::string&);` `void SetText(const std::string&);` `std::string GetText() const;` `int SendLength() const;` `bool Empty() const;` `void Clear();` `void Focus();` `bool IsFocused() const;` `int CaretCell() const;` `std::vector<ChipCell> SplitOffTail();` `void SetCells(std::vector<ChipCell>, int cursor);` — non-copyable, owns an opaque stb state pointer.

- [ ] **Step 1: Create `src/ChipTextEdit.h`**

```cpp
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
    bool        IsFocused() const { return focused; }

    // --- multi-line split support (Say Again editor) ---
    int                   CaretCell() const;   // current caret cell index (clamped)
    std::vector<ChipCell> SplitOffTail();      // remove + return cells from caret to end
    void                  SetCells(std::vector<ChipCell> newCells, int cursor);
};

} // namespace ChipUI
```

- [ ] **Step 2: Create `src/ChipTextEdit.cpp`**

```cpp
#include "ChipTextEdit.h"
#include "ChipResolve.h"     // ChipResolve::Resolve
#include "ChatLinks.h"       // ChatLinks::ParseSegments (parse [&..] back into chips)
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>

// stb_textedit, included in two passes (the pattern ImGui itself uses): first for the TYPES, then
// again with our macros + IMPLEMENTATION. We deliberately do NOT include imgui_internal.h, so this
// is the ONLY stb_textedit instance in the TU — no clash with ImGui's own ImStb-namespaced copy.
#include "imstb_textedit.h"    // pass 1: types only

static inline int   ClampI(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline float ClampF(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

static constexpr int kGw2ChatMaxChars = ChipUI::ChipTextEdit::kMaxChars;
static inline int Utf8Bytes(unsigned cp) { return cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4; }

// ---- stb_textedit glue: STRING = ChipTextEdit, each "char" = one ChipCell --------------------
namespace ChipUI {

static int SendLen(const std::vector<ChipCell>& cells) {
    int n = 0;
    for (const auto& c : cells) n += c.isChip ? (int)c.code.size() : Utf8Bytes(c.cp);
    return n;
}

static int   ChipTE_StringLen(ChipTextEdit* o)              { return (int)o->cells.size(); }
static int   ChipTE_GetChar(ChipTextEdit* o, int i)         { return o->cells[i].isChip ? 0xFFFC : (int)o->cells[i].cp; }
static float ChipTE_GetWidth(ChipTextEdit* o, int, int i)   { return o->cells[i].width; }

static void  ChipTE_Layout(StbTexteditRow* r, ChipTextEdit* o, int) {
    float w = 0.0f;
    for (auto& c : o->cells) w += c.width;
    const float lh = ImGui::GetTextLineHeight();
    r->x0 = 0.0f; r->x1 = w;
    r->baseline_y_delta = lh;
    r->ymin = 0.0f; r->ymax = lh;
    r->num_chars = (int)o->cells.size();
}

static void  ChipTE_DeleteChars(ChipTextEdit* o, int i, int n) {
    if (i < 0 || n <= 0 || i >= (int)o->cells.size()) return;
    n = std::min(n, (int)o->cells.size() - i);
    o->cells.erase(o->cells.begin() + i, o->cells.begin() + i + n);
}
static int   ChipTE_InsertChars(ChipTextEdit* o, int i, const int* chars, int n) {
    if (i < 0 || i > (int)o->cells.size()) return 0;
    int budget = kGw2ChatMaxChars - SendLen(o->cells);
    std::vector<ChipCell> ins; ins.reserve(n);
    for (int k = 0; k < n; ++k) {
        int b = Utf8Bytes((unsigned)chars[k]);
        if (b > budget) break;
        budget -= b;
        ChipCell c; c.isChip = false; c.cp = (unsigned)chars[k]; ins.push_back(std::move(c));
    }
    if (ins.empty()) return 0;
    o->cells.insert(o->cells.begin() + i, ins.begin(), ins.end());
    return 1;
}

} // namespace ChipUI

#define STB_TEXTEDIT_CHARTYPE       int
#define STB_TEXTEDIT_POSITIONTYPE   int
#define STB_TEXTEDIT_STRING         ChipUI::ChipTextEdit
#define STB_TEXTEDIT_STRINGLEN(o)            ChipUI::ChipTE_StringLen(o)
#define STB_TEXTEDIT_GETCHAR(o,i)            ChipUI::ChipTE_GetChar(o,i)
#define STB_TEXTEDIT_GETWIDTH(o,ls,i)        ChipUI::ChipTE_GetWidth(o,ls,i)
#define STB_TEXTEDIT_LAYOUTROW(r,o,ls)       ChipUI::ChipTE_Layout(r,o,ls)
#define STB_TEXTEDIT_DELETECHARS(o,i,n)      ChipUI::ChipTE_DeleteChars(o,i,n)
#define STB_TEXTEDIT_INSERTCHARS(o,i,c,n)    ChipUI::ChipTE_InsertChars(o,i,c,n)
#define STB_TEXTEDIT_KEYTOTEXT(k)            ((k) >= 0x200000 ? 0 : (k))
#define STB_TEXTEDIT_NEWLINE                 '\n'
#define STB_TEXTEDIT_GETWIDTH_NEWLINE        (-1.0f)
#define STB_TEXTEDIT_K_LEFT       0x200000
#define STB_TEXTEDIT_K_RIGHT      0x200001
#define STB_TEXTEDIT_K_UP         0x200002
#define STB_TEXTEDIT_K_DOWN       0x200003
#define STB_TEXTEDIT_K_LINESTART  0x200004
#define STB_TEXTEDIT_K_LINEEND    0x200005
#define STB_TEXTEDIT_K_TEXTSTART  0x200006
#define STB_TEXTEDIT_K_TEXTEND    0x200007
#define STB_TEXTEDIT_K_DELETE     0x200008
#define STB_TEXTEDIT_K_BACKSPACE  0x200009
#define STB_TEXTEDIT_K_UNDO       0x20000A
#define STB_TEXTEDIT_K_REDO       0x20000B
#define STB_TEXTEDIT_K_WORDLEFT   0x20000C
#define STB_TEXTEDIT_K_WORDRIGHT  0x20000D
#define STB_TEXTEDIT_K_PGUP       0x20000E
#define STB_TEXTEDIT_K_PGDOWN     0x20000F
#define STB_TEXTEDIT_K_SHIFT      0x400000
#define STB_TEXTEDIT_IMPLEMENTATION
#include "imstb_textedit.h"

namespace ChipUI {

static STB_TexteditState& St(ChipTextEdit* o) {
    if (!o->m_stb) { auto* s = new STB_TexteditState(); stb_textedit_initialize_state(s, 1); o->m_stb = s; }
    return *static_cast<STB_TexteditState*>(o->m_stb);
}

ChipTextEdit::~ChipTextEdit() { delete static_cast<STB_TexteditState*>(m_stb); m_stb = nullptr; }

// ---- UTF-8 <-> codepoint helpers --------------------------------------------------------------
static void AppendUtf8(std::string& s, unsigned int cp) {
    if (cp < 0x80) s += (char)cp;
    else if (cp < 0x800) { s += (char)(0xC0 | (cp >> 6)); s += (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { s += (char)(0xE0 | (cp >> 12)); s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F)); }
    else { s += (char)(0xF0 | (cp >> 18)); s += (char)(0x80 | ((cp >> 12) & 0x3F)); s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F)); }
}
static std::vector<unsigned int> DecodeUtf8(const std::string& s) {
    std::vector<unsigned int> out;
    for (size_t i = 0; i < s.size();) {
        unsigned char b = (unsigned char)s[i];
        unsigned int cp; int n;
        if (b < 0x80)       { cp = b; n = 1; }
        else if (b < 0xE0)  { cp = b & 0x1F; n = 2; }
        else if (b < 0xF0)  { cp = b & 0x0F; n = 3; }
        else                { cp = b & 0x07; n = 4; }
        if (i + (size_t)n > s.size()) break;
        for (int k = 1; k < n; ++k) cp = (cp << 6) | ((unsigned char)s[i + k] & 0x3F);
        out.push_back(cp); i += n;
    }
    return out;
}

// Parse `s` into (isLink, text) segments via Say Again's ChatLinks. ParseSegments returns an EMPTY
// vector when there are no codes, so treat that as a single plain-text segment (never drop text).
static void ForEachSegment(const std::string& s,
        const std::function<void(bool isLink, const std::string& text)>& fn) {
    auto segs = ChatLinks::ParseSegments(s);
    if (segs.empty()) { fn(false, s); return; }
    for (const auto& seg : segs) {
        if (seg.is_link) fn(true, seg.link.raw);
        else             fn(false, seg.text);
    }
}

// ---- per-frame cell measurement ---------------------------------------------------------------
static void Measure(std::vector<ChipCell>& cells) {
    ImFont* font = ImGui::GetFont();
    const float scale = ImGui::GetFontSize() / font->FontSize;
    for (auto& c : cells) {
        if (c.isChip) {
            std::string name; ImU32 col;
            ChipResolve::Resolve(c.code, name, col);
            if (name.empty()) name = c.name.empty() ? "[link]" : c.name;
            c.name = name; c.color = col;
            c.width = ImGui::CalcTextSize(name.c_str()).x + 6.0f;   // small side padding
        } else {
            c.width = font->GetCharAdvance((ImWchar)c.cp) * scale;
        }
    }
}

void ChipTextEdit::Clear() { cells.clear(); stb_textedit_initialize_state(&St(this), 1); }

void ChipTextEdit::InsertChip(const std::string& code) {
    if (SendLen(cells) + (int)code.size() > kGw2ChatMaxChars) return;
    STB_TexteditState& s = St(this);
    int at = ClampI(s.cursor, 0, (int)cells.size());
    ChipCell c; c.isChip = true; c.code = code;
    ChipResolve::Resolve(code, c.name, c.color);
    if (c.name.empty()) c.name = "[link]";
    cells.insert(cells.begin() + at, std::move(c));
    s.cursor = at + 1;
    s.select_start = s.select_end = s.cursor;
}

std::string ChipTextEdit::GetText() const {
    std::string out;
    for (const auto& c : cells) { if (c.isChip) out += c.code; else AppendUtf8(out, c.cp); }
    return out;
}

int ChipTextEdit::SendLength() const { return SendLen(cells); }

void ChipTextEdit::SetText(const std::string& utf8) {
    cells.clear();
    ForEachSegment(utf8, [&](bool isLink, const std::string& t) {
        if (isLink) {
            ChipCell c; c.isChip = true; c.code = t;
            ChipResolve::Resolve(t, c.name, c.color);
            if (c.name.empty()) c.name = "[link]";
            cells.push_back(std::move(c));
        } else {
            for (unsigned int cp : DecodeUtf8(t)) { ChipCell c; c.isChip = false; c.cp = cp; cells.push_back(std::move(c)); }
        }
    });
    STB_TexteditState& s = St(this);
    s.cursor = (int)cells.size();
    s.select_start = s.select_end = s.cursor;
}

int ChipTextEdit::CaretCell() const {
    if (!m_stb) return 0;
    int c = static_cast<STB_TexteditState*>(m_stb)->cursor;
    return ClampI(c, 0, (int)cells.size());
}

std::vector<ChipCell> ChipTextEdit::SplitOffTail() {
    int cut = CaretCell();
    std::vector<ChipCell> tail(cells.begin() + cut, cells.end());
    cells.erase(cells.begin() + cut, cells.end());
    STB_TexteditState& s = St(this);
    s.cursor = s.select_start = s.select_end = (int)cells.size();
    return tail;
}

void ChipTextEdit::SetCells(std::vector<ChipCell> newCells, int cursor) {
    cells = std::move(newCells);
    STB_TexteditState& s = St(this);
    stb_textedit_initialize_state(&s, 1);
    s.cursor = s.select_start = s.select_end = ClampI(cursor, 0, (int)cells.size());
}

// x position (px from text start) of caret index i.
static float CaretX(const std::vector<ChipCell>& cells, int i) {
    float x = 0.0f;
    for (int k = 0; k < i && k < (int)cells.size(); ++k) x += cells[k].width;
    return x;
}

bool ChipTextEdit::Render(const char* id, float width, ImU32 borderCol, ImU32 textColor) {
    STB_TexteditState& s = St(this);
    ImGuiIO& io = ImGui::GetIO();
    ImGuiStyle& style = ImGui::GetStyle();

    const float h = ImGui::GetFrameHeight();
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, ImVec2(width, h));
    const bool hovered = ImGui::IsItemHovered();
    const bool held    = ImGui::IsItemActive();
    if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);

    // Focus: claim on click inside; release on a click that isn't ours.
    if (ImGui::IsItemClicked()) focused = true;
    else if (io.MouseClicked[0] && !hovered) focused = false;
    if (focusNext) { focused = true; focusNext = false; }
    if (focused) {
        // Make Nexus forward keyboard to ImGui. We're not a real InputText, so raise the
        // text-input flags ourselves (some hosts gate WM_CHAR on WantTextInput, not just capture).
        ImGui::CaptureKeyboardFromApp(true);
        io.WantCaptureKeyboard = true;
        io.WantTextInput       = true;
    }

    Measure(cells);

    const ImVec2 inner_min(p0.x + style.FramePadding.x, p0.y + style.FramePadding.y);
    const float  inner_w = width - style.FramePadding.x * 2.0f;

    // ---- input (only while focused) ----
    bool submitted = false;
    if (focused) {
        const float mx = io.MousePos.x - inner_min.x + scrollX;
        if (ImGui::IsItemClicked()) stb_textedit_click(this, &s, mx, 0.0f);
        else if (held && io.MouseDelta.x != 0.0f) stb_textedit_drag(this, &s, mx, 0.0f);

        for (int i = 0; i < io.InputQueueCharacters.Size; ++i) {
            ImWchar c = io.InputQueueCharacters[i];
            if (c >= 0x20 && c != 0x7F) stb_textedit_key(this, &s, c);
        }
        auto K = [&](ImGuiKey k){ int idx = io.KeyMap[k]; return idx >= 0 && ImGui::IsKeyPressed(idx); };
        const int sh = io.KeyShift ? STB_TEXTEDIT_K_SHIFT : 0;
        const bool word = io.KeyCtrl;
        if (K(ImGuiKey_LeftArrow))   stb_textedit_key(this, &s, (word ? STB_TEXTEDIT_K_WORDLEFT  : STB_TEXTEDIT_K_LEFT)  | sh);
        if (K(ImGuiKey_RightArrow))  stb_textedit_key(this, &s, (word ? STB_TEXTEDIT_K_WORDRIGHT : STB_TEXTEDIT_K_RIGHT) | sh);
        if (K(ImGuiKey_Home))        stb_textedit_key(this, &s, STB_TEXTEDIT_K_LINESTART | sh);
        if (K(ImGuiKey_End))         stb_textedit_key(this, &s, STB_TEXTEDIT_K_LINEEND | sh);
        if (K(ImGuiKey_Delete))      stb_textedit_key(this, &s, STB_TEXTEDIT_K_DELETE | sh);
        if (K(ImGuiKey_Backspace))   stb_textedit_key(this, &s, STB_TEXTEDIT_K_BACKSPACE | sh);
        if (io.KeyCtrl && K(ImGuiKey_Z)) stb_textedit_key(this, &s, STB_TEXTEDIT_K_UNDO);
        if (io.KeyCtrl && K(ImGuiKey_Y)) stb_textedit_key(this, &s, STB_TEXTEDIT_K_REDO);
        if (io.KeyCtrl && K(ImGuiKey_A)) { s.select_start = 0; s.select_end = (int)cells.size(); s.cursor = (int)cells.size(); }
        if (io.KeyCtrl && (K(ImGuiKey_C) || K(ImGuiKey_X))) {
            int a = std::min(s.select_start, s.select_end), b = std::max(s.select_start, s.select_end);
            if (b > a) {
                std::string sub; for (int k = a; k < b; ++k) { if (cells[k].isChip) sub += cells[k].code; else AppendUtf8(sub, cells[k].cp); }
                ImGui::SetClipboardText(sub.c_str());
                if (K(ImGuiKey_X)) stb_textedit_cut(this, &s);
            }
        }
        if (io.KeyCtrl && K(ImGuiKey_V)) {
            const char* clip = ImGui::GetClipboardText();
            if (clip && *clip) {
                // A pasted [&..] code becomes an atomic chip; plain text types through as chars.
                ForEachSegment(clip, [&](bool isLink, const std::string& t) {
                    if (isLink) InsertChip(t);
                    else for (unsigned int cp : DecodeUtf8(t)) if (cp >= 0x20) stb_textedit_key(this, &s, (int)cp);
                });
            }
        }
        if (K(ImGuiKey_Enter) || K(ImGuiKey_KeyPadEnter)) submitted = true;
        if (K(ImGuiKey_Escape)) focused = false;

        Measure(cells);   // input may have changed cells; re-measure for the draw below
    }

    // ---- horizontal scroll so the caret stays visible ----
    const float caretX = CaretX(cells, s.cursor);
    if (caretX - scrollX > inner_w) scrollX = caretX - inner_w;
    if (caretX - scrollX < 0.0f)    scrollX = caretX;
    float total = 0.0f; for (auto& c : cells) total += c.width;
    scrollX = ClampF(scrollX, 0.0f, std::max(0.0f, total - inner_w));

    // ---- draw ----
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p1(p0.x + width, p0.y + h);
    dl->AddRectFilled(p0, p1, ImGui::GetColorU32(ImGuiCol_FrameBg), style.FrameRounding);
    if (borderCol & 0xFF000000) dl->AddRect(p0, p1, borderCol, style.FrameRounding);

    dl->PushClipRect(ImVec2(inner_min.x, p0.y), ImVec2(p1.x - style.FramePadding.x, p1.y), true);

    if (focused && s.select_start != s.select_end) {
        int a = std::min(s.select_start, s.select_end), b = std::max(s.select_start, s.select_end);
        float ax = inner_min.x + CaretX(cells, a) - scrollX;
        float bx = inner_min.x + CaretX(cells, b) - scrollX;
        dl->AddRectFilled(ImVec2(ax, inner_min.y), ImVec2(bx, inner_min.y + ImGui::GetTextLineHeight()),
                          ImGui::GetColorU32(ImGuiCol_TextSelectedBg));
    }

    const ImU32 textCol = textColor ? textColor : ImGui::GetColorU32(ImGuiCol_Text);
    float x = inner_min.x - scrollX;
    std::string run; float runStart = x;
    auto flushRun = [&]() {
        if (!run.empty()) { dl->AddText(ImVec2(runStart, inner_min.y), textCol, run.c_str()); run.clear(); }
    };
    for (auto& c : cells) {
        if (c.isChip) {
            flushRun();
            ImU32 bg = (c.color & 0x00FFFFFF) | 0x33000000;   // faint tint of the chip colour
            dl->AddRectFilled(ImVec2(x + 1, inner_min.y), ImVec2(x + c.width - 1, inner_min.y + ImGui::GetTextLineHeight()), bg, 3.0f);
            dl->AddText(ImVec2(x + 3, inner_min.y), c.color, c.name.c_str());
            x += c.width; runStart = x;
        } else {
            AppendUtf8(run, c.cp);
            x += c.width;
        }
    }
    flushRun();

    if (focused) {
        float cx = inner_min.x + caretX - scrollX;
        if (std::fmod((float)ImGui::GetTime(), 1.0f) < 0.6f)
            dl->AddLine(ImVec2(cx, inner_min.y), ImVec2(cx, inner_min.y + ImGui::GetTextLineHeight()), textCol);
    }

    dl->PopClipRect();
    return submitted;
}

} // namespace ChipUI
```

- [ ] **Step 3: Add the source to `CMakeLists.txt`**

Add `src/ChipTextEdit.cpp` after `src/ChipResolve.cpp`:

```cmake
    src/LinkResolve.cpp
    src/ChipResolve.cpp
    src/ChipTextEdit.cpp
    ${IMGUI_SOURCES}
```

- [ ] **Step 4: Build**

Run: `cd /home/tony/Dev/say_again/build && make`
Expected: clean build. The widget compiles though nothing renders it yet. If the compiler complains that `imstb_textedit.h` symbols are already defined, confirm this TU does NOT include `imgui_internal.h` (directly or transitively) — it must not.

- [ ] **Step 5: Commit**

```bash
cd /home/tony/Dev/say_again
git add src/ChipTextEdit.h src/ChipTextEdit.cpp CMakeLists.txt
git commit -m "Add ChipTextEdit widget (ported from Pie UI, options-window adapted)"
```

---

### Task 3: Integrate chips into the message editor

Rewrite `RenderDecoratedEditor` so each line is a `ChipTextEdit` (owned via `unique_ptr`) instead of an `InputText`, dropping the per-line faded preview row. Keep the `>` prefix, Enter-splits-at-caret, and the `x` delete button. Remove the now-dead `InputText` callback helpers.

**Files:**
- Modify: `/home/tony/Dev/say_again/src/dllmain.cpp` — add includes; replace the `s_DecLines`/`s_DecFocusLine` state and the whole `RenderDecoratedEditor` body (lines ~263–384); remove `LineCallbackData` (line ~245) and `LineInputCallback` (line ~251).

**Interfaces:**
- Consumes: `ChipUI::ChipTextEdit` and its methods `SetText`, `GetText`, `Render`, `Focus`, `CaretCell`, `SplitOffTail`, `SetCells` (Task 2).
- The function signature `static void RenderDecoratedEditor(char* buf, size_t bufSize, bool resetState)` is UNCHANGED — its one call site (`RenderDecoratedEditor(textBuf, sizeof(textBuf), decReset);` at line ~728) stays as-is.

- [ ] **Step 1: Add includes to `dllmain.cpp`**

Near the other includes at the top of `src/dllmain.cpp`, add:

```cpp
#include "ChipTextEdit.h"
#include <memory>
```

(`#include "LinkResolve.h"` is already present from the prior migration and is still used elsewhere in this file — leave it.)

- [ ] **Step 2: Remove the dead InputText callback helpers**

Delete the `LineCallbackData` struct (around line 245) and the `LineInputCallback` function (around line 251) in their entirety. They were only used by the old per-line `InputText`. After deletion, confirm no remaining references:

```bash
cd /home/tony/Dev/say_again && grep -n "LineCallbackData\|LineInputCallback" src/dllmain.cpp
```
Expected: no output.

- [ ] **Step 3: Replace the editor state declarations**

Replace these two lines (around 263–264):

```cpp
static std::vector<std::string> s_DecLines;
static int s_DecFocusLine = -1;
```

with:

```cpp
// One chip box per line. ChipTextEdit owns a raw stb_textedit pointer (non-copyable), so the
// lines are held by unique_ptr. The combined message text is each line's GetText() joined by '\n'.
static std::vector<std::unique_ptr<ChipUI::ChipTextEdit>> s_DecLines;
static int s_DecFocusLine = -1;
```

- [ ] **Step 4: Replace the whole `RenderDecoratedEditor` body**

Replace the entire function `RenderDecoratedEditor` (from `static void RenderDecoratedEditor(char* buf, size_t bufSize, bool resetState) {` through its closing brace, around lines 266–384) with:

```cpp
static void RenderDecoratedEditor(char* buf, size_t bufSize, bool resetState) {
    if (resetState || s_DecLines.empty()) {
        s_DecLines.clear();
        std::string full(buf);
        std::vector<std::string> parts;
        size_t start = 0, pos;
        while ((pos = full.find('\n', start)) != std::string::npos) {
            parts.push_back(full.substr(start, pos - start));
            start = pos + 1;
        }
        parts.push_back(full.substr(start));
        if (parts.empty()) parts.push_back("");
        for (const auto& p : parts) {
            auto line = std::make_unique<ChipUI::ChipTextEdit>();
            line->SetText(p);
            s_DecLines.push_back(std::move(line));
        }
        s_DecFocusLine = -1;
    }

    static const ImVec4 kGreen(0.2f, 0.8f, 0.2f, 1.0f);
    static const ImVec4 kRed      (0.55f, 0.10f, 0.10f, 1.0f);
    static const ImVec4 kRedHover (0.75f, 0.15f, 0.15f, 1.0f);
    static const ImVec4 kRedActive(0.90f, 0.20f, 0.20f, 1.0f);

    int  pendingEnterIdx  = -1;
    int  pendingDeleteIdx = -1;
    bool showX = s_DecLines.size() > 1;

    float lineH = ImGui::GetFrameHeightWithSpacing();
    float padY  = ImGui::GetStyle().WindowPadding.y * 2.0f;
    int   vis   = std::min((int)s_DecLines.size(), 6);
    float childH = (float)vis * lineH + padY;

    float arrowW = ImGui::CalcTextSize(">").x + ImGui::GetStyle().ItemSpacing.x;
    float xBtnW  = showX ? (ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x) : 0.0f;

    ImGui::BeginChild("##deceditor", ImVec2(400, childH), true);

    float inputW = ImGui::GetContentRegionAvail().x - arrowW - xBtnW;

    for (int i = 0; i < (int)s_DecLines.size(); ++i) {
        ImGui::TextColored(kGreen, ">"); ImGui::SameLine();

        char id[32];
        snprintf(id, sizeof(id), "##chipline%d", i);

        if (s_DecFocusLine == i) { s_DecLines[i]->Focus(); s_DecFocusLine = -1; }

        bool entered = s_DecLines[i]->Render(id, inputW, 0, 0);

        if (showX) {
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button,        kRed);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kRedHover);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  kRedActive);
            char xId[32]; snprintf(xId, sizeof(xId), "x##del%d", i);
            if (ImGui::Button(xId, ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight())))
                pendingDeleteIdx = i;
            ImGui::PopStyleColor(3);
        }

        if (entered && pendingEnterIdx == -1) pendingEnterIdx = i;
    }

    ImGui::EndChild();

    // Split line at the caret on Enter.
    if (pendingEnterIdx != -1) {
        int i = pendingEnterIdx;
        auto tail = s_DecLines[i]->SplitOffTail();
        auto nl = std::make_unique<ChipUI::ChipTextEdit>();
        nl->SetCells(std::move(tail), 0);
        s_DecLines.insert(s_DecLines.begin() + i + 1, std::move(nl));
        s_DecFocusLine = i + 1;
    }
    // Delete line via X button (keep at least one line).
    else if (pendingDeleteIdx != -1 && (int)s_DecLines.size() > 1) {
        int i = pendingDeleteIdx;
        s_DecLines.erase(s_DecLines.begin() + i);
        s_DecFocusLine = std::max(0, i - 1);
    }

    // Reconstruct buf from each line's send text (codes round-trip verbatim).
    std::string result;
    for (int i = 0; i < (int)s_DecLines.size(); ++i) {
        if (i > 0) result += '\n';
        result += s_DecLines[i]->GetText();
    }
    strncpy(buf, result.c_str(), bufSize - 1);
    buf[bufSize - 1] = '\0';
}
```

- [ ] **Step 5: Build**

Run: `cd /home/tony/Dev/say_again/build && make`
Expected: clean build. Then confirm no stale references remain:

```bash
cd /home/tony/Dev/say_again && grep -n "s_DecLines\[.*\]\.\|LineInputCallback\|lineBuf" src/dllmain.cpp
```
Expected: no output (all line access now goes through `->`, and the old `InputText` locals are gone).

- [ ] **Step 6: Commit**

```bash
cd /home/tony/Dev/say_again
git add src/dllmain.cpp
git commit -m "Editor: render message lines as inline chip boxes (replaces preview rows)"
```

- [ ] **Step 7: In-game verification (user-driven — the de-risk checkpoint)**

The user deploys the DLL. With Decoder Ring installed, open the add/edit message editor and confirm:
- A line containing a code (e.g. `Next Up: Casino Blitz at [&BLsKAAA=]`) shows the code as an inline coloured **chip** reading `[Amnoon Waypoint]` — no separate grey preview row.
- **Typing works** in the editor line (this is the focus de-risk: the widget claims keyboard focus inside the Nexus Options window). Click a line, type — characters appear.
- Caret skips a chip as one unit; one Backspace next to a chip deletes the whole code.
- Enter splits the line at the caret into a new line below; the `x` button deletes a line (hidden at one line).
- Pasting a `[&...]` code turns it into a chip; pasting plain text types through.
- Save the message, reopen it — the codes survive (chips reconstruct from the saved text), and sending still pastes the raw `[&...]` into chat.
- An item code shows a rarity-coloured chip (e.g. an Exotic in orange).

> If typing does NOT register in the editor line, the keyboard-focus assumption failed. Fallback: the `Render` focused-branch already calls `ImGui::CaptureKeyboardFromApp(true)` + sets `io.WantCaptureKeyboard`/`io.WantTextInput`; if Nexus still withholds `WM_CHAR`, that is a host forwarding issue to investigate before proceeding — report it rather than guessing.

---

### Task 4: Final verification and documentation

**Files:**
- Modify: `/home/tony/Dev/say_again/handover.md` (on disk; gitignored — do NOT commit it)
- Modify: `/home/tony/Dev/say_again/plans/2026-06-07-chip-editor-discussion.md` (tracked — commit it)

- [ ] **Step 1: Full clean rebuild**

```bash
cd /home/tony/Dev/say_again/build && cmake .. && make
```
Expected: `SayAgain.dll` builds cleanly from scratch.

- [ ] **Step 2: Update `handover.md`** (on disk, no commit — it is gitignored)

In the "Source Files" section, add `ChipResolve.{h,cpp}` (single-code → bracketed name + rarity/default colour, backed by `DecoderClient`) and `ChipTextEdit.{h,cpp}` (single-line `stb_textedit` chip widget ported from Pie UI, keyboard-capture plumbing dropped, non-copyable). In the "Decorated message editor" design block, replace the "faded preview row" description: lines are now `ChipUI::ChipTextEdit` instances (one per line, held by `unique_ptr`); codes render as inline rarity-coloured chips; Enter splits at the caret via `SplitOffTail`/`SetCells`; the separate preview row is gone. Note chips are editor-only — the floating panel still uses `LinkResolve::Display` plain text.

- [ ] **Step 3: Update the discussion note** `plans/2026-06-07-chip-editor-discussion.md`

Add a dated 2026-06-17 line under "Status"/"Decisions": the chip editor is **implemented** — `ChipTextEdit` ported from Pie UI into Say Again, resolving through `ChipResolve`/`DecoderClient`, editor-only, multi-line preserved, rarity-coloured chips. Decisions settled: keyboard-capture plumbing dropped (Nexus Options window owns focus); rarity colour for items, default tint for other types.

- [ ] **Step 4: Commit the tracked doc only**

```bash
cd /home/tony/Dev/say_again
git add plans/2026-06-07-chip-editor-discussion.md
git commit -m "Docs: chip editor implemented"
```

---

## Self-Review

**Spec coverage:**
- Inline chips replacing the preview row → Task 2 (widget) + Task 3 (integration removes the preview row). ✓
- Rarity-coloured chips, items only; default tint for other types; placeholder fallback → Task 1 (`ChipResolve`). ✓
- Editor-only (panel/tooltips unchanged) → Task 3 touches only `RenderDecoratedEditor`; Global Constraints forbid panel changes. ✓
- Multi-line preserved (per-line chip boxes, Enter-split, x-delete) → Task 3. ✓
- Codes round-trip verbatim for sending → `GetText()`/`SetText()` contract (Task 2), reconstruction in Task 3. ✓
- Keyboard focus in the Nexus Options window → plumbing stripped, `CaptureKeyboardFromApp` retained; verified in Task 3 Step 7 with a stated fallback. ✓
- Paste chip-ifies codes → `ForEachSegment` in the Ctrl+V handler (Task 2). ✓
- Non-copyable widget stored by `unique_ptr` → deleted copy ctor (Task 2), `unique_ptr` vector (Task 3). ✓

**Placeholder scan:** No TBD/TODO/"handle edge cases"/"similar to Task N" — every code step carries complete code. ✓

**Type consistency:** `ChipUI::ChipTextEdit` + `ChipCell` defined in Task 2, consumed in Task 3. `ChipResolve::Resolve(const std::string&, std::string&, ImU32&)` defined in Task 1, called in Task 2 (`Measure`, `InsertChip`, `SetText`). `SplitOffTail()`/`SetCells(std::vector<ChipCell>, int)`/`CaretCell()` defined in Task 2, used in Task 3. `RenderDecoratedEditor(char*, size_t, bool)` signature unchanged, call site untouched. ✓
