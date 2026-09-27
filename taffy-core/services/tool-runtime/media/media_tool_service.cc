// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/services/tool-runtime/media/media_tool_service.h"

#include <algorithm>
#include <optional>
#include <utility>

#include "base/functional/bind.h"
#include "base/task/bind_post_task.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "taffy/services/tool-runtime/media/media_probe.h"
#include "taffy/services/tool-runtime/media/media_transform.h"

namespace taffy {

namespace mojom = tool_runtime::mojom;

namespace {

// The contract's own ceilings, restated where they are enforced. A worker that
// trusted the browser to have checked them would be one review away from being
// the place a bad job got through.
constexpr size_t kMaxJobIdBytes = 128u;
constexpr size_t kMaxHandleIdBytes = 256u;
constexpr uint64_t kMaxJobInputBytes = 16u * 1024u * 1024u;
constexpr uint64_t kMaxJobOutputBytes = 16u * 1024u * 1024u;
constexpr uint32_t kMaxSampledFrames = 12u;
constexpr uint32_t kMaxFrameWidth = 1920u;
constexpr uint32_t kMaxFrameHeight = 1080u;
constexpr uint32_t kMaxDecodedBuffers = 12000u;
constexpr base::TimeDelta kMaxMediaDuration = base::Minutes(10);
constexpr base::TimeDelta kMaxWorkerTime = base::Seconds(30);

size_t JobBodyCount(const mojom::ToolJob& job) {
  return static_cast<size_t>(!!job.bundled_python) +
         static_cast<size_t>(!!job.local_model) +
         static_cast<size_t>(!!job.media_probe) +
         static_cast<size_t>(!!job.audio_extract) +
         static_cast<size_t>(!!job.frame_sample) +
         static_cast<size_t>(!!job.transcode) +
         static_cast<size_t>(!!job.signed_wasm) +
         static_cast<size_t>(!!job.local_embedding);
}

bool IsIdentifier(const std::string& value, size_t maximum) {
  return !value.empty() && value.size() <= maximum;
}

bool HasOnlyMediaResources(const mojom::ToolJobResources& resources,
                           bool output) {
  return resources.media_input.IsValid() &&
         resources.media_output.IsValid() == output &&
         !resources.model_artifact.IsValid() &&
         !resources.model_adapter.IsValid() &&
         !resources.bulk_input.IsValid() && !resources.bulk_output.is_valid() &&
         !resources.python_library.IsValid();
}

std::optional<media_tool::TransformKind> TransformFor(
    const mojom::ToolJob& job) {
  switch (job.operation_kind) {
    case mojom::ToolOperation::kExtractAudio:
      if (job.audio_extract && job.tool_id == "media.audio.extract" &&
          job.audio_extract->preset_id == media_tool::kAudioExtractPreset) {
        return media_tool::TransformKind::kExtractAudio;
      }
      return std::nullopt;
    case mojom::ToolOperation::kSampleFrames:
      if (job.frame_sample && job.tool_id == "media.frames.sample" &&
          job.frame_sample->preset_id == media_tool::kFrameSamplePreset &&
          job.frame_sample->max_frames > 0u &&
          job.frame_sample->max_frames <= kMaxSampledFrames) {
        return media_tool::TransformKind::kSampleFrames;
      }
      return std::nullopt;
    case mojom::ToolOperation::kTranscodePreset:
      if (job.transcode && job.tool_id == "media.transcode" &&
          job.transcode->preset_id == media_tool::kTranscodePreset) {
        return media_tool::TransformKind::kTranscode;
      }
      return std::nullopt;
    case mojom::ToolOperation::kProbeMedia:
    case mojom::ToolOperation::kRunBundledPythonModule:
    case mojom::ToolOperation::kGenerateLocalModel:
    case mojom::ToolOperation::kRunSignedWasmTransform:
    case mojom::ToolOperation::kEmbedLocalModel:
      return std::nullopt;
  }
}

const std::string* InputHandle(const mojom::ToolJob& job) {
  if (job.audio_extract) {
    return &job.audio_extract->input_handle;
  }
  if (job.frame_sample) {
    return &job.frame_sample->input_handle;
  }
  if (job.transcode) {
    return &job.transcode->input_handle;
  }
  return nullptr;
}

const std::string* OutputHandle(const mojom::ToolJob& job) {
  if (job.audio_extract) {
    return &job.audio_extract->output_handle;
  }
  if (job.frame_sample) {
    return &job.frame_sample->output_handle;
  }
  if (job.transcode) {
    return &job.transcode->output_handle;
  }
  return nullptr;
}

mojom::ToolTerminalStatus TerminalFor(media_tool::ProbeFailure failure) {
  switch (failure) {
    case media_tool::ProbeFailure::kUnreadableFile:
    case media_tool::ProbeFailure::kUnparseableContainer:
    case media_tool::ProbeFailure::kNoStreams:
      return mojom::ToolTerminalStatus::kInvalidInput;
    case media_tool::ProbeFailure::kInputTooLarge:
      return mojom::ToolTerminalStatus::kResourceLimit;
  }
}

mojom::ToolTerminalStatus TerminalFor(media_tool::TransformFailure failure) {
  switch (failure) {
    case media_tool::TransformFailure::kUnreadableInput:
    case media_tool::TransformFailure::kNoMatchingStream:
    case media_tool::TransformFailure::kMalformedMedia:
    case media_tool::TransformFailure::kUnsupportedCodec:
      return mojom::ToolTerminalStatus::kInvalidInput;
    case media_tool::TransformFailure::kInputTooLarge:
    case media_tool::TransformFailure::kOutputTooLarge:
    case media_tool::TransformFailure::kDurationTooLong:
    case media_tool::TransformFailure::kDimensionsTooLarge:
    case media_tool::TransformFailure::kTooManyFrames:
      return mojom::ToolTerminalStatus::kResourceLimit;
    case media_tool::TransformFailure::kUnwritableOutput:
      return mojom::ToolTerminalStatus::kRuntimeCrashed;
    case media_tool::TransformFailure::kCancelled:
      return mojom::ToolTerminalStatus::kCancelled;
    case media_tool::TransformFailure::kDeadlineExceeded:
      return mojom::ToolTerminalStatus::kDeadlineExceeded;
  }
}

}  // namespace

MediaToolServiceImpl::MediaToolServiceImpl(
    mojo::PendingReceiver<mojom::MediaToolService> receiver)
    : receiver_(this, std::move(receiver)),
      parse_runner_(base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
           base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN})) {}

