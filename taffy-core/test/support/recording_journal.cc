// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/test/support/recording_journal.h"

#include "base/check.h"
#include "base/strings/strcat.h"

namespace taffy::test {

RecordingJournal::RecordingJournal() = default;
RecordingJournal::~RecordingJournal() {
  while (!pending_appends_.empty()) {
    PendingAppend append = std::move(pending_appends_.front());
    pending_appends_.pop_front();
    Complete(std::move(append), false);
  }
}

void RecordingJournal::RecordDispatching(
    DispatchIntentRecord record,
    std::unique_ptr<TaskJournalAppendCallback> callback) {
  PendingAppend append{std::move(record), std::move(callback)};
  if (hold_appends_) {
    pending_appends_.push_back(std::move(append));
    return;
  }
  const bool committed = appends_to_fail_ == 0;
  if (appends_to_fail_ > 0) {
    --appends_to_fail_;
  }
  Complete(std::move(append), committed);
}

void RecordingJournal::RecordTerminalResult(
    ActionResult result,
    std::unique_ptr<TaskJournalAppendCallback> callback) {
  sequence_.push_back(base::StrCat({"terminal:", result.action_id.value}));
  terminal_results_.push_back(std::move(result));
  callback->Run(true);
}

void RecordingJournal::FailNextAppends(int count) {
  appends_to_fail_ = count;
}

void RecordingJournal::HoldAppends() {
  hold_appends_ = true;
}

void RecordingJournal::CompleteNextAppend(bool committed) {
  CHECK(!pending_appends_.empty());
  PendingAppend append = std::move(pending_appends_.front());
  pending_appends_.pop_front();
  Complete(std::move(append), committed);
}

void RecordingJournal::Complete(PendingAppend append, bool committed) {
  if (committed) {
    sequence_.push_back(
        base::StrCat({"append:", append.record.action_id.value}));
    intents_.push_back(std::move(append.record));
  } else {
    ++refused_append_count_;
    sequence_.push_back(
        base::StrCat({"append-refused:", append.record.action_id.value}));
  }
  append.callback->Run(committed);
}

std::vector<DispatchIntentRecord> RecordingJournal::IntentsForAction(
    const ActionId& action_id) const {
  std::vector<DispatchIntentRecord> out;
  for (const DispatchIntentRecord& record : intents_) {
    if (record.action_id == action_id) {
      out.push_back(record);
    }
  }
  return out;
}

void RecordingJournal::Clear() {
  CHECK(pending_appends_.empty());
  intents_.clear();
  terminal_results_.clear();
  sequence_.clear();
  appends_to_fail_ = 0;
  refused_append_count_ = 0;
  hold_appends_ = false;
}

}  // namespace taffy::test
