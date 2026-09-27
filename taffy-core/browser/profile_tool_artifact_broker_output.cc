// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <stdint.h>

#include <algorithm>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/task/thread_pool.h"
#include "taffy/browser/profile_tool_artifact_broker.h"
#include "taffy/browser/profile_tool_artifact_validation.h"
#include "taffy/browser/profile_tool_handle_store.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/services/tool-runtime/supervisor/profile_tool_supervisor.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

mojom::TaskProducedArtifactReceiptPtr ArtifactReceipt(
    const std::string& artifact_id,
    mojom::TaskArtifactKind kind) {
  return mojom::TaskProducedArtifactReceipt::New(artifact_id, kind);
}

mojom::TaskToolOutputReceiptPtr OutputReceipt(
    std::vector<uint8_t> digest,
    uint64_t byte_count,
    mojom::TaskProducedArtifactReceiptPtr artifact) {
  return mojom::TaskToolOutputReceipt::New(std::move(digest), byte_count, 1u,
                                           std::move(artifact));
}

bool IsCompleted(const mojom::EffectResult& result) {
  return result.status == mojom::EffectStatus::kCompleted && result.tool &&
         result.tool->status == mojom::ToolTerminalStatus::kCompleted &&
         result.tool->success;
}

std::optional<VerifiedToolOutput> ValidatePythonOutput(
    std::string entrypoint,
    std::vector<uint8_t> content) {
  return ValidateBundledPythonOutput(std::move(entrypoint),
                                     std::move(content));
}

}  // namespace

bool ProfileToolArtifactBroker::RetainsArtifactKind(
    mojom::TaskArtifactKind kind) {
  switch (kind) {
    case mojom::TaskArtifactKind::kDocx:
    case mojom::TaskArtifactKind::kXlsx:
    case mojom::TaskArtifactKind::kWaveAudio:
    case mojom::TaskArtifactKind::kFrameArchive:
      return true;
    case mojom::TaskArtifactKind::kMarkdown:
    case mojom::TaskArtifactKind::kCsv:
    case mojom::TaskArtifactKind::kPdf:
    case mojom::TaskArtifactKind::kPptx:
      return false;
  }
  return false;
}

