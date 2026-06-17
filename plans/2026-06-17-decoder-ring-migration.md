# Decoder Ring Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace Say Again's built-in chat-link name resolver (WinHTTP worker + caches + 2,900-entry waypoint table) with a thin client that gets names from the Decoder Ring sibling addon, degrading gracefully to generic placeholders when Decoder Ring is absent.

**Architecture:** Keep the local `[&base64]` *parser* (`ChatLinks`) — we still need to find codes and extract their wire type byte + id. Hand those to Decoder Ring over its published DataLink function table (`DecoderClient`). A new `LinkResolve::Display()` replaces `GW2API::ResolveDisplay()` at every call site, substituting resolved names inline or falling back to `ChatLinks::GenericLabel()`. Delete the entire `GW2API` module and `WaypointData.h`.

**Tech Stack:** C++17, MinGW cross-compile (`x86_64-w64-mingw32-g++`), CMake → `build/SayAgain.dll`, Nexus Addon API, Dear ImGui. Decoder Ring public ABI header (pure, `<cstdint>`-only).

## Global Constraints

- Build/verify after every code change: `cd /home/tony/Dev/say_again/build && make`. A task is not done until this succeeds with no new warnings about the touched files.
- This project has **no host unit-test framework** (unlike Decoder Ring). Verification per task = a clean DLL build plus the stated reasoning/inspection. Final behavioural confirmation is in-game (Task 7), per "test as we go".
- Decoder Ring is a **soft dependency**: never block the render thread, never call a `DecoderRingApi*` without re-validating `apiVersion == DECODER_RING_API_VERSION`, and always have a working fallback when it is absent.
- Pass Decoder Ring the **canonical GW2 wire byte** (`0x02` item, `0x04` map/waypoint, `0x06` skill, `0x0A` skin, `0x0D` build, …), NOT Say Again's internal `ChatLinks::LinkType` enum. (Say Again's enum mislabels `0x0F` as "BuildTemplate"; routing the raw byte sidesteps that.)
- Never deploy the DLL into the game folder — the user deploys. Building is fine.
- Do not bump the version number; the user does that.

---

### Task 1: Vendor the Decoder Ring header and build the DecoderClient seam

The single Nexus↔Nexus seam: acquire Decoder Ring's function table from the DataLink, version-gate it, track it across Decoder Ring load/unload, and expose a warm `Resolve`. Mirrors Pie UI's `src/chat/DecoderClient.{h,cpp}`, trimmed (no trading-post price — Say Again only needs names). Compiles into the build but is not yet called.

**Files:**
- Create: `/home/tony/Dev/say_again/src/DecoderRingApi.h`
- Create: `/home/tony/Dev/say_again/src/DecoderClient.h`
- Create: `/home/tony/Dev/say_again/src/DecoderClient.cpp`
- Modify: `/home/tony/Dev/say_again/CMakeLists.txt` (SOURCES list)

**Interfaces:**
- Consumes: `extern AddonAPI_t* APIDefs;` from `src/SharedState.h` (provides `DataLink_Get`, `Events_Subscribe`, `Events_Unsubscribe`, `Events_Raise`, `Log`, `LOGL_*`).
- Produces:
  - `bool DecoderClient::Available();`
  - `bool DecoderClient::Resolve(uint8_t wireByte, uint32_t id, const char* chatCode, DecoderRecord& out);` — true only on a version-matched `DR_Resolved` record (copied into `out`).
  - `void DecoderClient::Init();` / `void DecoderClient::Shutdown();`
  - The `DecoderRecord` / `DecoderRingApi` PODs and `DECODER_RING_*` constants from the vendored header.

- [ ] **Step 1: Vendor the Decoder Ring public ABI header**

Copy the contract header verbatim into Say Again's source tree (do not hand-edit it):

```bash
cp /home/tony/Dev/decoder_ring/public/DecoderRingApi.h /home/tony/Dev/say_again/src/DecoderRingApi.h
```

- [ ] **Step 2: Create `src/DecoderClient.h`**

