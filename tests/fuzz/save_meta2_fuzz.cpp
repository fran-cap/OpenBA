// ============================================================================
// tests/fuzz/save_meta2_fuzz.cpp -- LoadMetaData's RECORD parse, behind a well-formed container
// ============================================================================
// The second target of P0_MAP rank 3a. save_meta_fuzz.cpp feeds LoadMetaData the embedded image as
// stored; its container defects (see the masks below) stop almost every mutated input before the
// BinaryReader record is reached. Here the fuzz input is the DECOMPRESSED record (0xaaaaaaaa,
// length, version, description String, width, height, w*h*4 thumbnail bytes, version, mod count,
// count x {String name, String path, 3 x u32}, 0xbbbbbbbb) and the target wraps it in a container
// built by OUR writer side (STORED blocks), so the fuzzer works on the description / thumbnail /
// mod-list parse: String::Serialize lengths, the w*h*4 product, the unbounded mod count.
//
// MASKS (each one hides a defect save_meta_fuzz.cpp already recorded):
//   SM-3 / SM-4  no block's compressedSize / uncompressedSize exceeds 0x8000 (the stack scratch and
//                the window buffer overflows in DecompressFromSource): every block is STORED with
//                uncompressedSize == 0x8000 exactly.
//   SM-7         the record is zero-padded to a whole number of 0x8000 blocks, so a read past the
//                record's end reads zeros and then FAILS at the missing next block instead of
//                spinning forever in CompressedStream::Read (chunk == window.end - pos == 0 when a
//                read crosses the end of a short last block).
//   SM-2 / SM-5  blockCount is ours, never negative and never overflowing blockCount*8.
//   SM-1         saveload_loadmeta.cpp is rebuilt without the signed-overflow check (MASK_SM1=1 in
//                the scratchpad build script), so the w*h*4 wrap reaches the allocator and the read.
// ============================================================================
#define SAVE_META_EMBEDDED 1
#include "save_meta_fuzz.cpp"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size > 0x20000)
        return 0;
    std::vector<uint8_t> plain(data, data + size);
    const size_t padded = ((size + 0x7fff) / 0x8000 + (size % 0x8000 == 0 ? 1 : 0)) * 0x8000;
    plain.resize(padded, 0);   // SM-7: every block is full, so no read can spin on a short last block
    const std::vector<uint8_t> img = Container(plain, true);   // SM-3 / SM-4: STORED, 0x8000 each
    RunOne(img.data(), img.size());
    return 0;
}
