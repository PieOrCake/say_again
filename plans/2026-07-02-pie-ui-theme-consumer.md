# Pie UI Theme Consumer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make Say Again tint its Options panel, floating panel, and floating icon to match Pie UI's broadcast theme, live, gated by a persisted user toggle.

**Architecture:** A small `PieTheme` module (modeled on `DecoderClient`) subscribes to Pie UI's `EV_PIEUI_THEME` event, version-gates and caches the palette behind a mutex. The existing `PushGW2Theme()` (invoked by both render callbacks via `ThemeGuard`) overwrites the mapped `ImGuiStyle.Colors[...]` from the cached palette each frame when active; hand-drawn gold elements adopt the palette's accent via helpers. Pie UI is a soft, optional dependency.

**Tech Stack:** C++, Dear ImGui, Nexus addon API (Events_Subscribe / Events_Raise / Events_Unsubscribe), CMake + MinGW cross-build.

## Global Constraints

- No unit-test harness exists. Each task's verification is a clean `cd build && make` (from repo root: `cd build && make`) plus, where noted, the observable behaviour to confirm in-game.
- Pie UI is a SOFT dependency: absent / disabled / pre-broadcast → Say Again looks and behaves exactly as today. Never render blank/black.
- Accept only `PIEUI_THEME_VERSION == 1`. Any other `version` value leaves the cache unchanged.
- Palette struct layout is fixed ABI: `{ uint32_t version, accent, window_bg, header_bg, text, text_muted, border }`. Colours are packed `0xAABBGGRR`.
- The event handler may run off the render thread — copy the struct under the mutex and do no ImGui work inside it.
- Preserve every push/pop and draw-call count. The style override happens inside the existing full-style assignment in `PushGW2Theme()`; accent sites are in-place literal swaps only.
- Do not bump the version number (user controls that separately).

---

### Task 1: `PieTheme` module + lifecycle wire-up

**Files:**
- Create: `src/PieTheme.h`
- Create: `src/PieTheme.cpp`
- Modify: `CMakeLists.txt:36-45` (add source to `SOURCES`)
- Modify: `src/dllmain.cpp` (include header; call `PieTheme::Init()` beside `DecoderClient::Init()` at line 843; call `PieTheme::Shutdown()` beside `DecoderClient::Shutdown()` at line 850)

**Interfaces:**
- Consumes: `APIDefs` and `g_Settings` from `src/SharedState.h`; `ImVec4`/`ImU32`/`IM_COL32` from `imgui.h`.
- Produces (used by Tasks 2–4):
  - `PieTheme::Init()` / `PieTheme::Shutdown()` — `void`
  - `bool PieTheme::HasPalette()`
  - `bool PieTheme::Active()` — `g_Settings.usePieTheme && HasPalette()`
  - `PieUiTheme PieTheme::Palette()` — copy of cached struct
  - `ImVec4 PieTheme::Unpack(uint32_t)` — `0xAABBGGRR` → normalised `ImVec4`
  - `ImVec4 PieTheme::AccentRGB(const ImVec4& fallback)` — accent RGB + fallback alpha when `Active()`, else `fallback`
  - `ImU32 PieTheme::AccentU32(ImU32 fallback)` — accent RGB + fallback alpha byte when `Active()`, else `fallback`
  - `struct PieUiTheme { uint32_t version, accent, window_bg, header_bg, text, text_muted, border; }`

- [ ] **Step 1: Create `src/PieTheme.h`**

```cpp
#pragma once
#include <cstdint>
#include "imgui.h"

// Optional integration with the Pie UI sibling addon. Pie UI broadcasts its
// active colour palette over the Nexus event bus; Say Again subscribes and
// tints its own ImGui UI to match. Pie UI is a SOFT dependency: when absent,
// disabled via the user toggle, or before its first broadcast, Active() is
// false and callers fall back to Say Again's built-in gold theme. The event
// handler may run off the render thread, so the cached palette is mutex-guarded
// and colours are applied on the render thread by the caller.

#define PIEUI_THEME_VERSION 1u

// Fixed ABI mirrored from Pie UI. Colours are IM_COL32 values packed 0xAABBGGRR.
struct PieUiTheme {
    uint32_t version;
    uint32_t accent;
    uint32_t window_bg;
    uint32_t header_bg;
    uint32_t text;
    uint32_t text_muted;
    uint32_t border;
};

namespace PieTheme {

void Init();      // subscribe to EV_PIEUI_THEME + raise EV_PIEUI_REQUEST_THEME. Call from AddonLoad.
void Shutdown();  // unsubscribe from EV_PIEUI_THEME. Call from AddonUnload.

bool       HasPalette();  // true once a version-matched palette has been received
bool       Active();      // g_Settings.usePieTheme && HasPalette()
PieUiTheme Palette();     // copy of the last-received palette (guarded)

ImVec4 Unpack(uint32_t packed);                 // 0xAABBGGRR -> normalised ImVec4
ImVec4 AccentRGB(const ImVec4& fallback);       // accent RGB + fallback alpha when Active(), else fallback
ImU32  AccentU32(ImU32 fallback);               // accent RGB + fallback alpha byte when Active(), else fallback

} // namespace PieTheme
```