```cpp
#pragma once
#include <cstdint>
#include "DecoderRingApi.h"

// Single seam between Say Again and the Decoder Ring (DR) sibling addon. DR
// resolves item / skill / skin names, waypoint/POI names and build labels and
// serves them over a Nexus DataLink function table. This adapter acquires that
// table, tracks it across DR load/unload via DR's READY/UNLOADING events, and
// version-gates it. DR is a SOFT dependency: when absent or version-skewed,
// Available() is false and Resolve() returns false (caller shows a placeholder).
// Nothing here blocks the render thread (warm queries only).
namespace DecoderClient {

void Init();      // subscribe + acquire + ping. Call from AddonLoad after APIDefs is set.
void Shutdown();  // unsubscribe + drop the cached pointer. Call from AddonUnload.

// DR present AND its apiVersion matches what we built against.
bool Available();

// Warm resolve. True only when DR returned DR_Resolved with a version-matched
// record (copied into out). NotReady / Failed / absent -> false (caller renders a
// placeholder and re-calls next frame; render already polls per frame).
// wireByte is the canonical GW2 chat-link type byte (0x02 item, 0x04 map, ...).
bool Resolve(uint8_t wireByte, uint32_t id, const char* chatCode, DecoderRecord& out);

} // namespace DecoderClient
```

- [ ] **Step 3: Create `src/DecoderClient.cpp`**

```cpp
#include "DecoderClient.h"
#include "SharedState.h"   // APIDefs + Nexus typedefs (DataLink_Get, Events_*, Log, LOGL_*)
#include <atomic>
#include <cstring>

namespace DecoderClient {
namespace {
// The acquired DR function table, or null when DR is absent / version-skewed.
// atomic because DR's event handlers may fire on a different thread than the
// render thread that calls Resolve().
std::atomic<const DecoderRingApi*> s_api{nullptr};
bool s_subscribed = false;

bool ApiVersionOk(const DecoderRingApi* api) {
    return api && api->Resolve && api->apiVersion == DECODER_RING_API_VERSION;
}
bool RecordUsable(const DecoderRecord& rec) {
    return rec.status == DR_Resolved && rec.schemaVersion == DECODER_RING_API_VERSION;
}

// (Re)acquire DR's table from the DataLink and version-gate it. Nexus keeps the
// shared block alive after DR unloads but zeroes its function pointers, so a
// non-null block with a null Resolve is a stale/absent service, NOT a version
// conflict — only warn on a genuine skew. Logs only on a transition to present.
void Acquire() {
    const DecoderRingApi* prev = s_api.load();
    auto* api = (APIDefs && APIDefs->DataLink_Get)
        ? (const DecoderRingApi*)APIDefs->DataLink_Get(DECODER_RING_DATALINK)
        : nullptr;
    bool ok = ApiVersionOk(api);
    s_api.store(ok ? api : nullptr);

    if (ok && !prev && APIDefs && APIDefs->Log)
        APIDefs->Log(LOGL_INFO, "SayAgain", "Decoder Ring acquired (version OK)");
    else if (!ok && api && api->Resolve &&
             api->apiVersion != DECODER_RING_API_VERSION && APIDefs && APIDefs->Log)
        APIDefs->Log(LOGL_WARNING, "SayAgain",
                     "Decoder Ring present but API version mismatch - treating as absent");
}

void OnReady(void* /*args*/)     { Acquire(); }
void OnUnloading(void* /*args*/) { s_api.store(nullptr); }
} // namespace

bool Available() { return s_api.load() != nullptr; }

bool Resolve(uint8_t wireByte, uint32_t id, const char* chatCode, DecoderRecord& out) {
    const DecoderRingApi* api = s_api.load();
    if (!api || !api->Resolve) return false;
    DecoderRecord rec;
    std::memset(&rec, 0, sizeof(rec));
    DecoderStatus st = api->Resolve(wireByte, id, chatCode, &rec);
    if (st != DR_Resolved || !RecordUsable(rec)) return false;
    out = rec;
    return true;
}

void Init() {
    if (!APIDefs || !APIDefs->Events_Subscribe) return;
    APIDefs->Events_Subscribe(EV_DECODER_RING_READY,     OnReady);
    APIDefs->Events_Subscribe(EV_DECODER_RING_UNLOADING, OnUnloading);
    s_subscribed = true;

    // DR may have loaded before us: try the DataLink now, then ping so DR re-announces.
    Acquire();
    if (APIDefs->Events_Raise)
        APIDefs->Events_Raise(EV_DECODER_RING_PING, nullptr);
}

void Shutdown() {
    if (APIDefs && s_subscribed && APIDefs->Events_Unsubscribe) {
        APIDefs->Events_Unsubscribe(EV_DECODER_RING_READY,     OnReady);
        APIDefs->Events_Unsubscribe(EV_DECODER_RING_UNLOADING, OnUnloading);
    }
    s_subscribed = false;
    s_api.store(nullptr);
}

} // namespace DecoderClient
```

