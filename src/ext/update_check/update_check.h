// ============================================================================
// src/ext/update_check/update_check.h -- ext.update_check (class A, default ON): "a newer release is out"
// ============================================================================
// NOT RETAIL. docs/extensions/handins/UPDATECHECK.md. Retail never touches the network. With the row
// on, the main menu's build line gains " - v1.0.5 available" when the project's newest release is
// newer than this exe:
//
//   17Se10  OpenBA v1.0.4 2424a9093 2026-10-19 - v1.0.5 available
//
// THE REQUEST. One GET of https://api.github.com/repos/fran-cap/OpenBA/releases/latest (the newest
// release that is neither a draft nor a pre-release), through Platform::Net::HttpsGet
// (src/platform/common/platform_net.h), on a detached worker thread started at boot. Nothing is sent
// but the request and "OpenBA/<version>" as the User-Agent. At most ONE request per calendar day
// (UTC) per install: an unauthenticated address may ask that server 60 times an hour. No network, a
// timeout or a platform without the hook: nothing happens and the next start asks again.
//
// WHAT IS KEPT (settings.cfg, plain keys this file reads and writes; they are not registry rows):
//   ext.update_check_day   the UTC day (days since 1970) of the last answer, whatever it said
//   ext.update_check_seen  the newest release version an answer named, packed (release_tag.h)
// so the line is shown from the first menu of every later start without asking again, and goes
// away by itself once the exe is that version or newer.
//
// THREADS. The worker only stores its answer in an atomic. The settings map is the main thread's:
// MenuNotice (the menu's OnOpened) collects a finished answer, records the two keys and flushes.
// An answer that arrives after the menu opened is shown at the menu's next open, or the next start.
//
// The gate's faithful profile turns the row off: no thread, no request, the build line retail's
// plus the stamp it always had.
// ============================================================================
#pragma once

#include <string>

namespace Ext {
namespace UpdateCheck {

// Boot (Ext::InitFromSettings, main thread, once): reads the two keys and, with the row on, a
// platform that can ask and no answer yet today, starts the worker.
void Boot();

// MainMenuDialog::OnOpened (main thread): what the build line gains, "" with the row off or no
// newer release known. Records a finished answer first.
std::string MenuNotice();

}  // namespace UpdateCheck
}  // namespace Ext
