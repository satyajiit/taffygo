// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_DOWNLOAD_RECORD_H_
#define TAFFY_BROWSER_DOWNLOAD_RECORD_H_

#include <stdint.h>

#include <string>

#include "taffy/common/public/bip_identity.h"
#include "taffy/common/public/taffy_download_facts.h"
#include "taffy/common/public/taffy_download_intent.h"

// What TaffyGo knows about one download (PAR-FILE-001 and CAP-DL-001:
// destination, progress, cancel, retry, open, duplicate filename and failure
// behavior).
//
// Chromium's download system owns the download. It picks the target path,
// uniquifies a duplicate name, runs the danger checks, writes the bytes,
// resumes an interrupted transfer and opens the finished file. This record is
// the browser-process projection of that work, and its whole purpose is that
// TaffyGo can show progress and offer controls **without maintaining a second
// download implementation** — the failure mode where the browser's idea of a
// download and the download system's idea of it drift, and the user is shown
// the wrong one.
//
// So every field below is a fact copied from a download::DownloadItem, and
// nothing here is computed. The two places that could be tempting and are
// deliberately refused:
//
//   * the duplicate filename resolution is *recorded*, never performed.
//     Chromium already has the uniquifier and the reservation tracker, and a
//     second one would eventually disagree about which file exists.
//   * the failure class is a translation of Chromium's interrupt reason into
//     the vocabulary the product speaks, not a judgement about it.
//
// The full target path is absent on purpose. The file name is what a person
// sees and what a parity test asserts on; the directory layout of a profile is
// not something a record that may reach a log should carry.
//
// `DownloadState` and `DownloadDestinationKind` are not declared here. They
// are in //taffy/common/public/taffy_download_facts.h, because the
// postcondition verifier a layer below needs the same two vocabularies and
// //taffy/components may not include //taffy/browser; that header states why
// a second copy would have been the defect. Every name they used to declare is
// still `taffy::` and still reached through this header.

namespace taffy {

// Why a download stopped, in the vocabulary the product explains failures in.
// Translated from download::DownloadInterruptReason, never invented.
enum class DownloadFailureClass : uint8_t {
  kNone = 0,
  kNetwork = 1,
  kServer = 2,
  kFileSystem = 3,
  kInsufficientSpace = 4,
  kPermissionDenied = 5,
  // Blocked by a security check — Safe Browsing, a policy, or a virus scanner.
  // PAR-FILE-008 preserves the upstream baseline from M1, so this class exists
  // to be reported honestly and never to be worked around.
  kBlockedBySecurityCheck = 6,
  kCancelledByUser = 7,
  kBrowserShutdown = 8,
  // A reason this translation does not name. Fails closed: treated as a
  // failure, never as a success.
  kUnknown = 9,
};

// How a name collision was resolved — by Chromium, and recorded here so a
// parity test can assert which of the three happened.
enum class DuplicateResolution : uint8_t {
  kNotApplicable = 0,
  // Chromium appended a counter: "report (1).pdf".
  kUniquifiedByBrowser = 1,
  // The user was asked and chose.
  kPromptedUser = 2,
  // The user chose to replace the existing file.
  kOverwrittenByUserChoice = 3,
};

struct DownloadRecord {
  // Chromium's own identifier for the item. Identity belongs to the download
  // system; a second allocator here would eventually name a different file.
  uint32_t download_id = 0;

  TabId tab_id;
  NavigationInitiator initiator = NavigationInitiator::kUnknown;

  DownloadState state = DownloadState::kCreated;
  DownloadFailureClass failure = DownloadFailureClass::kNone;
  DuplicateResolution duplicate_resolution = DuplicateResolution::kNotApplicable;
  DownloadDestinationKind destination = DownloadDestinationKind::kUndecided;

  // Base name only, as Chromium settled it after any uniquifying.
  std::string target_file_name;

  int64_t received_bytes = 0;
  // -1 when the server did not say. Progress surfaces must handle that rather
  // than pretending the size is zero.
  int64_t total_bytes = -1;

  // True when Chromium reports the item can be resumed from where it stopped.
  bool is_resumable = false;

  // True when Chromium's danger checks want an explicit user confirmation
  // before the file is opened or kept. The router refuses to open such a file
  // without one; it never clears the flag itself.
  bool requires_danger_confirmation = false;

  friend bool operator==(const DownloadRecord&, const DownloadRecord&) = default;
};

// True when the record describes a download that finished writing its bytes.
// Written as a function so there is one definition of "done" rather than an
// equality test repeated at every surface.
constexpr bool DownloadIsFinished(const DownloadRecord& record) {
  return record.state == DownloadState::kComplete;
}

// Fraction complete in the range [0, 1], or -1 when the total size is unknown.
// A surface that shows a bar rather than a spinner has to know which it has.
double DownloadProgressFraction(const DownloadRecord& record);

}  // namespace taffy

#endif  // TAFFY_BROWSER_DOWNLOAD_RECORD_H_
