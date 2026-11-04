// ============================================================================
// src/engine/io/settings_file.h — the settings.cfg codec (Phase D.4 SETTINGS E.1)
// ============================================================================
// NOT RETAIL. Retail kept IO::Persistence's flat settings map in the registry
// (HKCU\Software\Shining Rock Software LLC\Banished, every value REG_SZ). The
// portable layer keeps the same flat map in a UTF-8 text file instead
// (docs/runtime_port/phase_d/plans/SETTINGS.md §B). This header is only the
// text format: it knows nothing about the engine, the map or the OS, so the
// unit tests drive it directly (tests/unit/settings_file_test.cpp).
//
// FORMAT. One `key=value` entry per line.
//   Write: LF line ends, no BOM, a final newline, entries sorted by strcmp on
//          the (unescaped) key — the map's own in-order walk, so the file is
//          deterministic. The one BOM written is in front of a first key that
//          itself begins with the BOM bytes, so the reader does not eat them.
//   Read:  a leading UTF-8 BOM is skipped; CRLF is accepted; blank lines and
//          lines whose first non-blank character is `#` are skipped; a line
//          with no unescaped `=` is skipped; the key / value split is the FIRST
//          unescaped `=`; unescaped spaces and tabs around the key and the value
//          are trimmed; a duplicate key keeps its first position and takes the
//          LAST value (last wins).
//   Escapes (key and value): `\\` backslash, `\n` LF, `\r` CR, `\t` tab,
//          `\s` a significant leading / trailing space, `\=` (written in keys
//          only, accepted anywhere), `\#` (written only for a key's leading
//          `#`, which would otherwise read as a comment; accepted anywhere).
//          An unknown escape reads as itself, backslash included; a lone
//          trailing backslash reads as a backslash.
// Bytes >= 0x80 pass through untouched (UTF-8 in, UTF-8 out). No length cap.
// ============================================================================
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace IO {
namespace SettingsFile {

struct Entry {
    std::string key;
    std::string value;
};

// An ordered list of unique keys.
using Entries = std::vector<Entry>;

// Text -> entries, in first-appearance order, duplicates collapsed (last value
// wins). Never fails: what cannot be read is skipped.
Entries Parse(const char* text, size_t length);
inline Entries Parse(const std::string& text) { return Parse(text.data(), text.size()); }

// Entries -> text, sorted by strcmp on the key. Duplicate keys in `entries`
// are written as they are (Parse's last-wins reads them back).
std::string Serialize(const Entries& entries);

// The union of `disk` and `map`, sorted by key: every key of either, the
// value from `map` where both hold it. This is retail's observable
// WriteSettings outcome — its delete pass never deleted anything, so a value
// absent from the in-memory map survived in the store.
Entries Merge(const Entries& disk, const Entries& map);

// One field's escaped spelling (exposed for the tests).
std::string Escape(const std::string& field, bool isKey);

}  // namespace SettingsFile
}  // namespace IO
