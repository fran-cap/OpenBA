#include "io/FileSystem.h"
// CompressedStream::Close (compressed_stream_write.cpp:824) names the FileSystem; the PoC never closes a file.
IO::FileSystem* IO::FileSystem::_singleton = nullptr;
void IO::FileSystem::CloseFile(RtIO::FileInfo&) {}
