// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/task/sequenced_task_runner.h"
#include "mojo/public/cpp/bindings/callback_helpers.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "taffy/browser/model/profile_model_broker.h"

namespace taffy {

namespace service = core_service::mojom;

ProfileModelBroker::ModelStreamConsumer::ModelStreamConsumer(
    base::WeakPtr<ProfileModelBroker> broker,
    std::string effect_id)
    : broker_(std::move(broker)), effect_id_(std::move(effect_id)) {}

ProfileModelBroker::ModelStreamConsumer::~ModelStreamConsumer() = default;

void ProfileModelBroker::ModelStreamConsumer::OnDataReceived(
    std::string_view data,
    base::OnceClosure resume) {
  if (!broker_) {
    return;
  }
  broker_->OnModelStreamData(effect_id_, data, std::move(resume));
}

void ProfileModelBroker::ModelStreamConsumer::OnComplete(bool success) {
  // OnComplete belongs to this consumer's call stack. Defer the owner update
  // so finishing the request may destroy the consumer without deleting the
  // object whose method is still returning.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&ProfileModelBroker::OnModelStreamComplete,
                                broker_, effect_id_, success));
}

void ProfileModelBroker::ModelStreamConsumer::OnRetry(
    base::OnceClosure start_retry) {
  // Transport retries are disabled. Keep the consumer total if that setting
  // ever regresses: SimpleURLLoader must not remain paused on an unrun hook.
  std::move(start_retry).Run();
}

void ProfileModelBroker::OnModelResponseStarted(
    std::string effect_id,
    const GURL& final_url,
    const network::mojom::URLResponseHead& response_head) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto it = in_flight_.find(effect_id);
  if (it == in_flight_.end()) {
    return;
  }
  PendingCall& pending = *it->second;
  if (!pending.loader || final_url != pending.url || pending.response_started) {
    AbortModelStream(effect_id, service::EffectStatus::kInvalidResult);
    return;
  }
  pending.response_started = true;
  if (response_head.headers) {
    pending.response_http_status = response_head.headers->response_code();
  }
}

void ProfileModelBroker::OnModelStreamData(std::string effect_id,
                                           std::string_view data,
                                           base::OnceClosure resume) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto it = in_flight_.find(effect_id);
  if (it == in_flight_.end()) {
    return;
  }
  PendingCall& pending = *it->second;
  if (!pending.loader || !pending.response_started || data.empty() ||
      pending.stream_resume || !pending.stream_piece.empty()) {
    pending.stream_resume = std::move(resume);
    AbortModelStream(effect_id, service::EffectStatus::kInvalidResult);
    return;
  }

  // Only a successful response can speak as Taffy. Error bodies are provider
  // diagnostics, and a managed 401/402 may be followed by a fresh-token retry;
  // neither is answer text. Their status still reaches the reducer at the
  // terminal below.
  if (pending.response_http_status < 200 ||
      pending.response_http_status >= 300) {
    std::move(resume).Run();
    return;
  }

  const uint64_t allowance = pending.effect->model_request->max_output_bytes;
  if (pending.streamed_response_bytes > allowance ||
      data.size() > allowance - pending.streamed_response_bytes) {
    pending.stream_resume = std::move(resume);
    AbortModelStream(effect_id, service::EffectStatus::kResourceLimit);
    return;
  }
  pending.streamed_response_bytes += data.size();
  pending.stream_piece.assign(data);
  pending.stream_piece_offset = 0u;
  pending.stream_resume = std::move(resume);
  SendNextModelStreamChunk(effect_id);
}

void ProfileModelBroker::SendNextModelStreamChunk(
    const std::string& effect_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto it = in_flight_.find(effect_id);
  if (it == in_flight_.end()) {
    return;
  }
  PendingCall& pending = *it->second;
  if (!pending.stream_resume || pending.stream_abort_scheduled) {
    return;
  }
  if (pending.stream_piece_offset == pending.stream_piece.size()) {
    pending.stream_piece.clear();
    pending.stream_piece_offset = 0u;
    base::OnceClosure resume = std::move(pending.stream_resume);
    std::move(resume).Run();
    return;
  }

  const size_t remaining =
      pending.stream_piece.size() - pending.stream_piece_offset;
  const size_t chunk_size = std::min(
      remaining, static_cast<size_t>(service::kMaxModelStreamChunkBytes));
  auto chunk = service::ModelStreamChunk::New();
  chunk->operation = pending.effect->operation.Clone();
  chunk->effect_id = pending.effect->effect_id;
  chunk->sequence = pending.stream_sequence;
  const auto begin =
      pending.stream_piece.begin() +
      static_cast<std::string::difference_type>(pending.stream_piece_offset);
  chunk->data.assign(
      begin, begin + static_cast<std::string::difference_type>(chunk_size));
  model_stream_chunk_dispatcher_.Run(
      std::move(chunk),
      mojo::WrapCallbackWithDefaultInvokeIfNotRun(
          base::BindOnce(&ProfileModelBroker::OnModelStreamChunkDelivered,
                         weak_factory_.GetWeakPtr(), effect_id),
          service::ModelStreamChunkStatus::kUnavailable));
}

void ProfileModelBroker::OnModelStreamChunkDelivered(
    std::string effect_id,
    service::ModelStreamChunkStatus status) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto it = in_flight_.find(effect_id);
  if (it == in_flight_.end()) {
    return;
  }
  PendingCall& pending = *it->second;
  if (status != service::ModelStreamChunkStatus::kAccepted) {
    AbortModelStream(effect_id,
                     status == service::ModelStreamChunkStatus::kInvalid
                         ? service::EffectStatus::kInvalidResult
                         : service::EffectStatus::kUnavailable);
    return;
  }
  if (pending.stream_sequence == std::numeric_limits<uint32_t>::max()) {
    AbortModelStream(effect_id, service::EffectStatus::kResourceLimit);
    return;
  }
  const size_t remaining =
      pending.stream_piece.size() - pending.stream_piece_offset;
  pending.stream_piece_offset += std::min(
      remaining, static_cast<size_t>(service::kMaxModelStreamChunkBytes));
  ++pending.stream_sequence;
  SendNextModelStreamChunk(effect_id);
}

void ProfileModelBroker::AbortModelStream(const std::string& effect_id,
                                          service::EffectStatus status) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto it = in_flight_.find(effect_id);
  if (it == in_flight_.end() || it->second->stream_abort_scheduled) {
    return;
  }
  it->second->stream_abort_scheduled = true;
  // The caller can be the stream consumer or a synchronous dispatcher. Finish
  // on the next task so deleting the loader and consumer never deletes an
  // object while one of its methods is still on the stack.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](base::WeakPtr<ProfileModelBroker> broker, std::string id,
             service::EffectStatus terminal_status) {
            if (!broker) {
              return;
            }
            auto found = broker->in_flight_.find(id);
            if (found == broker->in_flight_.end()) {
              return;
            }
            PendingCall& pending = *found->second;
            broker->Finish(
                id, broker->MakeResult(*pending.effect, terminal_status, {},
                                       pending.response_http_status < 0
                                           ? 0u
                                           : static_cast<uint32_t>(
                                                 pending.response_http_status),
                                       pending.stream_sequence != 0u));
          },
          weak_factory_.GetWeakPtr(), effect_id, status));
}

}  // namespace taffy
