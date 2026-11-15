#pragma once
// The C interface of libairconv.dylib that the Metal backend uses, declared here so the
// engine builds without the external converter source tree (LGPL, never linked, dlopened).
// Layouts and enum values mirror the converter's public header at AIRCONV_VERSION 26; the
// dylib is the only thing that has to match, not any header on this machine. If the
// converter's header is also included in a translation unit, that one wins and this file
// adds nothing.
// Source: DXMT airconv_public.h (LGPL-2.1, commit 50102f994b406f58f56d5d09f2ad20f9b90390a2);
// only the C ABI the dylib exports is declared here. See THIRD_PARTY_NOTICES.md.
#ifndef __AIRCONV_H
#define __AIRCONV_H

#include <cstddef>
#include <cstdint>

#define AIRCONV_VERSION 26

enum class SM50BindingType : uint32_t {
    ConstantBuffer,
    Sampler,
    SRV,
    UAV,
};

enum SM50_BINDING_INDEX : uint32_t {
    SM50_BINDING_INDEX_CONSTANT_BUFFER = 0,
    SM50_BINDING_INDEX_ARGUMENT_TABLE = 1,
};

enum MTL_SM50_SHADER_ARGUMENT_FLAG : uint32_t {
    MTL_SM50_SHADER_ARGUMENT_BUFFER = 1 << 0,
    MTL_SM50_SHADER_ARGUMENT_TEXTURE = 1 << 1,
    MTL_SM50_SHADER_ARGUMENT_ELEMENT_WIDTH = 1 << 2,
    MTL_SM50_SHADER_ARGUMENT_UAV_COUNTER = 1 << 3,
    MTL_SM50_SHADER_ARGUMENT_TEXTURE_MINLOD_CLAMP = 1 << 4,
    MTL_SM50_SHADER_ARGUMENT_TBUFFER_OFFSET = 1 << 5,
    MTL_SM50_SHADER_ARGUMENT_TEXTURE_ARRAY = 1 << 6,
    MTL_SM50_SHADER_ARGUMENT_READ_ACCESS = 1 << 10,
    MTL_SM50_SHADER_ARGUMENT_WRITE_ACCESS = 1 << 11,
};

struct MTL_SM50_SHADER_ARGUMENT {
    SM50BindingType Type;
    uint32_t SM50BindingSlot;
    MTL_SM50_SHADER_ARGUMENT_FLAG Flags;
    uint32_t StructurePtrOffset;
};

struct MTL_TESSELLATOR_REFLECTION {
    uint32_t Partition;
    float MaxFactor;
    uint32_t OutputPrimitive;
};

struct MTL_GEOMETRY_SHADER_PASS_THROUGH {
    uint8_t RenderTargetArrayIndexReg;
    uint8_t RenderTargetArrayIndexComponent;
    uint8_t ViewportArrayIndexReg;
    uint8_t ViewportArrayIndexComponent;
};

struct MTL_GEOMETRY_SHADER_REFLECTION {
    union {
        MTL_GEOMETRY_SHADER_PASS_THROUGH Data;
        uint32_t GSPassThrough;
    };
    uint32_t Primitive;
};

struct MTL_POST_TESSELLATOR_REFLECTION {
    uint32_t MaxPotentialTessFactor;
};

struct MTL_PIXEL_SHADER_REFLECTION {
    uint32_t ValidRenderTargets;
    uint32_t HasCoverageOutput;
};

struct MTL_SHADER_REFLECTION {
    uint32_t ConstanttBufferTableBindIndex;
    uint32_t ArgumentBufferBindIndex;
    uint32_t NumConstantBuffers;
    uint32_t NumArguments;
    union {
        uint32_t ThreadgroupSize[3];
        MTL_TESSELLATOR_REFLECTION Tessellator;
        MTL_GEOMETRY_SHADER_REFLECTION GeometryShader;
        MTL_POST_TESSELLATOR_REFLECTION PostTessellator;
        uint32_t PSValidRenderTargets;
        MTL_PIXEL_SHADER_REFLECTION PixelShader;
    };
    uint16_t ConstantBufferSlotMask;
    uint16_t SamplerSlotMask;
    uint64_t UAVSlotMask;
    uint64_t SRVSlotMaskHi;
    uint64_t SRVSlotMaskLo;
    uint32_t NumOutputElement;
    uint32_t ThreadsPerPatch;
    uint32_t ArgumentTableQwords;
};

// 64-bit only (arm64): the handles are plain pointers.
typedef void* sm50_ptr64_t;
typedef sm50_ptr64_t sm50_shader_t;
typedef sm50_ptr64_t sm50_bitcode_t;
typedef sm50_ptr64_t sm50_error_t;

struct SM50_COMPILED_BITCODE {
    sm50_ptr64_t Data;
    uint64_t Size;
};

enum SM50_SHADER_COMPILATION_ARGUMENT_TYPE {
    SM50_SHADER_COMMON = 2,
    SM50_SHADER_ARGUMENT_TYPE_MAX = 0xffffffff,
};

struct SM50_SHADER_COMPILATION_ARGUMENT_DATA {
    void* next;
    SM50_SHADER_COMPILATION_ARGUMENT_TYPE type;
};

enum SM50_SHADER_METAL_VERSION {
    SM50_SHADER_METAL_310 = 310,
    SM50_SHADER_METAL_320 = 320,
    SM50_SHADER_METAL_MAX = 0xffffffff,
};

enum SM50_SHADER_FLAG {
    SM50_SHADER_FLAG_SAMPLE_NAN_TO_ZERO = 1 << 0,
    SM50_SHADER_FLAG_DEFUSE_FMA = 1 << 1,
};

struct SM50_SHADER_COMMON_DATA {
    void* next;
    SM50_SHADER_COMPILATION_ARGUMENT_TYPE type;
    SM50_SHADER_METAL_VERSION metal_version;
    SM50_SHADER_FLAG flags;
};

// The entry points the backend resolves with dlsym (plain C ABI on arm64).
extern "C" {
typedef int (*SM50InitializeFn)(const void*, size_t, sm50_shader_t*, MTL_SHADER_REFLECTION*, sm50_error_t*);
typedef void (*SM50DestroyFn)(sm50_shader_t);
typedef int (*SM50CompileFn)(sm50_shader_t, SM50_SHADER_COMPILATION_ARGUMENT_DATA*, const char*, sm50_bitcode_t*,
                             sm50_error_t*);
typedef void (*SM50GetCompiledBitcodeFn)(sm50_bitcode_t, SM50_COMPILED_BITCODE*);
typedef void (*SM50DestroyBitcodeFn)(sm50_bitcode_t);
typedef size_t (*SM50GetErrorMessageFn)(sm50_error_t, char*, size_t);
typedef void (*SM50FreeErrorFn)(sm50_error_t);
typedef void (*SM50GetArgumentsInfoFn)(sm50_shader_t, MTL_SM50_SHADER_ARGUMENT*, MTL_SM50_SHADER_ARGUMENT*);
}

#endif
