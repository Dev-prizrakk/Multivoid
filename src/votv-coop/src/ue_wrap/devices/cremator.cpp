// ue_wrap/devices/cremator.cpp -- see ue_wrap/devices/cremator.h. The two class resolves and the
// world-stamped instance cache; no member offsets (the door's state and verbs are the swinger
// family's own, inherited).

#include "ue_wrap/devices/cremator.h"

#include "ue_wrap/engine/world_identity.h"  // Generation -- the cache's world stamp
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/reflection.h"

#include <atomic>

namespace ue_wrap::cremator {
namespace {

namespace R = reflection;

std::atomic<bool> g_resolved{false};

void* g_crematorCls    = nullptr;  // cremator_C UClass (the machine)
void* g_doorCls        = nullptr;  // prop_swinger_crematorDoor_C UClass (the hatch door)

// The instance cache. Rebuilt when the world generation changes (a load/new world retires every
// cached actor) or when a cached actor has died; otherwise the scan cost is one pass per world.
std::vector<void*> g_instances;
std::vector<int32_t> g_instanceIdx;  // InternalIndexOf per instance, for cheap liveness
uint32_t g_cacheGen = 0;
bool     g_cacheValid = false;

bool CacheEntryLive(size_t i) {
    return R::IsLiveByIndex(g_instances[i], g_instanceIdx[i]);
}

}  // namespace

bool EnsureResolved() {
    if (g_resolved.load(std::memory_order_acquire)) return true;
    void* crematorCls = R::FindClass(L"cremator_C");
    if (!crematorCls) return false;  // the sublevel that holds the cremator has not loaded yet
    void* doorCls = R::FindClass(L"prop_swinger_crematorDoor_C");
    if (!doorCls) {
        UE_LOGW("cremator: cremator_C resolved but prop_swinger_crematorDoor_C did not -- "
                "the door keying stays inert until it loads");
        return false;
    }
    g_crematorCls = crematorCls;
    g_doorCls = doorCls;
    g_resolved.store(true, std::memory_order_release);
    UE_LOGI("cremator: resolved cremator_C=%p prop_swinger_crematorDoor_C=%p "
            "(the door syncs through the swinger family; this is its identity anchor)",
            crematorCls, doorCls);
    return true;
}

bool IsCrematorDoor(void* obj) {
    if (!obj || !g_doorCls) return false;
    void* cls = R::ClassOf(obj);
    if (!cls) return false;
    void* bases[1] = { g_doorCls };
    return R::IsDescendantOfAny(cls, bases, 1);
}

bool IsCremator(void* obj) {
    if (!obj || !g_crematorCls) return false;
    void* cls = R::ClassOf(obj);
    if (!cls) return false;
    void* bases[1] = { g_crematorCls };
    return R::IsDescendantOfAny(cls, bases, 1);
}

const std::vector<void*>& Instances() {
    const uint32_t gen = world_identity::Generation();
    if (g_cacheValid && g_cacheGen == gen) {
        // A dead entry (a torn-down world the generation compare alone did not catch, or a
        // destroyed cremator) prunes the cache; an empty-but-valid cache stays (the scan found
        // nothing this world and must not rescan every call).
        for (size_t i = 0; i < g_instances.size();) {
            if (CacheEntryLive(i)) { ++i; continue; }
            g_instances.erase(g_instances.begin() + static_cast<long>(i));
            g_instanceIdx.erase(g_instanceIdx.begin() + static_cast<long>(i));
        }
        return g_instances;
    }
    g_instances.clear();
    g_instanceIdx.clear();
    if (g_crematorCls) {
        const int32_t n = R::NumObjects();
        for (int32_t i = 0; i < n; ++i) {
            void* obj = R::ObjectAt(i);
            if (!obj || !IsCremator(obj)) continue;
            if (R::NameStartsWith(R::NameOf(obj), L"Default__")) continue;  // the CDO
            if (!R::IsLive(obj)) continue;
            g_instances.push_back(obj);
            g_instanceIdx.push_back(R::InternalIndexOf(obj));
        }
    }
    g_cacheGen = gen;
    g_cacheValid = true;
    if (!g_instances.empty())
        UE_LOGI("cremator: %zu live instance(s) cached (world gen %u)",
                g_instances.size(), gen);
    return g_instances;
}

}  // namespace ue_wrap::cremator
