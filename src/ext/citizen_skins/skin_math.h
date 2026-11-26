#pragma once
// ============================================================================
// src/ext/citizen_skins/skin_math.h -- ext.citizen_skins: the engine-free half
// (docs/extensions/handins/CITIZENSKINS.md; tests/unit/ext_citizen_skins_test.cpp)
// ============================================================================
// NOT RETAIL. A citizen's shirt colour is one of 8 cells of a 1024 x 128 texture strip (one strip for the
// men, one for the women): the citizen's variant 0..7 is written into its mesh colour and the character
// vertex program turns it into `u = (u + variant) / 8`. This half works on bytes only:
//
//   ReadTextureLevel0   a compiled Texture resource (a package's `CitizenTexture_resource.crs`, decompressed)
//                       -> its top level as straight RGBA, whatever its pixel format (BC1, BC3, BGRA8, grey)
//   BuildChain          N equal strips side by side -> one BGRA8 image with its full mip chain
//   PatchVertexBlob     the vertex program's one `l(0.125)` -> `l(1 / (8 N))`, the DXBC checksum made again
//   SetForName          which strip a citizen wears: the set whose identity scores highest with its name
//                       (highest random weight), nothing random, nothing saved; taking a set out of the
//                       list only moves the citizens who wore it
//   SetIdentity         a set's own number, from its pixels (not its place in the list)
//   ChooseSets          which packages' strips become sets: the first package's (the first mod's, once a mod
//                       has a citizen texture) are set 0, a package that only has one of the two wears the
//                       first's for the other, equal pixels count once,
//                       other sizes are left out, and no more than the limit or the image width allows
//   ParseStripName      a package entry's name -> male or female, and which set of the mod's it is (0 = the
//                       mod's own pair; 2..99 = a numbered extra pair: CitizenTexture2 + CitizenTextureF2)
//   SlotFor             set * 8 + variant, the number the mesh colour carries
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Ext {
namespace CitizenSkins {

constexpr int kCells = 8;      // the colours in one strip
constexpr int kMaxSets = 16;   // strips side by side: 128 colours, a 16384 x 128 image
// D3D11's 2D texture limit: the joined image is never wider. A device with a smaller limit refuses the widest
// image: OnGameStart then tries fewer sets (16, 8, 4, 2), because no backend answers its limit through the
// interface the extensions use.
constexpr int kMaxTextureSide = 16384;
// A mod's file says how big its strip is, so the size is bounded before anything is allocated: one strip is
// at most 2 M pixels (vanilla's is 128 K; 8 MB decoded) and the joined image at most 16 M (64 MB).
constexpr int kMaxStripPixels = 2 * 1024 * 1024;
constexpr int kMaxChainPixels = 16 * 1024 * 1024;

// One strip, straight RGBA8, row-major, top-down.
struct Strip {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba;
};

// The top level of a decompressed compiled Texture resource. False (and `why` says what) for a file this does
// not understand: it never guesses a pixel format, and it refuses a strip over kMaxStripPixels before
// decoding it. With `expectWidth` and `expectHeight` above 0 any other size is refused too, again before
// anything is allocated (the sets must all be one size).
bool ReadTextureLevel0(const uint8_t* data, size_t size, Strip* out, std::string* why, int expectWidth = 0,
                       int expectHeight = 0);

// BC1 and BC3 blocks to RGBA (the colour block's own 4-colour / 3-colour rule; BC3's alpha ramp).
void DecodeBc1(const uint8_t* blocks, int width, int height, uint8_t* rgba);
void DecodeBc3(const uint8_t* blocks, int width, int height, uint8_t* rgba);

// One image of `width x height` BGRA8 and every level below it (each level half the size in both directions,
// 1 at least, until 1 x 1: Video::ImageBuffer::GetLevelCount's ladder), a 2 x 2 box filter between levels.
struct Chain {
    int width = 0;
    int height = 0;
    std::vector<std::vector<uint8_t>> levels;   // BGRA8, level 0 first
};

// The strips side by side (all one size; an alpha of 255 everywhere: the citizens' pixel program does not
// draw a strip's alpha, see BuildChain). False for none, for strips of different
// sizes, and for a joined image wider than kMaxTextureSide or over kMaxChainPixels.
bool BuildChain(const std::vector<const Strip*>& strips, Chain* out, std::string* why);

// `sets` strips make `u = (u + slot) * (1 / (8 * sets))`: the one immediate in the vertex program that is
// 1/8. Returns the number of immediates changed (0 = this is not the program: no `mul ..., l(0.125)`, or more
// than one). The blob is left alone when it returns 0. The container is checked first: the checksum must be
// the one it carries, or the blob is not touched.
int PatchVertexBlob(std::vector<uint8_t>* blob, int sets);

// The same test without the change: how many `l(0.125)` immediates the blob holds (-1: not a DXBC container).
int CountEighths(const uint8_t* blob, size_t size);

// Whether `entry` (a package entry's path, any case, `/` or `\`) is the compiled resource of a citizen
// strip: `Models\Citizen\MaterialInstance\CitizenTexture[F][N]_resource.crs`. Returns -1 when it is not
// one; else the pair's number: 0 for the plain names (the mod's own, and the one retail reads), 2..99 for a
// numbered extra. `*female` says which of the two (F). A number of 1, 0 or with a leading zero is not a
// strip.
int ParseStripName(const char* entry, bool* female);

// A set's identity from the hashes of its two strips' pixels: the same pixels, the same number, whatever the
// set's place in the list or the mod's name.
uint64_t SetIdentity(uint64_t maleHash, uint64_t femaleHash);

// The strip a citizen wears, 0 .. sets-1: each set scores hash(name, identity) and the highest score wins
// (ties go to the earlier set). The same citizen wears the same set in every load; and when a set leaves the
// list (a mod is removed), only the citizens who wore it change: every other citizen's best score is still
// there. `ids` holds `sets` identities; with none, or one set, the answer is 0.
int SetForName(const char* name, const uint64_t* ids, int sets);

// One package's pair as read: its two strips (when it has them) and their pixel hashes.
struct PackSkin {
    Strip male;
    Strip female;
    bool hasMale = false;
    bool hasFemale = false;
    uint64_t hashMale = 0;
    uint64_t hashFemale = 0;
};

// A hash of a strip's pixels and size (what makes two strips "the same" and gives a set its identity).
uint64_t StripHash(const Strip& s);

// The sets that come of the packages, in order: set 0 is the first package's pair (the packages given are the
// mods' alone once any mod has a citizen texture: vanilla's is only the fallback), which must have both
// strips (else none: the feature is off). Later packages follow in the given order; a package with only one
// of the two wears the first package's for the other; a pair whose pixels are already taken adds nothing; a
// pair that is not the first package's size is left out; and the list stops at kMaxSets, or fewer when the
// joined image would be wider than kMaxTextureSide (the wider of the two first strips counts). The pointers
// point into `packs`, which must outlive the result. `notes` (may be null) gets one line per pair left out,
// for the log.
struct Sets {
    std::vector<const Strip*> male;
    std::vector<const Strip*> female;
    std::vector<uint64_t> ids;   // SetIdentity of each set, from its pixels
};
Sets ChooseSets(const std::vector<PackSkin>& packs, std::vector<std::string>* notes);

// The mesh colour's number: the set's first cell plus the citizen's own variant (0..7).
inline int SlotFor(int set, int variant) { return set * kCells + (variant & (kCells - 1)); }

}  // namespace CitizenSkins
}  // namespace Ext