- [ ] **Step 2: Create `src/PieTheme.cpp`**

```cpp
#include "PieTheme.h"
#include "SharedState.h"   // APIDefs + Nexus typedefs, g_Settings

#include <mutex>

namespace PieTheme {
namespace {
    std::mutex s_mutex;
    PieUiTheme s_palette{};
    bool       s_hasPalette = false;
    bool       s_subscribed = false;

    const char* const EV_THEME         = "EV_PIEUI_THEME";
    const char* const EV_REQUEST_THEME = "EV_PIEUI_REQUEST_THEME";

    // May be called on a non-render thread. Copy the struct; never store the pointer.
    void OnTheme(void* payload) {
        if (!payload) return;
        const PieUiTheme* incoming = (const PieUiTheme*)payload;
        if (incoming->version != PIEUI_THEME_VERSION) return; // ignore unknown versions
        std::lock_guard<std::mutex> lock(s_mutex);
        s_palette = *incoming;
        s_hasPalette = true;
    }
} // namespace

void Init() {
    if (!APIDefs || !APIDefs->Events_Subscribe) return;
    APIDefs->Events_Subscribe(EV_THEME, OnTheme);
    s_subscribed = true;
    // Pie may have loaded first: ask it to re-broadcast now.
    if (APIDefs->Events_Raise)
        APIDefs->Events_Raise(EV_REQUEST_THEME, nullptr);
}

void Shutdown() {
    if (APIDefs && s_subscribed && APIDefs->Events_Unsubscribe)
        APIDefs->Events_Unsubscribe(EV_THEME, OnTheme);
    s_subscribed = false;
}

bool HasPalette() {
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_hasPalette;
}

PieUiTheme Palette() {
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_palette;
}

bool Active() {
    return g_Settings.usePieTheme && HasPalette();
}

ImVec4 Unpack(uint32_t c) {
    return ImVec4(
        ( c        & 0xFF) / 255.0f,
        ((c >> 8)  & 0xFF) / 255.0f,
        ((c >> 16) & 0xFF) / 255.0f,
        ((c >> 24) & 0xFF) / 255.0f);
}

ImVec4 AccentRGB(const ImVec4& fallback) {
    if (!Active()) return fallback;
    ImVec4 a = Unpack(Palette().accent);
    return ImVec4(a.x, a.y, a.z, fallback.w);
}

ImU32 AccentU32(ImU32 fallback) {
    if (!Active()) return fallback;
    ImVec4 a = Unpack(Palette().accent);
    ImU32  alpha = (fallback >> IM_COL32_A_SHIFT) & 0xFF;
    return IM_COL32((int)(a.x * 255.0f), (int)(a.y * 255.0f), (int)(a.z * 255.0f), (int)alpha);
}

} // namespace PieTheme
```

- [ ] **Step 3: Add the source to `CMakeLists.txt`**

In the `set(SOURCES ...)` block (currently `src/dllmain.cpp` … `src/ChipTextEdit.cpp`), add the new file after `src/DecoderClient.cpp`:

```cmake
    src/DecoderClient.cpp
    src/PieTheme.cpp
```

- [ ] **Step 4: Include the header and wire lifecycle in `src/dllmain.cpp`**

Add the include next to the other module includes near the top of the file (beside `#include "DecoderClient.h"`):

```cpp
#include "PieTheme.h"
```

At line 843, immediately after `DecoderClient::Init();`, add:

```cpp
    PieTheme::Init();
```

At line 850, immediately after `DecoderClient::Shutdown();`, add:

```cpp
    PieTheme::Shutdown();
```

- [ ] **Step 5: Build**

Run: `cd build && make`
Expected: compiles cleanly, produces `build/SayAgain.dll`, no new warnings. (At this point `g_Settings.usePieTheme` does not yet exist, so `Active()` will not compile — that field is added in Task 2. **If the build fails only on `usePieTheme`, proceed to Task 2 and build there;** otherwise fix the reported error.)

