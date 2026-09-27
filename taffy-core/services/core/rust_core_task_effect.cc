// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/services/core/rust_core_task_effect.h"

#include <stddef.h>
#include <stdint.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/services/core/rust_core_task_effect_projection.h"
#include "taffy/services/core/rust_core_task_effect_records.h"

namespace taffy::core_service_internal {
namespace {

namespace mojom = core_service::mojom;
namespace bridge = core_bridge;


mojom::ToolJobEffectPtr PythonJob(const bridge::BridgeTaskEffect& input,
                                  mojom::ToolRuntimeKind runtime) {
  if (!input.has_python_job) {
    return nullptr;
  }
  if (runtime != mojom::ToolRuntimeKind::kPython ||
      !ValidIdentifier(input.python_entrypoint) ||
      input.python_input.empty() ||
      input.python_input.size() > 262144u) {
    return nullptr;
  }
  auto job = mojom::ToolJobEffect::New();
  job->job_id = std::string(input.job_id);
  job->runtime = runtime;
  job->tool_id = "python.execute";
  job->tool_version = "1";
  job->operation_kind = mojom::ToolOperation::kRunBundledPythonModule;
  job->budget = mojom::ToolResourceBudget::New(
      262144u, 1024u * 1024u, 64u * 1024u * 1024u, 5000u, 0u, 1u);
  auto arguments = mojom::BundledPythonArguments::New();
  arguments->entrypoint_id = std::string(input.python_entrypoint);
  arguments->input.assign(input.python_input.begin(), input.python_input.end());
  job->bundled_python = std::move(arguments);
  job->task_id = std::string(input.task_id);
  return job;
}

mojom::ToolJobEffectPtr MediaJob(const bridge::BridgeTaskEffect& input,
                                 mojom::ToolRuntimeKind runtime) {
  constexpr uint64_t kMaximumMediaBytes = 16u * 1024u * 1024u;
  constexpr uint64_t kMaximumResidentBytes = 256u * 1024u * 1024u;
  constexpr char kOutputHandleMarker[] = "browser-custody-output";
  if (!input.has_media_job || runtime != mojom::ToolRuntimeKind::kMedia ||
      !ValidIdentifier(input.media_source_id) ||
      !ValidIdentifier(input.media_source_browser_session_id) ||
      input.media_source_bytes == 0u ||
      input.media_source_bytes > kMaximumMediaBytes ||
      input.media_operation > 3u) {
    return nullptr;
  }
  const auto operation = static_cast<mojom::ToolOperation>(
      static_cast<uint32_t>(mojom::ToolOperation::kProbeMedia) +
      input.media_operation);
  const bool sampling = operation == mojom::ToolOperation::kSampleFrames;
  if ((sampling &&
       (input.media_max_frames == 0u || input.media_max_frames > 12u)) ||
      (!sampling && input.media_max_frames != 0u)) {
    return nullptr;
  }

  auto job = mojom::ToolJobEffect::New();
  job->job_id = std::string(input.job_id);
  job->runtime = runtime;
  job->tool_version = "1";
  job->operation_kind = operation;
  job->budget = mojom::ToolResourceBudget::New(
      input.media_source_bytes, kMaximumMediaBytes, kMaximumResidentBytes,
      30'000u, 0u, 1u);
  switch (operation) {
    case mojom::ToolOperation::kProbeMedia:
      job->tool_id = "media.probe";
      job->media_probe = mojom::MediaProbeArguments::New(
          std::string(input.media_source_id));
      break;
    case mojom::ToolOperation::kExtractAudio:
      job->tool_id = "media.audio.extract";
      job->audio_extract = mojom::AudioExtractArguments::New(
          std::string(input.media_source_id), kOutputHandleMarker,
          "audio.wav.pcm16.v1");
      break;
    case mojom::ToolOperation::kSampleFrames:
      job->tool_id = "media.frames.sample";
      job->frame_sample = mojom::FrameSampleArguments::New(
          std::string(input.media_source_id), kOutputHandleMarker,
          "frames.png.zip.v1", input.media_max_frames);
      break;
    case mojom::ToolOperation::kTranscodePreset:
      job->tool_id = "media.transcode";
      job->transcode = mojom::TranscodeArguments::New(
          std::string(input.media_source_id), kOutputHandleMarker,
          "audio.wav.mono-pcm16.v1");
      break;
    case mojom::ToolOperation::kRunBundledPythonModule:
    case mojom::ToolOperation::kGenerateLocalModel:
    case mojom::ToolOperation::kRunSignedWasmTransform:
    case mojom::ToolOperation::kEmbedLocalModel:
      return nullptr;
  }
  job->task_id = std::string(input.task_id);
  return job;
}

}  // namespace

std::optional<mojom::TaskEffectBindingPtr> ToMojoTaskEffect(
    const bridge::BridgeTaskEffect& input) {
  // The ceiling is the LAST member of the enum, not the last one this switch
  // happens to handle. Adding a member and a case without raising it here is
  // silent in both directions: the switch compiles, and every effect of the
  // new kind is answered `nullopt`, so the binding never reaches the browser
  // and the task waits on a surface nothing was ever asked to open.
  const auto kind =
      ClosedEnum(input.kind, mojom::TaskReducerEffectKind::kRunMemoryTool);
  if (!kind || !ValidIdentifier(input.effect_id) ||
      !ValidIdentifier(input.task_id) ||
      !ValidIdentifier(input.operation.operation_id) ||
      !ValidIdentifier(input.operation.idempotency_key) ||
      input.operation.service_generation == 0u ||
      input.operation.task_revision == 0u ||
      input.operation.deadline_monotonic_ms == 0u ||
      input.ordinal >= mojom::kMaxTaskEffectsPerState) {
    return std::nullopt;
  }
  auto out = mojom::TaskEffectBinding::New();
  out->operation = ToMojoOperation(input.operation);
  out->effect_id = std::string(input.effect_id);
  out->task_id = std::string(input.task_id);
  out->ordinal = input.ordinal;
  out->kind = *kind;
  switch (*kind) {
    case mojom::TaskReducerEffectKind::kRevokeAuthority: {
      const auto reason = ClosedEnum(input.recovery_rule,
                                     mojom::TaskRevocationReason::kTabClosed);
      if (!reason) {
        return std::nullopt;
      }
      out->revocation = mojom::TaskRevocationEffect::New(*reason);
      break;
    }
    case mojom::TaskReducerEffectKind::kAskPolicy:
      out->policy = ToMojoPolicyEffect(input).value_or(nullptr);
      if (!out->policy) {
        return std::nullopt;
      }
      break;
    case mojom::TaskReducerEffectKind::kRequestApproval: {
      if (!ValidIdentifier(input.action_id) ||
          !ValidDigest(input.proposal_digest)) {
        return std::nullopt;
      }
      const auto action_class = ClosedEnum(
          input.action_class, mojom::PolicyActionClass::kProfileStoreRead);
      if (!action_class) {
        return std::nullopt;
      }
      const bool is_form =
          *action_class == mojom::PolicyActionClass::kFillField ||
          *action_class == mojom::PolicyActionClass::kSelectOption ||
          *action_class == mojom::PolicyActionClass::kToggleControl ||
          *action_class == mojom::PolicyActionClass::kSubmitForm;
      std::optional<mojom::TaskExecutableActionPtr> form_action;
      if (is_form) {
        form_action = FormApprovalAction(input);
        if (!form_action) {
          return std::nullopt;
        }
      }
      out->approval = mojom::TaskApprovalEffect::New(
          std::string(input.action_id), std::string(input.proposal_digest),
          form_action ? std::move(*form_action) : nullptr);
      break;
    }
    case mojom::TaskReducerEffectKind::kRequestPermission: {
      const auto permission =
          ClosedEnum(input.permission, mojom::PlatformPermission::kLocation);
      if (!permission || !ValidIdentifier(input.request_id) ||
          !ValidIdentifier(input.permission_browser_session_id) ||
          input.deadline_monotonic_ms == 0u || input.deadline_utc_ms == 0u) {
        return std::nullopt;
      }
      out->permission = mojom::TaskPermissionEffect::New(
          std::string(input.request_id), *permission,
          input.deadline_monotonic_ms, input.deadline_utc_ms,
          std::string(input.permission_browser_session_id));
      break;
    }
    case mojom::TaskReducerEffectKind::kDispatchAction:
      out->action = ActionEffect(input).value_or(nullptr);
      if (!out->action) {
        return std::nullopt;
      }
      break;
    case mojom::TaskReducerEffectKind::kAwaitInFlightWork: {
      const auto settlement =
          ClosedEnum(input.settlement_kind, mojom::TaskSettlementKind::kCancel);
      if (!settlement) {
        return std::nullopt;
      }
      out->settlement = mojom::TaskSettlementEffect::New(*settlement);
      break;
    }
    case mojom::TaskReducerEffectKind::kReconcileAction: {
      const auto rule = ClosedEnum(
          input.recovery_rule, mojom::TaskRecoveryRule::kNeverAutomatically);
      // Every operation kind can end with an outcome nobody could confirm,
      // so the ceiling here is the enum's own last member and not a member
      // name that was last when this was written. `kDownloadCancel` stopped
      // being last when reload, stop, history and bookmarks were added, and
      // from then on a reload that answered OUTCOME_UNKNOWN produced a
      // reconcile effect the browser could not project — which refuses the
      // whole state publication and ends the core, taking every task with it.
      const auto operation =
          ClosedEnum(input.action_operation,
                     mojom::TaskActionOperationKind::kMaxValue);
      if (!rule || !operation || !ValidIdentifier(input.action_id) ||
          !ValidIdentifier(input.dispatch_id)) {
        return std::nullopt;
      }
      out->reconcile = mojom::TaskReconcileEffect::New(
          std::string(input.action_id), *rule, std::string(input.dispatch_id),
          *operation);
      break;
    }
    case mojom::TaskReducerEffectKind::kReleaseTaskTabs:
      out->release_tabs =
          mojom::TaskReleaseTabsEffect::New(input.operation.task_revision);
      break;
    case mojom::TaskReducerEffectKind::kGenerateArtifact:
    case mojom::TaskReducerEffectKind::kExportArtifact: {
      const auto artifact =
          ClosedEnum(input.artifact_kind,
                     mojom::TaskArtifactKind::kFrameArchive);
      if (!artifact || !ValidIdentifier(input.artifact_id) ||
          input.artifact_workspace_revision == 0u) {
        return std::nullopt;
      }
      const bool exporting =
          *kind == mojom::TaskReducerEffectKind::kExportArtifact;
      if (!exporting && !input.artifact_content.empty()) {
        return std::nullopt;
      }
      const bool browser_custody_required =
          *artifact == mojom::TaskArtifactKind::kWaveAudio ||
          *artifact == mojom::TaskArtifactKind::kFrameArchive;
      const bool browser_custody_supported =
          *artifact == mojom::TaskArtifactKind::kDocx ||
          *artifact == mojom::TaskArtifactKind::kXlsx ||
          browser_custody_required;
      if (exporting &&
          ((input.artifact_content.empty() && !browser_custody_supported) ||
           (!input.artifact_content.empty() && browser_custody_required) ||
           input.artifact_content.size() >
               mojom::kMaxTaskArtifactExportBytes)) {
        return std::nullopt;
      }
      std::vector<uint8_t> content(input.artifact_content.begin(),
                                   input.artifact_content.end());
      if (*kind == mojom::TaskReducerEffectKind::kGenerateArtifact) {
        out->generate_artifact = mojom::TaskArtifactEffect::New(
            *artifact, std::string(input.artifact_id),
            input.artifact_workspace_revision, std::move(content));
      } else {
        out->export_artifact = mojom::TaskArtifactEffect::New(
            *artifact, std::string(input.artifact_id),
            input.artifact_workspace_revision, std::move(content));
      }
      break;
    }
    case mojom::TaskReducerEffectKind::kCallModel:
      // Refused whole rather than repaired. A model call is the one effect here
      // that spends a person's money, so an effect this seam could not read
      // exactly must not become a smaller one it could.
      out->model = ModelEffect(input).value_or(nullptr);
      if (!out->model) {
        return std::nullopt;
      }
      break;
    case mojom::TaskReducerEffectKind::kAwaitHandover: {
      // The window is the reducer's decision and is carried across unchanged; a
      // browser that shortened it would end a handover the person was still
      // inside. Zero is refused rather than defaulted, because a window of no
      // length is a handover that closes before it opens.
      if (!ValidIdentifier(input.handover_id) ||
          input.handover_window_ms == 0u) {
        return std::nullopt;
      }
      out->handover = mojom::TaskHandoverEffect::New(
          std::string(input.handover_id), input.handover_window_ms);
      break;
    }
    case mojom::TaskReducerEffectKind::kRunToolJob: {
      const auto runtime =
          ClosedEnum(input.tool_runtime, mojom::ToolRuntimeKind::kWasm);
      if (!runtime || !ValidIdentifier(input.action_id) ||
          !ValidIdentifier(input.job_id)) {
        return std::nullopt;
      }
      auto job = *runtime == mojom::ToolRuntimeKind::kPython
                     ? PythonJob(input, *runtime)
                     : MediaJob(input, *runtime);
      const bool python_runtime = *runtime == mojom::ToolRuntimeKind::kPython;
      const bool media_runtime = *runtime == mojom::ToolRuntimeKind::kMedia;
      if ((python_runtime || media_runtime) && !job) {
        return std::nullopt;
      }
      if (input.has_python_job != python_runtime ||
          input.has_media_job != media_runtime ||
          (!input.has_python_job &&
           (!input.python_entrypoint.empty() || !input.python_input.empty())) ||
          (!input.has_media_job &&
           (input.media_operation != 0u || !input.media_source_id.empty() ||
            !input.media_source_browser_session_id.empty() ||
            input.media_source_bytes != 0u || input.media_max_frames != 0u))) {
        return std::nullopt;
      }
      out->tool_job = mojom::TaskToolJobEffect::New(
          std::string(input.action_id), std::string(input.job_id), *runtime,
          std::move(job));
      break;
    }
    case mojom::TaskReducerEffectKind::kRunLibraryTool: {
      const auto operation =
          ClosedEnum(input.action_operation,
                     mojom::TaskActionOperationKind::kLibraryRemove);
      if (!operation || !ValidIdentifier(input.action_id) ||
          (*operation != mojom::TaskActionOperationKind::kLibrarySearch &&
           *operation != mojom::TaskActionOperationKind::kLibrarySave &&
           *operation != mojom::TaskActionOperationKind::kLibraryRemove)) {
        return std::nullopt;
      }
      out->library_tool = mojom::TaskLibraryToolEffect::New(
          std::string(input.action_id), *operation);
      break;
    }
    case mojom::TaskReducerEffectKind::kRunMemoryTool: {
      const auto operation =
          ClosedEnum(input.action_operation,
                     mojom::TaskActionOperationKind::kMemoryDelete);
      if (!operation || !ValidIdentifier(input.action_id) ||
          (*operation != mojom::TaskActionOperationKind::kMemorySearch &&
           *operation != mojom::TaskActionOperationKind::kMemorySave &&
           *operation != mojom::TaskActionOperationKind::kMemoryUpdate &&
           *operation != mojom::TaskActionOperationKind::kMemoryDelete)) {
        return std::nullopt;
      }
      out->memory_tool = mojom::TaskMemoryToolEffect::New(
          std::string(input.action_id), *operation);
      break;
    }
    case mojom::TaskReducerEffectKind::kRequestFieldValues: {
      // The form and nothing inside it. A node identity is required rather than
      // optional: an ask with no form to point at is a sheet with nothing in
      // it, and refusing it here is better than drawing one.
      if (!ValidIdentifier(input.request_id) ||
          !ValidIdentifier(input.tab_id) || !input.has_node_id ||
          !ValidIdentifier(input.node_id)) {
        return std::nullopt;
      }
      // The other fields on the page only the person can supply, asked about
      // on the same sheet (decision 0238). Each is an identity like the one
      // above and none repeats it or another: a repeated row is a sheet asking
      // twice for one value. How many a sheet may hold is the coordinator's
      // bound to apply, against the rows it actually resolves.
      std::vector<std::string> companions;
      companions.reserve(input.field_values_companion_node_ids.size());
      for (const rust::String& companion :
           input.field_values_companion_node_ids) {
        std::string id(companion);
        if (!ValidIdentifier(companion) || id == std::string(input.node_id) ||
            std::find(companions.begin(), companions.end(), id) !=
                companions.end()) {
          return std::nullopt;
        }
        companions.push_back(std::move(id));
      }
      out->field_values = mojom::TaskFieldValuesEffect::New(
          std::string(input.request_id), std::string(input.tab_id),
          std::string(input.node_id), std::move(companions));
      break;
    }
    case mojom::TaskReducerEffectKind::kPrepareDiscoveryTab:
      if (!ValidIdentifier(input.discovery_bootstrap_browser_session_id) ||
          input.discovery_bootstrap_remaining_new_source_cap == 0u ||
          input.discovery_bootstrap_remaining_new_source_cap >
              mojom::kMaxNewSourceCap) {
        return std::nullopt;
      }
      out->discovery_bootstrap = mojom::TaskDiscoveryBootstrapEffect::New(
          std::string(input.discovery_bootstrap_browser_session_id),
          input.discovery_bootstrap_remaining_new_source_cap);
      break;
  }
  return out;
}

}  // namespace taffy::core_service_internal