MediaToolServiceImpl::~MediaToolServiceImpl() = default;

mojom::ToolAdmissionStatus MediaToolServiceImpl::Admit(
    const mojom::ToolJob& job,
    const mojom::ToolJobResources& resources) const {
  if (running_) {
    // One job per process. A second is not queued, because a queue here would
    // be a scheduler the browser cannot see, cancel or bound.
    return mojom::ToolAdmissionStatus::kBackpressure;
  }
  if (job.job_id.empty() || job.job_id.size() > kMaxJobIdBytes ||
      !job.operation) {
    return mojom::ToolAdmissionStatus::kInvalidJob;
  }
  if (job.runtime != mojom::ToolRuntimeKind::kMedia ||
      job.tool_version != "1" || JobBodyCount(job) != 1u) {
    return mojom::ToolAdmissionStatus::kUnsupported;
  }
  const bool probe = job.operation_kind == mojom::ToolOperation::kProbeMedia;
  const std::optional<media_tool::TransformKind> transform = TransformFor(job);
  if ((!probe && !transform) ||
      (probe &&
       (!job.media_probe || job.tool_id != "media.probe" ||
        !IsIdentifier(job.media_probe->input_handle, kMaxHandleIdBytes)))) {
    return mojom::ToolAdmissionStatus::kUnsupported;
  }
  const std::string* const input = InputHandle(job);
  const std::string* const output = OutputHandle(job);
  if (transform &&
      (!input || !output || !IsIdentifier(*input, kMaxHandleIdBytes) ||
       !IsIdentifier(*output, kMaxHandleIdBytes) || *input == *output)) {
    return mojom::ToolAdmissionStatus::kInvalidJob;
  }
  // A probe reads a descriptor and returns five numbers. It carries no input
  // payload and streams nothing, so any other transport is a job whose plan
  // does not describe this operation.
  if (job.input_payload.is_null() || job.output_payload.is_null() ||
      job.input_payload->transport != mojom::ToolInputTransport::kInline ||
      job.input_payload->byte_length != 0u ||
      job.output_payload->transport !=
          mojom::ToolOutputTransport::kInlineChunks) {
    return mojom::ToolAdmissionStatus::kInvalidJob;
  }
  if (job.budget.is_null() || job.budget->max_input_bytes == 0u ||
      job.budget->max_input_bytes > kMaxJobInputBytes ||
      job.budget->max_output_bytes == 0u ||
      job.budget->max_output_bytes > kMaxJobOutputBytes ||
      job.budget->max_cpu_ms == 0u ||
      job.output_payload->max_byte_length == 0u ||
      job.output_payload->max_byte_length > job.budget->max_output_bytes) {
    return mojom::ToolAdmissionStatus::kInvalidJob;
  }
  // The resource the handle names, and nothing else. A worker that accepted a
  // model artifact or a writable output beside a probe would be accepting
  // authority the operation has no use for.
  if (!HasOnlyMediaResources(resources, transform.has_value())) {
    return mojom::ToolAdmissionStatus::kInvalidJob;
  }
  return mojom::ToolAdmissionStatus::kAccepted;
}

