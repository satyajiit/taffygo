// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <utility>

#include "base/functional/bind.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/services/core/core_service_impl.h"

namespace taffy {

namespace mojom = core_service::mojom;

void CoreServiceImpl::DeliverModelStreamChunk(
    mojom::ModelStreamChunkPtr chunk,
    DeliverModelStreamChunkCallback callback) {
  if (!chunk || !chunk->operation || chunk->effect_id.empty() ||
      chunk->data.empty() ||
      chunk->data.size() > mojom::kMaxModelStreamChunkBytes) {
    std::move(callback).Run(mojom::ModelStreamChunkStatus::kInvalid);
    return;
  }
  if (chunk->operation->service_generation != generation_) {
    std::move(callback).Run(mojom::ModelStreamChunkStatus::kStale);
    return;
  }
  if (!ready_ || shutdown_started_) {
    std::move(callback).Run(mojom::ModelStreamChunkStatus::kUnavailable);
    return;
  }
  core_.AsyncCall(&RustCore::DeliverModelStreamChunk)
      .WithArgs(std::move(chunk))
      .Then(base::BindOnce(&CoreServiceImpl::OnModelStreamChunkDelivered,
                           weak_factory_.GetWeakPtr(), std::move(callback)));
}

void CoreServiceImpl::OnModelStreamChunkDelivered(
    DeliverModelStreamChunkCallback callback,
    CoreModelStreamDelivery delivery) {
  if (delivery.status != mojom::ModelStreamChunkStatus::kAccepted ||
      delivery.answer_events.empty()) {
    std::move(callback).Run(delivery.status);
    return;
  }
  if (!ready_ || !host_.is_bound() || shutdown_started_) {
    std::move(callback).Run(mojom::ModelStreamChunkStatus::kUnavailable);
    return;
  }
  host_->PublishTaskAnswerEvents(
      std::move(delivery.answer_events),
      base::BindOnce(&CoreServiceImpl::OnModelStreamAnswerEventsPublished,
                     weak_factory_.GetWeakPtr(), std::move(callback),
                     delivery.status));
}

void CoreServiceImpl::OnModelStreamAnswerEventsPublished(
    DeliverModelStreamChunkCallback callback,
    mojom::ModelStreamChunkStatus status,
    bool accepted) {
  std::move(callback).Run(
      accepted ? status : mojom::ModelStreamChunkStatus::kUnavailable);
}

}  // namespace taffy
