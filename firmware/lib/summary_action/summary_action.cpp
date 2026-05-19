// EMS DoseSync Pro — summary sub-menu 決策純函式實作

#include "summary_action.h"

namespace ems {

SummaryAction decide_summary_action(SubmenuCursor cursor,
                                    bool history_mode,
                                    bool already_in_sync,
                                    bool already_synced) {
    switch (cursor) {
        case SubmenuCursor::TIMELINE:
            if (history_mode) {
                return SummaryAction::TIMELINE_NOOP_HISTORY;
            }
            return SummaryAction::TIMELINE_SHOW;

        case SubmenuCursor::SYNC:
            if (already_in_sync) {
                return SummaryAction::SYNC_BLOCKED_REENTRY;
            }
            if (already_synced) {
                return SummaryAction::CONFIRM_RESYNC;
            }
            return SummaryAction::START_SYNC;

        default:
            return SummaryAction::UNKNOWN_CURSOR;
    }
}

}  // namespace ems
