// ue_wrap/devices/cargo_lift.cpp -- see ue_wrap/devices/cargo_lift.h.

#include "ue_wrap/devices/cargo_lift.h"

#include "ue_wrap/core/call.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/reflection.h"

#include <atomic>
#include <cstdint>

namespace ue_wrap::cargo_lift {
namespace {

namespace R = reflection;

std::atomic<bool> g_resolved{false};
void*   g_liftCls  = nullptr;
int32_t g_openOff  = -1;
void*   g_acivaeFn = nullptr;

}  // namespace

bool EnsureResolved() {
    if (g_resolved.load(std::memory_order_acquire)) return true;
    void* cls = R::FindClass(L"cargoLift_C");
    if (!cls) return false;

    const int32_t openOff = R::FindPropertyOffset(cls, L"Open");
    void* acivaeFn = R::FindFunction(cls, L"acivae");
    if (openOff < 0 || !acivaeFn) {
        UE_LOGW("cargo_lift: cargoLift_C unresolved members Open@%d acivae=%p -- retrying",
                openOff, acivaeFn);
        return false;
    }

    g_liftCls = cls;
    g_openOff = openOff;
    g_acivaeFn = acivaeFn;
    g_resolved.store(true, std::memory_order_release);
    UE_LOGI("cargo_lift: resolved cargoLift_C=%p Open@0x%04X acivae=%p "
            "(controller edge drives platform + both child doors)", cls, openOff, acivaeFn);
    return true;
}

bool IsCargoLift(void* obj) {
    if (!obj || !g_liftCls) return false;
    void* cls = R::ClassOf(obj);
    void* bases[1] = {g_liftCls};
    return cls && R::IsDescendantOfAny(cls, bases, 1);
}

std::wstring GetNameKey(void* lift) {
    return lift ? R::ToString(R::NameOf(lift)) : std::wstring();
}

bool TryReadOpen(void* lift, bool& open) {
    if (!lift || g_openOff < 0) return false;
    open = *reinterpret_cast<const bool*>(reinterpret_cast<const uint8_t*>(lift) + g_openOff);
    return true;
}

bool ApplyOpen(void* lift, bool open) {
    if (!lift || g_openOff < 0 || !g_acivaeFn) return false;
    bool cur = false;
    if (TryReadOpen(lift, cur) && cur == open) return true;
    *reinterpret_cast<bool*>(reinterpret_cast<uint8_t*>(lift) + g_openOff) = open;
    ParamFrame f(g_acivaeFn);
    return f.valid() && Call(lift, f);
}

}  // namespace ue_wrap::cargo_lift
