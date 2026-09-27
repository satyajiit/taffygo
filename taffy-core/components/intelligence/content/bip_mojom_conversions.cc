// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The action and node vocabulary: identity, digests, action types, node
// states, preconditions, and the result codes a renderer reply carries.
//
// Split from the negotiation and stream conversions in
// bip_mojom_negotiation.cc on the seam the header already draws. The two
// halves answer different questions — "what is this action about" and "what
// does this endpoint support" — and they change for different reasons.

#include "taffy/components/intelligence/content/bip_mojom_conversions.h"

#include <utility>

namespace taffy {

mojom::OriginPtr ToMojom(const Origin& origin) {
  auto out = mojom::Origin::New();
  out->kind = origin.kind == OriginKind::kTuple ? mojom::OriginKind::kTuple
                                                : mojom::OriginKind::kOpaque;
  if (origin.kind == OriginKind::kTuple) {
    out->serialization = origin.serialization;
  } else {
    out->opaque_id = origin.opaque_id;
  }
  return out;
}

Origin FromMojom(const mojom::Origin& origin) {
  Origin out;
  switch (origin.kind) {
    case mojom::OriginKind::kTuple:
      out.kind = OriginKind::kTuple;
      out.serialization = origin.serialization.value_or(std::string());
      break;
    case mojom::OriginKind::kOpaque:
      out.kind = OriginKind::kOpaque;
      out.opaque_id = origin.opaque_id.value_or(std::string());
      break;
  }
  return out;
}

mojom::ContentDigestPtr ToMojom(const ContentDigest& digest) {
  auto out = mojom::ContentDigest::New();
  out->algorithm = mojom::DigestAlgorithm::kSha256;
  out->value = digest.value;
  return out;
}

std::optional<ContentDigest> FromMojom(const mojom::ContentDigest& digest) {
  ContentDigest out;
  switch (digest.algorithm) {
    case mojom::DigestAlgorithm::kSha256:
      out.algorithm = DigestAlgorithm::kSha256;
      break;
  }
  // A digest whose algorithm this build does not know is not a digest it can
  // compare, so FromMojom returns nullopt below on an empty value and the
  // caller treats the precondition as unsatisfiable.
  out.value = digest.value;
  return out.is_valid() ? std::optional<ContentDigest>(std::move(out))
                        : std::nullopt;
}

mojom::ActionType ToMojom(ActionType type) {
  switch (type) {
    case ActionType::kActivate:
      return mojom::ActionType::kActivate;
    case ActionType::kFocus:
      return mojom::ActionType::kFocus;
    case ActionType::kScrollIntoView:
      return mojom::ActionType::kScrollIntoView;
    case ActionType::kSetText:
      return mojom::ActionType::kSetText;
    case ActionType::kSelectOption:
      return mojom::ActionType::kSelectOption;
    case ActionType::kToggle:
      return mojom::ActionType::kToggle;
    case ActionType::kSubmitForm:
      return mojom::ActionType::kSubmitForm;
  }
  // Unreachable for a valid enumerator. Present because the compiler cannot
  // prove that, and because a value outside the enumeration must not fall off
  // the end of the function. Callers validate with IsKnownActionType before
  // this conversion; this fallback never authorizes an action.
  return mojom::ActionType::kSubmitForm;
}

std::optional<ActionType> FromMojom(mojom::ActionType type) {
  switch (type) {
    case mojom::ActionType::kActivate:
      return ActionType::kActivate;
    case mojom::ActionType::kFocus:
      return ActionType::kFocus;
    case mojom::ActionType::kScrollIntoView:
      return ActionType::kScrollIntoView;
    case mojom::ActionType::kSetText:
      return ActionType::kSetText;
    case mojom::ActionType::kSelectOption:
      return ActionType::kSelectOption;
    case mojom::ActionType::kToggle:
      return ActionType::kToggle;
    case mojom::ActionType::kSubmitForm:
      return ActionType::kSubmitForm;
  }
  return std::nullopt;
}

mojom::NodeState ToMojom(NodeState state) {
  switch (state) {
    case NodeState::kVisible:
      return mojom::NodeState::kVisible;
    case NodeState::kNotVisible:
      return mojom::NodeState::kNotVisible;
    case NodeState::kOffscreen:
      return mojom::NodeState::kOffscreen;
    case NodeState::kObscured:
      return mojom::NodeState::kObscured;
    case NodeState::kEnabled:
      return mojom::NodeState::kEnabled;
    case NodeState::kDisabled:
      return mojom::NodeState::kDisabled;
    case NodeState::kEditable:
      return mojom::NodeState::kEditable;
    case NodeState::kReadOnly:
      return mojom::NodeState::kReadOnly;
    case NodeState::kRequired:
      return mojom::NodeState::kRequired;
    case NodeState::kInvalid:
      return mojom::NodeState::kInvalid;
    case NodeState::kChecked:
      return mojom::NodeState::kChecked;
    case NodeState::kUnchecked:
      return mojom::NodeState::kUnchecked;
    case NodeState::kMixed:
      return mojom::NodeState::kMixed;
    case NodeState::kSelected:
      return mojom::NodeState::kSelected;
    case NodeState::kExpanded:
      return mojom::NodeState::kExpanded;
    case NodeState::kCollapsed:
      return mojom::NodeState::kCollapsed;
    case NodeState::kFocused:
      return mojom::NodeState::kFocused;
    case NodeState::kBusy:
      return mojom::NodeState::kBusy;
  }
  // Unreachable for a valid enumerator. kBusy is the least permissive state to
  // name: it satisfies no positive action requirement.
  return mojom::NodeState::kBusy;
}

std::optional<NodeState> FromMojom(mojom::NodeState state) {
  switch (state) {
    case mojom::NodeState::kVisible:
      return NodeState::kVisible;
    case mojom::NodeState::kNotVisible:
      return NodeState::kNotVisible;
    case mojom::NodeState::kOffscreen:
      return NodeState::kOffscreen;
    case mojom::NodeState::kObscured:
      return NodeState::kObscured;
    case mojom::NodeState::kEnabled:
      return NodeState::kEnabled;
    case mojom::NodeState::kDisabled:
      return NodeState::kDisabled;
    case mojom::NodeState::kEditable:
      return NodeState::kEditable;
    case mojom::NodeState::kReadOnly:
      return NodeState::kReadOnly;
    case mojom::NodeState::kRequired:
      return NodeState::kRequired;
    case mojom::NodeState::kInvalid:
      return NodeState::kInvalid;
    case mojom::NodeState::kChecked:
      return NodeState::kChecked;
    case mojom::NodeState::kUnchecked:
      return NodeState::kUnchecked;
    case mojom::NodeState::kMixed:
      return NodeState::kMixed;
    case mojom::NodeState::kSelected:
      return NodeState::kSelected;
    case mojom::NodeState::kExpanded:
      return NodeState::kExpanded;
    case mojom::NodeState::kCollapsed:
      return NodeState::kCollapsed;
    case mojom::NodeState::kFocused:
      return NodeState::kFocused;
    case mojom::NodeState::kBusy:
      return NodeState::kBusy;
  }
  return std::nullopt;
}

mojom::NodeHandlePtr ToMojom(const NodeHandle& handle) {
  auto out = mojom::NodeHandle::New();
  out->tab_id = handle.tab_id.value;
  out->frame_id = handle.frame_id.value;
  out->page_epoch = handle.page_epoch.value;
  out->graph_revision = handle.graph_revision;
  out->node_id = handle.node_id.value;
  out->expected_origin = ToMojom(handle.expected_origin);
  return out;
}

ResolvedNodeFacts FromMojom(const mojom::ResolvedNode& node) {
  ResolvedNodeFacts facts;
  facts.node_id = SemanticNodeId{node.node_id};
  facts.display_label = node.display_label;
  facts.observed_at_revision = node.observed_at_revision;
  facts.role = static_cast<uint16_t>(node.role);
  facts.sensitivity = static_cast<Sensitivity>(node.sensitivity);
  switch (node.content_trust) {
    case mojom::ContentTrust::kFirstPartyDocument:
      facts.content_trust = ContentTrust::kFirstPartyDocument;
      break;
    case mojom::ContentTrust::kUserGeneratedContent:
      facts.content_trust = ContentTrust::kUserGeneratedContent;
      break;
    case mojom::ContentTrust::kThirdPartyEmbedded:
      facts.content_trust = ContentTrust::kThirdPartyEmbedded;
      break;
    case mojom::ContentTrust::kUnknownUntrusted:
    case mojom::ContentTrust::kUserAuthored:
    case mojom::ContentTrust::kTaffyAuthored:
    case mojom::ContentTrust::kModelAuthored:
      facts.content_trust = ContentTrust::kUnknownUntrusted;
      break;
  }
  switch (node.challenge_kind) {
    case mojom::ChallengeKind::kNone:
      facts.challenge_kind = ChallengeKind::kNone;
      break;
    case mojom::ChallengeKind::kImage:
      facts.challenge_kind = ChallengeKind::kImage;
      break;
    case mojom::ChallengeKind::kInteractive:
      facts.challenge_kind = ChallengeKind::kInteractive;
      break;
    case mojom::ChallengeKind::kOneTimeCode:
      facts.challenge_kind = ChallengeKind::kOneTimeCode;
      break;
  }
  if (node.challenge_bounds) {
    facts.challenge_bounds = ChallengeBounds{
        .x = node.challenge_bounds->x,
        .y = node.challenge_bounds->y,
        .width = node.challenge_bounds->width,
        .height = node.challenge_bounds->height,
    };
  }
  for (const std::string& child : node.form_field_node_ids) {
    facts.form_field_node_ids.emplace_back(child);
  }

  for (mojom::ActionType action : node.actions) {
    if (std::optional<ActionType> converted = FromMojom(action)) {
      facts.available_actions.push_back(*converted);
    }
  }
  for (mojom::NodeState state : node.states) {
    if (std::optional<NodeState> converted = FromMojom(state)) {
      facts.asserted_states.push_back(*converted);
    }
  }
  if (node.value_digest) {
    facts.value_digest = FromMojom(*node.value_digest);
  }
  facts.value_changed_at_revision = node.value_changed_at_revision;
  // The destination is read as a normalized string only. It is compared
  // against the browser's own record, never followed.
  if (node.destination && node.destination->url_metadata) {
    Destination destination;
    destination.is_cross_origin = node.destination->is_cross_origin;
    destination.opens_new_tab = node.destination->opens_new_tab;
    destination.is_download = node.destination->is_download;
    const mojom::UrlMetadata& metadata = *node.destination->url_metadata;
    destination.url_metadata.origin = FromMojom(*metadata.origin);
    destination.url_metadata.disclosure =
        static_cast<UrlDisclosure>(metadata.disclosure);
    destination.url_metadata.path = metadata.path;
    destination.url_metadata.url = metadata.url;
    destination.url_metadata.has_query = metadata.has_query;
    destination.url_metadata.has_fragment = metadata.has_fragment;
    facts.destination = std::move(destination);
  }
  return facts;
}

ActionResultCode FromMojom(mojom::NodeResolutionCode code) {
  switch (code) {
    case mojom::NodeResolutionCode::kOk:
      // Callers check for kOk before converting; reaching here means the
      // caller has a bug, not that the action succeeded.
      return ActionResultCode::kInternalError;
    case mojom::NodeResolutionCode::kStalePageEpoch:
      return ActionResultCode::kStalePageEpoch;
    case mojom::NodeResolutionCode::kStaleGraph:
      return ActionResultCode::kStaleGraph;
    case mojom::NodeResolutionCode::kNodeGone:
      return ActionResultCode::kNodeGone;
    case mojom::NodeResolutionCode::kOriginChanged:
      return ActionResultCode::kOriginChanged;
    case mojom::NodeResolutionCode::kDocumentInactive:
      return ActionResultCode::kDocumentInactive;
    case mojom::NodeResolutionCode::kUnsupported:
      return ActionResultCode::kUnsupported;
    case mojom::NodeResolutionCode::kInternalError:
      return ActionResultCode::kInternalError;
  }
  // An unknown wire value fails closed rather than falling through to
  // anything permissive (protocol section 6.2).
  return ActionResultCode::kInternalError;
}

ActionResultCode FromMojom(mojom::RendererActionOutcome outcome) {
  switch (outcome) {
    case mojom::RendererActionOutcome::kDispatched:
      // Not a terminal success. The dispatcher checks for kDispatched before
      // converting and hands over to the verifier; there is no path from here
      // to kVerified.
      return ActionResultCode::kNavigationStarted;
    case mojom::RendererActionOutcome::kUnsupported:
      return ActionResultCode::kUnsupported;
    case mojom::RendererActionOutcome::kStalePageEpoch:
      return ActionResultCode::kStalePageEpoch;
    case mojom::RendererActionOutcome::kStaleGraph:
      return ActionResultCode::kStaleGraph;
    case mojom::RendererActionOutcome::kNodeGone:
      return ActionResultCode::kNodeGone;
    case mojom::RendererActionOutcome::kOriginChanged:
      return ActionResultCode::kOriginChanged;
    case mojom::RendererActionOutcome::kRoleOrActionChanged:
      return ActionResultCode::kRoleOrActionChanged;
    case mojom::RendererActionOutcome::kNotVisible:
      return ActionResultCode::kNotVisible;
    case mojom::RendererActionOutcome::kOccluded:
      return ActionResultCode::kOccluded;
    case mojom::RendererActionOutcome::kNotEnabled:
      return ActionResultCode::kNotEnabled;
    case mojom::RendererActionOutcome::kNotEditable:
      return ActionResultCode::kNotEditable;
    case mojom::RendererActionOutcome::kSensitiveField:
      return ActionResultCode::kSensitiveField;
    case mojom::RendererActionOutcome::kDestinationChanged:
      return ActionResultCode::kDestinationChanged;
    case mojom::RendererActionOutcome::kDocumentInactive:
      return ActionResultCode::kDocumentInactive;
    case mojom::RendererActionOutcome::kPreconditionFailed:
      return ActionResultCode::kPostconditionFailed;
    case mojom::RendererActionOutcome::kDeadlineExceeded:
      return ActionResultCode::kPostconditionTimeout;
    case mojom::RendererActionOutcome::kCancelled:
      return ActionResultCode::kCancelledByNavigation;
    case mojom::RendererActionOutcome::kInternalError:
      return ActionResultCode::kDispatchFailed;
  }
  return ActionResultCode::kInternalError;
}

}  // namespace taffy
