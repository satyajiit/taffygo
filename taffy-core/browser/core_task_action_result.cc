// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_task_action.h"

namespace taffy {

namespace mojom = core_service::mojom;

mojom::TaskEffectCompletionStatus CompletionStatusForActionResult(
    ActionResultCode code) {
  switch (code) {
    case ActionResultCode::kVerified:
      return mojom::TaskEffectCompletionStatus::kSucceeded;
    case ActionResultCode::kCancelledByUser:
    case ActionResultCode::kCancelledByNavigation:
      return mojom::TaskEffectCompletionStatus::kCancelled;
    case ActionResultCode::kOutcomeUnknown:
    case ActionResultCode::kPostconditionTimeout:
    case ActionResultCode::kNavigationStarted:
    case ActionResultCode::kRendererCrashed:
    case ActionResultCode::kDispatchFailed:
    case ActionResultCode::kInternalError:
      return mojom::TaskEffectCompletionStatus::kOutcomeUnknown;
    case ActionResultCode::kUnsupported:
    case ActionResultCode::kTabGone:
    case ActionResultCode::kFrameGone:
    case ActionResultCode::kDocumentInactive:
    case ActionResultCode::kActorLeaseMissing:
    case ActionResultCode::kCapabilityExpired:
    case ActionResultCode::kBudgetExceeded:
      return mojom::TaskEffectCompletionStatus::kUnavailable;
    case ActionResultCode::kDeniedByPolicy:
    case ActionResultCode::kApprovalRequired:
    case ActionResultCode::kApprovalDenied:
    case ActionResultCode::kStalePageEpoch:
    case ActionResultCode::kStaleGraph:
    case ActionResultCode::kNodeGone:
    case ActionResultCode::kOriginChanged:
    case ActionResultCode::kRoleOrActionChanged:
    case ActionResultCode::kNotVisible:
    case ActionResultCode::kOccluded:
    case ActionResultCode::kNotEnabled:
    case ActionResultCode::kNotEditable:
    case ActionResultCode::kSensitiveField:
    case ActionResultCode::kDestinationChanged:
    case ActionResultCode::kPostconditionFailed:
    case ActionResultCode::kEgressNotAuthorized:
    case ActionResultCode::kDestinationClassRestricted:
    case ActionResultCode::kUntrustedContentOrigin:
    case ActionResultCode::kPreparedEffectChanged:
    case ActionResultCode::kCommitWithoutPrepare:
    case ActionResultCode::kGraphMovedDuringPreflight:
      return mojom::TaskEffectCompletionStatus::kRefused;
    case ActionResultCode::kValueReferenceUnknown:
      return mojom::TaskEffectCompletionStatus::kValueReferenceUnknown;
  }
  return mojom::TaskEffectCompletionStatus::kRefused;
}

}  // namespace taffy
