// ============================================================================
// src/ext/better_clouds/sky_fit.h -- does our sky pixel program fit the installed sky? (pure)
// ============================================================================
// NOT RETAIL. docs/extensions/handins/BETTERCLOUDS.md. No D3D, no retail bytes: it reads the shader blobs
// it is handed (the user's installed sky vertex program at run time; the unit tests build their own).
//
// ext.better_clouds puts its own pixel program behind the installed sky vertex program. That is right
// only when every input ours reads is an output the installed one writes: the same semantic name and
// index (the Vulkan backend links stages by name), the same register, and at least the components ours
// declares. A mod's own sky vertex program that does not fit keeps the installed pixel program, and the
// sky stays as installed.
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Ext {
namespace BetterClouds {

// One element of a DXBC ISGN / OSGN chunk (D3D11_SIGNATURE_PARAMETER_DESC's stored fields).
struct SigElement {
    std::string name;
    uint32_t semanticIndex = 0;
    uint32_t systemValue = 0;     // D3D_NAME: 0 = none, 1 = position
    uint32_t componentType = 0;   // 1 uint32, 2 sint32, 3 float32
    uint32_t reg = 0;
    uint8_t mask = 0;
    uint8_t rwMask = 0;
};

// The elements of the blob's chunk `fourcc` ('ISGN' / 'OSGN'). False when the blob is not a DXBC
// container, has no such chunk, or the chunk is malformed.
bool ReadSignature(const uint8_t* blob, size_t size, uint32_t fourcc, std::vector<SigElement>* out);

// THE TEST above. `why` names the first input that did not fit.
bool PixelFitsVertex(const uint8_t* ourPs, size_t ourPsSize, const uint8_t* installedVs, size_t installedVsSize,
                     std::string* why);

}  // namespace BetterClouds
}  // namespace Ext
