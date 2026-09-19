// coop/items/player_inventory_sync.h -- the per-player profile (coop/items/player_profile.h:
// what a player carries, wears and holds, their vitals, where they stood), host-persisted at <game dir>/coop_players/<host save>/<guid>.json. The GUID is derived from the
// key that peer PROVED at admission (coop/net/peer_identity.h), never from a value it sent.
//
// A joiner's world is built from a capture of the HOST's save object, and the carried items
// live in that object (saveSlot.GObjStack[0]), so without a substitution every joiner carries
// the host's items under the host's keys. In session order: the host sends a joining peer its
// profile while that peer is still pre-world (a first join gets a starter kit under fresh
// keys), and the client writes it into the save object before the world materialises; the
// client streams its profile back as it changes, only from a world that was built from one;
// the host persists each complete, changed blob. A file exists only once a client has
// streamed, so a missing file IS the first-join test.

#pragma once

#include <cstdint>

namespace coop::net { class Session; struct BlobChunkPayload; }

namespace coop::player_inventory_sync {

// Cache the session pointer. Call once at boot (subsystems Install).
void Install(coop::net::Session* session);

// ---- transport and host persistence ----

// Bidirectional PlayerInventoryBlob receiver (event_feed -> here); branches by role:
//   * HOST receiving from a CLIENT slot (1..): one chunk of that client's inventory STREAM.
//     Reassembles (per-sender) and, on a complete + CHANGED blob, persists it to that peer's
//     coop_players/<guid>.json (atomic, magic + FNV integrity, .bak of last good, 15s rate-limit).
//   * CLIENT receiving from the HOST (slot 0): one chunk of the host's ON-JOIN apply blob.
//     Reassembles and, on completion, deserializes + stashes it as the pending
//     per-player inventory (HasPendingApply()), to be written into the save object by the
//     SaveObjectReadyHook before the world materializes.
// Game thread.
void OnReliable(const coop::net::BlobChunkPayload& p, uint8_t senderPeerSlot);

// ---- the live apply on join: host to client, then the save-object-ready hook ----

// HOST: send peer `peerSlot` its persisted per-player inventory (read from coop_players/<guid>.json,
// FNV-verified, .bak fallback, the starter kit on missing/corrupt -- never another player's
// inventory). Chunked to that ONE slot over PlayerInventoryBlob. Called from the host tick the
// moment the slot is connected and its GUID has arrived, so the blob lands in the joiner's
// pre-world window. No-op off the host or when the peer's GUID hasn't arrived. Returns true iff
// the blob was actually enqueued -- the caller (HostPersistTick) latches "sent" ONLY on true, and retries next tick on a channel-busy refusal
// (else the connect-edge refusal was never retried -> the client never got its inventory). Game thread.
bool SendInventoryToSlot(int peerSlot);

// CLIENT: true once the host's on-join apply blob has arrived + deserialized (the join boot waits
// on this before loading the world, so the SaveObjectReadyHook always has the data). Game thread.
bool HasPendingApply();

// CLIENT: the next save object to come ready belongs to a join, so the hook may substitute the
// profile into it (or empty the host's items out of it when none arrived). One-shot, consumed by
// the hook and cleared on disconnect; the join boot calls it right before each world load. Every
// other load in the process -- a later Host-with-save above all -- is left alone. Any thread.
void BeginJoinApply();

// CLIENT: where the applied profile says this player stood, for the join's world appearance.
// One-shot: a later body in the same session is a respawn, which belongs at the start point.
// False on a first join, for a player who left dead, and with no profile applied: the caller
// then uses the start point. Game thread.
bool TakeJoinPose(float& x, float& y, float& z, float& yaw);

// Per-slot disconnect (host): flush that peer's last inventory blob to disk + drop its
// in-memory entry. Client: no-op. Game thread.
void OnDisconnectForSlot(int peerSlot);

// Aggregate disconnect: host flushes all pending blobs; client clears its send-dedup. Game thread.
void OnDisconnect();

// Host shutdown hook: flush every connected peer's last blob to disk BEFORE the session stops
// (pure file I/O on captured bytes -- safe on the WM_CLOSE thread). No-op off the host.
void FlushAllToDisk();

// Per-tick: the client's outbound inventory stream, or the host's persist pass, by role. It
// also carries a one-shot read-verify self-test (ini inventory_selftest=1) that reads the local
// saveSlot inventory a few seconds after world-up and logs what it found; that part is a no-op
// unless the flag is set. Game thread.
void Tick();

}  // namespace coop::player_inventory_sync
