// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_task_effect.h"

#include <algorithm>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include "base/strings/string_util.h"
#include "taffy/browser/core_model_effect_validation.h"
#include "taffy/browser/core_service_command_validation.h"
#include "taffy/browser/core_task_action.h"
#include "taffy/browser/core_task_effect_action.h"
#include "taffy/browser/core_task_policy.h"
#include "taffy/browser/core_tool_validation.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

bool IsIdentifier(const std::string& value, size_t maximum) {
  return !value.empty() && value.size() <= maximum &&
         std::none_of(value.begin(), value.end(), [](char character) {
           return static_cast<unsigned char>(character) < 0x20u;
         });
}

bool IsDigest(const std::string& value) {
  return value.size() == 64u &&
         std::all_of(value.begin(), value.end(), [](char character) {
           return (character >= '0' && character <= '9') ||
                  (character >= 'a' && character <= 'f');
         });
}

bool OperationsMatch(const mojom::OperationEnvelope& left,
                     const mojom::OperationEnvelope& right) {
  return left.operation_id == right.operation_id &&
         left.service_generation == right.service_generation &&
         left.task_revision == right.task_revision &&
         left.deadline_monotonic_ms == right.deadline_monotonic_ms &&
         left.idempotency_key == right.idempotency_key;
}

bool IsRichArtifact(mojom::TaskArtifactKind kind) {
  switch (kind) {
    case mojom::TaskArtifactKind::kXlsx:
    case mojom::TaskArtifactKind::kPdf:
    case mojom::TaskArtifactKind::kDocx:
    case mojom::TaskArtifactKind::kPptx:
      return true;
    case mojom::TaskArtifactKind::kMarkdown:
    case mojom::TaskArtifactKind::kCsv:
    case mojom::TaskArtifactKind::kWaveAudio:
    case mojom::TaskArtifactKind::kFrameArchive:
      return false;
  }
  return false;
}

bool HasArtifactMagic(mojom::TaskArtifactKind kind,
                      const std::vector<uint8_t>& content) {
  if (kind == mojom::TaskArtifactKind::kPdf) {
    constexpr uint8_t kPdf[] = {'%', 'P', 'D', 'F', '-'};
    return content.size() >= std::size(kPdf) &&
           std::equal(std::begin(kPdf), std::end(kPdf), content.begin());
  }
  constexpr uint8_t kZip[] = {'P', 'K', 0x03u, 0x04u};
  return content.size() >= std::size(kZip) &&
         std::equal(std::begin(kZip), std::end(kZip), content.begin());
}

bool RequiresBrowserCustody(mojom::TaskArtifactKind kind) {
  return kind == mojom::TaskArtifactKind::kWaveAudio ||
         kind == mojom::TaskArtifactKind::kFrameArchive;
}

bool SupportsBrowserCustody(mojom::TaskArtifactKind kind) {
  return kind == mojom::TaskArtifactKind::kDocx ||
         kind == mojom::TaskArtifactKind::kXlsx ||
         RequiresBrowserCustody(kind);
}

bool IsRegisteredTaskTool(const mojom::ToolJobEffect& job) {
  if (job.runtime == mojom::ToolRuntimeKind::kPython) {
    return job.operation_kind ==
               mojom::ToolOperation::kRunBundledPythonModule &&
           job.tool_id == "python.execute";
  }
  if (job.runtime != mojom::ToolRuntimeKind::kMedia) {
    return false;
  }
  switch (job.operation_kind) {
    case mojom::ToolOperation::kProbeMedia:
      return job.tool_id == "media.probe";
    case mojom::ToolOperation::kExtractAudio:
      return job.tool_id == "media.audio.extract";
    case mojom::ToolOperation::kSampleFrames:
      return job.tool_id == "media.frames.sample";
    case mojom::ToolOperation::kTranscodePreset:
      return job.tool_id == "media.transcode";
    case mojom::ToolOperation::kRunBundledPythonModule:
    case mojom::ToolOperation::kGenerateLocalModel:
    case mojom::ToolOperation::kRunSignedWasmTransform:
    case mojom::ToolOperation::kEmbedLocalModel:
      return false;
  }
  return false;
}

