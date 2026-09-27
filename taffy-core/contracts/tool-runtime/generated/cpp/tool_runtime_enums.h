// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Generated from schema/contract.json. Do not edit.
// Contract tool_runtime 2.4.

#ifndef TAFFY_CONTRACTS_TOOL_RUNTIME_GENERATED_CPP_TOOL_RUNTIME_ENUMS_H_
#define TAFFY_CONTRACTS_TOOL_RUNTIME_GENERATED_CPP_TOOL_RUNTIME_ENUMS_H_

#include <stdint.h>

#include <optional>

#include "taffy/contracts/tool-runtime/generated/mojom/tool_runtime.mojom-shared.h"

// Closed-enumeration decoders for this contract. Every enumeration here is
// closed: a wire integer that names no member is not a member, and these return
// std::nullopt for it instead of forming an out-of-range enumerator, which is
// undefined behaviour and, at a trust seam, a fail-open. Any caller holding an
// integer must come through here.

namespace taffy::tool_runtime::wire {

// Compile-time runtime families with separate process sandboxes.
constexpr std::optional<mojom::ToolRuntimeKind>
ToolRuntimeKindFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::ToolRuntimeKind::kPython;
    case 1:
      return mojom::ToolRuntimeKind::kLocalModel;
    case 2:
      return mojom::ToolRuntimeKind::kMedia;
    case 3:
      return mojom::ToolRuntimeKind::kWasm;
    default:
      return std::nullopt;
  }
}

// Closed operations; workers never receive command lines or executable source.
constexpr std::optional<mojom::ToolOperation>
ToolOperationFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::ToolOperation::kRunBundledPythonModule;
    case 1:
      return mojom::ToolOperation::kGenerateLocalModel;
    case 2:
      return mojom::ToolOperation::kProbeMedia;
    case 3:
      return mojom::ToolOperation::kExtractAudio;
    case 4:
      return mojom::ToolOperation::kSampleFrames;
    case 5:
      return mojom::ToolOperation::kTranscodePreset;
    case 6:
      return mojom::ToolOperation::kRunSignedWasmTransform;
    case 7:
      return mojom::ToolOperation::kEmbedLocalModel;
    default:
      return std::nullopt;
  }
}

// Closed on-device model artifact formats the browser knows how to open; a
// worker never names a format the browser did not register.
constexpr std::optional<mojom::ToolModelArtifactKind>
ToolModelArtifactKindFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::ToolModelArtifactKind::kLitertTflite;
    case 1:
      return mojom::ToolModelArtifactKind::kOnnxRuntime;
    case 2:
      return mojom::ToolModelArtifactKind::kGguf;
    default:
      return std::nullopt;
  }
}

// Closed ways bounded job input reaches a worker. The browser chooses from the
// declared length; a worker opens nothing itself.
constexpr std::optional<mojom::ToolInputTransport>
ToolInputTransportFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::ToolInputTransport::kInline;
    case 1:
      return mojom::ToolInputTransport::kSharedMemory;
    default:
      return std::nullopt;
  }
}

// Closed ways bounded job output leaves a worker, decided by the browser before
// the job starts.
constexpr std::optional<mojom::ToolOutputTransport>
ToolOutputTransportFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::ToolOutputTransport::kInlineChunks;
    case 1:
      return mojom::ToolOutputTransport::kStreamChunks;
    case 2:
      return mojom::ToolOutputTransport::kDataPipe;
    default:
      return std::nullopt;
  }
}

// Closed element encoding of an embedding vector, stated so two processes never
// disagree about width or byte order.
constexpr std::optional<mojom::EmbeddingElementKind>
EmbeddingElementKindFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::EmbeddingElementKind::kFloat32Le;
    default:
      return std::nullopt;
  }
}

// Closed streaming output representations.
constexpr std::optional<mojom::ToolChunkKind>
ToolChunkKindFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::ToolChunkKind::kTextUtf8;
    case 1:
      return mojom::ToolChunkKind::kBinary;
    default:
      return std::nullopt;
  }
}

// Closed local-model terminal reasons that do not expose provider details.
constexpr std::optional<mojom::LocalModelFinishReason>
LocalModelFinishReasonFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::LocalModelFinishReason::kStop;
    case 1:
      return mojom::LocalModelFinishReason::kTokenLimit;
    default:
      return std::nullopt;
  }
}

// Closed content-free worker terminal outcomes.
constexpr std::optional<mojom::ToolTerminalStatus>
ToolTerminalStatusFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::ToolTerminalStatus::kCompleted;
    case 1:
      return mojom::ToolTerminalStatus::kCancelled;
    case 2:
      return mojom::ToolTerminalStatus::kDeadlineExceeded;
    case 3:
      return mojom::ToolTerminalStatus::kResourceLimit;
    case 4:
      return mojom::ToolTerminalStatus::kRuntimeCrashed;
    case 5:
      return mojom::ToolTerminalStatus::kInvalidInput;
    case 6:
      return mojom::ToolTerminalStatus::kUnsupported;
    case 7:
      return mojom::ToolTerminalStatus::kOutcomeUnknown;
    case 8:
      return mojom::ToolTerminalStatus::kModelArtifactMissing;
    case 9:
      return mojom::ToolTerminalStatus::kModelArtifactIncompatible;
    case 10:
      return mojom::ToolTerminalStatus::kLocalRuntimeUnavailable;
    default:
      return std::nullopt;
  }
}

// Immediate broker admission for a bounded tool job.
constexpr std::optional<mojom::ToolAdmissionStatus>
ToolAdmissionStatusFromWire(uint32_t value) {
  switch (value) {
    case 0:
      return mojom::ToolAdmissionStatus::kAccepted;
    case 1:
      return mojom::ToolAdmissionStatus::kUnsupported;
    case 2:
      return mojom::ToolAdmissionStatus::kInvalidJob;
    case 3:
      return mojom::ToolAdmissionStatus::kBackpressure;
    case 4:
      return mojom::ToolAdmissionStatus::kDeadlineExceeded;
    case 5:
      return mojom::ToolAdmissionStatus::kModelArtifactMissing;
    case 6:
      return mojom::ToolAdmissionStatus::kModelArtifactIncompatible;
    case 7:
      return mojom::ToolAdmissionStatus::kLocalRuntimeUnavailable;
    default:
      return std::nullopt;
  }
}

}  // namespace taffy::tool_runtime::wire

#endif  // TAFFY_CONTRACTS_TOOL_RUNTIME_GENERATED_CPP_TOOL_RUNTIME_ENUMS_H_
