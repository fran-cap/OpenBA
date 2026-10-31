// src/ext/family_tree/family_picture.cpp -- see family_picture.h
#include "ext/family_tree/family_picture.h"

#include <algorithm>
#include <cstdio>
#include <functional>
#include <map>
#include <set>
#include <unordered_map>

namespace Ext {
namespace FamilyTree {

namespace {

// ---- text -------------------------------------------------------------------------------------------------

int CodePoints(const std::string& s) {
    int n = 0;
    for (const char c : s)
        if ((static_cast<uint8_t>(c) & 0xC0) != 0x80) ++n;
    return n;
}

// The first `count` code points of `s`, with "..." when cut.
std::string Shorten(const std::string& s, int count) {
    if (CodePoints(s) <= count) return s;
    int seen = 0;
    size_t end = 0;
    while (end < s.size()) {
        if ((static_cast<uint8_t>(s[end]) & 0xC0) != 0x80) {
            if (seen == count - 1) break;
            ++seen;
        }
        ++end;
    }
    return s.substr(0, end) + "...";
}

std::string Escape(const std::string& s) {
    std::string out;
    for (const char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default:
                // control characters are not valid XML
                if (static_cast<uint8_t>(c) >= 0x20 || c == '\t') out += c;
        }
    }
    return out;
}

std::string Year(int32_t stamp) { return "Year " + std::to_string(stamp / 12); }

std::string NameOf(const Person& p) { return Shorten(p.name.empty() ? std::string("?") : p.name, 24); }

// The lines under the name: how the person came (born or arrived), and how they went.
std::string LineArrival(const Person& p) {
    std::string s = p.bornHere ? "born " : "arrived ";
    s += p.joined >= 0 ? Year(p.joined) : std::string("date not known");
    if (!p.bornHere && p.joinedAge > 0) s += ", age " + std::to_string(p.joinedAge);
    return s;
}

std::string LineDeath(const Person& p) {
    if (p.Alive()) return std::string();
    std::string s = "died " + (p.died >= 0 ? Year(p.died) : std::string("date not known"));
    s += " (age " + std::to_string(p.ageAtDeath) + ")";
    return s;
}

// The job the person worked longest, as "Farmer 12 y"; empty when none was recorded.
std::string MainJob(const Tree& tree, const Person& p, const PictureJobs& jobs) {
    for (const JobTime& j : tree.JobsOf(p.id)) {
        const std::string& key = tree.JobName(j.job);
        if (key.empty()) continue;
        return Shorten((jobs.nameOf ? jobs.nameOf(key) : key) + " " + Tree::JobSpanText(j.units), 28);
    }
    return std::string();
}

int BoxWidthFor(const Person& p, const std::string& job = std::string()) {
    const int name = static_cast<int>(CodePoints(NameOf(p)) * 7.6) + 22;
    const int a = static_cast<int>(CodePoints(LineArrival(p)) * 5.6) + 22;
    const int b = static_cast<int>(CodePoints(LineDeath(p)) * 5.6) + 22;
    const int j = static_cast<int>(CodePoints(job) * 5.6) + 22;
    return std::max({96, name, a, b, j});
}

// ---- the layout -------------------------------------------------------------------------------------------

struct Cluster {
    std::vector<int> members;   // person indexes, left to right
    int gen = 0;
    int w = 0;
    bool visited = false;
    std::vector<int> kids;      // clusters hanging under this one
    int subtreeW = 0;
    int kidsW = 0;
    int x = 0;                  // left edge
};

// The people in the picture, ascending ids.
std::vector<uint32_t> Choose(const Tree& tree, bool livingOnly) {
    std::vector<uint32_t> out;
    if (!livingOnly) {
        for (const Person& p : tree.People()) out.push_back(p.id);
        return out;
    }
    std::set<uint32_t> keep;
    std::vector<uint32_t> work;
    for (const Person& p : tree.People()) {
        if (!p.Alive()) continue;
        keep.insert(p.id);
        work.push_back(p.id);
    }
    // their husbands and wives (the widowed one's partner is on the picture)
    for (const uint32_t id : std::vector<uint32_t>(work)) {
        const Person* p = tree.Find(id);
        for (const Marriage& m : p->marriages)
            if (tree.Find(m.spouse) != nullptr && keep.insert(m.spouse).second) work.push_back(m.spouse);
    }
    // everyone above: parents, their parents, ...
    while (!work.empty()) {
        const uint32_t id = work.back();
        work.pop_back();
        const Person* p = tree.Find(id);
        if (p == nullptr) continue;
        for (const uint32_t par : {p->mother, p->father})
            if (par != 0 && tree.Find(par) != nullptr && keep.insert(par).second) work.push_back(par);
    }
    out.assign(keep.begin(), keep.end());
    return out;
}

}  // namespace

PictureLayout BuildPictureLayout(const Tree& tree, bool livingOnly, const PictureJobs* jobs) {
    PictureLayout out;
    out.jobs = jobs != nullptr;
    out.boxHeight = out.jobs ? kPictureBoxHeightWithJob : kPictureBoxHeight;
    std::vector<uint32_t> ids = Choose(tree, livingOnly);
    if (!livingOnly && static_cast<int>(ids.size()) > kPictureMaxPeople) {
        ids = Choose(tree, true);
        out.trimmed = true;
    }
    if (static_cast<int>(ids.size()) > kPictureMaxPeople) {
        ids.resize(static_cast<size_t>(kPictureMaxPeople));
        out.trimmed = true;
    }
    const int n = static_cast<int>(ids.size());
    out.people = n;
    if (n == 0) return out;
    std::unordered_map<uint32_t, int> index;
    for (int i = 0; i < n; ++i) index[ids[static_cast<size_t>(i)]] = i;
    const auto indexOf = [&](uint32_t id) {
        const auto it = index.find(id);
        return it == index.end() ? -1 : it->second;
    };

    std::vector<const Person*> person(static_cast<size_t>(n));
    std::vector<std::string> job(static_cast<size_t>(n));
    std::vector<int> mother(static_cast<size_t>(n), -1), father(static_cast<size_t>(n), -1);
    std::vector<std::vector<int>> spouses(static_cast<size_t>(n)), children(static_cast<size_t>(n));
    std::set<std::pair<int, int>> pairs;
    for (int i = 0; i < n; ++i) {
        const Person* p = tree.Find(ids[static_cast<size_t>(i)]);
        person[static_cast<size_t>(i)] = p;
        if (jobs != nullptr) job[static_cast<size_t>(i)] = MainJob(tree, *p, *jobs);
        mother[static_cast<size_t>(i)] = indexOf(p->mother);
        father[static_cast<size_t>(i)] = indexOf(p->father);
        for (const Marriage& m : p->marriages) {
            const int j = indexOf(m.spouse);
            if (j < 0 || j == i) continue;
            if (pairs.insert({std::min(i, j), std::max(i, j)}).second) {
                spouses[static_cast<size_t>(i)].push_back(j);
                spouses[static_cast<size_t>(j)].push_back(i);
            }
        }
    }
    for (int i = 0; i < n; ++i) {
        if (mother[static_cast<size_t>(i)] >= 0)
            children[static_cast<size_t>(mother[static_cast<size_t>(i)])].push_back(i);
        if (father[static_cast<size_t>(i)] >= 0)
            children[static_cast<size_t>(father[static_cast<size_t>(i)])].push_back(i);
    }
    for (auto& v : spouses) std::sort(v.begin(), v.end());

    // generations: a marriage cluster (the people joined by marriages) is one row, and a cluster is below the
    // clusters its members' parents are in. A parent in the same cluster (a widow who marries her stepson)
    // adds no row, so the child is level with the parent instead of above it, and one link of a loop between
    // clusters (a grandchild who marries a grandparent's spouse) is left out, so the rows always settle and
    // nobody outside the loop is drawn above a parent.
    std::vector<int> comp(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) comp[static_cast<size_t>(i)] = i;
    const std::function<int(int)> rootOf = [&](int v) {
        while (comp[static_cast<size_t>(v)] != v) {
            comp[static_cast<size_t>(v)] = comp[static_cast<size_t>(comp[static_cast<size_t>(v)])];
            v = comp[static_cast<size_t>(v)];
        }
        return v;
    };
    for (const auto& pr : pairs) {
        const int ra = rootOf(pr.first), rb = rootOf(pr.second);
        if (ra != rb) comp[static_cast<size_t>(std::max(ra, rb))] = std::min(ra, rb);
    }
    // the lowest person index of the cluster
    for (int i = 0; i < n; ++i) comp[static_cast<size_t>(i)] = rootOf(i);
    // the parent links between clusters. A loop of them (a person who married a grandparent's spouse) cannot
    // all be kept, so the walk down from the clusters nobody is below leaves out the one link that comes back
    // up to a cluster still being walked; every other link stays, and a person in a loop is the only one
    // drawn above a parent.
    // cluster -> the clusters of its members' children
    std::vector<std::vector<int>> below(static_cast<size_t>(n));
    std::vector<int> hasAbove(static_cast<size_t>(n), 0);
    for (int i = 0; i < n; ++i)
        for (const int par : {mother[static_cast<size_t>(i)], father[static_cast<size_t>(i)]})
            if (par >= 0 && comp[static_cast<size_t>(par)] != comp[static_cast<size_t>(i)]) {
                below[static_cast<size_t>(comp[static_cast<size_t>(par)])]
                    .push_back(comp[static_cast<size_t>(i)]);
                hasAbove[static_cast<size_t>(comp[static_cast<size_t>(i)])] = 1;
            }
    std::vector<int> state(static_cast<size_t>(n), 0);   // 0 not reached, 1 being walked, 2 done
    std::vector<std::vector<char>> kept(static_cast<size_t>(n));
    const std::function<void(int)> walkDown = [&](int v) {
        state[static_cast<size_t>(v)] = 1;
        kept[static_cast<size_t>(v)].assign(below[static_cast<size_t>(v)].size(), 1);
        for (size_t k = 0; k < below[static_cast<size_t>(v)].size(); ++k) {
            const int w = below[static_cast<size_t>(v)][k];
            if (state[static_cast<size_t>(w)] == 1) kept[static_cast<size_t>(v)][k] = 0;
            else if (state[static_cast<size_t>(w)] == 0) walkDown(w);
        }
        state[static_cast<size_t>(v)] = 2;
    };
    for (const int pass : {0, 1})
        for (int v = 0; v < n; ++v)
            if (comp[static_cast<size_t>(v)] == v && state[static_cast<size_t>(v)] == 0 &&
                (pass == 1 || hasAbove[static_cast<size_t>(v)] == 0))
                walkDown(v);
    std::vector<int> row(static_cast<size_t>(n), 0), gen(static_cast<size_t>(n), 0);
    for (int pass = 0; pass < n + 2; ++pass) {
        bool changed = false;
        for (int v = 0; v < n; ++v) {
            if (comp[static_cast<size_t>(v)] != v) continue;
            for (size_t k = 0; k < below[static_cast<size_t>(v)].size(); ++k) {
                const int w = below[static_cast<size_t>(v)][k];
                if (!kept[static_cast<size_t>(v)][k]) continue;
                if (row[static_cast<size_t>(w)] < row[static_cast<size_t>(v)] + 1) {
                    row[static_cast<size_t>(w)] = row[static_cast<size_t>(v)] + 1;
                    changed = true;
                }
            }
        }
        if (!changed) break;
    }
    for (int i = 0; i < n; ++i)
        gen[static_cast<size_t>(i)] = row[static_cast<size_t>(comp[static_cast<size_t>(i)])];

    // clusters: the marriage components, laid out as a chain (an end first, then along the marriages)
    std::vector<int> clusterOf(static_cast<size_t>(n), -1);
    std::vector<Cluster> clusters;
    for (int i = 0; i < n; ++i) {
        if (clusterOf[static_cast<size_t>(i)] >= 0) continue;
        // the component
        std::vector<int> comp;
        std::vector<int> stack{i};
        clusterOf[static_cast<size_t>(i)] = static_cast<int>(clusters.size());
        while (!stack.empty()) {
            const int v = stack.back();
            stack.pop_back();
            comp.push_back(v);
            for (const int w : spouses[static_cast<size_t>(v)]) {
                if (clusterOf[static_cast<size_t>(w)] < 0) {
                    clusterOf[static_cast<size_t>(w)] = static_cast<int>(clusters.size());
                    stack.push_back(w);
                }
            }
        }
        // the start: the one with the fewest marriages (an end of the chain), the smallest index among those
        int start = comp.front();
        for (const int v : comp) {
            const size_t dv = spouses[static_cast<size_t>(v)].size();
            const size_t ds = spouses[static_cast<size_t>(start)].size();
            if (dv < ds || (dv == ds && v < start)) start = v;
        }
        Cluster c;
        std::set<int> seen;
        std::function<void(int)> walk = [&](int v) {
            seen.insert(v);
            c.members.push_back(v);
            for (const int w : spouses[static_cast<size_t>(v)])
                if (seen.find(w) == seen.end()) walk(w);
        };
        walk(start);
        c.gen = gen[static_cast<size_t>(start)];
        for (size_t k = 0; k < c.members.size(); ++k) {
            c.w += BoxWidthFor(*person[static_cast<size_t>(c.members[k])],
                               job[static_cast<size_t>(c.members[k])]);
            if (k > 0) c.w += kPictureSpouseGap;
        }
        clusters.push_back(c);
    }

    // the forest: a cluster hangs under the first cluster of the walk that has a child of its own
    std::vector<int> roots;
    std::function<void(int)> hang = [&](int ci) {
        for (const int member : std::vector<int>(clusters[static_cast<size_t>(ci)].members)) {
            for (const int child : children[static_cast<size_t>(member)]) {
                const int cc = clusterOf[static_cast<size_t>(child)];
                if (clusters[static_cast<size_t>(cc)].visited) continue;
                clusters[static_cast<size_t>(cc)].visited = true;
                clusters[static_cast<size_t>(ci)].kids.push_back(cc);
                hang(cc);
            }
        }
    };
    for (size_t ci = 0; ci < clusters.size(); ++ci) {
        if (clusters[ci].visited) continue;
        clusters[ci].visited = true;
        roots.push_back(static_cast<int>(ci));
        hang(static_cast<int>(ci));
    }

    // widths, then places
    std::function<void(int)> measure = [&](int ci) {
        Cluster& c = clusters[static_cast<size_t>(ci)];
        c.kidsW = 0;
        for (size_t k = 0; k < c.kids.size(); ++k) {
            measure(c.kids[k]);
            c.kidsW += clusters[static_cast<size_t>(c.kids[k])].subtreeW;
            if (k > 0) c.kidsW += kPictureSiblingGap;
        }
        c.subtreeW = std::max(c.w, c.kidsW);
    };
    // returns the cluster's center
    std::function<int(int, int)> place = [&](int ci, int left) {
        Cluster& c = clusters[static_cast<size_t>(ci)];
        int center = left + c.subtreeW / 2;
        if (!c.kids.empty()) {
            int at = left + (c.subtreeW - c.kidsW) / 2;
            int first = 0, last = 0;
            for (size_t k = 0; k < c.kids.size(); ++k) {
                const int kc = place(c.kids[k], at);
                if (k == 0) first = kc;
                last = kc;
                at += clusters[static_cast<size_t>(c.kids[k])].subtreeW + kPictureSiblingGap;
            }
            center = (first + last) / 2;
        }
        c.x = std::max(left, std::min(center - c.w / 2, left + c.subtreeW - c.w));
        return c.x + c.w / 2;
    };
    int left = kPictureMargin;
    for (const int r : roots) {
        measure(r);
        place(r, left);
        left += clusters[static_cast<size_t>(r)].subtreeW + 3 * kPictureSiblingGap;
    }

    // the boxes
    int maxRight = 0;
    int maxGen = 0;
    for (const Cluster& c : clusters) {
        int x = c.x;
        for (size_t k = 0; k < c.members.size(); ++k) {
            const int m = c.members[k];
            PictureBox b;
            b.id = ids[static_cast<size_t>(m)];
            b.gen = c.gen;
            b.w = BoxWidthFor(*person[static_cast<size_t>(m)], job[static_cast<size_t>(m)]);
            b.job = job[static_cast<size_t>(m)];
            b.x = x;
            x += b.w + kPictureSpouseGap;
            maxRight = std::max(maxRight, b.x + b.w);
            maxGen = std::max(maxGen, b.gen);
            out.boxes.push_back(b);
        }
    }
    // Rows come from the generations, places from the walk down the families, and the two disagree when a
    // marriage joins lines of different depth (a widow's second husband, a niece's uncle): two families can
    // then land in one row over each other. Each row is swept left to right and a marriage cluster that
    // overlaps the one before it moves right.
    {
        struct Group { int row; int x; std::vector<size_t> boxes; };
        std::map<std::pair<int, int>, size_t> at;
        std::vector<Group> groups;
        size_t bi = 0;
        for (size_t ci = 0; ci < clusters.size(); ++ci) {
            for (size_t k = 0; k < clusters[ci].members.size(); ++k, ++bi) {
                const auto key = std::make_pair(clusters[ci].gen, static_cast<int>(ci));
                auto it = at.find(key);
                if (it == at.end()) {
                    it = at.emplace(key, groups.size()).first;
                    groups.push_back(Group{clusters[ci].gen, clusters[ci].x, {}});
                }
                groups[it->second].boxes.push_back(bi);
            }
        }
        std::sort(groups.begin(), groups.end(),
                  [](const Group& a, const Group& b) { return a.row != b.row ? a.row < b.row : a.x < b.x; });
        int row = -1, edge = 0;
        for (Group& g : groups) {
            const int left = out.boxes[g.boxes.front()].x;
            const int right = out.boxes[g.boxes.back()].x + out.boxes[g.boxes.back()].w;
            if (g.row != row) {
                row = g.row;
                edge = left;
            }
            const int shift = edge > left ? edge - left : 0;
            for (const size_t b : g.boxes) out.boxes[b].x += shift;
            edge = right + shift + kPictureSiblingGap;
        }
        maxRight = 0;
        for (const PictureBox& b : out.boxes) maxRight = std::max(maxRight, b.x + b.w);
    }
    out.gens = maxGen + 1;
    out.width = maxRight + kPictureMargin;
    for (const auto& pr : pairs) out.couples.push_back({ids[static_cast<size_t>(pr.first)],
                                                        ids[static_cast<size_t>(pr.second)]});

    // the families: the children of one pair of parents (or of one known parent)
    std::map<std::pair<int, int>, std::vector<int>> fam;
    for (int i = 0; i < n; ++i) {
        const int m = mother[static_cast<size_t>(i)], f = father[static_cast<size_t>(i)];
        if (m < 0 && f < 0) continue;
        fam[{m, f}].push_back(i);
    }
    for (const auto& kv : fam) {
        PictureFamily f;
        f.mother = kv.first.first >= 0 ? ids[static_cast<size_t>(kv.first.first)] : 0;
        f.father = kv.first.second >= 0 ? ids[static_cast<size_t>(kv.first.second)] : 0;
        for (const int k : kv.second) f.kids.push_back(ids[static_cast<size_t>(k)]);
        out.families.push_back(f);
    }
    return out;
}

// ---- the picture ------------------------------------------------------------------------------------------

namespace {

const char* kPalette[] = {"#d1495b", "#00798c", "#e09f1f", "#5f9e7a", "#7b6cc4", "#e0701f",
                          "#2e86ab", "#a23b72", "#4f772d", "#b5651d", "#3a6ea5", "#8d6b94"};
constexpr int kPaletteSize = static_cast<int>(sizeof(kPalette) / sizeof(kPalette[0]));

struct Out {
    std::string s;
    // snprintf into one line's worth, appended (a template, not a va_list: tools/abi_audit.py)
    template <typename... Args>
    void Add(const char* fmt, Args... args) {
        char buf[1024];
        std::snprintf(buf, sizeof buf, fmt, args...);
        s += buf;
    }
};

}  // namespace

const char* PictureColor(int index) {
    return kPalette[((index % kPaletteSize) + kPaletteSize) % kPaletteSize];
}

PictureCanvas CanvasFor(const PictureLayout& lay) {
    PictureCanvas c;
    c.rowStep = lay.boxHeight + kPictureRowGap;
    c.top = kPictureHeader + kPictureMargin;
    c.width = std::max(lay.width, 640);
    c.height = c.top + std::max(lay.gens, 1) * c.rowStep - kPictureRowGap + kPictureMargin + kPictureLegend;
    return c;
}

std::string PictureTitle(const PictureOptions& opt) {
    return opt.village.empty() ? std::string("Village family tree") : Shorten(opt.village, 60);
}

std::string PictureKind(const PictureOptions& opt) {
    return opt.village.empty() ? std::string() : std::string("Village family tree");
}

std::string PictureSubtitle(const PictureLayout& lay, const PictureOptions& opt) {
    std::string sub = std::to_string(lay.people) + (lay.people == 1 ? " person, " : " people, ") +
                      std::to_string(lay.gens) + (lay.gens == 1 ? " generation" : " generations");
    if (opt.livingOnly) sub += "; the living, their husbands and wives, and their ancestors";
    if (opt.highlight != 0 && !opt.highlightName.empty())
        sub += "; " + opt.highlightName + " and the descendants are outlined in orange";
    if (lay.trimmed) sub += "; the town is large, so not everyone is shown";
    if (opt.now >= 0) sub += "; " + Year(opt.now);
    return sub;
}

std::vector<uint32_t> PictureHighlighted(const Tree& tree, const PictureOptions& opt) {
    if (opt.highlight == 0 || tree.Find(opt.highlight) == nullptr) return {};
    std::vector<uint32_t> out{opt.highlight};
    const std::vector<uint32_t> below = tree.DescendantIds(opt.highlight);
    out.insert(out.end(), below.begin(), below.end());
    return out;
}

const char* PictureHighlightColor() { return "#e07b00"; }

std::string PictureName(const Person& p) { return NameOf(p); }
std::string PictureArrival(const Person& p) { return LineArrival(p); }
std::string PictureDeath(const Person& p) { return LineDeath(p); }

std::vector<PictureStroke> BuildPictureStrokes(const PictureLayout& lay) {
    const PictureCanvas canvas = CanvasFor(lay);
    std::unordered_map<uint32_t, const PictureBox*> box;
    for (const PictureBox& b : lay.boxes) box[b.id] = &b;
    const auto rowY = [&](int g) { return canvas.RowY(g); };
    const auto centerX = [&](uint32_t id) {
        const auto it = box.find(id);
        return it == box.end() ? 0 : it->second->x + it->second->w / 2;
    };
    std::vector<PictureStroke> out;

    // marriages: one stroke each, side by side or an arc over the boxes between them
    std::set<std::pair<uint32_t, uint32_t>> adjacent;
    for (const auto& c : lay.couples) {
        const auto a = box.find(c.first), b = box.find(c.second);
        if (a == box.end() || b == box.end()) continue;
        const PictureBox* l = a->second->x < b->second->x ? a->second : b->second;
        const PictureBox* r = l == a->second ? b->second : a->second;
        const int y = rowY(l->gen);
        PictureStroke s;
        if (l->gen == r->gen && r->x - (l->x + l->w) == kPictureSpouseGap) {
            const int my = y + lay.boxHeight / 2;
            s.segments.push_back({l->x + l->w, my, r->x, my});
            adjacent.insert(c);
        } else {
            const int lift = 18;
            const int lx = l->x + l->w / 2, rx = r->x + r->w / 2;
            s.segments.push_back({lx, y, lx, y - lift});
            s.segments.push_back({lx, y - lift, rx, y - lift});
            s.segments.push_back({rx, y - lift, rx, rowY(r->gen)});
        }
        out.push_back(std::move(s));
    }

    // families: sorted as they stand on the page, so the palette goes round neighbors
    struct FamEdge {
        const PictureFamily* f;
        int gen, ax;
    };
    std::vector<FamEdge> edges;
    for (const PictureFamily& f : lay.families) {
        const uint32_t p = f.mother != 0 && box.count(f.mother) ? f.mother : f.father;
        if (p == 0 || box.find(p) == box.end()) continue;
        const int ax = f.mother != 0 && f.father != 0 && box.count(f.mother) && box.count(f.father)
                           ? (centerX(f.mother) + centerX(f.father)) / 2 : centerX(p);
        edges.push_back({&f, box[p]->gen, ax});
    }
    std::stable_sort(edges.begin(), edges.end(), [](const FamEdge& a, const FamEdge& b) {
        return a.gen != b.gen ? a.gen < b.gen : a.ax < b.ax;
    });
    std::map<int, int> levelInRow;
    int colorIndex = 0;
    for (const FamEdge& e : edges) {
        const PictureFamily& f = *e.f;
        PictureStroke s;
        s.color = colorIndex % kPaletteSize;
        ++colorIndex;
        const int level = levelInRow[e.gen]++ % 5;
        const int bottom = rowY(e.gen) + lay.boxHeight;
        const int bus = bottom + 16 + level * 9;
        const bool both = f.mother != 0 && f.father != 0 && box.count(f.mother) && box.count(f.father);
        int startY;
        if (both) {
            const std::pair<uint32_t, uint32_t> key{std::min(f.mother, f.father),
                                                    std::max(f.mother, f.father)};
            if (adjacent.count(key)) {
                startY = rowY(e.gen) + lay.boxHeight / 2;
            } else {
                // not side by side: a stub from each parent to a joining line
                startY = bottom + 8;
                for (const uint32_t par
                     : {f.mother, f.father}) s.segments.push_back({centerX(par), bottom, centerX(par),
                        startY});
                s.segments.push_back({centerX(f.mother), startY, centerX(f.father), startY});
            }
        } else {
            startY = bottom;
        }
        s.segments.push_back({e.ax, startY, e.ax, bus});
        int lo = e.ax, hi = e.ax;
        for (const uint32_t k : f.kids) {
            if (!box.count(k)) continue;
            lo = std::min(lo, centerX(k));
            hi = std::max(hi, centerX(k));
        }
        if (lo != hi) s.segments.push_back({lo, bus, hi, bus});
        for (const uint32_t k : f.kids) {
            const auto it = box.find(k);
            if (it == box.end()) continue;
            s.segments.push_back({centerX(k), bus, centerX(k), rowY(it->second->gen)});
            s.dots.push_back({centerX(k), rowY(it->second->gen)});
        }
        out.push_back(std::move(s));
    }
    return out;
}

std::string RenderPictureSvg(const Tree& tree, const PictureLayout& lay, const PictureOptions& opt) {
    const PictureCanvas canvas = CanvasFor(lay);
    const int width = canvas.width;
    const int height = canvas.height;
    const auto rowY = [&](int g) { return canvas.RowY(g); };

    Out o;
    o.Add("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    o.Add("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%d\" height=\"%d\" viewBox=\"0 0 %d %d\" "
          "font-family=\"'Segoe UI', Helvetica, Arial, sans-serif\">\n", width, height, width, height);
    o.Add("<rect width=\"%d\" height=\"%d\" fill=\"#ffffff\"/>\n", width, height);
    // the village's name is the title, "Village family tree" under it; without a name that is the title
    const bool named = !opt.village.empty();
    o.Add("<text x=\"%d\" y=\"44\" font-size=\"28\" font-weight=\"700\" fill=\"#2b2b2b\">%s</text>\n",
          kPictureMargin,
          Escape(PictureTitle(opt)).c_str());
    if (named) {
        o.Add("<text x=\"%d\" y=\"68\" font-size=\"16\" font-weight=\"600\" fill=\"#555555\">%s</text>\n",
              kPictureMargin,
              Escape(PictureKind(opt)).c_str());
    }
    o.Add("<text x=\"%d\" y=\"%d\" font-size=\"14\" fill=\"#666666\">%s</text>\n", kPictureMargin, named ? 90
          : 72,
          Escape(PictureSubtitle(lay, opt)).c_str());

    // marriages, then each family's line in its color
    const std::vector<PictureStroke> strokes = BuildPictureStrokes(lay);
    o.Add("<g stroke=\"#4a4a4a\" stroke-width=\"2.5\" fill=\"none\" stroke-linecap=\"round\">\n");
    for (const PictureStroke& s : strokes) {
        if (s.color >= 0) continue;
        std::string d;
        char buf[96];
        for (const PictureSegment& g : s.segments) {
            if (g.x0 == g.x1) std::snprintf(buf, sizeof buf, "M%d %d V%d ", g.x0, g.y0, g.y1);
            else std::snprintf(buf, sizeof buf, "M%d %d H%d ", g.x0, g.y0, g.x1);
            d += buf;
        }
        o.s += "<path d=\"" + d + "\"/>\n";
    }
    o.Add("</g>\n");
    o.Add("<g fill=\"none\" stroke-width=\"2.2\" stroke-linejoin=\"round\" stroke-linecap=\"round\">\n");
    for (const PictureStroke& s : strokes) {
        if (s.color < 0) continue;
        std::string d;
        char buf[96];
        for (const PictureSegment& g : s.segments) {
            if (g.x0 == g.x1) std::snprintf(buf, sizeof buf, "M%d %d V%d ", g.x0, g.y0, g.y1);
            else std::snprintf(buf, sizeof buf, "M%d %d H%d ", g.x0, g.y0, g.x1);
            d += buf;
        }
        // a big family's path is long
        o.s += std::string("<path stroke=\"") + PictureColor(s.color) + "\" d=\"" + d + "\"/>\n";
        for (const auto& dot : s.dots) {
            o.Add("<circle cx=\"%d\" cy=\"%d\" r=\"3.2\" fill=\"%s\" stroke=\"none\"/>\n", dot.first,
                  dot.second,
                  PictureColor(s.color));
        }
    }
    o.Add("</g>\n");

    // the people
    const std::vector<uint32_t> outlined = PictureHighlighted(tree, opt);
    for (const PictureBox& b : lay.boxes) {
        const Person* p = tree.Find(b.id);
        if (p == nullptr) continue;
        const bool dead = !p->Alive();
        const bool mark = std::find(outlined.begin(), outlined.end(), b.id) != outlined.end();
        const char* fill = dead ? "#efefef" : (p->gender == 0 ? "#fde8ef" : "#e4eefb");
        const char* stroke = mark ? PictureHighlightColor() : (dead ? "#9b9b9b"
                : (p->gender == 0 ? "#c4718b" : "#6d95c8"));
        const char* ink = dead ? "#6a6a6a" : "#262626";
        const int y = rowY(b.gen);
        const int cx = b.x + b.w / 2;
        o.Add("<rect x=\"%d\" y=\"%d\" width=\"%d\" height=\"%d\" rx=\"6\" fill=\"%s\" stroke=\"%s\" "
              "stroke-width=\"%s\"%s/>\n",
              b.x, y, b.w, lay.boxHeight, fill, stroke, mark ? "3.4" : "1.6", dead ? " stroke-dasharray=\"5 "
                      "3\"" : "");
        o.Add("<text x=\"%d\" y=\"%d\" text-anchor=\"middle\" font-size=\"13\" font-weight=\"700\" "
              "fill=\"%s\">%s</text>\n", cx,
              y + 20, ink, Escape(NameOf(*p)).c_str());
        o.Add("<text x=\"%d\" y=\"%d\" text-anchor=\"middle\" font-size=\"10.5\" fill=\"%s\">%s</text>\n", cx,
              y + 35, ink,
              Escape(LineArrival(*p)).c_str());
        const std::string death = LineDeath(*p);
        if (!death.empty()) {
            o.Add("<text x=\"%d\" y=\"%d\" text-anchor=\"middle\" font-size=\"10.5\" fill=\"%s\">%s</text>\n",
                  cx, y + 48, ink,
                  Escape(death).c_str());
        }
        if (!b.job.empty()) {
            o.Add("<text x=\"%d\" y=\"%d\" text-anchor=\"middle\" font-size=\"10.5\" font-style=\"italic\" "
                  "fill=\"%s\">%s</text>\n", cx,
                  y + (death.empty() ? 48 : 62), ink, Escape(b.job).c_str());
        }
    }

    // the key
    const int ly = height - kPictureLegend + 26;
    o.Add("<g font-size=\"12\" fill=\"#555555\">\n");
    o.Add("<rect x=\"%d\" y=\"%d\" width=\"26\" height=\"14\" rx=\"3\" fill=\"#fde8ef\" stroke=\"#c4718b\"/>"
          "<text x=\"%d\" y=\"%d\">woman</text>\n", kPictureMargin, ly - 11, kPictureMargin + 34, ly);
    o.Add("<rect x=\"%d\" y=\"%d\" width=\"26\" height=\"14\" rx=\"3\" fill=\"#e4eefb\" stroke=\"#6d95c8\"/>"
          "<text x=\"%d\" y=\"%d\">man</text>\n", kPictureMargin + 110, ly - 11, kPictureMargin + 144, ly);
    o.Add("<rect x=\"%d\" y=\"%d\" width=\"26\" height=\"14\" rx=\"3\" fill=\"#efefef\" stroke=\"#9b9b9b\" "
          "stroke-dasharray=\"5 3\"/><text x=\"%d\" y=\"%d\">has "
                  "died</text>\n", kPictureMargin + 200, ly - 11, kPictureMargin + 234,
          ly);
    o.Add("<path d=\"M%d %d H%d\" stroke=\"#4a4a4a\" stroke-width=\"2.5\"/><text x=\"%d\" "
          "y=\"%d\">marriage</text>\n",
          kPictureMargin + 330, ly - 4, kPictureMargin + 356, kPictureMargin + 364, ly);
    o.Add("<text x=\"%d\" y=\"%d\">Each family's line, from its parents down to its children, has one "
          "color.</text>\n",
          kPictureMargin + 450, ly);
    o.Add("</g>\n</svg>\n");
    return o.s;
}

}  // namespace FamilyTree
}  // namespace Ext