> Note: because `Active()` references `g_Settings.usePieTheme`, Tasks 1 and 2 must both be in place to compile. If executing task-by-task with a required green build per task, implement Task 2 before the first build. The two tasks are still reviewed separately.

- [ ] **Step 6: Commit**

```bash
git add src/PieTheme.h src/PieTheme.cpp CMakeLists.txt src/dllmain.cpp
git commit -m "Add PieTheme module: subscribe to Pie UI theme broadcast"
```

---

### Task 2: `use_pie_theme` setting + persistence + Appearance toggle

**Files:**
- Modify: `src/SharedState.h:17-30` (add field to `Settings`)
- Modify: `src/dllmain.cpp:206-207` (save), `:228-229` (load), Appearance section at `:395` (checkbox)

**Interfaces:**
- Consumes: `PieTheme` header (already included in Task 1) is not required here.
- Produces: `g_Settings.usePieTheme` (`bool`, default `true`), persisted as JSON key `"use_pie_theme"`.

- [ ] **Step 1: Add the field to `Settings` in `src/SharedState.h`**

After the `channel` line (line 29), add:

```cpp
    int         channel        = 0;   // sticky chat channel, index into Channels::kAll (0 = Say)
    bool        usePieTheme    = true; // tint UI to match Pie UI's broadcast theme when available
```

- [ ] **Step 2: Persist on save in `src/dllmain.cpp` (`SaveSettings`)**

After line 207 (`j["channel"] = g_Settings.channel;`), add:

```cpp
    j["use_pie_theme"]     = g_Settings.usePieTheme;
```

- [ ] **Step 3: Restore on load in `src/dllmain.cpp` (`LoadSettings`)**

After line 229 (`if (j.contains("channel")) ...`), add:

```cpp
        if (j.contains("use_pie_theme")) g_Settings.usePieTheme = j["use_pie_theme"].get<bool>();
```

- [ ] **Step 4: Add the checkbox to the Appearance section**

The Appearance section begins at `RenderSectionHeader("Appearance", kGold);` (line 395). Add the toggle at the end of that section, immediately before `RenderSectionHeader("Messages", kGold);` (line 446). Insert:

```cpp
    dirty |= ImGui::Checkbox("Use Pie UI theme (if available)", &g_Settings.usePieTheme);
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("When Pie UI is installed, match its colours.\nTurn off to always use Say Again's own theme.");
```

- [ ] **Step 5: Build**

Run: `cd build && make`
Expected: compiles cleanly, produces `build/SayAgain.dll`. Task 1's `Active()` now resolves.

- [ ] **Step 6: Verify observable behaviour**

Load in-game (or confirm intent by reading): the Appearance section shows the new "Use Pie UI theme (if available)" checkbox, default ON; toggling it writes `use_pie_theme` to `settings.json` and the value survives a reload. No visual change yet (applied in Tasks 3–4).

- [ ] **Step 7: Commit**

```bash
git add src/SharedState.h src/dllmain.cpp
git commit -m "Add persisted 'Use Pie UI theme' toggle (default on)"
```

---

### Task 3: Apply Pie palette to the ImGui style in `PushGW2Theme()`

**Files:**
- Modify: `src/dllmain.cpp:55-58` (`PushGW2Theme`)

**Interfaces:**
- Consumes: `PieTheme::Active()`, `PieTheme::Palette()`, `PieTheme::Unpack()` (Task 1); `g_GW2Style` (existing).
- Produces: both render surfaces (Options panel + floating panel/icon) render with Pie colours when `Active()`, else `g_GW2Style` unchanged.

- [ ] **Step 1: Replace the body of `PushGW2Theme()`**

Current (lines 55–58):

```cpp
static void PushGW2Theme() {
    g_StyleStack.push_back(ImGui::GetStyle());
    ImGui::GetStyle() = g_GW2Style;
}
```

Replace with (copies the base style, overwrites mapped slots from the palette before assigning — `g_GW2Style` itself is never mutated, so it stays the fallback; recomputed each frame so Pie theme/trim changes appear next frame):

