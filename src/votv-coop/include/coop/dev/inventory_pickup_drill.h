// coop/dev/inventory_pickup_drill.h -- dev-only: a CLIENT pockets loose props through the game's
// own pickup verb, once (env VOTVCOOP_INV_PICKUP_DRILL=1, off by default).
//
// The per-player profile is only proven by an item that is the client's ALONE: a joiner that
// carries nothing looks the same whether the profile works or the slot was merely emptied. This
// gives the client carried records the host never had, so a rejoin has something to return: a
// plain item, a drive holding a signal and a disc holding data. The last two are there because a
// carried record's payload SHAPE survives a rejoin whether or not its VALUES do; with
// live_store_readout=1 the readout's `values` lines, read under the same key in both lives, are
// the comparison. The host's half of the drill makes sure the world holds such a drive and disc,
// and with VOTVCOOP_INV_PICKUP_DRILL_SAVE=1 also saves its world once a minute, which is the only
// moment a host writes profiles to disk (`quick` alternates a quicksave, which writes a new subsave
// file, with a plain save of the main slot). Both peers print a census of the keys that stand in the
// world under the wanted classes, on change: a pickup is a destroy everywhere else, so a key that
// comes back is a duplicate caught where it is born.
// The pickup moves nobody: mainPlayer::putObjectInventory2 has no reach test. The same run then
// sets food and sleep to numbers no fresh life has and walks the player, by the bot director, to
// a point the NavMesh routes to some way off, so the vitals and the pose of the profile have
// something to return too.

#pragma once

namespace coop::dev::inventory_pickup_drill {

// Fire once, ~20 s after the local player is up with the env set: the host seeds, a client
// pockets. Game thread.
void Tick();

}  // namespace coop::dev::inventory_pickup_drill
