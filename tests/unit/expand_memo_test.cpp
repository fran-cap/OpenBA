// ============================================================================
// tests/unit/expand_memo_test.cpp -- blocks SAVELOAD / PATHINDEX: the
//   ExpandRelativePath memo returns an entry only for the same request
// ============================================================================
// The memo (src/engine/object/expand_memo.{h,cpp}) is keyed on the request's
// characters + appendIdentifier. Under an empty path stack the body's answer is
// a function of those alone (expand_memo.h, point 4), so entries are never
// dropped but for the size bound. What an entry holds is the body's own
// output; the debug exe checks every hit against the body on the game's real
// request stream (src/engine/io/external_path.cpp). Here: the bookkeeping.
// ============================================================================
#include "unit.h"
#include "object/expand_memo.h"

#include <string>

using namespace RtObject;

namespace {
// The suite is one thread: the ResLoad lock the memo REQUIRES is not taken here.
const char* Find(const char* p, bool app) NO_THREAD_SAFETY_ANALYSIS {
    return ExpandMemoFind(p, static_cast<int>(std::string(p).size()), app);
}
void Store(const char* p, bool app, const char* r) NO_THREAD_SAFETY_ANALYSIS {
    ExpandMemoStore(p, static_cast<int>(std::string(p).size()), app, r, static_cast<int>(std::string(r).size()));
}
bool Is(const char* got, const char* want) {
    return got != nullptr && std::string(got) == want;
}
}  // namespace

UNIT_TEST(expand_memo_hit_only_for_the_same_request) {
    ExpandMemoClear();
    CHECK(Find("Template\\Tree.rsc:description", true) == nullptr);
    Store("Template\\Tree.rsc:description", true, "Template\\Tree.rsc:description");
    CHECK(Is(Find("Template\\Tree.rsc:description", true), "Template\\Tree.rsc:description"));
    // a different flag, a prefix, a longer name, a case variant, the other delimiter: all misses
    CHECK(Find("Template\\Tree.rsc:description", false) == nullptr);
    CHECK(Find("Template\\Tree.rsc:descriptio", true) == nullptr);
    CHECK(Find("Template\\Tree.rsc:descriptionX", true) == nullptr);
    CHECK(Find("template\\Tree.rsc:description", true) == nullptr);
    CHECK(Find("Template/Tree.rsc:description", true) == nullptr);
    CHECK_EQ(ExpandMemoSize(), static_cast<size_t>(1));
}

UNIT_TEST(expand_memo_answer_equal_to_the_request_is_the_request) {
    ExpandMemoClear();
    const char* const request = "Models\\Tree.rsc:mesh";
    Store(request, true, request);
    CHECK(Find(request, true) == request);   // handed back the caller's own characters
    Store("Models/Rock.rsc", true, "Models\\Rock.rsc:resource");
    CHECK(Is(Find("Models/Rock.rsc", true), "Models\\Rock.rsc:resource"));
    // a request whose answer is a strict prefix of it is not "the same"
    Store("a.rsc:x", false, "a.rsc");
    CHECK(Is(Find("a.rsc:x", false), "a.rsc"));
}

UNIT_TEST(expand_memo_flag_is_part_of_the_key) {
    ExpandMemoClear();
    Store("a.rsc", true, "a.rsc:resource");
    Store("a.rsc", false, "a.rsc");
    CHECK(Is(Find("a.rsc", true), "a.rsc:resource"));
    CHECK(Is(Find("a.rsc", false), "a.rsc"));
}

UNIT_TEST(expand_memo_is_bounded) {
    ExpandMemoClear();
    for (int i = 0; i < (1 << 20) + 10; ++i) {
        const std::string p = "n" + std::to_string(i) + ".rsc";
        Store(p.c_str(), true, p.c_str());
    }
    CHECK(ExpandMemoSize() <= static_cast<size_t>(1u << 20));
    CHECK(Find(("n" + std::to_string(1 << 20) + ".rsc").c_str(), true) != nullptr);
    ExpandMemoClear();
}
