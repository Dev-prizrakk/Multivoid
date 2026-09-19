// ue_wrap/devices/cremator.h -- engine access for the CREMATOR (the trash incinerator that makes
// fuel): the machine actor (/Game/objects/cremator, class cremator_C) and the identity anchor for
// its hatch door. Principle-7 wrapper layer: no network or coop state.
//
// The door (prop_swinger_crematorDoor_C, a prop_swinger descendant) needs no state wrapper: the
// container lane drives it through ue_wrap::swinger's inherited `opened` offset and Open/Close
// verbs. What it needs is a cross-peer NAME: it is spawned by the cremator at BeginPlay,
// top-level and keyless, so the generic identity rule returns "" for it (coop/element/
// portable_identity.h names this exact actor as the residual case) and the container index never
// held it -- the door state that diverged. The name this wrapper enables is the ANCHORED one,
// composed coop-side: the door of the nearest cremator, after the cremator's own portable
// identity, plus "/crematorDoor". This wrapper resolves the two classes and caches the instances.
//
// The burn machinery (the lever's crematorAction, the fireball, the fuel gauge) is NOT synced
// here -- the converging door state is what the lever's own "hatch sealed" check reads.

#pragma once

#include <cstdint>
#include <vector>

namespace ue_wrap::cremator {

// Resolve cremator_C + prop_swinger_crematorDoor_C. Idempotent; true once both are found. Game
// thread.
bool EnsureResolved();

// True iff `obj`'s class is prop_swinger_crematorDoor_C or a subclass. False if not resolved.
bool IsCrematorDoor(void* obj);

// True iff `obj`'s class is cremator_C or a subclass.
bool IsCremator(void* obj);

// The live cremator instances, in GUObjectArray order. The cache is world-generation-stamped and
// rebuilt when the generation changes or a cached actor dies; the scan itself is one
// GUObjectArray pass (the game has a small fixed number of cremators -- one at the base). Game
// thread.
const std::vector<void*>& Instances();

}  // namespace ue_wrap::cremator
