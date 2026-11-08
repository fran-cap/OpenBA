// ============================================================================
// tests/unit/ext_render_culling_test.cpp -- PERFXEXT: PerfX as ext.render_culling (class A)
// and ext.anim_batch (class B)
// ============================================================================
// src/ext/render_culling/render_culling_rules.h on its own (the pure decisions) and the two registry
// rows (src/ext/registry.cpp): the flags each extension state gives the seams, the
// particle exactness rule (a frozen emitter is one whose retail tick could not call
// StopEmitter, checked against a literal copy of ParticleComponent::Tick's opacity
// branch over a grid of inputs) and the rows' class / lock / save behaviour; then the PERFXTBL
// side table (src/ext/render_culling/side_table.h, below). The IsAnimating-at-clock rule behind the
// anim_batch shadow is tests/unit/ext_anim_batch_test.cpp.
// docs/extensions/handins/PERFXEXT.md
// ============================================================================
#include "unit.h"
#include "ext/render_culling/render_culling_rules.h"
#include "ext/registry.h"

#include <cstring>
#include <string>
#include <vector>

namespace {

const Ext::Entry& E(const char* id) {
    const Ext::Entry* e = Ext::FindEntry(id);
    if (e == nullptr) { std::printf("  no entry %s\n", id); std::abort(); }
    return *e;
}

// ParticleComponent::Tick (src/game/Component/particlecomponent.cpp, retail 0x140052b30),
// the opacity branch of one live emitter, copied literally: does this tick call
// StopEmitter? (`op` is the emitter's opacity, `fadeRate` its +0x10.)
bool TickCallsStopEmitter(float op, float fadeRate, float dt, float uvScale) {
    if (fadeRate != 0.0f) {
        op += dt * fadeRate;
        if (op > uvScale) {
            return false;
        } else if (op < 0.0f) {
            return true;
        } else if (op <= 0.0f) {
            return true;
        }
    } else if (op <= 0.0f) {
        return true;
    }
    return false;
}

}  // namespace

UNIT_TEST(ext_render_culling_flags_for_each_extension_state) {
    const Ext::RenderCulling::Flags off = Ext::RenderCulling::FlagsFor(false, false);
    CHECK(!off.any && !off.animCull && !off.geomXfLastOnly && !off.geomCull && !off.iconLastOnly &&
          !off.iconCull && !off.pickCull && !off.particleCull && !off.animBatch);
    CHECK(!Ext::RenderCulling::SideTableNeeded(off));   // the faithful profile builds no side table

    const Ext::RenderCulling::Flags cull = Ext::RenderCulling::FlagsFor(true, false);
    CHECK(cull.any && cull.animCull && cull.geomXfLastOnly && cull.geomCull && cull.iconLastOnly &&
          cull.iconCull && cull.pickCull && cull.particleCull);
    CHECK(!cull.animBatch);
    CHECK(Ext::RenderCulling::SideTableNeeded(cull));   // particle verdicts

    const Ext::RenderCulling::Flags batch = Ext::RenderCulling::FlagsFor(false, true);
    CHECK(batch.any && batch.animBatch);
    CHECK(!batch.animCull && !batch.geomXfLastOnly && !batch.geomCull && !batch.iconLastOnly &&
          !batch.iconCull && !batch.pickCull && !batch.particleCull);
    CHECK(Ext::RenderCulling::SideTableNeeded(batch));  // batch counts

    const Ext::RenderCulling::Flags both = Ext::RenderCulling::FlagsFor(true, true);
    CHECK(both.any && both.animBatch && both.animCull && both.particleCull);
    // The two culls that a later flush repairs ride the anim cull (FlushCulled pushes the
    // geometry, ModelCulled needs the frustum the anim cull keeps).
    for (int bits = 0; bits < 4; ++bits) {
        const Ext::RenderCulling::Flags f = Ext::RenderCulling::FlagsFor((bits & 1) != 0, (bits & 2) != 0);
        CHECK(!f.geomCull || f.animCull);
        CHECK(!f.iconCull || f.animCull);
        CHECK(f.any == (f.animCull || f.animBatch || f.particleCull || f.pickCull));
    }
    CHECK(Ext::RenderCulling::kParticleMargin > Ext::RenderCulling::kCullMargin);
}

