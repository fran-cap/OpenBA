// ============================================================================
// src/ext/savetail.cpp -- the class B / C manifest chunk (savetail.h)
// ============================================================================
// NOT RETAIL. Standard C++ only (the unit tests link it with registry.cpp).
// ============================================================================

#include "ext/savetail.h"

#include "ext/registry.h"

#include <string>
#include <utility>

namespace Ext {
namespace SaveTail {

namespace {

void PutU32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v));
    out.push_back(static_cast<uint8_t>(v >> 8));
    out.push_back(static_cast<uint8_t>(v >> 16));
    out.push_back(static_cast<uint8_t>(v >> 24));
}

uint32_t GetU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

}  // namespace

const char* StatusName(Status s) {
    switch (s) {
    case Status::NoManifest:         return "no manifest";
    case Status::Ok:                 return "ok";
    case Status::Truncated:          return "truncated";
    case Status::BadChecksum:        return "bad checksum";
    case Status::NewerChunkVersion:  return "chunk version newer than this build";
    case Status::Malformed:          return "malformed";
    case Status::UnknownId:          return "unknown extension";
    case Status::NotLockedClass:     return "not a class B / C extension";
    case Status::NewerFormatVersion: return "format version newer than this build";
    case Status::DuplicateId:        return "extension named twice";
    case Status::UnknownKind:        return "record kind this build does not have";
    }
    return "?";
}

uint32_t Crc32(const uint8_t* data, size_t size) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return crc ^ 0xFFFFFFFFu;
}

std::vector<uint8_t> Encode(const std::vector<Record>& records) {
    std::vector<uint8_t> out;
    if (records.empty()) return out;   // zero bytes: the faithful profile's tail
    PutU32(out, kMagic);
    PutU32(out, kVersion);
    PutU32(out, static_cast<uint32_t>(records.size()));
    for (const Record& r : records) {
        out.push_back(r.kind);
        out.push_back(r.flags);
        PutU32(out, static_cast<uint32_t>(r.id.size()));
        out.insert(out.end(), r.id.begin(), r.id.end());
        PutU32(out, static_cast<uint32_t>(r.formatVersion));
        PutU32(out, static_cast<uint32_t>(r.payload.size()));
        out.insert(out.end(), r.payload.begin(), r.payload.end());
    }
    const uint32_t bodyLength = static_cast<uint32_t>(out.size());
    const uint32_t crc = Crc32(out.data(), out.size());
    PutU32(out, bodyLength);
    PutU32(out, crc);
    return out;
}

Status Decode(const uint8_t* data, size_t size, std::vector<Record>& out) {
    out.clear();
    if (data == nullptr || size == 0) return Status::NoManifest;

    // Ours only if it starts with the magic. A cut inside the magic itself is
    // a manifest that was truncated; anything else is not ours.
    uint8_t magic[4];
    for (int i = 0; i < 4; ++i) magic[i] = static_cast<uint8_t>(kMagic >> (8 * i));
    const size_t head = size < 4 ? size : 4;
    for (size_t i = 0; i < head; ++i)
        if (data[i] != magic[i]) return Status::NoManifest;
    if (size < 12 + kTrailerBytes) return Status::Truncated;

    // The trailer, from the end.
    const uint32_t bodyLength = GetU32(data + size - kTrailerBytes);
    const uint32_t crc        = GetU32(data + size - 4);
    if (static_cast<size_t>(bodyLength) != size - kTrailerBytes) return Status::Truncated;
    if (Crc32(data, bodyLength) != crc) return Status::BadChecksum;

    Reader r(data, bodyLength);
    uint32_t m = 0, version = 0, count = 0;
    r.U32(m);
    r.U32(version);
    if (version > kVersion) return Status::NewerChunkVersion;
    if (version < 1) return Status::Malformed;
    r.U32(count);

    std::vector<Record> records;
    for (uint32_t i = 0; i < count; ++i) {
        Record rec;
        if (version >= 2) {
            // the kind and flags bytes (version 1 has none: every record an extension's, must-understand)
            const uint8_t* kf = nullptr;
            if (!r.Bytes(2, kf)) {
                return Status::Malformed;
            }
            rec.kind = kf[0];
            rec.flags = kf[1];
        }
        uint32_t idLength = 0;
        const uint8_t* idBytes = nullptr;
        if (!r.U32(idLength) || idLength == 0 || idLength > kMaxIdLength ||
            !r.Bytes(idLength, idBytes))
            return Status::Malformed;
        rec.id.assign(idBytes, idBytes + idLength);   // byte by byte: no pointer cast
        if (!ValidId(rec.id.c_str())) return Status::Malformed;
        uint32_t fv = 0, payloadLength = 0;
        const uint8_t* payload = nullptr;
        if (!r.U32(fv) || !r.U32(payloadLength) || !r.Bytes(payloadLength, payload))
            return Status::Malformed;
        rec.formatVersion = static_cast<int32_t>(fv);
        if (rec.formatVersion < 1) return Status::Malformed;
        rec.payload.assign(payload, payload + payloadLength);
        records.push_back(std::move(rec));
    }
    if (r.left != 0) return Status::Malformed;
    out = std::move(records);
    return Status::Ok;
}

