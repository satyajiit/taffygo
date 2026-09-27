// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/services/tool-runtime/supervisor/tool_job_resources.h"

#include <algorithm>
#include <array>
#include <utility>
#include <vector>

#include "base/containers/span.h"
#include "base/memory/read_only_shared_memory_region.h"
#include "crypto/hash.h"
#include "taffy/contracts/tool-runtime/generated/mojom/tool_runtime.mojom.h"
#include "taffy/contracts/tool-runtime/tool_runtime_service.mojom.h"

namespace taffy::tool_job_resources {
namespace {

namespace runtime = tool_runtime::mojom;

// The contract's `bytes32`. Mojo carries a fixed-length array as a vector and
// checks the length when the message is serialized, so every producer here
// writes exactly this many bytes rather than relying on that check.
constexpr size_t kDigestBytes = 32u;

// The bytes a job carries inside its own argument record, before any decision
// about how they should travel. Every other operation names a resource rather
// than carrying one, so its inline input is empty by construction.
std::vector<uint8_t>* InlineInput(runtime::ToolJob* job) {
  if (job->bundled_python) {
    return &job->bundled_python->input;
  }
  if (job->local_model) {
    return &job->local_model->prompt_utf8;
  }
  if (job->local_embedding) {
    return &job->local_embedding->input_utf8;
  }
  if (job->signed_wasm) {
    return &job->signed_wasm->input;
  }
  return nullptr;
}

runtime::ToolModelArtifact* Artifact(runtime::ToolJob* job) {
  if (job->local_model) {
    return job->local_model->model.get();
  }
  if (job->local_embedding) {
    return job->local_embedding->model.get();
  }
  return nullptr;
}

runtime::ToolOutputTransport OutputTransportFor(const runtime::ToolJob& job,
                                                bool streaming_available) {
  // Generation is the one operation whose value arrives incrementally, so it
  // is the one that streams. Everything else produces a bounded result the
  // reducer has no use for a prefix of.
  //
  // DATA_PIPE is a declared transport this broker does not yet select, in the
  // same way the WASM runtime is a declared family it does not yet start. The
  // browser side of a pipe is a drain with its own bounds and its own failure
  // modes, and selecting the transport before that exists would mean handing a
  // worker a producer nobody reads.
  if (job.operation_kind == runtime::ToolOperation::kGenerateLocalModel &&
      streaming_available) {
    return runtime::ToolOutputTransport::kStreamChunks;
  }
  return runtime::ToolOutputTransport::kInlineChunks;
}

BindResult OpenArtifact(const ModelArtifactPort& port,
                        const std::string& model_id,
                        const std::string& model_revision,
                        bool adapter,
                        ModelArtifactSource* resolved) {
  if (port.is_null() ||
      port.Run(model_id, model_revision, adapter, resolved) ==
          ModelArtifactResolution::kMissing) {
    return BindResult::kModelArtifactMissing;
  }
  if (!resolved->file.IsValid() || resolved->digest.size() != kDigestBytes ||
      resolved->byte_length == 0u ||
      resolved->byte_length > runtime::kMaxModelArtifactBytes) {
    return BindResult::kModelArtifactIncompatible;
  }
  return BindResult::kBound;
}

bool Matches(const ModelArtifactSource& resolved,
             uint64_t expected_bytes,
             const std::vector<uint8_t>& expected_digest,
             runtime::ToolModelArtifactKind expected_kind) {
  return resolved.byte_length == expected_bytes &&
         resolved.digest == expected_digest && resolved.kind == expected_kind;
}

BindResult BindArtifact(const ModelArtifactPort& port,
                        runtime::ToolJob* job,
                        runtime::ToolJobResources* resources) {
  runtime::ToolModelArtifact* const artifact = Artifact(job);
  if (!artifact) {
    return BindResult::kBound;
  }
  ModelArtifactSource model;
  const BindResult model_result =
      OpenArtifact(port, artifact->model_id, artifact->model_revision, false,
                   &model);
  if (model_result != BindResult::kBound) {
    return model_result;
  }
  if (!Matches(model, artifact->artifact_bytes, artifact->artifact_digest,
               artifact->kind)) {
    return BindResult::kModelArtifactIncompatible;
  }
  resources->model_artifact = std::move(model.file);
  if (artifact->adapter_id.empty()) {
    return artifact->adapter_bytes == 0u &&
                   artifact->adapter_digest.size() == kDigestBytes &&
                   std::ranges::all_of(
                       artifact->adapter_digest,
                       [](uint8_t byte) { return byte == 0u; })
               ? BindResult::kBound
               : BindResult::kModelArtifactIncompatible;
  }
  ModelArtifactSource adapter;
  const BindResult adapter_result =
      OpenArtifact(port, artifact->adapter_id, artifact->model_revision, true,
                   &adapter);
  if (adapter_result != BindResult::kBound) {
    return adapter_result;
  }
  if (!Matches(adapter, artifact->adapter_bytes, artifact->adapter_digest,
               artifact->kind)) {
    return BindResult::kModelArtifactIncompatible;
  }
  resources->model_adapter = std::move(adapter.file);
  return BindResult::kBound;
}

// The archive the job's interpreter imports from, and the refusal that keeps
// the declaration and the descriptor from ever disagreeing.
//
// A job that is not running an interpreter keeps the empty declaration the
// conversion gave it and receives no descriptor. A job that is, and for which
// a library register exists, gets both or neither: a Python worker handed an
// archive whose length and digest are not the ones its job declares cannot
// tell what it is importing, and half of this pair is worse than none of it.
bool BindPythonLibrary(const PythonLibraryPort& port,
                       runtime::ToolJob* job,
                       runtime::ToolJobResources* resources) {
  // A job the conversion did not build. It has no complete declaration to
  // narrow, so there is nothing here to be consistent with.
  if (!job->python_library) {
    return false;
  }
  if (!job->bundled_python || port.is_null()) {
    return true;
  }
  PythonLibrarySource resolved;
  if (!port.Run(&resolved) || !resolved.file.IsValid() ||
      resolved.digest.size() != kDigestBytes || resolved.byte_length == 0u ||
      resolved.byte_length > runtime::kMaxPythonLibraryBytes ||
      resolved.library_id.empty() ||
      resolved.library_id.size() > runtime::kMaxPythonLibraryIdBytes ||
      resolved.library_version.size() >
          runtime::kMaxPythonLibraryVersionBytes) {
    return false;
  }
  job->python_library = runtime::ToolPythonLibrary::New(
      std::move(resolved.library_id), std::move(resolved.library_version),
      resolved.byte_length, std::move(resolved.digest));
  resources->python_library = std::move(resolved.file);
  return true;
}

bool BindMediaHandles(ToolHandleBroker* broker,
                      const runtime::ToolJob& job,
                      runtime::ToolJobResources* resources) {
  const std::string* input = nullptr;
  const std::string* output = nullptr;
  if (job.media_probe) {
    input = &job.media_probe->input_handle;
  } else if (job.audio_extract) {
    input = &job.audio_extract->input_handle;
    output = &job.audio_extract->output_handle;
  } else if (job.frame_sample) {
    input = &job.frame_sample->input_handle;
    output = &job.frame_sample->output_handle;
  } else if (job.transcode) {
    input = &job.transcode->input_handle;
    output = &job.transcode->output_handle;
  }
  if (!input) {
    return true;
  }
  base::File read =
      broker->Resolve(job.job_id, *input, ToolHandleBroker::Mode::kRead);
  if (!read.IsValid()) {
    return false;
  }
  resources->media_input = std::move(read);
  if (!output) {
    return true;
  }
  base::File write =
      broker->Resolve(job.job_id, *output, ToolHandleBroker::Mode::kWrite);
  if (!write.IsValid()) {
    return false;
  }
  resources->media_output = std::move(write);
  return true;
}

bool BindBulkInput(runtime::ToolJob* job, runtime::ToolJobResources* resources) {
  if (job->input_payload->transport !=
      runtime::ToolInputTransport::kSharedMemory) {
    return true;
  }
  std::vector<uint8_t>* const inline_input = InlineInput(job);
  if (!inline_input) {
    return false;
  }
  base::MappedReadOnlyRegion mapped =
      base::ReadOnlySharedMemoryRegion::Create(inline_input->size());
  if (!mapped.IsValid()) {
    return false;
  }
  base::span<uint8_t> destination(mapped.mapping);
  destination.copy_from(base::span<const uint8_t>(*inline_input));
  resources->bulk_input = std::move(mapped.region);
  // The two carriers are alternatives, never a redundant pair: leaving the
  // bytes in the record as well would mean two copies whose disagreement no
  // reader could detect, and would double a payload the bound exists to cap.
  inline_input->clear();
  return true;
}

}  // namespace

ModelArtifactSource::ModelArtifactSource() = default;
ModelArtifactSource::ModelArtifactSource(ModelArtifactSource&&) = default;
ModelArtifactSource& ModelArtifactSource::operator=(ModelArtifactSource&&) =
    default;
ModelArtifactSource::~ModelArtifactSource() = default;

PythonLibrarySource::PythonLibrarySource() = default;
PythonLibrarySource::PythonLibrarySource(PythonLibrarySource&&) = default;
PythonLibrarySource& PythonLibrarySource::operator=(PythonLibrarySource&&) =
    default;
PythonLibrarySource::~PythonLibrarySource() = default;

void PlanPayloads(bool streaming_available, runtime::ToolJob* job) {
  const std::vector<uint8_t>* const inline_input = InlineInput(job);
  const size_t length = inline_input ? inline_input->size() : 0u;
  std::vector<uint8_t> digest(kDigestBytes, 0u);
  if (length > 0u) {
    const std::array<uint8_t, kDigestBytes> computed =
        crypto::hash::Sha256(base::span<const uint8_t>(*inline_input));
    digest.assign(computed.begin(), computed.end());
  }
  job->input_payload = runtime::ToolPayloadInput::New(
      length > runtime::kMaxInlinePayloadBytes
          ? runtime::ToolInputTransport::kSharedMemory
          : runtime::ToolInputTransport::kInline,
      static_cast<uint64_t>(length), std::move(digest));
  job->output_payload = runtime::ToolPayloadOutput::New(
      OutputTransportFor(*job, streaming_available),
      job->budget->max_output_bytes);
}

BindResult Bind(const ModelArtifactPort& open_model_artifact,
                const PythonLibraryPort& open_python_library,
                ToolHandleBroker* broker,
                runtime::ToolJob* job,
                runtime::ToolJobResourcesPtr* resources) {
  auto bound = runtime::ToolJobResources::New();
  const BindResult artifact =
      BindArtifact(open_model_artifact, job, bound.get());
  if (artifact != BindResult::kBound) {
    return artifact;
  }
  if (!BindPythonLibrary(open_python_library, job, bound.get()) ||
      !BindMediaHandles(broker, *job, bound.get()) ||
      !BindBulkInput(job, bound.get())) {
    return BindResult::kInvalidJob;
  }
  *resources = std::move(bound);
  return BindResult::kBound;
}

}  // namespace taffy::tool_job_resources
