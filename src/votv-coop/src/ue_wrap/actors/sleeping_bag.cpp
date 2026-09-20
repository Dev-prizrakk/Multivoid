// ue_wrap/actors/sleeping_bag.cpp -- see ue_wrap/actors/sleeping_bag.h.

#include "ue_wrap/actors/sleeping_bag.h"

#include "ue_wrap/core/reflection.h"

#include <string>

namespace ue_wrap::sleeping_bag {

bool IsSleepingBagClass(void* actorClass) {
    if (!actorClass) return false;
    const std::wstring cls = reflection::ToString(reflection::NameOf(actorClass));
    return cls == L"prop_sleepingbag_C" ||
           cls == L"prop_sleepingbag_blue_C" ||
           cls == L"prop_sleepingbag_brown_C" ||
           cls == L"prop_sleepingbag_mattress_C" ||
           cls == L"prop_sleepingbag_stolas_C" ||
           cls == L"sleepingbagWrap_C" ||
           cls == L"sleepingbagWrap_blue_C" ||
           cls == L"sleepingbagWrap_brown_C" ||
           cls == L"sleepingbagWrap_mattress_C" ||
           cls == L"sleepingbagWrap_stolas_C" ||
           cls == L"ragdoll_mattress_C";
}

bool IsSleepingBagForm(void* actor) {
    return actor && IsSleepingBagClass(reflection::ClassOf(actor));
}

}  // namespace ue_wrap::sleeping_bag
