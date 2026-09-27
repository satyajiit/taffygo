// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/browsing_projection.h"

#include <optional>
#include <utility>

#include "base/strings/string_number_conversions.h"

namespace taffy {
namespace browsing_projection {

namespace wire = browsing::mojom;

wire::NavigationFailure Project(NavigationErrorClass value) {
  switch (value) {
    case NavigationErrorClass::kNone:
      return wire::NavigationFailure::kNone;
    case NavigationErrorClass::kDnsFailure:
      return wire::NavigationFailure::kDnsFailure;
    case NavigationErrorClass::kOffline:
      return wire::NavigationFailure::kOffline;
    case NavigationErrorClass::kConnectionFailure:
      return wire::NavigationFailure::kConnectionFailure;
    case NavigationErrorClass::kTimeout:
      return wire::NavigationFailure::kTimeout;
    case NavigationErrorClass::kTlsFailure:
      return wire::NavigationFailure::kTlsFailure;
    case NavigationErrorClass::kHttpErrorStatus:
      return wire::NavigationFailure::kHttpErrorStatus;
    case NavigationErrorClass::kBlockedByClient:
      return wire::NavigationFailure::kBlockedByClient;
    case NavigationErrorClass::kBlockedBySafeBrowsing:
      return wire::NavigationFailure::kBlockedBySafeBrowsing;
    case NavigationErrorClass::kAborted:
      return wire::NavigationFailure::kAborted;
    case NavigationErrorClass::kUnknownFailure:
      return wire::NavigationFailure::kUnknownFailure;
  }
}

wire::InterstitialKind Project(InterstitialKind value) {
  switch (value) {
    case InterstitialKind::kNone:
      return wire::InterstitialKind::kNone;
    case InterstitialKind::kCertificateError:
      return wire::InterstitialKind::kCertificateError;
    case InterstitialKind::kSafeBrowsing:
      return wire::InterstitialKind::kSafeBrowsing;
    case InterstitialKind::kBlockedByPolicy:
      return wire::InterstitialKind::kBlockedByPolicy;
    case InterstitialKind::kOther:
      return wire::InterstitialKind::kOther;
  }
}

wire::DownloadState Project(DownloadState value) {
  switch (value) {
    case DownloadState::kCreated:
      return wire::DownloadState::kCreated;
    case DownloadState::kInProgress:
      return wire::DownloadState::kInProgress;
    case DownloadState::kPaused:
      return wire::DownloadState::kPaused;
    case DownloadState::kInterrupted:
      return wire::DownloadState::kInterrupted;
    case DownloadState::kComplete:
      return wire::DownloadState::kComplete;
    case DownloadState::kCancelled:
      return wire::DownloadState::kCancelled;
  }
}

wire::DownloadFailure Project(DownloadFailureClass value) {
  switch (value) {
    case DownloadFailureClass::kNone:
      return wire::DownloadFailure::kNone;
    case DownloadFailureClass::kNetwork:
      return wire::DownloadFailure::kNetwork;
    case DownloadFailureClass::kServer:
      return wire::DownloadFailure::kServer;
    case DownloadFailureClass::kFileSystem:
      return wire::DownloadFailure::kFileSystem;
    case DownloadFailureClass::kInsufficientSpace:
      return wire::DownloadFailure::kInsufficientSpace;
    case DownloadFailureClass::kPermissionDenied:
      return wire::DownloadFailure::kPermissionDenied;
    case DownloadFailureClass::kBlockedBySecurityCheck:
      return wire::DownloadFailure::kBlockedBySecurityCheck;
    case DownloadFailureClass::kCancelledByUser:
      return wire::DownloadFailure::kCancelledByUser;
    case DownloadFailureClass::kBrowserShutdown:
      return wire::DownloadFailure::kBrowserShutdown;
    case DownloadFailureClass::kUnknown:
      return wire::DownloadFailure::kUnknown;
  }
}

wire::DownloadDestination Project(DownloadDestinationKind value) {
  switch (value) {
    case DownloadDestinationKind::kUndecided:
      return wire::DownloadDestination::kUndecided;
    case DownloadDestinationKind::kDefaultDownloadsDirectory:
      return wire::DownloadDestination::kDefaultDirectory;
    case DownloadDestinationKind::kUserChosenLocation:
      return wire::DownloadDestination::kUserChosenLocation;
    case DownloadDestinationKind::kApplicationPrivateDirectory:
      return wire::DownloadDestination::kApplicationPrivateDirectory;
  }
}

wire::TabOwner Project(TabOwnership value) {
  switch (value) {
    case TabOwnership::kUnknown:
      return wire::TabOwner::kUnknown;
    case TabOwnership::kUser:
      return wire::TabOwner::kUser;
    case TabOwnership::kAssistant:
      return wire::TabOwner::kAssistant;
  }
}

wire::BrowsingStatus Project(TabOpenResult value) {
  switch (value) {
    case TabOpenResult::kOpened:
      return wire::BrowsingStatus::kAccepted;
    case TabOpenResult::kRefusedNoDelegate:
      return wire::BrowsingStatus::kUnavailable;
    // A request the registry could not attribute, and one whose disposition
    // this build does not implement, are both malformed rather than refused:
    // the caller sent something this seam does not describe.
    case TabOpenResult::kRefusedUnattributedOrigin:
    case TabOpenResult::kRefusedUnsupportedDisposition:
      return wire::BrowsingStatus::kInvalidRequest;
    case TabOpenResult::kRefusedAssistantOutsideTaskScope:
      return wire::BrowsingStatus::kRefusedByPolicy;
    case TabOpenResult::kPlatformRefused:
      return wire::BrowsingStatus::kUnavailable;
  }
}

wire::BrowsingStatus Project(TabActivateResult value) {
  switch (value) {
    // Already active is an accepted request: the caller asked for a state and
    // that state holds. Reporting it as a refusal would make an idempotent
    // command look like a failure the surface has to explain.
    case TabActivateResult::kActivated:
    case TabActivateResult::kAlreadyActive:
      return wire::BrowsingStatus::kAccepted;
    case TabActivateResult::kRefusedNoDelegate:
    case TabActivateResult::kPlatformRefused:
      return wire::BrowsingStatus::kUnavailable;
    case TabActivateResult::kRefusedUnknownTab:
      return wire::BrowsingStatus::kUnknownTab;
    case TabActivateResult::kRefusedUnattributedOrigin:
      return wire::BrowsingStatus::kInvalidRequest;
    case TabActivateResult::kRefusedAssistantMayNotStealFocus:
      return wire::BrowsingStatus::kRefusedByPolicy;
  }
}

wire::BrowsingStatus Project(TabCloseResult value) {
  switch (value) {
    case TabCloseResult::kClosed:
      return wire::BrowsingStatus::kAccepted;
    case TabCloseResult::kRefusedNoDelegate:
    case TabCloseResult::kPlatformRefused:
      return wire::BrowsingStatus::kUnavailable;
    case TabCloseResult::kRefusedUnknownTab:
      return wire::BrowsingStatus::kUnknownTab;
    case TabCloseResult::kRefusedUnattributedOrigin:
      return wire::BrowsingStatus::kInvalidRequest;
    case TabCloseResult::kRefusedAssistantMayNotCloseUserTab:
      return wire::BrowsingStatus::kRefusedByPolicy;
  }
}

wire::BrowsingStatus Project(DownloadCommandLegality value) {
  switch (value) {
    case DownloadCommandLegality::kLegal:
      return wire::BrowsingStatus::kAccepted;
    // All three refusals are about the download's own state, and the surface
    // is told that and nothing finer. Which predicate fired — the state table,
    // the download system's resumability, or a pending danger confirmation —
    // is a browser fact, and a surface that branched on it would be carrying a
    // copy of the table this seam exists to keep in one place.
    case DownloadCommandLegality::kIllegalInState:
    case DownloadCommandLegality::kNotResumable:
    case DownloadCommandLegality::kNeedsDangerConfirmation:
      return wire::BrowsingStatus::kIllegalInState;
  }
}

wire::DownloadViewPtr ProjectDownload(const DownloadRecord& record,
                                      std::string_view host) {
  auto view = wire::DownloadView::New();
  view->download_id = base::NumberToString(record.download_id);
  view->file_name = record.target_file_name;
  view->host = std::string(host);
  view->received_bytes =
      record.received_bytes < 0 ? 0u
                                : static_cast<uint64_t>(record.received_bytes);
  // A negative total is the download system saying the server did not tell it.
  // The contract carries that as a separate flag rather than as a sentinel,
  // because a sentinel in a size field is a size to anything that does not
  // know it is one.
  view->total_known = record.total_bytes >= 0;
  view->total_bytes =
      view->total_known ? static_cast<uint64_t>(record.total_bytes) : 0u;
  view->state = Project(record.state);
  view->failure = Project(record.failure);
  view->destination = Project(record.destination);
  view->resumable = record.is_resumable;
  view->requires_danger_confirmation = record.requires_danger_confirmation;
  return view;
}

std::optional<DownloadCommand> AcceptCommand(wire::DownloadCommand value) {
  switch (value) {
    case wire::DownloadCommand::kPause:
      return DownloadCommand::kPause;
    case wire::DownloadCommand::kResume:
      return DownloadCommand::kResume;
    case wire::DownloadCommand::kCancel:
      return DownloadCommand::kCancel;
    case wire::DownloadCommand::kRetry:
      return DownloadCommand::kRetry;
    case wire::DownloadCommand::kOpenWhenComplete:
      return DownloadCommand::kOpenWhenComplete;
    case wire::DownloadCommand::kOpenNow:
      return DownloadCommand::kOpenNow;
  }
  return std::nullopt;
}

bool StateIsCoherent(const wire::BrowsingStateView& state) {
  const wire::TabView* selected = nullptr;
  for (const wire::TabViewPtr& tab : state.tabs) {
    if (!tab || !tab->selected) {
      continue;
    }
    if (selected) {
      return false;
    }
    selected = tab.get();
  }
  if (!selected || !state.navigation) {
    return false;
  }
  // A tab that has been nowhere has no host, and the navigation beside it has
  // none either. Comparing them is still the right check: it is the pair that
  // has to agree, not the pair that has to be non-empty.
  return selected->host == state.navigation->host;
}

}  // namespace browsing_projection
}  // namespace taffy
