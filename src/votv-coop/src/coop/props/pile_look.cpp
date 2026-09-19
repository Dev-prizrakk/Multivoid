// coop/props/pile_look.cpp -- see pile_look.h.

#include "coop/props/pile_look.h"

#include "coop/element/element.h"
#include "coop/element/registry.h"
#include "ue_wrap/actors/chip_pile.h"
#include "ue_wrap/core/hot_path_guard.h"  // UE_ASSERT_GAME_THREAD
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/reflection.h"
#include "ue_wrap/core/types.h"

#include <cmath>
#include <unordered_map>

namespace coop::pile_look {
namespace {

namespace R  = ue_wrap::reflection;
namespace CP = ue_wrap::chip_pile;

constexpr float kAngleSteps = 65536.f / 360.f;
constexpr float kScaleSteps = 4096.f;

std::unordered_map<uint32_t, coop::net::WirePileLook> g_looks;   // eid -> the host's look
int g_applied = 0;                                                // throttles the log

int16_t PackAngle(float deg) {
    // Wraps by construction: 360 degrees is 65536 steps, and the cast keeps the low 16 bits.
    return static_cast<int16_t>(static_cast<uint16_t>(static_cast<int32_t>(std::lround(deg * kAngleSteps)) & 0xFFFF));
}
float UnpackAngle(int16_t q) { return static_cast<float>(q) / kAngleSteps; }

uint16_t PackScale(float s) {
    const long q = std::lround(s * kScaleSteps);
    return static_cast<uint16_t>(q < 1 ? 1 : (q > 65535 ? 65535 : q));
}
float UnpackScale(uint16_t q) { return static_cast<float>(q) / kScaleSteps; }

bool SameLook(const coop::net::WirePileLook& a, const coop::net::WirePileLook& b) {
    return a.relPitch == b.relPitch && a.relYaw == b.relYaw && a.relRoll == b.relRoll &&
           a.sclX == b.sclX && a.sclY == b.sclY && a.sclZ == b.sclZ;
}

void Apply(uint32_t eid, void* actor, const coop::net::WirePileLook& w) {
    // A pile that already shows this look is left alone: a re-bracket re-expresses every pile, and a
    // mobility flip rebuilds the component's render and physics state.
    if (SameLook(Capture(actor), w)) return;
    CP::Look look;
    look.relRotation = ue_wrap::FRotator{UnpackAngle(w.relPitch), UnpackAngle(w.relYaw), UnpackAngle(w.relRoll)};
    look.relScale    = ue_wrap::FVector{UnpackScale(w.sclX), UnpackScale(w.sclY), UnpackScale(w.sclZ)};
    const bool ok = CP::ApplyLook(actor, look);
    ++g_applied;
    if (!ok)
        UE_LOGW("[PILE] pile_look: eid=%u actor=%p -- the look did NOT apply (no visible mesh, or the scene "
                "component's verbs did not resolve)", eid, actor);
    else if (g_applied <= 8 || (g_applied % 500) == 0)
        UE_LOGI("[PILE] pile_look: #%d eid=%u actor=%p takes the host's look rot=(%.1f,%.1f,%.1f) scale=(%.2f,%.2f,%.2f)",
                g_applied, eid, actor, look.relRotation.Pitch, look.relRotation.Yaw, look.relRotation.Roll,
                look.relScale.X, look.relScale.Y, look.relScale.Z);
}

}  // namespace

coop::net::WirePileLook Capture(void* actor) {
    coop::net::WirePileLook w{};
    CP::Look look;
    if (!CP::ReadLook(actor, look)) return w;
    w.relPitch = PackAngle(look.relRotation.Pitch);
    w.relYaw   = PackAngle(look.relRotation.Yaw);
    w.relRoll  = PackAngle(look.relRotation.Roll);
    w.sclX = PackScale(look.relScale.X);
    w.sclY = PackScale(look.relScale.Y);
    w.sclZ = PackScale(look.relScale.Z);
    return w;
}

void OnHostLook(uint32_t eid, const coop::net::WirePileLook& look, int senderSlot) {
    UE_ASSERT_GAME_THREAD("pile_look::OnHostLook");
    if (senderSlot != 0 || eid == 0 || look.sclX == 0) return;
    g_looks[eid] = look;
    coop::element::Element* el = coop::element::Registry::Get().Get(static_cast<coop::element::ElementId>(eid));
    void* actor = el ? el->GetActor() : nullptr;
    // IsLiveByIndex: the cached actor is engine-owned, so only its GUObjectArray slot is read.
    if (actor && R::IsLiveByIndex(actor, el->GetInternalIdx()) && CP::VisibleMesh(actor))
        Apply(eid, actor, look);   // a clump holding E has no such mesh: the look waits for the pile
}

void OnBound(uint32_t eid, void* actor) {
    UE_ASSERT_GAME_THREAD("pile_look::OnBound");
    if (g_looks.empty() || !actor) return;
    auto it = g_looks.find(eid);
    if (it == g_looks.end()) return;
    if (!CP::VisibleMesh(actor)) return;   // the eid's clump form: the look waits for the pile
    Apply(eid, actor, it->second);
}

void LogLandLook(const char* who, uint32_t eid, void* actor) {
    CP::Look look;
    if (!CP::ReadLook(actor, look)) {
        UE_LOGW("[PILE] pile_look: %s LAND eid=%u actor=%p -- no look to read", who, eid, actor);
        return;
    }
    UE_LOGI("[PILE] pile_look: %s LAND eid=%u rot=(%.2f,%.2f,%.2f) scale=(%.3f,%.3f,%.3f)", who, eid,
            look.relRotation.Pitch, look.relRotation.Yaw, look.relRotation.Roll,
            look.relScale.X, look.relScale.Y, look.relScale.Z);
}

void Forget(uint32_t eid) {
    UE_ASSERT_GAME_THREAD("pile_look::Forget");
    g_looks.erase(eid);
}

void OnDisconnect() {
    UE_ASSERT_GAME_THREAD("pile_look::OnDisconnect");
    if (!g_looks.empty())
        UE_LOGI("[PILE] pile_look: OnDisconnect dropped %zu kept look(s), %d applied this session",
                g_looks.size(), g_applied);
    g_looks.clear();
    g_applied = 0;
}

}  // namespace coop::pile_look
