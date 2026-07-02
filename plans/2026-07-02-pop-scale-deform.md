# Pop Animation Scale-From-Icon Deform Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Upgrade the floating panel's "Pop" animation so the whole panel (buttons, text, borders, badges) scales out of / into the floating icon, instead of the window animating while the buttons pop in at full size.

**Architecture:** Render the panel at full size (correct layout, no clipping), capture its ImGui window draw list, and after `End()` scale that draw list's vertices and per-command clip rects about the icon centre by an eased factor of `g_AnimProgress`. Interaction is blocked until the panel is essentially open, but items stay full-colour (no disabled-item flag) for this style. Fade and Slide are untouched.

**Tech Stack:** C++, Dear ImGui (immediate-mode; draw-list vertex manipulation), Nexus addon API, CMake + MinGW cross-build.

## Global Constraints

- No unit-test harness. Verification is a clean `cd build && make` (from repo root: `cd build && make`) plus in-game observation of the described behaviour.
- Only the "Pop" style (`g_Settings.animStyle == 2`) changes. Fade (`0`) and Slide (`1`) must behave exactly as today.
- The warp must be a no-op at rest: at `g_AnimProgress == 1` the scale factor is exactly `1.0` and the panel is pixel-identical to today.
- Anchor is the floating icon centre: `ImVec2(ix + sz * 0.5f, iy + sz * 0.5f)` (`ix`, `iy`, `sz` are the icon's screen position/size, already in scope in `RenderFloatingIcon`).
- Keep every existing `Push*`/`Pop*` (style vars, style colours, item flag) exactly balanced; do not change their counts.
- Do not bump the version number.

---

### Task 1: Scale-from-icon deform for the Pop style

**Files:**
- Modify: `src/FloatingIcon.cpp` — add a draw-list warp helper (near the other animation helpers, after `PopScale` ~line 224); rewire the `animStyle == 2` branch, the interaction gate, the draw-list capture, and the post-`End()` warp in `RenderFloatingIcon()`.

**Interfaces:**
- Consumes: existing `g_AnimProgress` (float 0..1), `PopScale(float)` (existing easing, returns exactly `1.0` at `t == 1`), icon geometry `ix`/`iy`/`sz`, and the panel window `"##sa_panel"`.
- Produces: `static void WarpDrawListScale(ImDrawList* dl, ImVec2 anchor, float s)` — scales all of `dl`'s vertex positions and each draw command's `ClipRect` about `anchor` by `s`; no-op when `s == 1.0f`. Used only within this file.

- [ ] **Step 1: Add the `WarpDrawListScale` helper**

In `src/FloatingIcon.cpp`, immediately after the `PopScale` function (which currently ends around line 224), add:

```cpp
// Scale every vertex position and clip rect of a window's draw list about
// `anchor` by `s`. Used by the Pop animation to deform the panel (its contents
// included) as it grows out of / collapses into the icon. Uniform scale keeps
// clip rects axis-aligned. No-op at s == 1.
static void WarpDrawListScale(ImDrawList* dl, ImVec2 anchor, float s) {
    if (!dl || s == 1.0f) return;
    for (int i = 0; i < dl->VtxBuffer.Size; ++i) {
        ImDrawVert& v = dl->VtxBuffer[i];
        v.pos.x = anchor.x + (v.pos.x - anchor.x) * s;
        v.pos.y = anchor.y + (v.pos.y - anchor.y) * s;
    }
    for (int i = 0; i < dl->CmdBuffer.Size; ++i) {
        ImVec4& c = dl->CmdBuffer[i].ClipRect; // (minX, minY, maxX, maxY), screen space
        c.x = anchor.x + (c.x - anchor.x) * s;
        c.y = anchor.y + (c.y - anchor.y) * s;
        c.z = anchor.x + (c.z - anchor.x) * s;
        c.w = anchor.y + (c.w - anchor.y) * s;
    }
}
```

- [ ] **Step 2: Stop the Pop branch from resizing/repositioning the window**

Find the `animStyle == 2` branch in `RenderFloatingIcon()` (currently lines ~417-425):

```cpp
    } else if (g_Settings.animStyle == 2) {
        float scale = PopScale(g_AnimProgress);
        float cx = px + panelW * 0.5f;
        float cy = py + panelH * 0.5f;
        panelSize.x *= scale;
        panelSize.y *= scale;
        panelPos.x = cx - panelSize.x * 0.5f;
        panelPos.y = cy - panelSize.y * 0.5f;
    }
```

Delete this entire `else if` block. The Slide branch immediately above it stays. The Pop style now leaves `panelPos`/`panelSize` at full size (`px, py` / `panelW, panelH`); the deform happens after render instead.

- [ ] **Step 3: Rework the interaction gate and add the draw-list handle**

Find (currently lines ~442-443):

```cpp
    bool disableItems = (g_AnimProgress < 0.1f);
    if (disableItems) ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
```

Replace with:

```cpp
    // The scale-from-icon style (2) warps the panel after render, so ImGui's
    // layout rects don't match the visual mid-animation. For that style: keep
    // items full-colour (no disabled flag) but block clicks until essentially
    // open. Fade/Slide keep their existing early disable behaviour unchanged.
    bool warping      = (g_Settings.animStyle == 2);
    bool acceptInput  = !warping || (g_AnimProgress > 0.98f);
    bool disableItems = !warping && (g_AnimProgress < 0.1f);
    ImDrawList* panelDL = nullptr;
    if (disableItems) ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
```

- [ ] **Step 4: Capture the panel's draw list after `Begin`**

Find (currently lines ~445-446):

```cpp
    if (ImGui::Begin("##sa_panel", nullptr, panelFlags)) {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 4));
```

Insert the capture as the first line inside the block:

```cpp
    if (ImGui::Begin("##sa_panel", nullptr, panelFlags)) {
        panelDL = ImGui::GetWindowDrawList(); // captured for the post-End() warp
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 4));
```

- [ ] **Step 5: Gate the three click sites with `acceptInput`**

5a. Channel header selectable (currently line ~463):

```cpp
            if (ImGui::Selectable(chLbl)) {
```
becomes:
```cpp
            if (ImGui::Selectable(chLbl) && acceptInput) {
```

5b. Message button (currently lines ~491-492):

```cpp
            if (ImGui::Button(lbl.c_str(), ImVec2(btnW, btnH)))
                PostMessage(*visible[i], Channels::Command(g_Settings.channel));
```
becomes:
```cpp
            if (ImGui::Button(lbl.c_str(), ImVec2(btnW, btnH)) && acceptInput)
                PostMessage(*visible[i], Channels::Command(g_Settings.channel));
```

5c. Right-click on a message (currently lines ~508-509):

```cpp
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                pendingRightClick = i;
```
becomes:
```cpp
            if (acceptInput && ImGui::IsItemClicked(ImGuiMouseButton_Right))
                pendingRightClick = i;
```

(The channel picker popup's own selectables need no gate: the popup only opens via the sites above, which require `acceptInput`, i.e. the panel is already essentially open.)

- [ ] **Step 6: Warp the panel after `End()`**

Find (currently lines ~570-574):

```cpp
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    if (disableItems) ImGui::PopItemFlag();
}
```

Replace with:

```cpp
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    // Deform the whole panel about the icon for the scale-from-icon style.
    if (warping && panelDL)
        WarpDrawListScale(panelDL,
            ImVec2(ix + sz * 0.5f, iy + sz * 0.5f),
            PopScale(g_AnimProgress));

    if (disableItems) ImGui::PopItemFlag();
}
```

- [ ] **Step 7: Build**

Run: `cd build && make`
Expected: clean compile, produces `build/SayAgain.dll`, no new warnings. (`PopScale` is still referenced by Step 6, so no unused-function warning.)

- [ ] **Step 8: Verify observable behaviour**

Confirm by reading the changed control flow and, if possible, in-game with the Pop animation style selected:
- Opening the panel: background, buttons, text, border and ×N badges all grow together out of the icon and settle crisp at full size.
- Closing: they collapse back into the icon.
- Buttons/header are clickable only once the panel is essentially open; at rest, clicks land on the correct control.
- Buttons are full-colour during the grow (not greyed).
- Fade and Slide styles look exactly as before.
- Edge placement (panel above / below / left / right of the icon) still animates from the icon in each case.

- [ ] **Step 9: Commit**

```bash
git add src/FloatingIcon.cpp
git commit -m "Pop animation: scale panel contents from the icon (draw-list warp)"
```

---

## Self-Review

**Spec coverage:**
- Render full-size, no window resize for Pop → Step 2. ✅
- Draw-list vertex + clip-rect warp about icon centre → Steps 1, 6. ✅
- Eased factor from `g_AnimProgress`, no-op at rest → `PopScale` reused (exactly 1.0 at t==1) + helper's `s == 1.0f` guard (Steps 1, 6). ✅
- Interaction blocked until ~open, full-colour (no disabled flag) for Pop; Fade/Slide unchanged → Steps 3, 5. ✅
- Background-alpha fade retained → untouched (`SetNextWindowBgAlpha` line ~429 not modified). ✅
- Confined to `src/FloatingIcon.cpp`, no settings/enum change → all steps. ✅
- Push/pop balance preserved → `PushItemFlag`/`PopItemFlag` both guarded by `disableItems`; no other push/pop touched (Steps 3, 6). ✅

**Placeholder scan:** No TBD/TODO; every code step shows full before/after. ✅

**Type consistency:** `WarpDrawListScale(ImDrawList*, ImVec2, float)` defined in Step 1 and called with those exact types in Step 6. `warping`/`acceptInput`/`disableItems`/`panelDL` declared in Step 3 and used in Steps 4, 5, 6. `PopScale` is the existing function, reused unchanged. ✅

**Note for implementer:** match on the quoted code text, not the line numbers (they may drift). If any quoted block is not found exactly, stop and report rather than guessing.