> If `SharedState.h` does not transitively include the Nexus API header (it declares `extern AddonAPI_t* APIDefs;`, so it must include whatever defines `AddonAPI_t`), add `#include "nexus/Nexus.h"` above `#include "SharedState.h"` in this file. Confirm at build time.

- [ ] **Step 4: Add the new source to `CMakeLists.txt`**

In the `set(SOURCES …)` block (around line 36), add `src/DecoderClient.cpp` after `src/ChatLinks.cpp`. Leave `src/GW2API.cpp` in place for now (removed in Task 5). Resulting block:

```cmake
set(SOURCES
    src/dllmain.cpp
    src/Messages.cpp
    src/FloatingIcon.cpp
    src/ChatLinks.cpp
    src/DecoderClient.cpp
    src/GW2API.cpp
    ${IMGUI_SOURCES}
)
```

- [ ] **Step 5: Build**

Run: `cd /home/tony/Dev/say_again/build && make`
Expected: links `SayAgain.dll` cleanly. `DecoderClient` compiles though nothing calls it yet (a few "unused" notes are fine; no errors).

- [ ] **Step 6: Commit**

```bash
cd /home/tony/Dev/say_again
git add src/DecoderRingApi.h src/DecoderClient.h src/DecoderClient.cpp CMakeLists.txt
git commit -m "Add Decoder Ring client seam (DataLink acquire + version gate)"
```

---

### Task 2: Expose the raw wire byte from the parser

Decoder Ring is keyed by the canonical GW2 wire byte. Say Again's `LinkType` enum is lossy/mislabelled for build links, so capture the raw byte during decode and carry it on the `Link`.

**Files:**
- Modify: `/home/tony/Dev/say_again/src/ChatLinks.h` (the `Link` struct)
- Modify: `/home/tony/Dev/say_again/src/ChatLinks.cpp` (`DecodeGW2Link`)

**Interfaces:**
- Produces: `uint8_t ChatLinks::Link::wire_byte;` — the first decoded payload byte (`0` when the code failed to decode).

- [ ] **Step 1: Add `wire_byte` to the `Link` struct**

In `src/ChatLinks.h`, the `Link` struct currently reads:

```cpp
struct Link {
    LinkType    type        = LinkType::Unknown;
    std::string raw;        // original text, e.g. "[&BNcHAAA=]"
    uint32_t    primary_id  = 0;
};
```

Change it to:

```cpp
struct Link {
    LinkType    type        = LinkType::Unknown;
    std::string raw;        // original text, e.g. "[&BNcHAAA=]"
    uint32_t    primary_id  = 0;
    uint8_t     wire_byte   = 0;  // canonical GW2 chat-link type byte (0 if undecodable)
};
```

- [ ] **Step 2: Populate `wire_byte` in `DecodeGW2Link`**

In `src/ChatLinks.cpp`, find the line `lk.type = TypeFromByte(bytes[0]);` and add the raw byte capture immediately after it:

```cpp
    lk.type      = TypeFromByte(bytes[0]);
    lk.wire_byte = bytes[0];
```

- [ ] **Step 3: Build**

Run: `cd /home/tony/Dev/say_again/build && make`
Expected: clean build (additive struct field; existing code unaffected).

- [ ] **Step 4: Commit**

```bash
cd /home/tony/Dev/say_again
git add src/ChatLinks.h src/ChatLinks.cpp
git commit -m "ChatLinks: capture raw GW2 wire byte for Decoder Ring keying"
```

---

### Task 3: Create the LinkResolve module

The drop-in replacement for `GW2API::ResolveDisplay`. Parses text into segments, resolves each link via Decoder Ring, and substitutes the resolved name inline — or `ChatLinks::GenericLabel` when Decoder Ring is absent or the lookup is not yet warm. Compiles but is not yet wired to call sites.

**Files:**
- Create: `/home/tony/Dev/say_again/src/LinkResolve.h`
- Create: `/home/tony/Dev/say_again/src/LinkResolve.cpp`
- Modify: `/home/tony/Dev/say_again/CMakeLists.txt` (SOURCES list)

**Interfaces:**
- Consumes: `ChatLinks::ParseSegments`, `ChatLinks::GenericLabel`, `ChatLinks::Link::wire_byte` (Task 2); `DecoderClient::Resolve`, `DecoderClient::Available` (Task 1).
- Produces:
  - `std::string LinkResolve::Display(const std::string& text);` — same output contract as the old `GW2API::ResolveDisplay` (codes → bracketed names / placeholders; returns input unchanged when it contains no codes).
  - `bool LinkResolve::Available();` — passthrough to `DecoderClient::Available()`, used by the options banner (Task 6).

