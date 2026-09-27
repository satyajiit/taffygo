// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <limits>
#include <optional>
#include <string>
#include <string_view>

#include "base/containers/span.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "taffy/common/public/bip_result.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/services/core/rust_core_task_effect.h"
#include "taffy/services/core/rust_core_task_effect_terminal_internal.h"

namespace taffy::core_service_internal {
namespace {

namespace mojom = core_service::mojom;
namespace bridge = core_bridge;

bridge::BridgeTaskTerminalOperation BridgeOperation(
    const mojom::OperationEnvelope& input) {
  bridge::BridgeTaskTerminalOperation out;
  out.operation_id = input.operation_id;
  out.service_generation = input.service_generation;
  out.task_revision = input.task_revision;
  out.deadline_monotonic_ms = input.deadline_monotonic_ms;
  out.idempotency_key = input.idempotency_key;
  return out;
}

std::string_view EffectActionId(const mojom::TaskEffectBinding& effect) {
  if (effect.policy) {
    return effect.policy->action_id;
  }
  if (effect.action) {
    return effect.action->action_id;
  }
  if (effect.reconcile) {
    return effect.reconcile->action_id;
  }
  if (effect.library_tool) {
    return effect.library_tool->action_id;
  }
  if (effect.memory_tool) {
    return effect.memory_tool->action_id;
  }
  if (effect.tool_job) {
    return effect.tool_job->action_id;
  }
  return {};
}

bool SameOperation(const mojom::OperationEnvelope& left,
                   const mojom::OperationEnvelope& right) {
  return left.operation_id == right.operation_id &&
         left.service_generation == right.service_generation &&
         left.task_revision == right.task_revision &&
         left.deadline_monotonic_ms == right.deadline_monotonic_ms &&
         left.idempotency_key == right.idempotency_key;
}

// Copies the provider's answer out of one CALL_MODEL completion.
//
// `false` refuses the whole terminal, and it is reached only by a result that
// is not this call's to read. `kModelRequest` is checked rather than assumed: a
// storage or tool result carried under a model call's identity is another
// effect family's completion wearing this one's name, and the core would read
// it as the provider's answer.
//
// An absent result is not that. A model call the browser reports as succeeded
// and brought nothing back crosses with the flag clear, because the core has a
// name for it — a turn gap of `Unreadable`, money spent and no reply to read —
// and refusing it here would instead hand `RustCore` an empty batch, which is a
// torn-down core rather than a recorded fact.
bool CopyModelTerminal(const mojom::TaskEffectBinding& effect,
                       const mojom::TaskEffectCompletion& completion,
                       bridge::BridgeTaskTerminal& out) {
  if (!completion.effect_result) {
    return true;
  }
  const auto& result = completion.effect_result;
  if (!result->operation ||
      !SameOperation(*effect.operation, *result->operation) ||
      result->effect_id != effect.effect_id ||
      result->kind != mojom::EffectKind::kModelRequest || !result->model) {
    return false;
  }
  if (result->status != mojom::EffectStatus::kCompleted) {
    if ((completion.status != mojom::TaskEffectCompletionStatus::kRefused &&
         completion.status !=
             mojom::TaskEffectCompletionStatus::kUnavailable) ||
        !result->model->failure || !result->model->completion.empty() ||
        result->model->streamed) {
      return false;
    }
    out.has_model_failure = true;
    out.model_completion_model_id = result->model->model_id;
    out.model_failure_class =
        static_cast<uint8_t>(result->model->failure->error_class);
    out.has_model_retry_after = result->model->failure->has_retry_after;
    out.model_retry_after_millis = result->model->failure->retry_after_millis;
    return out.has_model_retry_after || out.model_retry_after_millis == 0u;
  }
  if (completion.status != mojom::TaskEffectCompletionStatus::kSucceeded ||
      result->model->failure) {
    return false;
  }
  out.has_model_completion = true;
  out.model_completion_model_id = result->model->model_id;
  out.model_completion_streamed = result->model->streamed;
  if (out.model_completion_streamed && !result->model->completion.empty()) {
    return false;
  }
  // The bytes cross unread, and only the bytes. `ModelEffectResult` also
  // reports the units the browser counted, and they are deliberately left
  // behind: `read_model_reply` takes the usage out of the reply body itself,
  // so a second pair of numbers arriving beside it would be a second opinion
  // about what one call spent.
  out.model_completion.reserve(result->model->completion.size());
  for (uint8_t byte : result->model->completion) {
    out.model_completion.push_back(byte);
  }
  return true;
}

bool CopyDiscoveryBootstrapCompletion(
    const mojom::TaskEffectBinding& effect,
    const mojom::TaskEffectCompletion& completion,
    bridge::BridgeTaskTerminal& out) {
  if (!effect.discovery_bootstrap || !completion.effect_result ||
      !completion.effect_result->operation ||
      !SameOperation(*effect.operation, *completion.effect_result->operation) ||
      completion.effect_result->effect_id != effect.effect_id ||
      completion.effect_result->kind != mojom::EffectKind::kBrowserAction ||
      completion.effect_result->status != mojom::EffectStatus::kCompleted ||
      !completion.effect_result->browser_action ||
      completion.effect_result->browser_action->outcome !=
          mojom::BrowserActionOutcome::kCompleted ||
      completion.effect_result->browser_action->dispatch_id ||
      completion.effect_result->browser_action->discovered_source ||
      completion.effect_result->browser_action->task_tab ||
      completion.effect_result->browser_action->task_download ||
      completion.effect_result->browser_action->task_store ||
      !completion.effect_result->browser_action->discovery_tab_id ||
      !completion.effect_result->browser_action->browser_session_id ||
      !BoundedIdentifier(
          *completion.effect_result->browser_action->discovery_tab_id) ||
      !BoundedIdentifier(
          *completion.effect_result->browser_action->browser_session_id) ||
      *completion.effect_result->browser_action->browser_session_id !=
          effect.discovery_bootstrap->browser_session_id) {
    return false;
  }
  out.has_discovery_tab = true;
  out.discovery_tab_id =
      *completion.effect_result->browser_action->discovery_tab_id;
  out.discovery_browser_session_id =
      *completion.effect_result->browser_action->browser_session_id;
  return true;
}

bool CopyMediaProbeCompletion(const mojom::TaskEffectBinding& effect,
                              const mojom::TaskEffectCompletion& completion,
                              const mojom::ToolJobEffect& job,
                              bridge::BridgeTaskTerminal& out) {
  if (!completion.effect_result || !completion.effect_result->operation ||
      !SameOperation(*effect.operation, *completion.effect_result->operation) ||
      completion.effect_result->effect_id != effect.effect_id ||
      completion.effect_result->kind != mojom::EffectKind::kToolJob ||
      completion.effect_result->status != mojom::EffectStatus::kCompleted ||
      !completion.effect_result->tool ||
      completion.effect_result->tool->job_id != job.job_id ||
      completion.effect_result->tool->status !=
          mojom::ToolTerminalStatus::kCompleted ||
      !completion.effect_result->tool->progress.empty() ||
      !completion.effect_result->tool->chunks.empty() ||
      completion.effect_result->tool->streamed_chunks != 0u ||
      !completion.effect_result->tool->success) {
    return false;
  }
  const auto& success = *completion.effect_result->tool->success;
  if (success.operation_kind != mojom::ToolOperation::kProbeMedia ||
      !success.media_probe || success.bundled_python || success.local_model ||
      success.audio_extract || success.frame_sample || success.transcode ||
      success.signed_wasm || success.local_embedding) {
    return false;
  }
  const auto& probe = *success.media_probe;
  if (probe.duration_ms > 600'000u || probe.audio_streams > 64u ||
      probe.video_streams > 64u || probe.width > 1920u ||
      probe.height > 1080u ||
      (probe.audio_streams == 0u && probe.video_streams == 0u) ||
      (probe.video_streams == 0u &&
       (probe.width != 0u || probe.height != 0u))) {
    return false;
  }
  out.has_media_probe = true;
  out.media_probe_duration_ms = probe.duration_ms;
  out.media_probe_audio_streams = probe.audio_streams;
  out.media_probe_video_streams = probe.video_streams;
  out.media_probe_width_px = probe.width;
  out.media_probe_height_px = probe.height;
  return true;
}

// Accepts only a browser-verified, content-free receipt for the exact typed
// job. Worker bytes never return to the core process; a probe's five bounded
// scalars cross only after the browser reconstructed the typed result.
bool CopyToolCompletion(const mojom::TaskEffectBinding& effect,
                        const mojom::TaskEffectCompletion& completion,
                        bridge::BridgeTaskTerminal& out) {
  if (!effect.tool_job || !effect.tool_job->job ||
      !effect.tool_job->job->budget || !completion.tool_output) {
    return false;
  }
  const auto& job = *effect.tool_job->job;
  const auto& receipt = *completion.tool_output;
  if (receipt.digest.size() != 32u || receipt.byte_count == 0u ||
      receipt.byte_count > job.budget->max_output_bytes ||
      receipt.chunk_count != 1u) {
    return false;
  }
  std::optional<mojom::TaskArtifactKind> expected_kind;
  if (job.runtime == mojom::ToolRuntimeKind::kPython && job.bundled_python &&
      job.operation_kind == mojom::ToolOperation::kRunBundledPythonModule) {
    if (job.bundled_python->entrypoint_id == "document.build") {
      expected_kind = mojom::TaskArtifactKind::kDocx;
    } else if (job.bundled_python->entrypoint_id == "spreadsheet.build") {
      expected_kind = mojom::TaskArtifactKind::kXlsx;
    } else {
      return false;
    }
  } else if (job.runtime == mojom::ToolRuntimeKind::kMedia) {
    switch (job.operation_kind) {
      case mojom::ToolOperation::kProbeMedia:
        if (receipt.byte_count != 24u || receipt.artifact ||
            !CopyMediaProbeCompletion(effect, completion, job, out)) {
          return false;
        }
        break;
      case mojom::ToolOperation::kExtractAudio:
      case mojom::ToolOperation::kTranscodePreset:
        if (completion.effect_result) {
          return false;
        }
        expected_kind = mojom::TaskArtifactKind::kWaveAudio;
        break;
      case mojom::ToolOperation::kSampleFrames:
        if (completion.effect_result) {
          return false;
        }
        expected_kind = mojom::TaskArtifactKind::kFrameArchive;
        break;
      case mojom::ToolOperation::kRunBundledPythonModule:
      case mojom::ToolOperation::kGenerateLocalModel:
      case mojom::ToolOperation::kRunSignedWasmTransform:
      case mojom::ToolOperation::kEmbedLocalModel:
        return false;
    }
  } else {
    return false;
  }
  if (job.runtime == mojom::ToolRuntimeKind::kPython &&
      completion.effect_result) {
    return false;
  }
  if (expected_kind &&
      (!receipt.artifact || receipt.artifact->kind != *expected_kind ||
       receipt.artifact->artifact_id !=
           "artifact-" + effect.tool_job->action_id)) {
    return false;
  }
  out.has_tool_output = true;
  out.tool_output_digest = base::ToLowerASCII(base::HexEncode(receipt.digest));
  out.tool_output_bytes = receipt.byte_count;
  out.tool_output_chunks = receipt.chunk_count;
  return true;
}

bool CopyReconciledActionCompletion(
    const mojom::TaskEffectBinding& effect,
    const mojom::TaskEffectCompletion& completion,
    bridge::BridgeTaskTerminal& out) {
  if (!effect.reconcile || completion.effect_result || completion.tool_output) {
    return false;
  }
  if (!completion.reconciled_action_result) {
    return completion.status ==
           mojom::TaskEffectCompletionStatus::kOutcomeUnknown;
  }
  const uint32_t wire_code = completion.reconciled_action_result->result_code;
  if (completion.status != mojom::TaskEffectCompletionStatus::kSucceeded ||
      wire_code > std::numeric_limits<uint8_t>::max() ||
      !IsKnownActionResultCode(static_cast<ActionResultCode>(wire_code))) {
    return false;
  }
  out.has_reconciled_action_result = true;
  out.reconciled_action_result_code = wire_code;
  return true;
}

}  // namespace

bool BoundedIdentifier(std::string_view value) {
  return !value.empty() && value.size() <= mojom::kMaxIdentifierBytes;
}

bool SameOperationEnvelope(const mojom::OperationEnvelope& left,
                           const mojom::OperationEnvelope& right) {
  return left.operation_id == right.operation_id &&
         left.service_generation == right.service_generation &&
         left.task_revision == right.task_revision &&
         left.deadline_monotonic_ms == right.deadline_monotonic_ms &&
         left.idempotency_key == right.idempotency_key;
}

bridge::BridgeTaskTerminal TaskTerminalBase(
    const mojom::TaskEffectBinding& effect,
    uint8_t status) {
  bridge::BridgeTaskTerminal out;
  out.operation = BridgeOperation(*effect.operation);
  out.effect_id = effect.effect_id;
  out.task_id = effect.task_id;
  out.action_id = std::string(EffectActionId(effect));
  out.kind = static_cast<uint8_t>(effect.kind);
  out.status = status;
  out.has_denial_code = false;
  out.denial_code = 0u;
  return out;
}

std::optional<bridge::BridgeTaskTerminal> ToBridgeTaskTerminal(
    const mojom::TaskEffectBinding& effect,
    const mojom::TaskEffectCompletion& completion) {
  if (!effect.operation || !completion.operation ||
      !SameOperation(*effect.operation, *completion.operation) ||
      completion.effect_id != effect.effect_id ||
      completion.task_id != effect.task_id || completion.kind != effect.kind) {
    return std::nullopt;
  }
  auto out = TaskTerminalBase(effect, static_cast<uint8_t>(completion.status));
  if (effect.kind == mojom::TaskReducerEffectKind::kCallModel) {
    if (completion.tool_output || completion.reconciled_action_result ||
        !CopyModelTerminal(effect, completion, out)) {
      return std::nullopt;
    }
    return out;
  }
  if (effect.kind == mojom::TaskReducerEffectKind::kReconcileAction) {
    return CopyReconciledActionCompletion(effect, completion, out)
               ? std::make_optional(std::move(out))
               : std::nullopt;
  }
  if (completion.reconciled_action_result) {
    return std::nullopt;
  }
  if (completion.status == mojom::TaskEffectCompletionStatus::kSucceeded) {
    if (effect.kind == mojom::TaskReducerEffectKind::kDispatchAction) {
      if (!CopyTaskActionCompletion(effect, completion, out)) {
        return std::nullopt;
      }
    } else if (effect.kind ==
               mojom::TaskReducerEffectKind::kPrepareDiscoveryTab) {
      if (!CopyDiscoveryBootstrapCompletion(effect, completion, out)) {
        return std::nullopt;
      }
    } else if (effect.kind == mojom::TaskReducerEffectKind::kRunToolJob) {
      if (!CopyToolCompletion(effect, completion, out)) {
        return std::nullopt;
      }
    } else if (completion.effect_result || completion.tool_output) {
      return std::nullopt;
    }
    // A completion that did not succeed carries terminal facts only. The one
    // exception is a refusal that names the code it was refused with — a read
    // that found the page had moved, or a press whose control was no longer
    // the control it was authorized against. Those two copiers are also what
    // check that nothing else rode along with the word.
  } else if (completion.tool_output ||
             (completion.effect_result &&
              !CopyRefusedObservationTerminal(effect, completion, out) &&
              !CopyRefusedActionTerminal(effect, completion, out))) {
    return std::nullopt;
  }
  return out;
}

}  // namespace taffy::core_service_internal
