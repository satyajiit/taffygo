// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/test/support/journal_assertions.h"

#include <string>

#include "base/strings/strcat.h"
#include "taffy/test/support/recording_journal.h"

namespace taffy::test {

namespace {

std::string SequenceText(const RecordingJournal& journal) {
  std::string out;
  for (const std::string& entry : journal.sequence()) {
    out = base::StrCat({out, "\n  ", entry});
  }
  return out.empty() ? std::string("\n  (the journal recorded nothing)") : out;
}

}  // namespace

// static
::testing::AssertionResult JournalAssertions::IntentPrecedesTerminalResult(
    const RecordingJournal& journal,
    const ActionId& action_id) {
  const std::string append = base::StrCat({"append:", action_id.value});
  const std::string terminal = base::StrCat({"terminal:", action_id.value});

  size_t append_at = journal.sequence().size();
  size_t terminal_at = journal.sequence().size();
  for (size_t index = 0; index < journal.sequence().size(); ++index) {
    if (journal.sequence()[index] == append &&
        append_at == journal.sequence().size()) {
      append_at = index;
    }
    if (journal.sequence()[index] == terminal &&
        terminal_at == journal.sequence().size()) {
      terminal_at = index;
    }
  }

  if (append_at == journal.sequence().size()) {
    return ::testing::AssertionFailure()
           << "No dispatch intent was journalled for action "
           << action_id.value
           << ". An unrecorded side effect is one nobody can reconcile "
              "afterwards."
           << SequenceText(journal);
  }
  if (terminal_at == journal.sequence().size()) {
    return ::testing::AssertionFailure()
           << "No terminal result was journalled for action " << action_id.value
           << SequenceText(journal);
  }
  if (append_at > terminal_at) {
    return ::testing::AssertionFailure()
           << "The dispatch intent for action " << action_id.value
           << " was journalled AFTER its terminal result. The append has to "
              "happen before the side effect, or an ambiguous outcome becomes "
              "an invisible one."
           << SequenceText(journal);
  }
  return ::testing::AssertionSuccess();
}

// static
::testing::AssertionResult JournalAssertions::ExactlyOneIntent(
    const RecordingJournal& journal,
    const ActionId& action_id) {
  const size_t count = journal.IntentsForAction(action_id).size();
  if (count == 1) {
    return ::testing::AssertionSuccess();
  }
  return ::testing::AssertionFailure()
         << "Action " << action_id.value << " has " << count
         << " dispatch intents; exactly one is the only correct number. More "
            "than one means the action was dispatched twice, which is the "
            "duplicate-effect failure the idempotency rules exist to prevent."
         << SequenceText(journal);
}

// static
::testing::AssertionResult JournalAssertions::NoIntent(
    const RecordingJournal& journal,
    const ActionId& action_id) {
  const size_t count = journal.IntentsForAction(action_id).size();
  if (count == 0) {
    return ::testing::AssertionSuccess();
  }
  return ::testing::AssertionFailure()
         << "Action " << action_id.value << " reached the journal " << count
         << " time(s), but this test expects it to have been refused before "
            "dispatch. A refusal that still journalled an intent means the "
            "refusal happened after something was attempted."
         << SequenceText(journal);
}

// static
::testing::AssertionResult JournalAssertions::IntentBindsDocument(
    const RecordingJournal& journal,
    const ActionId& action_id,
    const TabId& tab_id,
    const FrameId& frame_id,
    const PageEpoch& page_epoch) {
  const auto intents = journal.IntentsForAction(action_id);
  if (intents.size() != 1) {
    return ::testing::AssertionFailure()
           << "Expected exactly one intent for action " << action_id.value
           << " before checking what it bound; found " << intents.size() << ".";
  }
  const DispatchIntentRecord& record = intents.front();
  if (record.tab_id != tab_id || record.frame_id != frame_id ||
      record.page_epoch != page_epoch) {
    return ::testing::AssertionFailure()
           << "The journalled intent for action " << action_id.value
           << " does not bind the document it was authorized against.\n"
           << "  expected tab " << tab_id.value << " frame " << frame_id.value
           << " epoch " << page_epoch.value << "\n"
           << "  recorded tab " << record.tab_id.value << " frame "
           << record.frame_id.value << " epoch " << record.page_epoch.value
           << "\n"
           << "An entry that records the tab but not the epoch cannot tell an "
              "action on this document from one on its successor.";
  }
  return ::testing::AssertionSuccess();
}

// static
::testing::AssertionResult JournalAssertions::IntentRecordsAuthority(
    const RecordingJournal& journal,
    const ActionId& action_id,
    const CapabilityReference& capability_reference,
    const ActorLeaseId& actor_lease_id) {
  const auto intents = journal.IntentsForAction(action_id);
  if (intents.size() != 1) {
    return ::testing::AssertionFailure()
           << "Expected exactly one intent for action " << action_id.value
           << "; found " << intents.size() << ".";
  }
  const DispatchIntentRecord& record = intents.front();
  if (record.capability_reference != capability_reference ||
      record.actor_lease_id != actor_lease_id) {
    return ::testing::AssertionFailure()
           << "The journalled intent for action " << action_id.value
           << " does not record the authority it acted under.\n"
           << "  expected capability " << capability_reference.value
           << " lease " << actor_lease_id.value << "\n"
           << "  recorded capability " << record.capability_reference.value
           << " lease " << record.actor_lease_id.value << "\n"
           << "Without both, an audit cannot answer under what authority this "
              "happened.";
  }
  return ::testing::AssertionSuccess();
}

// static
::testing::AssertionResult JournalAssertions::AmbiguousOutcomesAreNotRepeatable(
    const RecordingJournal& journal) {
  for (const ActionResult& result : journal.terminal_results()) {
    if (!IsAmbiguousOutcome(result.result_code)) {
      continue;
    }
    if (!result.repeat_may_duplicate_effect) {
      return ::testing::AssertionFailure()
             << "Action " << result.action_id.value
             << " ended with an ambiguous outcome and was still marked safe to "
                "repeat. The browser could not establish whether the effect "
                "happened, so repeating it is exactly how a duplicate external "
                "effect is produced.";
    }
  }
  return ::testing::AssertionSuccess();
}

}  // namespace taffy::test
