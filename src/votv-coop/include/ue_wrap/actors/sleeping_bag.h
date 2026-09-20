// ue_wrap/actors/sleeping_bag.h -- class-family identification for the deployable sleeping bags.
//
// A placed bag changes from a sleepingbagWrap_* inventory prop into a prop_sleepingbag_* world
// prop. The Blueprint performs that birth internally, so the coop layer asks this wrapper whether
// the finished actor belongs to that family before admitting it as a client-authored birth.

#pragma once

namespace ue_wrap::sleeping_bag {

// True for either deployable form (the wrapped inventory prop or the laid-out world prop), and
// for the mattress ragdoll the same Blueprint family creates. Pure reflected class-name read.
bool IsSleepingBagForm(void* actor);
bool IsSleepingBagClass(void* actorClass);

}  // namespace ue_wrap::sleeping_bag