UNIT_TEST(ext_render_culling_particle_rule_matches_the_tick) {
    // RetailStopsEmitter is the tick's own decision, over a grid that includes the
    // boundaries (0, the UV scale, a fade that lands exactly on 0).
    const float ops[]   = {-1.0f, -0.0f, 0.0f, 1e-7f, 0.25f, 0.5f, 1.0f, 1.5f};
    const float fades[] = {-4.0f, -2.0f, -0.5f, -1e-7f, 0.0f, 1e-7f, 0.5f, 2.0f};
    const float dts[]   = {0.0f, 1.0f / 60.0f, 0.125f, 0.25f, 0.5f, 1.0f};
    const float uvs[]   = {1.0f, 0.5f};
    int checked = 0, quiet = 0;
    for (float op : ops)
        for (float fade : fades)
            for (float dt : dts)
                for (float uv : uvs) {
                    const bool tick = TickCallsStopEmitter(op, fade, dt, uv);
                    CHECK(Ext::RenderCulling::RetailStopsEmitter(fade, op, dt, uv) == tick);
                    // The exactness rule: a QUIET emitter's tick never stops it, whatever
                    // the dt (so freezing it hides no sim-visible write).
                    if (Ext::RenderCulling::EmitterQuiet(fade, op)) { CHECK(!tick); ++quiet; }
                    ++checked;
                }
    CHECK(checked == 8 * 8 * 6 * 2);
    CHECK(quiet > 0);
    // Not quiet: fading either way, or invisible.
    CHECK(Ext::RenderCulling::EmitterQuiet(0.0f, 0.5f));
    CHECK(!Ext::RenderCulling::EmitterQuiet(-1.0f, 0.5f));    // fading out: may reach 0 this tick
    CHECK(!Ext::RenderCulling::EmitterQuiet(1.0f, 0.5f));     // fading in: frozen, it would lag
    CHECK(!Ext::RenderCulling::EmitterQuiet(0.0f, 0.0f));     // retail stops it on this tick
    CHECK(!Ext::RenderCulling::EmitterQuiet(0.0f, -0.25f));
    // A fade-out that reaches exactly 0 stops (the `<= 0` arm).
    CHECK(Ext::RenderCulling::RetailStopsEmitter(-2.0f, 0.5f, 0.25f, 1.0f));
}

