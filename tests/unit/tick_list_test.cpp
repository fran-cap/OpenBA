// ============================================================================
// tests/unit/tick_list_test.cpp -- WALKCACHE: the cached slot-8 walk ticks exactly
// what retail's TickComponents (0x1400c2de0) ticks
// ============================================================================
// src/game/Entity/tick_list.h is templated on the entity type; these tests drive it
// with plain structs of the same shape (_entityFlags, _components{_data,_size,[i]},
// slot.Get(), component->_flags). Two copies of one scripted world run side by side,
// one through TickList::Retail and one through TickList::Cached with a persistent
// Entry per entity; every tick is logged as (iteration, entity, component) and the
// logs must be identical. The scripts mutate the world from inside ticks the way the
// game does: a component clears its own bit 2 (ModelComponent::Tick), sets a
// sibling's or another entity's bit, components are added / inserted / removed,
// the entity is marked deleted mid-loop, and a later Refresh un-marks it. Every
// flag write calls the mock RefreshEntityFlags, which stales the entry, exactly as
// the game's writers do; one test breaks that rule on purpose and checks that the
// COMPPROF=4 shadow (Cached<true>) reports it.
// ============================================================================
#include "unit.h"
#include "Entity/tick_list.h"

#include <functional>
#include <memory>
#include <tuple>
#include <vector>

namespace {

struct World;
struct MComp;

struct MSlot {
    MComp* p;
    MComp* Get() const { return p; }
};

struct MArr {
    MSlot*  _data = nullptr;
    int32_t _size = 0;
    MSlot&       operator[](int i)       { return _data[i]; }
    const MSlot& operator[](int i) const { return _data[i]; }
};

struct MEnt {
    uint32_t        _entityFlags = 0;
    MArr            _components;
    int             id = 0;
    TickList::Entry entry{nullptr, 0, TickList::kStale, 0, {0, 0, 0, 0}};
};

struct MComp {
    uint32_t _flags = 0x80;
    int      id = 0;
    MEnt*    ent = nullptr;
    int      ticks = 0;
    std::function<void(World&, MComp*)> hook;
};

using LogRow = std::tuple<int, int, int>;   // iteration, entity id, component id

struct World {
    std::vector<std::unique_ptr<MEnt>>  ents;
    std::vector<std::unique_ptr<MComp>> comps;
    std::vector<std::unique_ptr<std::vector<MSlot>>> bufs;   // never freed: a moved array stays readable
    std::vector<LogRow> log;
    int iteration = 0;
    int refreshes = 0;