void ProfileToolArtifactBroker::RetainTaskToolOutput(
    const mojom::TaskEffectBinding& binding,
    mojom::EffectResultPtr result,
    RetainCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!callback || !binding.operation || !binding.tool_job ||
      !binding.tool_job->job || !result || !result->operation ||
      binding.operation->service_generation != active_generation_ ||
      binding.tool_job->runtime != binding.tool_job->job->runtime ||
      binding.tool_job->job_id != binding.tool_job->job->job_id ||
      binding.task_id != binding.tool_job->job->task_id ||
      result->effect_id != binding.effect_id ||
      result->kind != mojom::EffectKind::kToolJob ||
      !OperationsMatch(*binding.operation, *result->operation) ||
      !IsCompleted(*result) ||
      result->tool->job_id != binding.tool_job->job_id ||
      result->tool->success->operation_kind !=
          binding.tool_job->job->operation_kind) {
    if (binding.tool_job) {
      AbandonTaskToolJob(binding.tool_job->job_id);
    }
    if (callback) {
      std::move(callback).Run(nullptr);
    }
    return;
  }
  const mojom::ToolJobEffect& job = *binding.tool_job->job;
  mojom::ToolSuccess& success = *result->tool->success;
  if (job.runtime == mojom::ToolRuntimeKind::kPython) {
    if (!job.bundled_python || !success.bundled_python || !job.budget ||
        success.bundled_python->output.empty() ||
        success.bundled_python->output.size() > job.budget->max_output_bytes ||
        success.bundled_python->output.size() > mojom::kMaxToolJobOutputBytes ||
        pending_media_.contains(job.job_id) ||
        pending_python_.contains(job.job_id) ||
        !CanReservePendingJob(binding.task_id)) {
      std::move(callback).Run(nullptr);
      return;
    }
    pending_python_.insert_or_assign(
        job.job_id,
        PendingPythonValidation{binding.task_id,
                                binding.tool_job->action_id,
                                binding.effect_id,
                                active_generation_,
                                CaptureOperation(*binding.operation),
                                job.operation_kind,
                                false});
    // Exact ZIP parsing includes a bitwise CRC over as much as 1 MiB. Keep
    // that untrusted linear work off the browser sequence just as the larger
    // descriptor-backed media validation below is kept off it. The result is
    // owned here, so the bounded worker buffer moves to that task without a
    // second browser-sequence allocation or copy.
    std::vector<uint8_t> output =
        std::move(success.bundled_python->output);
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE, {base::TaskPriority::USER_VISIBLE},
        base::BindOnce(&ValidatePythonOutput, job.bundled_python->entrypoint_id,
                       std::move(output)),
        base::BindOnce(&ProfileToolArtifactBroker::OnPythonOutputValidated,
                       weak_factory_.GetWeakPtr(), binding.Clone(),
                       std::move(callback)));
    return;
  }
  if (job.runtime != mojom::ToolRuntimeKind::kMedia) {
    std::move(callback).Run(nullptr);
    return;
  }
  const auto pending = pending_media_.find(job.job_id);
  if (pending == pending_media_.end() ||
      pending->second.task_id != binding.task_id ||
      pending->second.action_id != binding.tool_job->action_id ||
      pending->second.effect_id != binding.effect_id ||
      pending->second.service_generation != active_generation_ ||
      pending->second.operation != job.operation_kind ||
      !MatchesOperation(pending->second.operation_identity,
                        *binding.operation) ||
      pending->second.async_work_in_flight || pending->second.abandoned) {
    AbandonTaskToolJob(job.job_id);
    std::move(callback).Run(nullptr);
    return;
  }
  if (job.operation_kind == mojom::ToolOperation::kProbeMedia) {
    if (!success.media_probe || success.media_probe->duration_ms > 600'000u ||
        success.media_probe->audio_streams > 64u ||
        success.media_probe->video_streams > 64u ||
        success.media_probe->width > 1920u ||
        success.media_probe->height > 1080u ||
        (success.media_probe->audio_streams == 0u &&
         success.media_probe->video_streams == 0u) ||
        (success.media_probe->video_streams == 0u &&
         (success.media_probe->width != 0u ||
          success.media_probe->height != 0u))) {
      AbandonTaskToolJob(job.job_id);
      std::move(callback).Run(nullptr);
      return;
    }
    VerifiedToolOutput encoded = EncodeMediaProbe(*success.media_probe);
    CleanupPendingMediaJob(pending);
    std::move(callback).Run(OutputReceipt(std::move(encoded.digest),
                                          encoded.content.size(), nullptr));
    return;
  }
  if (!pending->second.artifact_kind) {
    AbandonTaskToolJob(job.job_id);
    std::move(callback).Run(nullptr);
    return;
  }
  std::string output_handle;
  uint64_t output_bytes = 0u;
  uint32_t frame_count = 0u;
  std::vector<uint8_t> digest;
  if (success.audio_extract) {
    output_handle = success.audio_extract->output_handle;
    output_bytes = success.audio_extract->output_bytes;
    digest = std::move(success.audio_extract->output_digest);
  } else if (success.frame_sample) {
    output_handle = success.frame_sample->output_handle;
    output_bytes = success.frame_sample->output_bytes;
    frame_count = success.frame_sample->frame_count;
    digest = std::move(success.frame_sample->output_digest);
  } else if (success.transcode) {
    output_handle = success.transcode->output_handle;
    output_bytes = success.transcode->output_bytes;
    digest = std::move(success.transcode->output_digest);
  } else {
    AbandonTaskToolJob(job.job_id);
    std::move(callback).Run(nullptr);
    return;
  }
  if (!job.budget || output_handle != pending->second.output_handle ||
      output_bytes == 0u || output_bytes > job.budget->max_output_bytes ||
      digest.size() != 32u) {
    AbandonTaskToolJob(job.job_id);
    std::move(callback).Run(nullptr);
    return;
  }
  base::File output = std::move(pending->second.output);
  const mojom::TaskArtifactKind artifact_kind = *pending->second.artifact_kind;
  pending->second.async_work_in_flight = true;
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&ProfileToolArtifactBroker::ReadAndValidateOutput,
                     std::move(output), output_bytes, artifact_kind,
                     frame_count, std::move(digest)),
      base::BindOnce(&ProfileToolArtifactBroker::OnMediaOutputValidated,
                     weak_factory_.GetWeakPtr(), job.job_id, output_bytes,
                     std::move(callback)));
}

void ProfileToolArtifactBroker::OnPythonOutputValidated(
    mojom::TaskEffectBindingPtr binding,
    RetainCallback callback,
    std::optional<VerifiedToolOutput> verified) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const std::string job_id =
      binding && binding->tool_job ? binding->tool_job->job_id : std::string();
  const auto pending = pending_python_.find(job_id);
  const bool exact =
      pending != pending_python_.end() && binding && binding->operation &&
      binding->tool_job && binding->tool_job->job &&
      binding->tool_job->job->budget && !pending->second.abandoned &&
      pending->second.task_id == binding->task_id &&
      pending->second.action_id == binding->tool_job->action_id &&
      pending->second.effect_id == binding->effect_id &&
      pending->second.service_generation == active_generation_ &&
      pending->second.operation ==
          binding->tool_job->job->operation_kind &&
      binding->operation->service_generation == active_generation_ &&
      MatchesOperation(pending->second.operation_identity,
                       *binding->operation) &&
      verified &&
      verified->content.size() <=
          binding->tool_job->job->budget->max_output_bytes;
  if (pending != pending_python_.end()) {
    pending_python_.erase(pending);
  }
  if (!exact) {
    std::move(callback).Run(nullptr);
    return;
  }
  std::move(callback).Run(RetainVerifiedArtifact(
      *binding, std::string(), verified->kind, std::move(verified->digest),
      std::move(verified->content)));
}

