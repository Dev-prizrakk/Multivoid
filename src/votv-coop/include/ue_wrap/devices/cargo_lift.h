// ue_wrap/devices/cargo_lift.h -- engine access for the base cargo lift controller.
//
// cargoLift_C owns the activation flag and the native timeline that moves its prop_cargolift_C
// platform and both cargoliftDoor_C child actors. Synchronising this controller edge, rather than
// driving the three children independently, keeps the platform and both doors on one native
// timeline on every peer.

#pragma once

#include <string>

namespace ue_wrap::cargo_lift {

bool EnsureResolved();
bool IsCargoLift(void* obj);
std::wstring GetNameKey(void* lift);
bool TryReadOpen(void* lift, bool& open);
bool ApplyOpen(void* lift, bool open);

}  // namespace ue_wrap::cargo_lift