- [ ] **Step 1: Create `src/LinkResolve.h`**

```cpp
#pragma once
#include <string>

// Decode the chat-link codes in a message for DISPLAY only (the raw [&...] is
// always what gets sent). Replaces the old GW2API::ResolveDisplay; names now come
// from the Decoder Ring sibling addon via DecoderClient.
namespace LinkResolve {

// Replace each [&...] code in text with its resolved name in brackets (e.g.
// "[Amnoon Waypoint]"), or a generic placeholder ("[Waypoint]") while the lookup
// is pending or when Decoder Ring is absent. Text with no codes is returned as-is.
std::string Display(const std::string& text);

// True when Decoder Ring is present and version-matched (drives the options banner).
bool Available();

} // namespace LinkResolve
```

- [ ] **Step 2: Create `src/LinkResolve.cpp`**

```cpp
#include "LinkResolve.h"
#include "ChatLinks.h"
#include "DecoderClient.h"

namespace LinkResolve {

bool Available() { return DecoderClient::Available(); }

std::string Display(const std::string& text) {
    auto segs = ChatLinks::ParseSegments(text);
    if (segs.empty()) return text;   // no codes present: unchanged

    std::string out;
    out.reserve(text.size());
    for (const auto& s : segs) {
        if (!s.is_link) {
            out += s.text;
            continue;
        }
        DecoderRecord rec;
        if (DecoderClient::Resolve(s.link.wire_byte, s.link.primary_id,
                                   s.link.raw.c_str(), rec) && rec.name[0]) {
            // rec.name is the bare display name; wrap it unless it is already bracketed.
            if (rec.name[0] == '[') out += rec.name;
            else { out += '['; out += rec.name; out += ']'; }
        } else {
            out += ChatLinks::GenericLabel(s.link.type);  // already bracketed, e.g. "[Waypoint]"
        }
    }
    return out;
}

} // namespace LinkResolve
```

- [ ] **Step 3: Add the new source to `CMakeLists.txt`**

Add `src/LinkResolve.cpp` after `src/DecoderClient.cpp` in the `set(SOURCES …)` block:

```cmake
    src/ChatLinks.cpp
    src/DecoderClient.cpp
    src/LinkResolve.cpp
    src/GW2API.cpp
```

- [ ] **Step 4: Build**

Run: `cd /home/tony/Dev/say_again/build && make`
Expected: clean build. `LinkResolve` compiles though still uncalled.

- [ ] **Step 5: Commit**

```bash
cd /home/tony/Dev/say_again
git add src/LinkResolve.h src/LinkResolve.cpp CMakeLists.txt
git commit -m "Add LinkResolve module (Decoder Ring-backed name display)"
```

---

### Task 4: Cut over — lifecycle and all call sites

Switch the lifecycle from the old resolver to `DecoderClient`, and repoint every `GW2API::ResolveDisplay` call to `LinkResolve::Display`. After this task the new path is live; the old `GW2API` files are still present but no longer referenced (removed in Task 5).

**Files:**
- Modify: `/home/tony/Dev/say_again/src/dllmain.cpp` (includes, AddonLoad, AddonUnload, 3 call sites: lines ~297, ~349, ~535)
- Modify: `/home/tony/Dev/say_again/src/FloatingIcon.cpp` (include, 2 call sites: lines ~486, ~509)

**Interfaces:**
- Consumes: `DecoderClient::Init/Shutdown` (Task 1), `LinkResolve::Display` (Task 3).

- [ ] **Step 1: Swap the include in `dllmain.cpp`**

Find `#include "GW2API.h"` (near the other includes around line 11) and replace it with:

```cpp
#include "LinkResolve.h"
#include "DecoderClient.h"
```

- [ ] **Step 2: Swap lifecycle calls in `AddonLoad`**

In `AddonLoad`, replace `GW2API::Initialize();` with:

```cpp
    DecoderClient::Init();
```

- [ ] **Step 3: Swap lifecycle calls in `AddonUnload`**

In `AddonUnload`, replace `GW2API::Shutdown();` with:

```cpp
    DecoderClient::Shutdown();
```

- [ ] **Step 4: Repoint the three `dllmain.cpp` call sites**

At each of the three sites (around lines 297, 349, 535) replace `GW2API::ResolveDisplay(` with `LinkResolve::Display(`. The surrounding lines look like:

