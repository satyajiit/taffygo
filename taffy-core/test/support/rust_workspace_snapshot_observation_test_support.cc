// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "taffy/browser/core_effect_broker.h"
#include "taffy/browser/core_state_binding_registry.h"
#include "taffy/browser/core_task_effect.h"
#include "taffy/services/core/rust_core.h"
#include "taffy/test/support/rust_workspace_snapshot_compat_test_support.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy::test {
namespace {

namespace mojom = core_service::mojom;

// This private graph framing is the current BIP 0.13 canonical one-node
// fixture. Keeping the bytes here makes this test exercise the Rust decoder
// without linking the browser's production encoder into the core seam test.
constexpr std::string_view kObservationSchemaVersion = "0.13";

void PutU16(std::vector<uint8_t>* output, uint16_t value) {
  output->push_back(static_cast<uint8_t>(value));
  output->push_back(static_cast<uint8_t>(value >> 8));
}

void PutU32(std::vector<uint8_t>* output, uint32_t value) {
  for (size_t byte = 0u; byte < sizeof(value); ++byte) {
    output->push_back(static_cast<uint8_t>(value >> (byte * 8u)));
  }
}

void PutU64(std::vector<uint8_t>* output, uint64_t value) {
  for (size_t byte = 0u; byte < sizeof(value); ++byte) {
    output->push_back(static_cast<uint8_t>(value >> (byte * 8u)));
  }
}

void PutShort(std::vector<uint8_t>* output, std::string_view value) {
  PutU16(output, static_cast<uint16_t>(value.size()));
  output->insert(output->end(), value.begin(), value.end());
}

mojom::TaskEffectBindingPtr PublishAfterStorage(
    RustCore* core,
    CoreEffectBroker* broker,
    CoreResponseBatch batch,
    uint64_t* now_monotonic_ms,
    size_t* commit_count = nullptr) {
  // Every refusal below says what it wanted. A bare `return nullptr` here
  // reaches the caller as "no effect was published", which is the same answer
  // for a refused admission, an unexpected effect shape, a failed storage
  // dispatch and a state that carried no effect — four different defects that
  // used to be one unreadable null.
  //
  // The walk is at rest when a batch carries no commit, not when it carries
  // a state: a landed commit publishes the durable state while the next
  // commit is still chained behind it, so the last state seen is the one the
  // settled batch stands on.
  std::vector<CoreStatePublication> published;
  for (size_t index = 0u; index < 12u; ++index) {
    if (!batch.states.empty()) {
      published = std::move(batch.states);
    }
    if (batch.effects.empty()) {
      break;
    }
    EXPECT_TRUE(batch.admission) << "commit loop: batch was not judged";
    if (batch.admission) {
      EXPECT_EQ(mojom::AdmissionStatus::kAccepted, batch.admission->status);
    }
    EXPECT_EQ(1u, batch.effects.size()) << "commit loop: expected one commit";
    if (batch.effects.size() == 1u && batch.effects.front()) {
      EXPECT_EQ(mojom::EffectKind::kStorageCommit, batch.effects.front()->kind);
      EXPECT_TRUE(batch.effects.front()->storage_commit);
    }
    if (!batch.admission ||
        batch.admission->status != mojom::AdmissionStatus::kAccepted ||
        batch.effects.size() != 1u || !batch.effects.front() ||
        batch.effects.front()->kind != mojom::EffectKind::kStorageCommit ||
        !batch.effects.front()->storage_commit) {
      return nullptr;
    }
    mojom::EffectResultPtr terminal =
        DispatchWorkspaceSnapshotEffect(broker, batch.effects.front().Clone());
    EXPECT_TRUE(terminal) << "the storage commit produced no result";
    if (terminal) {
      EXPECT_EQ(mojom::EffectStatus::kCompleted, terminal->status);
    }
    if (!terminal || terminal->status != mojom::EffectStatus::kCompleted) {
      return nullptr;
    }
    if (commit_count) {
      ++*commit_count;
    }
    batch = core->DeliverEffectResult(std::move(terminal), ++*now_monotonic_ms);
  }
  EXPECT_TRUE(batch.admission) << "published batch was not judged";
  if (batch.admission) {
    EXPECT_EQ(mojom::AdmissionStatus::kAccepted, batch.admission->status);
  }
  EXPECT_TRUE(batch.effects.empty()) << "a settled batch still carries effects";
  if (!batch.states.empty()) {
    published = std::move(batch.states);
  }
  EXPECT_EQ(1u, published.size());
  if (published.size() == 1u) {
    EXPECT_EQ(1u, published.front().task_effects.size());
  }
  if (!batch.admission ||
      batch.admission->status != mojom::AdmissionStatus::kAccepted ||
      !batch.effects.empty() || published.size() != 1u ||
      published.front().task_effects.size() != 1u) {
    return nullptr;
  }
  return published.front().task_effects.front().Clone();
}

std::vector<uint8_t> FirstGraphPayload(const std::string& frame_id) {
  std::vector<uint8_t> output = {4u};  // framing version
  PutShort(&output, kObservationSchemaVersion);
  PutU32(&output, 1u);  // one node
  PutShort(&output, "node-1");
  PutShort(&output, frame_id);
  PutU16(&output, 0u);   // DOCUMENT
  output.push_back(0u);  // NOT_SENSITIVE
  output.push_back(0u);  // no node flags
  output.push_back(9u);  // UNKNOWN value kind
  PutShort(&output, "Public heading");
  PutU32(&output, 2u);   // declared text runs
  PutU64(&output, 14u);  // declared text bytes
  PutU32(&output, 1u);   // one available action
  PutU16(&output, 1u);   // FOCUS
  PutU32(&output, 1u);   // one node state
  output.push_back(0u);  // VISIBLE
  output.push_back(0u);  // no destination
  output.push_back(2u);  // FIRST_PARTY_DOCUMENT
  PutU32(&output, 0u);   // no node signals
  PutU32(&output, 2u);   // two emitted text runs
  for (std::string_view text : {"Public ", "heading"}) {
    PutShort(&output, text);
    output.push_back(0u);  // RENDERED_TEXT
    output.push_back(0u);  // NOT_SENSITIVE
    output.push_back(0u);  // not truncated
    output.push_back(2u);  // FIRST_PARTY_DOCUMENT
    PutU32(&output, 0u);   // no run signals
  }
  PutU32(&output, 1u);  // one edge
  PutShort(&output, "node-1");
  PutShort(&output, "node-1");
  PutU16(&output, 0u);   // CONTAINS
  output.push_back(0u);  // not inferred
  return output;
}

mojom::TaskEffectCompletionPtr ObservationCompletion(
    const mojom::TaskEffectBinding& effect,
    uint64_t graph_revision) {
  if (!effect.operation || !effect.action || !effect.action->document ||
      !effect.action->executable || !effect.action->observation) {
    return nullptr;
  }
  std::vector<uint8_t> payload =
      FirstGraphPayload(effect.action->document->frame_id);
  if (payload.empty() ||
      payload.size() > std::numeric_limits<uint32_t>::max()) {
    return nullptr;
  }
  auto result = mojom::EffectResult::New();
  result->operation = effect.operation.Clone();
  result->effect_id = effect.effect_id;
  result->status = mojom::EffectStatus::kCompleted;
  result->kind = mojom::EffectKind::kPageObservation;
  result->observation = mojom::ObservationEffectResult::New();
  mojom::ObservationEffectResult& observation = *result->observation;
  observation.status = mojom::BipObservationStatus::kOk;
  observation.schema_version = kObservationSchemaVersion;
  observation.tab_id = effect.action->executable->tab_id;
  observation.frame_id = effect.action->document->frame_id;
  observation.page_epoch = effect.action->document->page_epoch;
  observation.graph_revision = graph_revision;
  observation.origin = effect.action->document->normalized_origin;
  observation.node_count = 1u;
  observation.total_bytes = static_cast<uint32_t>(payload.size());
  observation.highest_sensitivity = mojom::BipSensitivity::kNotSensitive;
  observation.graph_encoding = mojom::BipGraphEncoding::kBipContract;
  observation.graph_payload = std::move(payload);
  mojom::TaskEffectCompletionPtr completion = MakeTaskEffectCompletion(
      &effect, mojom::TaskEffectCompletionStatus::kSucceeded);
  completion->effect_result = std::move(result);
  return completion;
}

mojom::TaskEffectBindingPtr GrantAndPublishAction(
    RustCore* core,
    CoreEffectBroker* broker,
    mojom::TaskEffectBindingPtr ask_policy,
    uint64_t graph_revision,
    uint64_t* now_monotonic_ms,
    mojom::PolicyEvaluationStatus* policy_status = nullptr) {
  if (!ask_policy ||
      ask_policy->kind != mojom::TaskReducerEffectKind::kAskPolicy ||
      !ask_policy->policy) {
    return nullptr;
  }
  mojom::PolicyEvaluationResultPtr policy = EvaluateWorkspaceSnapshotReadPolicy(
      core, *ask_policy->policy, *now_monotonic_ms, graph_revision);
  if (!policy) {
    return nullptr;
  }
  if (policy_status) {
    *policy_status = policy->status;
  }
  if (policy->status != mojom::PolicyEvaluationStatus::kGranted ||
      !policy->minted_grant || !policy->minted_grant->scope ||
      policy->minted_grant->scope->required_graph_revision != graph_revision) {
    return nullptr;
  }
  CoreResponseBatch completed = core->CompleteTaskPolicy(
      std::move(ask_policy), std::move(policy), ++*now_monotonic_ms);
  return PublishAfterStorage(core, broker, std::move(completed),
                             now_monotonic_ms);
}

mojom::AdmissionStatus ExerciseBelowFloor(RustCore* core,
                                          CoreEffectBroker* broker,
                                          uint64_t* now_monotonic_ms) {
  mojom::CoreServiceCommandPtr start = MakeWorkspaceSnapshotStartTask();
  start->operation->operation_id = "open-task-revision-floor";
  start->operation->idempotency_key = "open-task-revision-floor";
  start->start_task->task_id = "task-revision-floor";
  start->start_task->trace_id = "trace-revision-floor";
  start->start_task->initial_consent_receipt_id = "consent-revision-floor";
  start->start_task->task_id_seed.front() ^= 0xffu;
  mojom::TaskEffectBindingPtr ask = PublishAfterStorage(
      core, broker, core->Submit(std::move(start), ++*now_monotonic_ms),
      now_monotonic_ms);
  // Every bail-out below says what it wanted before it gives up. They all
  // answer kCoreUnavailable, which is also this helper's "the core refused the
  // final completion" answer, so without these the caller cannot tell six
  // different setup failures apart from the one result it is asking about.
  EXPECT_TRUE(ask && ask->policy) << "the second task did not ask for policy";
  if (!ask || !ask->policy) {
    return mojom::AdmissionStatus::kCoreUnavailable;
  }
  mojom::PolicyEvaluationResultPtr first = EvaluateWorkspaceSnapshotReadPolicy(
      core, *ask->policy, *now_monotonic_ms, 2u);
  EXPECT_TRUE(first) << "no policy evaluation result";
  if (first) {
    EXPECT_EQ(mojom::PolicyEvaluationStatus::kApprovalRequired, first->status);
  }
  if (!first ||
      first->status != mojom::PolicyEvaluationStatus::kApprovalRequired) {
    return mojom::AdmissionStatus::kCoreUnavailable;
  }
  mojom::TaskEffectBindingPtr request = PublishAfterStorage(
      core, broker,
      core->CompleteTaskPolicy(std::move(ask), std::move(first),
                               ++*now_monotonic_ms),
      now_monotonic_ms);
  EXPECT_TRUE(request) << "no effect published after the policy completion";
  if (request) {
    EXPECT_EQ(mojom::TaskReducerEffectKind::kRequestApproval, request->kind);
    EXPECT_TRUE(request->operation);
    EXPECT_TRUE(request->approval);
  }
  if (!request ||
      request->kind != mojom::TaskReducerEffectKind::kRequestApproval ||
      !request->operation || !request->approval) {
    return mojom::AdmissionStatus::kCoreUnavailable;
  }
  CoreResponseBatch surface = core->CompleteTaskEffect(
      request.Clone(),
      MakeTaskEffectCompletion(request.get(),
                               mojom::TaskEffectCompletionStatus::kSucceeded),
      ++*now_monotonic_ms);
  EXPECT_TRUE(surface.admission) << "approval surface completion not admitted";
  if (surface.admission) {
    EXPECT_EQ(mojom::AdmissionStatus::kAccepted, surface.admission->status);
  }
  EXPECT_TRUE(surface.effects.empty());
  // Completing the approval-surface effect moves the task to waiting-on-a-
  // person, which every surface reads, so exactly one state comes with it.
  // Releasing an already-terminal task's tabs is the case that publishes none.
  EXPECT_EQ(1u, surface.states.size());
  if (!surface.admission ||
      surface.admission->status != mojom::AdmissionStatus::kAccepted ||
      !surface.effects.empty() || surface.states.size() != 1u) {
    return mojom::AdmissionStatus::kCoreUnavailable;
  }
  PendingApprovalLookup pending{
      .service_generation = request->operation->service_generation,
      .task_revision = request->operation->task_revision,
      .proposal_digest = request->approval->proposal_digest,
  };
  mojom::CoreServiceCommandPtr approve =
      MakeWorkspaceSnapshotApproval(pending, request->approval->action_id);
  approve->operation->operation_id = "approve-revision-floor";
  approve->operation->idempotency_key = "approve-revision-floor";
  approve->user_decision->task_id = "task-revision-floor";
  approve->user_decision->trace_id = "trace-approve-revision-floor";
  approve->user_decision->approval_receipt_id = "receipt-revision-floor";
  mojom::TaskEffectBindingPtr approved = PublishAfterStorage(
      core, broker, core->Submit(std::move(approve), ++*now_monotonic_ms),
      now_monotonic_ms);
  // Checked before the move, not after: a moved-from pointer is always null,
  // so an assertion placed below reports a failure that is its own doing.
  EXPECT_TRUE(approved) << "the approval published no effect";
  mojom::TaskEffectBindingPtr dispatch = GrantAndPublishAction(
      core, broker, std::move(approved), 2u, now_monotonic_ms);
  EXPECT_TRUE(dispatch) << "no action was granted after the approval";
  if (dispatch) {
    EXPECT_EQ(mojom::TaskReducerEffectKind::kDispatchAction, dispatch->kind);
    EXPECT_TRUE(dispatch->action && dispatch->action->document);
    if (dispatch->action && dispatch->action->document) {
      EXPECT_EQ(2u, dispatch->action->document->graph_revision);
    }
  }
  if (!dispatch ||
      dispatch->kind != mojom::TaskReducerEffectKind::kDispatchAction ||
      !dispatch->action || !dispatch->action->document ||
      dispatch->action->document->graph_revision != 2u) {
    return mojom::AdmissionStatus::kCoreUnavailable;
  }
  CoreResponseBatch refused = core->CompleteTaskEffect(
      dispatch.Clone(), ObservationCompletion(*dispatch, 1u),
      ++*now_monotonic_ms);
  EXPECT_TRUE(refused.admission) << "the below-floor completion was not judged";
  return refused.admission ? refused.admission->status
                           : mojom::AdmissionStatus::kCoreUnavailable;
}

}  // namespace