UNIT_TEST(ext_render_culling_registry_rows) {
    const Ext::Entry& cull = E("render_culling");
    CHECK(cull.tier == Ext::Tier::Extension);
    CHECK(cull.cls == Ext::Class::A);
    CHECK(cull.defaultOn);      // class A ships ON
    CHECK(cull.formatVersion == 0);
    const Ext::Entry& batch = E("anim_batch");
    CHECK(batch.tier == Ext::Tier::Extension);
    CHECK(batch.cls == Ext::Class::B);
    CHECK(batch.defaultOn);   // ON by default (ruled 2026-11-08 for 1.0.3)
    CHECK(batch.formatVersion >= 1);

    // Defaults and the faithful profile: both off.
    Ext::State s{};
    CHECK(!s.ext.render_culling && !s.ext.anim_batch);
    Ext::State all{};
    cull.slot(all) = true;
    batch.slot(all) = true;
    Ext::Install(all, Ext::Profile::PerEntry, false);
    CHECK(Ext::Ext().render_culling && Ext::Ext().anim_batch);

    // Under a game's lock: render_culling (A) toggles live, anim_batch (B) waits.
    int owner = 0;
    Ext::LockForGame(&owner);
    CHECK(Ext::SetRequested(cull, false));          // took effect now
    CHECK(!Ext::Ext().render_culling);
    CHECK(!Ext::SetRequested(batch, false));        // locked: the game keeps it
    CHECK(Ext::Ext().anim_batch);
    CHECK(Ext::EntryLocked(batch) && !Ext::EntryLocked(cull));
    // The save carries anim_batch as the pseudo-mod ext:anim_batch, never render_culling.
    std::vector<Ext::PseudoMod> mods(Ext::EntryCount());
    const int n = Ext::PseudoModsForSave(mods.data(), (int)mods.size());
    bool sawBatch = false, sawCull = false;
    for (int i = 0; i < n; ++i) {
        if (mods[i].path == "ext:anim_batch") { sawBatch = true; CHECK(mods[i].formatVersion == batch.formatVersion); }
        if (mods[i].path == "ext:render_culling") sawCull = true;
    }
    CHECK(sawBatch && !sawCull);
    Ext::UnlockForGame(&owner);
    CHECK(!Ext::Ext().anim_batch);                  // back at the menu: the request (off)

    Ext::Install(Ext::State{}, Ext::Profile::PerEntry, false);   // leave the defaults behind
}

// ============================================================================
// PERFXTBL (was tests/unit/perfx_table_test.cpp): the per-entity PerfX side table
// (src/ext/render_culling/side_table.h) gives every model the batch count the old per-call hash
// probe (BatchEntry) gave it, and every position key its frame's first verdict.
// ============================================================================
// Two worlds see the same script of calls. The OLD one is PerfX's (render_culling.cpp)
// former AnimBatchDecide count path: BatchHash::Entry on every call. The NEW one is
// Side::SlotFor + Side::Batch over a second BatchHash (the overflow home), with the
// walk index as the hint, and Side::Rebuild whenever the scripted snapshot changes.
// After every call the counts must be equal. The scripts exercise what the game can
// do between and inside iterations: entities joining and leaving the update tree
// (the model loses and regains its slot mid-frame), every index shifting, an
// unsorted snapshot, a failed rebuild, a model ticked outside the walk (hint -1 or a
// wrong hint), two models on one entity, and a new sim frame.
// ============================================================================
#include "ext/render_culling/side_table.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <random>
#include <vector>

namespace {

using Hash = Ext::RenderCulling::SideTable::BatchHash<4096>;

struct Ent { int id; };
struct Model { int id; Ent* ent; };

struct Rig {
    std::unique_ptr<Hash> oldTbl{new Hash()};
    std::unique_ptr<Hash> newTbl{new Hash()};
    Ext::RenderCulling::SideTable::Side<Hash>  side{*newTbl};
    std::vector<Ent*>     snap;
    unsigned              serial = 1;
    int                   mismatches = 0;
    int                   calls = 0;

    Rig() {
        std::memset(oldTbl->tbl, 0, sizeof(oldTbl->tbl));
        std::memset(newTbl->tbl, 0, sizeof(newTbl->tbl));
    }
    void Sort() { std::sort(snap.begin(), snap.end(), std::less<const Ent*>()); }
    void Rebuild(bool sorted = true) { side.Rebuild(snap.data(), snap.size(), sorted, serial); }
    int64_t IndexOf(const Ent* e) const {
        for (size_t i = 0; i < snap.size(); ++i) if (snap[i] == e) return (int64_t)i;
        return -1;
    }
    // One AnimBatchDecide count step: skip=true means the model skipped (count + 1).
    void Call(Model& m, int64_t hint, bool skip) {
        ++calls;
        Ext::RenderCulling::SideTable::BatchSlot* o = oldTbl->Entry(&m, serial);
        Ext::RenderCulling::SideTable::Slot* s = side.SlotFor(m.ent, hint);
        unsigned* n = side.Batch(s, &m, serial);
        const bool ok = (o == nullptr) == (n == nullptr) && (!o || o->skipped == *n);
        if (!ok) ++mismatches;
        if (skip) { if (o) ++o->skipped; if (n) ++*n; }
    }
    unsigned NewCount(Model& m) {
        unsigned* n = side.Batch(side.SlotFor(m.ent, -1), &m, serial);
        return n ? *n : 0xffffffffu;
    }
};

}  // namespace

