// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/download_state_machine.h"

namespace taffy {

namespace {

// The matrix, written out rather than reasoned about at each call site. Every
// cell is a decision somebody made once.
bool CommandIsLegalInState(DownloadState state, DownloadCommand command) {
  switch (command) {
    case DownloadCommand::kPause:
      return state == DownloadState::kInProgress;

    case DownloadCommand::kResume:
      // Paused is the obvious one. Interrupted is the one that matters: a
      // transfer that dropped is what a person most wants to continue, and
      // whether it can be is Chromium's answer, checked separately.
      return state == DownloadState::kPaused ||
             state == DownloadState::kInterrupted;

    case DownloadCommand::kCancel:
      return state == DownloadState::kCreated ||
             state == DownloadState::kInProgress ||
             state == DownloadState::kPaused ||
             state == DownloadState::kInterrupted;

    case DownloadCommand::kRetry:
      // Only from a terminal failure. Retrying something still in flight would
      // produce two downloads of the same file, which is the duplicate the
      // user was trying to avoid.
      return state == DownloadState::kInterrupted ||
             state == DownloadState::kCancelled;

    case DownloadCommand::kOpenWhenComplete:
      return state == DownloadState::kCreated ||
             state == DownloadState::kInProgress ||
             state == DownloadState::kPaused;

    case DownloadCommand::kOpenNow:
      return state == DownloadState::kComplete;
  }
}

}  // namespace

DownloadCommandLegality EvaluateDownloadCommand(const DownloadRecord& record,
                                                DownloadCommand command) {
  if (!CommandIsLegalInState(record.state, command)) {
    return DownloadCommandLegality::kIllegalInState;
  }

  if (command == DownloadCommand::kResume &&
      record.state == DownloadState::kInterrupted && !record.is_resumable) {
    // Offering resume for something that cannot resume is worse than not
    // offering it: the button appears to work and nothing happens. Retry is
    // the honest offer here.
    return DownloadCommandLegality::kNotResumable;
  }

  const bool opening = command == DownloadCommand::kOpenNow ||
                       command == DownloadCommand::kOpenWhenComplete;
  if (opening && record.requires_danger_confirmation) {
    // Chromium's danger checks want an explicit confirmation. This seam never
    // clears the flag and never opens past it; it reports that the
    // confirmation is missing and the browser's own surface asks for it.
    return DownloadCommandLegality::kNeedsDangerConfirmation;
  }

  return DownloadCommandLegality::kLegal;
}

}  // namespace taffy
