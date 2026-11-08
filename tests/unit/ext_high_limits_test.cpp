// ============================================================================
// tests/unit/ext_high_limits_test.cpp -- ext.high_limits
// ============================================================================
// src/ext/high_limits/high_limits.{h,cpp} without the game: the registry row (class B, off by default),
// the retail and high values, and that the values follow the row (the game's locked value). The spinner
// glue (spinner.cpp) and the seam in ResourceLimitUI::OnCreated are the exe's, looked at in a run.
// ============================================================================
#include "unit.h"
#include "ext/high_limits/high_limits.h"
#include "ext/registry.h"
#include "io/settings_file.h"

#include <string>

namespace {

using IO::SettingsFile::Entries;
using namespace Ext::HighLimits;

bool EntriesLookup(void* ctx, const char* key, std::string& value)
{
    const Entries* entries = static_cast<const Entries*>(ctx);
    for (const IO::SettingsFile::Entry& e : *entries) {
        if (e.key == key) {
            value = e.value;
            return true;
        }
    }
    return false;
}

Ext::State ResolveText(const char* text)
{
    Entries entries = IO::SettingsFile::Parse(std::string(text));
    return Ext::Resolve(&EntriesLookup, &entries, nullptr, nullptr);
}

void InstallText(const char* text)
{
    Ext::Install(ResolveText(text), Ext::Profile::PerEntry, false);
}

int Digits(int32_t v)
{
    int n = 0;
    do {
        ++n;
        v /= 10;
    } while (v != 0);
    return n;
}

}  // namespace

UNIT_TEST(ext_high_limits_registry_row) {
    const Ext::Entry* e = Ext::FindEntry("high_limits");
    CHECK(e != nullptr);
    if (!e)
        return;
    CHECK(e->tier == Ext::Tier::Extension);
    CHECK(e->cls == Ext::Class::B);   // no per-town state, but a value the faithful spinner cannot show
    CHECK(e->defaultOn);   // ruled 2026-11-08 for 1.0.3, DESIGN.md section 2
    CHECK(Ext::SettingsKey(*e) == "ext.high_limits");
    CHECK(ResolveText("").ext.high_limits);   // ON by default (ruled 2026-11-08 for 1.0.3)
    CHECK(ResolveText("ext.high_limits=1\n").ext.high_limits);
    CHECK(!ResolveText("profile=faithful\n").ext.high_limits);   // the faithful profile turns it off
}

UNIT_TEST(ext_high_limits_the_values_are_consistent) {
    CHECK_EQ(kRetailMax, 999999);       // the SetMinMax in OnCreated and the description's _maxValue
    CHECK_EQ(kRetailDigits, 6);         // the description's _maxLength: exactly the digits of kRetailMax
    CHECK_EQ(Digits(kRetailMax), kRetailDigits);
    CHECK_EQ(kHighMax, 999999999);
    CHECK_EQ(Digits(kHighMax), kHighDigits);   // typing admits every digit of the largest value
    CHECK(kHighMax > kRetailMax);
    CHECK(kHighMax < 2147483647);       // an int32: the ledger's maxCount and the spinner's value
}

UNIT_TEST(ext_high_limits_the_values_follow_the_row) {
    InstallText("ext.high_limits=0\n");
    CHECK(!Active());
    CHECK_EQ(MaxValue(), kRetailMax);
    CHECK_EQ(MaxDigits(), kRetailDigits);   // off: retail's range
    CHECK_EQ(MinWidth(), kRetailMinWidth);
    InstallText("ext.high_limits=1\n");
    CHECK(Active());
    CHECK_EQ(MaxValue(), kHighMax);
    CHECK_EQ(MaxDigits(), kHighDigits);
    CHECK_EQ(MinWidth(), kHighMinWidth);
    CHECK(kHighMinWidth > kRetailMinWidth);
    InstallText("ext.high_limits=1\nprofile=faithful\n");
    CHECK(!Active());   // the faithful profile wins
    CHECK_EQ(MaxValue(), kRetailMax);
    InstallText("");
}