```cpp
static void PushGW2Theme() {
    g_StyleStack.push_back(ImGui::GetStyle());
    ImGuiStyle themed = g_GW2Style;
    if (PieTheme::Active()) {
        const PieUiTheme p = PieTheme::Palette();
        const ImVec4 windowBg = PieTheme::Unpack(p.window_bg);
        const ImVec4 headerBg = PieTheme::Unpack(p.header_bg);
        const ImVec4 text     = PieTheme::Unpack(p.text);
        const ImVec4 muted    = PieTheme::Unpack(p.text_muted);
        const ImVec4 border   = PieTheme::Unpack(p.border);
        const ImVec4 accent   = PieTheme::Unpack(p.accent);

        themed.Colors[ImGuiCol_WindowBg]      = windowBg;
        themed.Colors[ImGuiCol_ChildBg]       = windowBg;
        themed.Colors[ImGuiCol_PopupBg]       = windowBg;

        themed.Colors[ImGuiCol_TitleBg]       = headerBg;
        themed.Colors[ImGuiCol_TitleBgActive] = headerBg;
        themed.Colors[ImGuiCol_Header]        = headerBg;

        themed.Colors[ImGuiCol_Text]          = text;
        themed.Colors[ImGuiCol_TextDisabled]  = muted;
        themed.Colors[ImGuiCol_Border]        = border;

        themed.Colors[ImGuiCol_Button]        = accent;
        themed.Colors[ImGuiCol_ButtonHovered] = accent;
        themed.Colors[ImGuiCol_ButtonActive]  = accent;
        themed.Colors[ImGuiCol_CheckMark]     = accent;
        themed.Colors[ImGuiCol_SliderGrab]    = accent;
        themed.Colors[ImGuiCol_SliderGrabActive] = accent;
        themed.Colors[ImGuiCol_FrameBgActive] = accent;
        themed.Colors[ImGuiCol_HeaderHovered] = accent;
    }
    ImGui::GetStyle() = themed;
}
```

- [ ] **Step 2: Build**

Run: `cd build && make`
Expected: compiles cleanly, produces `build/SayAgain.dll`.

- [ ] **Step 3: Verify observable behaviour**

With Pie UI installed and broadcasting: opening Say Again's Options panel and floating panel shows window/header/text/border/button colours matching Pie's palette; changing Pie's theme updates them on the next frame. With Pie absent or the toggle OFF: unchanged gold theme. Push/pop unchanged — the override is inside the existing single style assignment.

- [ ] **Step 4: Commit**

```bash
git add src/dllmain.cpp
git commit -m "Tint ImGui style from Pie UI palette when active"
```

---

### Task 4: Route hand-drawn gold elements through the accent

**Files:**
- Modify: `src/FloatingIcon.cpp:310` (flash), `:316` (icon pinned border), `:431` (panel pinned border), `:451` (panel channel-header colour), `:501` (×N badge text)
- Modify: `src/dllmain.cpp:365,395,446` (section-header colour) — via a local accent-aware colour
- Add `#include "PieTheme.h"` to `src/FloatingIcon.cpp` if not already present

**Interfaces:**
- Consumes: `PieTheme::AccentRGB()`, `PieTheme::AccentU32()` (Task 1).
- Produces: all hand-drawn gold highlights follow Pie's accent when `Active()`, keeping each site's own alpha; unchanged otherwise.

- [ ] **Step 1: Include the header in `src/FloatingIcon.cpp`**

Near the existing includes (it already has `#include "imgui.h"` at line 11 and includes `SharedState.h`), add:

```cpp
#include "PieTheme.h"
```

- [ ] **Step 2: Icon right-click flash (line 310)**

Current:

```cpp
            dl->AddCircleFilled(fc, sz * 0.5f - 1.0f, IM_COL32(220, 190, 80, flashAlpha));
```

Replace with (accent RGB, keep the computed `flashAlpha`):

```cpp
            dl->AddCircleFilled(fc, sz * 0.5f - 1.0f,
                PieTheme::AccentU32(IM_COL32(220, 190, 80, flashAlpha)));
```

- [ ] **Step 3: Icon pinned-open border (line 316)**

Current:

```cpp
            dl->AddCircle(bc, sz * 0.5f - 1.0f, IM_COL32(220, 190, 80, 255), 0, 2.0f);
```

Replace with:

```cpp
            dl->AddCircle(bc, sz * 0.5f - 1.0f,
                PieTheme::AccentU32(IM_COL32(220, 190, 80, 255)), 0, 2.0f);
```

- [ ] **Step 4: Panel pinned border (line 431)**

Current:

```cpp
    ImGui::PushStyleColor(ImGuiCol_Border,
        pinned ? ImVec4(0.86f, 0.75f, 0.31f, alpha * 0.9f) : ImVec4(0, 0, 0, 0));
```

Replace with (only the gold branch takes the accent; the transparent branch is untouched, so push/pop count is unchanged):