WorkspaceSnapshotObservationResult ExerciseWorkspaceSnapshotObservation(
    RustCore* core,
    CoreEffectBroker* broker,
    mojom::TaskEffectBindingPtr approved_policy_effect,
    uint64_t now_monotonic_ms) {
  WorkspaceSnapshotObservationResult output;
  mojom::TaskEffectBindingPtr dispatch =
      GrantAndPublishAction(core, broker, std::move(approved_policy_effect), 0u,
                            &now_monotonic_ms, &output.policy_status);
  if (!dispatch || !dispatch->action || !dispatch->action->document) {
    return output;
  }
  output.action_kind = dispatch->kind;
  output.grant_graph_revision = dispatch->action->document->graph_revision;
  output.observation_graph_revision = 1u;
  CoreResponseBatch observed = core->CompleteTaskEffect(
      dispatch.Clone(), ObservationCompletion(*dispatch, 1u),
      ++now_monotonic_ms);
  if (!observed.admission) {
    return output;
  }
  output.observation_status = observed.admission->status;
  mojom::TaskEffectBindingPtr terminal =
      PublishAfterStorage(core, broker, std::move(observed), &now_monotonic_ms,
                          &output.post_observation_commit_count);
  if (!terminal || !terminal->operation) {
    return output;
  }
  output.terminal_effect_kind = terminal->kind;
  output.terminal_task_revision = terminal->operation->task_revision;
  CoreResponseBatch released = core->CompleteTaskEffect(
      terminal.Clone(),
      MakeTaskEffectCompletion(terminal.get(),
                               mojom::TaskEffectCompletionStatus::kSucceeded),
      ++now_monotonic_ms);
  // Asserted before the guard, not folded into it. Returning here silently
  // leaves below_floor_status at its initializer, which reads as "the core was
  // unavailable" and names none of the four reasons the release batch can be
  // wrong; that is exactly how this step's own failure stayed unreadable.
  EXPECT_TRUE(released.admission);
  if (!released.admission) {
    return output;
  }
  EXPECT_EQ(mojom::AdmissionStatus::kAccepted, released.admission->status);
  EXPECT_TRUE(released.effects.empty());
  // No state here, unlike the approval-surface completion earlier in this
  // sequence: releasing the tabs of an already-terminal task changes nothing a
  // surface reads, so there is nothing to publish.
  EXPECT_TRUE(released.states.empty());
  if (released.admission->status != mojom::AdmissionStatus::kAccepted ||
      !released.effects.empty() || !released.states.empty()) {
    return output;
  }
  output.below_floor_status =
      ExerciseBelowFloor(core, broker, &now_monotonic_ms);
  return output;
}

}  // namespace taffy::test
