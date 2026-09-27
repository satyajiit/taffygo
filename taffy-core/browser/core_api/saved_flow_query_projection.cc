// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "taffy/browser/core_api/saved_flow_query_projection.h"

#include <utility>

#include "base/containers/flat_set.h"
#include "taffy/contracts/core-api/generated/cpp/core_api_enums.h"
namespace taffy {
namespace {
namespace api = core_api::mojom;
namespace wire = core_api::wire;
namespace service = core_service::mojom;
bool Bounded(const std::string& value, size_t maximum) {
  return !value.empty() && value.size() <= maximum;
}
api::SiteSkillViewPtr Review(service::SavedFlowReviewPtr flow) {
  if (!flow) {
    return nullptr;
  }
  auto provenance = wire::SiteSkillProvenanceViewFromWire(
      static_cast<uint32_t>(flow->provenance));
  auto status =
      wire::SiteSkillStatusViewFromWire(static_cast<uint32_t>(flow->status));
  if (!provenance || !status ||
      !Bounded(flow->skill_id, api::kMaxSkillIdBytes) ||
      !Bounded(flow->origin, api::kMaxSkillOriginBytes) ||
      flow->active_version == 0u ||
      (flow->recorded_from_task_id &&
       !Bounded(*flow->recorded_from_task_id, api::kMaxIdentifierBytes)) ||
      flow->reviewed_steps.size() != flow->step_count ||
      flow->reviewed_steps.empty() || flow->step_count > api::kMaxSkillSteps) {
    return nullptr;
  }
  auto out = api::SiteSkillView::New();
  out->skill_id = flow->skill_id;
  out->origin = flow->origin;
  out->provenance = *provenance;
  out->status = *status;
  out->active_version = flow->active_version;
  out->step_count = flow->step_count;
  out->installed_at_epoch_ms = flow->installed_at_epoch_ms;
  out->updated_at_epoch_ms = flow->updated_at_epoch_ms;
  out->recorded_from_task_id = flow->recorded_from_task_id;
  for (const auto& step : flow->reviewed_steps) {
    if (!step || !Bounded(step->verb, api::kMaxSkillToolNameBytes) ||
        step->arguments.size() > api::kMaxSkillArgumentsPerStep) {
      return nullptr;
    }
    auto target_step = api::SiteSkillObservedStep::New();
    target_step->verb = step->verb;
    target_step->postcondition = step->postcondition;
    target_step->has_fill = step->has_fill;
    target_step->fill_purpose = step->fill_purpose;
    for (const auto& arg : step->arguments) {
      if (!arg) {
        return nullptr;
      }
      auto kind =
          wire::SiteSkillArgumentKindFromWire(static_cast<uint32_t>(arg->kind));
      if (!kind ||
          (*kind == api::SiteSkillArgumentKind::kPublicAddress) !=
              arg->public_address.has_value() ||
          (*kind == api::SiteSkillArgumentKind::kSemanticTarget) !=
              static_cast<bool>(arg->semantic_target)) {
        return nullptr;
      }
      auto target = api::SiteSkillObservedArgument::New();
      target->parameter = arg->parameter;
      target->kind = *kind;
      target->value = arg->value;
      target->purpose = arg->purpose;
      target->public_address = arg->public_address;
      if (arg->semantic_target) {
        target->semantic_target = api::SiteSkillSemanticTarget::New(
            arg->semantic_target->role, arg->semantic_target->phrase);
      }
      target_step->arguments.push_back(std::move(target));
    }
    out->reviewed_steps.push_back(std::move(target_step));
  }
  return out;
}
}  // namespace
api::SavedFlowQueryResultPtr ProjectSavedFlowQuery(
    const std::string& request_id,
    uint64_t generation,
    service::SavedFlowQueryResultPtr result) {
  auto out = api::SavedFlowQueryResult::New();
  out->request_id = request_id;
  out->service_generation = generation;
  out->availability = api::SavedFlowQueryAvailability::kUnavailable;
  if (!result || !result->operation ||
      result->operation->service_generation != generation) {
    return out;
  }
  auto status = wire::SavedFlowQueryAvailabilityFromWire(
      static_cast<uint32_t>(result->status));
  if (!status || result->flows.size() > api::kMaxSavedFlowQueryResults ||
      (*status != api::SavedFlowQueryAvailability::kAvailable &&
       !result->flows.empty())) {
    out->availability = api::SavedFlowQueryAvailability::kInvalidRequest;
    return out;
  }
  out->availability = *status;
  base::flat_set<std::string> identities;
  for (auto& flow : result->flows) {
    auto projected = Review(std::move(flow));
    if (!projected || !identities.insert(projected->skill_id).second) {
      out->flows.clear();
      out->availability = api::SavedFlowQueryAvailability::kInvalidRequest;
      return out;
    }
    out->flows.push_back(std::move(projected));
  }
  return out;
}
}  // namespace taffy
