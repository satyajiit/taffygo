// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_RESTORED_TASK_RECORD_H_
#define TAFFY_BROWSER_RESTORED_TASK_RECORD_H_

#include <stdint.h>

#include <type_traits>

#include "taffy/components/intelligence/content/observability_recorder.h"

// What survives a process death, and what deliberately does not
// (PAR-TAB-004: "Restored task is paused/interrupted; no queued action is
// replayed blindly"; REQ-AG-007; system architecture section 11.3).
//
// The requirement is usually written as a rule — "never replay a queued action
// after recovery" — and a rule in a comment is a rule somebody breaks under
// deadline. This file makes it a property of the type instead.
//
// **A RestoredTaskRecord cannot express a replay, because it cannot hold one.**
// Every member is a fixed-width integer, an enum, or a bounded identifier
// buffer, and the class is asserted trivially copyable. That makes it
// impossible to add a target node handle, a set of action parameters, a
// destination URL, a capability reference, or a form value: none of those fit
// in a trivially copyable record, and adding one is a compile error with a
// message that names this rule.
//
// So the restored state can say *that* an action was in flight and *which*
// action identifier it had, which is what reconciliation needs. It cannot say
// what the action was going to do, which is what a replay would need. The
// profile's browser-side task-action journal holds only bounded identity,
// decision and result facts; it is never handed to the dispatcher on a
// recovery path. No shipping Rust audit engine owns that journal.
//
// The other half of the rule is that authority does not survive either.
// Capabilities are one-use and actor leases are process-lifetime, so both are
// gone by construction after a restart; kAuthorityExpired records that, and a
// continuation must obtain fresh authority through the ordinary path.

namespace taffy {

// Where the journal entry was when the process died.
enum class JournalPhase : uint8_t {
  // Nothing was recorded for this task beyond its existence.
  kIdle = 0,
  kPlanned = 1,
  kAuthorized = 2,
  // The browser had handed the action on and had not yet seen a result.
  kDispatching = 3,
  // The action was acknowledged and the postcondition verifier had not
  // finished.
  kVerifying = 4,
  // A terminal result was written. Nothing is outstanding.
  kTerminal = 5,
};

// Why the process restarted. The disposition depends on it: a clean restart
// and an eviction leave the same journal, and they do not deserve the same
// answer, because only one of them may have interrupted an effect in flight.
enum class RestartCause : uint8_t {
  kUnknown = 0,
  kCleanRestart = 1,
  // Android reclaimed the process. PAR-TAB-004 and PAR-AND-007.
  kProcessEviction = 2,
  kBrowserCrash = 3,
  kApplicationUpdate = 4,
};

// What the restored task is, stated so that no consumer has to infer it.
enum class RecoveryDisposition : uint8_t {
  kNoTaskWasRunning = 0,
  // The task was between steps and nothing had been dispatched. It is
  // interrupted, not lost.
  kInterruptedBeforeDispatch = 1,
  // Something was in flight and the browser cannot prove whether the effect
  // happened. Never repeated automatically (protocol result taxonomy
  // kOutcomeUnknown).
  kOutcomeUnknown = 2,
  // The step was read-only and carries an idempotency key, so asking again
  // costs a request and changes nothing in the world.
  kReReadableReadOnlyStep = 3,
  // The capability or the actor lease died with the process.
  kAuthorityExpired = 4,
};

// The only continuations this type can name. Read the list for what is not
// here: there is no value meaning "dispatch the recorded action again".
enum class ContinuationRequirement : uint8_t {
  kNothingToDo = 0,
  // The user sees recovery status and controls before anything continues
  // (system architecture section 11.3).
  kUserMustSeeRecoveryStatus = 1,
  // The page must be observed afresh; every handle from before the restart is
  // dead.
  kFreshObservationRequired = 2,
  // A new capability and a new actor lease must be obtained through the
  // ordinary approval path.
  kFreshAuthorizationRequired = 3,
  // The verifier must reconcile the current browser and site state before the
  // task may say anything about the outcome.
  kReconciliationRequired = 4,
};

// One journal entry as it was persisted. Bounded and content-free for the same
// reason the record is: a persisted entry that could hold an action payload
// would be a persisted replay waiting to happen.
struct PersistedTaskJournalEntry {
  RecordIdentifier task_id;
  RecordIdentifier action_id;
  RecordIdentifier dispatch_id;
  JournalPhase phase = JournalPhase::kIdle;

  // True when repeating the step could cause an effect outside the browser —
  // a form submission, a message, a purchase. False for a read.
  bool effect_is_external = false;

  // True when the step is read-only and was recorded with an idempotency key.
  bool is_idempotent_read = false;
};

class RestoredTaskRecord {
 public:
  static RestoredTaskRecord FromJournalEntry(
      const PersistedTaskJournalEntry& entry,
      RestartCause cause);

  const RecordIdentifier& task_id() const { return task_id_; }

  // The identifier of the action that was in flight, for reconciliation. It
  // names an action; it does not describe one, and nothing in this class can.
  const RecordIdentifier& unresolved_action_id() const {
    return unresolved_action_id_;
  }
  bool has_unresolved_action() const { return has_unresolved_action_; }

  RecoveryDisposition disposition() const { return disposition_; }
  RestartCause cause() const { return cause_; }

  ContinuationRequirement RequiredNextStep() const;

  // True when the user must be shown that the task was interrupted before
  // anything else happens. Never false for a disposition that touched the
  // world.
  bool RequiresUserVisibleRecoveryStatus() const;

 private:
  RestoredTaskRecord() = default;

  RecordIdentifier task_id_;
  RecordIdentifier unresolved_action_id_;
  bool has_unresolved_action_ = false;
  RecoveryDisposition disposition_ = RecoveryDisposition::kNoTaskWasRunning;
  RestartCause cause_ = RestartCause::kUnknown;
};

// The enforcement. If a future change adds an owning member — a std::string
// of parameters, a std::vector of node handles, a GURL destination — the build
// stops here with a message naming the rule it broke.
static_assert(std::is_trivially_copyable_v<RestoredTaskRecord>,
              "A restored task record carries identifiers and enums only. An "
              "owning member would let a queued action's payload survive a "
              "restart, and a payload that survives is a payload something "
              "will eventually replay. PAR-TAB-004 forbids it.");
static_assert(std::is_trivially_copyable_v<PersistedTaskJournalEntry>,
              "A persisted journal entry carries identifiers and enums only, "
              "for the same reason as the record it produces.");

// A second tripwire on the same rule, in case a future member is trivially
// copyable but large — a fixed-size character array holding a serialized
// action would satisfy the assertion above. Three bounded identifiers plus a
// handful of scalars is the whole design; anything materially larger is a
// payload wearing a disguise.
static_assert(sizeof(RestoredTaskRecord) <= 3 * (kMaxIdentifierChars + 1) + 16,
              "A restored task record grew past the size of the identifiers it "
              "is allowed to hold. Check whether the new member is an action "
              "payload in a fixed-size buffer.");

}  // namespace taffy

#endif  // TAFFY_BROWSER_RESTORED_TASK_RECORD_H_
