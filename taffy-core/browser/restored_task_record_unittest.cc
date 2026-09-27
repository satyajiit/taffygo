// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/restored_task_record.h"

#include <string_view>
#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

// PAR-TAB-004: a restored task is interrupted, and no queued action is
// replayed blindly.
//
// The first test in this file is the important one, and it is a compile-time
// test: the record cannot express a replay because it cannot hold the payload
// a replay would need. The rest assert the disposition table, which is where a
// mistake would be silent rather than loud.

namespace taffy {
namespace {

PersistedTaskJournalEntry Entry(JournalPhase phase) {
  PersistedTaskJournalEntry entry;
  entry.task_id = ToRecordIdentifier("task_1");
  entry.action_id = ToRecordIdentifier("action_1");
  entry.dispatch_id = ToRecordIdentifier("dispatch_1");
  entry.phase = phase;
  return entry;
}

TEST(RestoredTaskRecordTest, ReplayIsUnrepresentable) {
  // A replay needs the action's target, its parameters and its authority. None
  // of the three can be stored in this type: it is trivially copyable, so no
  // owning member can be added, and it is size-bounded, so no fixed-size
  // buffer can smuggle a serialized payload in either. Both assertions live in
  // the header; naming them here is what makes the rule findable from the
  // test suite.
  static_assert(std::is_trivially_copyable_v<RestoredTaskRecord>);
  static_assert(sizeof(RestoredTaskRecord) <=
                3 * (kMaxIdentifierChars + 1) + 16);

  // And the API offers no continuation that names an action to run. Reading
  // the enumeration is the assertion; the value below exists so that adding a
  // "dispatch the recorded action" member would have to change this line.
  RestoredTaskRecord record = RestoredTaskRecord::FromJournalEntry(
      Entry(JournalPhase::kDispatching), RestartCause::kProcessEviction);
  EXPECT_EQ(ContinuationRequirement::kReconciliationRequired,
            record.RequiredNextStep());
}

TEST(RestoredTaskRecordTest, DispatchingAcrossAnEvictionIsOutcomeUnknown) {
  PersistedTaskJournalEntry entry = Entry(JournalPhase::kDispatching);
  entry.effect_is_external = true;

  RestoredTaskRecord record =
      RestoredTaskRecord::FromJournalEntry(entry, RestartCause::kProcessEviction);

  EXPECT_EQ(RecoveryDisposition::kOutcomeUnknown, record.disposition());
  EXPECT_TRUE(record.has_unresolved_action());
  EXPECT_EQ(std::string_view("action_1"),
            std::string_view(record.unresolved_action_id().chars.data()));
  EXPECT_TRUE(record.RequiresUserVisibleRecoveryStatus());
}

TEST(RestoredTaskRecordTest, VerifyingAcrossACrashIsOutcomeUnknown) {
  PersistedTaskJournalEntry entry = Entry(JournalPhase::kVerifying);
  entry.effect_is_external = true;

  RestoredTaskRecord record =
      RestoredTaskRecord::FromJournalEntry(entry, RestartCause::kBrowserCrash);

  EXPECT_EQ(RecoveryDisposition::kOutcomeUnknown, record.disposition());
  EXPECT_EQ(ContinuationRequirement::kReconciliationRequired,
            record.RequiredNextStep());
}

TEST(RestoredTaskRecordTest, IdempotentReadMayBeAskedAgain) {
  PersistedTaskJournalEntry entry = Entry(JournalPhase::kDispatching);
  entry.is_idempotent_read = true;
  entry.effect_is_external = false;

  RestoredTaskRecord record =
      RestoredTaskRecord::FromJournalEntry(entry, RestartCause::kProcessEviction);

  // Asking again costs a request and changes nothing in the world. The user
  // still sees that the task was interrupted.
  EXPECT_EQ(RecoveryDisposition::kReReadableReadOnlyStep, record.disposition());
  EXPECT_EQ(ContinuationRequirement::kFreshObservationRequired,
            record.RequiredNextStep());
  EXPECT_TRUE(record.RequiresUserVisibleRecoveryStatus());
}

TEST(RestoredTaskRecordTest, AuthorizedButUndispatchedLosesItsAuthority) {
  RestoredTaskRecord record = RestoredTaskRecord::FromJournalEntry(
      Entry(JournalPhase::kAuthorized), RestartCause::kCleanRestart);

  // Capabilities are one-use and actor leases are process-lifetime; both died
  // with the process. Continuing needs fresh authority through the ordinary
  // approval path, never the old approval.
  EXPECT_EQ(RecoveryDisposition::kAuthorityExpired, record.disposition());
  EXPECT_EQ(ContinuationRequirement::kFreshAuthorizationRequired,
            record.RequiredNextStep());
}

TEST(RestoredTaskRecordTest, PlannedWorkIsInterruptedNotLost) {
  RestoredTaskRecord record = RestoredTaskRecord::FromJournalEntry(
      Entry(JournalPhase::kPlanned), RestartCause::kProcessEviction);

  EXPECT_EQ(RecoveryDisposition::kInterruptedBeforeDispatch,
            record.disposition());
  EXPECT_FALSE(record.has_unresolved_action());
  EXPECT_EQ(ContinuationRequirement::kFreshObservationRequired,
            record.RequiredNextStep());
}

TEST(RestoredTaskRecordTest, TerminalEntriesAreLeftAlone) {
  RestoredTaskRecord record = RestoredTaskRecord::FromJournalEntry(
      Entry(JournalPhase::kTerminal), RestartCause::kBrowserCrash);

  // A terminal entry already has its answer. Re-deriving one would overwrite a
  // recorded outcome with a guess.
  EXPECT_EQ(RecoveryDisposition::kNoTaskWasRunning, record.disposition());
  EXPECT_EQ(ContinuationRequirement::kNothingToDo, record.RequiredNextStep());
  EXPECT_FALSE(record.RequiresUserVisibleRecoveryStatus());
}

TEST(RestoredTaskRecordTest, EveryPhaseAndCauseCombinationIsDecided) {
  // The disposition table is two axes and both are small, so cover all of it.
  // The property asserted is the one that must never break: an entry that was
  // in flight never comes back as "nothing was running".
  const std::vector<JournalPhase> phases = {
      JournalPhase::kIdle,       JournalPhase::kPlanned,
      JournalPhase::kAuthorized, JournalPhase::kDispatching,
      JournalPhase::kVerifying,  JournalPhase::kTerminal};
  const std::vector<RestartCause> causes = {
      RestartCause::kUnknown,     RestartCause::kCleanRestart,
      RestartCause::kProcessEviction, RestartCause::kBrowserCrash,
      RestartCause::kApplicationUpdate};

  for (JournalPhase phase : phases) {
    for (RestartCause cause : causes) {
      PersistedTaskJournalEntry entry = Entry(phase);
      entry.effect_is_external = true;
      RestoredTaskRecord record =
          RestoredTaskRecord::FromJournalEntry(entry, cause);

      const bool was_in_flight = phase == JournalPhase::kAuthorized ||
                                 phase == JournalPhase::kDispatching ||
                                 phase == JournalPhase::kVerifying;
      if (was_in_flight) {
        EXPECT_NE(RecoveryDisposition::kNoTaskWasRunning, record.disposition())
            << "phase " << static_cast<int>(phase) << " cause "
            << static_cast<int>(cause);
        EXPECT_TRUE(record.RequiresUserVisibleRecoveryStatus());
        EXPECT_NE(ContinuationRequirement::kNothingToDo,
                  record.RequiredNextStep());
      }
    }
  }
}

}  // namespace
}  // namespace taffy
