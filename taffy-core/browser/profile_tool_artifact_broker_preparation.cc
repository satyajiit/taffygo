// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/profile_tool_artifact_broker.h"

#include <stdint.h>

#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "base/check.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/task/thread_pool.h"
#include "taffy/browser/profile_tool_handle_store.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/services/tool-runtime/supervisor/profile_tool_supervisor.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

constexpr uint64_t kMaxMediaBytes = 16u * 1024u * 1024u;
constexpr std::string_view kOutputMarker = "browser-custody-output";

std::string* InputHandle(mojom::ToolJobEffect& job) {
  switch (job.operation_kind) {
    case mojom::ToolOperation::kProbeMedia:
      return job.media_probe ? &job.media_probe->input_handle : nullptr;
    case mojom::ToolOperation::kExtractAudio:
      return job.audio_extract ? &job.audio_extract->input_handle : nullptr;
    case mojom::ToolOperation::kSampleFrames:
      return job.frame_sample ? &job.frame_sample->input_handle : nullptr;
    case mojom::ToolOperation::kTranscodePreset:
      return job.transcode ? &job.transcode->input_handle : nullptr;
    case mojom::ToolOperation::kRunBundledPythonModule:
    case mojom::ToolOperation::kGenerateLocalModel:
    case mojom::ToolOperation::kRunSignedWasmTransform:
    case mojom::ToolOperation::kEmbedLocalModel:
      return nullptr;
  }
  return nullptr;
}

bool HasExactTransformArguments(const mojom::ToolJobEffect& job) {
  switch (job.operation_kind) {
    case mojom::ToolOperation::kProbeMedia:
      return !!job.media_probe;
    case mojom::ToolOperation::kExtractAudio:
      return job.audio_extract &&
             job.audio_extract->output_handle == kOutputMarker &&
             job.audio_extract->preset_id == "audio.wav.pcm16.v1";
    case mojom::ToolOperation::kSampleFrames:
      return job.frame_sample &&
             job.frame_sample->output_handle == kOutputMarker &&
             job.frame_sample->preset_id == "frames.png.zip.v1" &&
             job.frame_sample->max_frames > 0u &&
             job.frame_sample->max_frames <= 12u;
    case mojom::ToolOperation::kTranscodePreset:
      return job.transcode && job.transcode->output_handle == kOutputMarker &&
             job.transcode->preset_id == "audio.wav.mono-pcm16.v1";
    case mojom::ToolOperation::kRunBundledPythonModule:
    case mojom::ToolOperation::kGenerateLocalModel:
    case mojom::ToolOperation::kRunSignedWasmTransform:
    case mojom::ToolOperation::kEmbedLocalModel:
      return false;
  }
  return false;
}

bool SourceTypeMatches(MediaTopLevelType type, mojom::ToolOperation operation) {
  if (operation == mojom::ToolOperation::kSampleFrames) {
    return type == MediaTopLevelType::kVideo;
  }
  return type == MediaTopLevelType::kAudio || type == MediaTopLevelType::kVideo;
}

std::optional<mojom::TaskArtifactKind> ArtifactKind(
    mojom::ToolOperation operation) {
  switch (operation) {
    case mojom::ToolOperation::kExtractAudio:
    case mojom::ToolOperation::kTranscodePreset:
      return mojom::TaskArtifactKind::kWaveAudio;
    case mojom::ToolOperation::kSampleFrames:
      return mojom::TaskArtifactKind::kFrameArchive;
    case mojom::ToolOperation::kProbeMedia:
    case mojom::ToolOperation::kRunBundledPythonModule:
    case mojom::ToolOperation::kGenerateLocalModel:
    case mojom::ToolOperation::kRunSignedWasmTransform:
    case mojom::ToolOperation::kEmbedLocalModel:
      return std::nullopt;
  }
  return std::nullopt;
}

std::string* OutputHandle(mojom::ToolJobEffect& job) {
  switch (job.operation_kind) {
    case mojom::ToolOperation::kExtractAudio:
      return job.audio_extract ? &job.audio_extract->output_handle : nullptr;
    case mojom::ToolOperation::kSampleFrames:
      return job.frame_sample ? &job.frame_sample->output_handle : nullptr;
    case mojom::ToolOperation::kTranscodePreset:
      return job.transcode ? &job.transcode->output_handle : nullptr;
    default:
      return nullptr;
  }
}

}  // namespace

