// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/restored_task_record.h"

namespace taffy {

namespace {

// The disposition table. Two axes — the phase the journal was in, and why the
// process restarted — and every cell is decided here rather than at a call
// site, because the cells that differ are the ones that are easy to get wrong.
RecoveryDisposition DispositionFor(const PersistedTaskJournalEntry& entry,
                                   RestartCause cause) {
  switch (entry.phase) {
    case JournalPhase::kIdle:
    case JournalPhase::kTerminal:
      // Nothing was outstanding. A terminal entry already has its answer, and
      // re-deriving one would overwrite a recorded outcome with a guess.
      return RecoveryDisposition::kNoTaskWasRunning;

    case JournalPhase::kPlanned:
      return RecoveryDisposition::kInterruptedBeforeDispatch;

    case JournalPhase::kAuthorized:
      // The capability was minted and never consumed. Capabilities are
      // one-use and process-lifetime, so it is gone; the task needs fresh
      // authority, not the old plan.
      return RecoveryDisposition::kAuthorityExpired;

    case JournalPhase::kDispatching:
    case JournalPhase::kVerifying:
      if (entry.is_idempotent_read && !entry.effect_is_external) {
        // Asking again costs a request and changes nothing in the world, so
        // this is the one cell where continuing without reconciliation is
        // honest.
        return RecoveryDisposition::kReReadableReadOnlyStep;
      }
      if (cause == RestartCause::kCleanRestart) {
        // A clean restart happens after the browser stopped dispatching, so
        // nothing was in flight across the boundary — but the entry is still
        // unresolved, and an unresolved consequential step is unknown until
        // the verifier says otherwise.
        return entry.effect_is_external
                   ? RecoveryDisposition::kOutcomeUnknown
                   : RecoveryDisposition::kInterruptedBeforeDispatch;
      }
      return RecoveryDisposition::kOutcomeUnknown;
  }
}

}  // namespace

// static
RestoredTaskRecord RestoredTaskRecord::FromJournalEntry(
    const PersistedTaskJournalEntry& entry,
    RestartCause cause) {
  RestoredTaskRecord record;
  record.task_id_ = entry.task_id;
  record.cause_ = cause;
  record.disposition_ = DispositionFor(entry, cause);

  // The action identifier travels so the verifier can reconcile it. Nothing
  // that describes the action travels, because nothing in this class could
  // hold it.
  record.has_unresolved_action_ =
      entry.phase == JournalPhase::kDispatching ||
      entry.phase == JournalPhase::kVerifying ||
      entry.phase == JournalPhase::kAuthorized;
  if (record.has_unresolved_action_) {
    record.unresolved_action_id_ = entry.action_id;
  }

  return record;
}

ContinuationRequirement RestoredTaskRecord::RequiredNextStep() const {
  switch (disposition_) {
    case RecoveryDisposition::kNoTaskWasRunning:
      return ContinuationRequirement::kNothingToDo;

    case RecoveryDisposition::kInterruptedBeforeDispatch:
      // Every handle from before the restart is dead, so the only legal next
      // step is to look at the page again.
      return ContinuationRequirement::kFreshObservationRequired;

    case RecoveryDisposition::kOutcomeUnknown:
      return ContinuationRequirement::kReconciliationRequired;

    case RecoveryDisposition::kReReadableReadOnlyStep:
      return ContinuationRequirement::kFreshObservationRequired;

    case RecoveryDisposition::kAuthorityExpired:
      return ContinuationRequirement::kFreshAuthorizationRequired;
  }
}

bool RestoredTaskRecord::RequiresUserVisibleRecoveryStatus() const {
  switch (disposition_) {
    case RecoveryDisposition::kNoTaskWasRunning:
      return false;

    // Everything else interrupted a task the user started. The user sees
    // recovery status and controls before continuation (system architecture
    // section 11.3), and that is not negotiable for an unknown outcome.
    case RecoveryDisposition::kInterruptedBeforeDispatch:
    case RecoveryDisposition::kOutcomeUnknown:
    case RecoveryDisposition::kReReadableReadOnlyStep:
    case RecoveryDisposition::kAuthorityExpired:
      return true;
  }
}

}  // namespace taffy
