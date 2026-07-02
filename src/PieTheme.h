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

#define PIEUI_THEME_VERSION 2u

// Fixed ABI mirrored from Pie UI. Colours are IM_COL32 values packed 0xAABBGGRR.
// v2 adds the button trio: resting buttons are a dark, dimmed fill (NOT the
// bright accent), leaning toward the accent only on hover/active.
struct PieUiTheme {
    uint32_t version;
    uint32_t accent;
    uint32_t window_bg;
    uint32_t header_bg;
    uint32_t text;
    uint32_t text_muted;
    uint32_t border;
    uint32_t button;          // resting button fill (v2)
    uint32_t button_hovered;  // hovered button fill (v2)
    uint32_t button_active;   // pressed/active button fill (v2)
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
