#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace ChatLinks {

enum class LinkType : uint8_t {
    Item,           // wire byte 0x02
    MapLink,        // wire byte 0x04 — waypoint / POI / vista
    Skill,          // wire byte 0x06
    Trait,          // wire byte 0x07
    Recipe,         // wire byte 0x09
    Skin,           // wire byte 0x0A
    Outfit,         // wire byte 0x0B
    BuildTemplate,  // wire byte 0x0F
    Unknown
};

struct Link {
    LinkType    type        = LinkType::Unknown;
    std::string raw;        // original text, e.g. "[&BNcHAAA=]"
    uint32_t    primary_id  = 0;
};

struct Segment {
    bool        is_link = false;
    std::string text;   // plain text when !is_link
    Link        link;   // valid when is_link
};

// Parse text into segments. Returns empty vector if no chat-code links found.
std::vector<Segment> ParseSegments(const std::string& text);

// Returns "[Item]" / "[Waypoint]" / etc. — the placeholder shown while pending.
const char* GenericLabel(LinkType t);

} // namespace ChatLinks
