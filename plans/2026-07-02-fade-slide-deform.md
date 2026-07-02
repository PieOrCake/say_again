# Fade & Slide Animation Deform Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the floating panel's Fade and Slide animations deform their contents (buttons, text, border, badges) like Pop does — Fade fades the whole panel; Slide travels in from the icon over a meaningful distance while fading — leaving Pop unchanged.

**Architecture:** Reuse the post-`End()` draw-list mechanism added for Pop. Add a `WarpDrawListAlpha` helper that multiplies every vertex's alpha. Route the fade for Fade/Slide through that single pass (instead of baking `g_AnimProgress` into the background/border/header alpha, which stays only for Pop), and increase the slide distance.

**Tech Stack:** C++, Dear ImGui (draw-list vertex manipulation), Nexus addon API, CMake + MinGW cross-build.

## Global Constraints

- No unit-test harness. Verification is a clean `cd build && make` (from repo root: `cd build && make`) plus in-game observation.
- Only Fade (`animStyle == 0`) and Slide (`animStyle == 1`) change behaviour. **Pop (`animStyle == 2`) must be byte-identical to current behaviour.**
- At rest (`g_AnimProgress == 1`) all three styles must be pixel-identical to a fully-open panel today: `WarpDrawListAlpha` is a no-op at `a >= 1`, bg/border return to resting alpha, slide offset is 0.
- Avoid double-fade: for Fade/Slide the vertex-alpha pass is the *only* fade; bg/border/header sit at resting alpha. Controlled by `bool vtxFade = (animStyle == 0 || animStyle == 1)`.
- Keep every existing `Push*`/`Pop*` balanced; counts unchanged. Confined to `src/FloatingIcon.cpp`; no settings/enum change.
- Do not bump the version number.

---

### Task 1: Fade & Slide deform via draw-list alpha

**Files:**
- Modify: `src/FloatingIcon.cpp` — add `WarpDrawListAlpha` helper (beside `WarpDrawListScale`, ~line 230); in `RenderFloatingIcon()` add the `vtxFade` flag, branch the three alpha sources, enlarge the slide distance, and extend the post-`End()` block.

**Interfaces:**
- Consumes: existing `WarpDrawListScale`, `PopScale`, `g_AnimProgress`, `panelDL` (window draw list captured after `Begin()`), `warping`/`disableItems` (already present), icon geometry `ix`/`iy`/`sz`.
- Produces: `static void WarpDrawListAlpha(ImDrawList* dl, float a)` — multiplies every `dl` vertex's alpha byte by `a` (clamped `[0,1]`); no-op at `a >= 1`. Used only in this file.

- [ ] **Step 1: Add the `WarpDrawListAlpha` helper**

In `src/FloatingIcon.cpp`, immediately after the `WarpDrawListScale` function (which ends around line 244), add:

```cpp
// Multiply every vertex's alpha in a window draw list by `a`, fading the whole
// panel (background, buttons, text, border, badges) uniformly. RGB and vertex
// positions are untouched. No-op at a >= 1.
static void WarpDrawListAlpha(ImDrawList* dl, float a) {
    if (!dl || a >= 1.0f) return;
    if (a < 0.0f) a = 0.0f;
    for (int i = 0; i < dl->VtxBuffer.Size; ++i) {
        ImU32& col = dl->VtxBuffer[i].col;
        ImU32 av = (ImU32)(((col >> IM_COL32_A_SHIFT) & 0xFF) * a + 0.5f);
        col = (col & ~IM_COL32_A_MASK) | (av << IM_COL32_A_SHIFT);
    }
}
```

- [ ] **Step 2: Add the `vtxFade` flag**

Find (line ~427):

```cpp
    // --- Apply animation transform ---
    float alpha = g_AnimProgress;
    ImVec2 panelPos(px, py);
    ImVec2 panelSize(panelW, panelH);
```

Replace with:

```cpp
    // --- Apply animation transform ---
    float alpha = g_AnimProgress;
    // Fade (0) and Slide (1) fade the whole panel via a post-render vertex-alpha
    // pass, so they leave bg/border/header at resting alpha to avoid double-fade.
    // Pop (2) keeps baking `alpha` into bg/border (contents stay opaque while it
    // scales). `bgHdrA` is the alpha multiplier for the bg/border/header sources.
    bool  vtxFade = (g_Settings.animStyle == 0 || g_Settings.animStyle == 1);
    float bgHdrA  = vtxFade ? 1.0f : alpha;
    ImVec2 panelPos(px, py);
    ImVec2 panelSize(panelW, panelH);
```

- [ ] **Step 3: Enlarge the slide distance**

Find (lines ~431-437):

```cpp
    if (g_Settings.animStyle == 1) {
        float slideOff = (1.0f - g_AnimProgress) * 20.0f;
        if (vertical)
            panelPos.y += edgeTop ? -slideOff : slideOff;
        else
            panelPos.x += flipX ? slideOff : -slideOff;
    }
```