    MComp* NewComp(uint32_t flags) {
        comps.push_back(std::make_unique<MComp>());
        MComp* c = comps.back().get();
        c->_flags = flags;
        c->id = static_cast<int>(comps.size()) * 10;
        return c;
    }
    // Replace the entity's array with a NEW buffer (Collection::Array's grow moves it).
    void SetArray(MEnt* e, const std::vector<MComp*>& list) {
        bufs.push_back(std::make_unique<std::vector<MSlot>>(list.size() + 4, MSlot{nullptr}));
        std::vector<MSlot>& b = *bufs.back();
        for (size_t i = 0; i < list.size(); ++i) { b[i].p = list[i]; list[i]->ent = e; }
        e->_components._data = b.data();
        e->_components._size = static_cast<int32_t>(list.size());
    }
    std::vector<MComp*> List(const MEnt* e) const {
        std::vector<MComp*> v;
        for (int i = 0; i < e->_components._size; ++i) v.push_back(e->_components[i].p);
        return v;
    }
    // RefreshEntityFlags: OR of the components' low words (drops 0x400, as retail's
    // does), and the WALKCACHE hook.
    void Refresh(MEnt* e) {
        ++refreshes;
        e->entry.state = TickList::kStale;
        uint32_t f = 0;
        for (int i = 0; i < e->_components._size; ++i)
            f |= static_cast<uint16_t>(e->_components[i].p->_flags);
        e->_entityFlags = f;
    }
    // The retail write idiom: write, then refresh when the low word changed.
    void Write(MComp* c, uint32_t orMask, uint32_t andNot) {
        const uint32_t o = c->_flags, n = (o | orMask) & ~andNot;
        c->_flags = n;
        if (((o ^ n) & 0xffff) != 0 && c->ent) Refresh(c->ent);
    }
    MEnt* NewEnt(const std::vector<uint32_t>& flags) {
        ents.push_back(std::make_unique<MEnt>());
        MEnt* e = ents.back().get();
        e->id = static_cast<int>(ents.size());
        std::vector<MComp*> list;
        for (uint32_t f : flags) list.push_back(NewComp(f));
        SetArray(e, list);
        Refresh(e);
        return e;
    }
};

// Run `iters` iterations over every entity, retail or cached. Returns the number of
// shadow reports that were not ok (V only).
int Run(World& w, bool cached, bool verify, int iters, int* reports = nullptr) {
    int bad = 0, nrep = 0;
    auto tick = [&w](MComp* c) {
        w.log.emplace_back(w.iteration, c->ent->id, c->id);
        ++c->ticks;
        if (c->hook) c->hook(w, c);
    };
    auto report = [&](bool ok, int64_t) { ++nrep; if (!ok) ++bad; };
    for (int it = 0; it < iters; ++it) {
        w.iteration = it;
        for (size_t i = 0; i < w.ents.size(); ++i) {
            MEnt* e = w.ents[i].get();
            if (e->_entityFlags & Bits(ComponentFlags::MarkedForRemoval)) continue;   // the walk's own deleted test
            if (!cached)     TickList::Retail(e, tick);
            else if (verify) TickList::Cached<true>(e, e->entry, tick, report);
            else             TickList::Cached<false>(e, e->entry, tick, report);
        }
    }
    if (reports) *reports = nrep;
    return bad;
}

// Build one scripted world twice, run it retail and cached (and cached + shadow),
// and compare the logs and the final flags.
void CheckIdentical(const std::function<void(World&)>& script, int iters) {
    World a, b, c;
    script(a); script(b); script(c);
    Run(a, false, false, iters);
    Run(b, true, false, iters);
    int reports = 0;
    const int bad = Run(c, true, true, iters, &reports);
    CHECK(a.log.size() > 0);
    CHECK(a.log == b.log);
    CHECK(a.log == c.log);
    CHECK_EQ(bad, 0);
    CHECK(reports > 0);   // the cached path ran (a report is made only there)
    for (size_t i = 0; i < a.ents.size(); ++i) {
        CHECK_EQ(a.ents[i]->_entityFlags, b.ents[i]->_entityFlags);
        CHECK_EQ(a.ents[i]->_components._size, b.ents[i]->_components._size);
    }
    for (size_t i = 0; i < a.comps.size(); ++i) CHECK_EQ(a.comps[i]->_flags, b.comps[i]->_flags);
}

}  // namespace

UNIT_TEST(tick_list_static_mix_identical) {
    // Mixed flag sets, no mutation: character-like (2 of 9), particle (1 of 3), state
    // machine (1 of 5), none ticking, every one ticking (wide), EF stale-clear.
    CheckIdentical([](World& w) {
        w.NewEnt({0x8f, 0x80, 0x83 | 4, 0x89, 0xcb, 0x8b, 0x2, 0xc3, 0x80});   // [0] 0x8f has bit 2
        w.NewEnt({0x43, 0xc3 | 4, 0x80});
        w.NewEnt({0x81, 0x80, 0xcd, 0x83, 0x2});
        w.NewEnt({0x80, 0x83, 0x2});
        w.NewEnt({0x84, 0x84, 0x84, 0x84, 0x84, 0x84});   // 6 candidates > kMaxIdx
        MEnt* e = w.NewEnt({0x84, 0x80});
        e->_entityFlags &= ~Bits(ComponentFlags::Tick);   // a stale entity word: no tick, in both
        w.NewEnt({});            // no components
    }, 5);
}

UNIT_TEST(tick_list_build_states) {
    World w;
    MEnt* e = w.NewEnt({0x80, 0x84, 0x83, 0x8f});
    TickList::Entry en{nullptr, 0, TickList::kStale, 0, {0, 0, 0, 0}};
    TickList::Build(en, e);
    CHECK_EQ((int)en.state, (int)TickList::kCached);
    CHECK_EQ((int)en.count, 2);
    CHECK_EQ((int)en.idx[0], 1);
    CHECK_EQ((int)en.idx[1], 3);
    CHECK(en.data == e->_components._data);
    CHECK_EQ((int)en.size, 4);

    MEnt* wide = w.NewEnt({0x84, 0x84, 0x84, 0x84, 0x84});
    TickList::Build(en, wide);
    CHECK_EQ((int)en.state, (int)TickList::kWide);

    MEnt* hole = w.NewEnt({0x84, 0x80});
    hole->_components[1].p = nullptr;   // a standalone factory miss
    TickList::Build(en, hole);
    CHECK_EQ((int)en.state, (int)TickList::kWide);
}