void ProfileToolArtifactBroker::PrepareTaskToolJob(
    mojom::TaskEffectBindingPtr binding,
    PrepareCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!callback || !binding || !binding->operation || !binding->tool_job ||
      !binding->tool_job->job) {
    if (callback) {
      std::move(callback).Run(nullptr);
    }
    return;
  }
  mojom::ToolJobEffect& job = *binding->tool_job->job;
  if (binding->tool_job->runtime != job.runtime) {
    std::move(callback).Run(nullptr);
    return;
  }
  if (job.runtime != mojom::ToolRuntimeKind::kMedia) {
    std::move(callback).Run(std::move(binding));
    return;
  }
  std::string* const source_id = InputHandle(job);
  if (!source_id || !HasExactTransformArguments(job) ||
      binding->operation->service_generation != active_generation_ ||
      binding->tool_job->job_id != job.job_id ||
      binding->task_id != job.task_id || !job.budget ||
      job.budget->max_input_bytes == 0u ||
      job.budget->max_input_bytes > kMaxMediaBytes ||
      pending_media_.contains(job.job_id) ||
      pending_python_.contains(job.job_id)) {
    std::move(callback).Run(nullptr);
    return;
  }
  const auto found = sources_.find({binding->task_id, *source_id});
  if (found == sources_.end() ||
      found->second.admission.service_generation != active_generation_ ||
      found->second.admission.byte_count != job.budget->max_input_bytes ||
      !SourceTypeMatches(found->second.admission.media_type,
                         job.operation_kind)) {
    std::move(callback).Run(nullptr);
    return;
  }
  const bool opened_source = found->second.resource.IsValid();
  const bool completed_download =
      found->second.download.has_value() && !!found->second.web_contents;
  if ((!opened_source && !completed_download) ||
      !CanReservePendingJob(binding->task_id)) {
    std::move(callback).Run(nullptr);
    return;
  }
  pending_media_.insert_or_assign(
      job.job_id,
      PendingMediaJob{binding->task_id,
                      binding->tool_job->action_id,
                      binding->effect_id,
                      *source_id,
                      std::string(),
                      std::string(),
                      active_generation_,
                      CaptureOperation(*binding->operation),
                      job.operation_kind,
                      ArtifactKind(job.operation_kind),
                      base::File(),
                      false,
                      false});
  if (opened_source) {
    // Keep the admitted source as task custody and give each bounded attempt
    // its own descriptor. Pause/resume and a second transform must not require
    // the person to select the same file again merely because one attempt
    // already opened it.
    base::File input = found->second.resource.Duplicate();
    OnInputOpened(std::move(binding), std::move(callback), std::move(input));
    return;
  }
  const auto pending = pending_media_.find(job.job_id);
  CHECK(pending != pending_media_.end());
  pending->second.async_work_in_flight = true;
  OpenCompletedTaskDownload(
      found->second.web_contents.get(), *found->second.download,
      base::BindOnce(&ProfileToolArtifactBroker::OnInputOpened,
                     weak_factory_.GetWeakPtr(), std::move(binding),
                     std::move(callback)));
}

void ProfileToolArtifactBroker::OnInputOpened(
    mojom::TaskEffectBindingPtr binding,
    PrepareCallback callback,
    base::File input) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const std::string job_id =
      binding && binding->tool_job ? binding->tool_job->job_id : std::string();
  const auto pending = pending_media_.find(job_id);
  if (pending != pending_media_.end()) {
    pending->second.async_work_in_flight = false;
  }
  const bool exact =
      input.IsValid() && binding && binding->tool_job &&
      binding->tool_job->job && binding->operation &&
      pending != pending_media_.end() && !pending->second.abandoned &&
      binding->operation->service_generation == active_generation_ &&
      pending->second.task_id == binding->task_id &&
      pending->second.action_id == binding->tool_job->action_id &&
      pending->second.effect_id == binding->effect_id &&
      pending->second.operation == binding->tool_job->job->operation_kind &&
      pending->second.service_generation == active_generation_ &&
      MatchesOperation(pending->second.operation_identity,
                       *binding->operation);
  if (!exact) {
    if (pending != pending_media_.end()) {
      CleanupPendingMediaJob(pending);
    }
    std::move(callback).Run(nullptr);
    return;
  }
  if (binding->tool_job->job->operation_kind ==
      mojom::ToolOperation::kProbeMedia) {
    AdmitPrepared(std::move(binding), std::move(callback), std::move(input),
                  OutputFiles{});
    return;
  }
  pending->second.async_work_in_flight = true;
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&ProfileToolArtifactBroker::CreateOutputFiles,
                     output_root_),
      base::BindOnce(&ProfileToolArtifactBroker::OnOutputCreated,
                     weak_factory_.GetWeakPtr(), std::move(binding),
                     std::move(callback), std::move(input)));
}