bool IsUtf8TextArtifact(const std::vector<uint8_t>& content) {
  const std::string_view text(reinterpret_cast<const char*>(content.data()),
                              content.size());
  return text.find('\0') == std::string_view::npos && base::IsStringUTF8(text);
}

bool IsValidFormApprovalAction(const mojom::TaskExecutableAction& action) {
  const bool form_class =
      action.action_class == mojom::PolicyActionClass::kFillField ||
      action.action_class == mojom::PolicyActionClass::kSelectOption ||
      action.action_class == mojom::PolicyActionClass::kToggleControl ||
      action.action_class == mojom::PolicyActionClass::kSubmitForm;
  return form_class &&
         IsIdentifier(action.tool_name, mojom::kMaxIdentifierBytes) &&
         IsIdentifier(action.tab_id, mojom::kMaxIdentifierBytes) &&
         action.node_id &&
         IsIdentifier(*action.node_id, mojom::kMaxIdentifierBytes) &&
         !action.canonical_intent.empty() &&
         action.canonical_intent.size() <=
             mojom::kMaxCanonicalActionIntentBytes &&
         !action.destination_origin &&
         !action.operand_handle && !action.destination_address &&
         !action.transient_search_query && !action.task_tab &&
         !action.task_download && !action.task_store &&
         TaskOperationMatchesClassAndTool(action.operation_kind,
                                          action.action_class,
                                          action.tool_name) &&
         TaskActionInputMatchesOperationAndCanonical(
             action.input.get(), action.operation_kind,
             action.canonical_intent, action.tab_id, action.node_id);
}

size_t BodyCount(const mojom::TaskEffectBinding& effect) {
  return static_cast<size_t>(!!effect.revocation) +
         static_cast<size_t>(!!effect.policy) +
         static_cast<size_t>(!!effect.approval) +
         static_cast<size_t>(!!effect.permission) +
         static_cast<size_t>(!!effect.action) +
         static_cast<size_t>(!!effect.settlement) +
         static_cast<size_t>(!!effect.reconcile) +
         static_cast<size_t>(!!effect.release_tabs) +
         static_cast<size_t>(!!effect.generate_artifact) +
         static_cast<size_t>(!!effect.export_artifact) +
         static_cast<size_t>(!!effect.model) +
         static_cast<size_t>(!!effect.handover) +
         static_cast<size_t>(!!effect.tool_job) +
         static_cast<size_t>(!!effect.field_values) +
         static_cast<size_t>(!!effect.discovery_bootstrap) +
         static_cast<size_t>(!!effect.library_tool) +
         static_cast<size_t>(!!effect.memory_tool);
}

bool HasMatchingBody(const mojom::TaskEffectBinding& effect) {
  if (BodyCount(effect) != 1u) {
    return false;
  }
  switch (effect.kind) {
    case mojom::TaskReducerEffectKind::kRevokeAuthority:
      return !!effect.revocation;
    case mojom::TaskReducerEffectKind::kAskPolicy:
      return !!effect.policy;
    case mojom::TaskReducerEffectKind::kRequestApproval:
      return !!effect.approval;
    case mojom::TaskReducerEffectKind::kRequestPermission:
      return !!effect.permission;
    case mojom::TaskReducerEffectKind::kDispatchAction:
      return !!effect.action;
    case mojom::TaskReducerEffectKind::kAwaitInFlightWork:
      return !!effect.settlement;
    case mojom::TaskReducerEffectKind::kReconcileAction:
      return !!effect.reconcile;
    case mojom::TaskReducerEffectKind::kReleaseTaskTabs:
      return !!effect.release_tabs;
    case mojom::TaskReducerEffectKind::kGenerateArtifact:
      return !!effect.generate_artifact;
    case mojom::TaskReducerEffectKind::kExportArtifact:
      return !!effect.export_artifact;
    case mojom::TaskReducerEffectKind::kCallModel:
      return !!effect.model;
    case mojom::TaskReducerEffectKind::kAwaitHandover:
      return !!effect.handover;
    case mojom::TaskReducerEffectKind::kRunToolJob:
      return !!effect.tool_job;
    case mojom::TaskReducerEffectKind::kRequestFieldValues:
      return !!effect.field_values;
    case mojom::TaskReducerEffectKind::kPrepareDiscoveryTab:
      return !!effect.discovery_bootstrap;
    case mojom::TaskReducerEffectKind::kRunLibraryTool:
      return !!effect.library_tool;
    case mojom::TaskReducerEffectKind::kRunMemoryTool:
      return !!effect.memory_tool;
  }
  return false;
}

