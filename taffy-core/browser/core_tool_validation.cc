// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_tool_validation.h"

#include <algorithm>
#include <string>

#include "base/strings/string_util.h"
#include "taffy/components/tools/entrypoints/tool_entrypoint_registry.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

bool IsIdentifier(const std::string& value, size_t maximum) {
  return !value.empty() && value.size() <= maximum;
}

size_t NarrowLimit(size_t boundary_limit, uint64_t schema_limit) {
  return std::min(boundary_limit, static_cast<size_t>(schema_limit));
}

bool IsValidModelArtifact(const mojom::ToolModelArtifact& artifact,
                          size_t max_identifier_bytes) {
  // The identity is all this edge checks. The length, digest and format the
  // caller proposed are replaced from the browser's own model register when
  // the supervisor opens the artifact, so bounding them here is about refusing
  // an absurd proposal early rather than about believing it.
  return IsIdentifier(artifact.model_id,
                      NarrowLimit(max_identifier_bytes,
                                  mojom::kMaxToolModelIdBytes)) &&
         IsIdentifier(artifact.model_revision,
                      NarrowLimit(max_identifier_bytes,
                                  mojom::kMaxToolModelRevisionBytes)) &&
         artifact.adapter_id.size() <=
             NarrowLimit(max_identifier_bytes, mojom::kMaxToolModelIdBytes) &&
         artifact.artifact_bytes <= mojom::kMaxToolModelArtifactBytes &&
         artifact.adapter_bytes <= mojom::kMaxToolModelArtifactBytes;
}

// Whether a Python worker may be started for this entrypoint at all.
//
// A well-formed identifier is not a permission, and until the frozen registry
// existed that was the whole of this edge's check: any name of the right shape
// and length reached a worker. This asks the compiled-in table whether the
// build carries the name, and refuses a row the table answers natively -
// starting a sandboxed interpreter to do something the product already owns
// spends a process, a budget and an attack surface to reach the same answer
// (decision 0064).
//
// The sandboxed core asks the same question of the same generated rows before
// it proposes the job. The two gates are deliberately not written in terms of
// each other: the browser is the process that starts the worker, so it does
// not take the core's word for what may be started.
bool IsAdmittedPythonEntrypoint(const std::string& entrypoint_id) {
  return tools::AdmitToolEntrypoint(entrypoint_id).admission ==
         tools::ToolEntrypointAdmission::kAdmitted;
}

size_t JobBodyCount(const mojom::ToolJobEffect& job) {
  return static_cast<size_t>(!!job.bundled_python) +
         static_cast<size_t>(!!job.local_model) +
         static_cast<size_t>(!!job.media_probe) +
         static_cast<size_t>(!!job.audio_extract) +
         static_cast<size_t>(!!job.frame_sample) +
         static_cast<size_t>(!!job.transcode) +
         static_cast<size_t>(!!job.signed_wasm) +
         static_cast<size_t>(!!job.local_embedding);
}

bool HasMatchingJobBody(const mojom::ToolJobEffect& job) {
  if (JobBodyCount(job) != 1u) {
    return false;
  }
  switch (job.operation_kind) {
    case mojom::ToolOperation::kRunBundledPythonModule:
      return !!job.bundled_python;
    case mojom::ToolOperation::kGenerateLocalModel:
      return !!job.local_model;
    case mojom::ToolOperation::kProbeMedia:
      return !!job.media_probe;
    case mojom::ToolOperation::kExtractAudio:
      return !!job.audio_extract;
    case mojom::ToolOperation::kSampleFrames:
      return !!job.frame_sample;
    case mojom::ToolOperation::kTranscodePreset:
      return !!job.transcode;
    case mojom::ToolOperation::kRunSignedWasmTransform:
      return !!job.signed_wasm;
    case mojom::ToolOperation::kEmbedLocalModel:
      return !!job.local_embedding;
  }
  return false;
}

