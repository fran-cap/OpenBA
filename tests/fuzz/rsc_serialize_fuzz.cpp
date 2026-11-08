// ============================================================================
// tests/fuzz/rsc_serialize_fuzz.cpp -- binary .rsc resource Serialize bodies over arbitrary bytes
// ============================================================================
// P0_MAP input surface rank 7: compiled resources are read by each class's Serialize through an
// IO::Serializer whose ReaderWriter is an IO::BinaryReader over the stream the package hands it.
// Here the stream is an IO::MemoryStream holding the input after the selector byte, in a heap
// buffer of exactly that size (so ASan sees a read past the record's end).
//
//   data[0] % kModes picks the body:
//     0  IO::PackageFile::Serialize                     (package_file_serialize.cpp)
//     1  Audio::SoundBuffer::Serialize                  (vtbatch7_soundbuffer_soundeffect.cpp,
//                                                        PlatformSerialize + the xWMA decode at load)
//     2  Video::ImageBuffer::Serialize                  (imagebuffer_vtable.cpp + PlatformSerialize;
//                                                        Create overridden, see FuzzImageBuffer)
//     3  Audio::SoundEffect::Serialize                  (counted Sample / Variant arrays)
//     4  Achievement::Achievement::Serialize            (achievement_serialize.cpp)
//     5  GraphicsMesh's node block + Array<Transform>   (animationstate.cpp: the shape of
//                                                        graphicsmesh_vtable.cpp:521-525, then
//                                                        RtGraphics_SerializeTransformArray)
//     6  RtGraphics_SerializeAnimInstances              (animationstate.cpp:323-357)
//
// HARNESS MODEL (not retail): ExternalPtrSerializeBody is replaced below. The real body
// (external_ptr_serialize.cpp) resolves the path through ExternalFactory::_singleton, which a
// standalone target has no instance of; the stand-in reads the same records of the non-reference
// mode (bool present, then the path String) and resolves nothing, which is the "not found" exit.
// It lets the counted arrays behind an ExternalPtr (SoundEffect variants, Achievement goals,
// animation layers / instances) be reached.
//
// Seeds: `fuzz_rsc_serialize -seedgen=<dir>` writes one record per mode with OUR writers (the
// same Serialize bodies on a writing MemoryStream) and exits. No retail content.
// ============================================================================
#include "io/Serializer.h"
#include "io/Stream.h"
#include "io/PackageFile.h"
#include "audio/SoundBuffer.h"
#include "audio/SoundEffect.h"
#include "achievement/AchievementManager.h"
#include "video/ImageBuffer.h"
#include "graphics/GraphicsMesh.h"
#include "graphics/AnimationState.h"
#include "graphics/AnimationManager.h"
#include "object/External.h"
#include "system/String.h"
#include "system/Filename.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// ---- the ExternalPtr stand-in (see the banner) ------------------------------------------------
namespace Object {
bool ExternalPtrSerializeBody(External* cur, IO::Serializer& ser, const BaseTypeInfo& typeInfo,
                              External*& loaded) {
    (void)typeInfo;
    bool present = cur != nullptr;
    ser._readerWriter->SerializeBool(present, nullptr);
    if (present) {
        System::String path("x");
        path.Serialize(ser);
    }
    loaded = nullptr;
    return ser._stream->_direction == IO::kReading;
}
}  // namespace Object

// ---- link stand-ins (not retail; nothing below is reached by a Serialize body) ----------------
// ~SoundBuffer (batchc_audio_leaves.cpp) notifies Audio::Device::_singleton, which a standalone
// target has none of; the stand-in is the rest of that body.
Audio::SoundBuffer::~SoundBuffer() {
    EnsureDecodedAtLoad();
    if (_data != nullptr) rtcoll::MemFree(_data);
    if (_wmaData != nullptr) rtcoll::MemFree(_wmaData);
}

// buffers.cpp's ImageBuffer members reach Video::Device::_singleton (PlatformConstruct /
// PlatformDestruct); these are its field stores without the device (the unit tests' recording
// stand-in shape, ImageBuffer.h's InitFields banner).
namespace Video {
ImageBuffer::~ImageBuffer() { ReleaseFields(); }
void ImageBuffer::InitFields() {
    _width = 0;
    _height = 0;
    _dimension = 0;
    _kind = 2;
    _format = 0;
    _flags = 0;
    _addressU = 0;
    _addressV = 0;
    _addressW = 0;
    _filter = 1;
}
void ImageBuffer::ReleaseFields() {}
bool ImageBuffer::Create(ImageType type, Usage usage, Format format, uint32_t flags, int32_t width,
                         int32_t height, FilterMode filter, AddressMode addressU,
                         AddressMode addressV, AddressMode addressW, LevelArray*) {
    _dimension = type;
    _width = width;
    _kind = usage;
    _height = height;
    _format = format;
    _flags = flags;
    _filter = filter;
    _addressU = addressU;
    _addressV = addressV;
    _addressW = addressW;
    return true;
}
}  // namespace Video

