// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/services/core/rust_core_skill_match.h"

#include <stdint.h>

#include <optional>
#include <string>
#include <utility>

#include "taffy/contracts/core-service/generated/cpp/core_service_enums.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy::core_service_internal {

namespace mojom = core_service::mojom;
namespace bridge = core_bridge;
namespace wire = core_service::wire;

namespace {

bool IsBoundedNonEmpty(const std::string& value, uint64_t maximum) {
  return !value.empty() && value.size() <= maximum;
}

bridge::BridgeSiteSkillMatchOperation ToOperation(
    const mojom::OperationEnvelope& input) {
  bridge::BridgeSiteSkillMatchOperation output;
  output.operation_id = input.operation_id;
  output.service_generation = input.service_generation;
  output.task_revision = input.task_revision;
  output.deadline_monotonic_ms = input.deadline_monotonic_ms;
  output.idempotency_key = input.idempotency_key;
  return output;
}

bool IsBoundedCommand(const mojom::SiteSkillMatchCommand& input) {
  const mojom::OperationEnvelope& operation = *input.operation;
  const mojom::ObservationEffectResult& observation = *input.observation;
  return IsBoundedNonEmpty(operation.operation_id,
                           mojom::kMaxIdentifierBytes) &&
         IsBoundedNonEmpty(operation.idempotency_key,
                           mojom::kMaxIdempotencyKeyBytes) &&
         IsBoundedNonEmpty(input.expected_tab_id,
                           mojom::kMaxIdentifierBytes) &&
         IsBoundedNonEmpty(input.expected_frame_id,
                           mojom::kMaxIdentifierBytes) &&
         IsBoundedNonEmpty(input.expected_page_epoch,
                           mojom::kMaxIdentifierBytes) &&
         input.expected_graph_revision != 0u &&
         IsBoundedNonEmpty(input.expected_origin,
                           mojom::kMaxNormalizedOriginBytes) &&
         IsBoundedNonEmpty(observation.schema_version,
                           mojom::kMaxIdentifierBytes) &&
         IsBoundedNonEmpty(observation.tab_id, mojom::kMaxIdentifierBytes) &&
         IsBoundedNonEmpty(observation.frame_id,
                           mojom::kMaxIdentifierBytes) &&
         IsBoundedNonEmpty(observation.page_epoch,
                           mojom::kMaxIdentifierBytes) &&
         IsBoundedNonEmpty(observation.origin,
                           mojom::kMaxNormalizedOriginBytes) &&
         !observation.graph_payload.empty() &&
         observation.graph_payload.size() <=
             mojom::kMaxTaskObservationTotalBytes &&
         observation.total_bytes == observation.graph_payload.size() &&
         observation.node_count <= mojom::kMaxTaskObservationNodes;
}

bool IsSuccessfulShape(const bridge::BridgeSiteSkillMatchResult& input,
                       mojom::SiteSkillMatchStatus status) {
  if (status != mojom::SiteSkillMatchStatus::kAvailable) {
    return input.tab_id.empty() && input.frame_id.empty() &&
           input.page_epoch.empty() && input.graph_revision == 0u &&
           input.origin.empty() && input.offers.empty();
  }
  if (input.tab_id.empty() ||
      input.tab_id.size() > mojom::kMaxIdentifierBytes ||
      input.frame_id.empty() ||
      input.frame_id.size() > mojom::kMaxIdentifierBytes ||
      input.page_epoch.empty() ||
      input.page_epoch.size() > mojom::kMaxIdentifierBytes ||
      input.graph_revision == 0u ||
      input.origin.empty() ||
      input.origin.size() > mojom::kMaxNormalizedOriginBytes ||
      input.offers.size() > mojom::kMaxSkillsPerProfile) {
    return false;
  }
  std::string previous_skill_id;
  for (const bridge::BridgeSiteSkillMatchOffer& offer : input.offers) {
    const std::string skill_id(offer.skill_id);
    if (offer.skill_version_id.empty() ||
        offer.skill_version_id.size() > mojom::kMaxIdentifierBytes ||
        skill_id.empty() || skill_id.size() > mojom::kMaxSkillIdBytes ||
        offer.active_version == 0u ||
        offer.active_version > mojom::kMaxSkillVersionsPerSkill ||
        offer.step_count == 0u ||
        offer.step_count > mojom::kMaxSkillSteps ||
        (!previous_skill_id.empty() && previous_skill_id >= skill_id)) {
      return false;
    }
    previous_skill_id = skill_id;
  }
  return true;
}

}  // namespace