bool HasValidRuntimeOperationPair(const mojom::ToolJobEffect& job) {
  switch (job.runtime) {
    case mojom::ToolRuntimeKind::kPython:
      return job.operation_kind ==
             mojom::ToolOperation::kRunBundledPythonModule;
    case mojom::ToolRuntimeKind::kLocalModel:
      return job.operation_kind == mojom::ToolOperation::kGenerateLocalModel ||
             job.operation_kind == mojom::ToolOperation::kEmbedLocalModel;
    case mojom::ToolRuntimeKind::kMedia:
      return job.operation_kind == mojom::ToolOperation::kProbeMedia ||
             job.operation_kind == mojom::ToolOperation::kExtractAudio ||
             job.operation_kind == mojom::ToolOperation::kSampleFrames ||
             job.operation_kind == mojom::ToolOperation::kTranscodePreset;
    case mojom::ToolRuntimeKind::kWasm:
      return job.operation_kind ==
             mojom::ToolOperation::kRunSignedWasmTransform;
  }
  return false;
}

size_t JobInputBytes(const mojom::ToolJobEffect& job) {
  if (job.bundled_python) {
    return job.bundled_python->input.size();
  }
  if (job.local_model) {
    return job.local_model->prompt_utf8.size();
  }
  if (job.local_embedding) {
    return job.local_embedding->input_utf8.size();
  }
  return job.signed_wasm ? job.signed_wasm->input.size() : 0u;
}

size_t SuccessBodyCount(const mojom::ToolSuccess& success) {
  return static_cast<size_t>(!!success.bundled_python) +
         static_cast<size_t>(!!success.local_model) +
         static_cast<size_t>(!!success.media_probe) +
         static_cast<size_t>(!!success.audio_extract) +
         static_cast<size_t>(!!success.frame_sample) +
         static_cast<size_t>(!!success.transcode) +
         static_cast<size_t>(!!success.signed_wasm) +
         static_cast<size_t>(!!success.local_embedding);
}

bool HasMatchingSuccessBody(const mojom::ToolSuccess& success) {
  if (SuccessBodyCount(success) != 1u) {
    return false;
  }
  switch (success.operation_kind) {
    case mojom::ToolOperation::kRunBundledPythonModule:
      return !!success.bundled_python;
    case mojom::ToolOperation::kGenerateLocalModel:
      return !!success.local_model;
    case mojom::ToolOperation::kProbeMedia:
      return !!success.media_probe;
    case mojom::ToolOperation::kExtractAudio:
      return !!success.audio_extract;
    case mojom::ToolOperation::kSampleFrames:
      return !!success.frame_sample;
    case mojom::ToolOperation::kTranscodePreset:
      return !!success.transcode;
    case mojom::ToolOperation::kRunSignedWasmTransform:
      return !!success.signed_wasm;
    case mojom::ToolOperation::kEmbedLocalModel:
      return !!success.local_embedding;
  }
  return false;
}

size_t ChunkBodyCount(const mojom::ToolOutputChunk& chunk) {
  return static_cast<size_t>(!!chunk.text) +
         static_cast<size_t>(!!chunk.binary);
}

bool ValidateChunk(const mojom::ToolOutputChunk& chunk,
                   const std::string& job_id,
                   size_t max_chunk_bytes) {
  if (chunk.job_id != job_id || ChunkBodyCount(chunk) != 1u) {
    return false;
  }
  switch (chunk.kind) {
    case mojom::ToolChunkKind::kTextUtf8:
      if (!chunk.text || chunk.text->utf8.size() > max_chunk_bytes) {
        return false;
      }
      return base::IsStringUTF8(std::string(chunk.text->utf8.begin(),
                                            chunk.text->utf8.end()));
    case mojom::ToolChunkKind::kBinary:
      return chunk.binary && chunk.binary->data.size() <= max_chunk_bytes;
  }
  return false;
}

size_t ChunkBytes(const mojom::ToolOutputChunk& chunk) {
  if (chunk.text) {
    return chunk.text->utf8.size();
  }
  return chunk.binary ? chunk.binary->data.size() : 0u;
}

bool AddWithin(size_t amount, size_t limit, size_t* total) {
  if (amount > limit || *total > limit - amount) {
    return false;
  }
  *total += amount;
  return true;
}

