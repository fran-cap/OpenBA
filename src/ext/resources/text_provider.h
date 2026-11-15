#pragma once
// ============================================================================
// src/ext/resources/text_provider.h -- the provider for our embedded .rsc text
// ============================================================================
// NOT RETAIL (docs/extensions/KITFREE.md, block RSCTEXT). Resources.h is the
// shared contract; this header is RSCTEXT's own surface: the boot call, and
// what the cross-check harness (src/diag/rsc_text_check.cpp) needs.
// ============================================================================

#include <string>

namespace Ext {
namespace Resources {

// Parses the embedded files (tools/ext_rsc_embed.py -> ext_rsc_text.h) and
// registers the provider for their "Dialog/..." prefixes (the list is in
// resources.cpp). Called from the ext boot (registry_boot.cpp), never there
// under a command-line faithful profile (Ext::GateProfileForced), AND from
// SaveLoadDialog::DoLoad's notice seam under any profile, just before an
// `ext:` pseudo-mod's notice dialog (Dialog/Extensions.rsc:extension*,
// src/ext/load_notice.h) is loaded: a faithful run can open a save made with a
// class B / C extension, and the notice must exist there too
// (docs/known_port_issues.md, row 1330). A command-line faithful profile
// therefore arms the engine's load-miss seam only when such a save is opened.
// Idempotent: the work is done once per run. Logs `[ExtRes]`.
void InitText();

// How many objects every provider has handed the factory so far (the seam's
// count): the run log's proof that the exe built them, not a loose file.
int ProvidedCount();

// The embedded text's objects, as factory paths ("Dialog/Extensions.rsc:dialogRoot"),
// for the cross-check: `index` in [0, TextObjectCount()).
int  TextObjectCount();
bool TextObjectAt(int index, std::string& path);

}  // namespace Resources
}  // namespace Ext
