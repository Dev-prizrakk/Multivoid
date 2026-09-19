// coop/player/puppet_carry_drive.cpp -- see coop/player/puppet_carry_drive.h.

#include "coop/player/puppet_carry_drive.h"

#include "coop/net/protocol.h"       // TrashClumpPoseSnapshot (the carry pose batch entry)
#include "coop/net/session.h"        // PublishTrashCarryPose (host publish)
#include "coop/player/players_registry.h"
#include "coop/player/remote_player.h"
#include "coop/props/trash_channel.h"     // IsCarrying / HasPendingSettle / CtxForEid (carry latch + stamp)
#include "ue_wrap/engine/engine.h"          // GetActorLocation / GetActorRotation
#include "ue_wrap/engine/engine_component.h"   // SetComponentTickEnabled (the handle's own tick)
#include "ue_wrap/engine/engine_mainplayer.h"  // ReadMainPlayerGrabHandle / SetPhysicsHandleTarget
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/reflection.h"      // IsLiveByIndex / InternalIndexOf
#include "ue_wrap/core/types.h"           // FVector / FRotator / NormalizeAxis

#include <cmath>
#include <cstdint>
#include <vector>

namespace coop::puppet_carry_drive {
namespace {

namespace R = ue_wrap::reflection;
namespace E = ue_wrap::engine;

// grabLen reads frozen at 150 on the puppet -- the BP-authored hold distance. We use it directly,
// because the puppet's grabLen Timeline never advances and there is no live value to read.
constexpr float kGrabLenCm = 150.f;

struct PuppetHeld {
    uint32_t eid   = 0;        // the trash entity
    uint8_t  slot  = 0;        // the peer whose puppet holds it
    void*    clump = nullptr;  // the held garbageClump actor (cross-tick: validate via clumpIdx)
    int32_t  clumpIdx = -1;    // GUObjectArray index captured at NotePuppetHeld (IsLiveByIndex)
    // FLIGHT (set by NoteThrown at a client's ThrowIntent): the throw released the puppet's grab + applied
    // physics velocity, so the host STOPS hand-driving and the clump flies free. We keep streaming its
    // (physics) pose each tick so every client renders the throw ARC, until the clump re-piles (the latch
    // closes / a settle commits) -> the entry is dropped + the ToPile convert lands the pile.
    bool     flying = false;
    // How far the clump trails the hold point, worst case this carry (cm). The hold is the game's own
    // physics handle, a force-limited spring: a free carry trails by a few centimetres, and a clump
    // pressed against something heavy trails by as much as the player pushes -- which is the point.
    float    maxLagCm = 0.f;
};

std::vector<PuppetHeld> g_held;  // GT-only; at most one per peer (carry AND flight, distinguished by `flying`)

}  // namespace

void NotePuppetHeld(coop::element::ElementId eid, uint8_t slot, void* clump) {
    if (eid == 0u || eid == coop::element::kInvalidId || !clump) return;
    const uint32_t e = static_cast<uint32_t>(eid);
    for (auto& h : g_held) {                       // idempotent: a re-register updates in place
        if (h.eid == e) {
            h.slot = slot; h.clump = clump; h.clumpIdx = R::InternalIndexOf(clump); h.flying = false;
            UE_LOGI("[PUPPET-DRIVE] re-note eid=%u slot=%u clump=%p", e, slot, clump);
            return;
        }
    }
    g_held.push_back(PuppetHeld{e, slot, clump, R::InternalIndexOf(clump), false});
    // The handle moves its hold toward the target in its own component tick; a puppet's actor tick
    // is off, and nothing says its components' are on.
    coop::RemotePlayer* rp = coop::players::Registry::Get().Puppet(slot);
    void* phc = (rp && rp->valid()) ? E::ReadMainPlayerGrabHandle(rp->GetActor()) : nullptr;
    const bool ticking = phc && E::SetComponentTickEnabled(phc, true);
    UE_LOGI("[PUPPET-DRIVE] NOTE eid=%u slot=%u clump=%p handle=%p ticking=%d -- the host advances the "
            "puppet's handle target each tick (the puppet's own tick, which does it natively, is off)",
            e, slot, clump, phc, ticking ? 1 : 0);
}

void NoteThrown(coop::element::ElementId eid) {
    const uint32_t e = static_cast<uint32_t>(eid);
    for (auto& h : g_held) {
        if (h.eid == e) {
            h.flying = true;   // stop hand-driving; keep streaming the physics flight pose until the re-pile
            UE_LOGI("[PUPPET-DRIVE] THROWN eid=%u slot=%u -- hand-drive OFF, flight pose stream ON "
                    "(physics owns the arc; re-pile ends it)", e, h.slot);
            return;
        }
    }
}

void Tick(coop::net::Session& s) {
    static thread_local int sTick = 0;
    ++sTick;
    for (auto it = g_held.begin(); it != g_held.end(); ) {
        const coop::element::ElementId E = static_cast<coop::element::ElementId>(it->eid);
        // Guard 1 (FIRST): the carry latch closed = a re-pile land COMMIT (TickCarry ran BEFORE this in
        // TickGameplay + already cleared g_heldBy). The clump became a pile -> stop following, no release.
        if (!coop::trash_channel::IsCarrying(E)) {
            UE_LOGI("[PUPPET-DRIVE] eid=%u slot=%u -- carry latch closed (landed) -> drive OFF", it->eid, it->slot);
            it = g_held.erase(it);
            continue;
        }
        // Guard 2: the clump still live? (cross-tick cached pointer -> IsLiveByIndex, never bare IsLive.) The
        // re-pile DESTROYS the clump (K2_DestroyActor(self)) while a land-settle is pending (g_carry still
        // open for kLandSettleTicks) -- that is a LANDING, NOT a lost clump: erase WITHOUT releasing so the
        // settle can COMMIT the ToPile. Only a clump gone with NO pending settle is a genuine loss: release
        // the hold there so the eid is re-grabbable, or g_heldBy strands it un-grabbable.
        if (!R::IsLiveByIndex(it->clump, it->clumpIdx)) {
            if (coop::trash_channel::HasPendingSettle(E)) {
                UE_LOGI("[PUPPET-DRIVE] eid=%u slot=%u -- clump consumed by a re-pile (settle pending) -> drive OFF (land commits)",
                        it->eid, it->slot);
            } else {
                UE_LOGI("[PUPPET-DRIVE] eid=%u slot=%u -- clump gone with NO land -> drive OFF + release hold", it->eid, it->slot);
                coop::trash_channel::ReleaseClientHold(s, E);
            }
            it = g_held.erase(it);
            continue;
        }
        if (!it->flying) {
            // Guard 3 (hand-drive only): the puppet still live? A not-live puppet without a full disconnect
            // would strand the hold -- release it. (During flight the clump is independent of the puppet.)
            coop::RemotePlayer* rp = coop::players::Registry::Get().Puppet(it->slot);
            if (!rp || !rp->valid()) {
                UE_LOGI("[PUPPET-DRIVE] eid=%u slot=%u -- puppet gone -> drive OFF + release hold", it->eid, it->slot);
                coop::trash_channel::ReleaseClientHold(s, E);
                it = g_held.erase(it);
                continue;
            }
            // The hold point: head + synced aim * grabLen, where the remote player looks. It goes to the
            // puppet's OWN physics handle as its target, the call the player's tick makes natively
            // (mainPlayer: grabHandle->SetTargetLocationAndRotation(camera + forward * grabLen)). The
            // clump stays in the solver, held by the handle's force-limited spring, so what it pushes
            // against pushes back: a light clump is stopped by a heavy box. Writing the clump's location
            // instead made it a kinematic collider, which shoves any dynamic body with no force limit.
            const ue_wrap::FVector head = rp->GetHeadPosition();
            const ue_wrap::FVector fwd  = rp->GetSyncedAimDirection();
            const ue_wrap::FVector hold{ head.X + fwd.X * kGrabLenCm,
                                         head.Y + fwd.Y * kGrabLenCm,
                                         head.Z + fwd.Z * kGrabLenCm };
            const float yaw   = std::atan2(fwd.Y, fwd.X) * 57.29578f;
            const float pitch = std::atan2(fwd.Z, std::sqrt(fwd.X * fwd.X + fwd.Y * fwd.Y)) * 57.29578f;
            E::SetPhysicsHandleTarget(E::ReadMainPlayerGrabHandle(rp->GetActor()), hold,
                                      ue_wrap::FRotator{pitch, yaw, 0.f});
            const ue_wrap::FVector cur = E::GetActorLocation(it->clump);
            const float lx = cur.X - hold.X, ly = cur.Y - hold.Y, lz = cur.Z - hold.Z;
            const float lag = std::sqrt(lx * lx + ly * ly + lz * lz);
            if (lag > it->maxLagCm) it->maxLagCm = lag;
        }
        // STREAM the clump's CURRENT pose (hand pos when carrying, physics pos when flying) to ALL peers so
        // every client renders the carry + the throw arc. Host-authoritative + host-originated (the relay
        // can't echo to the grabber; a client drives only slot 0). eid+ctx keyed -> the receiver's per-eid
        // ActiveDrive interp; ctx is the carry generation (stale-pose guard on the client). A clump a
        // player moved, carried or thrown, goes ahead of the ones a broom set rolling.
        if (s.TrashCarryPoseTurn(it->eid, /*ahead=*/true) != coop::net::PoseTurn::Wait) {
            const ue_wrap::FVector  loc = E::GetActorLocation(it->clump);
            const ue_wrap::FRotator rot = E::GetActorRotation(it->clump);
            coop::net::TrashClumpPoseSnapshot snap{};
            snap.eid   = it->eid;
            snap.x = loc.X; snap.y = loc.Y; snap.z = loc.Z;
            snap.pitch = ue_wrap::NormalizeAxis(rot.Pitch);
            snap.yaw   = ue_wrap::NormalizeAxis(rot.Yaw);
            snap.roll  = ue_wrap::NormalizeAxis(rot.Roll);
            snap.ctx   = coop::trash_channel::CtxForEid(E);
            s.PublishTrashCarryPose(snap, /*ahead=*/true);
            if ((sTick % 60) == 0)
                UE_LOGI("[TRASH-CARRY] HOST PUBLISH eid=%u slot=%u %s -> (%.1f,%.1f,%.1f) ctx=%u maxLagCm=%.1f",
                        it->eid, it->slot, it->flying ? "FLIGHT" : "carry", loc.X, loc.Y, loc.Z,
                        static_cast<unsigned>(snap.ctx), it->maxLagCm);
        }
        ++it;
    }
}

void OnTakenOver(coop::element::ElementId eid) {
    const uint32_t e = static_cast<uint32_t>(eid);
    for (auto it = g_held.begin(); it != g_held.end(); ++it) {
        if (it->eid != e) continue;
        UE_LOGI("[PUPPET-DRIVE] eid=%u slot=%u -- taken over by the host's hand or a broom -> drive OFF (the "
                "entity lives on in the taker's clump)", it->eid, it->slot);
        g_held.erase(it);
        return;
    }
}

void OnPeerLeft(uint8_t slot) {
    for (auto it = g_held.begin(); it != g_held.end(); ) {
        if (it->slot == slot) {
            UE_LOGI("[PUPPET-DRIVE] OnPeerLeft slot=%u -- dropping held eid=%u", slot, it->eid);
            it = g_held.erase(it);
        } else { ++it; }
    }
}

void OnDisconnect() {
    g_held.clear();
}

}  // namespace coop::puppet_carry_drive