std::optional<bridge::BridgeSiteSkillMatchCommand>
ToBridgeSiteSkillMatchCommand(mojom::SiteSkillMatchCommandPtr input) {
  if (!input || !input->operation || !input->observation ||
      input->observation->media || !IsBoundedCommand(*input)) {
    return std::nullopt;
  }
  bridge::BridgeSiteSkillMatchCommand output;
  output.operation = ToOperation(*input->operation);
  output.expected_tab_id = std::move(input->expected_tab_id);
  output.expected_frame_id = std::move(input->expected_frame_id);
  output.expected_page_epoch = std::move(input->expected_page_epoch);
  output.expected_graph_revision = input->expected_graph_revision;
  output.expected_origin = std::move(input->expected_origin);
  mojom::ObservationEffectResult& observation = *input->observation;
  output.observation.status = static_cast<uint8_t>(observation.status);
  output.observation.schema_version = std::move(observation.schema_version);
  output.observation.tab_id = std::move(observation.tab_id);
  output.observation.frame_id = std::move(observation.frame_id);
  output.observation.page_epoch = std::move(observation.page_epoch);
  output.observation.graph_revision = observation.graph_revision;
  output.observation.origin = std::move(observation.origin);
  output.observation.is_potentially_trustworthy =
      observation.is_potentially_trustworthy;
  output.observation.private_profile = observation.private_profile;
  output.observation.node_count = observation.node_count;
  output.observation.total_bytes = observation.total_bytes;
  output.observation.truncated = observation.truncated;
  output.observation.may_change_answer = observation.may_change_answer;
  output.observation.redacted_field_count = observation.redacted_field_count;
  output.observation.suppressed_secret_value_count =
      observation.suppressed_secret_value_count;
  output.observation.sensitive_zone_count = observation.sensitive_zone_count;
  output.observation.policy_filtered_frame_count =
      observation.policy_filtered_frame_count;
  output.observation.highest_sensitivity =
      static_cast<uint8_t>(observation.highest_sensitivity);
  output.observation.graph_encoding =
      static_cast<uint8_t>(observation.graph_encoding);
  output.observation.graph_payload.reserve(observation.graph_payload.size());
  for (const uint8_t byte : observation.graph_payload) {
    output.observation.graph_payload.push_back(byte);
  }
  return output;
}

mojom::SiteSkillMatchResultPtr ToMojoSiteSkillMatchResult(
    bridge::BridgeSiteSkillMatchResult input) {
  const std::optional<mojom::SiteSkillMatchStatus> status =
      wire::SiteSkillMatchStatusFromWire(input.status);
  if (!status || !IsSuccessfulShape(input, *status)) {
    return nullptr;
  }
  auto output = mojom::SiteSkillMatchResult::New();
  output->operation = mojom::OperationEnvelope::New(
      std::string(input.operation.operation_id),
      input.operation.service_generation, input.operation.task_revision,
      input.operation.deadline_monotonic_ms,
      std::string(input.operation.idempotency_key));
  output->status = *status;
  output->tab_id = std::string(input.tab_id);
  output->frame_id = std::string(input.frame_id);
  output->page_epoch = std::string(input.page_epoch);
  output->graph_revision = input.graph_revision;
  output->origin = std::string(input.origin);
  output->offers.reserve(input.offers.size());
  for (const bridge::BridgeSiteSkillMatchOffer& offer : input.offers) {
    output->offers.push_back(mojom::SiteSkillMatchOffer::New(
        std::string(offer.skill_version_id), std::string(offer.skill_id),
        offer.active_version, offer.step_count));
  }
  return output;
}

}  // namespace taffy::core_service_internal
