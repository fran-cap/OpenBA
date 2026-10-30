// src/ext/tool_names/tool_names.cpp -- see tool_names.h: the note table and the save-tail codec (standard C++)
#include "ext/tool_names/tool_names.h"

#include "ext/savetail.h"

#include <algorithm>
#include <map>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace Ext {
namespace ToolNames {

namespace {

// ENCODE RUNS ON THE SAVE WORKER (ext.background_save) while the panel on the main thread may claim a record, so
// every access to the two tables takes this lock.
std::mutex g_lock;
std::unordered_map<const void*, Note> g_live;        // citizen component -> its tool
std::unordered_map<int32_t, Note> g_saved;           // entity id -> a record from a save, not claimed yet

bool Same(const Citizen& a, const Citizen& b) {
    return a.entity == b.entity && a.gender == b.gender && a.name == b.name;
}

bool PathIsValid(const std::string& path) {
    if (path.empty() || path.size() > kMaxPathBytes) return false;
    for (const char c : path)
        if (static_cast<unsigned char>(c) < 0x20) return false;
    return true;
}

// ---- wire format v1 (little-endian) ----
//   u32  count          0..kMaxNotes
//   count x {
//     i32  entity       the citizen's entity id
//     i32  uses         > 0
//     u8   gender       0 or 1
//     u32  length + name bytes      0..kMaxNameBytes, no NUL
//     u32  length + path bytes      1..kMaxPathBytes, bytes 0x20..0xFF
//   }
// Decode accepts exactly what Encode writes: it refuses a count the payload cannot hold, a gender other than 0
// or 1, a use count below 1, an over-long or NUL-holding name, an empty, over-long or control-character path,
// an entity twice, bytes left over, or a cut-short payload. A refused payload changes nothing.
constexpr size_t kMinEntryBytes = 4 + 4 + 1 + 4 + 4 + 1;

void Encode(std::vector<uint8_t>& payload) {
    std::map<int32_t, Note> all;   // by entity id: a live note wins over a waiting record
    {
        std::lock_guard<std::mutex> guard(g_lock);
        for (const auto& kv : g_saved) all[kv.first] = kv.second;
        for (const auto& kv : g_live) {
            const Note& n = kv.second;
            if (n.who.entity < 0 || n.uses <= 0 || !PathIsValid(n.path)) continue;
            if (n.who.name.size() > kMaxNameBytes) continue;
            all[n.who.entity] = n;
        }
    }
    SaveTail::Writer w(payload);
    w.U32(static_cast<uint32_t>(std::min(all.size(), kMaxNotes)));
    size_t written = 0;
    for (const auto& kv : all) {
        if (written++ >= kMaxNotes) break;
        const Note& n = kv.second;
        w.I32(n.who.entity);
        w.I32(n.uses);
        const uint8_t gender = n.who.gender == 0 ? 0 : 1;
        w.Bytes(&gender, 1);
        w.String(n.who.name);
        w.String(n.path);
    }
}

bool Decode(const uint8_t* bytes, size_t size, int32_t savedVersion) {
    if (savedVersion != 1) return false;
    SaveTail::Reader r(bytes, size);
    uint32_t count = 0;
    if (!r.U32(count) || count > kMaxNotes || count > r.left / kMinEntryBytes) return false;
    std::unordered_map<int32_t, Note> records;
    for (uint32_t i = 0; i < count; ++i) {
        Note n;
        const uint8_t* gender = nullptr;
        if (!r.I32(n.who.entity) || !r.I32(n.uses) || !r.Bytes(1, gender)) return false;
        if (n.uses < 1 || *gender > 1) return false;
        n.who.gender = *gender;
        if (!r.String(n.who.name) || n.who.name.size() > kMaxNameBytes) return false;
        if (n.who.name.find('\0') != std::string::npos) return false;
        if (!r.String(n.path) || !PathIsValid(n.path)) return false;
        if (!records.emplace(n.who.entity, n).second) return false;   // an entity twice
    }
    if (!r.AtEnd()) return false;
    std::lock_guard<std::mutex> guard(g_lock);
    g_live.clear();
    g_saved.swap(records);
    return true;
}

void Reset() { Clear(); }

}  // namespace

void Remember(const void* citizen, const void* item, int32_t uses, const Citizen& who, const std::string& path) {
    if (citizen == nullptr || item == nullptr) return;
    std::lock_guard<std::mutex> guard(g_lock);
    if (g_live.size() >= kMaxNotes && g_live.find(citizen) == g_live.end()) g_live.clear();
    Note n;
    n.item = item;
    n.uses = uses;
    n.who = who;
    n.path = path;
    g_live[citizen] = n;
    g_saved.erase(who.entity);   // a new pick-up supersedes a record the save held for this citizen
}

const void* Lookup(const void* citizen, int32_t uses) {
    std::lock_guard<std::mutex> guard(g_lock);
    const auto it = g_live.find(citizen);
    if (it == g_live.end() || it->second.uses != uses) return nullptr;
    return it->second.item;
}

bool Claim(const Citizen& who, int32_t uses, std::string& path) {
    std::lock_guard<std::mutex> guard(g_lock);
    const auto it = g_saved.find(who.entity);
    if (it == g_saved.end() || it->second.uses != uses || !Same(it->second.who, who)) return false;
    path = it->second.path;
    g_saved.erase(it);
    return true;
}

void Prune(const std::unordered_set<const void*>& alive) {
    std::lock_guard<std::mutex> guard(g_lock);
    for (auto it = g_live.begin(); it != g_live.end();) {
        it = alive.count(it->first) != 0 ? std::next(it) : g_live.erase(it);
    }
}

void Forget(const void* citizen) {
    std::lock_guard<std::mutex> guard(g_lock);
    g_live.erase(citizen);
}

void Clear() {
    std::lock_guard<std::mutex> guard(g_lock);
    g_live.clear();
    g_saved.clear();
}

std::size_t LiveCount() {
    std::lock_guard<std::mutex> guard(g_lock);
    return g_live.size();
}

std::size_t SavedCount() {
    std::lock_guard<std::mutex> guard(g_lock);
    return g_saved.size();
}

const SaveTail::StateCodec& Codec() {
    static const SaveTail::StateCodec c = { "tool_names", Encode, Decode, Reset };
    return c;
}

}  // namespace ToolNames
}  // namespace Ext
