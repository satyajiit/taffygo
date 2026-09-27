// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The two fan-outs: which plane owns an arriving command, and which plane owns
// an arriving effect result. They are one file because they are one question
// asked in two directions, and because neither decides anything itself — each
// picks a projection and hands the ordered core a typed record.

#include <optional>
#include <utility>

#include "base/logging.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/services/core/rust_core.h"
#include "taffy/services/core/rust_core_account.h"
#include "taffy/services/core/rust_core_assets.h"
#include "taffy/services/core/rust_core_bridge_handle.h"
#include "taffy/services/core/rust_core_command_conversions.h"
#include "taffy/services/core/rust_core_composer.h"
#include "taffy/services/core/rust_core_endpoint_probe.h"
#include "taffy/services/core/rust_core_listing.h"
#include "taffy/services/core/rust_core_probe.h"
#include "taffy/services/core/rust_core_provider.h"
#include "taffy/services/core/rust_core_response.h"
#include "taffy/services/core/rust_core_saved_data.h"
#include "taffy/services/core/rust_core_skills.h"
#include "taffy/services/core/rust_core_task.h"
#include "taffy/services/core/rust_core_workspace.h"
#include "taffy/services/core/service_bridge.rs.h"

namespace taffy {

namespace mojom = core_service::mojom;
namespace bridge = core_bridge;

namespace {

// One refusal, named, for every projection that answers nothing.
//
// A command whose typed record cannot be built is refused as an invalid
// command, and the wire carries a status and nothing else — so nine different
// faults reached a person as one sentence and left no line behind. The site is
// compiled in and the only value printed is the command kind, which is a
// closed enumeration and names no content.
CoreResponseBatch RefusedProjection(const mojom::CoreServiceCommand& command,
                                    const char* site) {
  LOG(ERROR) << "[taffy_core_projection_refused] at=" << site
             << " kind=" << static_cast<int>(command.kind);
  CoreResponseBatch batch;
  batch.admission = mojom::Admission::New();
  batch.admission->operation_id = command.operation->operation_id;
  batch.admission->status = mojom::AdmissionStatus::kInvalidCommand;
  return batch;
}

}  // namespace

CoreResponseBatch RustCore::Submit(mojom::CoreServiceCommandPtr command,
                                   uint64_t now_monotonic_ms) {
  if (!bridge_ || !command || !command->operation) {
    CoreResponseBatch batch;
    batch.admission = mojom::Admission::New();
    if (command && command->operation) {
      batch.admission->operation_id = command->operation->operation_id;
    }
    batch.admission->status = mojom::AdmissionStatus::kInvalidCommand;
    return batch;
  }
  if (command->kind == mojom::CoreServiceCommandKind::kStartTask) {
    std::optional<bridge::BridgeStartTask> projected =
        core_service_internal::ToBridgeStart(*command);
    if (!projected) {
      return RefusedProjection(*command, "start-task");
    }
    return Answer(core_service_internal::ToResponseBatch(bridge::SubmitStartTask(
                      *bridge_->runtime(), std::move(*projected),
                      now_monotonic_ms, NowUtcMillis())),
                  "start-task");
  }
  if (command->kind ==
      mojom::CoreServiceCommandKind::kReplaceSavedDataSnapshot) {
    std::optional<bridge::BridgeSavedDataCommand> projected =
        core_service_internal::ToBridgeSavedDataCommand(*command);
    if (!projected) {
      return RefusedProjection(*command, "saved-data");
    }
    return core_service_internal::ToResponseBatch(bridge::SubmitSavedData(
        *bridge_->runtime(), std::move(*projected), now_monotonic_ms));
  }
  if (command->kind ==
      mojom::CoreServiceCommandKind::kSetAssistantConfiguration) {
    std::optional<bridge::BridgeAssistantConfigurationCommand> projected =
        core_service_internal::ToBridgeAssistantConfiguration(*command);
    if (!projected) {
      return RefusedProjection(*command, "assistant-configuration");
    }
    return core_service_internal::ToResponseBatch(
        bridge::SubmitAssistantConfiguration(
            *bridge_->runtime(), std::move(*projected), now_monotonic_ms));
  }
  if (command->kind == mojom::CoreServiceCommandKind::kMutateSkill) {
    std::optional<bridge::BridgeSkillCommand> projected =
        core_service_internal::ToBridgeSkillCommand(*command);
    if (!projected) {
      return RefusedProjection(*command, "skill-mutation");
    }
    return core_service_internal::ToResponseBatch(bridge::SubmitSkillMutation(
        *bridge_->runtime(), std::move(*projected), now_monotonic_ms));
  }
  if (command->kind == mojom::CoreServiceCommandKind::kCancelTask ||
      command->kind == mojom::CoreServiceCommandKind::kPauseTask ||
      command->kind == mojom::CoreServiceCommandKind::kResumeTask ||
      command->kind == mojom::CoreServiceCommandKind::kTakeOver ||
      command->kind == mojom::CoreServiceCommandKind::kUserDecision ||
      command->kind == mojom::CoreServiceCommandKind::kPermissionResult ||
      command->kind == mojom::CoreServiceCommandKind::kCompleteHandover ||
      command->kind == mojom::CoreServiceCommandKind::kExpireHandover ||
      command->kind == mojom::CoreServiceCommandKind::kSupplyUserInput ||
      command->kind == mojom::CoreServiceCommandKind::kFollowUp ||
      command->kind == mojom::CoreServiceCommandKind::kSupplyFieldValues ||
      command->kind == mojom::CoreServiceCommandKind::kAcceptTaskArtifact ||
      command->kind == mojom::CoreServiceCommandKind::kExportTaskArtifact) {
    std::optional<bridge::BridgeTaskCommand> projected =
        core_service_internal::ToBridgeTaskCommand(*command);
    if (!projected) {
      return RefusedProjection(*command, "task-command");
    }
    return core_service_internal::ToResponseBatch(
        bridge::SubmitTask(*bridge_->runtime(), std::move(*projected),
                           now_monotonic_ms, NowUtcMillis()));
  }
  if (command->kind == mojom::CoreServiceCommandKind::kCorrectWorkspaceFact ||
      command->kind == mojom::CoreServiceCommandKind::kExcludeWorkspaceSource ||
      command->kind == mojom::CoreServiceCommandKind::kRequestWorkspaceExport ||
      command->kind == mojom::CoreServiceCommandKind::kSaveWorkspace ||
      command->kind == mojom::CoreServiceCommandKind::kRenameWorkspace ||
      command->kind == mojom::CoreServiceCommandKind::kDeleteWorkspace ||
      command->kind == mojom::CoreServiceCommandKind::kDiscardWorkspace ||
      command->kind == mojom::CoreServiceCommandKind::kSearchLibrary ||
      command->kind == mojom::CoreServiceCommandKind::kSaveLibraryFact ||
      command->kind == mojom::CoreServiceCommandKind::kRemoveLibraryEntry ||
      command->kind == mojom::CoreServiceCommandKind::kRequestLibraryExport ||
      command->kind == mojom::CoreServiceCommandKind::kSearchMemory ||
      command->kind == mojom::CoreServiceCommandKind::kUpsertMemory ||
      command->kind == mojom::CoreServiceCommandKind::kDeleteMemory) {
    std::optional<bridge::BridgeWorkspaceCommand> projected =
        core_service_internal::ToBridgeWorkspaceCommand(*command);
    if (!projected) {
      return RefusedProjection(*command, "workspace-command");
    }
    return core_service_internal::ToResponseBatch(
        bridge::SubmitWorkspace(*bridge_->runtime(), std::move(*projected),
                                now_monotonic_ms, NowUtcMillis()));
  }
  if (command->kind == mojom::CoreServiceCommandKind::kSetAssetDeliveryPolicy ||
      command->kind == mojom::CoreServiceCommandKind::kRequestAsset ||
      command->kind == mojom::CoreServiceCommandKind::kRemoveAsset) {
    std::optional<bridge::BridgeAssetCommand> asset =
        core_service_internal::ToBridgeAssetCommand(*command);
    if (!asset) {
      return RefusedProjection(*command, "asset-command");
    }
    return core_service_internal::ToResponseBatch(
        bridge::SubmitAsset(*bridge_->runtime(), std::move(*asset),
                            now_monotonic_ms, NowUtcMillis()));
  }
  if (command->kind == mojom::CoreServiceCommandKind::kSaveProviderCredential ||
      command->kind ==
          mojom::CoreServiceCommandKind::kForgetProviderCredential ||
      command->kind ==
          mojom::CoreServiceCommandKind::kSetProviderCredentialState ||
      command->kind ==
          mojom::CoreServiceCommandKind::kProbeProviderCredential ||
      command->kind == mojom::CoreServiceCommandKind::kStartProviderAuth ||
      command->kind == mojom::CoreServiceCommandKind::kCancelProviderAuth ||
      command->kind == mojom::CoreServiceCommandKind::kProviderAuthCallback ||
      command->kind == mojom::CoreServiceCommandKind::kSaveCustomProvider ||
      command->kind == mojom::CoreServiceCommandKind::kRemoveCustomProvider ||
      command->kind ==
          mojom::CoreServiceCommandKind::kSetProviderModelPreference ||
      // An address a person typed, asked what it is before anything is saved
      // under it (decision 0096). It belongs with the provider commands
      // because it carries a provider's fields and claims the same single
      // probe flight the key probe claims.
      command->kind == mojom::CoreServiceCommandKind::kProbeCustomEndpoint) {
    std::optional<bridge::BridgeProviderCommand> provider =
        core_service_internal::ToBridgeProviderCommand(*command);
    if (!provider) {
      return RefusedProjection(*command, "provider-command");
    }
    return core_service_internal::ToResponseBatch(
        bridge::SubmitProvider(*bridge_->runtime(), std::move(*provider),
                               now_monotonic_ms, NowUtcMillis()));
  }
  if (command->kind ==
      mojom::CoreServiceCommandKind::kRequestComposerCompletion) {
    return core_service_internal::SubmitComposerCommand(
        *bridge_->runtime(), *command, now_monotonic_ms, NowUtcMillis());
  }
  if (command->kind ==
      mojom::CoreServiceCommandKind::kCancelComposerCompletion) {
    // The withdrawal a surface states on its own (decision 0097 section 3).
    // It reaches the same plane and answers with the same batch a submission
    // does: nothing to dispatch, and the displaced flight named.
    return core_service_internal::CancelComposerCommand(
        *bridge_->runtime(), *command, now_monotonic_ms, NowUtcMillis());
  }
  std::optional<bridge::BridgeAccountCommand> projected =
      core_service_internal::ToBridgeAccountCommand(*command);
  if (!projected) {
    return RefusedProjection(*command, "account-command");
  }
  return core_service_internal::ToResponseBatch(
      bridge::SubmitAccount(*bridge_->runtime(), std::move(*projected),
                            now_monotonic_ms, NowUtcMillis()));
}

CoreResponseBatch RustCore::DeliverEffectResult(mojom::EffectResultPtr result,
                                                uint64_t now_monotonic_ms) {
  if (!bridge_ || !result || !result->operation) {
    return CoreResponseBatch();
  }
  if (result->kind == mojom::EffectKind::kStorageCommit && result->storage) {
    bridge::BridgeStorageCompletion completion;
    completion.operation =
        core_service_internal::ToBridgeStorageCompletionOperation(
            *result->operation);
    completion.effect_id = result->effect_id;
    completion.status = static_cast<uint8_t>(result->status);
    completion.committed_revision = result->storage->committed_revision;
    return Answer(core_service_internal::ToResponseBatch(
                      bridge::DeliverStorageCompletion(
                          *bridge_->runtime(), std::move(completion),
                          now_monotonic_ms, NowUtcMillis())),
                  "storage-completion");
  }
  if (std::optional<bridge::BridgeAssetReport> asset =
          core_service_internal::ToBridgeAssetReport(*result)) {
    return core_service_internal::ToResponseBatch(
        bridge::DeliverAssetReport(*bridge_->runtime(), std::move(*asset),
                                   now_monotonic_ms, NowUtcMillis()));
  }
  if (std::optional<bridge::BridgeListingResult> listing =
          core_service_internal::ToBridgeProviderListingResult(*result)) {
    // A served listing's terminal (decision 0098). It names its own kind, so
    // there is nothing here to guess: a build with no fetcher answers
    // UNAVAILABLE with an empty body, and the ordered core reads that as an
    // endpoint it could not reach rather than as a provider with no models.
    return core_service_internal::ToResponseBatch(
        bridge::DeliverProviderListingResult(
            *bridge_->runtime(), std::move(*listing), NowUtcMillis()));
  }
  if (std::optional<bridge::BridgeEndpointProbeResult> endpoint =
          core_service_internal::ToBridgeEndpointProbeResult(*result)) {
    // What answered at an address a person typed (decision 0096). It names its
    // own kind, so it is claimed here rather than offered to the model-request
    // legs below: the key probe's terminal is a MODEL_REQUEST result and this
    // one is not, which is the whole reason the two have separate effects.
    return core_service_internal::ToResponseBatch(
        bridge::DeliverEndpointProbeResult(*bridge_->runtime(),
                                           std::move(*endpoint),
                                           now_monotonic_ms, NowUtcMillis()));
  }
  if (result->kind == mojom::EffectKind::kDeliverComposerCompletion &&
      result->composer_completion) {
    // The suggestion push's own terminal (decision 0097). Claimed by kind
    // rather than by identity, because this kind belongs to one plane and to
    // no other; an unclaimed terminal would go on to a delivery leg that never
    // asked for it. It carries no state and produces no effect, so the batch
    // is empty on purpose: what the plane does with it is settle the request,
    // and a suggestion is journalled nowhere and charged to nothing.
    bridge::BridgeComposerTerminal terminal;
    terminal.request_id = result->composer_completion->request_id;
    terminal.delivered = result->composer_completion->delivered;
    bridge::RecordComposerDelivery(*bridge_->runtime(), std::move(terminal));
    return CoreResponseBatch();
  }
  // Two planes terminate a model call here — the composer's suggestion
  // (decision 0097) and the key probe (decision 0083) — and a terminal names
  // neither: both are MODEL_REQUEST results, and the effect identity that
  // tells them apart was minted inside the ordered core. So the composer is
  // offered every one and answers whether it was waiting for it; a terminal
  // it does not claim goes on to the probe, whose protocol refuses a foreign
  // identity the same way.
  if (std::optional<CoreResponseBatch> composed =
          core_service_internal::DeliverComposerTerminal(
              *bridge_->runtime(), *result, NowUtcMillis())) {
    return std::move(*composed);
  }
  if (std::optional<bridge::BridgeProbeCompletion> probe =
          core_service_internal::ToBridgeProbeCompletion(*result)) {
    // The task workflow's model calls terminate through the task-effect
    // terminals, never here.
    return core_service_internal::ToResponseBatch(
        bridge::DeliverProbeCompletion(*bridge_->runtime(), std::move(*probe),
                                       now_monotonic_ms, NowUtcMillis()));
  }
  std::optional<bridge::BridgeAccountCompletion> account =
      core_service_internal::ToBridgeAccountCompletion(result.get());
  if (!account) {
    return CoreResponseBatch();
  }
  return core_service_internal::ToResponseBatch(
      bridge::DeliverAccountCompletion(*bridge_->runtime(), std::move(*account),
                                       now_monotonic_ms, NowUtcMillis()));
}

}  // namespace taffy