ProfileToolArtifactBroker::OutputFiles
ProfileToolArtifactBroker::CreateOutputFiles(base::FilePath output_root) {
  OutputFiles files;
  if (output_root.empty() || !base::CreateDirectory(output_root)) {
    return files;
  }
  base::FilePath path;
  files.worker = base::CreateAndOpenTemporaryFileInDir(
      output_root, &path, base::File::FLAG_DELETE_ON_CLOSE,
      FILE_PATH_LITERAL("media-output-"));
  if (!files.worker.IsValid()) {
    return OutputFiles{};
  }
  files.retained = files.worker.Duplicate();
  if (!files.retained.IsValid()) {
    return OutputFiles{};
  }
  // Android and host builds are POSIX, where an unlinked open file is the
  // smallest possible custody surface. FLAG_DELETE_ON_CLOSE supplies the
  // equivalent behavior on Windows.
  base::DeleteFile(path);
  return files;
}

void ProfileToolArtifactBroker::OnOutputCreated(
    mojom::TaskEffectBindingPtr binding,
    PrepareCallback callback,
    base::File input,
    OutputFiles output) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const std::string job_id =
      binding && binding->tool_job ? binding->tool_job->job_id : std::string();
  const auto pending = pending_media_.find(job_id);
  if (pending != pending_media_.end()) {
    pending->second.async_work_in_flight = false;
  }
  const bool exact =
      output.worker.IsValid() && output.retained.IsValid() && binding &&
      binding->operation && binding->tool_job && binding->tool_job->job &&
      pending != pending_media_.end() && !pending->second.abandoned &&
      binding->operation->service_generation == active_generation_ &&
      pending->second.task_id == binding->task_id &&
      pending->second.action_id == binding->tool_job->action_id &&
      pending->second.effect_id == binding->effect_id &&
      pending->second.operation == binding->tool_job->job->operation_kind &&
      pending->second.service_generation == active_generation_ &&
      MatchesOperation(pending->second.operation_identity,
                       *binding->operation);
  if (!exact) {
    if (pending != pending_media_.end()) {
      CleanupPendingMediaJob(pending);
    }
    std::move(callback).Run(nullptr);
    return;
  }
  AdmitPrepared(std::move(binding), std::move(callback), std::move(input),
                std::move(output));
}

void ProfileToolArtifactBroker::AdmitPrepared(
    mojom::TaskEffectBindingPtr binding,
    PrepareCallback callback,
    base::File input,
    OutputFiles output) {
  if (!binding || !binding->operation || !binding->tool_job ||
      !binding->tool_job->job) {
    std::move(callback).Run(nullptr);
    return;
  }
  mojom::ToolJobEffect& job = *binding->tool_job->job;
  const auto pending = pending_media_.find(job.job_id);
  if (pending == pending_media_.end() ||
      pending->second.task_id != binding->task_id ||
      pending->second.action_id != binding->tool_job->action_id ||
      pending->second.effect_id != binding->effect_id ||
      pending->second.service_generation != active_generation_ ||
      pending->second.operation != job.operation_kind ||
      !MatchesOperation(pending->second.operation_identity,
                        *binding->operation) ||
      pending->second.abandoned) {
    AbandonTaskToolJob(job.job_id);
    std::move(callback).Run(nullptr);
    return;
  }
  std::string* const input_field = InputHandle(job);
  if (!input_field) {
    AbandonTaskToolJob(job.job_id);
    std::move(callback).Run(nullptr);
    return;
  }
  const std::string source_id = *input_field;
  const std::string input_handle = supervisor_->handle_broker().Mint(
      job.job_id, ToolHandleBroker::Mode::kRead);
  if (input_handle.empty() ||
      !handles_->Admit(input_handle, ToolHandleBroker::Mode::kRead,
                       std::move(input))) {
    AbandonTaskToolJob(job.job_id);
    std::move(callback).Run(nullptr);
    return;
  }
  pending->second.input_handle = input_handle;
  *input_field = input_handle;
  if (!pending->second.artifact_kind) {
    std::move(callback).Run(std::move(binding));
    return;
  }
  std::string* const output_field = OutputHandle(job);
  if (!output_field) {
    AbandonTaskToolJob(job.job_id);
    std::move(callback).Run(nullptr);
    return;
  }
  const std::string output_handle = supervisor_->handle_broker().Mint(
      job.job_id, ToolHandleBroker::Mode::kWrite);
  if (output_handle.empty() ||
      !handles_->Admit(output_handle, ToolHandleBroker::Mode::kWrite,
                       std::move(output.worker))) {
    AbandonTaskToolJob(job.job_id);
    std::move(callback).Run(nullptr);
    return;
  }
  *output_field = output_handle;
  pending->second.source_id = source_id;
  pending->second.output_handle = output_handle;
  pending->second.output = std::move(output.retained);
  std::move(callback).Run(std::move(binding));
}

}  // namespace taffy
