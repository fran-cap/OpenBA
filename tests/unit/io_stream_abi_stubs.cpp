// ============================================================================
// tests/unit/io_stream_abi_stubs.cpp — the out-of-line virtuals the REAL
// `IO` stream classes own, defined for the host link only.
// ENDGAME B.3.4 block 4 (STREAMS).
// ============================================================================
//
// WHY THIS FILE EXISTS, AND WHY IT IS NOT A MIRROR
// ------------------------------------------------
// `tests/unit/io_vtable_abi_test.cpp` PART G pins the real stream classes'
// vtable order by constructing a concrete probe subclass of each and
// raw-calling every slot. Constructing one runs the BASE's constructor, which
// stores the base's vptr — so the link needs `vtable for IO::Stream`,
// `vtable for IO::MemoryStream` and so on.
//
// GCC emits a class's vtable in the translation unit that defines its KEY
// FUNCTION (the first non-inline, non-pure virtual), and for these classes that
// is `src/engine/io/memory_stream.cpp`, `file_stream.cpp`,
// `compressedstream.cpp` and `text_file_stream.cpp` — files that stand on
// the port registry, the allocator and <windows.h> and cannot link into a host
// test (tests/unit/README.md, B31_RULES rule 6).
//
// THE CLASS IS STILL THE REAL ONE. What is stubbed here is only the BODY of
// each member: the declarations, their order and their signatures come from
// src/engine/io/Stream.h and FileStream.h, which is exactly what PART G is
// testing. Every probe overrides every slot, so not one of these bodies is ever
// entered — each one aborts if it is, so "the probe forgot to override a slot"
// fails loudly instead of silently passing through a stub.
//
// This is the opposite of a mirror class. A mirror would re-declare the slots
// and then test its own copy of the order; this file re-declares NOTHING.
//
// IF ONE OF THOSE `src/engine/io/*.cpp` FILES IS EVER ADDED TO
// tests/unit/sources.cmake, the linker will report a duplicate definition here.
// That is the correct signal: delete the stub, not the real body.
// ============================================================================

#include "io/Stream.h"
#include "io/FileStream.h"
#include "io/PackageFile.h"
#include "io/FileSystem.h"

#include <cstdio>
#include <cstdlib>

namespace {

[[noreturn]] void StubReached(const char* what) {
    std::fprintf(stderr,
                 "\n[unit] io_stream_abi_stubs: %s was actually CALLED.\n"
                 "[unit] A PART G probe is missing an override for that slot,\n"
                 "[unit] so a raw slot call fell through to the host stub.\n\n",
                 what);
    std::fflush(stderr);
    std::abort();
}

}  // namespace

