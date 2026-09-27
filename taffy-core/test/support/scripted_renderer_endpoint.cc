// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/test/support/scripted_renderer_endpoint.h"

#include <string>
#include <utility>

#include "base/check.h"
#include "base/check_op.h"
#include "base/functional/bind.h"
#include "base/memory/ptr_util.h"
#include "base/strings/string_number_conversions.h"
#include "taffy/components/intelligence/content/bip_schema_version.h"
#include "taffy/test/support/scripted_reply_factory.h"
#include "content/public/browser/render_frame_host.h"
#include "mojo/public/cpp/bindings/pending_associated_receiver.h"
#include "third_party/blink/public/common/associated_interfaces/associated_interface_provider.h"

namespace taffy::test {


// static
std::unique_ptr<ScriptedRendererEndpoint> ScriptedRendererEndpoint::InstallFor(
    content::RenderFrameHost* host) {
  CHECK(host) << "Cannot install a scripted endpoint on a null frame.";
  auto endpoint = base::WrapUnique(new ScriptedRendererEndpoint());
  // VERIFY AT SP-04: blink::AssociatedInterfaceProvider::OverrideBinderForTesting
  // and RenderFrameHost::GetRemoteAssociatedInterfaces(). Upstream files to
  // read:
  //   content/public/browser/render_frame_host.h
  //   third_party/blink/public/common/associated_interfaces/
  //       associated_interface_provider.h
  // If the override helper is spelled differently at the pinned milestone, this
  // is the only call site that changes; if associated interfaces are dropped in
  // favour of ordinary ones — which is open decision OD-027 — the receiver type
  // changes with it and nothing else in this file does.
  host->GetRemoteAssociatedInterfaces()->OverrideBinderForTesting(
      mojom::PageIntelligence::Name_,
      base::BindRepeating(&ScriptedRendererEndpoint::Bind,
                          base::Unretained(endpoint.get())));
  return endpoint;
}

ScriptedRendererEndpoint::ScriptedRendererEndpoint() = default;
ScriptedRendererEndpoint::~ScriptedRendererEndpoint() = default;

void ScriptedRendererEndpoint::Bind(mojo::ScopedInterfaceEndpointHandle handle) {
  receiver_.reset();
  receiver_.Bind(mojo::PendingAssociatedReceiver<mojom::PageIntelligence>(
      std::move(handle)));
}

void ScriptedRendererEndpoint::SetPlan(RendererReplyPlan plan) {
  plan_ = std::move(plan);
}

void ScriptedRendererEndpoint::GetProtocolInfo(
    GetProtocolInfoCallback callback) {
  if (plan_.protocol_info == ProtocolInfoBehaviour::kNeverAnswers) {
    // Deliberately never answered. The browser's own deadline is what has to
    // produce the single terminal result; a reply here would prove nothing.
    // Parked rather than dropped - see the member's comment.
    parked_protocol_info_callbacks_.push_back(std::move(callback));
    return;
  }

  auto info = mojom::ProtocolInfo::New();
  info->protocol_version =
      plan_.protocol_info == ProtocolInfoBehaviour::kUnsupportedMajorVersion
          ? "99.0"
          : kBipSchemaVersion;
  info->implementation_id = "scripted-renderer-endpoint";
  info->supported_adapters = {mojom::AdapterKind::kDom,
                              mojom::AdapterKind::kAccessibility,
                              mojom::AdapterKind::kMetadata};
  info->supported_scopes = {
      mojom::ObservationScope::kViewport, mojom::ObservationScope::kInteractive,
      mojom::ObservationScope::kSection, mojom::ObservationScope::kDocument};
  info->supported_action_types = {
      mojom::ActionType::kScrollIntoView, mojom::ActionType::kFocus,
      mojom::ActionType::kActivate,       mojom::ActionType::kSetText,
      mojom::ActionType::kSelectOption,   mojom::ActionType::kToggle,
      mojom::ActionType::kSubmitForm,
  };
  info->supported_redaction_features = {
      mojom::RedactionFeature::kSecretValueSuppression,
      mojom::RedactionFeature::kSensitiveZoneClassification};

  auto limits = mojom::ProtocolLimits::New();
  const bool oversized =
      plan_.protocol_info == ProtocolInfoBehaviour::kOversizedLimits;
  // The honest values are small and unremarkable; the oversized ones are large
  // enough that no plausible process ceiling could be above them, so the test
  // asserts that clamping happened without restating the ceiling — which lives
  // in exactly one place and is not this file.
  limits->max_message_bytes = oversized ? 1u << 30 : 1u << 20;
  limits->max_nodes = oversized ? 1u << 24 : 2000;
  limits->max_text_bytes = oversized ? 1u << 28 : 1u << 18;
  limits->max_total_bytes = oversized ? 1u << 30 : 1u << 20;
  limits->max_depth = oversized ? 4096 : 64;
  limits->max_frames = oversized ? 4096 : 16;
  limits->max_delta_queue_depth = oversized ? 1u << 20 : 32;
  limits->max_snapshot_deadline_ms = oversized ? 1u << 24 : 10000;
  limits->min_delta_interval_ms = oversized ? 0 : 16;
  info->limits = std::move(limits);

  std::move(callback).Run(std::move(info));
}

mojom::PageSnapshotPtr ScriptedRendererEndpoint::BuildSnapshot(
    const mojom::SnapshotRequest& request) {
  auto snapshot = mojom::PageSnapshot::New();
  snapshot->schema_version = kBipSchemaVersion;
  snapshot->snapshot_id = "scripted-snapshot";
  snapshot->request_id = request.request_id;
  snapshot->profile_id = request.profile_id;
  snapshot->browser_window_id = "scripted-window";
  snapshot->tab_id = request.tab_id;
  snapshot->root_frame_id = request.root_frame_id;

  const bool foreign_epoch =
      plan_.snapshot == SnapshotBehaviour::kForeignPageEpochEcho;
  snapshot->page_epoch =
      foreign_epoch ? plan_.foreign_page_epoch
                    : request.expected_page_epoch.value_or("scripted-epoch");
  snapshot->graph_revision = 1;
  snapshot->event_sequence = next_event_sequence_++;
  snapshot->lifecycle_state = mojom::DocumentLifecycleState::kActive;

  const std::string origin =
      plan_.snapshot == SnapshotBehaviour::kForeignOriginEcho
          ? plan_.foreign_origin_serialization
          : "http://scripted.endpoint.invalid";
  snapshot->committed_url_metadata = ScriptedReplyFactory::OriginOnlyUrl(origin);
  snapshot->origin_metadata = ScriptedReplyFactory::OriginMetadata(origin);
  snapshot->capture_time_monotonic_ms = 0;
  snapshot->scope = request.scope;

  auto report = mojom::AdapterReport::New();
  report->adapter = mojom::AdapterKind::kDom;
  report->status = mojom::AdapterStatus::kOk;
  report->adapter_version = 1;
  report->extraction_rule_version = 1;
  snapshot->adapters.push_back(std::move(report));

  uint32_t node_count = plan_.honest_node_count;
  if (plan_.snapshot == SnapshotBehaviour::kOverBudgetNodeCount) {
    CHECK_GT(plan_.over_budget_node_count, request.max_nodes)
        << "An over-budget snapshot has to exceed the budget the browser "
           "actually sent, which is the clamped one. Read it from "
           "received_snapshot_requests() rather than assuming it.";
    node_count = plan_.over_budget_node_count;
  }
  std::string text = "scripted node text";
  if (plan_.snapshot == SnapshotBehaviour::kSecretInTextRun) {
    CHECK(!plan_.secret_to_emit.empty())
        << "A secret-carrying snapshot with no secret proves nothing. Read the "
           "token from the corpus.";
    text = plan_.secret_to_emit;
  }
  if (plan_.snapshot == SnapshotBehaviour::kOversizedMessage) {
    CHECK_GT(plan_.oversized_message_bytes, 0u);
    text = std::string(plan_.oversized_message_bytes, 'A');
  }
  for (uint32_t index = 0; index < node_count; ++index) {
    snapshot->nodes.push_back(ScriptedReplyFactory::Node(
        "node-" + base::NumberToString(index), request.root_frame_id, text));
  }

  snapshot->frames.push_back(ScriptedReplyFactory::MainFrame(
      request.root_frame_id, snapshot->page_epoch, snapshot->graph_revision,
      origin));

  if (plan_.snapshot == SnapshotBehaviour::kFabricatedFrame) {
    auto invented = mojom::FrameDescriptor::New();
    invented->frame_id = "frame-that-is-not-in-the-tree";
    invented->parent_frame_id = request.root_frame_id;
    invented->is_main_frame = false;
    invented->is_out_of_process = true;
    invented->is_cross_origin_to_parent = true;
    invented->origin_metadata = ScriptedReplyFactory::OriginMetadata("http://invented.invalid");
    invented->page_epoch = snapshot->page_epoch;
    invented->graph_revision = snapshot->graph_revision;
    invented->lifecycle_state = mojom::DocumentLifecycleState::kActive;
    invented->included = true;
    snapshot->frames.push_back(std::move(invented));
  }

  snapshot->truncation = ScriptedReplyFactory::NoTruncation();
  snapshot->redaction_summary = ScriptedReplyFactory::NoRedaction();
  return snapshot;
}

void ScriptedRendererEndpoint::GetSnapshot(mojom::SnapshotRequestPtr request,
                                           GetSnapshotCallback callback) {
  received_snapshot_requests_.push_back(request.Clone());
  // The only place a renderer endpoint is ever told which tab it is in. The
  // production endpoint keeps it for the same reason and from the same field.
  tab_id_ = request->tab_id;

  if (plan_.snapshot == SnapshotBehaviour::kNeverAnswers) {
    parked_snapshot_callbacks_.push_back(std::move(callback));
    return;
  }

  auto result = mojom::SnapshotResult::New();
  result->request_id = request->request_id;
  result->code = mojom::ObservationResultCode::kOk;
  result->snapshot = BuildSnapshot(*request);

  if (plan_.snapshot == SnapshotBehaviour::kRepliesTwice) {
    // Mojo permits one reply per callback, so a second reply is expressed the
    // only way a real compromised renderer could express it: as an unsolicited
    // stream message carrying the same sequence number. The subscriber path is
    // what has to drop it.
    auto second = result.Clone();
    std::move(callback).Run(std::move(result));
    if (delta_client_) {
      auto invalidation = mojom::PageInvalidation::New();
      invalidation->schema_version = kBipSchemaVersion;
      invalidation->subscription_id = subscription_id_;
      invalidation->tab_id = second->snapshot->tab_id;
      invalidation->frame_id = second->snapshot->root_frame_id;
      invalidation->page_epoch = second->snapshot->page_epoch;
      invalidation->event_sequence = second->snapshot->event_sequence;
      invalidation->reason = mojom::InvalidationReason::kAdapterRestart;
      invalidation->retires_page_epoch = false;
      invalidation->invalidates_child_frames_only = false;
      invalidation->resnapshot_required = false;
      invalidation->observed_at_monotonic_ms = 0;
      delta_client_->OnInvalidated(std::move(invalidation));
    }
    return;
  }

  std::move(callback).Run(std::move(result));
}

void ScriptedRendererEndpoint::Subscribe(
    mojom::PageSubscriptionOptionsPtr options,
    mojo::PendingRemote<mojom::PageDeltaClient> client,
    SubscribeCallback callback) {
  delta_client_.Bind(std::move(client));
  subscription_id_ = "scripted-subscription";
  subscription_epoch_ = options->expected_page_epoch;
  subscription_frame_id_ = options->frame_id;
  subscription_revision_ = 1;

  if (plan_.subscribe == SubscriptionBehaviour::kNeverAnswers) {
    // The stream state above is recorded first on purpose: this endpoint is
    // still here and still holding the delta pipe, which is exactly the case
    // teardown cannot see. Deliberately never answered - the browser's own
    // deadline is what has to settle the request. Parked rather than dropped;
    // see the member's comment.
    parked_subscribe_callbacks_.push_back(std::move(callback));
    return;
  }

  auto result = mojom::SubscriptionResult::New();
  result->request_id = options->request_id;
  result->code = mojom::ObservationResultCode::kOk;
  result->subscription_id = subscription_id_;
  result->base_revision = subscription_revision_;
  std::move(callback).Run(std::move(result));
}

void ScriptedRendererEndpoint::PumpDeltas() {
  if (!delta_client_ || plan_.delta == DeltaBehaviour::kNone) {
    return;
  }

  CHECK(!tab_id_.empty())
      << "PumpDeltas before any snapshot request: this endpoint has not been "
         "told which tab it is in, so every delta it sent would be discarded "
         "by the browser's identity echo before reaching the behaviour under "
         "test. Drive an observation first, exactly as production does - the "
         "tab id reaches a renderer endpoint only on a snapshot request.";
  CHECK(!subscription_frame_id_.empty())
      << "PumpDeltas with no subscribe options recorded. The frame id a delta "
         "echoes is the one the browser named when it opened the stream.";

  const uint32_t count =
      plan_.delta == DeltaBehaviour::kFlood ? plan_.flood_delta_count : 1;
  for (uint32_t index = 0; index < count; ++index) {
    auto delta = mojom::PageDelta::New();
    delta->schema_version = kBipSchemaVersion;
    delta->subscription_id = subscription_id_;
    // The identity the browser bound, echoed back. A hostile endpoint gets
    // nothing from lying here - the broker checks the echo against its own
    // record and drops the message - so inventing an identity would only stop
    // the delta reaching the behaviour each DeltaBehaviour exists to attack.
    delta->tab_id = tab_id_;
    delta->frame_id = subscription_frame_id_;
    delta->page_epoch = plan_.delta == DeltaBehaviour::kForeignEpoch
                            ? plan_.foreign_page_epoch
                            : subscription_epoch_;
    delta->from_revision = plan_.delta == DeltaBehaviour::kRevisionMismatch
                               ? subscription_revision_ + 100
                               : subscription_revision_;
    delta->to_revision = delta->from_revision + 1;

    if (plan_.delta == DeltaBehaviour::kSequenceGap) {
      // Skip one. The subscriber's projection is dead, and only a fresh
      // snapshot is a legal way back.
      ++next_event_sequence_;
    } else if (plan_.delta == DeltaBehaviour::kDuplicateSequence &&
               next_event_sequence_ > 1) {
      --next_event_sequence_;
    }
    delta->event_sequence = next_event_sequence_++;
    delta->coalesced_mutation_count = 1;

    const std::string text = plan_.delta == DeltaBehaviour::kSecretInDelta
                                 ? plan_.secret_to_emit
                                 : "scripted delta text";
    delta->changed_nodes.push_back([&] {
      auto changed = mojom::ChangedNode::New();
      changed->node = ScriptedReplyFactory::Node("node-0", delta->frame_id, text);
      changed->changed_fields = {mojom::SemanticField::kTextRuns};
      return changed;
    }());
    delta->truncation = ScriptedReplyFactory::NoTruncation();
    delta->observed_at_monotonic_ms = 0;

    subscription_revision_ = delta->to_revision;
    delta_client_->OnDelta(std::move(delta));
  }
}

void ScriptedRendererEndpoint::ResolveNode(
    mojom::ResolveNodeRequestPtr request,
    ResolveNodeCallback callback) {
  auto result = mojom::ResolveNodeResult::New();
  result->request_id = request->request_id;

  // kResolvesThenNeverAnswers deliberately does not park here: it resolves
  // honestly so that the dispatch sequence gets past step 5 and the wedge
  // lands on ExecuteRendererAction instead.
  if (plan_.action == ActionBehaviour::kNeverAnswers) {
    parked_resolve_node_callbacks_.push_back(std::move(callback));
    return;
  }

  auto node = mojom::ResolvedNode::New();
  node->node_id = request->node_handle->node_id;
  node->observed_at_revision =
      plan_.action == ActionBehaviour::kInventsAdvancedRevision
          ? request->required_graph_revision + 1000
          : request->required_graph_revision;
  if (plan_.action == ActionBehaviour::kSwapsResolvedNode) {
    // A different role with a different action set. The browser authorized an
    // activate on a link; this claims a text field.
    node->role = mojom::SemanticRole::kTextField;
    node->actions = {mojom::ActionType::kFocus};
  } else {
    node->role = mojom::SemanticRole::kLink;
    node->actions = {mojom::ActionType::kActivate,
                     mojom::ActionType::kScrollIntoView};
  }
  node->states = {mojom::NodeState::kVisible, mojom::NodeState::kEnabled};
  node->sensitivity = mojom::Sensitivity::kNotSensitive;

  result->code = mojom::NodeResolutionCode::kOk;
  result->node = std::move(node);
  std::move(callback).Run(std::move(result));
}

void ScriptedRendererEndpoint::InspectMediaTarget(
    mojom::MediaTargetRequestPtr request,
    InspectMediaTargetCallback callback) {
  received_media_target_requests_.push_back(request.Clone());

  switch (plan_.media_target) {
    case MediaTargetBehaviour::kNeverAnswers:
      parked_media_target_callbacks_.push_back(std::move(callback));
      return;
    case MediaTargetBehaviour::kNodeGone:
    case MediaTargetBehaviour::kHonest:
      break;
  }

  auto result = mojom::MediaTargetResult::New();
  result->request_id = request->request_id;
  result->kind = request->expected_kind;
  result->observed_graph_revision = request->required_graph_revision;
  if (plan_.media_target == MediaTargetBehaviour::kNodeGone) {
    result->code = mojom::MediaTargetResultCode::kNodeGone;
    std::move(callback).Run(std::move(result));
    return;
  }

  result->bounds = mojom::Bounds::New();
  result->bounds->width = 1;
  result->bounds->height = 1;
  result->code = mojom::MediaTargetResultCode::kOk;
  std::move(callback).Run(std::move(result));
}

void ScriptedRendererEndpoint::ExecuteRendererAction(
    mojom::RendererActionCommandPtr command,
    ExecuteRendererActionCallback callback) {
  received_commands_.push_back(command.Clone());

  // Recorded before the park, so a test can prove the command reached the
  // endpoint and that it is therefore this call's deadline under test rather
  // than the resolve's.
  if (plan_.action == ActionBehaviour::kNeverAnswers ||
      plan_.action == ActionBehaviour::kResolvesThenNeverAnswers) {
    parked_action_callbacks_.push_back(std::move(callback));
    return;
  }

  auto result = mojom::RendererActionResult::New();
  result->schema_version = kBipSchemaVersion;
  result->command_id = command->command_id;
  result->outcome = mojom::RendererActionOutcome::kDispatched;
  result->observed_at_revision =
      plan_.action == ActionBehaviour::kInventsAdvancedRevision
          ? command->required_graph_revision + 1000
          : command->required_graph_revision;

  auto node = mojom::ResolvedNode::New();
  node->node_id = command->node_id;
  node->observed_at_revision = result->observed_at_revision;
  node->role = plan_.action == ActionBehaviour::kSwapsResolvedNode
                   ? mojom::SemanticRole::kTextField
                   : mojom::SemanticRole::kLink;
  node->actions = {mojom::ActionType::kActivate};
  node->states = {mojom::NodeState::kVisible, mojom::NodeState::kEnabled};
  node->sensitivity = mojom::Sensitivity::kNotSensitive;
  result->observed_state = std::move(node);

  std::move(callback).Run(std::move(result));
}

void ScriptedRendererEndpoint::Cancel(const std::string& command_id) {
  received_cancellations_.push_back(command_id);
}

}  // namespace taffy::test