UNIT_TEST(ext_render_culling_side_table_hash_entry_is_the_old_probe) {
    std::unique_ptr<Hash> h(new Hash());
    std::memset(h->tbl, 0, sizeof(h->tbl));
    int a = 0, b = 0;
    Ext::RenderCulling::SideTable::BatchSlot* ea = h->Entry(&a, 1);
    CHECK(ea != nullptr);
    CHECK_EQ(ea->skipped, 0u);
    ea->skipped = 5;
    CHECK(h->Entry(&a, 1) == ea);
    CHECK(h->Find(&a, 1) == ea);
    CHECK(h->Find(&b, 1) == nullptr);
    CHECK(h->Find(&a, 2) == nullptr);
    Ext::RenderCulling::SideTable::BatchSlot* ea2 = h->Entry(&a, 2);   // new frame: the stale entry is reclaimed at 0
    CHECK(ea2 == ea);
    CHECK_EQ(ea2->skipped, 0u);
}

UNIT_TEST(ext_render_culling_side_table_find_follows_probe_chains) {
    std::unique_ptr<Hash> h(new Hash());
    std::memset(h->tbl, 0, sizeof(h->tbl));
    // Many keys: collisions build chains; Find must find every live key, and claim none.
    std::vector<int> keys(3000);
    for (size_t i = 0; i < keys.size(); ++i) {
        Ext::RenderCulling::SideTable::BatchSlot* e = h->Entry(&keys[i], 7);
        CHECK(e != nullptr);
        if (e) e->skipped = (unsigned)i;
    }
    for (size_t i = 0; i < keys.size(); ++i) {
        Ext::RenderCulling::SideTable::BatchSlot* f = h->Find(&keys[i], 7);
        CHECK(f != nullptr && f->skipped == (unsigned)i);
    }
    int other = 0;
    CHECK(h->Find(&other, 7) == nullptr);
}

UNIT_TEST(ext_render_culling_side_table_walk_hits_and_frames) {
    Rig r;
    std::vector<Ent> ents(64);
    std::vector<Model> models(64);
    for (int i = 0; i < 64; ++i) { ents[i].id = i; models[i] = Model{i, &ents[i]}; r.snap.push_back(&ents[i]); }
    r.Sort(); r.Rebuild();
    for (int frame = 0; frame < 4; ++frame) {
        ++r.serial;
        for (int it = 0; it < 10; ++it)
            for (size_t i = 0; i < r.snap.size(); ++i) {
                Model& m = models[r.snap[i]->id];
                r.Call(m, (int64_t)i, it < 9 && (m.id % 3) != 0);
            }
        for (Model& m : models) CHECK_EQ(r.NewCount(m), (m.id % 3) != 0 ? 9u : 0u);
    }
    CHECK_EQ(r.mismatches, 0);
}

UNIT_TEST(ext_render_culling_side_table_entity_leaves_and_returns_mid_frame) {
    Rig r;
    std::vector<Ent> ents(16);
    std::vector<Model> models(16);
    for (int i = 0; i < 16; ++i) { ents[i].id = i; models[i] = Model{i, &ents[i]}; r.snap.push_back(&ents[i]); }
    r.Sort(); r.Rebuild();
    ++r.serial;
    auto pass = [&](bool skip) {
        for (size_t i = 0; i < r.snap.size(); ++i) r.Call(models[r.snap[i]->id], (int64_t)i, skip);
    };
    pass(true); pass(true);
    // entity 5 leaves the tree (its count goes to the hash), the rest shift
    r.snap.erase(std::find(r.snap.begin(), r.snap.end(), &ents[5]));
    r.Rebuild();
    pass(true);
    r.Call(models[5], -1, true);           // ticked outside the walk while out of the tree
    CHECK_EQ(r.NewCount(models[5]), 3u);
    // it comes back: the slot pulls the count from the hash
    r.snap.push_back(&ents[5]); r.Sort(); r.Rebuild();
    pass(true);
    pass(false);
    for (Model& m : models) CHECK_EQ(r.NewCount(m), 4u);
    CHECK_EQ(r.mismatches, 0);
}

