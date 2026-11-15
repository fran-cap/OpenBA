#pragma once
// ============================================================================
// src/ext/stream_cache/stream_cache.h -- ext.stream_cache (class A, next start)
// ============================================================================
// NOT RETAIL (issue #13, docs/extensions/handins/STREAMCACHE.md). A boot with a large mod set
// opens ~376k compiled resource streams; most are a few hundred bytes long, and the large ones
// (meshes, textures) are read only in their header and in the size fields between the payloads
// ext.create_on_demand skips -- yet every one pays the compressed stream's setup (a 32 KB window
// allocated and zeroed, the block directory read) and the LZF decode of every block the consumed
// bytes fall in. This extension records, per packed entry, the ranges of the decompressed stream a
// load consumed and their bytes (stream_record.h), keeps them per package in the derived-data cache
// (src/ext/derived_cache/, keyed by the package's identity -- header, name, file size, last write
// time, entry table -- and whether create_on_demand skips payloads; within a package by the entry's
// offset and size), and at the next start serializes the resource from that record
// (segment_stream.h) instead of the compressed stream. A hit reads nothing of the package: the
// record carries the entry's block directory (STREAMCACHE2.md).
//
// EXACTNESS. The record is the bytes the serializer itself consumed, in order; the stream served
// from it answers the same reads with the same bytes and the same positions, and a read outside the
// record goes on through retail's compressed stream on the same entry (segment_stream.h). WHICH
// resources load, and in what order, is the serializer's business and unchanged: the save's reference
// table cannot move. A resource with no
// record loads as retail loads it, recorded; the records a load bracket produced are stored at the
// bracket's end by the derived cache's writer thread.
//
// Seams (DESIGN.md section 3 hook rule 5), all null by default and under the faithful profile:
//   Object::ExternalFactory::_extLoadBegin / _extLoadEnd   External::Load: the stream to read from
//   IO::CompressedStream::_extReadTap                       what the retail read consumed (recording)
//   Engine::LoaderLoop::_extLoadDone                        a load bracket closed: store the records
//   CreateOnDemand::g_altLocate / g_altSkip                  payload Locators over the record stream
// ============================================================================

namespace Ext {
namespace StreamCache {

// Once at boot, after the registry is final: installs the seams when the row is on and the derived
// cache opened.
void Boot();

}  // namespace StreamCache
}  // namespace Ext