std::optional<VerifiedToolOutput>
ProfileToolArtifactBroker::ReadAndValidateOutput(
    base::File output,
    uint64_t expected_bytes,
    mojom::TaskArtifactKind kind,
    uint32_t frame_count,
    std::vector<uint8_t> expected_digest) {
  if (!output.IsValid() || expected_bytes == 0u ||
      expected_bytes > 16u * 1024u * 1024u ||
      output.GetLength() != static_cast<int64_t>(expected_bytes)) {
    return std::nullopt;
  }
  std::vector<uint8_t> content(static_cast<size_t>(expected_bytes));
  if (!output.ReadAndCheck(0, content)) {
    return std::nullopt;
  }
  // CRC, archive traversal and the second digest are deliberately on this
  // may-block worker, never on the browser sequence.
  return ValidateMediaOutput(kind, frame_count, std::move(expected_digest),
                             std::move(content));
}

void ProfileToolArtifactBroker::OnMediaOutputValidated(
    std::string job_id,
    uint64_t expected_bytes,
    RetainCallback callback,
    std::optional<VerifiedToolOutput> verified) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const auto pending = pending_media_.find(job_id);
  if (pending == pending_media_.end()) {
    std::move(callback).Run(nullptr);
    return;
  }
  PendingMediaJob record = std::move(pending->second);
  handles_->Forget(record.input_handle);
  handles_->Forget(record.output_handle);
  supervisor_->handle_broker().RevokeJob(job_id);
  pending_media_.erase(pending);
  if (!verified || verified->content.size() != expected_bytes ||
      record.service_generation != active_generation_ || record.abandoned) {
    std::move(callback).Run(nullptr);
    return;
  }
  const std::string artifact_id = "artifact-" + record.action_id;
  const auto key = std::make_pair(record.task_id, artifact_id);
  if (artifacts_.contains(key) ||
      !CanRetainArtifact(record.task_id, verified->content.size())) {
    std::move(callback).Run(nullptr);
    return;
  }
  artifact_bytes_ += verified->content.size();
  artifacts_.insert_or_assign(
      key, ArtifactEntry{record.task_id, std::move(record.source_id),
                         active_generation_,
                         record.operation_identity.task_revision, verified->kind,
                         std::move(verified->content)});
  std::move(callback).Run(
      OutputReceipt(std::move(verified->digest), expected_bytes,
                    ArtifactReceipt(artifact_id, verified->kind)));
}

mojom::TaskToolOutputReceiptPtr
ProfileToolArtifactBroker::RetainVerifiedArtifact(
    const mojom::TaskEffectBinding& binding,
    std::string source_id,
    mojom::TaskArtifactKind kind,
    std::vector<uint8_t> digest,
    std::vector<uint8_t> content) {
  const std::string artifact_id = "artifact-" + binding.tool_job->action_id;
  const auto key = std::make_pair(binding.task_id, artifact_id);
  if (artifacts_.contains(key) ||
      !CanRetainArtifact(binding.task_id, content.size())) {
    return nullptr;
  }
  const uint64_t byte_count = content.size();
  artifact_bytes_ += content.size();
  artifacts_.insert_or_assign(
      key, ArtifactEntry{binding.task_id, std::move(source_id),
                         active_generation_, binding.operation->task_revision,
                         kind, std::move(content)});
  return OutputReceipt(std::move(digest), byte_count,
                       ArtifactReceipt(artifact_id, kind));
}

bool ProfileToolArtifactBroker::DeliverArtifact(
    base::ObserverList<CoreServiceObserver>& observers,
    const std::string& task_id,
    uint64_t service_generation,
    const mojom::TaskArtifactEffect& artifact) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (private_profile_ || service_generation != active_generation_ ||
      !RetainsArtifactKind(artifact.kind) || !artifact.content.empty()) {
    return false;
  }
  const auto found = artifacts_.find({task_id, artifact.artifact_id});
  if (found == artifacts_.end() || found->second.task_id != task_id ||
      found->second.service_generation != service_generation ||
      found->second.task_revision != artifact.workspace_revision ||
      found->second.kind != artifact.kind) {
    return false;
  }
  bool delivered = false;
  for (CoreServiceObserver& observer : observers) {
    delivered |= observer.OnTaskArtifactExport(
        task_id, artifact.artifact_id, artifact.kind, found->second.content);
  }
  if (delivered) {
    artifact_bytes_ -=
        std::min(artifact_bytes_, found->second.content.size());
    artifacts_.erase(found);
  }
  return delivered;
}

}  // namespace taffy
