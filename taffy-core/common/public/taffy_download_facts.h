// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_PUBLIC_TAFFY_DOWNLOAD_FACTS_H_
#define TAFFY_PUBLIC_TAFFY_DOWNLOAD_FACTS_H_

#include <stdint.h>

// What a download is doing, where its bytes are going and what kind of thing
// it is, as three closed enumerations that carry no page-authored text.
//
// The first two lived in //taffy/browser's download_record.h, which is where
// the only reader was: a browser-process record of one download, for the
// surfaces that show progress and offer controls. A second reader has appeared
// a layer below — the postcondition verifier's browser-flow evidence
// (//taffy/components/intelligence/content/postcondition_evidence.h) — and
// //taffy/components may not include //taffy/browser, so the choice was to
// move the definitions down or to write a second pair of enumerations saying
// the same thing in the other layer.
//
// A second pair is the defect. The two would agree on the day they were
// written and would then drift, and the drift would be invisible: each layer's
// translation from Chromium's own state would compile, pass its own tests, and
// disagree with the other about what "interrupted" means. So the definitions
// moved here, to the process-neutral boundary both layers already depend on,
// and download_record.h includes this file rather than restating it.
//
// The rule that governs what may join them: a value in this header must be a
// **class**, never an instance. "The default downloads directory" is a class;
// `/storage/emulated/0/Download/report.pdf` is an instance and belongs
// nowhere near a record that may reach a log, an audit entry or the AI data
// plane. Nothing here is a path, a file name, a URL or a header value.

namespace taffy {

// The lifecycle as the product speaks it. A mirror of
// download::DownloadItem::DownloadState plus the paused sub-state, which
// Chromium reports separately and which every user-facing surface needs.
enum class DownloadState : uint8_t {
  // Created and not yet given a target. Nothing is on disk.
  kCreated = 0,
  kInProgress = 1,
  kPaused = 2,
  // Stopped by a failure. May be resumable; ask the state machine.
  kInterrupted = 3,
  kComplete = 4,
  kCancelled = 5,
};

// Where the bytes are going. A kind rather than a path, for the reason in the
// file comment.
enum class DownloadDestinationKind : uint8_t {
  kUndecided = 0,
  kDefaultDownloadsDirectory = 1,
  // The user picked a location through the Android document picker.
  kUserChosenLocation = 2,
  kApplicationPrivateDirectory = 3,
};

// What kind of thing was transferred, to the only precision that is a class
// rather than an instance: the top-level type of a media type.
//
// A full media type is not admissible here and the reason is decision 0062
// section 3. `type/subtype` is server-supplied text and its subtype half is an
// RFC 9110 token, which is to say it is any run of letters, digits, dots and
// hyphens the server likes — `application/TaffyCanaryQ3-statement.pdf` is a
// well-formed media type. So a field holding the whole of one is a field
// holding a bounded, attacker-chosen string, and a reader treating it as
// browser-owned fact is wrong about it. The top-level half is different in
// kind: it is a closed set somebody else maintains, this header can enumerate
// it, and a value outside the set is simply not one of these.
//
// The names are exactly the IANA top-level media types registry, so the set a
// reviewer has to check this against is one document rather than a judgement
// call. `kExample` is in it because the registry is; it never appears on the
// wire.
enum class MediaTopLevelType : uint8_t {
  // No admissible answer, for any of three reasons that are the same answer to
  // the only question asked of this field: nothing was reported, what was
  // reported did not parse as `type/subtype`, or its type half is not a
  // registered name. A reader that needs to tell those apart is asking about
  // the server, and this evidence is not about the server.
  kUnknown = 0,
  kApplication = 1,
  kAudio = 2,
  kExample = 3,
  kFont = 4,
  kHaptics = 5,
  kImage = 6,
  kMessage = 7,
  kModel = 8,
  kMultipart = 9,
  kText = 10,
  kVideo = 11,
};

// True when the download system has stopped moving bytes for this item.
//
// kInterrupted counts. An interrupted transfer has stopped, and whether it can
// be picked up again is a different question with a different answer
// (`DownloadRecord::is_resumable`), asked by a different reader. Folding the
// two together here would mean a witness reporting "still going" about a
// transfer that has not moved a byte since it failed.
constexpr bool DownloadStateIsTerminal(DownloadState state) {
  switch (state) {
    case DownloadState::kCreated:
    case DownloadState::kInProgress:
    case DownloadState::kPaused:
      return false;
    case DownloadState::kInterrupted:
    case DownloadState::kComplete:
    case DownloadState::kCancelled:
      return true;
  }
}

}  // namespace taffy

#endif  // TAFFY_PUBLIC_TAFFY_DOWNLOAD_FACTS_H_
