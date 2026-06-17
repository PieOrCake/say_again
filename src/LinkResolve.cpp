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
