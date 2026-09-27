// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Generated from schema/contract.json. Do not edit.
// Contract browsing 1.1.

#ifndef TAFFY_CONTRACTS_BROWSING_GENERATED_CPP_BROWSING_ENUMS_H_
#define TAFFY_CONTRACTS_BROWSING_GENERATED_CPP_BROWSING_ENUMS_H_

#include <stdint.h>

#include <optional>

#include "taffy/contracts/browsing/generated/mojom/browsing.mojom-shared.h"

// Closed-enumeration decoders for this contract. Every enumeration here is
// closed: a wire integer that names no member is not a member, and these return
// std::nullopt for it instead of forming an out-of-range enumerator, which is
// undefined behaviour and, at a trust seam, a fail-open. Any caller holding an
// integer must come through here.

namespace taffy::browsing::wire {

// Who a tab belongs to. An unattributed tab is the user's, because the rules
// that protect a user's tab are the stricter ones.
constexpr std::optional<mojom::TabOwner> TabOwnerFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::TabOwner::kUnknown;
    case 1:
      return mojom::TabOwner::kUser;
    case 2:
      return mojom::TabOwner::kAssistant;
    default:
      return std::nullopt;
  }
}

// Whether a tab forgets everything when it closes.
constexpr std::optional<mojom::TabPrivacy> TabPrivacyFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::TabPrivacy::kNormal;
    case 1:
      return mojom::TabPrivacy::kPrivate;
    default:
      return std::nullopt;
  }
}

// What a failed navigation was, in the vocabulary the parity matrix uses. A
// member this build does not name fails closed as a failure and never as a
// load.
constexpr std::optional<mojom::NavigationFailure>
NavigationFailureFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::NavigationFailure::kNone;
    case 1:
      return mojom::NavigationFailure::kDnsFailure;
    case 2:
      return mojom::NavigationFailure::kOffline;
    case 3:
      return mojom::NavigationFailure::kConnectionFailure;
    case 4:
      return mojom::NavigationFailure::kTimeout;
    case 5:
      return mojom::NavigationFailure::kTlsFailure;
    case 6:
      return mojom::NavigationFailure::kHttpErrorStatus;
    case 7:
      return mojom::NavigationFailure::kBlockedByClient;
    case 8:
      return mojom::NavigationFailure::kBlockedBySafeBrowsing;
    case 9:
      return mojom::NavigationFailure::kAborted;
    case 10:
      return mojom::NavigationFailure::kUnknownFailure;
    default:
      return std::nullopt;
  }
}

// Which trusted browser surface stood between the person and the content.
constexpr std::optional<mojom::InterstitialKind>
InterstitialKindFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::InterstitialKind::kNone;
    case 1:
      return mojom::InterstitialKind::kCertificateError;
    case 2:
      return mojom::InterstitialKind::kSafeBrowsing;
    case 3:
      return mojom::InterstitialKind::kBlockedByPolicy;
    case 4:
      return mojom::InterstitialKind::kOther;
    default:
      return std::nullopt;
  }
}

// Where a download has got to, mirroring the download system's own lifecycle
// plus the paused sub-state it reports separately.
constexpr std::optional<mojom::DownloadState>
DownloadStateFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::DownloadState::kCreated;
    case 1:
      return mojom::DownloadState::kInProgress;
    case 2:
      return mojom::DownloadState::kPaused;
    case 3:
      return mojom::DownloadState::kInterrupted;
    case 4:
      return mojom::DownloadState::kComplete;
    case 5:
      return mojom::DownloadState::kCancelled;
    default:
      return std::nullopt;
  }
}

// Why a download stopped, translated from the download system's interrupt
// reason and never invented.
constexpr std::optional<mojom::DownloadFailure>
DownloadFailureFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::DownloadFailure::kNone;
    case 1:
      return mojom::DownloadFailure::kNetwork;
    case 2:
      return mojom::DownloadFailure::kServer;
    case 3:
      return mojom::DownloadFailure::kFileSystem;
    case 4:
      return mojom::DownloadFailure::kInsufficientSpace;
    case 5:
      return mojom::DownloadFailure::kPermissionDenied;
    case 6:
      return mojom::DownloadFailure::kBlockedBySecurityCheck;
    case 7:
      return mojom::DownloadFailure::kCancelledByUser;
    case 8:
      return mojom::DownloadFailure::kBrowserShutdown;
    case 9:
      return mojom::DownloadFailure::kUnknown;
    default:
      return std::nullopt;
  }
}

// Where the bytes are going, as a kind rather than a path: a record that may
// reach a log carries no profile directory layout.
constexpr std::optional<mojom::DownloadDestination>
DownloadDestinationFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::DownloadDestination::kUndecided;
    case 1:
      return mojom::DownloadDestination::kDefaultDirectory;
    case 2:
      return mojom::DownloadDestination::kUserChosenLocation;
    case 3:
      return mojom::DownloadDestination::kApplicationPrivateDirectory;
    default:
      return std::nullopt;
  }
}

// What a person may ask of a download. Which of these is legal in which state
// is the browser's decision, not the surface's.
constexpr std::optional<mojom::DownloadCommand>
DownloadCommandFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::DownloadCommand::kPause;
    case 1:
      return mojom::DownloadCommand::kResume;
    case 2:
      return mojom::DownloadCommand::kCancel;
    case 3:
      return mojom::DownloadCommand::kRetry;
    case 4:
      return mojom::DownloadCommand::kOpenWhenComplete;
    case 5:
      return mojom::DownloadCommand::kOpenNow;
    default:
      return std::nullopt;
  }
}

// One terminal verdict for one browsing intent. It says whether the browser
// took the request, never whether the page loaded.
constexpr std::optional<mojom::BrowsingStatus>
BrowsingStatusFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::BrowsingStatus::kAccepted;
    case 1:
      return mojom::BrowsingStatus::kInvalidRequest;
    case 2:
      return mojom::BrowsingStatus::kUnknownTab;
    case 3:
      return mojom::BrowsingStatus::kUnknownDownload;
    case 4:
      return mojom::BrowsingStatus::kIllegalInState;
    case 5:
      return mojom::BrowsingStatus::kRefusedByPolicy;
    case 6:
      return mojom::BrowsingStatus::kNoHistory;
    case 7:
      return mojom::BrowsingStatus::kUnavailable;
    default:
      return std::nullopt;
  }
}

}  // namespace taffy::browsing::wire

#endif  // TAFFY_CONTRACTS_BROWSING_GENERATED_CPP_BROWSING_ENUMS_H_
