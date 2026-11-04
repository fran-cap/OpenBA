// Repro for src/render/common/better_trees/natural_growth.cpp:152 (AppendElement):
// strncmp(d.data()+no, "TEXCOORD", 9) with only `no < d.size()` checked.
// Body of the loop copied verbatim; the chunk ends in "TEXCOORD" with no NUL.
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <vector>
int Scan(const std::vector<uint8_t>& d) {
    uint32_t count;
    std::memcpy(&count, d.data(), 4);
    const size_t table = 8 + 24ull * count;
    if (d.size() < table) return -2;
    int tex = -1;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t no;
        std::memcpy(&no, d.data() + 8 + 24 * i, 4);
        if (no < table || no >= d.size()) return -3;
        if (std::strncmp(reinterpret_cast<const char*>(d.data() + no), "TEXCOORD", 9) == 0) tex = static_cast<int>(i);
    }
    return tex;
}
int main() {
    std::vector<uint8_t> d(8 + 24 + 8, 0);     // header, one element, 8 name bytes
    uint32_t count = 1, no = 32;
    std::memcpy(d.data(), &count, 4);
    std::memcpy(d.data() + 8, &no, 4);
    std::memcpy(d.data() + 32, "TEXCOORD", 8);   // no terminator inside the chunk
    std::printf("tex=%d\n", Scan(d));
}
