// coop/session/rig_ready.h -- the one log line a test rig waits on.
//
// A rig that waits a fixed number of seconds for a peer wastes the run when the peer is ready
// sooner and measures nothing when it is not. So each peer says when it has reached a milestone,
// and the rig waits on the saying. Every other log line may be reworded; the text of this one is a
// contract with tools outside the tree:
//
//   rig: READY <milestone>[ slot=<n>]
//
//   hosting            the host's session is up, in a loaded world
//   world-ready        this client announced ClientWorldReady: its world is up, its registry is
//                      coherent and the load's tail has settled
//   peer-world-ready   the host took a client's ClientWorldReady and is replaying to slot <n>
//
// A milestone is said every time it is reached: a client says `world-ready` again after a world
// change, the host says `peer-world-ready` for every join and rejoin.

#pragma once

#include "ue_wrap/core/log.h"

namespace coop::rig_ready {

inline void Say(const char* milestone) { UE_LOGI("rig: READY %s", milestone); }
inline void Say(const char* milestone, int slot) { UE_LOGI("rig: READY %s slot=%d", milestone, slot); }

}  // namespace coop::rig_ready
