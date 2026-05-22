#include "ChatLinks.h"

namespace ChatLinks {

// --- Base64 decode (standard alphabet) ---
static int B64Val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static std::vector<uint8_t> Base64Decode(const std::string& b64) {
    std::vector<uint8_t> out;
    out.reserve(b64.size() * 3 / 4 + 1);
    int val = 0, bits = -8;
    for (char c : b64) {
        if (c == '=') break;
        int v = B64Val(c);
        if (v < 0) continue;
        val = (val << 6) | v;
        bits += 6;
        if (bits >= 0) {
            out.push_back(static_cast<uint8_t>((val >> bits) & 0xFF));
            bits -= 8;
        }
    }
    return out;
}

static uint32_t ReadU32LE(const uint8_t* p) {
    return  static_cast<uint32_t>(p[0])
        | (static_cast<uint32_t>(p[1]) <<  8)
        | (static_cast<uint32_t>(p[2]) << 16)
        | (static_cast<uint32_t>(p[3]) << 24);
}

static uint32_t ReadU24LE(const uint8_t* p) {
    return  static_cast<uint32_t>(p[0])
        | (static_cast<uint32_t>(p[1]) <<  8)
        | (static_cast<uint32_t>(p[2]) << 16);
}

static LinkType TypeFromByte(uint8_t b) {
    switch (b) {
        case 0x02: return LinkType::Item;
        case 0x04: return LinkType::MapLink;
        case 0x06: return LinkType::Skill;
        case 0x07: return LinkType::Trait;
        case 0x09: return LinkType::Recipe;
        case 0x0A: return LinkType::Skin;
        case 0x0B: return LinkType::Outfit;
        case 0x0F: return LinkType::BuildTemplate;
        default:   return LinkType::Unknown;
    }
}

const char* GenericLabel(LinkType t) {
    switch (t) {
        case LinkType::Item:          return "[Item]";
        case LinkType::MapLink:       return "[Waypoint]";
        case LinkType::Skill:         return "[Skill]";
        case LinkType::Trait:         return "[Trait]";
        case LinkType::Recipe:        return "[Recipe]";
        case LinkType::Skin:          return "[Skin]";
        case LinkType::Outfit:        return "[Outfit]";
        case LinkType::BuildTemplate: return "[Build]";
        default:                      return "[Link]";
    }
}

static Link DecodeGW2Link(const std::string& raw) {
    Link lk;
    lk.raw = raw;

    size_t amp = raw.find('&');
    if (amp == std::string::npos || amp + 1 >= raw.size()) return lk;

    size_t close = raw.find(']', amp + 1);
    if (close == std::string::npos) return lk;

    std::string b64 = raw.substr(amp + 1, close - (amp + 1));
    auto bytes = Base64Decode(b64);
    if (bytes.empty()) return lk;

    lk.type = TypeFromByte(bytes[0]);

    // Item: [0]=quantity [1..3]=ItemId(uint24) ...
    // Others: [0..3]=id(uint32) after the type byte
    if (lk.type == LinkType::Item) {
        if (bytes.size() >= 5) lk.primary_id = ReadU24LE(&bytes[2]);
    } else {
        if (bytes.size() >= 5) lk.primary_id = ReadU32LE(&bytes[1]);
    }
    return lk;
}

std::vector<Segment> ParseSegments(const std::string& text) {
    std::vector<Segment> result;
    bool any_link = false;
    std::string plain;

    auto flushPlain = [&]() {
        if (!plain.empty()) {
            Segment s;
            s.is_link = false;
            s.text    = std::move(plain);
            plain.clear();
            result.push_back(std::move(s));
        }
    };

    size_t i = 0;
    while (i < text.size()) {
        if (text[i] == '[' && i + 1 < text.size() && text[i+1] == '&') {
            size_t close = text.find(']', i + 2);
            if (close != std::string::npos) {
                flushPlain();
                Segment s;
                s.is_link = true;
                s.link    = DecodeGW2Link(text.substr(i, close - i + 1));
                result.push_back(std::move(s));
                any_link = true;
                i = close + 1;
                continue;
            }
        }
        plain += text[i++];
    }
    flushPlain();

    if (!any_link) result.clear();
    return result;
}

} // namespace ChatLinks