UNIT_TEST(tick_list_self_clear_during_tick) {
    // ModelComponent::Tick: clears its own bit 2 (with the refresh) on its 3rd tick;
    // a later component in the same entity still ticks in that pass.
    CheckIdentical([](World& w) {
        MEnt* e = w.NewEnt({0x8f, 0x80, 0x83 | 4, 0x80, 0x84});
        e->_components[2].p->hook = [](World& ww, MComp* c) {
            if (c->ticks == 3) ww.Write(c, 0, 4);
        };
        // and one that re-arms it from a sibling two iterations later
        e->_components[0].p->hook = [e](World& ww, MComp* c) {
            if (c->ticks == 5) ww.Write(e->_components[2].p, 4, 0);
        };
    }, 8);
}

UNIT_TEST(tick_list_set_sibling_later_and_earlier) {
    CheckIdentical([](World& w) {
        MEnt* e = w.NewEnt({0x84, 0x80, 0x80, 0x83, 0x80});
        // comp 0's 2nd tick arms comp 3 (later: retail ticks it in the same pass)
        e->_components[0].p->hook = [e](World& ww, MComp* c) {
            if (c->ticks == 2) ww.Write(e->_components[3].p, 4, 0);
        };
        // comp 3's 2nd tick arms comp 1 (earlier: next pass) and clears itself
        e->_components[3].p->hook = [e](World& ww, MComp* c) {
            if (c->ticks == 2) { ww.Write(e->_components[1].p, 4, 0); ww.Write(c, 0, 4); }
        };
    }, 6);
}

UNIT_TEST(tick_list_set_other_entity) {
    CheckIdentical([](World& w) {
        MEnt* a = w.NewEnt({0x84, 0x80});
        MEnt* b = w.NewEnt({0x80, 0x84, 0x83});   // before and after `a` in the walk
        MEnt* c = w.NewEnt({0x84, 0x83, 0x80});
        a->_components[0].p->hook = [b, c](World& ww, MComp* self) {
            if (self->ticks == 2) ww.Write(c->_components[2].p, 4, 0);   // later this pass
            if (self->ticks == 3) ww.Write(b->_components[2].p, 4, 0);   // b is after a too
            if (self->ticks == 4) ww.Write(c->_components[0].p, 0, 4);   // a clear elsewhere
        };
        (void)b;
    }, 7);
}

UNIT_TEST(tick_list_add_insert_remove_components) {
    CheckIdentical([](World& w) {
        MEnt* e = w.NewEnt({0x84, 0x80, 0x84, 0x80});
        // comp 0, tick 2: append an armed component (the array moves; retail's count is
        // the one read at entry, so the new one ticks from the next pass)
        e->_components[0].p->hook = [e](World& ww, MComp* c) {
            if (c->ticks == 2) {
                std::vector<MComp*> l = ww.List(e);
                l.push_back(ww.NewComp(0x84));
                ww.SetArray(e, l);
                ww.Refresh(e);
            }
            if (c->ticks == 4) {   // insert an armed one at the FRONT: everything shifts
                std::vector<MComp*> l = ww.List(e);
                l.insert(l.begin(), ww.NewComp(0x84));
                ww.SetArray(e, l);
                ww.Refresh(e);
            }
            if (c->ticks == 6) {   // remove index 1 in place (shift down, count - 1)
                for (int i = 1; i + 1 < e->_components._size; ++i)
                    e->_components[i] = e->_components[i + 1];
                --e->_components._size;
                ww.Refresh(e);
            }
        };
    }, 10);
}

UNIT_TEST(tick_list_array_change_without_refresh_is_caught_by_key) {
    // Not a game path (every array change is in a ctor / Persist / the dtor), but the
    // (data, size) key alone must send the entity back to retail's loop.
    CheckIdentical([](World& w) {
        MEnt* e = w.NewEnt({0x84, 0x80, 0x80});
        MEnt* o = w.NewEnt({0x84});
        o->_components[0].p->hook = [e](World& ww, MComp* c) {
            if (c->ticks == 2) {
                std::vector<MComp*> l = ww.List(e);
                l.insert(l.begin(), ww.NewComp(0x84));
                ww.SetArray(e, l);   // no Refresh
            }
        };
    }, 5);
}

