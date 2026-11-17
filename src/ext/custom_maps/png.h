#pragma once
// ============================================================================
// src/ext/custom_maps/png.h -- ext.custom_maps (class A): a small PNG reader for a map's thumbnail
// ============================================================================
// NOT RETAIL. docs/extensions/handins/CUSTOMMAPS.md. A .bmap carries its thumbnail as PNG bytes (bmap.h); the
// Custom map picker shows it the way the Load screen shows a save's, as a BGRA texture, and the game has no
// PNG reader. This one reads what a browser's canvas (or any common tool) writes: not interlaced, bit depth 1
// to 16, gray, gray + alpha, RGB, RGB + alpha and palette (with tRNS alpha). A file outside that, or damaged,
// is refused (false); chunk CRCs are not checked, the .bmap's own CRC covers the bytes.
//
// Standard C++ only: tests/unit/ext_custom_maps_test.cpp links this header and png.cpp.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Ext {
namespace CustomMaps {

constexpr uint32_t kMaxPngSide   = 2048;
constexpr size_t   kMaxPngPixels = 4u * 1024u * 1024u;

struct Image {
    uint32_t             width  = 0;
    uint32_t             height = 0;
    std::vector<uint8_t> bgra;   // width * height * 4, row 0 at the top
};

// Raw DEFLATE (RFC 1951) of `size` bytes into `out`, at most `maxOut` bytes. False on a damaged stream or
// when the result would be longer than `maxOut`.
bool Inflate(const uint8_t* data, size_t size, size_t maxOut, std::vector<uint8_t>& out);

// A zlib stream (RFC 1950: a 2 byte header, DEFLATE, an Adler-32 that is not checked). Same
// limits as Inflate.
bool ZlibInflate(const uint8_t* data, size_t size, size_t maxOut, std::vector<uint8_t>& out);

// Decodes a PNG file image. On false `out` is empty.
bool DecodePng(const uint8_t* data, size_t size, Image& out);

// The image scaled to exactly `width` x `height` so it covers that frame: the largest centered part of the
// source with the frame's aspect ratio is taken and resampled bilinearly (nothing is stretched). Empty for an
// empty source or a zero frame.
Image FitCover(const Image& source, uint32_t width, uint32_t height);

}  // namespace CustomMaps
}  // namespace Ext