// Factory rows of the type-record TUs the archive pulls in, and globals of bodies never called.
#define FZ_NULL_FN(sym) extern "C" void* sym() { return nullptr; }
FZ_NULL_FN(_ZN11Achievement12GoalInstance15CreateNewObjectEv)
FZ_NULL_FN(_ZN11Achievement19AchievementInstance15CreateNewObjectEv)
FZ_NULL_FN(_ZN11Achievement4Goal15CreateNewObjectEv)
FZ_NULL_FN(_ZN11Achievement4Goal19CreateInPlaceObjectEPv)
FZ_NULL_FN(_ZN11Achievement11Achievement15CreateNewObjectEv)
FZ_NULL_FN(_ZN11Achievement11Achievement19CreateInPlaceObjectEPv)
FZ_NULL_FN(_ZN2IO11PackageFile15CreateNewObjectEv)
FZ_NULL_FN(_ZN2IO11PackageFile19CreateInPlaceObjectEPv)
FZ_NULL_FN(_ZN5Video11ImageBuffer15CreateNewObjectEv)
FZ_NULL_FN(_ZN5Video11ImageBuffer19CreateInPlaceObjectEPv)
FZ_NULL_FN(_ZN5Video11RenderState15CreateNewObjectEv)
FZ_NULL_FN(_ZN5Video11RenderState19CreateInPlaceObjectEPv)
FZ_NULL_FN(_ZN5Video12PixelProgram15CreateNewObjectEv)
FZ_NULL_FN(_ZN5Video12PixelProgram19CreateInPlaceObjectEPv)
FZ_NULL_FN(_ZN5Video12VertexLayout15CreateNewObjectEv)
FZ_NULL_FN(_ZN5Video12VertexLayout19CreateInPlaceObjectEPv)
FZ_NULL_FN(_ZN5Video12VideoProgram15CreateNewObjectEv)
FZ_NULL_FN(_ZN5Video12VideoProgram19CreateInPlaceObjectEPv)
FZ_NULL_FN(_ZN5Video13ShaderProgram15CreateNewObjectEv)
FZ_NULL_FN(_ZN5Video13ShaderProgram19CreateInPlaceObjectEPv)
FZ_NULL_FN(_ZN5Video13VertexProgram15CreateNewObjectEv)
FZ_NULL_FN(_ZN5Video13VertexProgram19CreateInPlaceObjectEPv)
FZ_NULL_FN(_ZN6Object12ExternalList15CreateNewObjectEv)
FZ_NULL_FN(_ZN6Object12ExternalList19CreateInPlaceObjectEPv)
FZ_NULL_FN(_ZN8Graphics12GraphicsMesh15CreateNewObjectEv)
FZ_NULL_FN(_ZN8Graphics12GraphicsMesh19CreateInPlaceObjectEPv)
FZ_NULL_FN(_ZN8Graphics14AnimationEvent15CreateNewObjectEv)
FZ_NULL_FN(_ZN8Graphics14AnimationEvent19CreateInPlaceObjectEPv)
FZ_NULL_FN(_ZN8Graphics14AnimationGroup15CreateNewObjectEv)
FZ_NULL_FN(_ZN8Graphics14AnimationGroup19CreateInPlaceObjectEPv)
FZ_NULL_FN(_ZN8Graphics14ParticleSystem15CreateNewObjectEv)
FZ_NULL_FN(_ZN8Graphics14ParticleSystem19CreateInPlaceObjectEPv)
FZ_NULL_FN(_ZN8Graphics16MaterialInstance15CreateNewObjectEv)
FZ_NULL_FN(_ZN8Graphics16MaterialInstance19CreateInPlaceObjectEPv)
FZ_NULL_FN(_ZN8Graphics20FrameCopyImageBuffer15CreateNewObjectEv)
FZ_NULL_FN(_ZN8Graphics20FrameCopyImageBuffer19CreateInPlaceObjectEPv)
FZ_NULL_FN(_ZN8Graphics20GeneratedImageBuffer15CreateNewObjectEv)
FZ_NULL_FN(_ZN8Graphics20GeneratedImageBuffer19CreateInPlaceObjectEPv)
FZ_NULL_FN(_ZN8Graphics21ReflectionImageBuffer15CreateNewObjectEv)
FZ_NULL_FN(_ZN8Graphics21ReflectionImageBuffer19CreateInPlaceObjectEPv)
FZ_NULL_FN(_ZN8Graphics8EdgeMesh15CreateNewObjectEv)
FZ_NULL_FN(_ZN8Graphics8EdgeMesh19CreateInPlaceObjectEPv)
FZ_NULL_FN(_ZN8Graphics8Material15CreateNewObjectEv)
FZ_NULL_FN(_ZN8Graphics8Material19CreateInPlaceObjectEPv)
FZ_NULL_FN(_ZN8Graphics8Particle15CreateNewObjectEv)
FZ_NULL_FN(_ZN8Graphics8Particle19CreateInPlaceObjectEPv)
// Called only on a copy (Node copy ctor door) / by AchievementManager::Persist / by Error::Display.
FZ_NULL_FN(_ZN10rtpickgrid20TransformArrayAssignEPN10Collection5ArrayIN4Math9TransformENS0_16DefaultAllocatorENS0_11DefaultSpecIS3_EEEEPKS7_)
FZ_NULL_FN(_ZN11Achievement18AchievementManager9RemoveAllEv)
FZ_NULL_FN(_ZN6Object24InternalPtrIsDerivedFromEPKNS_8InternalEj)
FZ_NULL_FN(_ZN6Object28InternalBasePtrSerializeBodyEPPNS_8InternalERN2IO10SerializerE)
FZ_NULL_FN(_ZN8Platform6Window16ShowErrorMessageEPKtS2_)
FZ_NULL_FN(_ZN11Achievement11Achievement11ReconstructEv)
FZ_NULL_FN(_ZN11Achievement11Achievement8DestructEv)
FZ_NULL_FN(_ZN11Achievement11Achievement9ConstructEv)
FZ_NULL_FN(_ZNK11Achievement11Achievement14CreateInstanceEv)
FZ_NULL_FN(_ZNK11Achievement11Achievement14GetFileVersionEv)
// achievement_manager.cpp:298 (the whole body; that TU drags the FileSystem and the GoalList).
Achievement::Achievement::~Achievement() {}
// Data: only their address is taken (the ExternalPtr stand-in ignores the type record).
extern "C" {
alignas(16) unsigned char _ZN2UI11SpriteSheet9_typeInfoE[256];
alignas(16) unsigned char _ZN2UI11StringTable9_typeInfoE[256];
void* _ZN8Graphics16AnimationManager10_singletonE = nullptr;
}
#undef FZ_NULL_FN

