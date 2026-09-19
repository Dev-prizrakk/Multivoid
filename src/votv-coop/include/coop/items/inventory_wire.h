// coop/items/inventory_wire.h -- serialize the per-player inventory POD <-> a byte blob.
//
// Turns ue_wrap::inventory::PlayerInventory (read off the live saveSlot by ue_wrap/inventory)
// into a self-contained, version-prefixed little-endian byte blob, and back. FNames and UClasses
// are wired as STRINGS -- pointers are not portable -- and re-interned or FindClass'd on apply,
// with the exact case preserved. The FTransform packs as 10 floats, and the 0x70 signal
// sub-element reuses the coop/signal_wire serializer rather than a second one.
//
// The blob is what coop/blob_chunks carries in chunks (client->host), what persists to
// <gameDir>/coop_players/<hostSlot>/<guid>.json, and what the join apply reads back.

#pragma once

#include "ue_wrap/actors/inventory.h"  // PlayerInventory

#include <cstdint>
#include <vector>

namespace coop::inventory_wire {

// Current blob format version (the first byte). Bump on any change of layout OR meaning.
//   1 -- the inventory third was saveSlot.inventoryData, a save-side projection the game never
//        reads back; equipment and hold were live data. Still parsed (same layout), so the host
//        can lift a stored profile out of it; see Deserialize's `outVersion`.
//   2 -- the inventory third is what the player carries (saveSlot.GObjStack[0]).
inline constexpr uint8_t kVersion = 2;
inline constexpr uint8_t kVersionProjection = 1;

// Serialize `inv` into a fresh blob (always succeeds; bounded by the inventory size).
std::vector<uint8_t> Serialize(const ue_wrap::inventory::PlayerInventory& inv);

// Parse `blob` back into `out` (cleared first). False on a truncated / malformed / unknown-
// version blob (a corrupt or hostile blob must never over-read or over-allocate). With
// `outVersion` null only kVersion parses; with it set, a kVersionProjection blob parses too and
// the caller is told which it got -- its inventory third is then NOT what the player carried.
bool Deserialize(const std::vector<uint8_t>& blob, ue_wrap::inventory::PlayerInventory& out,
                 uint8_t* outVersion = nullptr);

}  // namespace coop::inventory_wire
