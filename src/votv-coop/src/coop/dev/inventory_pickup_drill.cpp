// coop/dev/inventory_pickup_drill.cpp -- see coop/dev/inventory_pickup_drill.h.

#include "coop/dev/inventory_pickup_drill.h"

#include "coop/config/config.h"
#include "coop/dev/director/director.h"
#include "coop/player/players_registry.h"
#include "coop/player/roster.h"
#include "ue_wrap/actors/inventory.h"
#include "ue_wrap/actors/prop.h"
#include "ue_wrap/actors/vitals.h"
#include "ue_wrap/core/call.h"
#include "ue_wrap/core/game_thread.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/engine/engine.h"
#include "ue_wrap/engine/engine_nav.h"

#include <atomic>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace coop::dev::inventory_pickup_drill {
namespace {

namespace R = ue_wrap::reflection;
namespace E = ue_wrap::engine;

constexpr int kSettleTicks = 2400;  // ~20 s at ~120 Hz: the join replay has drained by then
constexpr float kDrillFood = 37.f, kDrillSleep = 61.f;
constexpr int kMaxTries    = 8;     // a full inventory refuses everything: do not dispatch per prop in the world

// The walk: the director's route over the NavMesh to a reachable point some way off, so the pose
// the profile records is somewhere the start point is not. A worker thread, since the director's
// Run blocks; every engine touch inside it is posted to the game thread by the director itself.
DWORD WINAPI WalkAwayThread(LPVOID /*arg*/) {
    namespace D = coop::director;
    auto goal = std::make_shared<D::DirectorGoal>();
    auto picked = std::make_shared<std::atomic<int>>(0);
    ue_wrap::game_thread::Post([goal, picked] {
        // A point the NavMesh can route to, eight ways round at 15 m: the route's own last point
        // is on the mesh by construction, which a computed offset is not. No pile is needed, so
        // the walk works from the start point as well as from the base.
        void* player = coop::players::Registry::Get().Local();
        if (!player) { picked->store(-1); return; }
        const ue_wrap::FVector me = E::GetActorLocation(player);
        for (int k = 0; k < 8; ++k) {
            const float a = static_cast<float>(k) * 0.785398f;
            const ue_wrap::FVector want{me.X + 1500.f * std::cos(a), me.Y + 1500.f * std::sin(a), me.Z};
            std::vector<ue_wrap::FVector> route;
            if (!E::FindNavPath(player, me, want, route) || route.size() < 2) continue;
            const ue_wrap::FVector end = route.back();
            const float dx = end.X - me.X, dy = end.Y - me.Y;
            if (dx * dx + dy * dy < 800.f * 800.f) continue;  // a route that ends at our feet
            goal->targetPos = end;
            picked->store(1);
            return;
        }
        picked->store(-1);
    });
    for (int waited = 0; picked->load() == 0 && waited < 4000; waited += 5) ::Sleep(5);
    if (picked->load() != 1) {
        UE_LOGW("[INV-PICKUP-DRILL] no route of 8 m or more from here -- the player stays put");
        return 0;
    }
    goal->reachCm = 200.f;
    D::ControlManager mgr;
    D::AddWalkToProcesses(mgr, *goal);
    mgr.Run(*goal, /*maxSeconds=*/60);
    ue_wrap::game_thread::Post([goal] {
        void* player = coop::players::Registry::Get().Local();
        if (!player) return;
        const ue_wrap::FVector at = E::GetActorLocation(player);
        UE_LOGI("[INV-PICKUP-DRILL] walked (%hs) -- the player now stands at (%.0f, %.0f, %.0f) yaw=%.0f",
                goal->reached ? "reached" : goal->failReason, at.X, at.Y, at.Z,
                E::GetActorRotation(player).Yaw);
    });
    return 0;
}

void PocketOneProp(void* player);

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

    PocketOneProp(player);

    // Vitals no fresh life and no host has: what a rejoin restores must be THESE numbers.
    namespace V = ue_wrap::vitals;
    const bool wrote = V::Write(V::Field::Food, kDrillFood) && V::Write(V::Field::Sleep, kDrillSleep);
    UE_LOGI("[INV-PICKUP-DRILL] vitals set to food=%.0f sleep=%.0f -> %hs", kDrillFood, kDrillSleep,
            wrote ? "written" : "WRITE FAILED");

    if (HANDLE t = ::CreateThread(nullptr, 0, &WalkAwayThread, nullptr, 0, nullptr)) ::CloseHandle(t);
}

namespace {

void PocketOneProp(void* player) {
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

}  // namespace

}  // namespace coop::dev::inventory_pickup_drill
