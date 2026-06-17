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
