# UPDATECHECK hand-in

Branch `ext/update-check`. The task: tell a player in the game that a newer OpenBA release is out.
**Result: `ext.update_check`, class A, on by default, takes effect at the next start.**

## 1. What retail does
Nothing: retail opens no connection. Its main menu's build line (`Dialog/BuildId.rsc:textBuildNotice`)
shows the game version and the build stamp; OpenBA's own stamp follows it (README "Versioning").

## 2. What the extension does
With the row on, the build line gains ` - v1.0.5 available` when the project's newest release is newer
than the exe:

    17Se10  OpenBA v1.0.4 2424a9093 2026-10-19 - v1.0.5 available

* **The request.** One `GET https://api.github.com/repos/fran-cap/OpenBA/releases/latest` (the newest
  release that is neither a draft nor a pre-release; the Release workflow tags `v<version>`), with
  `OpenBA/<version>` as the User-Agent and nothing else. A detached worker thread started at boot
  (`Ext::UpdateCheck::Boot`, from `Ext::InitFromSettings`) makes it, so the boot never waits for it.
* **Once a day.** An answer of any status stores the UTC day; a start on the same day asks nothing.
  GitHub allows an unauthenticated address 60 requests an hour, so one a day per install cannot reach
  it. No answer at all (no network, a timeout) stores nothing, and the next start asks again.
* **What is kept.** Two plain `settings.cfg` keys, read and written by `update_check.cpp` only (not
  registry rows: they are state, not settings): `ext.update_check_day` and `ext.update_check_seen`
  (the newest version an answer named, packed `major * 1000000 + minor * 1000 + patch`). The notice
  is drawn from `seen`, so every later start shows it at its first menu without asking, and it goes
  away once the exe is that version or newer.
* **Threads.** The worker stores its answer in one atomic. `MenuNotice` (the menu's `OnOpened`, main
  thread) records a finished answer into the settings map and flushes. An answer that lands after the
  menu opened is shown at the menu's next open (the pause menu, a return to the menu) or the next start.
* **Off.** The row off, or the gate's faithful profile: no thread, no request, no text.

## 3. The seams
| Where | What |
|---|---|
| `src/game/Dialog/mainmenudialog.cpp`, `OnOpened`, the BuildId child | `buildIdStr += Ext::UpdateCheck::MenuNotice()` after OpenBA's own stamp |
| `src/ext/registry_boot.cpp`, `InitFromSettings` | `Ext::UpdateCheck::Boot()` |
| `src/platform/common/platform_net.h` | `Platform::Net::CanHttpsGet` / `HttpsGet`, one twin per platform |

## 4. The platform hook
`Platform::Net::HttpsGet(host, path, userAgent, timeoutMs, maxBytes, body)` is one blocking HTTPS GET
on the system's TLS stack and trust store, certificate verified, the whole call bounded by the timeout;
it returns the HTTP status, or 0 when no response was read. The contract is the header's banner. A
platform without a body answers `CanHttpsGet()` false and the extension logs one `[UpdateCheck] not
supported on this platform` line and does nothing.

| Platform | Twin | Body |
|---|---|---|
| Windows | `src/platform/win/net_win.cpp` | not supported yet |
| Linux | `src/platform/posix/net_posix.cpp` | the system's libcurl, opened at run time (`libcurl.so.4`, then `libcurl-gnutls.so.4`) with its TLS library and CA bundle; not shipped, no link-time dependency, no curl headers at build time. Without it, not supported |
| macOS | the posix twin (no darwin twin) | the same file: the OS's own `/usr/lib/libcurl.4.dylib` (Apple's build, LibreSSL with `/etc/ssl/cert.pem`) |
| Android | the posix twin, or `src/platform/android/net_android.cpp` | not supported yet |

## 5. Tests
`tests/unit/ext_update_check_test.cpp`: a tag packed and printed, the tag found in an answer (compact,
pretty-printed, a quoted key in the notes skipped, error answers and cut-off bodies refused), the
once-a-day rule, the notice's text, this build's own version readable, the row's class and default.

## 6. Not done
* The line is text: it is not a button, and nothing opens the releases page.
* A pre-release tag (`v1.1.0-rc1`) is never announced: `releases/latest` does not return one, and
  `PackVersion` refuses it.
