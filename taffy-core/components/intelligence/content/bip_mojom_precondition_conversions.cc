// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Browser-to-renderer action precondition projection. Browser-only policy
// witnesses stay in the browser; live node guards cross only when repeating
// them in the renderer can narrow, never widen, the browser decision.

#include "taffy/components/intelligence/content/bip_mojom_conversions.h"

namespace taffy {
namespace {

mojom::ContentTrust ToMojomContentTrust(ContentTrust trust) {
  switch (trust) {
    case ContentTrust::kUserAuthored:
      return mojom::ContentTrust::kUserAuthored;
    case ContentTrust::kTaffyAuthored:
      return mojom::ContentTrust::kTaffyAuthored;
    case ContentTrust::kFirstPartyDocument:
      return mojom::ContentTrust::kFirstPartyDocument;
    case ContentTrust::kUserGeneratedContent:
      return mojom::ContentTrust::kUserGeneratedContent;
    case ContentTrust::kThirdPartyEmbedded:
      return mojom::ContentTrust::kThirdPartyEmbedded;
    case ContentTrust::kModelAuthored:
      return mojom::ContentTrust::kModelAuthored;
    case ContentTrust::kUnknownUntrusted:
      return mojom::ContentTrust::kUnknownUntrusted;
  }
  return mojom::ContentTrust::kUnknownUntrusted;
}

mojom::DestinationPtr ToMojomDestination(const Destination& destination) {
  auto out = mojom::Destination::New();
  out->url_metadata = mojom::UrlMetadata::New();
  out->url_metadata->origin = ToMojom(destination.url_metadata.origin);
  out->url_metadata->disclosure =
      static_cast<mojom::UrlDisclosure>(destination.url_metadata.disclosure);
  out->url_metadata->path = destination.url_metadata.path;
  out->url_metadata->url = destination.url_metadata.url;
  out->url_metadata->has_query = destination.url_metadata.has_query;
  out->url_metadata->has_fragment = destination.url_metadata.has_fragment;
  out->is_cross_origin = destination.is_cross_origin;
  out->opens_new_tab = destination.opens_new_tab;
  out->is_download = destination.is_download;
  return out;
}

}  // namespace

mojom::PreconditionKind ToMojom(PreconditionKind kind) {
  switch (kind) {
    case PreconditionKind::kExactPageEpoch:
      return mojom::PreconditionKind::kExactPageEpoch;
    case PreconditionKind::kAcceptableGraphRevision:
      return mojom::PreconditionKind::kAcceptableGraphRevision;
    case PreconditionKind::kExactOrigin:
      return mojom::PreconditionKind::kExactOrigin;
    case PreconditionKind::kAllowedRedirectSet:
      return mojom::PreconditionKind::kAllowedRedirectSet;
    case PreconditionKind::kDocumentActive:
      return mojom::PreconditionKind::kDocumentActive;
    case PreconditionKind::kNodeExists:
      return mojom::PreconditionKind::kNodeExists;
    case PreconditionKind::kNodeRoleUnchanged:
      return mojom::PreconditionKind::kNodeRoleUnchanged;
    case PreconditionKind::kNodeActionAvailable:
      return mojom::PreconditionKind::kNodeActionAvailable;
    case PreconditionKind::kNodeStateAsserted:
      return mojom::PreconditionKind::kNodeStateAsserted;
    case PreconditionKind::kNodeStateAbsent:
      return mojom::PreconditionKind::kNodeStateAbsent;
    case PreconditionKind::kExpectedDestination:
      return mojom::PreconditionKind::kExpectedDestination;
    case PreconditionKind::kExpectedValueDigest:
      return mojom::PreconditionKind::kExpectedValueDigest;
    case PreconditionKind::kNotSensitiveField:
      return mojom::PreconditionKind::kNotSensitiveField;
    case PreconditionKind::kNoUserInteractionSinceLease:
      return mojom::PreconditionKind::kNoUserInteractionSinceLease;
    case PreconditionKind::kBudgetRemaining:
      return mojom::PreconditionKind::kBudgetRemaining;
    case PreconditionKind::kDestinationClassAllowed:
      return mojom::PreconditionKind::kDestinationClassAllowed;
    case PreconditionKind::kContentTrustAtLeast:
      return mojom::PreconditionKind::kContentTrustAtLeast;
    case PreconditionKind::kPreparedEffectUnchanged:
      return mojom::PreconditionKind::kPreparedEffectUnchanged;
    case PreconditionKind::kNoUndeclaredEgress:
      return mojom::PreconditionKind::kNoUndeclaredEgress;
  }
  return mojom::PreconditionKind::kNodeExists;
}

mojom::PreconditionPtr ToRendererPrecondition(
    const Precondition& precondition) {
  switch (precondition.kind) {
    case PreconditionKind::kExactOrigin:
    case PreconditionKind::kAllowedRedirectSet:
    case PreconditionKind::kBudgetRemaining:
    case PreconditionKind::kNoUserInteractionSinceLease:
    case PreconditionKind::kDestinationClassAllowed:
    case PreconditionKind::kPreparedEffectUnchanged:
    case PreconditionKind::kNoUndeclaredEgress:
      return nullptr;
    case PreconditionKind::kExactPageEpoch:
    case PreconditionKind::kAcceptableGraphRevision:
    case PreconditionKind::kDocumentActive:
    case PreconditionKind::kNodeExists:
    case PreconditionKind::kNodeRoleUnchanged:
    case PreconditionKind::kNodeActionAvailable:
    case PreconditionKind::kNodeStateAsserted:
    case PreconditionKind::kNodeStateAbsent:
    case PreconditionKind::kExpectedDestination:
    case PreconditionKind::kExpectedValueDigest:
    case PreconditionKind::kNotSensitiveField:
    case PreconditionKind::kContentTrustAtLeast:
      break;
  }

  auto out = mojom::Precondition::New();
  out->kind = ToMojom(precondition.kind);
  if (precondition.page_epoch) {
    out->page_epoch = precondition.page_epoch->value;
  }
  out->min_graph_revision = precondition.min_graph_revision;
  if (precondition.expected_role) {
    out->expected_role =
        static_cast<mojom::SemanticRole>(*precondition.expected_role);
  }
  if (precondition.expected_action_type) {
    out->expected_action_type = ToMojom(*precondition.expected_action_type);
  }
  if (precondition.node_state) {
    out->node_state = ToMojom(*precondition.node_state);
  }
  if (precondition.expected_value_digest) {
    out->expected_value_digest = ToMojom(*precondition.expected_value_digest);
  }
  if (precondition.max_sensitivity) {
    out->max_sensitivity =
        static_cast<mojom::Sensitivity>(*precondition.max_sensitivity);
  }
  if (precondition.expected_destination) {
    out->expected_destination =
        ToMojomDestination(*precondition.expected_destination);
  }
  if (precondition.min_content_trust) {
    out->min_content_trust =
        ToMojomContentTrust(*precondition.min_content_trust);
  }
  return out;
}

}  // namespace taffy
