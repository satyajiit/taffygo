// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <string>
#include <utility>
#include <vector>

#include "base/numerics/clamped_math.h"
#include "taffy/browser/core_page_observation_broker.h"
#include "taffy/browser/profile_page_media_store.h"
#include "taffy/browser/taffy_page_intelligence_host.h"

namespace taffy {

namespace core_mojom = core_service::mojom;

void CorePageObservationBroker::CancelTask(std::string_view task_id,
                                           uint64_t generation) {
  if (task_id.empty() || generation == 0u) {
    return;
  }
  std::vector<PendingObservation> cancellations;
  for (const auto& [effect_id, pending] : pending_observations_) {
    static_cast<void>(effect_id);
    if (pending.task_id == task_id && pending.generation == generation &&
        pending.request_id.is_valid()) {
      cancellations.push_back(pending);
    }
  }
  for (const PendingObservation& pending : cancellations) {
    CancelTabObservationForCore(browser_context_, pending.tab_id,
                                pending.request_id);
  }
  page_media_store_->RevokeTask(task_id, generation);
  std::vector<std::string> media_cancellations;
  for (const auto& [effect_id, pending] : pending_media_observations_) {
    if (pending.task_id == task_id && pending.generation == generation) {
      media_cancellations.push_back(effect_id);
    }
  }
  for (const std::string& effect_id : media_cancellations) {
    auto pending = pending_media_observations_.find(effect_id);
    if (pending != pending_media_observations_.end() &&
        pending->second.cancel) {
      base::OnceClosure cancel = std::move(pending->second.cancel);
      std::move(cancel).Run();
    }
  }
}

void CorePageObservationBroker::CancelEffect(std::string_view effect_id,
                                             uint64_t generation) {
  if (effect_id.empty() || generation == 0u) {
    return;
  }
  auto pending = pending_observations_.find(std::string(effect_id));
  if (pending != pending_observations_.end() &&
      pending->second.generation == generation &&
      pending->second.request_id.is_valid()) {
    CancelTabObservationForCore(browser_context_, pending->second.tab_id,
                                pending->second.request_id);
  }
}

void CorePageObservationBroker::CancelGeneration(uint64_t generation) {
  if (generation == 0u) {
    return;
  }
  std::vector<std::pair<std::string, RequestId>> observations;
  for (const auto& [effect_id, pending] : pending_observations_) {
    static_cast<void>(effect_id);
    if (pending.generation == generation && pending.request_id.is_valid()) {
      observations.emplace_back(pending.tab_id, pending.request_id);
    }
  }
  for (const auto& [tab_id, request_id] : observations) {
    CancelTabObservationForCore(browser_context_, tab_id, request_id);
  }
  page_media_store_->RevokeGeneration(generation);
  std::vector<std::string> media_cancellations;
  for (const auto& [effect_id, pending] : pending_media_observations_) {
    if (pending.generation == generation) {
      media_cancellations.push_back(effect_id);
    }
  }
  for (const std::string& effect_id : media_cancellations) {
    auto pending = pending_media_observations_.find(effect_id);
    if (pending != pending_media_observations_.end() &&
        pending->second.cancel) {
      base::OnceClosure cancel = std::move(pending->second.cancel);
      std::move(cancel).Run();
    }
  }
}

void CorePageObservationBroker::OnMediaObservation(
    core_mojom::EffectEnvelopePtr effect,
    CompletionCallback callback,
    bool create_direct_projection,
    AuthorizedObservationTarget authorized_target,
    ObservationEnvelope observation,
    core_mojom::MediaObservationResultPtr media,
    uint32_t suppressed_secret_value_count) {
  if (effect) {
    pending_media_observations_.erase(effect->effect_id);
  }
  observation.redaction.suppressed_secret_value_count =
      base::ClampAdd(observation.redaction.suppressed_secret_value_count,
                     suppressed_secret_value_count);
  FinishObservation(std::move(effect), std::move(callback),
                    create_direct_projection, std::move(authorized_target),
                    std::move(observation), std::move(media));
}

}  // namespace taffy
