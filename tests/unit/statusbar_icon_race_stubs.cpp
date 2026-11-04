// ============================================================================
// tests/unit/statusbar_icon_race_stubs.cpp -- what src/game/Dialog/statusbarui.cpp names but
// the unit build does not link (statusbar_icon_race_test.cpp, review S-006)
// ============================================================================
// The test drives only StatusBarUI::OnElementEvent and its handler (0x14007a3a0 / 0x14007a120).
// OnOpened and OnUpdateLayout name the element bodies below; each aborts loudly, so a test
// that does drive them has to link the real translation unit.
// ============================================================================
#include "ui/Element.h"
#include "ui/TextElement.h"
#include "ui/DialogInstance.h"
#include "ui/String.h"
#include "Dialog/RatingGroup.h"
#include "Game/exe_bodies.h"
#include "Component/StatusIconComponent.h"
#include "Component/CitizenDescription.h"

#include <cstdio>
#include <cstdlib>

namespace {
[[noreturn]] void Unlinked(const char* what) {
    std::fprintf(stderr, "[unit] %s is not linked into the unit build; see statusbar_icon_race_stubs.cpp.\n", what);
    std::abort();
}
::Object::Base* NoObject() { return nullptr; }
::Object::Base* NoObjectInPlace(void*) { return nullptr; }
}  // namespace

void UI::Element::SetElementId(int32_t) { Unlinked("UI::Element::SetElementId"); }
void UI::Element::SetFlagsOnOff(uint32_t, uint32_t) { Unlinked("UI::Element::SetFlagsOnOff"); }
void UI::TextElement::SetParameter(int32_t, const System::String&) { Unlinked("UI::TextElement::SetParameter"); }
void UI::TextElement::SetText(const UI::String*) { Unlinked("UI::TextElement::SetText"); }
Object::InternalPtr<UI::Element> UI::DialogInstance::FindElement(const System::String&) {
    Unlinked("UI::DialogInstance::FindElement");
}
void TownHall_GroupRatingInit(RatingGroup*, UI::Element* const*) { Unlinked("TownHall_GroupRatingInit"); }
GameSettings* g_gameSettings = nullptr;

// The two type records statusbarui.cpp reads TypeId() from; their classes' translation units are
// not linked. The ids stay 0xFFFFFFFF (no Factory::Startup in the unit build).
::Object::BaseTypeInfo StatusIconComponent::_typeInfo("StatusIconComponent", "Component", nullptr, &NoObject,
                                                      &NoObjectInPlace);
::Object::BaseTypeInfo CitizenDescription::_typeInfo("CitizenDescription", "Object::External", nullptr, &NoObject,
                                                     &NoObjectInPlace);
