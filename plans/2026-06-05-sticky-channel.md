# Sticky Chat Channel Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make Say Again send to a remembered ("sticky") chat channel shown in the panel, instead of relying on whatever channel the in-game chatbox has selected.

**Architecture:** A small channel table (label + slash command) drives a new persisted `channel` setting. Left-clicking a message prepends the sticky channel's command. A clickable gold header row in the panel shows the current channel and opens a 2-column picker; the same picker also appears on message right-click, where it both sends and updates the sticky channel.

**Tech Stack:** C++17, ImGui (Nexus), nlohmann/json. No unit-test harness exists — **each task is verified by a clean `cd build && make`**, and the whole feature is verified in-game by the user at the end.

**Spec:** `docs/superpowers/specs/2026-06-05-sticky-channel-design.md`

**Channel table (index → label → command):**

| Idx | Label   | Command   |
|-----|---------|-----------|
| 0   | Say     | `/say `   |
| 1   | Map     | `/map `   |
| 2   | Party   | `/party ` |
| 3   | Squad   | `/squad ` |
| 4   | Team    | `/team `  |
| 5   | Guild 1 | `/g1 `    |
| 6   | Guild 2 | `/g2 `    |
| 7   | Guild 3 | `/g3 `    |
| 8   | Guild 4 | `/g4 `    |
| 9   | Guild 5 | `/g5 `    |
| 10  | Guild 6 | `/g6 `    |

---

## File Structure

- **Create** `src/Channels.h` — the channel table and small accessor helpers (`Command`, `Label`, counts). Single responsibility: define the channel set. Header-only so both FloatingIcon and any future caller can use it.
- **Modify** `src/SharedState.h` — add `int channel` to the `Settings` struct.
- **Modify** `src/dllmain.cpp` — persist `channel` in `SaveSettings()` / `LoadSettings()`.
- **Modify** `src/FloatingIcon.cpp` — left-click uses sticky channel, panel grows by one header row, new clickable channel header, reworked grid picker with two modes.

---

## Task 1: Channel table header

**Files:**
- Create: `src/Channels.h`

- [ ] **Step 1: Create the channel table header**

Create `src/Channels.h` with exactly this content:

```cpp
#pragma once

// Sticky chat-channel definitions for Say Again.
// Each command string carries a trailing space so it can be prepended directly.
namespace Channels {

struct Channel { const char* label; const char* command; };

static const Channel kAll[] = {
    { "Say",     "/say "   },
    { "Map",     "/map "   },
    { "Party",   "/party " },
    { "Squad",   "/squad " },
    { "Team",    "/team "  },
    { "Guild 1", "/g1 "    },
    { "Guild 2", "/g2 "    },
    { "Guild 3", "/g3 "    },
    { "Guild 4", "/g4 "    },
    { "Guild 5", "/g5 "    },
    { "Guild 6", "/g6 "    },
};

static const int kCount         = (int)(sizeof(kAll) / sizeof(kAll[0])); // 11
static const int kStandardCount = 5; // Say..Team (left column); guild slots follow

inline int         Clamp(int idx)   { return (idx < 0 || idx >= kCount) ? 0 : idx; }
inline const char* Command(int idx) { return kAll[Clamp(idx)].command; }
inline const char* Label(int idx)   { return kAll[Clamp(idx)].label; }

} // namespace Channels
```

- [ ] **Step 2: Verify it builds**

Run: `cd build && make`
Expected: builds to `build/SayAgain.dll` with no new warnings/errors. (Nothing includes the header yet, but a clean build confirms no syntax error.)

- [ ] **Step 3: Commit**

```bash
git add src/Channels.h
git commit -m "Add channel table for sticky chat channel"
```

---

## Task 2: Settings field + persistence

**Files:**
- Modify: `src/SharedState.h:17-29` (Settings struct)
- Modify: `src/dllmain.cpp:188-224` (SaveSettings / LoadSettings)

- [ ] **Step 1: Add the field to the Settings struct**

In `src/SharedState.h`, inside `struct Settings`, add the `channel` field after `messagePrefix`:

```cpp
    std::string messagePrefix;        // prepended to each sent line (after channel command)
    int         channel        = 0;   // sticky chat channel, index into Channels::kAll (0 = Say)
```

- [ ] **Step 2: Persist it in SaveSettings**

In `src/dllmain.cpp`, in `SaveSettings()`, after the `msg_prefix` line, add:

```cpp
    j["msg_prefix"]        = g_Settings.messagePrefix;
    j["channel"]           = g_Settings.channel;
```

(Keep the existing `msg_prefix` line; the second line is the addition.)

- [ ] **Step 3: Load it in LoadSettings**

In `src/dllmain.cpp`, in `LoadSettings()`, after the `msg_prefix` load line, add:

