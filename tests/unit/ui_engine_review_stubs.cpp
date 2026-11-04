// ============================================================================
// tests/unit/ui_engine_review_stubs.cpp — the symbols userinterface.cpp,
// widgets_input.cpp and documenttext.cpp reference but do not own
// ============================================================================
// ui_engine_review_test.cpp drives the real UserInterface::UpdateEvents /
// CleanRemovedDialogs / Play, ComboElement::UpdateText and
// DocumentTextElement::GetNextItem. Their translation units name bodies that
// live in files the host build cannot link (application, input, video, the
// rest of the UI tree). None of the tests reaches them: each stub aborts
// loudly rather than fake a body, so a future test that does reach one has
// to link the real file (and delete the stub, tests/unit/README.md).
// The three class statics are plain null data slots, as in the exe before boot.
// ============================================================================
#include "application/Application.h"
#include "input/Input.h"
#include "video/Device.h"
#include "ui/UserInterface.h"
#include "ui/DialogInstance.h"
#include "ui/Element.h"
#include "ui/Font.h"

#include <cstdio>
#include <cstdlib>

namespace {
[[noreturn]] void UnlinkedUiBody(const char* what) {
    std::fprintf(stderr, "[unit] %s is not linked into the unit build; see ui_engine_review_stubs.cpp.\n",
                 what);
    std::abort();
}
}  // namespace

Application::Application* Application::Application::_singleton = nullptr;
Input::Configuration* Input::Configuration::_singleton = nullptr;
Video::Device* Video::Device::_singleton = nullptr;
float (*UI::UserInterface::_extDialogScale)(const UI::DialogInstance*) = nullptr;

void Application::Application::EnableCursor(bool) {
    UnlinkedUiBody("Application::EnableCursor");
}

namespace UI {

Object::InternalPtr<Element> DialogInstance::GetRoot() {
    UnlinkedUiBody("DialogInstance::GetRoot");
}

int32_t DialogInstance::GetMouseCursorIndex(Object::InternalPtr<Element>, int32_t, int32_t) const {
    UnlinkedUiBody("DialogInstance::GetMouseCursorIndex");
}

Object::InternalPtr<Element> Element::FindElement(const System::String&) {
    UnlinkedUiBody("Element::FindElement");
}

Object::InternalPtr<Element> Element::FindElementType(uint32_t) {
    UnlinkedUiBody("Element::FindElementType");
}

int32_t Font::ComputeLineLayout(const char*, int32_t, int32_t, int32_t, int32_t, int32_t,
                                Layout*) const {
    UnlinkedUiBody("Font::ComputeLineLayout");
}

void UserInterface::CloseDialog(Object::InternalPtr<DialogInstance>*) {
    UnlinkedUiBody("UserInterface::CloseDialog");
}

}  // namespace UI
