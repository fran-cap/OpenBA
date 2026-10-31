// src/ext/path_query_first/path_query_first.cpp -- see path_query_first.h. NOT RETAIL:
// fix.path_query_first (docs/extensions/handins/PATHQUERYFIRST.md).
#include "path_query_first.h"

#include "Action/PathSearch.h"
#include "Game/PathfinderThread.h"
#include "ext/path_workers/path_workers.h"
#include "ext/path_query_first/queue_first.h"
#include "system/MemoryAllocator.h"

namespace Ext {
namespace PathQueryFirst {
namespace {

// The settings the query in flight was made under. One at a time: RunPathQuery spins until
// its answer is ready, and the worker reads this before it publishes that answer.
Settings s_query;

void Capture(const PathfinderThread& pf, Settings* out) {
    std::memcpy(out->stepCost, pf.stagedStepCost, sizeof(out->stepCost));
    out->passableTypes = static_cast<uint32_t>(pf.stagedPassableTypes);
    out->allowDiagonal = pf.stagedDiagonal;
    out->manhattan = pf.stagedManhattan;
    out->respectBlocked = pf.stagedRespectBlocked;
}

}  // namespace

bool RequestFirst(PathfinderThread* pf, PathTicket* ticket, int start, int goal) {
    if (pf->runInline != 0 || Ext::PathWorkers::Engaged()) return false;
    pf->AllocResult(ticket);   // 0x1400a6240, as RequestPath claims its slot
    pf->lock.Lock();
    Capture(*pf, &s_query);
    PathCommand* node = static_cast<PathCommand*>(System::MemoryAllocator::MemAlloc(0x20, 8));
    node->args.type = kCommand;
    node->args.arg1 = start;
    node->args.arg2 = goal;
    node->args.arg3 = ticket->slot;
    PushHead(pf->queueHead, pf->queueTail, pf->queueCount, node);
    pf->lock.Unlock();
    if ((pf->_runFlags & 1) == 0) pf->Start();
    return true;
}

void Apply(PathfinderThread& pf, const PathCommandArgs& cmd) {
    PathCommandArgs search;
    search.type = 5;
    search.arg1 = cmd.arg1;
    search.arg2 = cmd.arg2;
    search.arg3 = cmd.arg3;
    // retail's command 5: the search and the publish
    RunWith(&pf.grid, s_query, [&pf, &search]() { pf.ApplyCommand(search); });
}

}  // namespace PathQueryFirst
}  // namespace Ext
