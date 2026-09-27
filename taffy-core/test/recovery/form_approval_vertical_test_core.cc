// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <array>
#include <set>
#include <string_view>
#include <utility>

#include "base/containers/span.h"
#include "base/time/time.h"
#include "taffy/browser/core_service_manager_task_effect_test_peer.h"
#include "taffy/contracts/bip/mojom/page_intelligence.mojom.h"
#include "taffy/test/recovery/form_approval_vertical_test_internal.h"
#include "taffy/test/support/bip_graph_payload_reader.h"

namespace taffy::test {
namespace {

namespace bip = ::taffy::mojom;

void AddField(std::vector<uint8_t>* output,
              uint8_t tag,
              base::span<const uint8_t> value) {
  output->push_back(tag);
  for (size_t index = 0; index < sizeof(uint64_t); ++index) {
    output->push_back(static_cast<uint8_t>(value.size() >> (index * 8u)));
  }
  output->insert(output->end(), value.begin(), value.end());
}

service::PolicyRiskClass RiskFor(service::PolicyActionClass action_class) {
  return action_class == service::PolicyActionClass::kFillField
             ? service::PolicyRiskClass::kSensitiveDisclosure
             : service::PolicyRiskClass::kLocalRead;
}

service::MintedCapabilityGrantPtr GrantFor(
    const service::PolicyEvaluationRequest& request,
    uint64_t sequence) {
  auto grant = service::MintedCapabilityGrant::New();
  grant->capability_id = "form-vertical-capability-" + std::to_string(sequence);
  grant->service_generation = request.operation->service_generation;
  grant->policy_version = request.policy_version;
  grant->actor_lease_id = request.actor_lease->lease_id;
  grant->task_id = request.task_id;
  grant->action_id = request.action_id;
  grant->action_class = request.action_class;
  grant->principal = request.principal.Clone();
  grant->proposal_digest = request.proposal_digest;
  grant->idempotency_key = request.operation->idempotency_key;
  grant->scope = request.scope.Clone();
  grant->data_classes = request.data_classes;
  grant->effective_risk = RiskFor(request.action_class);
  grant->approval = request.approval.Clone();
  grant->issued_at_monotonic_ms = request.now_monotonic_ms;
  grant->expires_at_monotonic_ms = request.expires_at_monotonic_ms;
  grant->authority_subject = request.authority_subject.Clone();
  grant->operation_kind = request.operation_kind;
  grant->canonical_intent_digest = request.canonical_intent_digest;
  grant->discovery = request.discovery.Clone();
  return grant;
}

service::EffectEnvelopePtr DirectObservationEffect(
    const service::PolicyEvaluationRequest& request,
    const service::MintedCapabilityGrant& grant) {
  auto effect = service::EffectEnvelope::New();
  effect->operation = request.operation.Clone();
  effect->effect_id = request.operation->operation_id;
  effect->kind = service::EffectKind::kPageObservation;
  effect->retry_class = service::RetryClass::kIdempotent;
  auto body = service::PageObservationEffect::New();
  body->tab_id = request.scope->tab_id;
  body->frame_id = request.scope->frame_id;
  body->page_epoch = request.scope->page_epoch;
  body->scope = service::ObservationScope::kCurrentDocument;
  body->max_bytes = service::kMaxDirectObservationTotalBytes;
  body->capability_id = grant.capability_id;
  body->proposal_digest = request.proposal_digest;
  body->idempotency_key = request.operation->idempotency_key;
  body->authority_subject = request.authority_subject.Clone();
  body->max_nodes = service::kMaxDirectObservationNodes;
  body->max_text_bytes = service::kMaxDirectObservationTextBytes;
  body->max_frames = service::kMaxDirectObservationFrames;
  body->deadline_ms = service::kMaxDirectObservationDeadlineMs;
  body->expected_graph_revision = request.scope->required_graph_revision;
  effect->page_observation = std::move(body);
  return effect;
}

std::optional<LiveFormApprovalTarget> FindTarget(
    const service::ObservationEffectResult& observation) {
  const std::optional<GraphPayload> parsed =
      ReadGraphPayload(observation.graph_payload);
  if (!parsed) {
    return std::nullopt;
  }
  const uint16_t set_text = static_cast<uint16_t>(bip::ActionType::kSetText);
  const uint16_t owns = static_cast<uint16_t>(bip::RelationshipKind::kOwns);
  const uint16_t same =
      static_cast<uint16_t>(bip::RelationshipKind::kSameEntityAs);
  std::set<std::string> owned;
  for (const auto& edge : parsed->edges) {
    if (edge.relationship == owns) {
      owned.insert(edge.to_node_id);
    }
  }
  const auto node = [&](std::string_view id) -> const GraphPayloadNode* {
    const auto found = std::find_if(parsed->nodes.begin(), parsed->nodes.end(),
                                    [id](const GraphPayloadNode& candidate) {
                                      return candidate.node_id == id;
                                    });
    return found == parsed->nodes.end() ? nullptr : &*found;
  };
  const auto named = [](const GraphPayloadNode* candidate) {
    return candidate && candidate->name.find("Search this fixture site") !=
                            std::string::npos;
  };
  const auto writable = [set_text](const GraphPayloadNode* candidate) {
    return candidate &&
           std::find(candidate->actions.begin(), candidate->actions.end(),
                     set_text) != candidate->actions.end();
  };
  std::set<std::string> fields;
  for (const auto& candidate : parsed->nodes) {
    if (named(&candidate) && writable(&candidate) &&
        owned.contains(candidate.node_id)) {
      fields.insert(candidate.node_id);
    }
  }
  for (const auto& edge : parsed->edges) {
    if (edge.relationship != same) {
      continue;
    }
    const GraphPayloadNode* from = node(edge.from_node_id);
    const GraphPayloadNode* to = node(edge.to_node_id);
    if (named(from) && writable(to) && owned.contains(to->node_id)) {
      fields.insert(to->node_id);
    }
    if (named(to) && writable(from) && owned.contains(from->node_id)) {
      fields.insert(from->node_id);
    }
  }
  if (fields.size() != 1u) {
    return std::nullopt;
  }
  std::set<std::string> related = fields;
  std::vector<std::string> pending(fields.begin(), fields.end());
  for (size_t index = 0; index < pending.size(); ++index) {
    for (const auto& edge : parsed->edges) {
      if (edge.relationship != same) {
        continue;
      }
      const std::string* other = nullptr;
      if (edge.from_node_id == pending[index]) {
        other = &edge.to_node_id;
      } else if (edge.to_node_id == pending[index]) {
        other = &edge.from_node_id;
      }
      if (other && related.insert(*other).second) {
        pending.push_back(*other);
      }
    }
  }
  std::set<std::string> forms;
  for (const auto& edge : parsed->edges) {
    if (edge.relationship == owns && related.contains(edge.to_node_id)) {
      forms.insert(edge.from_node_id);
    }
  }
  if (forms.size() != 1u) {
    return std::nullopt;
  }
  return LiveFormApprovalTarget{
      .tab_id = observation.tab_id,
      .form_node_id = *forms.begin(),
      .field_node_id = *fields.begin(),
      .frame_id = observation.frame_id,
      .page_epoch = observation.page_epoch,
      .normalized_origin = observation.origin,
      .graph_revision = observation.graph_revision,
  };
}

}  // namespace

uint64_t FormTestMonotonicMillis() {
  return static_cast<uint64_t>(std::max<int64_t>(
      0, base::TimeTicks::Now().since_origin().InMilliseconds()));
}

uint64_t FormTestUtcMillis() {
  return static_cast<uint64_t>(
      std::max<int64_t>(0, base::Time::Now().InMillisecondsSinceUnixEpoch()));
}

RecordingFieldValueClient::RecordingFieldValueClient() = default;
RecordingFieldValueClient::~RecordingFieldValueClient() = default;

mojo::PendingRemote<surface::TaffyFieldValueClient>
RecordingFieldValueClient::Bind() {
  return receiver_.BindNewPipeAndPassRemote();
}

void RecordingFieldValueClient::Open(surface::FieldValueRequestPtr request) {
  request_ = std::move(request);
}

void RecordingFieldValueClient::Close(const std::string& request_id,
                                      surface::FieldValueCloseReason reason) {
  static_cast<void>(reason);
  if (request_ && request_->request_id == request_id) {
    request_.reset();
  }
}

ScriptedFormCoreSession::ScriptedFormCoreSession(CoreServiceManager* manager)
    : manager_(manager) {}
ScriptedFormCoreSession::~ScriptedFormCoreSession() = default;

void ScriptedFormCoreSession::Bind(
    mojo::PendingReceiver<service::CoreSession> receiver) {
  receiver_.Bind(std::move(receiver));
}

void ScriptedFormCoreSession::Submit(service::CoreServiceCommandPtr command,
                                     SubmitCallback callback) {
  const std::string operation_id =
      command && command->operation ? command->operation->operation_id : "";
  if (command) {
    commands_.push_back(command.Clone());
  }
  std::move(callback).Run(service::Admission::New(
      operation_id, command ? service::AdmissionStatus::kAccepted
                            : service::AdmissionStatus::kInvalidCommand));
}

void ScriptedFormCoreSession::EvaluatePolicy(
    service::PolicyEvaluationRequestPtr request,
    EvaluatePolicyCallback callback) {
  auto result = service::PolicyEvaluationResult::New();
  result->operation_id =
      request && request->operation ? request->operation->operation_id : "";
  if (!request || !request->operation || !request->actor_lease ||
      !request->scope || !request->authority_subject || !request->principal) {
    result->status = service::PolicyEvaluationStatus::kInvalidRequest;
    std::move(callback).Run(std::move(result));
    return;
  }
  auto grant = GrantFor(*request, next_capability_++);
  const auto registration =
      CoreServiceManagerTaskEffectTestPeer::RegisterCapability(*manager_,
                                                               grant.Clone());
  if (registration != service::CapabilityRegistrationStatus::kRegistered) {
    result->status = service::PolicyEvaluationStatus::kInvalidRequest;
    std::move(callback).Run(std::move(result));
    return;
  }
  result->status = service::PolicyEvaluationStatus::kGranted;
  if (request->authority_subject->kind ==
      service::AuthoritySubjectKind::kDirectUserIntent) {
    result->direct_observation_effect =
        DirectObservationEffect(*request, *grant);
  }
  result->minted_grant = std::move(grant);
  std::move(callback).Run(std::move(result));
}

void ScriptedFormCoreSession::MatchSiteSkills(
    service::SiteSkillMatchCommandPtr command,
    MatchSiteSkillsCallback callback) {
  if (command && command->observation) {
    target_ = FindTarget(*command->observation);
  }
  auto result = service::SiteSkillMatchResult::New();
  result->operation = command && command->operation
                          ? command->operation.Clone()
                          : service::OperationEnvelope::New();
  result->status = service::SiteSkillMatchStatus::kUnavailable;
  std::move(callback).Run(std::move(result));
}

void ScriptedFormCoreSession::QuerySavedFlows(
    service::SavedFlowQueryCommandPtr command,
    QuerySavedFlowsCallback callback) {
  auto result = service::SavedFlowQueryResult::New();
  result->operation = command && command->operation
                          ? command->operation.Clone()
                          : service::OperationEnvelope::New();
  result->status = service::SavedFlowQueryStatus::kUnavailable;
  std::move(callback).Run(std::move(result));
}

void ScriptedFormCoreSession::ValidateAccountTokenResponse(
    service::AccountTokenValidationRequestPtr request,
    ValidateAccountTokenResponseCallback callback) {
  static_cast<void>(request);
  std::move(callback).Run(nullptr);
}

void ScriptedFormCoreSession::ExportPageSnapshot(
    service::PageSnapshotExportCommandPtr command,
    ExportPageSnapshotCallback callback) {
  static_cast<void>(command);
  std::move(callback).Run(nullptr);
}

void ScriptedFormCoreSession::CancelPageSnapshotExport(
    service::OperationEnvelopePtr operation,
    CancelPageSnapshotExportCallback callback) {
  static_cast<void>(operation);
  std::move(callback).Run(false);
}

void ScriptedFormCoreSession::CompleteTaskSettlement(
    service::TaskSettlementBindingPtr settlement) {
  static_cast<void>(settlement);
}
void ScriptedFormCoreSession::Cancel(service::OperationEnvelopePtr operation) {
  static_cast<void>(operation);
}
void ScriptedFormCoreSession::DeliverToolStreamChunk(
    service::ToolStreamChunkPtr chunk) {
  static_cast<void>(chunk);
}
void ScriptedFormCoreSession::DeliverEffectResult(
    service::EffectResultPtr result) {
  static_cast<void>(result);
}

void ScriptedFormCoreSession::DeliverModelStreamChunk(
    service::ModelStreamChunkPtr chunk,
    DeliverModelStreamChunkCallback callback) {
  static_cast<void>(chunk);
  std::move(callback).Run(service::ModelStreamChunkStatus::kUnavailable);
}

void ScriptedFormCoreSession::PlanEntitlementRefresh(
    service::EntitlementFetchReason reason,
    PlanEntitlementRefreshCallback callback) {
  static_cast<void>(reason);
  std::move(callback).Run(nullptr);
}

void ScriptedFormCoreSession::DeliverEntitlementFetchResult(
    service::EffectResultPtr result,
    DeliverEntitlementFetchResultCallback callback) {
  static_cast<void>(result);
  std::move(callback).Run(false);
}

std::vector<uint8_t> CanonicalFormFillIntent(
    const LiveFormApprovalTarget& target,
    const std::string& request_id,
    uint32_t supplied_value_index) {
  constexpr std::string_view prefix = "taffy.action-intent.v1";
  std::vector<uint8_t> intent(prefix.begin(), prefix.end());
  const std::array<uint8_t, 1> operation = {13u};
  AddField(&intent, 0u, operation);
  AddField(&intent, 1u, base::as_byte_span(std::string_view(target.tab_id)));
  AddField(&intent, 2u,
           base::as_byte_span(std::string_view(target.field_node_id)));
  AddField(&intent, 3u, base::as_byte_span(std::string_view(request_id)));
  std::array<uint8_t, sizeof(supplied_value_index)> position = {};
  for (size_t index = 0; index < position.size(); ++index) {
    position[index] =
        static_cast<uint8_t>(supplied_value_index >> (index * 8u));
  }
  AddField(&intent, 4u, position);
  return intent;
}

service::TaskExecutableActionPtr MakeFillExecutable(
    const LiveFormApprovalTarget& target,
    const std::string& request_id,
    uint32_t supplied_value_index) {
  auto executable = service::TaskExecutableAction::New();
  executable->action_class = service::PolicyActionClass::kFillField;
  executable->tool_name = "browser.form.fill";
  executable->tab_id = target.tab_id;
  executable->node_id = target.field_node_id;
  executable->operation_kind = service::TaskActionOperationKind::kFormFill;
  executable->canonical_intent =
      CanonicalFormFillIntent(target, request_id, supplied_value_index);
  executable->input = service::TaskActionInput::New();
  executable->input->kind = service::TaskActionInputKind::kSuppliedValue;
  executable->input->supplied_value =
      service::TaskSuppliedValuePosition::New(supplied_value_index, request_id);
  return executable;
}

service::CoreStateUpdatePtr MakeFormState(uint64_t generation,
                                          uint64_t sequence) {
  auto state = service::CoreStateUpdate::New();
  state->service_generation = generation;
  state->sequence = sequence;
  state->core_status_schema_version = 1u;
  state->payload = {1u};
  return state;
}

}  // namespace taffy::test
