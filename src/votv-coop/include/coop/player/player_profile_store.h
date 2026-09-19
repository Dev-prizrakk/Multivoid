// coop/player/player_profile_store.h -- where the HOST keeps each player's profile blob: the newest
// one in memory, and a copy on disk that is cut only when the host's world is saved.
//
// A profile and the world are two halves of one state. An item a player pockets leaves the world
// and enters the profile, so a disk profile NEWER than the saved world holds that item twice after
// a host restart (in the pocket, and back on the floor the old world still has), and an OLDER one
// loses what the player dropped. So a profile goes to disk as it stood when the saved world was
// GATHERED, and at no other moment:
//   * Put keeps the newest profile per GUID in memory, for as long as the hosted world lives --
//     past the player's disconnect and past the last player's, so a rejoin gets what they left
//     with, and a save made after they left still cuts their last state. Loading another world to
//     host (save_transfer::SetHostSlot) drops what is held: that world is as old as its save;
//   * MarkWorldGathered, called when the game gathers the world into its save object, sets aside
//     every profile that changed since the last gather. That is not always the moment of a save:
//     during an event the game writes saves WITHOUT gathering (ue_wrap/engine/save_capture.h), and
//     the world in such a file is the last gather's -- so the profiles beside it must be too;
//   * CutToDisk, called when the host's world save is written, writes what was set aside;
//   * nothing is written on a disconnect, on shutdown or on a timer. A host that quits without
//     saving reverts its world to the last save, and the profiles revert with it.
// MTA saves an account when it changed and 15 s have passed, and at quit (CAccountManager.cpp:
// 142-148 the pulse, 303-316 Save, 115-118 the destructor): its world is not persisted, so its accounts have nothing to stay consistent
// WITH. Ours has a saved world, which is the reason for the divergence.
//
// What this does not close: a profile reaches the host up to a second after the pickup it
// reports (the client's 1 Hz poll), so a save inside that second cuts a profile one pickup
// behind its world. Closing it needs the pickup itself to be a host-side transaction.
//
// The file is <game dir>/coop_players/<host save slot>/<guid>.json: in the game folder beside the
// ini and the log rather than in AppData, so the per-player files are easy to find and hand-edit;
// keyed per host save slot, so different worlds keep separate profiles. The blob is opaque here
// (coop/items/inventory_wire owns its layout): the file wraps it in a magic, an FNV integrity
// hash, and a readable nick and last-seen time. The GUID names the file, so it is checked again
// at this boundary: one that is not exactly 32 hex characters never becomes a path component.
//
// Game thread: the memory half has no lock.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace coop::player_profile_store {

// A nick as it may stand inside the file's JSON string field: UTF-8, capped, with the two JSON
// metacharacters dropped.
std::string NickForJson(const std::wstring& nick);

// Keep `blob` as the newest profile of `guid`. False when it is byte-identical to the one held,
// which costs nothing at the next cut.
bool Put(const std::string& guid, std::vector<uint8_t> blob, const std::string& nickJson);

// The newest profile of `guid`: the one held in memory, else the file (FNV-verified, falling back
// to the .bak of the last good file). A corrupt file never yields unverified bytes, and it is NOT
// the same answer as no file: Absent is the first-join test, Unreadable is a returning player
// whose profile is still on disk.
enum class Found { Yes, Absent, Unreadable };
Found Get(const std::string& guid, std::vector<uint8_t>& outBlob);

// This player's stored profile did not read, or was written by a build this one cannot parse.
// Nothing of theirs is held or written for the rest of this hosted world, so the files stay as
// they are for recovery: a starter kit streamed back must not overwrite them.
void Quarantine(const std::string& guid);

// Is any profile held that a gather or a save would have to do something with?
bool AnythingPending();

// The game gathered the world into its save object: set aside every profile that changed since
// the last gather, as the copy the next save writes.
void MarkWorldGathered();

// The host's world save was written: write what the last gather set aside (atomically -- a temp
// file and a rename -- keeping the previous file as .bak). Returns how many were written. A
// failed write stays for the next cut; a profile that is on disk and unchanged since is let go of,
// because Get gives it back from its file.
size_t CutToDisk();

}  // namespace coop::player_profile_store