bool IsValidBody(const mojom::TaskEffectBinding& effect,
                 uint64_t service_generation,
                 uint64_t task_revision,
                 uint64_t now_monotonic_ms) {
  if (effect.policy) {
    return effect.policy->operation &&
           OperationsMatch(*effect.operation, *effect.policy->operation) &&
           effect.policy->effect_id == effect.effect_id &&
           effect.policy->task_id == effect.task_id &&
           IsValidReadOnlyTaskPolicyEffect(*effect.policy, service_generation,
                                           task_revision, now_monotonic_ms);
  }
  if (effect.approval) {
    return IsIdentifier(effect.approval->action_id,
                        mojom::kMaxIdentifierBytes) &&
           IsDigest(effect.approval->proposal_digest) &&
           (!effect.approval->form_action ||
            IsValidFormApprovalAction(*effect.approval->form_action));
  }
  if (effect.permission) {
    return IsIdentifier(effect.permission->request_id,
                        mojom::kMaxIdentifierBytes) &&
           effect.permission->deadline_monotonic_ms > now_monotonic_ms &&
           effect.permission->deadline_monotonic_ms <=
               effect.operation->deadline_monotonic_ms &&
           effect.permission->deadline_utc_ms != 0u &&
           IsIdentifier(effect.permission->browser_session_id,
                        mojom::kMaxIdentifierBytes);
  }
  if (effect.action) {
    return IsValidTaskActionEffect(*effect.action, effect, now_monotonic_ms);
  }
  if (effect.reconcile) {
    return IsIdentifier(effect.reconcile->action_id,
                        mojom::kMaxIdentifierBytes) &&
           IsIdentifier(effect.reconcile->dispatch_id,
                        mojom::kMaxIdentifierBytes);
  }
  if (effect.release_tabs) {
    return effect.release_tabs->terminal_revision == task_revision;
  }
  if (effect.generate_artifact) {
    return IsIdentifier(effect.generate_artifact->artifact_id,
                        mojom::kMaxIdentifierBytes) &&
           effect.generate_artifact->workspace_revision != 0u &&
           effect.generate_artifact->content.empty();
  }
  if (effect.export_artifact) {
    if (!IsIdentifier(effect.export_artifact->artifact_id,
                      mojom::kMaxIdentifierBytes) ||
        effect.export_artifact->workspace_revision == 0u) {
      return false;
    }
    if (effect.export_artifact->content.empty()) {
      // Isolated-tool bytes remain behind browser custody. DOCX/XLSX may also
      // arrive inline from the deterministic Rust file engine, so absence of
      // content selects custody while presence selects ordinary validation.
      return SupportsBrowserCustody(effect.export_artifact->kind);
    }
    if (RequiresBrowserCustody(effect.export_artifact->kind)) {
      return false;
    }
    if (effect.export_artifact->content.size() >
            mojom::kMaxTaskArtifactExportBytes) {
      return false;
    }
    return IsRichArtifact(effect.export_artifact->kind)
               ? HasArtifactMagic(effect.export_artifact->kind,
                                  effect.export_artifact->content)
               : IsUtf8TextArtifact(effect.export_artifact->content);
  }
  if (effect.handover) {
    // A window of no length is a handover that closes before it opens, so it
    // is refused rather than defaulted.
    //
    // There is deliberately no upper bound here, and in particular the window
    // is NOT bounded by the operation's deadline the way the permission
    // surface above is. That deadline is how long this operation has to be
    // *delivered* — 30s (`REVIEWED_COMMAND_DEADLINE_MS`) — while the window is
    // how long a person has to answer, which is minutes by design and outlives
    // the operation that opened it on purpose. Bounding one by the other would
    // refuse every real handover. The duration is the reducer's decision and
    // is carried across unchanged, because a browser that quietly shortened it
    // would end a handover the person was still inside.
    return IsIdentifier(effect.handover->handover_id,
                        mojom::kMaxIdentifierBytes) &&
           effect.handover->window_ms != 0u;
  }
  if (effect.model) {
    // The binding names a task and the request inside it names one too, and
    // they have to be the same task. They arrive as two fields because the
    // request is also dispatched on its own, where the binding is not there to
    // read; a pair that disagreed would be a call cancelling one task can stop
    // and the other cannot.
    return effect.model->request &&
           IsIdentifier(effect.model->call_id, mojom::kMaxIdentifierBytes) &&
           effect.model->request->task_id == effect.task_id &&
           IsValidCoreModelRequest(*effect.model->request,
                                   mojom::kMaxIdentifierBytes,
                                   mojom::kMaxEffectBytes);
  }
  if (effect.tool_job) {
    const auto& task_job = *effect.tool_job;
    if (!IsIdentifier(task_job.action_id, mojom::kMaxIdentifierBytes) ||
        !IsIdentifier(task_job.job_id, mojom::kMaxIdentifierBytes)) {
      return false;
    }
    if (task_job.runtime != mojom::ToolRuntimeKind::kPython &&
        task_job.runtime != mojom::ToolRuntimeKind::kMedia) {
      return !task_job.job;
    }
    return task_job.job && IsRegisteredTaskTool(*task_job.job) &&
           task_job.job->job_id == task_job.job_id &&
           task_job.job->runtime == task_job.runtime &&
           task_job.job->task_id == effect.task_id &&
           task_job.job->tool_version == "1" &&
           IsValidCoreToolJob(*task_job.job, mojom::kMaxIdentifierBytes,
                              mojom::kMaxEffectBytes);
  }
  if (effect.library_tool) {
    return IsIdentifier(effect.library_tool->action_id,
                        mojom::kMaxIdentifierBytes) &&
           (effect.library_tool->operation_kind ==
                mojom::TaskActionOperationKind::kLibrarySearch ||
            effect.library_tool->operation_kind ==
                mojom::TaskActionOperationKind::kLibrarySave ||
            effect.library_tool->operation_kind ==
                mojom::TaskActionOperationKind::kLibraryRemove);
  }
  if (effect.memory_tool) {
    return IsIdentifier(effect.memory_tool->action_id,
                        mojom::kMaxIdentifierBytes) &&
           (effect.memory_tool->operation_kind ==
                mojom::TaskActionOperationKind::kMemorySearch ||
            effect.memory_tool->operation_kind ==
                mojom::TaskActionOperationKind::kMemorySave ||
            effect.memory_tool->operation_kind ==
                mojom::TaskActionOperationKind::kMemoryUpdate ||
            effect.memory_tool->operation_kind ==
                mojom::TaskActionOperationKind::kMemoryDelete);
  }
  if (effect.field_values) {
    // Three identities and nothing else, because there is nothing else in the
    // body: the request the person is being asked to answer, the tab the form
    // is in, and the form itself. The node identity is required rather than
    // optional — an ask with no form to point at is a sheet with nothing in
    // it, and the browser has no second way to find one.
    //
    // Nothing here is checked about a value or a field class, and that is the
    // shape rather than an omission: the core names a form, and which of its
    // fields need a person is re-read from the node at the moment the sheet is
    // built (decision 0088 section 1). A claim in this body would be a second
    // place a field's class was decided.
    //
    // The companions are the one list: other fields on the same page that
    // only the person can supply, asked about on the same sheet (decision
    // 0238). Identities again, bounded so the named field and its companions
    // fit the sheet's rows, none repeated and none the named field itself.
    // Whether each still needs a person is re-read exactly as the named one
    // is, so a companion is a candidate and never a claim.
    const std::vector<std::string>& companions =
        effect.field_values->companion_node_ids;
    if (companions.size() >= kMaxSuppliedFieldValues) {
      return false;
    }
    for (auto it = companions.begin(); it != companions.end(); ++it) {
      if (!IsIdentifier(*it, mojom::kMaxIdentifierBytes) ||
          *it == effect.field_values->node_id ||
          std::find(companions.begin(), it, *it) != it) {
        return false;
      }
    }
    return IsIdentifier(effect.field_values->request_id,
                        mojom::kMaxIdentifierBytes) &&
           IsIdentifier(effect.field_values->tab_id,
                        mojom::kMaxIdentifierBytes) &&
           IsIdentifier(effect.field_values->node_id,
                        mojom::kMaxIdentifierBytes);
  }
  if (effect.discovery_bootstrap) {
    return IsIdentifier(effect.discovery_bootstrap->browser_session_id,
                        mojom::kMaxIdentifierBytes) &&
           effect.discovery_bootstrap->remaining_new_source_cap != 0u &&
           effect.discovery_bootstrap->remaining_new_source_cap <=
               mojom::kMaxNewSourceCap;
  }
  return true;
}

}  // namespace