```cpp
    ImGui::PushStyleColor(ImGuiCol_Border,
        pinned ? PieTheme::AccentRGB(ImVec4(0.86f, 0.75f, 0.31f, alpha * 0.9f))
               : ImVec4(0, 0, 0, 0));
```

- [ ] **Step 5: Panel channel-header colour (line 451)**

Current:

```cpp
        ImVec4 headerColour(0.86f, 0.75f, 0.31f, alpha);
```

Replace with:

```cpp
        ImVec4 headerColour = PieTheme::AccentRGB(ImVec4(0.86f, 0.75f, 0.31f, alpha));
```

- [ ] **Step 6: ×N multi-line badge text (line 501)**

Current:

```cpp
                bdl->AddText({ rMin.x + 3.0f, rMin.y + 2.0f }, IM_COL32(200, 170, 100, 255), badge);
```

Replace with:

```cpp
                bdl->AddText({ rMin.x + 3.0f, rMin.y + 2.0f },
                    PieTheme::AccentU32(IM_COL32(200, 170, 100, 255)), badge);
```

- [ ] **Step 7: Section headers in `src/dllmain.cpp`**

`RenderSectionHeader(label, color)` derives its gradient and underline from `color`. The three call sites pass `kGold` (`ImVec4(0.70f, 0.58f, 0.20f, 1.0f)`, defined at line 362). Make those calls pass the accent-aware colour. At each of the three call sites (lines 365, 395, 446), change `kGold` to `PieTheme::AccentRGB(kGold)`:

```cpp
    RenderSectionHeader("Behaviour", PieTheme::AccentRGB(kGold));
```
```cpp
    RenderSectionHeader("Appearance", PieTheme::AccentRGB(kGold));
```
```cpp
    RenderSectionHeader("Messages", PieTheme::AccentRGB(kGold));
```

(The panel's own section headers rendered via `RenderSectionHeader` in `FloatingIcon.cpp`, if any, use the gold header colour already handled in Step 5; the "All Maps"/current-map gold headers in the panel use `headerColour`/`kGold` — if a `RenderSectionHeader` call exists in `FloatingIcon.cpp`, wrap its colour argument the same way with `PieTheme::AccentRGB(...)`.)

- [ ] **Step 8: Build**

Run: `cd build && make`
Expected: compiles cleanly, produces `build/SayAgain.dll`.

- [ ] **Step 9: Verify observable behaviour**

With Pie UI active: the floating icon's pinned border and right-click flash, the panel's pinned border and "Sending to <Channel>" header, the ×N badges, and all section headers show Pie's accent colour; fades/flashes still animate (alpha preserved). With Pie absent or toggle OFF: original gold everywhere.

- [ ] **Step 10: Commit**

```bash
git add src/FloatingIcon.cpp src/dllmain.cpp
git commit -m "Route hand-drawn gold highlights through Pie UI accent"
```

---

## Self-Review

**Spec coverage:**
- PieTheme module (Init/Shutdown/HasPalette/Palette/helpers), soft dependency, cross-thread copy, version guard → Task 1. ✅
- `use_pie_theme` setting, default ON, persisted, Appearance checkbox, subscribe regardless of toggle → Task 2 (+ subscribe in Task 1 Init runs regardless of toggle; `Active()` gates application). ✅
- Style application at the single `PushGW2Theme()` integration point, full mapping table, fallback preserved, live updates → Task 3. ✅
- All hand-drawn gold → accent (icon border/flash, panel border, channel header, ×N badge, section headers), keeping own alpha → Task 4. ✅
- Fallback matrix (absent / no palette / toggle off / wrong version) → covered by `Active()` returning false and version guard in `OnTheme`. ✅
- Unsubscribe on unload → Task 1 Shutdown. ✅
- CMake source addition → Task 1 Step 3. ✅

**Placeholder scan:** No TBD/TODO/"handle edge cases". Every code step shows full code. ✅

**Type consistency:** `PieUiTheme` fields, `Active()`/`HasPalette()`/`Palette()`/`Unpack()`/`AccentRGB()`/`AccentU32()` signatures are identical between the Task 1 definition and their uses in Tasks 3–4. `usePieTheme` field name matches between SharedState (Task 2) and `Active()` (Task 1). ✅

**Note for implementer:** Tasks 1 and 2 must both be applied before the first successful build, because `PieTheme::Active()` reads `g_Settings.usePieTheme`. Implement Task 1 then Task 2, then build. They remain separately reviewable.
