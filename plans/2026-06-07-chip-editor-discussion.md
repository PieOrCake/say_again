# Chip Text Editor — Discussion Notes (2026-06-07)

**Status:** Discussion only. No decision made, no code written. Resume next session.

## Goal

Replace Say Again's add/edit message text entry + faded preview rows with a
rich-text entry like the one Pie UI uses in its chatbox: chat-link codes decoded
inline into coloured **chips** (atomic tokens).

## The reference widget: Pie UI `ChipTextEdit`

- Files: `pie_ui/src/widgets/ChipTextEdit.{h,cpp}` (~320 lines .cpp, 55 .h).
- A **single-line** custom text box built directly on `stb_textedit` (the same
  low-level engine ImGui's `InputText` uses), driven by hand.
- Each "character" is a cell; a chat-link code is stored as **one atomic cell**
  rendered as a coloured token. Caret skips a chip as one unit, one backspace
  removes the whole code, and you can't corrupt the middle of a `[&base64]`.
- `GetText()` reconstructs the raw codes for sending; `SetText()` parses
  `[&...]` back into chips.
- Resolves chip labels via Pie UI's `RichLineResolveChip()`, which sits on a
  **heavy** stack: item/skin/skill caches, waypoint table, rarity colouring.

## Say Again's current editor

- `RenderDecoratedEditor` in `src/dllmain.cpp` (~line 265).
- **Multi-line**: one `ImGui::InputText` per line, green `>` prefix, Enter
  splits the line at the cursor, red `x` deletes a line.
- Codes shown as raw `[&...]` in the box, with a **separate faded grey preview
  row** beneath each line showing resolved names (via `GW2API::ResolveDisplay`).
- Say Again already has its own async resolver: `GW2API::ResolveDisplay` /
  `LookupOrRequest` (background WinHTTP + persisted cache) and its own
  `ChatLinks::ParseSegments`.

## Mapping it onto Say Again — key facts

This is **NOT a drop-in**. The shapes differ:

| | Say Again now | ChipTextEdit |
|---|---|---|
| Lines | Multi-line (one InputText/line) | Single-line only |
| Codes | Raw text + separate grey preview row | Inline coloured chip |
| Resolver | Say Again's own (WinHTTP + cache) | Pie UI's heavy RichLine stack |

**Realistic plan:** keep Say Again's line-list structure; swap each line's
`InputText` for its own `ChipTextEdit` instance. The per-line grey preview row
then disappears (resolved names live inline as chips).

## Decisions / work items

1. **Don't port Pie UI's resolver.** Write a ~20-line adapter `ResolveChip(code)`
   that calls Say Again's existing `ChatLinks::ParseSegments` +
   `GW2API::LookupOrRequest`. Keeps dependency surface tiny.

2. **Probably drop the keyboard-capture plumbing.** The `ChipInputActive()` /
   WndProc reinforcement in Pie UI exists because its chatbox is an in-game
   overlay fighting the game for keypresses. Say Again's editor lives in the
   **Nexus Addon Options window**, where Nexus already manages focus. Likely
   unnecessary — **needs a quick test to confirm** (this is the main de-risk
   item before planning).

3. **Two genuine modifications to the widget:**
   - **Enter-to-split at cursor:** expose the caret index so we can split a
     line's cells at the cursor into a new line below.
   - **Paste should chip-ify codes:** today only `SetText` parses `[&...]`;
     `Ctrl+V` inserts raw chars. Say Again users paste codes from wiki/game, so
     paste should re-parse.

4. **Per-line state management:** each line becomes a `ChipTextEdit` object
   (owns its own edit state) instead of a `std::string`; split/delete/focus
   handoff needs bookkeeping. Combined text = each line's `GetText()` joined by
   `\n`.

5. **No conflict with existing features:** `{option|option}` random-word braces
   and the message prefix are plain text — they ride along fine (just not
   chipped).

## Open questions for the user (asked, not yet answered)

- **Colour:** single link tint (reuse existing gold/cyan) vs. porting rarity
  colouring (more resolver work)?
- **Scope:** keep multi-line (per-line chip boxes), or collapse messages to
  single-line in the process?

## Assessment

- **Strongest reason to do it:** correctness — chips make codes atomic and
  tamper-proof, and remove the separate preview row (less clutter). Matches the
  in-game / Pie UI look; colour is a bonus.
- **Against:** a hand-driven `stb_textedit` widget is more fragile than
  `InputText` (selection/clipboard/IME edge cases) and is more code to own.
- **Effort:** medium — ~a focused day, mostly split-at-cursor + paste-parsing +
  resolver adapter.
- **Recommendation:** worth it, primarily for tamper-proof codes. De-risk item 2
  (keyboard/focus in the options window) before committing to a plan.