UNIT_TEST(tick_list_deleted_mid_loop) {
    CheckIdentical([](World& w) {
        MEnt* e = w.NewEnt({0x84, 0x80, 0x84, 0x84});
        MEnt* other = w.NewEnt({0x84});
        // comp 2's 2nd tick marks the entity deleted: comp 3 does not tick that pass,
        // and the walk skips the entity after
        e->_components[2].p->hook = [e](World&, MComp* c) {
            if (c->ticks == 2) e->_entityFlags |= Bits(ComponentFlags::MarkedForRemoval);
        };
        // two iterations later another entity's flag write refreshes it, which (as
        // retail's RefreshEntityFlags does) drops 0x400: it ticks again
        other->_components[0].p->hook = [e](World& ww, MComp* c) {
            if (c->ticks == 4) ww.Refresh(e);
        };
    }, 7);
    // the deleted bit set by the LAST candidate, and by a tick that also stales
    CheckIdentical([](World& w) {
        MEnt* e = w.NewEnt({0x80, 0x84, 0x80, 0x84});
        e->_components[3].p->hook = [e](World& ww, MComp* c) {
            if (c->ticks == 2) {
                ww.Write(e->_components[0].p, 4, 0);
                e->_entityFlags |= Bits(ComponentFlags::MarkedForRemoval);
            }
        };
        e->_components[1].p->hook = [e](World& ww, MComp* c) {
            if (c->ticks == 4) ww.Refresh(e);
        };
    }, 6);
}

UNIT_TEST(tick_list_entity_word_cleared) {
    // The last bit-2 component clears itself: RefreshEntityFlags drops the entity's
    // bit 2 and neither walk ticks it again until a sibling's Update re-arms it.
    CheckIdentical([](World& w) {
        MEnt* e = w.NewEnt({0x80, 0x84, 0x80});
        MEnt* o = w.NewEnt({0x84});
        e->_components[1].p->hook = [](World& ww, MComp* c) { if (c->ticks == 2) ww.Write(c, 0, 4); };
        o->_components[0].p->hook = [e](World& ww, MComp* c) {
            if (c->ticks == 5) ww.Write(e->_components[2].p, 4, 0);
        };
    }, 8);
}

UNIT_TEST(tick_list_shadow_reports_a_write_without_refresh) {
    // Break the invariant on purpose: bit 2 set with no refresh. Retail ticks the
    // component, the cached walk does not, and Cached<true> reports the visit.
    World a, b;
    auto script = [](World& w) {
        MEnt* e = w.NewEnt({0x84, 0x80, 0x80});
        MEnt* o = w.NewEnt({0x84});
        o->_components[0].p->hook = [e](World&, MComp* c) {
            if (c->ticks == 2) e->_components[2].p->_flags |= Bits(ComponentFlags::Tick);   // no Refresh
        };
    };
    script(a); script(b);
    Run(a, false, false, 4);
    int reports = 0;
    const int bad = Run(b, true, true, 4, &reports);
    CHECK(a.log != b.log);
    CHECK(bad > 0);
    CHECK(reports > 0);
}

UNIT_TEST(tick_list_index_of_sorted_pointers) {
    int pool[8];
    int* arr[8];
    for (int i = 0; i < 8; ++i) arr[i] = &pool[i];   // ascending addresses
    CHECK(TickList::StrictlyAscending(arr, 8));
    for (int i = 0; i < 8; ++i) CHECK_EQ(TickList::IndexOf(arr, 8, &pool[i]), (int64_t)i);
    int other = 0;
    CHECK_EQ(TickList::IndexOf(arr, 8, &other), (int64_t)-1);
    CHECK_EQ(TickList::IndexOf(arr, 0, &pool[0]), (int64_t)-1);
    int* dup[3] = {&pool[1], &pool[1], &pool[2]};
    CHECK(!TickList::StrictlyAscending(dup, 3));
    int* desc[2] = {&pool[2], &pool[1]};
    CHECK(!TickList::StrictlyAscending(desc, 2));
}
