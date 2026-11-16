// ============================================================================
// src/engine/ui/ui_operator_delete.cpp — the 60 UI class `operator delete`s
// ============================================================================
//
// `??3<Class>@UI@@SAXPEAX@Z` — MSVC's CLASS-SCOPED `operator delete(void*)`,
// emitted once per UI class that has a virtual destructor. Sixty of them are
// reachable from our call surface (they are the scalar arm of every
// `??_E<Class>@UI@@` deleting destructor, and the failure arm of every
// `CreateNewObject`), and not one was ported.
//
// THEY ARE ONE FUNCTION, SIXTY TIMES. Not "similar" — measured. All sixty are
// 93 bytes; all sixty were read out of the shipped DLL and compared byte for
// byte against each other; every difference is inside a RIP-relative
// displacement, and when those five displacements are resolved to absolute
// addresses all sixty name the SAME five targets:
//
//     0x1801fc718  ?_singleton@MemoryAllocator@System@@0PEAV12@EA
//     0x18014b390  KERNEL32!EnterCriticalSection   (the IAT slot)
//     0x1800c6c00  MemoryAllocator::Free
//     0x18014b388  KERNEL32!LeaveCriticalSection   (the IAT slot)
//
//     ??3X(void* p):
//         if (!p) return;                        // TEST RCX,RCX ; JZ ret
//         cs = *_singleton + 0x640;
//         EnterCriticalSection(cs);
//         Free(*_singleton, p);
//         LeaveCriticalSection(cs);
//
// That is instruction for instruction `?MemFree@MemoryAllocator@System@@SAXPEAX@Z`
// (0x1800c7ad0), which was disassembled beside them for this file and differs
// only in where it sits.
//
// The null test is retail's own and it lives INSIDE 0x1800c7ad0 (`TEST RCX,RCX ;
// JZ ret` is its first instruction).
//
// WHAT IS NOT HERE: `??2<Class>@UI@@SAPEAX_K@Z`, the matching class `operator
// new`. Only one of those is in this batch (`??2ComboDialog@UI@@`, 0x180074770)
// and it is ported in src/engine/ui/combodialog_ctor.cpp beside the ComboDialog
// constructor; its caller is src/engine/ui/comboelement.cpp.
// ============================================================================

