#include "ChipTextEdit.h"
#include "ChipResolve.h"     // ChipResolve::Resolve
#include "ChatLinks.h"       // ChatLinks::ParseSegments (parse [&..] back into chips)
#include <algorithm>
#include <atomic>
#include <chrono>
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
    if (ins.size() < (size_t)n) return 0;   // reject partial inserts (avoids stb cursor desync)
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

// Focus timestamp (ms, steady clock) for the WndProc keyboard-capture reinforcement (see header).
// A focused Render stamps NowMs(); ChipInputActive() is true while that stamp is recent. A timeout
// (not a per-frame reset) keeps it continuously true while typing — no reset-gap for a held key to
// leak — and self-clears shortly after focus is lost / the editor stops rendering.
static std::atomic<long long> s_lastFocusMs{-100000};
static long long NowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}
static void StampChipFocus() { s_lastFocusMs.store(NowMs(), std::memory_order_relaxed); }
bool ChipInputActive() {
    constexpr long long kHold = 60;   // ms; > a slow frame, < a perceptible input lag
    return (NowMs() - s_lastFocusMs.load(std::memory_order_relaxed)) < kHold;
}

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
        StampChipFocus();   // the addon's WndProc reinforces capture while this stays recent
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