Status Validate(const std::vector<Record>& records, std::string* offending) {
    for (size_t i = 0; i < records.size(); ++i) {
        const Record& rec = records[i];
        Status st = Status::Ok;
        const Entry* e = FindEntry(rec.id.c_str());
        // A retired row's record at or below its last formatVersion is accepted here and dropped by
        // DropRetired before the lock (registry.h RETIRED ROWS); any other unknown id stays fatal.
        if (e == nullptr) {
            if (!RetiredSaveEntry(rec.id.c_str(), rec.formatVersion)) st = Status::UnknownId;
        } else if (e->tier != Tier::Extension || !IsLockedClass(e->cls)) {
            st = Status::NotLockedClass;
        } else if (rec.formatVersion > e->formatVersion) {
            st = Status::NewerFormatVersion;
        }
        if (st == Status::Ok) {
            for (size_t j = 0; j < i; ++j)
                if (records[j].id == rec.id) { st = Status::DuplicateId; break; }
        }
        if (st != Status::Ok) {
            if (offending != nullptr) *offending = rec.id;
            return st;
        }
    }
    return Status::Ok;
}

void DropRetired(std::vector<Record>& records, std::vector<std::string>* dropped) {
    size_t kept = 0;
    for (size_t i = 0; i < records.size(); ++i) {
        const bool retired = FindEntry(records[i].id.c_str()) == nullptr &&
                             RetiredSaveEntry(records[i].id.c_str(), records[i].formatVersion);
        if (retired) {
            if (dropped != nullptr) {
                dropped->push_back(records[i].id);
            }
            continue;
        }
        if (kept != i) {
            records[kept] = std::move(records[i]);
        }
        ++kept;
    }
    records.resize(kept);
}

std::vector<Record> ActiveRecords() {
    std::vector<Record> out;
    for (int i = 0; i < EntryCount(); ++i) {
        const Entry& e = EntryAt(i);
        if (e.tier != Tier::Extension || !IsLockedClass(e.cls) || !Active(e)) continue;
        Record r;
        r.id = e.id;
        r.formatVersion = e.formatVersion;
        if (const StateCodec* c = FindCodec(e.id)) c->encode(r.payload);   // class B: no codec, empty payload
        out.push_back(std::move(r));
    }
    return out;
}

std::vector<Record> HostRecords() {
    std::vector<Record> out;
    for (int i = 0;; ++i) {
        const HostCodec* h = HostCodecAt(i);
        if (h == nullptr) {
            break;
        }
        Record r;
        r.kind = kKindHost;
        r.flags = 0;   // presentation only: a build without this host drops it
        r.id = h->id;
        r.formatVersion = h->formatVersion;
        h->encode(r.payload);
        out.push_back(std::move(r));
    }
    return out;
}

std::vector<uint8_t> EncodeActive() {
    std::vector<Record> records = ActiveRecords();
    if (records.empty()) {
        return std::vector<uint8_t>();   // zero bytes: no host record without an extension record
    }
    std::vector<Record> hosts = HostRecords();
    for (Record& h : hosts) {
        records.push_back(std::move(h));
    }
    return Encode(records);
}

Status SplitKinds(const std::vector<Record>& all, std::vector<Record>& extensions, std::vector<Record>& hosts,
                  std::vector<std::string>* dropped, std::string* offending) {
    extensions.clear();
    hosts.clear();
    for (const Record& r : all) {
        if (r.kind == kKindExtension) {
            extensions.push_back(r);
        } else if (r.kind == kKindHost) {
            hosts.push_back(r);
        } else if ((r.flags & kFlagMustUnderstand) != 0) {
            if (offending != nullptr) {
                *offending = r.id;
            }
            extensions.clear();
            hosts.clear();
            return Status::UnknownKind;
        } else if (dropped != nullptr) {
            dropped->push_back(r.id);
        }
    }
    return Status::Ok;
}

void ApplyHostState(const std::vector<Record>& hosts, std::vector<std::string>* notes) {
    for (const Record& r : hosts) {
        const HostCodec* h = FindHostCodec(r.id.c_str());
        std::string why;
        if (h == nullptr) {
            why = "no such host in this build";
        } else if (r.formatVersion > h->formatVersion) {
            why = "saved by a newer version of the host";
        } else if (!h->decode(r.payload.data(), r.payload.size(), r.formatVersion)) {
            why = "its payload did not decode";
        }
        if (why.empty()) {
            continue;
        }
        if (h != nullptr) {
            h->reset();
        }
        if (notes != nullptr) {
            notes->push_back("host " + r.id + " (v" + std::to_string(r.formatVersion) + ") dropped: " + why);
        }
    }
}

bool ApplyState(const std::vector<Record>& records, std::string* offending) {
    for (const Record& r : records) {
        const StateCodec* c = FindCodec(r.id.c_str());
        if (c == nullptr) continue;   // a class B row: the payload is empty
        if (!c->decode(r.payload.data(), r.payload.size(), r.formatVersion)) {
            if (offending != nullptr) *offending = r.id;
            return false;
        }
    }
    return true;
}

}  // namespace SaveTail
}  // namespace Ext