UNIT_TEST(ext_render_culling_side_table_unsorted_and_failed_rebuild) {
    Rig r;
    std::vector<Ent> ents(12);
    std::vector<Model> models(12);
    for (int i = 0; i < 12; ++i) { ents[i].id = i; models[i] = Model{i, &ents[i]}; r.snap.push_back(&ents[i]); }
    r.Sort(); r.Rebuild();
    ++r.serial;
    for (size_t i = 0; i < r.snap.size(); ++i) r.Call(models[r.snap[i]->id], (int64_t)i, true);
    // unsorted: everything spills, the side is unusable (every call is the hash)
    std::reverse(r.snap.begin(), r.snap.end());
    r.Rebuild(false);
    CHECK(!r.side.Usable());
    for (size_t i = 0; i < r.snap.size(); ++i) r.Call(models[r.snap[i]->id], (int64_t)i, true);
    // sorted again: the slots pull the counts back
    r.Sort(); r.Rebuild();
    CHECK(r.side.Usable());
    for (size_t i = 0; i < r.snap.size(); ++i) r.Call(models[r.snap[i]->id], (int64_t)i, true);
    // a failed rebuild (corrupt tree: n = 0, unsorted)
    r.side.Rebuild(static_cast<Ent* const*>(nullptr), 0, false, r.serial);
    for (Model& m : models) r.Call(m, -1, true);
    r.Rebuild();
    for (size_t i = 0; i < r.snap.size(); ++i) r.Call(models[r.snap[i]->id], (int64_t)i, false);
    for (Model& m : models) CHECK_EQ(r.NewCount(m), 4u);
    CHECK_EQ(r.mismatches, 0);
}

UNIT_TEST(ext_render_culling_side_table_two_models_one_entity_and_wrong_hints) {
    Rig r;
    std::vector<Ent> ents(8);
    std::vector<Model> models;
    for (int i = 0; i < 8; ++i) { ents[i].id = i; r.snap.push_back(&ents[i]); }
    for (int i = 0; i < 8; ++i) models.push_back(Model{i, &ents[i]});
    for (int i = 0; i < 8; i += 2) models.push_back(Model{100 + i, &ents[i]});   // second models
    r.Sort(); r.Rebuild();
    ++r.serial;
    for (int it = 0; it < 6; ++it)
        for (Model& m : models) {
            int64_t hint = r.IndexOf(m.ent);
            if (m.id % 4 == 1) hint = (hint + 1) % (int64_t)r.snap.size();   // a wrong hint
            if (m.id % 5 == 2) hint = -1;
            r.Call(m, hint, it < 5);
        }
    for (Model& m : models) CHECK_EQ(r.NewCount(m), 5u);
    CHECK_EQ(r.mismatches, 0);
}

