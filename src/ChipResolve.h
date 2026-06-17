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