```cpp
        std::string preview = GW2API::ResolveDisplay(s_DecLines[i]);
```
→
```cpp
        std::string preview = LinkResolve::Display(s_DecLines[i]);
```

and (line ~535):

```cpp
        std::string preview = GW2API::ResolveDisplay(msg.fullText);
```
→
```cpp
        std::string preview = LinkResolve::Display(msg.fullText);
```

> Use a find/replace of `GW2API::ResolveDisplay` → `LinkResolve::Display` across `dllmain.cpp` to catch all three identically.

- [ ] **Step 5: Swap the include and call sites in `FloatingIcon.cpp`**

Replace `#include "GW2API.h"` with `#include "LinkResolve.h"`, then repoint both call sites (around lines 486 and 509):

```cpp
            std::string lbl = LinkResolve::Display(visible[i]->shortLabel);
```
```cpp
                    std::string tip = LinkResolve::Display(visible[i]->fullText);
```

- [ ] **Step 6: Build**

Run: `cd /home/tony/Dev/say_again/build && make`
Expected: clean build. No remaining references to `GW2API::` anywhere — confirm with:

```bash
cd /home/tony/Dev/say_again && grep -rn "GW2API" src/dllmain.cpp src/FloatingIcon.cpp
```
Expected: no output.

- [ ] **Step 7: Commit**

```bash
cd /home/tony/Dev/say_again
git add src/dllmain.cpp src/FloatingIcon.cpp
git commit -m "Cut name resolution over to Decoder Ring (LinkResolve + DecoderClient)"
```

---

### Task 5: Remove the built-in resolver

Delete the now-orphaned `GW2API` module and the static waypoint table. This is the maintenance payoff (~3,300 lines + a background thread gone). Verifying nothing else references them is the whole point of isolating this as its own task.

**Files:**
- Delete: `/home/tony/Dev/say_again/src/GW2API.h`
- Delete: `/home/tony/Dev/say_again/src/GW2API.cpp`
- Delete: `/home/tony/Dev/say_again/src/WaypointData.h`
- Modify: `/home/tony/Dev/say_again/CMakeLists.txt` (remove `src/GW2API.cpp`)

- [ ] **Step 1: Confirm nothing still references the doomed files**

```bash
cd /home/tony/Dev/say_again
grep -rn "GW2API\|WaypointData" src/ CMakeLists.txt | grep -v "src/GW2API\."
```
Expected: no output (the only remaining mentions are inside GW2API.* themselves, which we are deleting). If anything else appears, repoint it to `LinkResolve` before continuing.

- [ ] **Step 2: Remove the source from `CMakeLists.txt`**

Delete the `src/GW2API.cpp` line from the `set(SOURCES …)` block. Resulting block:

```cmake
set(SOURCES
    src/dllmain.cpp
    src/Messages.cpp
    src/FloatingIcon.cpp
    src/ChatLinks.cpp
    src/DecoderClient.cpp
    src/LinkResolve.cpp
    ${IMGUI_SOURCES}
)
```

- [ ] **Step 3: Delete the files**

```bash
cd /home/tony/Dev/say_again
git rm src/GW2API.h src/GW2API.cpp src/WaypointData.h
```

- [ ] **Step 4: Build**

Run: `cd /home/tony/Dev/say_again/build && make`
Expected: clean build. (If CMake caches the old file list and errors, re-run `cmake ..` in `build/` first, then `make`.)

- [ ] **Step 5: Commit**

```bash
cd /home/tony/Dev/say_again
git add CMakeLists.txt
git commit -m "Remove built-in resolver: GW2API worker + static waypoint table"
```

---

### Task 6: "Decoder Ring not installed" options banner

Since names now come only from Decoder Ring, tell the user how to get them when it is absent. A single, unobtrusive line at the top of the Messages section, shown only when Decoder Ring is missing.

**Files:**
- Modify: `/home/tony/Dev/say_again/src/dllmain.cpp` (Messages section, just after `RenderSectionHeader("Messages", kGold);` near line 480)

**Interfaces:**
- Consumes: `LinkResolve::Available()` (Task 3).

- [ ] **Step 1: Add the conditional banner**

In `AddonOptions`, immediately after the line `RenderSectionHeader("Messages", kGold);` (around line 480) and before the `Add Message` toolbar button, insert:

```cpp
    if (!LinkResolve::Available()) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.1f, 1.0f),
            "Decoder Ring not installed - chat-link names show as placeholders.");
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
            "Install Decoder Ring from the Nexus addon library to decode link names.");
        ImGui::Spacing();
    }
```