UNIT_TEST(ext_render_culling_side_table_random_scripts) {
    std::mt19937 rng(12345);
    for (int script = 0; script < 40; ++script) {
        Rig r;
        const int nEnt = 20 + (int)(rng() % 60);
        std::vector<Ent> ents((size_t)nEnt);
        std::vector<Model> models;
        for (int i = 0; i < nEnt; ++i) {
            ents[(size_t)i].id = i;
            models.push_back(Model{i, &ents[(size_t)i]});
            if (rng() % 8 == 0) models.push_back(Model{1000 + i, &ents[(size_t)i]});
        }
        std::vector<bool> inTree((size_t)nEnt, true);
        auto resnap = [&] {
            r.snap.clear();
            for (int i = 0; i < nEnt; ++i) if (inTree[(size_t)i]) r.snap.push_back(&ents[(size_t)i]);
            r.Sort();
            const bool sorted = rng() % 25 != 0;
            if (!sorted) std::shuffle(r.snap.begin(), r.snap.end(), rng);
            r.Rebuild(sorted);
        };
        resnap();
        for (int frame = 0; frame < 6; ++frame) {
            ++r.serial;
            const int iters = 1 + (int)(rng() % 10);
            for (int it = 0; it < iters; ++it) {
                if (rng() % 3 == 0) {   // entities join / leave between iterations
                    for (int k = 0; k < 3; ++k) { const size_t e = rng() % (size_t)nEnt; inTree[e] = !inTree[e]; }
                    resnap();
                }
                for (Model& m : models) {
                    if (!inTree[(size_t)m.ent->id] && rng() % 4 != 0) continue;
                    int64_t hint = r.IndexOf(m.ent);
                    if (rng() % 10 == 0) hint = -1;
                    if (rng() % 10 == 0) hint = (int64_t)(rng() % (r.snap.size() + 1));
                    r.Call(m, hint, rng() % 5 != 0);
                }
            }
        }
        CHECK_EQ(r.mismatches, 0);
        CHECK(r.calls > 0);
    }
}

UNIT_TEST(ext_render_culling_side_table_visibility_first_verdict_per_frame) {
    std::unique_ptr<Hash> h(new Hash());
    Ext::RenderCulling::SideTable::Side<Hash> side(*h);
    Ent a{0}, b{1};
    std::vector<Ent*> snap{&a, &b};
    std::sort(snap.begin(), snap.end(), std::less<const Ent*>());
    side.Rebuild(snap.data(), snap.size(), true, 1);
    float posA[3] = {0, 0, 0}, posB[3] = {1, 1, 1};
    int computes = 0;
    bool answer = false;
    auto compute = [&] { ++computes; return answer; };
    Ext::RenderCulling::SideTable::Slot* sa = side.SlotFor(&a, -1);
    CHECK(sa != nullptr);
    CHECK(!Ext::RenderCulling::SideTable::Side<Hash>::Visible(sa, posA, 1, compute));
    answer = true;   // a later compute would say otherwise: the slot keeps the first verdict
    for (int it = 0; it < 9; ++it) CHECK(!Ext::RenderCulling::SideTable::Side<Hash>::Visible(sa, posA, 1, compute));
    CHECK_EQ(computes, 1);
    CHECK(Ext::RenderCulling::SideTable::Side<Hash>::Visible(sa, posA, 2, compute));    // new frame: recomputed
    CHECK_EQ(computes, 2);
    CHECK(Ext::RenderCulling::SideTable::Side<Hash>::Visible(sa, posB, 2, compute));    // another key: recomputed
    CHECK_EQ(computes, 3);
    CHECK(Ext::RenderCulling::SideTable::Side<Hash>::Visible(nullptr, posA, 2, compute));   // no slot: always compute
    CHECK_EQ(computes, 4);
    // the verdict follows its entity through a rebuild that shifts it
    Ent c{2};
    snap.push_back(&c);
    std::sort(snap.begin(), snap.end(), std::less<const Ent*>());
    Ext::RenderCulling::SideTable::Side<Hash>::Visible(side.SlotFor(&a, -1), posA, 3, compute);
    const int before = computes;
    side.Rebuild(snap.data(), snap.size(), true, 1);
    answer = false;
    CHECK(Ext::RenderCulling::SideTable::Side<Hash>::Visible(side.SlotFor(&a, -1), posA, 3, compute));
    CHECK_EQ(computes, before);
}