bool ValidateSuccess(const mojom::ToolSuccess& success,
                     const mojom::ToolJobEffect& job,
                     size_t max_identifier_bytes,
                     size_t max_output_bytes,
                     size_t* output_bytes) {
  if (success.operation_kind != job.operation_kind ||
      !HasMatchingSuccessBody(success)) {
    return false;
  }
  if (success.bundled_python) {
    return AddWithin(success.bundled_python->output.size(), max_output_bytes,
                     output_bytes);
  }
  if (success.local_model || success.media_probe) {
    return true;
  }
  if (success.local_embedding) {
    // The declared length and the byte count are two numbers a worker produces
    // independently, so the only safe thing to do with them is require that
    // they agree.
    const auto& embedding = *success.local_embedding;
    return embedding.element_kind == mojom::EmbeddingElementKind::kFloat32Le &&
           embedding.dimensions > 0u &&
           embedding.dimensions <= mojom::kMaxToolEmbeddingDimensions &&
           embedding.values.size() ==
               static_cast<size_t>(embedding.dimensions) * 4u &&
           embedding.values.size() <= mojom::kMaxToolEmbeddingValueBytes &&
           AddWithin(embedding.values.size(), max_output_bytes, output_bytes);
  }
  if (success.audio_extract) {
    return IsIdentifier(success.audio_extract->output_handle,
                        NarrowLimit(max_identifier_bytes,
                                    mojom::kMaxToolHandleIdBytes)) &&
           success.audio_extract->output_bytes <= max_output_bytes;
  }
  if (success.frame_sample) {
    return IsIdentifier(success.frame_sample->output_handle,
                        NarrowLimit(max_identifier_bytes,
                                    mojom::kMaxToolHandleIdBytes)) &&
           success.frame_sample->frame_count > 0u &&
           success.frame_sample->output_bytes <= max_output_bytes;
  }
  if (success.transcode) {
    return IsIdentifier(success.transcode->output_handle,
                        NarrowLimit(max_identifier_bytes,
                                    mojom::kMaxToolHandleIdBytes)) &&
           success.transcode->output_bytes <= max_output_bytes;
  }
  return success.signed_wasm &&
         AddWithin(success.signed_wasm->output.size(), max_output_bytes,
                   output_bytes);
}

}  // namespace

bool IsValidCoreToolJob(const mojom::ToolJobEffect& job,
                        size_t max_identifier_bytes,
                        size_t max_effect_bytes) {
  const size_t input_limit =
      NarrowLimit(max_effect_bytes, mojom::kMaxToolJobInputBytes);
  const size_t output_limit =
      NarrowLimit(max_effect_bytes, mojom::kMaxToolJobOutputBytes);
  if (!job.budget ||
      !IsIdentifier(job.job_id,
                    NarrowLimit(max_identifier_bytes,
                                mojom::kMaxToolJobIdBytes)) ||
      !IsIdentifier(job.tool_id,
                    NarrowLimit(max_identifier_bytes,
                                mojom::kMaxToolIdBytes)) ||
      !IsIdentifier(job.tool_version,
                    NarrowLimit(max_identifier_bytes,
                                mojom::kMaxToolVersionBytes)) ||
      // Empty means no task owns this job, which is a direct request and not
      // a defect. A non-empty one is the identity CancelTask matches to find
      // the worker it must stop.
      job.task_id.size() > max_identifier_bytes ||
      !HasMatchingJobBody(job) || !HasValidRuntimeOperationPair(job) ||
      job.budget->max_input_bytes > input_limit ||
      job.budget->max_output_bytes > output_limit ||
      job.budget->max_memory_bytes == 0u || job.budget->max_cpu_ms == 0u ||
      job.budget->max_memory_bytes > mojom::kMaxToolJobMemoryBytes ||
      job.budget->max_cpu_ms > mojom::kMaxToolJobCpuMs ||
      job.budget->max_temporary_bytes >
          mojom::kMaxToolJobTemporaryBytes ||
      job.budget->max_output_chunks == 0u ||
      job.budget->max_output_chunks > mojom::kMaxToolOutputChunks ||
      JobInputBytes(job) > job.budget->max_input_bytes) {
    return false;
  }
  if (job.bundled_python) {
    return IsIdentifier(job.bundled_python->entrypoint_id,
                        NarrowLimit(max_identifier_bytes,
                                    mojom::kMaxToolEntrypointIdBytes)) &&
           IsAdmittedPythonEntrypoint(job.bundled_python->entrypoint_id);
  }
  if (job.local_model) {
    return job.local_model->model &&
           IsValidModelArtifact(*job.local_model->model,
                                max_identifier_bytes) &&
           IsIdentifier(job.local_model->conversation_id,
                        NarrowLimit(max_identifier_bytes,
                                    mojom::kMaxToolConversationIdBytes)) &&
           job.local_model->max_output_tokens > 0u;
  }
  if (job.local_embedding) {
    return job.local_embedding->model &&
           IsValidModelArtifact(*job.local_embedding->model,
                                max_identifier_bytes) &&
           job.local_embedding->max_input_tokens > 0u;
  }
  if (job.media_probe) {
    return IsIdentifier(job.media_probe->input_handle,
                        NarrowLimit(max_identifier_bytes,
                                    mojom::kMaxToolHandleIdBytes));
  }
  if (job.audio_extract) {
    return IsIdentifier(job.audio_extract->input_handle,
                        NarrowLimit(max_identifier_bytes,
                                    mojom::kMaxToolHandleIdBytes)) &&
           IsIdentifier(job.audio_extract->output_handle,
                        NarrowLimit(max_identifier_bytes,
                                    mojom::kMaxToolHandleIdBytes)) &&
           IsIdentifier(job.audio_extract->preset_id,
                        NarrowLimit(max_identifier_bytes,
                                    mojom::kMaxToolPresetIdBytes));
  }
  if (job.frame_sample) {
    return IsIdentifier(job.frame_sample->input_handle,
                        NarrowLimit(max_identifier_bytes,
                                    mojom::kMaxToolHandleIdBytes)) &&
           IsIdentifier(job.frame_sample->output_handle,
                        NarrowLimit(max_identifier_bytes,
                                    mojom::kMaxToolHandleIdBytes)) &&
           IsIdentifier(job.frame_sample->preset_id,
                        NarrowLimit(max_identifier_bytes,
                                    mojom::kMaxToolPresetIdBytes)) &&
           job.frame_sample->max_frames > 0u;
  }
  if (job.transcode) {
    return IsIdentifier(job.transcode->input_handle,
                        NarrowLimit(max_identifier_bytes,
                                    mojom::kMaxToolHandleIdBytes)) &&
           IsIdentifier(job.transcode->output_handle,
                        NarrowLimit(max_identifier_bytes,
                                    mojom::kMaxToolHandleIdBytes)) &&
           IsIdentifier(job.transcode->preset_id,
                        NarrowLimit(max_identifier_bytes,
                                    mojom::kMaxToolPresetIdBytes));
  }
  return job.signed_wasm &&
         IsIdentifier(job.signed_wasm->entrypoint_id,
                      NarrowLimit(max_identifier_bytes,
                                  mojom::kMaxToolEntrypointIdBytes));
}