namespace {

constexpr int kModes = 7;

// FuzzImageBuffer: Create is the backend's (DX11 / Vulkan / Metal); the stand-in records the
// format and checks the two DX11 table indices (render/dx11/imagebufferdata.cpp:212/610/634,
// `kFormatToDxgi[8]` / `kFormatBytes[ib->_format]`) the way that backend would use them.
int g_lastFormat = 0;
bool g_checkDx11Index = false;

class FuzzImageBuffer : public Video::ImageBuffer {
public:
    bool Create(ImageType, Usage, Format format, uint32_t, int32_t, int32_t, FilterMode,
                AddressMode, AddressMode, AddressMode, LevelArray*) override {
        g_lastFormat = format;
        if (g_checkDx11Index && static_cast<uint32_t>(format) >= 8u) {
            std::fprintf(stderr, "DX11 kFormatToDxgi[%u]: index past the 8-entry table\n",
                         static_cast<uint32_t>(format));
            std::abort();
        }
        return true;
    }
};

// A MemoryStream over `buf` (not owned), reading or writing.
struct Mem {
    IO::MemoryStream ms;
    Mem(void* buf, uint32_t size, int direction) {
        ms._data = buf;
        ms._ownsData = 0;
        ms._cursor = 0;
        ms._size = size;
        ms._direction = direction;
    }
    ~Mem() { ms._data = nullptr; }
};

void RunNodesAndTransforms(IO::Stream& stream) {
    IO::Serializer ser(stream, 0);
    IO::ReaderWriter* rw = ser._readerWriter;
    const bool reading = stream._direction == IO::kReading;
    Graphics::GraphicsMesh::NodeArray nodes;
    {
        // graphicsmesh_vtable.cpp:521-525, transcribed: GraphicsMesh::Serialize's node block.
        int32_t n = nodes._size;
        rw->SerializeInt(n, nullptr);
        if (reading) rtcoll::Rt_SetSize_18005aa30(&nodes, n);
        RtGraphics_SerializeMeshNodes(ser, n, nodes._data);
    }
    Collection::Array<Math::Transform> keys;
    RtGraphics_SerializeTransformArray(&keys, ser);   // AnimationGroup's keys (vtbatch7_animgroup:259)
    rtcoll::Rt_ArrayDtor_180014090(&nodes);
}

void RunAnimInstances(IO::Stream& stream) {
    IO::Serializer ser(stream, 0);
    // The caller's count: the one record before the element loop, as every Array<T>::Serialize.
    int32_t n = 0;
    ser._readerWriter->SerializeInt(n, nullptr);
    if (n < 0 || n > 8) return;   // harness bound on the OUTER array only (allocated here)
    if (n == 0) return;
    std::vector<Graphics::AnimationManager::Instance> inst(static_cast<size_t>(n));
    std::memset(static_cast<void*>(inst.data()), 0, inst.size() * sizeof(inst[0]));
    RtGraphics_SerializeAnimInstances(ser, n, inst.data());
    for (auto& i : inst) {
        rtcoll::Rt_ArrayDtor_180014090(&i._nodes);
        rtcoll::Rt_ArrayDtor_18005bbb0(&i._states);
    }
}

void RunMode(int mode, IO::Stream& stream) {
    switch (mode) {
    case 0: {
        IO::PackageFile pf;
        pf.Serialize(stream);
        break;
    }
    case 1: {
        Audio::SoundBuffer sb;
        sb.Serialize(stream);
        break;
    }
    case 2: {
        FuzzImageBuffer ib;
        ib.Serialize(stream);
        break;
    }
    case 3: {
        Audio::SoundEffect se;
        se.Serialize(stream);
        break;
    }
    case 4: {
        Achievement::Achievement a;
        a.Serialize(stream);
        break;
    }
    case 5:
        RunNodesAndTransforms(stream);
        break;
    case 6:
        RunAnimInstances(stream);
        break;
    }
}

// ---- seed generation: our writers ------------------------------------------------------------
void WriteSeed(const char* dir, int mode, int variant, void (*fill)(IO::Stream&)) {
    std::vector<uint8_t> buf(1 << 16, 0);
    uint32_t used = 0;
    {
        Mem m(buf.data(), static_cast<uint32_t>(buf.size()), IO::kWriting);
        fill(m.ms);
        used = m.ms._cursor;
    }
    std::string path = std::string(dir) + "/seed_" + std::to_string(mode) + "_" + std::to_string(variant);
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    const uint8_t sel = static_cast<uint8_t>(mode);
    std::fwrite(&sel, 1, 1, f);
    std::fwrite(buf.data(), 1, used, f);
    std::fclose(f);
    std::printf("seed %s: %u bytes\n", path.c_str(), used + 1);
}

void SeedGen(const char* dir) {
    WriteSeed(dir, 0, 0, [](IO::Stream& s) {
        IO::PackageFile pf;
        pf._name.Set("pkg");
        pf._author.Set("me");
        pf._description.Set("desc");
        pf._includeList.SetSize(2);
        pf._includeList._data[0].Set("a");
        pf._includeList._data[1].Set("bb");
        pf._excludeList.SetSize(1);
        pf._excludeList._data[0].Set("ccc");
        pf._iconWidth = 2;
        pf._iconHeight = 2;
        pf._iconData.SetSize(4);
        for (int i = 0; i < 4; ++i) pf._iconData._data[i] = 0x11223344u * i;
        pf.Serialize(s);
    });
    WriteSeed(dir, 1, 0, [](IO::Stream& s) {
        Audio::SoundBuffer sb;
        sb._streamPath.Set("s");
        sb._durationSeconds = 1.0f;
        // a PCM-shaped format (tag 1, 1 channel, 22050 Hz, 16-bit): no decode at load
        uint8_t fmt[0x28] = {1, 0, 1, 0, 0x22, 0x56, 0, 0, 0x44, 0xac, 0, 0, 2, 0, 16, 0};
        std::memcpy(sb._format, fmt, sizeof fmt);
        static uint8_t pcm[64];
        static uint32_t none[1];
        sb._data = pcm;
        sb._length = 64;
        sb._wmaData = none;   // length 0, but not null: Write(0, nullptr) is F-rsc-1 (memcpy null)
        sb.Serialize(s);
        sb._data = nullptr;
        sb._length = 0;
        sb._wmaData = nullptr;
    });
    WriteSeed(dir, 1, 1, [](IO::Stream& s) {
        Audio::SoundBuffer sb;
        // an xWMA-shaped format (tag 0x161, 2 ch, 44100, blockAlign 0x2000) + a 2-packet seek table
        uint8_t fmt[0x28] = {0x61, 1, 2, 0, 0x44, 0xac, 0, 0, 0x10, 0x27, 0, 0, 0, 0x20, 16, 0};
        std::memcpy(sb._format, fmt, sizeof fmt);
        static uint32_t seek[2] = {4096, 8192};
        static uint8_t pkt[0x4000];
        sb._wmaData = seek;
        sb._wmaLength = 8;
        sb._data = pkt;
        sb._length = sizeof pkt;
        sb.Serialize(s);
        sb._wmaData = nullptr;
        sb._wmaLength = 0;
        sb._data = nullptr;
        sb._length = 0;
    });
    WriteSeed(dir, 2, 0, [](IO::Stream& s) {
        FuzzImageBuffer ib;
        ib._width = 4;
        ib._height = 4;
        ib._kind = 2;
        ib._format = 3;
        ib.Serialize(s);   // the writing path emits a zero level count (PlatformSerialize)
    });
    WriteSeed(dir, 3, 0, [](IO::Stream& s) {
        Audio::SoundEffect se;
        se._samples.SetSize(2);
        se._samples._data[0].variants.SetSize(1);
        se._samples._data[1].variants.SetSize(2);
        se.Serialize(s);
    });
    WriteSeed(dir, 4, 0, [](IO::Stream& s) {
        Achievement::Achievement a;
        a._title.Set("t");
        a._description.Set("d");
        a._goals.SetSize(2);
        a.Serialize(s);
    });
    WriteSeed(dir, 5, 0, [](IO::Stream& s) {
        IO::Serializer ser(s, 0);
        IO::ReaderWriter* rw = ser._readerWriter;
        Graphics::GraphicsMesh::NodeArray nodes;
        rtcoll::Rt_SetSize_18005aa30(&nodes, 2);
        nodes._data[0]._name.Set("root");
        nodes._data[1]._name.Set("leaf");
        int32_t n = nodes._size;
        rw->SerializeInt(n, nullptr);
        RtGraphics_SerializeMeshNodes(ser, n, nodes._data);
        Collection::Array<Math::Transform> keys;
        keys.SetSize(3);
        RtGraphics_SerializeTransformArray(&keys, ser);
        rtcoll::Rt_ArrayDtor_180014090(&nodes);
    });
    WriteSeed(dir, 6, 0, [](IO::Stream& s) {
        IO::Serializer ser(s, 0);
        int32_t n = 1;
        ser._readerWriter->SerializeInt(n, nullptr);
        std::vector<Graphics::AnimationManager::Instance> inst(1);
        std::memset(static_cast<void*>(inst.data()), 0, sizeof(inst[0]));
        rtcoll::Rt_SetSize_18005b1e0(&inst[0]._states, 1);
        rtcoll::Rt_SetSize_18005c8a0(&inst[0]._states._data[0]._state._layers, 1);
        rtcoll::Rt_SetSize_18005ac50(&inst[0]._states._data[0]._nodes, 2);
        rtcoll::Rt_SetSize_18005aa30(&inst[0]._nodes, 1);
        RtGraphics_SerializeAnimInstances(ser, n, inst.data());
        rtcoll::Rt_ArrayDtor_180014090(&inst[0]._nodes);
        rtcoll::Rt_ArrayDtor_18005bbb0(&inst[0]._states);
    });
}

}  // namespace

extern "C" int LLVMFuzzerInitialize(int* argc, char*** argv) {
    for (int i = 1; i < *argc; ++i) {
        if (std::strncmp((*argv)[i], "-seedgen=", 9) == 0) {
            SeedGen((*argv)[i] + 9);
            std::exit(0);
        }
        if (std::strcmp((*argv)[i], "-dx11_index") == 0) g_checkDx11Index = true;
    }
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 1 || size > 0x40000)
        return 0;
    const int mode = data[0] % kModes;
    const uint32_t n = static_cast<uint32_t>(size - 1);
    uint8_t* body = static_cast<uint8_t*>(std::malloc(n ? n : 1));
    if (n) std::memcpy(body, data + 1, n);
    {
        Mem m(body, n, IO::kReading);
        RunMode(mode, m.ms);
    }
    std::free(body);
    return 0;
}
