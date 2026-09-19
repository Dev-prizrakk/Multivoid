// coop/dev/inventory_pickup_drill.h -- dev-only: a CLIENT pockets one loose prop through
// the game's own pickup verb, once (env VOTVCOOP_INV_PICKUP_DRILL=1, off by default).
//
// The per-player profile is only proven by an item that is the client's ALONE: a joiner that
// carries nothing looks the same whether the profile works or the slot was merely emptied. This
// gives the client one carried record the host never had, so a rejoin has something to return.
// The pickup moves nobody: mainPlayer::putObjectInventory2 has no reach test. The same run then
// sets food and sleep to numbers no fresh life has and walks the player, by the bot director, to
// a point the NavMesh routes to some way off, so the vitals and the pose of the profile have something to return too.

#pragma once

namespace coop::dev::inventory_pickup_drill {

// Fire once, ~20 s after the local player is up on a client with the env set. Game thread.
void Tick();

}  // namespace coop::dev::inventory_pickup_drill
