// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <string>
#include <utility>
#include <vector>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/contracts/tool-runtime/generated/mojom/tool_runtime.mojom.h"
#include "taffy/services/tool-runtime/supervisor/tool_contract_conversion.h"
#include "taffy/services/tool-runtime/supervisor/tool_job_resources.h"

namespace taffy::tool_contract_conversion {
namespace {

namespace core = core_service::mojom;
namespace runtime = tool_runtime::mojom;

bool Identifier(const std::string& value, uint64_t maximum) {
  return !value.empty() && value.size() <= maximum;
}

runtime::OperationEnvelopePtr ConvertOperation(
    const core::OperationEnvelope& operation) {
  auto result = runtime::OperationEnvelope::New();
  result->operation_id = operation.operation_id;
  result->service_generation = operation.service_generation;
  result->task_revision = operation.task_revision;
  result->deadline_monotonic_ms = operation.deadline_monotonic_ms;
  result->idempotency_key = operation.idempotency_key;
  return result;
}

bool ConvertKind(core::ToolRuntimeKind input,
                 runtime::ToolRuntimeKind* output) {
  switch (input) {
    case core::ToolRuntimeKind::kPython:
      *output = runtime::ToolRuntimeKind::kPython;
      return true;
    case core::ToolRuntimeKind::kLocalModel:
      *output = runtime::ToolRuntimeKind::kLocalModel;
      return true;
    case core::ToolRuntimeKind::kMedia:
      *output = runtime::ToolRuntimeKind::kMedia;
      return true;
    case core::ToolRuntimeKind::kWasm:
      *output = runtime::ToolRuntimeKind::kWasm;
      return true;
  }
  return false;
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

bool ConvertArtifactKind(core::ToolModelArtifactKind input,
                         runtime::ToolModelArtifactKind* output) {
  switch (input) {
    case core::ToolModelArtifactKind::kLitertTflite:
      *output = runtime::ToolModelArtifactKind::kLitertTflite;
      return true;
    case core::ToolModelArtifactKind::kOnnxRuntime:
      *output = runtime::ToolModelArtifactKind::kOnnxRuntime;
      return true;
    case core::ToolModelArtifactKind::kGguf:
      *output = runtime::ToolModelArtifactKind::kGguf;
      return true;
  }
  return false;
}

// All fields are a typed claim the browser register verifies before opening a
// worker resource. Bounds here keep malformed claims from reaching that seam;
// no later layer repairs a disagreement into a different job.
bool ValidArtifact(const core::ToolModelArtifact& artifact) {
  return Identifier(artifact.model_id, runtime::kMaxModelIdBytes) &&
         Identifier(artifact.model_revision, runtime::kMaxModelRevisionBytes) &&
         artifact.adapter_id.size() <= runtime::kMaxModelIdBytes &&
         artifact.artifact_bytes <= runtime::kMaxModelArtifactBytes &&
         artifact.adapter_bytes <= runtime::kMaxModelArtifactBytes;
}

runtime::ToolModelArtifactPtr ConvertArtifact(
    const core::ToolModelArtifact& artifact) {
  runtime::ToolModelArtifactKind kind;
  if (!ValidArtifact(artifact) || !ConvertArtifactKind(artifact.kind, &kind)) {
    return nullptr;
  }
  return runtime::ToolModelArtifact::New(
      artifact.model_id, artifact.model_revision, kind, artifact.artifact_bytes,
      artifact.artifact_digest, artifact.adapter_id, artifact.adapter_bytes,
      artifact.adapter_digest);
}

bool ValidPair(core::ToolRuntimeKind runtime_kind,
               core::ToolOperation operation) {
  switch (runtime_kind) {
    case core::ToolRuntimeKind::kPython:
      return operation == core::ToolOperation::kRunBundledPythonModule;
    case core::ToolRuntimeKind::kLocalModel:
      return operation == core::ToolOperation::kGenerateLocalModel ||
             operation == core::ToolOperation::kEmbedLocalModel;
    case core::ToolRuntimeKind::kMedia:
      return operation == core::ToolOperation::kProbeMedia ||
             operation == core::ToolOperation::kExtractAudio ||
             operation == core::ToolOperation::kSampleFrames ||
             operation == core::ToolOperation::kTranscodePreset;
    case core::ToolRuntimeKind::kWasm:
      return operation == core::ToolOperation::kRunSignedWasmTransform;
  }
  return false;
}

size_t JobBodyCount(const core::ToolJobEffect& job) {
  return !!job.bundled_python + !!job.local_model + !!job.media_probe +
         !!job.audio_extract + !!job.frame_sample + !!job.transcode +
         !!job.signed_wasm + !!job.local_embedding;
}

bool ValidJob(const core::OperationEnvelope& operation,
              const core::ToolJobEffect& job) {
  if (!Identifier(operation.operation_id, runtime::kMaxOperationIdBytes) ||
      !Identifier(operation.idempotency_key,
                  runtime::kMaxIdempotencyKeyBytes) ||
      !Identifier(job.job_id, runtime::kMaxJobIdBytes) ||
      !Identifier(job.tool_id, runtime::kMaxToolIdBytes) ||
      !Identifier(job.tool_version, runtime::kMaxToolVersionBytes) ||
      !job.budget || JobBodyCount(job) != 1u ||
      !ValidPair(job.runtime, job.operation_kind)) {
    return false;
  }
  const auto& budget = *job.budget;
  return budget.max_input_bytes <= runtime::kMaxJobInputBytes &&
         budget.max_output_bytes <= runtime::kMaxJobOutputBytes &&
         budget.max_memory_bytes > 0u &&
         budget.max_memory_bytes <= runtime::kMaxJobMemoryBytes &&
         budget.max_cpu_ms > 0u && budget.max_cpu_ms <= runtime::kMaxJobCpuMs &&
         budget.max_temporary_bytes <= runtime::kMaxJobTemporaryBytes &&
         budget.max_output_chunks > 0u &&
         budget.max_output_chunks <= runtime::kMaxOutputChunks;
}

bool ConvertArguments(const core::ToolJobEffect& input,
                      runtime::ToolJob* output) {
  if (input.bundled_python) {
    if (!Identifier(input.bundled_python->entrypoint_id,
                    runtime::kMaxEntrypointIdBytes) ||
        input.bundled_python->input.size() > input.budget->max_input_bytes) {
      return false;
    }
    output->bundled_python = runtime::BundledPythonArguments::New(
        input.bundled_python->entrypoint_id, input.bundled_python->input);
  } else if (input.local_model) {
    auto artifact = ConvertArtifact(*input.local_model->model);
    if (!artifact ||
        !Identifier(input.local_model->conversation_id,
                    runtime::kMaxConversationIdBytes) ||
        input.local_model->prompt_utf8.size() > input.budget->max_input_bytes ||
        input.local_model->max_output_tokens == 0u) {
      return false;
    }
    output->local_model = runtime::LocalModelArguments::New(
        input.local_model->conversation_id, input.local_model->prompt_utf8,
        input.local_model->max_output_tokens, std::move(artifact));
  } else if (input.local_embedding) {
    auto artifact = ConvertArtifact(*input.local_embedding->model);
    if (!artifact ||
        input.local_embedding->input_utf8.size() >
            input.budget->max_input_bytes ||
        input.local_embedding->max_input_tokens == 0u) {
      return false;
    }
    output->local_embedding = runtime::LocalEmbeddingArguments::New(
        std::move(artifact), input.local_embedding->input_utf8,
        input.local_embedding->max_input_tokens,
        input.local_embedding->normalize);
  } else if (input.media_probe) {
    if (!Identifier(input.media_probe->input_handle,
                    runtime::kMaxHandleIdBytes)) {
      return false;
    }
    output->media_probe =
        runtime::MediaProbeArguments::New(input.media_probe->input_handle);
  } else if (input.audio_extract) {
    if (!Identifier(input.audio_extract->input_handle,
                    runtime::kMaxHandleIdBytes) ||
        !Identifier(input.audio_extract->output_handle,
                    runtime::kMaxHandleIdBytes) ||
        !Identifier(input.audio_extract->preset_id,
                    runtime::kMaxPresetIdBytes)) {
      return false;
    }
    output->audio_extract = runtime::AudioExtractArguments::New(
        input.audio_extract->input_handle, input.audio_extract->output_handle,
        input.audio_extract->preset_id);
  } else if (input.frame_sample) {
    if (!Identifier(input.frame_sample->input_handle,
                    runtime::kMaxHandleIdBytes) ||
        !Identifier(input.frame_sample->output_handle,
                    runtime::kMaxHandleIdBytes) ||
        !Identifier(input.frame_sample->preset_id,
                    runtime::kMaxPresetIdBytes) ||
        input.frame_sample->max_frames == 0u) {
      return false;
    }
    output->frame_sample = runtime::FrameSampleArguments::New(
        input.frame_sample->input_handle, input.frame_sample->output_handle,
        input.frame_sample->preset_id, input.frame_sample->max_frames);
  } else if (input.transcode) {
    if (!Identifier(input.transcode->input_handle,
                    runtime::kMaxHandleIdBytes) ||
        !Identifier(input.transcode->output_handle,
                    runtime::kMaxHandleIdBytes) ||
        !Identifier(input.transcode->preset_id, runtime::kMaxPresetIdBytes)) {
      return false;
    }
    output->transcode = runtime::TranscodeArguments::New(
        input.transcode->input_handle, input.transcode->output_handle,
        input.transcode->preset_id);
  } else if (input.signed_wasm) {
    if (!Identifier(input.signed_wasm->entrypoint_id,
                    runtime::kMaxEntrypointIdBytes) ||
        input.signed_wasm->input.size() > input.budget->max_input_bytes) {
      return false;
    }
    output->signed_wasm = runtime::SignedWasmArguments::New(
        input.signed_wasm->entrypoint_id, input.signed_wasm->input);
  }
  return true;
}

}  // namespace

bool ConvertJob(const core::OperationEnvelope& operation,
                const core::ToolJobEffect& job,
                bool streaming_available,
                runtime::ToolJobPtr* converted) {
  if (!ValidJob(operation, job)) {
    return false;
  }
  auto result = runtime::ToolJob::New();
  result->operation = ConvertOperation(operation);
  result->job_id = job.job_id;
  result->tool_id = job.tool_id;
  result->tool_version = job.tool_version;
  if (!ConvertKind(job.runtime, &result->runtime) ||
      !ConvertOperationKind(job.operation_kind, &result->operation_kind)) {
    return false;
  }
  result->budget = runtime::ResourceBudget::New(
      job.budget->max_input_bytes, job.budget->max_output_bytes,
      job.budget->max_memory_bytes, job.budget->max_cpu_ms,
      job.budget->max_temporary_bytes, job.budget->max_output_chunks);
  if (!ConvertArguments(job, result.get())) {
    return false;
  }
  // The library a caller may not choose, in the shape that means none was
  // resolved. It is written here rather than left unset because a job with a
  // hole in it is a message that cannot be sent, and it is written empty
  // rather than filled because resolving the archive is the browser's own
  // step: `tool_job_resources::Bind` replaces this when a library register
  // exists, and refuses the job outright if it cannot open what it names.
  result->python_library = runtime::ToolPythonLibrary::New(
      std::string(), std::string(), 0u, std::vector<uint8_t>(32u, 0u));
  tool_job_resources::PlanPayloads(streaming_available, result.get());
  *converted = std::move(result);
  return true;
}

}  // namespace taffy::tool_contract_conversion
