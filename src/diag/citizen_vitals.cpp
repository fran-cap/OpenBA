// src/diag/citizen_vitals.cpp — periodic citizen vitals census.
//
// "The citizens froze and then died" needs a cause, and the death branches in
// CitizenComponent::Update pick between four of them by field value:
//   +0x3C age    vs  +0x40 maxAge   -> HandleLifeEvent(2)  old age
//   +0x44 hunger vs  desc+0xD0 cap  -> HandleLifeEvent(0)  starvation
//   +0x48 thirst vs  desc+0xF0 cap  -> HandleLifeEvent(1)  thirst
//   health                          -> HandleLifeEvent(3)
// Burials all logged flags=0x80 ("death with no grave, eventType != 2"), which
// already rules out old age — so this samples the two accumulators directly and
// prints one aggregate line, letting a load run be diffed against a new game.
//
// Reads only the TownManager citizen registry (gameWorld+0xF0, array +0xA0 /
// count +0xA8) and four floats per citizen; it never writes.

#include "citizen_vitals.h"
#include "options.h"
#include "Game/GameUpdateLoop.h"            // B4: m_citizenList / m_entityList
#include "Entity/CitizenBehavior.h"
#include "Entity/EntityList.h"
#include "Component/CitizenComponent.h"
#include "Component/CharacterComponent.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {

bool Enabled() {
    return Opt().citizenVitals;          // BANISHED_VITALS=1
}

bool Readable(const void* p) {
    const uintptr_t a = reinterpret_cast<uintptr_t>(p);  // CAST-OK: ptr_int
    return a >= 0x10000 && a < 0x00007FFFFFFFFFFFULL;
}

} // namespace

void CitizenVitals_Tick(GameUpdateLoop* gameWorld) {
    if (!Enabled() || !Readable(gameWorld)) return;

    CitizenBehavior* town = gameWorld->m_citizenList;   // +0xF0
    if (!Readable(town)) return;

    Object::InternalPtr<CitizenComponent>* arr = town->citizens._data;            // +0xA0
    const int  n = town->citizens._size;                  // +0xA8
    if (!arr || n <= 0 || n > 100000) return;

    // Count only frames that have a populated town, so frame 1 is the first frame
    // AFTER the load rather than the first frame of the menu. The home IBP (+0x238)
    // is restored intact by the serializer but reads null later, so the window that
    // matters is the first seconds of the loaded world; sample it densely, then
    // settle into a heartbeat.
    static int32_t frame = 0;
    ++frame;
    static const int32_t kEarly[] = {1, 2, 5, 10, 30, 60, 120, 240, 480, 900, 1200};
    bool due = (frame % 1800 == 0);
    for (int32_t f : kEarly) if (frame == f) { due = true; break; }
    if (!due) return;

    int live = 0, busy = 0, has238 = 0, has248 = 0, hasChar = 0;
    float ageMax = 0, hungerMax = 0, thirstMax = 0;
    double ageSum = 0, hungerSum = 0, thirstSum = 0;
    float maxAgeOf0 = 0;

    for (int i = 0; i < n; ++i) {
        CitizenComponent* c = arr[i].Get();
        if (!Readable(c)) continue;
        const float age    = c->age;        // +0x3C
        const float maxAge = c->maxAge;     // +0x40
        const float hunger = c->hunger;     // +0x44
        const float thirst = c->coldness;   // +0x48 (the header's name; this file calls it thirst)
        if (live == 0) maxAgeOf0 = maxAge;
        ++live;
        ageSum += age; hungerSum += hunger; thirstSum += thirst;
        if (age > ageMax)       ageMax = age;
        if (hunger > hungerMax) hungerMax = hunger;
        if (thirst > thirstMax) thirstMax = thirst;

        // "Frozen" has two very different causes and this separates them:
        //   busy == 0 every sample  -> the AI never issues an action at all
        //   busy > 0 but nothing changes -> actions are issued and never complete
        // CharacterComponent hangs off CitizenComponent +0x278; its +0x60 is the
        // action-stack top, seeded to -1 by the ctor (empty).
        CharacterComponent* ch = c->character.Get();           // +0x278
        if (Readable(ch)) {
            ++hasChar;
            if (ch->actionStack.current >= 0) ++busy;          // +0x60
        }
        if (c->home) ++has238;                           // +0x238
        if (c->workplace) ++has248;                      // +0x248
    }
    if (live == 0) return;

    // ENTITY COUNT. "Models are popping back in after a load" has two entirely
    // different causes and only this number separates them: if the EntityList count
    // climbs over the first seconds, entities are genuinely being created/attached
    // late and the world is rebuilding itself; if it is flat at the number the save
    // reported, every entity is already there and whatever is popping is on the
    // RENDER side (a mesh, a scene registration, a scale) — a completely different
    // hunt. EntityList lives at gameWorld+0xE8 with {+0x08 data, +0x10 count}, the
    // pair retail reads at 0x1400f96xx.
    int entities = -1;
    {
        EntityList* el = gameWorld->m_entityList;          // +0xE8
        if (Readable(el)) {
            const int n2 = el->masterArray._size;            // +0x10
            if (n2 >= 0 && n2 < 10000000) entities = n2;
        }
    }
    std::fprintf(stderr, "[EntityCensus] frame=%d entities=%d\n", frame, entities);

    std::fprintf(stderr,
        "[Vitals] frame=%d citizens=%d  age mean=%.2f max=%.2f (maxAge[0]=%.2f)  "
        "hunger mean=%.1f max=%.1f  thirst mean=%.1f max=%.1f  "
        "char=%d busy=%d ibp238=%d ibp248=%d\n",
        frame, live, ageSum / live, ageMax, maxAgeOf0,
        hungerSum / live, hungerMax, thirstSum / live, thirstMax,
        hasChar, busy, has238, has248);
    std::fflush(stderr);
}
