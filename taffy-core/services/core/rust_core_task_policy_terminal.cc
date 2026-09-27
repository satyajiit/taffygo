// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <optional>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/services/core/rust_core_task_effect.h"
#include "taffy/services/core/rust_core_task_effect_terminal_internal.h"

namespace taffy::core_service_internal {

namespace mojom = core_service::mojom;
namespace bridge = core_bridge;

namespace {

// A grant with no discovery authority ordinarily stands on a page, and a
// page's origin is a tuple. The one other document a task may act from is one
// with no site of its own - the error document Chromium writes when an address
// does not answer - and from there it may only leave. This admits that shape
// and nothing wider: the clause the browser's narrowing gate carries in
// `CommandDiscoveryMatchesGrant` (decision 0176). Refusing it here dropped a
// granted search from a dead host's error page and ended the core.
bool NonDiscoveryOriginIsWellFormed(const mojom::TaskPolicyEffect& policy,
                                    const mojom::PolicyCapabilityScope& scope) {
  const mojom::PolicyOrigin& origin = *scope.origin;
  if (origin.kind == mojom::PolicyOriginKind::kTuple) {
    return origin.serialization.has_value() && !origin.opaque_id.has_value();
  }
  return origin.kind == mojom::PolicyOriginKind::kOpaque &&
         (policy.operation_kind == mojom::TaskActionOperationKind::kNavigate ||
          policy.operation_kind == mojom::TaskActionOperationKind::kSearch) &&
         origin.opaque_id.has_value() && BoundedIdentifier(*origin.opaque_id) &&
         !origin.serialization.has_value() && !scope.node_id.has_value() &&
         scope.required_graph_revision == 0u &&
         scope.destination_address.has_value();
}

}  // namespace

// The browser's answer to one ASK_POLICY effect, as the terminal the bridge
// settles the action with. A granted answer carries the minted grant's exact
// facts; a denied one carries its closed code; every other status crosses
// bare, and the bridge refuses the proposal under it rather than leaving the
// action open.
std::optional<bridge::BridgeTaskTerminal> ToBridgeTaskPolicyTerminal(
    const mojom::TaskEffectBinding& effect,
    const mojom::PolicyEvaluationResult& result) {
  if (!effect.operation || !effect.policy ||
      effect.kind != mojom::TaskReducerEffectKind::kAskPolicy ||
      result.direct_observation_effect ||
      result.operation_id != effect.operation->operation_id) {
    return std::nullopt;
  }
  auto out = TaskTerminalBase(effect, static_cast<uint8_t>(result.status));
  out.result_operation_id = result.operation_id;
  // A denial's code travels only on a denied answer. A denied answer without
  // one is still a denial — the bridge reads it as the bare policy refusal.
  if (result.denial) {
    if (result.status != mojom::PolicyEvaluationStatus::kDenied) {
      return std::nullopt;
    }
    out.has_denial_code = true;
    out.denial_code = static_cast<uint8_t>(result.denial->code);
  }
  if (result.status == mojom::PolicyEvaluationStatus::kGranted) {
    if (!result.minted_grant || !result.minted_grant->scope ||
        !result.minted_grant->scope->origin) {
      return std::nullopt;
    }
    const bool discovery = !effect.policy->discovery.is_null();
    const auto& grant_discovery = result.minted_grant->discovery;
    const auto& origin = result.minted_grant->scope->origin;
    if (discovery != !grant_discovery.is_null() ||
        (discovery && (grant_discovery->discovery_tab_id !=
                           effect.policy->discovery->discovery_tab_id ||
                       grant_discovery->browser_session_id !=
                           effect.policy->discovery->browser_session_id ||
                       grant_discovery->remaining_new_source_cap !=
                           effect.policy->discovery->remaining_new_source_cap ||
                       origin->kind != mojom::PolicyOriginKind::kOpaque ||
                       origin->serialization || !origin->opaque_id ||
                       !BoundedIdentifier(*origin->opaque_id))) ||
        (!discovery && !NonDiscoveryOriginIsWellFormed(
                           *effect.policy, *result.minted_grant->scope))) {
      return std::nullopt;
    }
    out.capability_id = result.minted_grant->capability_id;
    out.frame_id = result.minted_grant->scope->frame_id;
    out.page_epoch = result.minted_grant->scope->page_epoch;
    out.graph_revision = result.minted_grant->scope->required_graph_revision;
    if (origin->kind == mojom::PolicyOriginKind::kOpaque) {
      out.has_opaque_origin_id = true;
      out.opaque_origin_id = *origin->opaque_id;
    } else {
      out.normalized_origin = *origin->serialization;
    }
    if (result.minted_grant->scope->destination_scope ||
        result.minted_grant->scope->destination_address) {
      const auto& destination = result.minted_grant->scope->destination_scope;
      if (!destination || !result.minted_grant->scope->destination_address ||
          destination->kind != mojom::PolicyOriginKind::kTuple ||
          !destination->serialization ||
          result.minted_grant->scope->destination_address->empty()) {
        return std::nullopt;
      }
      out.has_destination_origin = true;
      out.destination_origin = *destination->serialization;
      out.has_destination_address = true;
      out.destination_address =
          *result.minted_grant->scope->destination_address;
    }
  } else if (result.minted_grant) {
    return std::nullopt;
  }
  return out;
}

}  // namespace taffy::core_service_internal
