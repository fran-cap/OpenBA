// sprite_debug.cpp — Debug logging wrappers for UI::Sprite functions.
//
// These are NOT hooks or trampolines. They are call-site wrappers that log
// parameters and results around the real UI::Sprite constructors and IsValid.
// ENDGAME B.3.6 closing pass: UI::Sprite is a real value type (RULE B6-10), so
// the wrappers construct / test it as C++ instead of calling the engine_funcs
// ctor doors, which are retired.
//
// Usage: In debug builds, call Debug_Sprite_* instead of the UI::Sprite members
// to get printf logging of sprite construction and validation calls.

#include "sprite_debug.h"
#include "object/External.h"
#include "ui/Sprite.h"
#include <cstdio>
#include <cstdint>
#include <new>

// ============================================================
// Debug wrapper for UI::Sprite::Sprite(ExternalPtr<SpriteSheet>, const String&)
// Original: 0x18011b9c0
// Resolves sprite name to index via SpriteSheet::GetSpriteIndexFromName
// ============================================================
UI::Sprite* Debug_Sprite_Ctor(void* retval, UI::SpriteSheet* sheet, const System::String* name) {
#ifdef _DEBUG
    const char* nameStr = "(null)";
    if (name) {
        const char* dataPtr = name->_data;
        if (dataPtr) nameStr = dataPtr;
    }
    printf("[Sprite::Ctor] sheet=%p name=\"%s\" retval=%p\n",
           static_cast<void*>(sheet), nameStr, retval);
#endif
    UI::Sprite* const s = new (retval) UI::Sprite(
        Object::ExternalPtr<UI::SpriteSheet>(sheet), *name);
#ifdef _DEBUG
    printf("[Sprite::Ctor] done -> sheet=%p index=%d valid=%s\n",
           s->spriteSheet, s->spriteIndex, s->IsValid() ? "true" : "false");
#endif
    return s;
}

// ============================================================
// Debug wrapper for UI::Sprite::Sprite() — default constructor
// Original: 0x18011ba10
// Sets sheet=null, index=-1
// ============================================================
UI::Sprite* Debug_Sprite_DefaultCtor(void* retval) {
#ifdef _DEBUG
    printf("[Sprite::DefaultCtor] retval=%p\n", retval);
#endif
    UI::Sprite* const s = new (retval) UI::Sprite();
#ifdef _DEBUG
    printf("[Sprite::DefaultCtor] done -> sheet=%p index=%d\n",
           s->spriteSheet, s->spriteIndex);
#endif
    return s;
}

// ============================================================
// Debug wrapper for UI::Sprite::IsValid()
// Original: 0x180030c40
// Returns true if index >= 0 AND sheet != null
// ============================================================
bool Debug_Sprite_IsValid(const UI::Sprite* sprite) {
    const UI::Sprite* const s = sprite;
#ifdef _DEBUG
    bool result = s->IsValid();
    printf("[Sprite::IsValid] sprite=%p sheet=%p index=%d -> %s\n",
           sprite, s->spriteSheet, s->spriteIndex, result ? "true" : "false");
    return result;
#else
    return s->IsValid();
#endif
}
