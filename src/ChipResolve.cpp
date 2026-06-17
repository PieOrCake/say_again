#include "ChipResolve.h"
#include "ChatLinks.h"
#include "DecoderClient.h"   // pulls in DecoderRingApi.h (DecoderRecord, DecoderRarity)

namespace ChipResolve {

// Default tint for non-item links (waypoints, skills, traits, builds, …): a light blue echoing
// GW2's in-chat link styling.
static const ImU32 kDefaultChipColor = IM_COL32(150, 200, 255, 255);

// GW2 item-rarity colours, keyed by DecoderRarity.
static ImU32 RarityColor(uint8_t rarity) {
    switch (rarity) {
        case DR_Junk:       return IM_COL32(170, 170, 170, 255);
        case DR_Basic:      return IM_COL32(255, 255, 255, 255);
        case DR_Fine:       return IM_COL32( 98, 164, 243, 255);
        case DR_Masterwork: return IM_COL32( 38, 168,  16, 255);
        case DR_Rare:       return IM_COL32(252, 208,  11, 255);
        case DR_Exotic:     return IM_COL32(255, 164,   5, 255);
        case DR_Ascended:   return IM_COL32(251,   0, 127, 255);
        case DR_Legendary:  return IM_COL32(168,  80, 255, 255);
        default:            return kDefaultChipColor;   // DR_RarityUnknown / unresolved
    }
}

void Resolve(const std::string& code, std::string& outName, ImU32& outColor) {
    outName.clear();
    outColor = kDefaultChipColor;

    auto segs = ChatLinks::ParseSegments(code);
    if (segs.empty() || !segs.front().is_link) {
        outName = ChatLinks::GenericLabel(ChatLinks::LinkType::Unknown);   // "[Link]"
        return;
    }
    const ChatLinks::Link& lk = segs.front().link;

    DecoderRecord rec;
    if (DecoderClient::Resolve(lk.wire_byte, lk.primary_id, lk.raw.c_str(), rec) && rec.name[0]) {
        outName = "[";
        outName += rec.name;
        outName += "]";
        outColor = (lk.type == ChatLinks::LinkType::Item) ? RarityColor(rec.rarity)
                                                          : kDefaultChipColor;
    } else {
        outName  = ChatLinks::GenericLabel(lk.type);   // "[Waypoint]" / "[Item]" / …
        outColor = kDefaultChipColor;
    }
}

} // namespace ChipResolve
