// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/services/tool-runtime/supervisor/tool_contract_conversion.h"

#include <string>
#include <utility>

#include "base/strings/string_util.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/contracts/tool-runtime/generated/mojom/tool_runtime.mojom.h"

namespace taffy::tool_contract_conversion {
namespace {

namespace core = core_service::mojom;
namespace runtime = tool_runtime::mojom;

bool Identifier(const std::string& value, uint64_t maximum) {
  return !value.empty() && value.size() <= maximum;
}

bool SameOperation(const runtime::OperationEnvelope& left,
                   const runtime::OperationEnvelope& right) {
  return left.operation_id == right.operation_id &&
         left.service_generation == right.service_generation &&
         left.task_revision == right.task_revision &&
         left.deadline_monotonic_ms == right.deadline_monotonic_ms &&
         left.idempotency_key == right.idempotency_key;
}

bool ConvertOperationKind(core::ToolOperation input,
                          runtime::ToolOperation* output) {
  switch (input) {
    case core::ToolOperation::kRunBundledPythonModule:
      *output = runtime::ToolOperation::kRunBundledPythonModule;
      return true;
    case core::ToolOperation::kGenerateLocalModel:
      *output = runtime::ToolOperation::kGenerateLocalModel;
      return true;
    case core::ToolOperation::kProbeMedia:
      *output = runtime::ToolOperation::kProbeMedia;
      return true;
    case core::ToolOperation::kExtractAudio:
      *output = runtime::ToolOperation::kExtractAudio;
      return true;
    case core::ToolOperation::kSampleFrames:
      *output = runtime::ToolOperation::kSampleFrames;
      return true;
    case core::ToolOperation::kTranscodePreset:
      *output = runtime::ToolOperation::kTranscodePreset;
      return true;
    case core::ToolOperation::kRunSignedWasmTransform:
      *output = runtime::ToolOperation::kRunSignedWasmTransform;
      return true;
    case core::ToolOperation::kEmbedLocalModel:
      *output = runtime::ToolOperation::kEmbedLocalModel;
      return true;
  }
  return false;
}

bool ConvertEmbedding(const runtime::LocalEmbeddingResult& input,
                      core::LocalEmbeddingResultPtr* output,
                      size_t* output_bytes) {
  // The declared length and the byte count are two numbers a worker produces
  // independently, so the only safe thing to do with them is require that they
  // agree. A reader that trusted `dimensions` alone would size a buffer from a
  // worker's arithmetic; one that trusted the bytes alone would hand a caller a
  // vector of a length nobody asked for.
  if (input.element_kind != runtime::EmbeddingElementKind::kFloat32Le ||
      input.dimensions == 0u ||
      input.dimensions > runtime::kMaxEmbeddingDimensions ||
      input.values.size() != static_cast<size_t>(input.dimensions) * 4u ||
      input.values.size() > runtime::kMaxEmbeddingValueBytes) {
    return false;
  }
  *output_bytes = input.values.size();
  *output = core::LocalEmbeddingResult::New(
      input.dimensions, core::EmbeddingElementKind::kFloat32Le, input.values,
      input.input_tokens);
  return true;
}

bool ConvertSuccess(const runtime::ToolSuccess& input,
                    const core::ToolJobEffect& job,
                    core::ToolSuccessPtr* output,
                    size_t* output_bytes) {
  runtime::ToolOperation expected;
  if (!ConvertOperationKind(job.operation_kind, &expected) ||
      input.operation_kind != expected) {
    return false;
  }
  auto result = core::ToolSuccess::New();
  result->operation_kind = job.operation_kind;
  size_t bodies = 0u;
  if (input.bundled_python) {
    ++bodies;
    *output_bytes = input.bundled_python->output.size();
    result->bundled_python =
        core::BundledPythonResult::New(input.bundled_python->output);
  }
  if (input.local_model) {
    ++bodies;
    core::LocalModelFinishReason reason;
    switch (input.local_model->finish_reason) {
      case runtime::LocalModelFinishReason::kStop:
        reason = core::LocalModelFinishReason::kStop;
        break;
      case runtime::LocalModelFinishReason::kTokenLimit:
        reason = core::LocalModelFinishReason::kTokenLimit;
        break;
      default:
        return false;
    }
    result->local_model =
        core::LocalModelResult::New(reason, input.local_model->input_tokens,
                                    input.local_model->output_tokens);
  }
  if (input.media_probe) {
    ++bodies;
    result->media_probe = core::MediaProbeResult::New(
        input.media_probe->duration_ms, input.media_probe->audio_streams,
        input.media_probe->video_streams, input.media_probe->width,
        input.media_probe->height);
  }
  if (input.audio_extract) {
    ++bodies;
    if (!Identifier(input.audio_extract->output_handle,
                    runtime::kMaxHandleIdBytes) ||
        input.audio_extract->output_digest.size() != 32u) {
      return false;
    }
    *output_bytes = input.audio_extract->output_bytes;
    result->audio_extract = core::AudioExtractResult::New(
        input.audio_extract->output_handle, input.audio_extract->output_bytes,
        input.audio_extract->output_digest);
  }
  if (input.frame_sample) {
    ++bodies;
    if (!Identifier(input.frame_sample->output_handle,
                    runtime::kMaxHandleIdBytes) ||
        input.frame_sample->frame_count == 0u ||
        input.frame_sample->output_digest.size() != 32u) {
      return false;
    }
    *output_bytes = input.frame_sample->output_bytes;
    result->frame_sample = core::FrameSampleResult::New(
        input.frame_sample->output_handle, input.frame_sample->frame_count,
        input.frame_sample->output_bytes,
        input.frame_sample->output_digest);
  }
  if (input.transcode) {
    ++bodies;
    if (!Identifier(input.transcode->output_handle,
                    runtime::kMaxHandleIdBytes) ||
        input.transcode->output_digest.size() != 32u) {
      return false;
    }
    *output_bytes = input.transcode->output_bytes;
    result->transcode = core::TranscodeResult::New(
        input.transcode->output_handle, input.transcode->output_bytes,
        input.transcode->output_digest);
  }
  if (input.signed_wasm) {
    ++bodies;
    *output_bytes = input.signed_wasm->output.size();
    result->signed_wasm =
        core::SignedWasmResult::New(input.signed_wasm->output);
  }
  if (input.local_embedding) {
    ++bodies;
    if (!ConvertEmbedding(*input.local_embedding, &result->local_embedding,
                          output_bytes)) {
      return false;
    }
  }
  bool matching_body = false;
  switch (job.operation_kind) {
    case core::ToolOperation::kRunBundledPythonModule:
      matching_body = !!input.bundled_python;
      break;
    case core::ToolOperation::kGenerateLocalModel:
      matching_body = !!input.local_model;
      break;
    case core::ToolOperation::kProbeMedia:
      matching_body = !!input.media_probe;
      break;
    case core::ToolOperation::kExtractAudio:
      matching_body = !!input.audio_extract;
      break;
    case core::ToolOperation::kSampleFrames:
      matching_body = !!input.frame_sample;
      break;
    case core::ToolOperation::kTranscodePreset:
      matching_body = !!input.transcode;
      break;
    case core::ToolOperation::kRunSignedWasmTransform:
      matching_body = !!input.signed_wasm;
      break;
    case core::ToolOperation::kEmbedLocalModel:
      matching_body = !!input.local_embedding;
      break;
  }
  if (bodies != 1u || !matching_body ||
      *output_bytes > job.budget->max_output_bytes) {
    return false;
  }
  *output = std::move(result);
  return true;
}

}  // namespace

bool ConvertProgress(const runtime::ToolProgress& progress,
                     const core::ToolJobEffect& job,
                     core::ToolProgressPtr* converted) {
  if (progress.job_id != job.job_id ||
      progress.progress_basis_points > 10000u) {
    return false;
  }
  *converted = core::ToolProgress::New(progress.job_id, progress.sequence,
                                       progress.progress_basis_points);
  return true;
}

bool ConvertChunk(const runtime::ToolOutputChunk& chunk,
                  const runtime::OperationEnvelope& operation,
                  const core::ToolJobEffect& job,
                  bool streaming,
                  core::ToolOutputChunkPtr* converted,
                  size_t* output_bytes) {
  if (!chunk.operation || !SameOperation(*chunk.operation, operation) ||
      chunk.job_id != job.job_id || (!!chunk.text + !!chunk.binary) != 1u) {
    return false;
  }
  auto result = core::ToolOutputChunk::New();
  result->job_id = chunk.job_id;
  result->sequence = chunk.sequence;
  result->is_final = chunk.is_final;
  if (chunk.kind == runtime::ToolChunkKind::kTextUtf8 && chunk.text) {
    *output_bytes = chunk.text->utf8.size();
    const std::string text(chunk.text->utf8.begin(), chunk.text->utf8.end());
    if (!base::IsStringUTF8(text)) {
      return false;
    }
    result->kind = core::ToolChunkKind::kTextUtf8;
    result->text = core::TextOutputChunk::New(chunk.text->utf8);
  } else if (chunk.kind == runtime::ToolChunkKind::kBinary && chunk.binary) {
    *output_bytes = chunk.binary->data.size();
    result->kind = core::ToolChunkKind::kBinary;
    result->binary = core::BinaryOutputChunk::New(chunk.binary->data);
  } else {
    return false;
  }
  // A streamed chunk is one increment of an answer, so it is held to the much
  // smaller stream bound: the point of streaming is many small messages, and
  // allowing the buffered bound here would let a worker send sixty-five
  // thousand chunks of sixty-four kilobytes each.
  const uint64_t chunk_bound = streaming ? runtime::kMaxStreamChunkBytes
                                         : runtime::kMaxOutputChunkBytes;
  if (*output_bytes > chunk_bound) {
    return false;
  }
  *converted = std::move(result);
  return true;
}

core::ToolTerminalStatus ConvertTerminalStatus(
    runtime::ToolTerminalStatus status) {
  switch (status) {
    case runtime::ToolTerminalStatus::kCompleted:
      return core::ToolTerminalStatus::kCompleted;
    case runtime::ToolTerminalStatus::kCancelled:
      return core::ToolTerminalStatus::kCancelled;
    case runtime::ToolTerminalStatus::kDeadlineExceeded:
      return core::ToolTerminalStatus::kDeadlineExceeded;
    case runtime::ToolTerminalStatus::kResourceLimit:
      return core::ToolTerminalStatus::kResourceLimit;
    case runtime::ToolTerminalStatus::kRuntimeCrashed:
      return core::ToolTerminalStatus::kRuntimeCrashed;
    case runtime::ToolTerminalStatus::kInvalidInput:
      return core::ToolTerminalStatus::kInvalidInput;
    case runtime::ToolTerminalStatus::kUnsupported:
      return core::ToolTerminalStatus::kUnsupported;
    case runtime::ToolTerminalStatus::kOutcomeUnknown:
      return core::ToolTerminalStatus::kOutcomeUnknown;
    case runtime::ToolTerminalStatus::kModelArtifactMissing:
      return core::ToolTerminalStatus::kModelArtifactMissing;
    case runtime::ToolTerminalStatus::kModelArtifactIncompatible:
      return core::ToolTerminalStatus::kModelArtifactIncompatible;
    case runtime::ToolTerminalStatus::kLocalRuntimeUnavailable:
      return core::ToolTerminalStatus::kLocalRuntimeUnavailable;
  }
  return core::ToolTerminalStatus::kInvalidInput;
}

bool ConvertCompletion(const runtime::ToolCompletion& completion,
                       const runtime::OperationEnvelope& operation,
                       const core::ToolJobEffect& job,
                       core::ToolTerminalStatus* status,
                       core::ToolSuccessPtr* success,
                       size_t* output_bytes) {
  if (!completion.operation ||
      !SameOperation(*completion.operation, operation) ||
      completion.job_id != job.job_id ||
      ((completion.status == runtime::ToolTerminalStatus::kCompleted) !=
       static_cast<bool>(completion.success))) {
    return false;
  }
  *status = ConvertTerminalStatus(completion.status);
  *output_bytes = 0u;
  if (completion.success &&
      !ConvertSuccess(*completion.success, job, success, output_bytes)) {
    return false;
  }
  return true;
}

}  // namespace taffy::tool_contract_conversion
