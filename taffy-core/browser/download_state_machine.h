// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_DOWNLOAD_STATE_MACHINE_H_
#define TAFFY_BROWSER_DOWNLOAD_STATE_MACHINE_H_

#include <stdint.h>

#include "taffy/browser/download_record.h"

// Which download command is legal in which state (PAR-FILE-001).
//
// This is not a reimplementation of Chromium's download state machine.
// Chromium decides what a download *does*; this table decides what a person is
// allowed to *ask for*, which is a smaller and different question. The two
// meet at exactly one place — DownloadCommandRouter — and the split is what
// keeps the product's control surface from drifting away from the download
// system's behavior.
//
// It is pure and table-driven, so the whole matrix is unit-testable without a
// network, a file system or a device.

namespace taffy {

// The controls PAR-FILE-001 requires, and nothing beyond them. Removing an
// entry from the download list and deleting the user's file are PAR-FILE-003
// at M4 and are deliberately absent: the distinction between the two is the
// whole substance of that row, and a command added here early would be a
// distinction nobody had drawn yet.
enum class DownloadCommand : uint8_t {
  kPause = 0,
  kResume = 1,
  kCancel = 2,
  // A retry starts a *new* download. It is a separate command from resume
  // because the two have different safety properties: resuming continues an
  // authorized transfer, retrying makes a fresh request.
  kRetry = 3,
  // Open once the bytes have finished arriving.
  kOpenWhenComplete = 4,
  // Open now. Legal only for a finished download.
  kOpenNow = 5,
};

// Why a command was refused. A single reason per refusal, so the surface that
// explains it uses a trusted local template keyed by this value.
enum class DownloadCommandLegality : uint8_t {
  kLegal = 0,
  // The state does not admit this command — cancelling something already
  // cancelled, resuming something that finished.
  kIllegalInState = 1,
  // Chromium reports the item cannot be resumed from where it stopped, so
  // resume would silently do nothing.
  kNotResumable = 2,
  // The file is finished but Chromium's danger checks want an explicit
  // confirmation before it is opened.
  kNeedsDangerConfirmation = 3,
};

// Pure. The whole matrix in one function.
DownloadCommandLegality EvaluateDownloadCommand(const DownloadRecord& record,
                                                DownloadCommand command);

// True when the command, if it succeeds, starts a fresh network request rather
// than continuing an authorized one. The router routes exactly these back
// through the download and external-intent seam, because a fresh request is a
// fresh decision.
constexpr bool DownloadCommandStartsANewRequest(DownloadCommand command) {
  return command == DownloadCommand::kRetry;
}

}  // namespace taffy

#endif  // TAFFY_BROWSER_DOWNLOAD_STATE_MACHINE_H_
