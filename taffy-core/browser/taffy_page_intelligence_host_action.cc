// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <utility>

#include "crypto/sha2.h"
#include "taffy/browser/core_task_action.h"
#include "taffy/browser/taffy_page_intelligence_host.h"
#include "taffy/common/public/bip_identity.h"
#include "taffy/components/intelligence/content/bip_schema_version.h"
#include "taffy/components/intelligence/content/page_intelligence_service_impl.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

Origin TupleOrigin(const std::string& serialization) {
  Origin origin;
  origin.kind = OriginKind::kTuple;
  origin.serialization = serialization;
  return origin;
}

// Builds the dispatcher envelope from a granted task action. The capability
// fields come from the ledger record minted at policy evaluation — the
// reducer's proposal digest is what that grant was bound to, not a BIP
// envelope digest computed later from live document facts.
std::optional<AuthorizedActionEnvelope> BuildTaskActionEnvelope(
    const mojom::TaskActionEffect& action,
    const std::string& task_id,
    uint64_t deadline_monotonic_ms,
    const CapabilityLedger& capabilities) {
  if (!action.document || !action.executable || !action.executable->node_id ||
      deadline_monotonic_ms == 0u) {
    return std::nullopt;
  }
  const std::optional<ActionType> action_type =
      PageActionTypeForClass(action.executable->action_class);
  const std::optional<PostconditionKind> postcondition =
      PagePostconditionForTaskAction(action.executable->action_class,
                                     action.postcondition);
  if (!action_type || !postcondition) {
    return std::nullopt;
  }
  const std::optional<ActionInput> input =
      PageInputForTaskAction(action, *action_type);
  if (!input) {
    return std::nullopt;
  }

  AuthorizedActionEnvelope envelope;
  envelope.schema_version = kBipSchemaVersion;
  envelope.dispatch_id = DispatchId{action.dispatch_id};
  envelope.action_id = ActionId{action.action_id};
  envelope.task_id = TaskId{task_id};
  envelope.idempotency_key = action.idempotency_key;
  envelope.action_type = *action_type;
  envelope.target_handle.tab_id = TabId{action.executable->tab_id};
  envelope.target_handle.frame_id = FrameId{action.document->frame_id};
  envelope.target_handle.page_epoch = PageEpoch{action.document->page_epoch};
  envelope.target_handle.graph_revision = action.document->graph_revision;
  envelope.target_handle.node_id = SemanticNodeId{*action.executable->node_id};
  envelope.target_handle.expected_origin =
      TupleOrigin(action.document->normalized_origin);
  envelope.required_graph_revision = action.document->graph_revision;
  envelope.idempotency_policy =
      *action_type == ActionType::kScrollIntoView ||
              *action_type == ActionType::kActivate ||
              *action_type == ActionType::kFocus
          ? IdempotencyPolicy::kConditionallyIdempotent
          : IdempotencyPolicy::kNonIdempotent;
  envelope.input = *input;
  envelope.principal.kind = PrincipalKind::kAssistant;
  envelope.absolute_deadline_monotonic_ms = deadline_monotonic_ms;

  // Preserve the exact frozen document scope the Core effect carried. These
  // are rechecked in the browser and forwarded where the renderer has a local
  // equivalent; omitting them would turn the task envelope into a less
  // constrained action than the one policy reviewed.
  Precondition page_epoch;
  page_epoch.kind = PreconditionKind::kExactPageEpoch;
  page_epoch.page_epoch = envelope.target_handle.page_epoch;
  envelope.preconditions.push_back(page_epoch);

  Precondition revision;
  revision.kind = PreconditionKind::kAcceptableGraphRevision;
  revision.min_graph_revision = envelope.required_graph_revision;
  envelope.preconditions.push_back(revision);

  Precondition origin;
  origin.kind = PreconditionKind::kExactOrigin;
  origin.origin = envelope.target_handle.expected_origin;
  envelope.preconditions.push_back(origin);

  Precondition active;
  active.kind = PreconditionKind::kDocumentActive;
  envelope.preconditions.push_back(active);

  Precondition exists;
  exists.kind = PreconditionKind::kNodeExists;
  envelope.preconditions.push_back(exists);

  Postcondition expected;
  expected.kind = *postcondition;
  if (*action_type == ActionType::kActivate) {
    const std::optional<CanonicalDomActivationIntent> activation =
        ReadCanonicalDomActivationIntent(action.executable->canonical_intent);
    if (!activation || activation->target.expected_origin_is_opaque ||
        activation->target.tab_id != action.executable->tab_id ||
        activation->target.frame_id != action.document->frame_id ||
        activation->target.page_epoch != action.document->page_epoch ||
        activation->target.graph_revision != action.document->graph_revision ||
        activation->target.node_id != *action.executable->node_id ||
        activation->target.expected_origin !=
            action.document->normalized_origin) {
      return std::nullopt;
    }
    if (activation->expected_expanded.has_value()) {
      // A disclosure activation, which is the narrow case and keeps its exact
      // claim. A newer snapshot containing the requested state is not by
      // itself proof that this activation changed the target: unrelated page
      // work could have advanced the revision while the disclosure was
      // already in that state. Require the exact opposite browser-observed
      // state immediately before dispatch, then verify the requested state on
      // a strictly newer snapshot.
      Precondition disclosure_before;
      disclosure_before.kind = PreconditionKind::kNodeStateAsserted;
      disclosure_before.node_state = *activation->expected_expanded
                                         ? NodeState::kCollapsed
                                         : NodeState::kExpanded;
      envelope.preconditions.push_back(disclosure_before);
      expected.expected_node_state = *activation->expected_expanded
                                         ? NodeState::kExpanded
                                         : NodeState::kCollapsed;
    } else {
      // An ordinary press. The class-level postcondition is narrowed here
      // rather than in `PagePostconditionForTaskAction`, because the
      // difference is not the action class — it is what the durable intent
      // claims, and that function is given the class alone.
      //
      // There is no state to require before dispatch and none to verify
      // after: a page's own control decides what a press does, and demanding
      // an expanded-or-collapsed target was the reason a sign-in button, a
      // send and a continue were each refused as POSTCONDITION_FAILED with no
      // other admissible move on the page. What the browser can still prove
      // is that the document moved past this dispatch, and that is what it
      // claims.
      expected.kind = PostconditionKind::kDocumentAdvanced;
    }
  } else if (*action_type == ActionType::kFocus) {
    if (!CanonicalDomFocusIntentMatchesDocument(
            action.executable->canonical_intent, action.executable->tab_id,
            action.document->frame_id, action.document->page_epoch,
            action.document->graph_revision, *action.executable->node_id,
            action.document->normalized_origin)) {
      return std::nullopt;
    }
    Precondition focus_before;
    focus_before.kind = PreconditionKind::kNodeStateAbsent;
    focus_before.node_state = NodeState::kFocused;
    envelope.preconditions.push_back(focus_before);
    expected.expected_node_state = NodeState::kFocused;
  } else if (*action_type == ActionType::kToggle) {
    expected.expected_node_state =
        input->checked.value() ? NodeState::kChecked : NodeState::kUnchecked;
  } else if (*action_type == ActionType::kSubmitForm) {
    // A submit control is a browser-observed navigation primitive. Until the
    // typed intent carries a reviewed destination, the strongest exact claim
    // this browser may make is a commit within the already-granted origin.
    // Cross-origin submissions are therefore not silently widened.
    expected.allowed_origins.push_back(envelope.target_handle.expected_origin);
  }
  envelope.expected_postconditions.push_back(expected);

  envelope.action_digest.algorithm = DigestAlgorithm::kSha256;
  envelope.action_digest.value = action.proposal_digest;
  envelope.canonical_intent_digest =
      crypto::SHA256Hash(action.executable->canonical_intent);
  if (!capabilities.CopyRegisteredCapability(
          CapabilityReference{action.capability_id}, envelope.capability)) {
    return std::nullopt;
  }
  envelope.capability_reference = envelope.capability.capability_reference;
  if (!envelope.target_handle.is_well_formed() ||
      !envelope.dispatch_id.is_valid() || !envelope.action_id.is_valid() ||
      !envelope.task_id.is_valid() || !envelope.action_digest.is_valid()) {
    return std::nullopt;
  }
  return envelope;
}

}  // namespace

