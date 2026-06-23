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

// Forward-compatible: the record layout is stable across ABI bumps (new data rides
// existing fields), so a service at-or-above the version we built against is usable.
// A version below ours — including the post-unload 0 — is treated as absent.
bool ApiVersionOk(const DecoderRingApi* api) {
    return api && api->Resolve && api->apiVersion >= DECODER_RING_API_VERSION;
}
// The fields this consumer reads (name, rarity) have existed since schema v3, so gate
// readability on that introducing version — NOT the built-against version, which would
// needlessly reject a future newer service.
bool RecordUsable(const DecoderRecord& rec) {
    return rec.status == DR_Resolved && rec.schemaVersion >= 3u;
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
             api->apiVersion < DECODER_RING_API_VERSION && APIDefs && APIDefs->Log)
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
