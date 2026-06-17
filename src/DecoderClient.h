#pragma once
#include <cstdint>
#include "DecoderRingApi.h"

// Single seam between Say Again and the Decoder Ring (DR) sibling addon. DR
// resolves item / skill / skin names, waypoint/POI names and build labels and
// serves them over a Nexus DataLink function table. This adapter acquires that
// table, tracks it across DR load/unload via DR's READY/UNLOADING events, and
// version-gates it. DR is a SOFT dependency: when absent or version-skewed,
// Available() is false and Resolve() returns false (caller shows a placeholder).
// Nothing here blocks the render thread (warm queries only).
namespace DecoderClient {

void Init();      // subscribe + acquire + ping. Call from AddonLoad after APIDefs is set.
void Shutdown();  // unsubscribe + drop the cached pointer. Call from AddonUnload.

// DR present AND its apiVersion matches what we built against.
bool Available();

// Warm resolve. True only when DR returned DR_Resolved with a version-matched
// record (copied into out). NotReady / Failed / absent -> false (caller renders a
// placeholder and re-calls next frame; render already polls per frame).
// wireByte is the canonical GW2 chat-link type byte (0x02 item, 0x04 map, ...).
bool Resolve(uint8_t wireByte, uint32_t id, const char* chatCode, DecoderRecord& out);

} // namespace DecoderClient