namespace IO {

// ---- IO::MemoryStream (src/engine/io/memory_stream.cpp) -------------------
// Retail slot 0 is the class's own virtual destructor (B3X W1; ENDGAME B.3.9 IO).
// The six slot bodies are the real ones: memory_stream.cpp stands on Stream.h
// alone and is linked (tests/unit/sources.cmake) for package_file_read_test.cpp
// and compressed_stream_bounds_test.cpp, and save_metadata_hardening_test.cpp (with Open /
// Create in compressedstream.cpp).

// ---- IO::FileStream (src/engine/io/file_stream.cpp + file_stream_write.cpp) --
// Slot 0 is PURE on this class, so there is nothing to stub for it.
System::String FileStream::GetSource() const     { StubReached("FileStream::GetSource"); }
bool     FileStream::Write(int, const void*)     { StubReached("FileStream::Write"); }
bool     FileStream::Read(int, void*)            { StubReached("FileStream::Read"); }
bool     FileStream::SetCurrentPos(int)          { StubReached("FileStream::SetCurrentPos"); }
uint32_t FileStream::GetCurrentPos() const       { StubReached("FileStream::GetCurrentPos"); }
uint32_t FileStream::GetSize() const             { StubReached("FileStream::GetSize"); }
bool     FileStream::Open(const System::Filename&) { StubReached("FileStream::Open"); }

// ---- IO::BinaryFileStream --------------------------------------------------
// Its ONE override, slot 0, whose real body is the registered thunk at
// 0x180023d80 in file_stream.cpp.
// ENDGAME B.3.9 IO: BinaryFileStream's slot 0 is its own destructor now (retail's
// vector deleting dtor). The three out-of-line lifecycle bodies below are
// file_stream.cpp's, which stands on the FileSystem singleton and the port
// registry; the host versions touch no file. ~FileStream's host body releases
// nothing because no probe opens a file or names one. The TextFileStream
// ctor makes retail's own-field stores but installs NO encoding and skips
// SetObject (the probes read neither).
FileStream::~FileStream() {}
TextFileStream::TextFileStream(const System::String&, const System::String&, int)
    : _objectName(), _objectType(), _requestedFormat(Text::Format::Detect),
      _activeEncoding(Text::Format::Detect), _buffer(nullptr), _bufSize(0), _bufCursor(0) {}
TextFileStream::~TextFileStream() {}

// ---- IO::CompressedStream / CompressedMemoryStream -------------------------
// The real bodies are linked (compressed_stream_{open,read,write,release}.cpp and
// compressedstream.cpp, since save_metadata_hardening_test.cpp drives
// LoadMetaData). CompressedFileStream's destructor stands on file_stream.cpp (the
// FileSystem singleton and the port registry); no probe opens a file, so its host
// body has nothing to release.
CompressedFileStream::~CompressedFileStream() {}

// compressed_stream_write.cpp's CompressedFileStream::Close reaches the FileSystem
// singleton and FileSystem::CloseFile (filesystem.cpp, not linked): both come from
// package_file_read_test.cpp's in-memory FileSystem fake; no metadata test closes a file.

// ---- IO::TextFileStream (src/engine/io/text_file_stream.cpp) ----------
// Slot 0 is PURE on this class. Slots 1 and 7 are FileStream's, above. What is
// left is the seven this class overrides or adds — note that slot 8 is
// `OpenWithFormat`, NOT a second `Open`: retail overloads `Open` across the
// base/derived boundary and B34_RULES section 1.3 gives the derived one its own
// name so the two slots cannot be confused.
bool     TextFileStream::Write(int, const void*) { StubReached("TextFileStream::Write"); }
bool     TextFileStream::Read(int, void*)        { StubReached("TextFileStream::Read"); }
bool     TextFileStream::SetCurrentPos(int)      { StubReached("TextFileStream::SetCurrentPos"); }
uint32_t TextFileStream::GetCurrentPos() const   { StubReached("TextFileStream::GetCurrentPos"); }
uint32_t TextFileStream::GetSize() const         { StubReached("TextFileStream::GetSize"); }
bool     TextFileStream::OpenWithFormat(const System::Filename&, Text::Format) {
    StubReached("TextFileStream::OpenWithFormat");
}
bool     TextFileStream::Create(const System::Filename&, Text::Format, Text::Format) {
    StubReached("TextFileStream::Create");
}

// ---- IO::VisitorStream (src/engine/io/iobase_vtables.cpp) -----------------
// The null sink. Slot 0 is PURE and slot 1 is IO::Stream's own GetSource, so
// these five are the whole class.
bool     VisitorStream::Write(int, const void*) { StubReached("VisitorStream::Write"); }
bool     VisitorStream::Read(int, void*)        { StubReached("VisitorStream::Read"); }
bool     VisitorStream::SetCurrentPos(int)      { StubReached("VisitorStream::SetCurrentPos"); }
uint32_t VisitorStream::GetCurrentPos() const   { StubReached("VisitorStream::GetCurrentPos"); }
uint32_t VisitorStream::GetSize() const         { StubReached("VisitorStream::GetSize"); }

// ---- IO::PackageFile (src/engine/io/package_file*.cpp) --------------------
// ENDGAME B.3.9 IO: the class is concrete, so the b39_io_packagefile test
// builds the REAL class and the link needs its vtable (key function
// GetFileVersion, in package_file.cpp, which stands on the port registry). The
// constructor and slot 0 are inline and real; the destructor's host body
// releases nothing because a freshly built package holds nothing. Slots 1/2
// are DECLARE_TYPE's inline bodies over the class's record (ENDGAME B.3.9
// TI-REST; src/engine/io/io_type_records.cpp is linked by sources.cmake).
int32_t      PackageFile::GetFileVersion() const       { StubReached("PackageFile::GetFileVersion"); }
Object::External::CompileResult PackageFile::Compile(const System::Filename&) {
    StubReached("PackageFile::Compile");
}
bool          PackageFile::Serialize(IO::Stream&)             { StubReached("PackageFile::Serialize"); }
Object::External* PackageFile::Reconstruct()            { StubReached("PackageFile::Reconstruct"); }
void          PackageFile::Destruct()                   { StubReached("PackageFile::Destruct"); }
Object::External* PackageFile::Construct()              { StubReached("PackageFile::Construct"); }
PackageFile::~PackageFile() {}

}  // namespace IO
