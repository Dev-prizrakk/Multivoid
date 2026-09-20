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
// and with VOTVCOOP_INV_PICKUP_DRILL_SAVE=1 also saves its world when a joiner's world comes up,
// which is the only moment a host writes profiles to disk (`quick` makes it a quicksave, which
// writes a new subsave file, then a plain save of the main slot). Nothing here waits on a clock: a
// client acts when its join is over and says `CLIENT LIFE DONE` when its life's work is done, the
// host saves on an arrival and says `HOST SAVES DONE`, and a rig waits on those lines (and on
// coop/session/rig_ready.h's) instead of on seconds. Both peers print a census of the keys that stand in the
// world under the wanted classes, on change: a pickup is a destroy everywhere else, so a key that
// comes back is a duplicate caught where it is born.
// The pickup moves nobody: mainPlayer::putObjectInventory2 has no reach test. The same run then
// sets food and sleep to numbers no fresh life has and walks the player, by the bot director, to
// a point the NavMesh routes to some way off, so the vitals and the pose of the profile have
// something to return too.

#pragma once

namespace coop::net { class Session; }

namespace coop::dev::inventory_pickup_drill {

// Fire once with the env set, on readiness: the host seeds once its player stands, a client
// pockets once its join is over. Game thread.
void Tick(coop::net::Session* session);

}  // namespace coop::dev::inventory_pickup_drill