Replace with (named, larger distance; direction logic unchanged):

```cpp
    if (g_Settings.animStyle == 1) {
        const float kSlideDistance = 60.0f; // travel from the icon; tunable
        float slideOff = (1.0f - g_AnimProgress) * kSlideDistance;
        if (vertical)
            panelPos.y += edgeTop ? -slideOff : slideOff;
        else
            panelPos.x += flipX ? slideOff : -slideOff;
    }
```

- [ ] **Step 4: Route the background alpha through `bgHdrA`**

Find (line ~441):

```cpp
    ImGui::SetNextWindowBgAlpha(alpha * 0.95f);
```

Replace with:

```cpp
    ImGui::SetNextWindowBgAlpha(bgHdrA * 0.95f);
```

- [ ] **Step 5: Route the border colour alpha through `bgHdrA`**

Find (line ~446):

```cpp
        pinned ? PieTheme::AccentRGB(ImVec4(0.86f, 0.75f, 0.31f, alpha * 0.9f))
```

Replace with:

```cpp
        pinned ? PieTheme::AccentRGB(ImVec4(0.86f, 0.75f, 0.31f, bgHdrA * 0.9f))
```

- [ ] **Step 6: Route the header colour alpha through `bgHdrA`**

Find (line ~475):

```cpp
        ImVec4 headerColour = PieTheme::AccentRGB(ImVec4(0.86f, 0.75f, 0.31f, alpha));
```

Replace with:

```cpp
        ImVec4 headerColour = PieTheme::AccentRGB(ImVec4(0.86f, 0.75f, 0.31f, bgHdrA));
```

- [ ] **Step 7: Extend the post-`End()` warp to Fade/Slide**

Find (lines ~595-598):

```cpp
    if (warping && panelDL)
        WarpDrawListScale(panelDL,
            ImVec2(ix + sz * 0.5f, iy + sz * 0.5f),
            PopScale(g_AnimProgress));
```

Replace with:

```cpp
    if (panelDL) {
        if (g_Settings.animStyle == 2) // Pop: scale the panel out of / into the icon
            WarpDrawListScale(panelDL,
                ImVec2(ix + sz * 0.5f, iy + sz * 0.5f),
                PopScale(g_AnimProgress));
        else // Fade (0) / Slide (1): fade the whole panel
            WarpDrawListAlpha(panelDL, g_AnimProgress);
    }
```

- [ ] **Step 8: Build**

Run: `cd build && make`
Expected: clean compile, produces `build/SayAgain.dll`, no new warnings.

- [ ] **Step 9: Verify observable behaviour**

Confirm by reading the changed control flow and, if possible, in-game:
- **Fade:** the whole panel (background, buttons, text, border, ×N badges) fades in and out together; nothing pops in at full opacity; no movement.
- **Slide:** the panel travels in from the icon's side over a clearly visible distance while fading in, settling at its resting position; edge placements (above/below/left/right) each slide from the icon side; clicks land correctly during and after (the window and its hit targets move together).
- **Pop:** unchanged from current behaviour.
- At rest, all three are pixel-identical to today (crisp, full opacity).

- [ ] **Step 10: Commit**

```bash
git add src/FloatingIcon.cpp
git commit -m "Fade/Slide animations: fade panel contents; slide from icon"
```

---

## Self-Review

**Spec coverage:**
- `WarpDrawListAlpha` helper (whole-panel alpha, no-op at rest) → Steps 1, 7. ✅
- Fade fades whole panel, no movement → Steps 4-7 (bg/border/header at resting via `bgHdrA`, vertex-alpha pass). ✅
- Slide travels from icon over meaningful distance + fades → Steps 3, 7 (direction unchanged, `kSlideDistance = 60`, alpha pass). ✅
- Avoid double-fade via `vtxFade`/`bgHdrA` branch → Steps 2, 4, 5, 6. ✅
- Pop byte-identical → Step 7 keeps `animStyle == 2` on `WarpDrawListScale`; `bgHdrA == alpha` when not `vtxFade`, so bg/border/header unchanged for Pop. ✅
- No interaction-gate change → `warping`/`acceptInput`/`disableItems` untouched (Fade/Slide keep original gating). ✅
- Confined to `src/FloatingIcon.cpp`, no settings/enum change → all steps. ✅
- Push/pop balance unchanged → no `Push*`/`Pop*` added or removed. ✅

**Placeholder scan:** No TBD/TODO; every code step shows full before/after. ✅

**Type consistency:** `WarpDrawListAlpha(ImDrawList*, float)` defined in Step 1, called in Step 7. `vtxFade`/`bgHdrA` defined in Step 2, used in Steps 4-6. `kSlideDistance` local to the Slide branch (Step 3). `PopScale`/`WarpDrawListScale` are existing, reused unchanged. ✅

**Note for implementer:** match on the quoted code text, not the line numbers (they may drift). If any quoted block is not found exactly, stop and report rather than guessing.