void DispatchTaskActionOnTabForCore(
    content::BrowserContext* browser_context,
    const core_service::mojom::TaskActionEffect& action,
    const std::string& task_id,
    uint64_t deadline_monotonic_ms,
    CapabilityLedger& capabilities,
    TaskActionCompletion callback) {
  if (!callback) {
    return;
  }
  if (!browser_context || !action.executable) {
    std::move(callback).Run(std::nullopt);
    return;
  }
  std::optional<AuthorizedActionEnvelope> envelope = BuildTaskActionEnvelope(
      action, task_id, deadline_monotonic_ms, capabilities);
  TaffyPageIntelligenceHost* host =
      FindPageIntelligenceHost(browser_context, action.executable->tab_id);
  if (!envelope || !host) {
    std::move(callback).Run(std::nullopt);
    return;
  }
  host->RequestTaskAction(std::move(*envelope), std::move(callback));
}

void TaffyPageIntelligenceHost::RequestTaskAction(
    AuthorizedActionEnvelope envelope,
    TaskActionCompletion callback) {
  if (!service_ || !callback) {
    if (callback) {
      std::move(callback).Run(std::nullopt);
    }
    return;
  }
  service_->SubmitTaskAction(
      std::move(envelope),
      base::BindOnce(
          [](TaskActionCompletion done, ActionResult result) {
            std::move(done).Run(std::move(result));
          },
          std::move(callback)));
}

}  // namespace taffy
