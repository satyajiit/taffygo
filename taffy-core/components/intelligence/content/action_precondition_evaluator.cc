// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/intelligence/content/action_precondition_evaluator.h"

#include "taffy/components/security/browser/restricted_destination_classifier.h"
#include "taffy/components/security/browser/value_reference_vault.h"

namespace taffy {
namespace {

ActionPreconditionFailure Refuse(PreconditionKind kind,
                                 ActionResultCode code) {
  return std::make_pair(kind, code);
}

bool IsFormAction(ActionType action) {
  switch (action) {
    case ActionType::kSetText:
    case ActionType::kSelectOption:
    case ActionType::kToggle:
    case ActionType::kSubmitForm:
      return true;
    case ActionType::kActivate:
    case ActionType::kFocus:
    case ActionType::kScrollIntoView:
      return false;
  }
  return false;
}

const Destination* UniqueExpectedDestination(
    const AuthorizedActionEnvelope& envelope) {
  const Destination* found = nullptr;
  for (const Precondition& precondition : envelope.preconditions) {
    if (precondition.kind != PreconditionKind::kExpectedDestination ||
        !precondition.expected_destination) {
      continue;
    }
    if (found) {
      return nullptr;
    }
    found = &*precondition.expected_destination;
  }
  return found;
}

}  // namespace

bool NonFormActionMayReach(ActionType action, Sensitivity sensitivity) {
  return action == ActionType::kScrollIntoView &&
         sensitivity == Sensitivity::kChallengeResponse;
}

std::optional<ActionPreconditionFailure> EvaluateBrowserActionPreconditions(
    const AuthorizedActionEnvelope& envelope,
    const ResolvedNodeFacts& facts,
    bool actor_lease_still_valid) {
  if (facts.observed_at_revision < envelope.required_graph_revision) {
    return Refuse(PreconditionKind::kAcceptableGraphRevision,
                  ActionResultCode::kStaleGraph);
  }
  if (facts.node_id != envelope.target_handle.node_id) {
    return Refuse(PreconditionKind::kNodeExists, ActionResultCode::kNodeGone);
  }
  if (!facts.SupportsAction(envelope.action_type)) {
    return Refuse(PreconditionKind::kNodeActionAvailable,
                  ActionResultCode::kRoleOrActionChanged);
  }
  const bool sensitivity_refused =
      IsFormAction(envelope.action_type)
          ? !FillClearance::For(facts.sensitivity).has_value()
          : !NonFormActionMayReach(envelope.action_type, facts.sensitivity) &&
                SensitivityStrictness(facts.sensitivity) >
                    SensitivityStrictness(Sensitivity::kNotSensitive);
  if (sensitivity_refused) {
    return Refuse(PreconditionKind::kNotSensitiveField,
                  ActionResultCode::kSensitiveField);
  }

  const Destination* class_destination = UniqueExpectedDestination(envelope);
  for (const Precondition& precondition : envelope.preconditions) {
    switch (precondition.kind) {
      case PreconditionKind::kNodeRoleUnchanged:
        if (!precondition.expected_role ||
            facts.role != *precondition.expected_role) {
          return Refuse(precondition.kind,
                        ActionResultCode::kRoleOrActionChanged);
        }
        break;
      case PreconditionKind::kNodeActionAvailable:
        if (!precondition.expected_action_type ||
            !facts.SupportsAction(*precondition.expected_action_type)) {
          return Refuse(precondition.kind,
                        ActionResultCode::kRoleOrActionChanged);
        }
        break;
      case PreconditionKind::kNodeStateAsserted:
        if (!precondition.node_state) {
          return Refuse(precondition.kind, ActionResultCode::kUnsupported);
        }
        if (!facts.HasState(*precondition.node_state)) {
          switch (*precondition.node_state) {
            case NodeState::kVisible:
              return Refuse(precondition.kind, ActionResultCode::kNotVisible);
            case NodeState::kEnabled:
              return Refuse(precondition.kind, ActionResultCode::kNotEnabled);
            case NodeState::kEditable:
              return Refuse(precondition.kind, ActionResultCode::kNotEditable);
            default:
              return Refuse(precondition.kind,
                            ActionResultCode::kPostconditionFailed);
          }
        }
        break;
      case PreconditionKind::kNodeStateAbsent:
        if (!precondition.node_state) {
          return Refuse(precondition.kind, ActionResultCode::kUnsupported);
        }
        if (facts.HasState(*precondition.node_state)) {
          return Refuse(precondition.kind,
                        *precondition.node_state == NodeState::kObscured
                            ? ActionResultCode::kOccluded
                            : ActionResultCode::kPostconditionFailed);
        }
        break;
      case PreconditionKind::kExpectedDestination:
        if (!precondition.expected_destination || !facts.destination ||
            *facts.destination != *precondition.expected_destination) {
          return Refuse(precondition.kind,
                        ActionResultCode::kDestinationChanged);
        }
        break;
      case PreconditionKind::kExpectedValueDigest:
        if (!precondition.expected_value_digest || !facts.value_digest ||
            *facts.value_digest != *precondition.expected_value_digest) {
          return Refuse(precondition.kind,
                        ActionResultCode::kPostconditionFailed);
        }
        break;
      case PreconditionKind::kNotSensitiveField:
        if (!precondition.max_sensitivity) {
          return Refuse(precondition.kind, ActionResultCode::kUnsupported);
        }
        if (SensitivityStrictness(facts.sensitivity) >
            SensitivityStrictness(*precondition.max_sensitivity)) {
          return Refuse(precondition.kind, ActionResultCode::kSensitiveField);
        }
        break;
      case PreconditionKind::kAcceptableGraphRevision:
        if (!precondition.min_graph_revision) {
          return Refuse(precondition.kind, ActionResultCode::kUnsupported);
        }
        if (facts.observed_at_revision < *precondition.min_graph_revision) {
          return Refuse(precondition.kind, ActionResultCode::kStaleGraph);
        }
        break;
      case PreconditionKind::kExactPageEpoch:
        if (!precondition.page_epoch) {
          return Refuse(precondition.kind, ActionResultCode::kUnsupported);
        }
        if (*precondition.page_epoch != envelope.target_handle.page_epoch) {
          return Refuse(precondition.kind, ActionResultCode::kStalePageEpoch);
        }
        break;
      case PreconditionKind::kExactOrigin:
        if (!precondition.origin) {
          return Refuse(precondition.kind, ActionResultCode::kUnsupported);
        }
        if (*precondition.origin != envelope.target_handle.expected_origin) {
          return Refuse(precondition.kind, ActionResultCode::kOriginChanged);
        }
        break;
      case PreconditionKind::kDocumentActive:
      case PreconditionKind::kNodeExists:
        // The broker established these against browser-owned lifecycle and
        // frame state immediately before this function.
        break;
      case PreconditionKind::kAllowedRedirectSet:
        // A landing-origin verifier cannot prevent an undeclared request that
        // redirects back. Until a dispatch-bound network authority consumes
        // this set, the condition cannot hold.
        return Refuse(precondition.kind,
                      ActionResultCode::kEgressNotAuthorized);
      case PreconditionKind::kNoUserInteractionSinceLease:
        if (!actor_lease_still_valid) {
          return Refuse(precondition.kind,
                        ActionResultCode::kCancelledByUser);
        }
        break;
      case PreconditionKind::kBudgetRemaining:
        // No browser budget reader exists. Unknown is treated as exhausted,
        // never as evidence that spending may continue.
        return Refuse(precondition.kind, ActionResultCode::kBudgetExceeded);
      case PreconditionKind::kDestinationClassAllowed: {
        if (!class_destination || !facts.destination ||
            *facts.destination != *class_destination) {
          return Refuse(precondition.kind, ActionResultCode::kUnsupported);
        }
        switch (ClassifyRestrictedDestination(
            class_destination->url_metadata.origin)) {
          case RestrictedDestinationStatus::kNotListed:
            break;
          case RestrictedDestinationStatus::kRestricted:
            return Refuse(precondition.kind,
                          ActionResultCode::kDestinationClassRestricted);
          case RestrictedDestinationStatus::kInvalidOrigin:
            return Refuse(precondition.kind, ActionResultCode::kUnsupported);
        }
        break;
      }
      case PreconditionKind::kContentTrustAtLeast:
        if (!precondition.min_content_trust) {
          return Refuse(precondition.kind, ActionResultCode::kUnsupported);
        }
        if (facts.content_trust == ContentTrust::kUnknownUntrusted ||
            facts.content_trust == *precondition.min_content_trust) {
          return Refuse(precondition.kind,
                        ActionResultCode::kUntrustedContentOrigin);
        }
        break;
      case PreconditionKind::kPreparedEffectUnchanged:
        // The browser has no trusted prepare ledger or standing in the grant.
        // Reporting changed or unchanged would invent a comparison.
        return Refuse(precondition.kind, ActionResultCode::kUnsupported);
      case PreconditionKind::kNoUndeclaredEgress:
        // Only a dispatch-bound browser network witness can satisfy this.
        return Refuse(precondition.kind,
                      ActionResultCode::kEgressNotAuthorized);
    }
  }
  return std::nullopt;
}

}  // namespace taffy
