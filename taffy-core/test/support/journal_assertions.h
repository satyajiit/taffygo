// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_TEST_SUPPORT_JOURNAL_ASSERTIONS_H_
#define TAFFY_TEST_SUPPORT_JOURNAL_ASSERTIONS_H_

#include "taffy/common/public/bip_action.h"
#include "taffy/common/public/bip_identity.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy::test {

class RecordingJournal;

// The journal properties every action test should assert, written once.
//
// Two of them are orderings and one is an absence, and all three are the kind
// of property a test written by hand gets subtly wrong. "The intent was
// journalled" is easy and nearly worthless; "the intent was journalled BEFORE
// the side effect" is the property that makes an ambiguous outcome
// reconcilable, and it can only be checked against the interleaved sequence.
//
// Each helper returns an AssertionResult rather than failing directly, so a
// caller can use it inside EXPECT_TRUE and add its own context.

class JournalAssertions {
 public:
  JournalAssertions() = delete;

  // The dispatch intent for `action_id` was appended, and appended before the
  // terminal result for the same action was recorded.
  [[nodiscard]] static ::testing::AssertionResult IntentPrecedesTerminalResult(
      const RecordingJournal& journal,
      const ActionId& action_id);

  // Exactly one dispatch intent exists for `action_id`. Two would mean the
  // action was dispatched twice, which is the duplicate-effect failure the
  // idempotency rules exist to prevent.
  [[nodiscard]] static ::testing::AssertionResult ExactlyOneIntent(
      const RecordingJournal& journal,
      const ActionId& action_id);

  // No dispatch intent exists for `action_id`. This is what a refusal before
  // dispatch must look like: nothing was attempted, so nothing has to be
  // reconciled.
  [[nodiscard]] static ::testing::AssertionResult NoIntent(
      const RecordingJournal& journal,
      const ActionId& action_id);

  // The recorded intent binds the action to the exact document it was
  // authorized against. A journal entry that recorded the tab but not the epoch
  // could not distinguish an action on this document from one on its successor.
  [[nodiscard]] static ::testing::AssertionResult IntentBindsDocument(
      const RecordingJournal& journal,
      const ActionId& action_id,
      const TabId& tab_id,
      const FrameId& frame_id,
      const PageEpoch& page_epoch);

  // The intent records the capability and the lease that authorized it. Without
  // both, an audit cannot answer "under what authority did this happen".
  [[nodiscard]] static ::testing::AssertionResult IntentRecordsAuthority(
      const RecordingJournal& journal,
      const ActionId& action_id,
      const CapabilityReference& capability_reference,
      const ActorLeaseId& actor_lease_id);

  // Every terminal result the journal saw carries the repeat-safety verdict,
  // and a non-idempotent action that ended ambiguously is never marked safe to
  // repeat.
  [[nodiscard]] static ::testing::AssertionResult
  AmbiguousOutcomesAreNotRepeatable(const RecordingJournal& journal);
};

}  // namespace taffy::test

#endif  // TAFFY_TEST_SUPPORT_JOURNAL_ASSERTIONS_H_
