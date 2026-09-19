// coop/dev/inventory_pickup_drill.cpp -- see coop/dev/inventory_pickup_drill.h.

#include "coop/dev/inventory_pickup_drill.h"

#include "coop/config/config.h"
#include "coop/player/players_registry.h"
#include "coop/player/roster.h"
#include "ue_wrap/actors/inventory.h"
#include "ue_wrap/actors/prop.h"
#include "ue_wrap/core/call.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/engine/engine.h"

#include <cmath>
#include <string>

namespace coop::dev::inventory_pickup_drill {
namespace {

namespace R = ue_wrap::reflection;
namespace E = ue_wrap::engine;

constexpr int kSettleTicks = 2400;  // ~20 s at ~120 Hz: the join replay has drained by then
constexpr int kMaxTries    = 8;     // a full inventory refuses everything: do not dispatch per prop in the world

}  // namespace

void Tick() {
    static const bool s_on = ::coop::config::ReadEnv("VOTVCOOP_INV_PICKUP_DRILL") == "1";
    if (!s_on) return;
    static bool s_done = false;
    if (s_done) return;

    if (coop::roster::LocalIsHost()) return;  // the client's item is the one under test
    void* player = coop::players::Registry::Get().Local();
    if (!player || !R::IsLive(player) || !E::GetController(player)) return;
    static int s_ticks = 0;
    if (++s_ticks < kSettleTicks) return;
    s_done = true;

    void* fn = R::FindFunction(R::ClassOf(player), L"putObjectInventory2");
    if (!fn) {
        UE_LOGE("[INV-PICKUP-DRILL] putObjectInventory2 did not resolve -- drill aborted");
        return;
    }
    // What is carried already, by key: a prop that stands in the world under a carried key is a
    // duplicate of a pocketed item, and pocketing it again would hide exactly that.
    ue_wrap::inventory::PlayerInventory mine;
    ue_wrap::inventory::ReadAll(mine);
    auto carriedKey = [&mine](const std::wstring& key) {
        for (const auto& r : mine.inventory) if (r.key == key) return true;
        return false;
    };
    const ue_wrap::FVector me = E::GetActorLocation(player);

    // Plain collectables, tried in order until the game accepts one. The verb refuses by class
    // (a nested container, a heavy prop, a filtered class), so "the nearest prop" is the wrong
    // pick: measured, it was a suitcase, then a hook, and both came back false. One class walk
    // per candidate class and at most kMaxTries dispatches, once per process.
    int tries = 0;
    for (const wchar_t* cls : {L"prop_food_C", L"prop_drive_C", L"prop_crowbar_C", L"prop_cup_C",
                               L"prop_battery_C"}) {
        for (void* prop : R::FindObjectsByClass(cls)) {
            if (!prop || !R::IsLive(prop)) continue;
            const std::wstring key = ue_wrap::prop::GetInteractableKeyString(prop);
            // Where the candidate sits: at the player's feet it was ejected from an inventory, at
            // a map location it came with the world.
            const ue_wrap::FVector at = E::GetActorLocation(prop);
            const float dx = at.X - me.X, dy = at.Y - me.Y, dz = at.Z - me.Z;
            const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (carriedKey(key)) {
                UE_LOGW("[INV-PICKUP-DRILL] DUPLICATE IN THE WORLD: '%ls' key='%ls' stands at "
                        "(%.0f, %.0f, %.0f), %.0f cm away, and that key is already carried -- "
                        "left alone", cls, key.c_str(), at.X, at.Y, at.Z, dist);
                continue;
            }
            if (++tries > kMaxTries) {
                UE_LOGW("[INV-PICKUP-DRILL] %d candidates refused -- giving up", kMaxTries);
                return;
            }
            ue_wrap::ParamFrame f(fn);
            f.Set(L"InputPin", prop);
            f.Set(L"noNotify", true);
            const bool called = ue_wrap::Call(player, f);
            const bool took = called && f.Get<bool>(L"return");
            UE_LOGI("[INV-PICKUP-DRILL] putObjectInventory2('%ls' key='%ls' at (%.0f, %.0f, %.0f), "
                    "%.0f cm away) -> %hs", cls, key.c_str(), at.X, at.Y, at.Z, dist,
                    !called ? "CALL FAILED" : took ? "POCKETED" : "refused");
            if (took) return;
        }
    }
    UE_LOGW("[INV-PICKUP-DRILL] no candidate was accepted -- nothing pocketed");
}

}  // namespace coop::dev::inventory_pickup_drill