bool IsStructurallyValidTaskEffectBinding(
    const mojom::TaskEffectBinding& effect,
    uint64_t service_generation,
    uint64_t task_revision,
    uint64_t now_monotonic_ms) {
  return effect.operation && service_generation != 0u && task_revision != 0u &&
         effect.operation->service_generation == service_generation &&
         effect.operation->task_revision == task_revision &&
         effect.operation->deadline_monotonic_ms > now_monotonic_ms &&
         IsIdentifier(effect.operation->operation_id,
                      mojom::kMaxOperationIdBytes) &&
         IsIdentifier(effect.operation->idempotency_key,
                      mojom::kMaxIdempotencyKeyBytes) &&
         IsIdentifier(effect.effect_id, mojom::kMaxIdentifierBytes) &&
         IsIdentifier(effect.task_id, mojom::kMaxIdentifierBytes) &&
         effect.ordinal < mojom::kMaxTaskEffectsPerState &&
         HasMatchingBody(effect) &&
         IsValidBody(effect, service_generation, task_revision,
                     now_monotonic_ms);
}

mojom::TaskEffectCompletionPtr MakeTaskEffectCompletion(
    const mojom::TaskEffectBinding* effect,
    mojom::TaskEffectCompletionStatus status) {
  auto completion = mojom::TaskEffectCompletion::New();
  completion->operation = effect && effect->operation
                              ? effect->operation.Clone()
                              : mojom::OperationEnvelope::New();
  if (effect) {
    completion->effect_id = effect->effect_id;
    completion->task_id = effect->task_id;
    completion->kind = effect->kind;
  }
  completion->status = status;
  return completion;
}

