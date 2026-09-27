// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_TEST_SUPPORT_RECORDING_JOURNAL_H_
#define TAFFY_TEST_SUPPORT_RECORDING_JOURNAL_H_

#include <stddef.h>

#include <deque>
#include <string>
#include <vector>

#include "taffy/common/public/bip_action.h"
#include "taffy/common/public/page_intelligence_service.h"

// The task journal, as a test can inspect it.
//
// In production the journal is the profile's browser-side sequenced SQLite
// writer. The dispatcher starts an asynchronous append immediately before it
// could cause a side effect and waits for the committed callback. Two
// properties of that arrangement are security properties rather than
// bookkeeping, and both are observable from here:
//
//   1. The intent is appended BEFORE the effect. An unrecorded side effect is
//      one nobody can reconcile afterwards, which is the difference between an
//      ambiguous outcome and an invisible one.
//   2. A failed append aborts the action. Nothing reaches a renderer.
//
// So this class does more than collect records. It can be told to refuse the
// next append, which is the only way to test the second property, and it
// records the order of appends against the order of terminal results so a test
// can assert the first.
//
// It records identifiers and decision facts, exactly as DispatchIntentRecord
// carries them. There is no page content in a dispatch intent and there is none
// here.

namespace taffy::test {

class RecordingJournal : public TaskJournalSink {
 public:
  RecordingJournal();
  RecordingJournal(const RecordingJournal&) = delete;
  RecordingJournal& operator=(const RecordingJournal&) = delete;
  ~RecordingJournal() override;

  // TaskJournalSink:
  void RecordDispatching(
      DispatchIntentRecord record,
      std::unique_ptr<TaskJournalAppendCallback> callback) override;
  void RecordTerminalResult(
      ActionResult result,
      std::unique_ptr<TaskJournalAppendCallback> callback) override;

  // Makes the next `count` appends fail. The dispatcher must then refuse the
  // action outright rather than dispatch it unrecorded.
  void FailNextAppends(int count);

  // Holds intent completions so a test can prove that no effect starts merely
  // because an append was requested. CompleteNextAppend releases the oldest
  // one with the supplied durable answer.
  void HoldAppends();
  void CompleteNextAppend(bool committed);
  size_t pending_append_count() const { return pending_appends_.size(); }

  const std::vector<DispatchIntentRecord>& intents() const { return intents_; }
  const std::vector<ActionResult>& terminal_results() const {
    return terminal_results_;
  }

  // The append and terminal events in the order they happened, as short
  // strings. A test asserts on this when the ORDER is the property, because
  // two separate vectors cannot express "the append came first".
  const std::vector<std::string>& sequence() const { return sequence_; }

  int refused_append_count() const { return refused_append_count_; }

  // Every dispatch intent recorded for one action identifier. Empty when the
  // action never reached the journal, which is what a refusal before dispatch
  // must look like.
  std::vector<DispatchIntentRecord> IntentsForAction(
      const ActionId& action_id) const;

  void Clear();

 private:
  struct PendingAppend {
    DispatchIntentRecord record;
    std::unique_ptr<TaskJournalAppendCallback> callback;
  };

  void Complete(PendingAppend append, bool committed);

  std::vector<DispatchIntentRecord> intents_;
  std::vector<ActionResult> terminal_results_;
  std::vector<std::string> sequence_;
  int appends_to_fail_ = 0;
  int refused_append_count_ = 0;
  bool hold_appends_ = false;
  std::deque<PendingAppend> pending_appends_;
};

}  // namespace taffy::test

#endif  // TAFFY_TEST_SUPPORT_RECORDING_JOURNAL_H_
