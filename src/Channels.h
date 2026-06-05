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
