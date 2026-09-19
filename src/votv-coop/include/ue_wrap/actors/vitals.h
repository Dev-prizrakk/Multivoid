// ue_wrap/actors/vitals.h -- local-player vitals scalar accessor (engine substrate).
//
// Resolves the canonical live vitals store -- UmainGameInstance_C::save_gameInst, a UsaveSlot_C*
// and exactly ONE per machine -- and reads and writes the player's vital scalars by reflected
// field offset. Blueprint-cooked offsets shift across recooks, so every offset is resolved by
// NAME and never hardcoded. Game thread only (UObject lookups and blueprint-state writes). No
// network, coop or quantization logic (principle 7): the callers in coop/ own the wire encoding.
//
// CAUTION: there is exactly ONE saveSlot per machine, and every remote-player puppet AND the
// local player resolve to it. This accessor only ever touches the LOCAL player's vitals. NEVER
// use it to store a remote puppet's display health -- that would corrupt the local player's
// persisted health. Puppet display health lives on coop::RemotePlayer.
#pragma once

#include <string>
#include <vector>

namespace ue_wrap::vitals {

// Vital scalars on UsaveSlot_C (offsets per CXXHeaderDump/saveSlot.hpp, resolved
// by name at runtime). `health` is current HP; `MaxHealth` is per-peer (upgrades
// / story can raise it) so health-bar wire encoding must normalize health/max.
enum class Field {
    Health,     // saveSlot.health    (current HP)
    MaxHealth,  // saveSlot.maxHealth (per-peer cap)
    Food,
    Sleep,
};

// Read the LOCAL player's vital `f` into *out. Returns false (leaving *out
// untouched) if the store isn't resolvable yet (still booting / save not
// registered) or the field offset can't be found. Game-thread only; O(1) after
// the one-time resolution cache fills.
bool Read(Field f, float* out);

// Write `v` to the LOCAL player's vital `f`. Returns false if unresolved.
// Game-thread only.
bool Write(Field f, float v);

// ---- The whole per-player vital state, as one value ------------------------------------------
//
// Gameplay reads and writes these IN PLACE on the save object (the hunger drain is a
// VictoryFloatMinusEquals on saveSlot.food), so the save object is their live store and there is
// no copy on the pawn. A joiner's world is built from a capture of the HOST's save object, so
// unless they are replaced there the joiner starts at the host's numbers.
struct Snapshot {
    float health = 0, maxHealth = 0, food = 0, sleep = 0;
    float battery = 0;                     // flashlight charge
    std::wstring flashlightBattery;        // the inserted battery's class, as its leaf name
    float coffeePower = 0, gasolinepilled = 0, strength = 0, agility = 0;
    std::vector<std::wstring> foodConsumed;   // parallel arrays: what was eaten, and the
    std::vector<float>        foodTolerance;  // tolerance built up against it
};

// Read the LOCAL player's state off the live save object. False if it is not resolvable yet.
bool ReadSnapshot(Snapshot& out);

// What a player who has never played here starts from: the save class's own defaults, read off
// its class default object rather than restated. False if the class is not loaded.
bool ReadDefaults(Snapshot& out);

// Write `s` onto `saveSlot`, which may be a save object whose world does not exist yet. The two
// food arrays are rebuilt as engine-owned arrays of equal length, the previous buffers orphaned
// (the contract of inventory::ApplyToSaveObject). An empty battery class is "no battery in the
// flashlight" and is written as one; a class that is not loaded leaves the field as it was.
// False, having written nothing, if `saveSlot` is dead or a field is missing.
bool ApplySnapshot(void* saveSlot, const Snapshot& s);

// saveSlot.playerTransform: where the game believes the player is. On a client nothing writes it
// (saveObjects never runs there), so it stays the HOST's position from the join capture -- and it
// is where the game's anti-noclip check puts a player it finds behind geometry. Written before
// the world exists so that fallback is the player's own spot. Scale 1. False if it did not resolve.
bool WritePlayerTransform(void* saveSlot, float x, float y, float z, float yawDeg);

}  // namespace ue_wrap::vitals