```cpp
        if (j.contains("msg_prefix"))      g_Settings.messagePrefix  = j["msg_prefix"].get<std::string>();
        if (j.contains("channel"))         g_Settings.channel        = j["channel"].get<int>();
```

(Keep the existing `msg_prefix` line; the second line is the addition.)

- [ ] **Step 4: Verify it builds**

Run: `cd build && make`
Expected: clean build, no errors.

- [ ] **Step 5: Commit**

```bash
git add src/SharedState.h src/dllmain.cpp
git commit -m "Persist sticky channel setting"
```

---

## Task 3: Left-click sends to the sticky channel

**Files:**
- Modify: `src/FloatingIcon.cpp` (top includes; `panelH` calc ~380-383; left-click ~466-467)

- [ ] **Step 1: Include the channel table**

In `src/FloatingIcon.cpp`, add the include alongside the existing project includes (near the top of the file, after the other `#include "..."` lines such as `#include "SharedState.h"`):

```cpp
#include "Channels.h"
```

- [ ] **Step 2: Make left-click use the sticky channel command**

In `src/FloatingIcon.cpp`, find the message button left-click (currently):

```cpp
            if (ImGui::Button(lbl.c_str(), ImVec2(btnW, btnH)))
                PostMessage(*visible[i], "");
```

Replace the body with:

```cpp
            if (ImGui::Button(lbl.c_str(), ImVec2(btnW, btnH)))
                PostMessage(*visible[i], Channels::Command(g_Settings.channel));
```

- [ ] **Step 3: Grow the panel by one header row**

In `src/FloatingIcon.cpp`, find the `panelH` calculation:

```cpp
    float panelH = (allMapsRows + curMapRows) * (btnH + 4.0f)
                 + (allMapsVis.empty() ? 0.0f : headerH + 4.0f)
                 + (curMapVis.empty()  ? 0.0f : headerH + 4.0f)
                 + sectionGap + 12.0f;
```

Add a constant for the channel header just above it and include it in the total. The block becomes:

```cpp
    float channelRowH = 24.0f; // always-present clickable channel header at top of panel
    float panelH = (allMapsRows + curMapRows) * (btnH + 4.0f)
                 + (allMapsVis.empty() ? 0.0f : headerH + 4.0f)
                 + (curMapVis.empty()  ? 0.0f : headerH + 4.0f)
                 + channelRowH + 4.0f
                 + sectionGap + 12.0f;
```

- [ ] **Step 4: Verify it builds**

Run: `cd build && make`
Expected: clean build, no errors.

- [ ] **Step 5: Commit**

```bash
git add src/FloatingIcon.cpp
git commit -m "Left-click sends to sticky channel; reserve panel header space"
```

---

## Task 4: Channel header row + reworked picker

This task replaces the old right-click-only picker with: (a) a clickable gold
header row at the top of the panel that opens the picker in **set-only** mode,
and (b) the message right-click opening the **same** picker in **send-and-set**
mode. The picker is laid out as a 2-column grid (standard channels left, guild
slots right). A single static `s_PickerMsgIdx` selects the mode: `-1` = set only,
`>= 0` = also send that message index.

**Files:**
- Modify: `src/FloatingIcon.cpp` (static state ~442; header render ~447; right-click capture ~482-483, 498-501; popup body ~505-516)

- [ ] **Step 1: Rename the picker-state static to carry the mode**

In `src/FloatingIcon.cpp`, find:

```cpp
        // FIX: collect right-click outside PushID scope so popup ID matches BeginPopup
        static int    s_RightClickIdx = -1;
        static int    s_HoverIdx      = -1;
        static double s_HoverStart    = 0.0;
        int pendingRightClick = -1;
```

Replace with:

```cpp
        // FIX: collect right-click outside PushID scope so popup ID matches BeginPopup.
        // s_PickerMsgIdx selects picker mode: -1 = set channel only, >=0 = also send that message.
        static int    s_PickerMsgIdx  = -1;
        static int    s_HoverIdx      = -1;
        static double s_HoverStart    = 0.0;
        int pendingRightClick = -1;
```

- [ ] **Step 2: Render the clickable channel header**

In `src/FloatingIcon.cpp`, find:

```cpp
        ImVec4 headerColour(0.86f, 0.75f, 0.31f, alpha);

        bool anyHovered = false;
```

Insert the header render between those two lines so it reads:

```cpp
        ImVec4 headerColour(0.86f, 0.75f, 0.31f, alpha);

        // Clickable channel header — shows the sticky channel, opens picker in set-only mode.
        {
            char chLbl[64];
            snprintf(chLbl, sizeof(chLbl), "\xe2\x96\xb8 %s", Channels::Label(g_Settings.channel)); // "▸ Name"
            ImGui::PushStyleColor(ImGuiCol_Text, headerColour);
            if (ImGui::Selectable(chLbl)) {
                s_PickerMsgIdx = -1;
                ImGui::OpenPopup("##chan");
            }
            ImGui::PopStyleColor();
        }

        bool anyHovered = false;
```

