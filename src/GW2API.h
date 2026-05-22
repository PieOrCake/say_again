#pragma once
#include "ChatLinks.h"
#include <string>

namespace GW2API {

void Initialize();
void Shutdown();

enum class State : uint8_t { Pending, Resolved, Failed };

// Look up a resolved display name. Returns the name on success; the generic
// placeholder ("[Waypoint]" etc.) while pending or on failure. Queues a fetch
// the first time a given (type, id) is requested.
std::string LookupOrRequest(ChatLinks::LinkType type, uint32_t id);

// Apply LookupOrRequest to every link segment in `text` and return a string
// with each [&...] replaced by its resolved name (or generic placeholder).
std::string ResolveDisplay(const std::string& text);

} // namespace GW2API
