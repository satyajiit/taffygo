// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_PUBLIC_TAFFY_DOWNLOAD_INTENT_H_
#define TAFFY_PUBLIC_TAFFY_DOWNLOAD_INTENT_H_

#include <stdint.h>

#include <string>

#include "taffy/common/public/bip_identity.h"

// The download and external-intent seam for the M1 browser core.
//
// M1 requires ordinary external-link intents and user-initiated downloads.
// Both are places where a page, or a task acting on a page, can reach outside
// the browser, so both get one entry point with one rule written down where a
// reviewer will find it.
//
// The rule: initiator is not a hint, it is the decision.
//
//   * kUser is the ordinary path. It goes to Chromium's normal download and
//     intent handling, with the platform's own confirmation surfaces intact.
//   * kAssistant is admitted only with an exact task, actor lease, capability
//     and journalled dispatch identity. A bare assistant attribution remains a
//     refusal even if a future call site forgets one of those gates.
//
// This seam never bypasses a file chooser, a permission prompt, an
// external-intent confirmation, a download policy check or a TLS interstitial
// (spec section 11.5). It runs before them and can only say no.

namespace taffy {

enum class NavigationInitiator : uint8_t {
  // Fail closed: an initiator that could not be attributed is refused for
  // anything consequential.
  kUnknown = 0,
  kUser = 1,
  kAssistant = 2,
  // The page started it without a user gesture. Chromium's own policies still
  // apply on top of this seam's answer.
  kPage = 3,
};

enum class DownloadDecision : uint8_t {
  // Continue into Chromium's ordinary download path, prompts and all.
  kAllowOrdinaryPath = 0,
  kRefusedUnattributedInitiator = 1,
  // A bare assistant request carried no exact task authority.
  kRefusedAssistantInitiated = 2,
  kRefusedNoActorLease = 3,
  kRefusedNoCapability = 4,
};

enum class ExternalIntentDecision : uint8_t {
  // Continue into Chromium's ordinary external-intent path, including its
  // confirmation surface.
  kAllowOrdinaryPath = 0,
  kKeepInBrowser = 1,
  kRefusedUnattributedInitiator = 2,
  kRefusedAssistantInitiated = 3,
};

struct DownloadRequestFacts {
  TabId tab_id;
  NavigationInitiator initiator = NavigationInitiator::kUnknown;
  // True when Chromium attributes a user activation to this request.
  bool has_user_activation = false;
  // Set when an assistant task is active in the tab, so a refusal can be
  // attributed in the journal.
  TaskId task_id;
  ActorLeaseId actor_lease_id;
  CapabilityReference capability_reference;
  DispatchId dispatch_id;
  // True only after the browser capability ledger revalidated the exact
  // task/document/destination tuple. The router still requires every opaque
  // identity above; this bit alone grants nothing.
  bool capability_admitted = false;
};

struct ExternalIntentFacts {
  TabId tab_id;
  NavigationInitiator initiator = NavigationInitiator::kUnknown;
  bool has_user_activation = false;
  // Scheme only. The full target never enters this struct: the decision does
  // not depend on it, and a content-free record is worth more than a
  // marginally better log line (spec section 16).
  std::string target_scheme;
  TaskId task_id;
};

// Implemented by //taffy/app/android where the platform half lives.
// The manager consults it only after its own refusals have passed.
class DownloadIntentDelegate {
 public:
  virtual ~DownloadIntentDelegate() = default;

  // True when the platform can hand this scheme to another application at
  // all. False means "keep it in the browser", not "block it".
  virtual bool CanHandleExternally(const std::string& target_scheme) = 0;
};

// The process-wide instance lives in //taffy/browser; see
// taffy_download_intent_router.h for the accessor.
class DownloadIntentRouter {
 public:
  virtual ~DownloadIntentRouter() = default;

  virtual void SetDelegate(DownloadIntentDelegate* delegate) = 0;

  virtual DownloadDecision EvaluateDownload(
      const DownloadRequestFacts& facts) = 0;

  virtual ExternalIntentDecision EvaluateExternalNavigation(
      const ExternalIntentFacts& facts) = 0;
};

}  // namespace taffy

#endif  // TAFFY_PUBLIC_TAFFY_DOWNLOAD_INTENT_H_
