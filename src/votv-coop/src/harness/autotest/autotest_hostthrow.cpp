// harness/autotest/autotest_hostthrow.cpp -- the host-throw scenario (VOTVCOOP_RUN_HOSTTHROW=1):
// the HOST walks to a nav-reachable pile with the bot director, grabs it through the input seam,
// carries it and throws it; the CLIENT samples what its own mirror of that clump does in the air.
// The counterpart of the grab-intent drill's hard throw, where the client throws. Nobody is
// teleported: the director walks (coop/dev/director/director.h says why).

#include "harness/autotest.h"

#include "coop/dev/director/director.h"
#include "coop/player/players_registry.h"
#include "coop/player/remote_player.h"
#include "ue_wrap/actors/prop.h"
#include "ue_wrap/core/game_thread.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/types.h"
#include "ue_wrap/engine/engine.h"
#include "ue_wrap/engine/engine_attach.h"      // SetActorRootPhysicsVelocity
#include "ue_wrap/engine/engine_mainplayer.h"  // ReadMainPlayerGrabState, ReleaseMainPlayerGrabIfHolding

#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>

namespace harness::autotest {
namespace {

namespace R  = ue_wrap::reflection;
namespace E  = ue_wrap::engine;
namespace GT = ue_wrap::game_thread;

template <class Fn>
int RunGT(Fn&& body) {
    auto done = std::make_shared<std::atomic<int>>(0);
    GT::Post([done, body]() mutable { body(*done); });
    while (done->load() == 0) ::Sleep(5);
    return done->load();
}

// The host half: the director's own walked grab, then a throw of what it holds.
void RunHost() {
    UE_LOGI("hostthrow: HOST -- waiting for the client's puppet to go live, then walking to a pile");
    for (int waited = 0; waited < 180; ++waited) {
        const int r = RunGT([](std::atomic<int>& d) {
            coop::RemotePlayer* rp = coop::players::Registry::Get().Puppet(1);
            d.store(rp && rp->valid() ? 1 : 2);
        });
        if (r == 1) break;
        ::Sleep(1000);
    }
    ::Sleep(35000);   // the client expresses its proxies after its puppet appears

    auto player = std::make_shared<void*>(nullptr);
    auto goal   = std::make_shared<coop::director::DirectorGoal>();
    if (RunGT([player, goal](std::atomic<int>& d) {
            void* p = coop::players::Registry::Get().Local();
            if (!p || !R::IsLive(p) || !E::GetController(p)) { d.store(2); return; }
            *player = p;
            d.store(coop::director::PickReachablePile(p, 0.f, 5000.f, *goal) ? 1 : 2);
        }) != 1) {
        UE_LOGW("hostthrow: VERDICT host=NO-PILE -- no possessed player or no nav-reachable pile");
        return;
    }
    coop::director::ControlManager mgr;
    coop::director::AddWalkGrabProcesses(mgr, *goal);
    if (!mgr.Run(*goal, /*maxSeconds=*/90)) {
        UE_LOGW("hostthrow: VERDICT host=NO-GRAB -- the director did not grab (%s)", goal->failReason);
        return;
    }
    UE_LOGI("hostthrow: grabbed; carrying 3 s so the client's mirror follows the hand");
    ::Sleep(3000);

    RunGT([player](std::atomic<int>& d) {
        ue_wrap::engine::MainPlayerGrabState gs{};
        void* clump = nullptr;
        if (E::ReadMainPlayerGrabState(*player, gs) && gs.grabbingActor &&
            ue_wrap::prop::IsGarbageClump(gs.grabbingActor))
            clump = gs.grabbingActor;
        if (!clump) { UE_LOGW("hostthrow: VERDICT host=NO-CLUMP -- nothing in the hand at the throw"); d.store(2); return; }
        const bool rel = E::ReleaseMainPlayerGrabIfHolding(*player, clump);
        // Along the facing, 6 m/s out and 6 m/s up: a second of arc over open floor.
        const ue_wrap::FVector fwd = E::GetActorForwardVector(*player);
        const ue_wrap::FVector lin{ fwd.X * 600.f, fwd.Y * 600.f, 600.f };
        const bool vel = E::SetActorRootPhysicsVelocity(clump, lin, ue_wrap::FVector{0.f, 0.f, 0.f});
        UE_LOGI("hostthrow: THROWN clump=%p released=%d velocitySet=%d vel=(%.0f,%.0f,%.0f)",
                clump, rel ? 1 : 0, vel ? 1 : 0, lin.X, lin.Y, lin.Z);
        d.store(1);
    });
    ::Sleep(7000);   // the flight, the impact, the re-pile and its settle
}

// The client half: follow the clump the host's puppet is carrying. The clump nearest the puppet
// is the carried one; a first-live-clump pick latched onto unrelated litter elsewhere in the world.
void RunClient() {
    UE_LOGI("hostthrow: CLIENT watcher -- sampling the clump nearest the host's puppet at 20 Hz");
    const ULONGLONG tEnd = ::GetTickCount64() + 360000;
    while (::GetTickCount64() < tEnd) {
        auto found = std::make_shared<void*>(nullptr);
        RunGT([found](std::atomic<int>& d) {
            coop::RemotePlayer* rp = coop::players::Registry::Get().Puppet(0);
            void* host = (rp && rp->valid()) ? rp->GetActor() : nullptr;
            if (host) {
                const ue_wrap::FVector hp = E::GetActorLocation(host);
                float best = 400.f * 400.f;   // within arm's length and a bit
                for (void* o : R::FindObjectsByClass(L"prop_garbageClump_C")) {
                    if (!o || !R::IsLive(o)) continue;
                    const ue_wrap::FVector cp = E::GetActorLocation(o);
                    const float dx = cp.X - hp.X, dy = cp.Y - hp.Y, dz = cp.Z - hp.Z;
                    const float d2 = dx * dx + dy * dy + dz * dz;
                    if (d2 < best) { best = d2; *found = o; }
                }
            }
            d.store(1);
        });
        if (!*found) { ::Sleep(500); continue; }
        const ULONGLONG t0 = ::GetTickCount64();
        UE_LOGI("hostthrow: CLIENT watcher -- following clump mirror %p", *found);
        for (;;) {
            auto alive = std::make_shared<bool>(false);
            RunGT([found, alive, t0](std::atomic<int>& d) {
                if (R::IsLive(*found) && ue_wrap::prop::IsGarbageClump(*found)) {
                    *alive = true;
                    const ue_wrap::FVector at = E::GetActorLocation(*found);
                    UE_LOGI("hostthrow: WATCH-SAMPLE t=%llu ms mirror=%p pos=(%.1f,%.1f,%.1f)",
                            ::GetTickCount64() - t0, *found, at.X, at.Y, at.Z);
                }
                d.store(1);
            });
            if (!*alive) break;
            ::Sleep(50);
        }
        UE_LOGI("hostthrow: CLIENT watcher -- clump mirror %p gone after %llu ms (landed or retired)",
                *found, ::GetTickCount64() - t0);
    }
}

}  // namespace

void RunHostThrowScenario() {
    if (IsClientRole()) { RunClient(); return; }
    RunHost();
    UE_LOGI("hostthrow: HOST finished");   // every exit of RunHost ends here: the driver waits on this line
}

DWORD WINAPI HostThrowThread(LPVOID /*arg*/) {
    RunHostThrowScenario();
    return 0;
}

}  // namespace harness::autotest
