// coop/dev/inventory_pickup_drill.h -- dev-only: a CLIENT pockets one loose prop through
// the game's own pickup verb, once (env VOTVCOOP_INV_PICKUP_DRILL=1, off by default).
//
// The per-player profile is only proven by an item that is the client's ALONE: a joiner that
// carries nothing looks the same whether the profile works or the slot was merely emptied. This
// gives the client one carried record the host never had, so a rejoin has something to return.
// It moves nobody: mainPlayer::putObjectInventory2 has no reach test, so a plain collectable
// anywhere in the world is pocketed from where the player stands.

#pragma once

namespace coop::dev::inventory_pickup_drill {

// Fire once, ~20 s after the local player is up on a client with the env set. Game thread.
void Tick();

}  // namespace coop::dev::inventory_pickup_drill
