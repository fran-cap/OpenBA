// P3 proof: RtIo_SerializeStringArray (text_codec.cpp:483) on a resource string whose length word
// has bit 31 clear (legacy UTF-16) and length 0x40000001: `static_cast<int32_t>(len) * 2 - 2`
// (:522) overflows int32. Reached from PackageFile::Serialize (package_file_serialize.cpp:216),
// AnimatedImageDescription / DocumentTextDescription / PixelProgram resources (packages, mods).
// The serializer runs over the same CompressedMemoryStream shape LoadMetaData builds.
#include "io/Serializer.h"
#include "io/Stream.h"
#include "system/String.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

int main() {
    std::vector<uint8_t> p;
    auto put32 = [&](std::vector<uint8_t>& v, uint32_t x) { for (int i = 0; i < 4; ++i) v.push_back(uint8_t(x >> (8 * i))); };
    auto put16 = [&](std::vector<uint8_t>& v, uint16_t x) { v.push_back(uint8_t(x)); v.push_back(uint8_t(x >> 8)); };
    put32(p, 0xAAAAAAAAu);   // BinaryReader magic
    put32(p, 0);             // word20
    put32(p, 1);             // version
    put32(p, 0x40000001u);   // tagged length: bit 31 clear (UTF-16), len 0x40000001
    p.resize(p.size() + 64, 0);
    std::vector<uint8_t> blob;
    put32(blob, 1);
    put32(blob, 12);
    put16(blob, 0);
    put16(blob, static_cast<uint16_t>(p.size()));
    blob.insert(blob.end(), p.begin(), p.end());

    IO::CompressedMemoryStream cms;
    cms._memoryStream.Open(static_cast<int>(blob.size()));
    std::memmove(cms._memoryStream._data, blob.data(), blob.size());
    cms.Open();
    IO::Serializer ser(cms, 0);
    System::String s;
    RtIo_SerializeStringArray(&ser, 1, &s, nullptr);
    std::printf("no overflow reported\n");
    return 0;
}