- [ ] **Step 3: Point the message right-click at the new state + mode**

In `src/FloatingIcon.cpp`, find the deferred open after the loop:

```cpp
        // Open popup after all PushID scopes are closed
        if (pendingRightClick >= 0) {
            s_RightClickIdx = pendingRightClick;
            ImGui::OpenPopup("##chan");
        }
```

Replace with:

```cpp
        // Open popup after all PushID scopes are closed
        if (pendingRightClick >= 0) {
            s_PickerMsgIdx = pendingRightClick; // send-and-set mode
            ImGui::OpenPopup("##chan");
        }
```

- [ ] **Step 4: Replace the popup body with the 2-column grid**

In `src/FloatingIcon.cpp`, find the whole popup body:

```cpp
        if (ImGui::BeginPopup("##chan")) {
            if (s_RightClickIdx >= 0 && s_RightClickIdx < (int)visible.size()) {
                const ChatMessage* m = visible[s_RightClickIdx];
                bool isML = m->fullText.find('\n') != std::string::npos;
                const ImVec2 itemSz(120, 28);
                if (!isML && ImGui::Selectable("Say",   false, 0, itemSz)) { PostMessage(*m, "/say ");   ImGui::CloseCurrentPopup(); }
                if (             ImGui::Selectable("Party", false, 0, itemSz)) { PostMessage(*m, "/party "); ImGui::CloseCurrentPopup(); }
                if (             ImGui::Selectable("Squad", false, 0, itemSz)) { PostMessage(*m, "/squad "); ImGui::CloseCurrentPopup(); }
                if (!isML && ImGui::Selectable("Map",   false, 0, itemSz)) { PostMessage(*m, "/map ");   ImGui::CloseCurrentPopup(); }
            }
            ImGui::EndPopup();
        }
```

Replace the entire block with:

```cpp
        if (ImGui::BeginPopup("##chan")) {
            const ImVec2 itemSz(120, 28);
            // Selecting a channel always updates the sticky channel; in send-and-set
            // mode (s_PickerMsgIdx >= 0) it also posts that message to the channel.
            auto selectChannel = [&](int chIdx) {
                g_Settings.channel = chIdx;
                SaveSettings();
                if (s_PickerMsgIdx >= 0 && s_PickerMsgIdx < (int)visible.size())
                    PostMessage(*visible[s_PickerMsgIdx], Channels::Command(chIdx));
                ImGui::CloseCurrentPopup();
            };
            // 6 rows: left column = standard channels (Say..Team, 5 + 1 blank),
            // right column = guild slots (Guild 1..Guild 6).
            for (int row = 0; row < 6; ++row) {
                if (row < Channels::kStandardCount) {
                    ImGui::PushID(row * 2);
                    if (ImGui::Selectable(Channels::Label(row), false, 0, itemSz))
                        selectChannel(row);
                    ImGui::PopID();
                } else {
                    ImGui::Dummy(itemSz);
                }
                ImGui::SameLine();
                int gi = Channels::kStandardCount + row; // 5..10 -> Guild 1..Guild 6
                ImGui::PushID(row * 2 + 1);
                if (ImGui::Selectable(Channels::Label(gi), false, 0, itemSz))
                    selectChannel(gi);
                ImGui::PopID();
            }
            ImGui::EndPopup();
        }
```

- [ ] **Step 5: Verify it builds**

Run: `cd build && make`
Expected: clean build to `build/SayAgain.dll`, no errors. There should be no remaining references to `s_RightClickIdx` (grep to confirm: `grep -n s_RightClickIdx src/FloatingIcon.cpp` returns nothing).

- [ ] **Step 6: Commit**

```bash
git add src/FloatingIcon.cpp
git commit -m "Add channel header row and 2-column sticky-channel picker"
```

---

## Final Verification (manual, with the user)

The build compiling is necessary but not sufficient — this is a GW2 overlay with
no automated UI tests. After Task 4, ask the user to load the addon in-game and
confirm:

- [ ] The panel shows a gold `▸ <Channel>` header at the top.
- [ ] Left-clicking a message posts to the displayed channel (verify in chat),
      independent of what the chatbox dropdown is set to.
- [ ] Clicking the header opens the 2-column picker; choosing a channel updates
      the header and does **not** send anything.
- [ ] Right-clicking a message opens the same picker; choosing a channel sends
      that message to it **and** updates the header for subsequent left-clicks.
- [ ] The choice survives a full game/addon restart (persisted to settings.json).
- [ ] Guild slots `/g1`–`/g6` reach the expected guilds.
```