mojom::TaskEffectCompletionPtr MakeRefusedTaskActionCompletion(
    const mojom::TaskEffectBinding& effect,
    mojom::TaskActionResultCode code) {
  auto completion = MakeTaskEffectCompletion(
      &effect, mojom::TaskEffectCompletionStatus::kRefused);
  if (!effect.operation || !effect.action || !effect.action->executable ||
      code == mojom::TaskActionResultCode::kVerified) {
    return completion;
  }
  // A refusal is a refusal and nothing else: the code and the dispatch it
  // belongs to. `CopyRefusedActionTerminal` checks exactly that on the far
  // side and drops a terminal carrying anything more.
  auto refusal = mojom::EffectResult::New();
  refusal->operation = effect.operation.Clone();
  refusal->effect_id = effect.effect_id;
  refusal->status = mojom::EffectStatus::kDenied;
  refusal->kind = mojom::EffectKind::kBrowserAction;
  refusal->browser_action = mojom::BrowserActionEffectResult::New();
  refusal->browser_action->outcome = mojom::BrowserActionOutcome::kRefused;
  refusal->browser_action->dispatch_id = effect.action->dispatch_id;
  refusal->browser_action->refused_code = mojom::TaskActionRefusal::New(code);
  completion->effect_result = std::move(refusal);
  return completion;
}

}  // namespace taffy
