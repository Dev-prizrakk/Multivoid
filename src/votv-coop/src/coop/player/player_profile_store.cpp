// coop/player/player_profile_store.cpp -- see coop/player/player_profile_store.h.

#include "coop/player/player_profile_store.h"

#include "coop/net/blob_chunks.h"  // Fnv64, the file's integrity hash
#include "coop/save/save_transfer.h"  // HostSlot, the world the profiles belong to
#include "coop/session/player_handshake.h"  // IsValidGuid
#include "coop/text/utf8_codec.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/core/paths.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>
#include <unordered_map>
#include <unordered_set>

namespace coop::player_profile_store {
namespace {

namespace fs = std::filesystem;

// The newest profile per GUID this host session has been handed, and the copy of it taken the last
// time the world was gathered, which is the one a save writes.
struct Held {
    std::vector<uint8_t> blob;
    std::string          nick;
    uint64_t             hash = 0;
    bool                 changed = false;   // since the last gather
    std::vector<uint8_t> atGather;          // valid while `uncut`
    std::string          nickAtGather;
    bool                 uncut = false;     // gathered, not yet on disk
};
std::unordered_map<std::string, Held> g_held;
std::unordered_set<std::string> g_quarantined;  // stored profile unreadable: never written over

// What is held belongs to ONE loaded world. A process that loads a world to host it names the slot
// once per load (save_transfer::SetHostSlot), so a new serial means a new world was loaded from its
// save -- and that world is as old as the save, so the profiles to go with it are the files', not
// the ones held from the world before.
uint32_t g_heldForSerial = 0;
void DropHeldOfAnotherWorld() {
    const uint32_t serial = coop::save_transfer::HostSlotSerial();
    if (serial == g_heldForSerial) return;
    if (!g_held.empty())
        UE_LOGI("player_profile: another world was loaded to host -- %zu held profile(s) of the "
                "previous one dropped", g_held.size());
    g_held.clear();
    g_quarantined.clear();
    g_heldForSerial = serial;
}

// Build <gameDir>/coop_players/<hostSlot>/<guid>.json, empty if any piece is missing.
fs::path PlayerFilePath(const std::string& guid) {
    // Defence in depth (the wire boundary already validates): a GUID that is not exactly 32 hex
    // characters never becomes a path component; an empty path makes every write no-op, so a
    // non-hex GUID cannot traverse.
    if (!coop::player_handshake::IsValidGuid(guid)) return {};
    const std::wstring base = ue_wrap::paths::ExeDir();
    if (base.empty()) return {};
    const std::wstring slot = coop::save_transfer::HostSlot();
    if (slot.empty()) return {};
    return fs::path(base) / L"coop_players" / slot / (std::wstring(guid.begin(), guid.end()) + L".json");
}

std::string Hex(const std::vector<uint8_t>& b) {
    static const char k[] = "0123456789abcdef";
    std::string s;
    s.reserve(b.size() * 2);
    for (uint8_t c : b) { s.push_back(k[c >> 4]); s.push_back(k[c & 0xF]); }
    return s;
}

// Decode a hex string of either case to bytes. False on an odd length or a non-hex digit.
bool UnHex(const std::string& s, std::vector<uint8_t>& out) {
    if (s.size() & 1) return false;
    auto nib = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    out.clear();
    out.reserve(s.size() / 2);
    for (size_t i = 0; i < s.size(); i += 2) {
        const int hi = nib(s[i]), lo = nib(s[i + 1]);
        if (hi < 0 || lo < 0) return false;
        out.push_back(static_cast<uint8_t>((hi << 4) | lo));
    }
    return true;
}

// Extract the value of a string field from our flat JSON (no nesting or escapes are used).
bool JsonStr(const std::string& doc, const char* key, std::string& out) {
    std::string needle = std::string("\"") + key + "\":\"";
    const size_t k = doc.find(needle);
    if (k == std::string::npos) return false;
    const size_t v = k + needle.size();
    const size_t e = doc.find('"', v);
    if (e == std::string::npos) return false;
    out = doc.substr(v, e - v);
    return true;
}

// Parse one stored file into `outBlob`. Defensive against a hand edit: requires the magic, a
// parseable hex blob and a matching FNV. False on any failure (the caller tries the .bak); never
// throws, never returns unverified bytes.
bool ParseBlobFile(const fs::path& file, std::vector<uint8_t>& outBlob) {
    std::error_code ec;
    if (file.empty() || !fs::exists(file, ec)) return false;
    std::ifstream f(file, std::ios::binary);
    if (!f) return false;
    std::string doc((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    std::string magic;
    if (!JsonStr(doc, "magic", magic) || magic != "VCPI") {
        UE_LOGW("player_profile: '%ls' missing/bad magic -- treating as corrupt", file.c_str());
        return false;
    }
    std::string blobHex, fnvHex;
    if (!JsonStr(doc, "blob", blobHex) || !JsonStr(doc, "fnv", fnvHex)) return false;
    std::vector<uint8_t> blob;
    if (!UnHex(blobHex, blob)) {
        UE_LOGW("player_profile: '%ls' blob hex unparseable -- corrupt", file.c_str());
        return false;
    }
    char want[17] = {};
    std::snprintf(want, sizeof(want), "%016llx",
                  static_cast<unsigned long long>(coop::blob_chunks::Fnv64(blob)));
    if (fnvHex != want) {
        UE_LOGW("player_profile: '%ls' FNV mismatch (have %s, stored %s) -- corrupt/tampered",
                file.c_str(), want, fnvHex.c_str());
        return false;
    }
    outBlob = std::move(blob);
    return true;
}

bool ReadFile(const std::string& guid, std::vector<uint8_t>& outBlob) {
    const fs::path file = PlayerFilePath(guid);
    if (file.empty()) return false;
    if (ParseBlobFile(file, outBlob)) return true;
    const fs::path bak = fs::path(file).concat(L".bak");
    if (ParseBlobFile(bak, outBlob)) {
        UE_LOGW("player_profile: recovered guid=%s inventory from .bak (primary corrupt)",
                guid.c_str());
        return true;
    }
    return false;
}

bool WriteFile(const std::string& guid, const std::vector<uint8_t>& blob, const std::string& nickJson) {
    const fs::path file = PlayerFilePath(guid);
    if (file.empty()) return false;
    std::error_code ec;
    // Keep the last good file as .bak before overwriting.
    if (fs::exists(file, ec))
        fs::copy_file(file, fs::path(file).concat(L".bak"), fs::copy_options::overwrite_existing, ec);
    const uint64_t fnv = coop::blob_chunks::Fnv64(blob);
    const long long epoch = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    char fnvhex[17] = {};
    std::snprintf(fnvhex, sizeof(fnvhex), "%016llx", static_cast<unsigned long long>(fnv));
    std::string json = "{\"magic\":\"VCPI\",\"ver\":1,\"fnv\":\"";
    json += fnvhex;
    json += "\",\"nick\":\"";
    json += nickJson;
    json += "\",\"lastSeen\":";
    json += std::to_string(epoch);
    json += ",\"blob\":\"";
    json += Hex(blob);
    json += "\"}\n";
    const fs::path tmp = fs::path(file).concat(L".part");
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f || !(f << json)) {
            UE_LOGE("player_profile: write failed ('%ls')", tmp.c_str());
            return false;
        }
    }
    fs::rename(tmp, file, ec);
    if (ec) {
        UE_LOGE("player_profile: rename('%ls') failed: %s", file.c_str(), ec.message().c_str());
        return false;
    }
    return true;
}

}  // namespace

// Encoding is the codec's job; escaping is this site's, because the container is JSON. Raw UTF-8
// is valid inside a JSON string, only the two structural metacharacters have to go, and they
// cannot appear inside a multi-byte sequence (continuation bytes are all above 0x7F), so dropping
// them cannot corrupt one.
std::string NickForJson(const std::wstring& nick) {
    std::string s = coop::text::CapUtf8Bytes(coop::text::ToUtf8(nick), coop::text::kNickMaxBytes);
    s.erase(std::remove_if(s.begin(), s.end(),
                           [](char c) { return c == '"' || c == '\\'; }),
            s.end());
    return s;
}

bool Put(const std::string& guid, std::vector<uint8_t> blob, const std::string& nickJson) {
    if (!coop::player_handshake::IsValidGuid(guid)) return false;
    DropHeldOfAnotherWorld();
    if (g_quarantined.count(guid)) return false;
    const uint64_t hash = coop::blob_chunks::Fnv64(blob);
    Held& h = g_held[guid];
    if (h.hash == hash && h.blob == blob) return false;
    h.blob = std::move(blob);
    h.nick = nickJson;
    h.hash = hash;
    h.changed = true;
    return true;
}

Found Get(const std::string& guid, std::vector<uint8_t>& outBlob) {
    DropHeldOfAnotherWorld();
    const auto it = g_held.find(guid);
    if (it != g_held.end()) { outBlob = it->second.blob; return Found::Yes; }
    if (ReadFile(guid, outBlob)) return Found::Yes;
    const fs::path file = PlayerFilePath(guid);
    std::error_code ec;
    const bool onDisk = !file.empty() &&
                        (fs::exists(file, ec) || fs::exists(fs::path(file).concat(L".bak"), ec));
    return onDisk ? Found::Unreadable : Found::Absent;
}

void Quarantine(const std::string& guid) {
    DropHeldOfAnotherWorld();
    g_held.erase(guid);
    if (g_quarantined.insert(guid).second)
        UE_LOGE("player_profile: the stored profile of guid=%s did NOT read -- nothing of this player "
                "is held or written for the rest of this hosted world; the files are left as they "
                "are for recovery", guid.c_str());
}

bool AnythingPending() {
    DropHeldOfAnotherWorld();
    for (const auto& [guid, h] : g_held)
        if (h.changed || h.uncut) return true;
    return false;
}

void MarkWorldGathered() {
    DropHeldOfAnotherWorld();
    for (auto& [guid, h] : g_held) {
        if (!h.changed) continue;
        h.atGather = h.blob;
        h.nickAtGather = h.nick;
        h.uncut = true;
        h.changed = false;
    }
}

size_t CutToDisk() {
    DropHeldOfAnotherWorld();
    size_t written = 0;
    bool dirReady = false;
    for (auto it = g_held.begin(); it != g_held.end();) {
        Held& h = it->second;
        if (h.uncut) {
            if (!dirReady) {  // one directory for every profile of this world
                const fs::path dir = PlayerFilePath(it->first).parent_path();
                std::error_code ec;
                if (!dir.empty()) fs::create_directories(dir, ec);
                if (dir.empty() || ec) {
                    UE_LOGW("player_profile: the profile directory could not be made (%s) -- "
                            "nothing cut, everything stays for the next save",
                            dir.empty() ? "no host slot or game directory" : ec.message().c_str());
                    return 0;
                }
                dirReady = true;
            }
            if (WriteFile(it->first, h.atGather, h.nickAtGather)) {
                h.uncut = false;
                h.atGather = {};
                ++written;
            }  // else: stays for the next cut
        }
        // What is on disk and unchanged since is the file's to give back (Get reads it), so it
        // need not be held: a host meets new identities for as long as its world lives.
        if (!h.uncut && !h.changed) it = g_held.erase(it);
        else ++it;
    }
    return written;
}

}  // namespace coop::player_profile_store
