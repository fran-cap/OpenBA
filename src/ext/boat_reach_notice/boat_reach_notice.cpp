// ============================================================================
// src/ext/boat_reach_notice/boat_reach_notice.cpp -- fix.boat_reach_notice (NOT RETAIL)
// ============================================================================
// docs/retail_bugs.md §28; the header has the mechanism. The line is an ordinary event-log entry,
// built as EventLog::AddEventCore builds one (the date prefix the game's messages carry, the
// severity colour, the post's tile), filed under the trader's event type 0xd so the event log
// and the ribbon file it with the merchant messages and show the trader icon. The game's own
// message table has no text for it, so the text is ours. A save keeps it as it keeps every entry
// (text and the five ints, EventLog::Serialize): retail reads it back as an ordinary line.
// ============================================================================
#include "ext/boat_reach_notice/boat_reach_notice.h"

#include "Game/GameUpdateLoop.h"
#include "Game/EventLog.h"
#include "Entity/ComponentEntity.h"
#include "object/Internal.h"
#include "system/String.h"

// eventlog.cpp's 0x140095030 (the entity's footprint centre as a map tile), its exported twin.
extern "C" int EventLog_TileIndex_Reg(void* unused, const ::Object::InternalBasePtr* entityIBP);

namespace Ext {
namespace BoatReachNotice {

namespace {

constexpr int32_t kTraderEventType = 0xd;    // TradeComponent::OnMerchantDocked's type
constexpr int32_t kTraderIcon = 5;           // EventLog_IconIndex(0xd) (0x1400950d0)
constexpr int32_t kSeverityWarning = 1;      // yellow, EventLog_SeverityColor(1)
constexpr uint32_t kWarningColor = 0xFFFFFF00u;

}  // namespace

void OnLegsFailed(ComponentEntity* post) {
    GameUpdateLoop* loop = GameUpdateLoop::_singleton;
    EventLog* log = loop != nullptr ? loop->m_eventNotifier : nullptr;
    if (log == nullptr || post == nullptr) return;

    const System::String season = log->SeasonString();
    const System::String year = log->YearString();
    const System::String text = System::String::Format(
        "%s %s: A merchant's boat found no water route to a trading post's dock. "
        "Boats need deep water joining the river to the dock.",
        season._data != nullptr ? season._data : "", year._data != nullptr ? year._data : "");

    Object::InternalPtr<ComponentEntity> postIBP(post);
    const int32_t tile = EventLog_TileIndex_Reg(nullptr, &postIBP);
    const EventLogEntry entry(kWarningColor, text, kTraderEventType, kSeverityWarning, tile, kTraderIcon, -1);
    const int count = log->entries.GetSize();
    log->entries.SetSize(count + 1, entry);
}

}  // namespace BoatReachNotice
}  // namespace Ext
