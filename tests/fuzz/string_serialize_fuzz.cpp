// ============================================================================
// tests/fuzz/string_serialize_fuzz.cpp -- System::String::Serialize and RtIo_SerializeStringArray
//   over an arbitrary binary record, through the real IO::BinaryReader / IO::BinaryWriter
// ============================================================================
// P0_MAP input surface rank 6 (and the string leaf of rank 3: every string in a save or a
// resource). Build: `~/fz.sh build string_serialize src/engine/io/text_codec.cpp`.
//
// The stream is a minimal IO::Stream subclass over the input (all-or-nothing reads, as a short
// read leaves the destination untouched). The 12-byte record header BinaryReader's constructor
// consumes is synthesised, not taken from the input. Input layout: a sequence of ops,
//   op byte % 2 == 0   String::Serialize into one String
//   op byte % 2 == 1   RtIo_SerializeStringArray(count = next byte % 8) into `count` Strings
// followed by whatever bytes those calls read. Every String read is then written back through
// BinaryWriter and read again; the second read must give the same length and bytes (round trip).
//
// MASKS (FUZZ_NO_MASK, set by string_serialize2_fuzz.cpp, turns them off):
//   M2  The 31-bit length word goes straight into SetLength(n - 1) / MemAlloc(n * 2) with no cap
//       (finding SS-1: a 0x7FFFFFFF word asks for a 2 GB / 4 GB block). Every 4-byte read is a
//       length word here (the payloads are read 1 or 2 bytes at a time), so the stream clamps the
//       low 31 bits of every 4-byte read to 0x1FFFF.
//   M3  The plain arm does not write the terminator: the String ends in whatever the stream's
//       last byte was (finding SS-2). Only the NO_MASK variant walks the result to its NUL.
// ============================================================================
#include "io/PackageFile.h"
#include "io/Serializer.h"
#include "io/Stream.h"
#include "object/External.h"
#include "system/String.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>

// Link stub: error.cpp (pulled in by the allocator) references the platform message box.
namespace Platform::Window {
void ShowErrorMessage(const uint16_t*, const uint16_t*) {}
}  // namespace Platform::Window

// Link stubs: the type-record tables pulled in with the BinaryReader name these factories, whose
// bodies live in registry-side sources outside the archive (as b39_obj_externallist_test.cpp does).
Object::Base* Object::ExternalList::CreateNewObject() { return nullptr; }
Object::Base* Object::ExternalList::CreateInPlaceObject(void*) { return nullptr; }
Object::Base* IO::PackageFile::CreateNewObject() { return nullptr; }
Object::Base* IO::PackageFile::CreateInPlaceObject(void*) { return nullptr; }

namespace {

class FuzzStream final : public IO::Stream {
public:
    FuzzStream(std::vector<uint8_t>* buf, int direction, bool clamp = false)
        : _buf(buf), _clamp(clamp) {
        _direction = direction;
    }
    ~FuzzStream() override {}
    bool Write(int n, const void* src) override {
        if (n <= 0)
            return true;
        const uint8_t* p = static_cast<const uint8_t*>(src);
        if (_pos + static_cast<size_t>(n) > _buf->size())
            _buf->resize(_pos + static_cast<size_t>(n));
        std::memcpy(_buf->data() + _pos, p, static_cast<size_t>(n));
        _pos += static_cast<size_t>(n);
        return true;
    }
    bool Read(int n, void* dst) override {
        if (n <= 0 || _pos + static_cast<size_t>(n) > _buf->size())
            return false;
        std::memcpy(dst, _buf->data() + _pos, static_cast<size_t>(n));
#ifndef FUZZ_NO_MASK
        // M2: see the banner.
        if (_clamp && n == 4 && _pos >= 12) {
            uint32_t v;
            std::memcpy(&v, dst, 4);
            v = (v & 0x80000000u) | (v & 0x1FFFFu);
            std::memcpy(dst, &v, 4);
        }
#endif
        _pos += static_cast<size_t>(n);
        return true;
    }
    bool SetCurrentPos(int pos) override {
        _pos = static_cast<size_t>(pos);
        return true;
    }
    uint32_t GetCurrentPos() const override { return static_cast<uint32_t>(_pos); }
    uint32_t GetSize() const override { return static_cast<uint32_t>(_buf->size()); }

private:
    std::vector<uint8_t>* _buf;
    bool _clamp;   // M2 applies to the fuzz input only, not to the round trip's own record
    size_t _pos = 0;
};

// IO::Serializer without its constructor (which would ask the stream's Encoding for a factory):
// the two fields, set by hand, as decode_exact_test.cpp does.
struct SerializerSeat {
    alignas(IO::Serializer) unsigned char mem[sizeof(IO::Serializer)];
    IO::Serializer& Get() { return *reinterpret_cast<IO::Serializer*>(mem); }
};

void CheckString(const System::String& s) {
    if (s._data == nullptr)
        return;
    const int len = s.GetLength();
    if (len < 0)
        __builtin_trap();
#ifdef FUZZ_NO_MASK
    // M3 off: what any C-string consumer of the result does.
    volatile size_t n = std::strlen(s._data);
    (void)n;
#endif
}

// Write `s` back out and read it again: same length and bytes.
void RoundTrip(System::String& s) {
    std::vector<uint8_t> out;
    {
        FuzzStream ws(&out, 1);
        SerializerSeat seat;
        IO::Serializer& ser = seat.Get();
        ser._stream = &ws;
        IO::BinaryWriter* w = new (std::malloc(sizeof(IO::BinaryWriter))) IO::BinaryWriter(ser, 0);
        ser._readerWriter = w;
        s.Serialize(ser);
        w->~BinaryWriter();
        std::free(w);
    }
    FuzzStream rs(&out, 0);
    SerializerSeat seat;
    IO::Serializer& ser = seat.Get();
    ser._stream = &rs;
    IO::BinaryReader r(ser, 0);
    ser._readerWriter = &r;
    System::String back;
    back.Serialize(ser);
    const int a = s.GetLength();
    const int b = back.GetLength();
    if (a != b)
        __builtin_trap();
    if (a > 0 && std::memcmp(s._data, back._data, static_cast<size_t>(a)) != 0)
        __builtin_trap();
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size == 0 || size > 0x10000)
        return 0;
    std::vector<uint8_t> buf(12, 0);   // the record header BinaryReader's constructor consumes
    buf.insert(buf.end(), data, data + size);
    FuzzStream st(&buf, 0, true);
    SerializerSeat seat;
    IO::Serializer& ser = seat.Get();
    ser._stream = &st;
    {
        IO::BinaryReader r(ser, 0);
        ser._readerWriter = &r;
        int ops = 0;
        uint8_t op = 0;
        while (ops++ < 16 && st.Read(1, &op)) {
            if ((op & 1) == 0) {
                System::String s;
                s.Serialize(ser);
                CheckString(s);
                RoundTrip(s);
            } else {
                uint8_t c = 0;
                if (!st.Read(1, &c))
                    break;
                const int count = c % 8;
                System::String arr[8];
                RtIo_SerializeStringArray(&ser, count, arr, nullptr);
                for (int i = 0; i < count; ++i) {
                    CheckString(arr[i]);
                    RoundTrip(arr[i]);
                }
            }
        }
    }
    return 0;
}