// B3R W0-A0 (R3): the 60 rows that registered RtUi_ClassOperatorDelete under these
// names and addresses had NO reader by name or by address anywhere in the tree
// (code lines, `ull`-suffixed and padded spellings included), so they are
// deleted. B.4 R0 deleted the body too (`RtUi_ClassOperatorDelete`, a one-line
// door onto `System::MemoryAllocator::MemFree`, which had no caller); the table
// survives as the record of what was read from the shipped DLL's export
// directory:
//
//   0x180036760  ??3AnimatedImageDescription@UI@@SAXPEAX@Z
//   0x180036b40  ??3AnimatedImageElement@UI@@SAXPEAX@Z
//   0x180040a70  ??3BorderDescription@UI@@SAXPEAX@Z
//   0x180041140  ??3BorderElement@UI@@SAXPEAX@Z
//   0x180047ec0  ??3ButtonDescription@UI@@SAXPEAX@Z
//   0x1800482f0  ??3ButtonElement@UI@@SAXPEAX@Z
//   0x1800374e0  ??3CharacterSet@UI@@SAXPEAX@Z
//   0x180048710  ??3CheckDescription@UI@@SAXPEAX@Z
//   0x180048b10  ??3CheckElement@UI@@SAXPEAX@Z
//   0x180037a20  ??3ClipRegionDescription@UI@@SAXPEAX@Z
//   0x180038000  ??3ClipRegionElement@UI@@SAXPEAX@Z
//   0x180074b70  ??3ComboDescription@UI@@SAXPEAX@Z
//   0x1800747f0  ??3ComboDialog@UI@@SAXPEAX@Z
//   0x180074fc0  ??3ComboElement@UI@@SAXPEAX@Z
//   0x180039960  ??3ContainerDescription@UI@@SAXPEAX@Z
//   0x180039d50  ??3ContainerElement@UI@@SAXPEAX@Z
//   0x18003b010  ??3Cursor@UI@@SAXPEAX@Z
//   0x1800384d0  ??3Dialog@UI@@SAXPEAX@Z
//   0x18003b990  ??3DocumentTextDescription@UI@@SAXPEAX@Z
//   0x18003bee0  ??3DocumentTextElement@UI@@SAXPEAX@Z
//   0x18003f760  ??3EditDescription@UI@@SAXPEAX@Z
//   0x18003fb70  ??3EditElement@UI@@SAXPEAX@Z
//   0x180031a50  ??3Font@UI@@SAXPEAX@Z
//   0x180040120  ??3GraphDescription@UI@@SAXPEAX@Z
//   0x1800404f0  ??3GraphElement@UI@@SAXPEAX@Z
//   0x1800466f0  ??3GroupDescription@UI@@SAXPEAX@Z
//   0x180046c40  ??3GroupElement@UI@@SAXPEAX@Z
//   0x180035d60  ??3ImageDescription@UI@@SAXPEAX@Z
//   0x180036340  ??3ImageElement@UI@@SAXPEAX@Z
//   0x180042440  ??3LabelDescription@UI@@SAXPEAX@Z
//   0x180042880  ??3LabelElement@UI@@SAXPEAX@Z
//   0x180042dd0  ??3LayoutDescription@UI@@SAXPEAX@Z
//   0x180043320  ??3LayoutElement@UI@@SAXPEAX@Z
//   0x180044c30  ??3ListDescription@UI@@SAXPEAX@Z
//   0x180045020  ??3ListElement@UI@@SAXPEAX@Z
//   0x18004a2d0  ??3NumberDescription@UI@@SAXPEAX@Z
//   0x180045f20  ??3NumberEditDescription@UI@@SAXPEAX@Z
//   0x1800462e0  ??3NumberEditElement@UI@@SAXPEAX@Z
//   0x18004a8b0  ??3NumberElement@UI@@SAXPEAX@Z
//   0x180046f70  ??3PageDescription@UI@@SAXPEAX@Z
//   0x180047300  ??3PageElement@UI@@SAXPEAX@Z
//   0x180077800  ??3ProgressDescription@UI@@SAXPEAX@Z
//   0x180077dd0  ??3ProgressElement@UI@@SAXPEAX@Z
//   0x180047700  ??3RadioDescription@UI@@SAXPEAX@Z
//   0x180047a90  ??3RadioElement@UI@@SAXPEAX@Z
//   0x1800441c0  ??3RibbonDescription@UI@@SAXPEAX@Z
//   0x180044770  ??3RibbonElement@UI@@SAXPEAX@Z
//   0x18003a1b0  ??3ScrollDescription@UI@@SAXPEAX@Z
//   0x18003a8e0  ??3ScrollElement@UI@@SAXPEAX@Z
//   0x1800497e0  ??3SliderDescription@UI@@SAXPEAX@Z
//   0x180049db0  ??3SliderElement@UI@@SAXPEAX@Z
//   0x18004adf0  ??3SpinnerDescription@UI@@SAXPEAX@Z
//   0x18004b1a0  ??3SpinnerElement@UI@@SAXPEAX@Z
//   0x18003c8a0  ??3SpriteCapture@UI@@SAXPEAX@Z
//   0x18004c280  ??3TabDescription@UI@@SAXPEAX@Z
//   0x18004c790  ??3TabElement@UI@@SAXPEAX@Z
//   0x180078f60  ??3TableDescription@UI@@SAXPEAX@Z
//   0x180079360  ??3TableElement@UI@@SAXPEAX@Z
//   0x180041b60  ??3TextDescription@UI@@SAXPEAX@Z
//   0x180041f80  ??3TextElement@UI@@SAXPEAX@Z
