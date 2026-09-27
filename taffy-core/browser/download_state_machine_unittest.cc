// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "base/no_destructor.h"
#include "taffy/browser/download_state_machine.h"

#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

// PAR-FILE-001's control matrix, exhaustively. Every state crossed with every
// command, because the table is small enough that "representative coverage"
// would just be a decision not to check some of it.

namespace taffy {
namespace {

DownloadRecord RecordInState(DownloadState state) {
  DownloadRecord record;
  record.download_id = 7;
  record.tab_id = TabId{"tab_1"};
  record.initiator = NavigationInitiator::kUser;
  record.state = state;
  record.target_file_name = "report.pdf";
  record.received_bytes = 1024;
  record.total_bytes = 4096;
  record.is_resumable = true;
  return record;
}

const std::vector<DownloadState>& AllStates() {
  // base::NoDestructor because a function-local static std::vector would need
  // an exit-time destructor, which -Wexit-time-destructors rejects. This is the
  // case NoDestructor exists for, and the mirror of budget_clamp.cc, where the
  // type IS trivially destructible and NoDestructor is therefore refused.
  static const base::NoDestructor<std::vector<DownloadState>> states({
      DownloadState::kCreated,     DownloadState::kInProgress,
      DownloadState::kPaused,      DownloadState::kInterrupted,
      DownloadState::kComplete,    DownloadState::kCancelled});
  return *states;
}

const std::vector<DownloadCommand>& AllCommands() {
  static const base::NoDestructor<std::vector<DownloadCommand>> commands({
      DownloadCommand::kPause,           DownloadCommand::kResume,
      DownloadCommand::kCancel,          DownloadCommand::kRetry,
      DownloadCommand::kOpenWhenComplete, DownloadCommand::kOpenNow});
  return *commands;
}

TEST(DownloadStateMachineTest, TheMatrixIsTotal) {
  // Every cell has an answer. A missing arm in the table would fall off the
  // end of a switch with no default, which the build rejects; this is the
  // run-time half, and it also catches an enumerator added without a row.
  EXPECT_EQ(static_cast<size_t>(DownloadState::kCancelled) + 1,
            AllStates().size());
  EXPECT_EQ(static_cast<size_t>(DownloadCommand::kOpenNow) + 1,
            AllCommands().size());

  for (DownloadState state : AllStates()) {
    for (DownloadCommand command : AllCommands()) {
      EvaluateDownloadCommand(RecordInState(state), command);
    }
  }
}

TEST(DownloadStateMachineTest, PauseOnlyWhileBytesAreArriving) {
  for (DownloadState state : AllStates()) {
    const DownloadCommandLegality legality =
        EvaluateDownloadCommand(RecordInState(state), DownloadCommand::kPause);
    EXPECT_EQ(state == DownloadState::kInProgress
                  ? DownloadCommandLegality::kLegal
                  : DownloadCommandLegality::kIllegalInState,
              legality)
        << "state " << static_cast<int>(state);
  }
}

TEST(DownloadStateMachineTest, CancelIsLegalUntilItIsTerminal) {
  for (DownloadState state : AllStates()) {
    const bool terminal = state == DownloadState::kComplete ||
                          state == DownloadState::kCancelled;
    EXPECT_EQ(terminal ? DownloadCommandLegality::kIllegalInState
                       : DownloadCommandLegality::kLegal,
              EvaluateDownloadCommand(RecordInState(state),
                                      DownloadCommand::kCancel))
        << "state " << static_cast<int>(state);
  }
}

TEST(DownloadStateMachineTest, ResumeIsRefusedWhenChromiumSaysItCannot) {
  DownloadRecord record = RecordInState(DownloadState::kInterrupted);
  EXPECT_EQ(DownloadCommandLegality::kLegal,
            EvaluateDownloadCommand(record, DownloadCommand::kResume));

  record.is_resumable = false;
  // Offering resume for something that cannot resume is worse than not
  // offering it: the control appears to work and nothing happens.
  EXPECT_EQ(DownloadCommandLegality::kNotResumable,
            EvaluateDownloadCommand(record, DownloadCommand::kResume));
  // Retry is the honest offer instead.
  EXPECT_EQ(DownloadCommandLegality::kLegal,
            EvaluateDownloadCommand(record, DownloadCommand::kRetry));
}

TEST(DownloadStateMachineTest, RetryOnlyFromATerminalFailure) {
  for (DownloadState state : AllStates()) {
    const bool terminal_failure = state == DownloadState::kInterrupted ||
                                  state == DownloadState::kCancelled;
    EXPECT_EQ(terminal_failure ? DownloadCommandLegality::kLegal
                               : DownloadCommandLegality::kIllegalInState,
              EvaluateDownloadCommand(RecordInState(state),
                                      DownloadCommand::kRetry))
        << "state " << static_cast<int>(state);
  }
}

TEST(DownloadStateMachineTest, OpeningADangerousFileNeedsConfirmationFirst) {
  DownloadRecord record = RecordInState(DownloadState::kComplete);
  EXPECT_EQ(DownloadCommandLegality::kLegal,
            EvaluateDownloadCommand(record, DownloadCommand::kOpenNow));

  record.requires_danger_confirmation = true;
  // This seam never clears the flag and never opens past it. Chromium's own
  // surface asks for the confirmation; PAR-FILE-008 preserves that baseline
  // from M1.
  EXPECT_EQ(DownloadCommandLegality::kNeedsDangerConfirmation,
            EvaluateDownloadCommand(record, DownloadCommand::kOpenNow));

  DownloadRecord in_flight = RecordInState(DownloadState::kInProgress);
  in_flight.requires_danger_confirmation = true;
  EXPECT_EQ(DownloadCommandLegality::kNeedsDangerConfirmation,
            EvaluateDownloadCommand(in_flight,
                                    DownloadCommand::kOpenWhenComplete));
}

TEST(DownloadStateMachineTest, OpenNowOnlyAfterTheBytesArrived) {
  for (DownloadState state : AllStates()) {
    EXPECT_EQ(state == DownloadState::kComplete
                  ? DownloadCommandLegality::kLegal
                  : DownloadCommandLegality::kIllegalInState,
              EvaluateDownloadCommand(RecordInState(state),
                                      DownloadCommand::kOpenNow))
        << "state " << static_cast<int>(state);
  }
}

TEST(DownloadStateMachineTest, OnlyRetryStartsANewRequest) {
  for (DownloadCommand command : AllCommands()) {
    EXPECT_EQ(command == DownloadCommand::kRetry,
              DownloadCommandStartsANewRequest(command))
        << "command " << static_cast<int>(command);
  }
}

TEST(DownloadStateMachineTest, ProgressReportsUnknownRatherThanZero) {
  DownloadRecord unknown_size = RecordInState(DownloadState::kInProgress);
  unknown_size.total_bytes = -1;
  // A surface that shows a bar rather than a spinner has to know which it has.
  EXPECT_LT(DownloadProgressFraction(unknown_size), 0.0);

  DownloadRecord half = RecordInState(DownloadState::kInProgress);
  half.received_bytes = 2048;
  half.total_bytes = 4096;
  EXPECT_DOUBLE_EQ(0.5, DownloadProgressFraction(half));

  DownloadRecord finished = RecordInState(DownloadState::kComplete);
  finished.received_bytes = 4096;
  finished.total_bytes = 4096;
  EXPECT_DOUBLE_EQ(1.0, DownloadProgressFraction(finished));
  EXPECT_TRUE(DownloadIsFinished(finished));

  // A server that under-reported its own content length must not produce a
  // fraction above one.
  DownloadRecord overshoot = RecordInState(DownloadState::kInProgress);
  overshoot.received_bytes = 8192;
  overshoot.total_bytes = 4096;
  EXPECT_DOUBLE_EQ(1.0, DownloadProgressFraction(overshoot));
}

}  // namespace
}  // namespace taffy
