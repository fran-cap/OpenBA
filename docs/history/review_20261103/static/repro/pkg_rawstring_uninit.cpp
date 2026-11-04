// Repro for src/engine/io/package_file_read.cpp ReadStringRaw (:381) -> BuildStringFrom (:302):
// formatVersion > 2, the first metadata string carries len <= 0, so nothing is read into
// frameBuf (uint8_t[0x1840], never initialised, :487) and BuildStringFrom scans it for a NUL.
// The two helpers are copied verbatim (System::String replaced by a char* + length).
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
struct Str { char* _data; int len; };
void BuildStringFrom(const uint8_t* buf, Str& tmp) {
    int32_t n1 = 0;
    while (buf[n1] != 0) ++n1;
    tmp._data = (char*)malloc(n1 + 1); tmp.len = n1;
    const int32_t cap = n1 + 1;
    int32_t n2 = 0;
    while (buf[n2] != 0) ++n2;
    ++n2;
    const int32_t copyLen = (cap < n2) ? cap : n2;
    std::memcpy(tmp._data, buf, static_cast<size_t>(copyLen));
    if (n2 > cap) tmp._data[cap - 1] = 0;
}
__attribute__((noinline)) int ReadPackfileFirstString(int32_t len /* from the .pkm */) {
    uint8_t frameBuf[0x1840];                 // :487, as retail: no initialiser
    uint8_t* const buf60 = frameBuf;
    if (len > 0) std::memset(buf60, 0, (size_t)len);   // stands in for cs->Read(len, buf60)
    Str tmp{};
    BuildStringFrom(buf60, tmp);
    int n = tmp.len; free(tmp._data); return n;
}
int main(int argc, char**) { std::printf("name length %d\n", ReadPackfileFirstString(argc - 1)); }
