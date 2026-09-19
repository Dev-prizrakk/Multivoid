// ue_wrap/engine/save_capture.h -- capture the host's LIVE world to a scratch save slot.
//
// Engine-wrapper layer (principle 7): pure engine-substrate access. It drives VOTV's own save
// populate (mainGamemode::saveObjects/saveTriggers) + the engine serializer
// (GameplayStatics::SaveGameToSlot). No network/gameplay logic here.
//
// WHY. save_transfer historically shipped the host's STALE on-disk .sav, so any entity the host
// changed AFTER its last autosave reached the joiner as it had been at that autosave -- a kerfur
// the host turned ON arrived as a turned-OFF prop, and the client's reconcile layer (divergence
// sweep + npc adoption) had to fight that staleness, duplicating or diverging the kerfur. The
// game's OWN save/load already round-trips a turned-on kerfur correctly, since it is an
// int_save_C actor and a single-player save/reload keeps it on. The only defect was snapshotting
// a stale file. This captures the host world LIVE at the instant a client joins, through the
// game's own save path, into a THROWAWAY slot -- so the joiner's native loadObjects() builds a
// world with nothing left to reconcile.

#pragma once

#include <string>

namespace ue_wrap::save_capture {

// Repopulate the host's in-memory world save (objects + triggers) from LIVE actors, then
// serialize it to `scratchSlotName` via GameplayStatics::SaveGameToSlot. The slot name is OURS (a
// transient zcoop_* file the caller deletes after reading), so the player's canonical slot is
// never named, opened, or written: this is not a real save and cannot clobber the host's
// progress. The capture carries the host's own player state (the save object is its live store);
// the joiner replaces the per-player part on its side, before its world is built. It is a gather
// into the host's LIVE save object, during an event too (see the .cpp for what that leaves behind).
// False also when no world gather can be shown to have run: the caller then knows its fallback is
// stale, which a previous gather handed over as live would hide.
//
// Returns true iff the scratch .sav was written. GAME THREAD ONLY -- it calls mainGamemode
// UFunctions + the engine serializer, both ProcessEvent-dispatched.
bool CaptureLiveWorldToScratchSlot(const std::wstring& scratchSlotName);

// ---- the moment the world is gathered ----------------------------------------------------------
//
// mainGamemode::saveObjects does not always gather: while lib_C::getEvent says an event is on, it
// returns without touching `objectsData` (and the two gathers it calls, saveTriggers and Save
// Primitives, skip theirs on the same test), and whatever is serialized next -- the game's own save
// included -- carries the world of the LAST gather. So "a save was written" and "the world in it
// is current" are two different moments, and state that has to stay consistent with the saved
// world (coop/player/player_profile_store) is tied to this one.
//
// The hook is raised when saveObjects is about to gather -- the game's own call, or the capture
// above, which forces the gather past the event gate -- and not when the gate sends saveObjects
// past it. Game thread, inside the saveObjects call.
using WorldGatherFn = void (*)();
void SetWorldGatherHook(WorldGatherFn fn);

// Register the watch both halves stand on (lib_C::getEvent as called by saveObjects). Idempotent;
// false while lib_C or mainGamemode_C is not loaded, or the script gate did not install. The
// capture calls it itself; a consumer of the hook calls it at world-up so the game's own saves are
// seen from the first one. Game thread.
bool InstallGatherWatch();

// Does the game count an event as running right now (lib_C::getEvent)? Three answers, because
// "cannot be asked" is not "no": a caller that would act on "no event" must not act on Unknown.
// Independent of the watch above. Game thread.
enum class EventState { Unknown, Off, On };
EventState GameEventState();

}  // namespace ue_wrap::save_capture
