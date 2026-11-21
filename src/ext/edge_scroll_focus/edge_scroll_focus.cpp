// src/ext/edge_scroll_focus/edge_scroll_focus.cpp -- fix.edge_scroll_focus (docs/retail_bugs.md §37)
#include "ext/edge_scroll_focus/edge_scroll_focus.h"

#include "application/Application.h"
#include "platform_input.h"   // Platform::Input::IsForeground

namespace Ext {
namespace EdgeScrollFocus {

bool WindowIsForeground() {
    ::Application::Application* app = ::Application::Application::_singleton;
    if (app == nullptr) {
        return true;
    }
    return Platform::Input::IsForeground(app->_windowHandle);
}

}  // namespace EdgeScrollFocus
}  // namespace Ext