void MediaToolServiceImpl::Start(
    mojom::ToolJobPtr job,
    mojom::ToolJobResourcesPtr resources,
    mojo::PendingRemote<mojom::ToolRuntimeClient> client,
    StartCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto admission = mojom::ToolAdmission::New();
  if (!job || !resources) {
    admission->job_id = std::string();
    admission->status = mojom::ToolAdmissionStatus::kInvalidJob;
    std::move(callback).Run(std::move(admission));
    return;
  }
  admission->job_id = job->job_id;
  admission->status = Admit(*job, *resources);
  const bool accepted =
      admission->status == mojom::ToolAdmissionStatus::kAccepted;
  std::move(callback).Run(std::move(admission));
  if (!accepted) {
    // A refused job leaves no state behind: the descriptors close with
    // `resources`, and no completion is owed for a job that never started.
    return;
  }

  running_ = true;
  cancelled_ = false;
  job_id_ = job->job_id;
  operation_ = job->operation.Clone();
  client_.Bind(std::move(client));
  cancelled_signal_ = std::make_shared<std::atomic_bool>(false);

  const uint64_t budget = job->budget->max_input_bytes;
  base::File input(std::move(resources->media_input));
  if (job->operation_kind != mojom::ToolOperation::kProbeMedia) {
    const media_tool::TransformKind kind = *TransformFor(*job);
    output_handle_ = *OutputHandle(*job);
    media_tool::TransformLimits limits;
    limits.max_input_bytes = budget;
    limits.max_output_bytes = std::min(job->budget->max_output_bytes,
                                       job->output_payload->max_byte_length);
    limits.max_frames = job->frame_sample ? job->frame_sample->max_frames : 1u;
    limits.max_width = kMaxFrameWidth;
    limits.max_height = kMaxFrameHeight;
    limits.max_decoded_buffers = kMaxDecodedBuffers;
    limits.max_duration = kMaxMediaDuration;
    limits.deadline =
        base::TimeTicks::Now() +
        std::min(kMaxWorkerTime, base::Milliseconds(job->budget->max_cpu_ms));
    base::File output(std::move(resources->media_output));
    parse_runner_->PostTask(
        FROM_HERE,
        base::BindOnce(&media_tool::TransformMedia, kind, std::move(input),
                       std::move(output), limits, cancelled_signal_,
                       base::BindPostTaskToCurrentDefault(base::BindOnce(
                           &MediaToolServiceImpl::OnTransformed,
                           weak_factory_.GetWeakPtr(), job_id_, kind))));
    return;
  }
  parse_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&media_tool::ProbeMedia, std::move(input), budget,
                     base::BindPostTaskToCurrentDefault(
                         base::BindOnce(&MediaToolServiceImpl::OnProbed,
                                        weak_factory_.GetWeakPtr(), job_id_))));
}

