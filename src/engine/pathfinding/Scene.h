#pragma once
// ============================================================================
// src/engine/pathfinding/Scene.h — Pathfinding::Scene, 0x90 bytes,
//                                      NOT polymorphic
// ============================================================================
//
// ENDGAME B.3 step 7, block B6 (RULE B7-9): DECLARATIONS ONLY. Nineteen
// exports and no body anywhere in this tree. The class is DEAD in retail: the
// ctor (0x18010f660) and `GeneratePath` (0x18010dcf0) have no code reference
// in Runtime-x64.dll (only export / .pdata / unwind data) and the exe imports
// nothing of it (B37_RULES 6.2). The exe's pathfinder is its own
// `System::Thread` subclass (pathfinder_seed.cpp), not this.
//
// NOT three classes that happen to share a name: `Audio::Scene`,
// `Picking::Scene` and the Graphics scene are unrelated. Always qualify.
//
// LAYOUT, read as RETAIL disassembly this pass (Runtime-x64.dll, 15290 fns):
//
//   ??0Scene@Pathfinding@@QEAA@PEBVIPathInfo@1@M@Z   0x18010f660
//     MOV [RCX],RDX                          +0x00  const IPathInfo*
//     XMM1 = arg * [0x1801616d8]; CALL 0x18006ce10(this+8, XMM1)
//                                            +0x08  HashGrid<Node> ctor
//     +0x68, +0x70, +0x78, +0x80 = 0         two Array headers
//     dword [+0x88] = -1
//     float [+0x8c] = the float argument (XMM2)
//   ??0Scene@Pathfinding@@QEAA@AEBV01@@Z (copy)       0x18006ca70
//     +0x00 copied; 0x18006cb70(+0x08) (HashGrid copy); 0x18006fd60(+0x68)
//     and 0x18006fe90(+0x78) (two Array copies — DEEP, B7-8); dwords +0x88 and
//     +0x8c copied last. Nothing past +0x8f is touched.
//   GetNodes  0x18006ca50  LEA RAX,[RCX+0x08]  -> HashGrid<Node> const&
//   GetLinks  0x18006ca60  LEA RAX,[RCX+0x68]  -> Array<Link> const&
//
//   sizeof 0x90   the copy constructor copies every member and ends with the
//                 dword at +0x8c; with the leading pointer the class aligns to
//                 8, so 0x8c + 4 = 0x90 is the size. NO allocation site exists
//                 (the class is dead), so this is the copy ctor's bound, not an
//                 `Allocate(n)` — B37_RULES 13 item 4 is closed to that extent.
//
// `Utility::HashGrid<Node>` has no header in this tree and `Node`, `Link` and
// the element type at +0x78 were never read (dead code): the 0x60 grid and the
// +0x78 array are opaque bytes, named by offset (B6-8).
// ============================================================================

#include <cstdint>
#include <cstddef>

#include "../collection/Collection.h"
#include "../math/Math.h"

namespace Utility { template <class T> class HashGrid; }   // no header in this tree

namespace Pathfinding {

class IPathInfo;

class Scene {
public:
    // Layout-only nested types retail names in its mangled signatures
    // (`UNode@Scene@Pathfinding@@`, `ULink@Scene@Pathfinding@@`). Their fields
    // were not read; declared, not defined.
    struct Node;
    struct Link;

    // ---- the 19 exports, in B37_SLOTS.md section 6 order ---------------------
    // EVERY ONE is NOT PORTED and dead in retail: declared, deliberately
    // undefined (no caller in either binary or in this tree).
    const Utility::HashGrid<Node>& GetNodes() const;                  // 0x18006ca50
    const Collection::Array<Link>& GetLinks() const;                  // 0x18006ca60
    Scene(const Scene& other);                                        // 0x18006ca70  DEEP (B7-8)
    Scene& operator=(const Scene& other);                             // 0x18006cb00
    bool GeneratePath(const Math::Vector2& from, const Math::Vector2& to,
                      float radius, uint32_t mask,
                      Collection::Array<Math::Vector2>& out) const;   // 0x18010dcf0
    void SmoothPath(const Collection::Array<Math::Vector2>& in,
                    Collection::Array<Math::Vector2>& out, float radius); // 0x18010e140
    void Build();                                                     // 0x18010ece0
    void RemoveNode(const Math::Vector2& at);                         // 0x18010f0f0
    void AddNode(const Math::Vector2& at, uint32_t a, uint32_t b);    // 0x18010f4e0
    ~Scene();                                                         // 0x18010f5f0  QEAA
    Scene(const IPathInfo* pathInfo, float radius);                   // 0x18010f660

private:
    // retail `AEBA`/`AEAA` — private members, exported all the same
    // (reference_aeaa_is_private_not_unexported.md)
    bool GeneratePath(int32_t from, int32_t to, uint32_t mask,
                      Collection::Array<int32_t>& out) const;         // 0x18010cb70
    bool IsPassable(const Math::Vector2& a, const Math::Vector2& b) const; // 0x18010e6f0
    int32_t FindClosestNode(const Math::Vector2& at) const;           // 0x18010e720
    void BuildNode(int32_t index);                                    // 0x18010e980
    void AddLink(int32_t a, int32_t b, float cost);                   // 0x18010ecf0
    bool HasLink(int32_t a, int32_t b) const;                         // 0x18010eea0
    void RemoveLinks(int32_t index);                                  // 0x18010ef00
    void RemoveLinkFrom(int32_t a, int32_t b);                        // 0x18010efd0

public:
    // ---- the 0x90 bytes (public: the layout is the documentation) -----------
    const IPathInfo* _pathInfo;      // +0x00  ctor arg 1 (MOV [RCX],RDX)
    uint8_t  _nodes[0x60];           // +0x08  Utility::HashGrid<Node>, opaque
    uint8_t  _links[0x10];           // +0x68  Collection::Array<Link> header
    uint8_t  _searchRecords[0x10];         // +0x78  Collection::Array<?> header
    int32_t  _freeLink;                 // +0x88  ctor -1
    float    _radius;                // +0x8c  ctor arg 2 (XMM2), stored raw
};

static_assert(sizeof(Scene) == 0x90,
              "Pathfinding::Scene is 0x90 (copy ctor 0x18006ca70 ends at the +0x8c dword)");
static_assert(offsetof(Scene, _pathInfo) == 0x0, "Scene::_pathInfo");
static_assert(alignof(Scene) == 8, "Pathfinding::Scene aligns to 8 (the leading pointer)");
static_assert(offsetof(Scene, _nodes)   == 0x08, "GetNodes 0x18006ca50 is LEA [RCX+0x08]");
static_assert(offsetof(Scene, _links)   == 0x68, "GetLinks 0x18006ca60 is LEA [RCX+0x68]");
static_assert(offsetof(Scene, _searchRecords) == 0x78, "second Array copied by 0x18006fe90");
static_assert(offsetof(Scene, _freeLink)   == 0x88, "ctor stores -1 at +0x88");
static_assert(offsetof(Scene, _radius)  == 0x8c, "ctor stores its float at +0x8c");

}  // namespace Pathfinding