bool IsValidCoreToolResult(const mojom::ToolEffectResult& result,
                           const mojom::ToolJobEffect& job,
                           size_t max_identifier_bytes,
                           size_t max_effect_bytes) {
  if (!job.budget || result.job_id != job.job_id ||
      result.progress.size() > mojom::kMaxToolProgressEvents ||
      result.chunks.size() > job.budget->max_output_chunks ||
      result.chunks.size() > mojom::kMaxToolOutputChunks ||
      (result.status == mojom::ToolTerminalStatus::kCompleted) !=
          static_cast<bool>(result.success)) {
    return false;
  }

  bool have_progress_sequence = false;
  uint32_t last_progress_sequence = 0u;
  for (const auto& progress : result.progress) {
    if (!progress || progress->job_id != job.job_id ||
        progress->progress_basis_points > 10000u ||
        (have_progress_sequence &&
         progress->sequence <= last_progress_sequence)) {
      return false;
    }
    have_progress_sequence = true;
    last_progress_sequence = progress->sequence;
  }

  size_t output_bytes = 0u;
  bool have_chunk_sequence = false;
  uint32_t last_chunk_sequence = 0u;
  for (const auto& chunk : result.chunks) {
    if (!chunk ||
        !ValidateChunk(*chunk, job.job_id,
                       static_cast<size_t>(mojom::kMaxToolOutputChunkBytes)) ||
        (have_chunk_sequence && chunk->sequence <= last_chunk_sequence) ||
        !AddWithin(ChunkBytes(*chunk), max_effect_bytes, &output_bytes)) {
      return false;
    }
    have_chunk_sequence = true;
    last_chunk_sequence = chunk->sequence;
  }

  if (result.success &&
      !ValidateSuccess(*result.success, job, max_identifier_bytes,
                       max_effect_bytes, &output_bytes)) {
    return false;
  }
  return output_bytes <= job.budget->max_output_bytes;
}

}  // namespace taffy