void MediaToolServiceImpl::Cancel(const std::string& job_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!running_ || job_id != job_id_) {
    // Cancelling a job this process was not launched for is a message about
    // someone else. It is ignored rather than answered, because answering it
    // would let one job's identifier end another job.
    return;
  }
  cancelled_ = true;
  if (cancelled_signal_) {
    cancelled_signal_->store(true, std::memory_order_relaxed);
  }
  Publish(mojom::ToolTerminalStatus::kCancelled, nullptr);
}

void MediaToolServiceImpl::OnProbed(std::string job_id,
                                    media_tool::ProbeResult result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!running_ || job_id != job_id_) {
    return;
  }
  if (cancelled_) {
    // The reading arrived after the cancel was answered. It is dropped rather
    // than published: one job produces exactly one terminal result.
    return;
  }
  if (!result.has_value()) {
    Publish(TerminalFor(result.error()), nullptr);
    return;
  }

  auto probe = mojom::MediaProbeResult::New();
  probe->duration_ms = result->duration_ms;
  probe->audio_streams = result->audio_streams;
  probe->video_streams = result->video_streams;
  probe->width = result->width;
  probe->height = result->height;

  auto success = mojom::ToolSuccess::New();
  success->operation_kind = mojom::ToolOperation::kProbeMedia;
  success->media_probe = std::move(probe);
  Publish(mojom::ToolTerminalStatus::kCompleted, std::move(success));
}

void MediaToolServiceImpl::OnTransformed(std::string job_id,
                                         media_tool::TransformKind kind,
                                         media_tool::TransformResult result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!running_ || job_id != job_id_ || cancelled_) {
    return;
  }
  if (!result.has_value()) {
    Publish(TerminalFor(result.error()), nullptr);
    return;
  }
  auto success = mojom::ToolSuccess::New();
  switch (kind) {
    case media_tool::TransformKind::kExtractAudio:
      success->operation_kind = mojom::ToolOperation::kExtractAudio;
      success->audio_extract = mojom::AudioExtractResult::New(
          output_handle_, result->output_bytes,
          std::vector<uint8_t>(result->output_digest.begin(),
                               result->output_digest.end()));
      break;
    case media_tool::TransformKind::kSampleFrames:
      success->operation_kind = mojom::ToolOperation::kSampleFrames;
      success->frame_sample = mojom::FrameSampleResult::New(
          output_handle_, result->frame_count, result->output_bytes,
          std::vector<uint8_t>(result->output_digest.begin(),
                               result->output_digest.end()));
      break;
    case media_tool::TransformKind::kTranscode:
      success->operation_kind = mojom::ToolOperation::kTranscodePreset;
      success->transcode = mojom::TranscodeResult::New(
          output_handle_, result->output_bytes,
          std::vector<uint8_t>(result->output_digest.begin(),
                               result->output_digest.end()));
      break;
  }
  Publish(mojom::ToolTerminalStatus::kCompleted, std::move(success));
}

void MediaToolServiceImpl::Publish(mojom::ToolTerminalStatus status,
                                   mojom::ToolSuccessPtr success) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!running_) {
    return;
  }
  running_ = false;
  cancelled_signal_.reset();
  auto completion = mojom::ToolCompletion::New();
  completion->operation = std::move(operation_);
  completion->job_id = job_id_;
  completion->status = status;
  completion->success = std::move(success);
  if (client_) {
    client_->Completed(std::move(completion));
  }
  // One process, one job — so the process ends here rather than waiting to be
  // collected. Closing this receiver is what the browser sees: its remote
  // disconnects, the launcher releases the entry, and `ServiceProcessHost`
  // terminates a process with nothing left to do. The completion and this
  // receiver travel on different pipes, so the browser launcher relays the
  // client and closes its process remote before it forwards the terminal. No
  // cross-pipe ordering is assumed here.
  receiver_.reset();
}

}  // namespace taffy