> `LinkResolve.h` is already included in `dllmain.cpp` from Task 4. The orange/grey colours match the existing warning style used elsewhere in this panel (e.g. the multi-map delete warning at line ~675).

- [ ] **Step 2: Build**

Run: `cd /home/tony/Dev/say_again/build && make`
Expected: clean build.

- [ ] **Step 3: Commit**

```bash
cd /home/tony/Dev/say_again
git add src/dllmain.cpp
git commit -m "Options: show banner prompting Decoder Ring install when absent"
```

---

### Task 7: Final verification and documentation

**Files:**
- Modify: `/home/tony/Dev/say_again/handover.md`
- Modify: `/home/tony/Dev/say_again/plans/2026-06-07-chip-editor-discussion.md` (note the resolver is now Decoder Ring, not the in-house stack)

- [ ] **Step 1: Full clean rebuild**

```bash
cd /home/tony/Dev/say_again/build && cmake .. && make
```
Expected: `SayAgain.dll` builds cleanly from scratch.

- [ ] **Step 2: In-game smoke test (user-driven, "test as we go")**

The user deploys the DLL (never the agent). Confirm, with Decoder Ring installed:
- A message containing a waypoint code (e.g. `[&BH4HAAA=]`) shows the real waypoint name in the panel button, the 1-second hover tooltip, the options message-list preview, and the editor preview rows.
- An item code shows the item name; a skill code shows the skill name.
- Disable/unload Decoder Ring → names fall back to `[Waypoint]`/`[Item]` placeholders and the options banner appears. Re-enable → names return (the READY ping re-acquires live).
- Sending a message still pastes the **raw** `[&...]` code into chat (display-only resolution never mutates `fullText`).

- [ ] **Step 3: Update `handover.md`**

Replace the `GW2API.h/cpp` and `WaypointData.h` bullets in the "Source Files" section and the "Chat code resolution" design-decision block with the new reality: `ChatLinks` parses codes locally and hands `(wire_byte, id, raw)` to `DecoderClient`, which talks to the Decoder Ring sibling addon over the `DECODER_RING_API` DataLink with the READY/UNLOADING/PING handshake; `LinkResolve::Display` substitutes names for display only; absent Decoder Ring degrades to `ChatLinks::GenericLabel` placeholders plus an options banner. Note the deleted files and that `chatcode_cache.json` is no longer written (Decoder Ring owns caching now). Record that Say Again sends the canonical wire byte to Decoder Ring, sidestepping its own enum's `0x0F`-as-build mislabel.

- [ ] **Step 4: Update the chip-editor discussion note**

In `plans/2026-06-07-chip-editor-discussion.md`, add a dated line under "Decisions / work items": the chip editor's resolver adapter is **superseded** — chips will resolve through `DecoderClient` (Decoder Ring), not Say Again's old in-house resolver, which no longer exists. Rarity colour comes from `DecoderRecord::rarity`.

- [ ] **Step 5: Commit**

```bash
cd /home/tony/Dev/say_again
git add handover.md plans/2026-06-07-chip-editor-discussion.md
git commit -m "Handover: document Decoder Ring migration"
```

---

## Self-Review

**Spec coverage:**
- Full cutover to Decoder Ring → Tasks 1, 3, 4 (wire-up) + Task 5 (delete in-house resolver). ✓
- Graceful degradation when absent → `DecoderClient` version-gate + `LinkResolve` fallback to `GenericLabel` (Task 3); soft-dependency handshake (Task 1). ✓
- Prominent options banner when absent → Task 6. ✓
- Decoder Ring is in the Nexus library → banner copy names it (Task 6). ✓
- Support all link types, waypoints especially → wire byte passed raw (Task 2); Decoder Ring resolves waypoints from its own offline data; `GenericLabel` covers every `LinkType` as fallback. ✓
- Chips deferred to a later plan → out of scope here; only the discussion note is touched (Task 7). ✓

**Placeholder scan:** No TBD/TODO/"handle edge cases"/"similar to Task N" — every code step shows complete code. ✓

**Type consistency:** `DecoderClient::Resolve(uint8_t, uint32_t, const char*, DecoderRecord&)` defined in Task 1, consumed identically in Task 3. `Link::wire_byte` (`uint8_t`) defined in Task 2, consumed in Task 3. `LinkResolve::Display`/`Available` defined in Task 3, consumed in Tasks 4/6. `DecoderClient::Init/Shutdown` defined in Task 1, consumed in Task 4. ✓
