// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/services/core/rust_core_assets.h"

#include <algorithm>
#include <string>

namespace taffy::core_service_internal {

namespace mojom = core_service::mojom;
namespace bridge = core_bridge;

namespace {

// The delivery plane mirrors the operation envelope under its own name, the
// same way the provider plane does: two cxx bridge modules cannot share a
// by-value struct without an include cycle between their generated headers.
bridge::BridgeAssetOperation ToBridgeAssetOperation(
    const mojom::OperationEnvelope& in) {
  bridge::BridgeAssetOperation out;
  out.operation_id = in.operation_id;
  out.service_generation = in.service_generation;
  out.task_revision = in.task_revision;
  out.deadline_monotonic_ms = in.deadline_monotonic_ms;
  out.idempotency_key = in.idempotency_key;
  return out;
}

}  // namespace

std::optional<bridge::BridgeAssetCommand> ToBridgeAssetCommand(
    const mojom::CoreServiceCommand& command) {
  if (!command.operation) {
    return std::nullopt;
  }
  bridge::BridgeAssetCommand out;
  out.operation = ToBridgeAssetOperation(*command.operation);
  out.kind = static_cast<uint8_t>(command.kind);
  switch (command.kind) {
    case mojom::CoreServiceCommandKind::kSetAssetDeliveryPolicy:
      if (!command.set_asset_delivery_policy) {
        return std::nullopt;
      }
      out.network_cost =
          static_cast<uint8_t>(command.set_asset_delivery_policy->network_cost);
      out.metered_permitted =
          command.set_asset_delivery_policy->metered_permitted;
      break;
    case mojom::CoreServiceCommandKind::kRequestAsset:
      if (!command.request_asset) {
        return std::nullopt;
      }
      out.asset_id = command.request_asset->asset_id;
      out.asset_revision = command.request_asset->asset_revision;
      break;
    case mojom::CoreServiceCommandKind::kRemoveAsset:
      if (!command.remove_asset) {
        return std::nullopt;
      }
      out.asset_id = command.remove_asset->asset_id;
      out.asset_revision = command.remove_asset->asset_revision;
      break;
    case mojom::CoreServiceCommandKind::kStartTask:
    case mojom::CoreServiceCommandKind::kCancelTask:
    case mojom::CoreServiceCommandKind::kUserDecision:
    case mojom::CoreServiceCommandKind::kPermissionResult:
    case mojom::CoreServiceCommandKind::kAuthCallback:
    case mojom::CoreServiceCommandKind::kStartAuth:
    case mojom::CoreServiceCommandKind::kRequestEmailLink:
    case mojom::CoreServiceCommandKind::kSignOut:
    case mojom::CoreServiceCommandKind::kAuthCredentialResult:
    case mojom::CoreServiceCommandKind::kCorrectWorkspaceFact:
    case mojom::CoreServiceCommandKind::kExcludeWorkspaceSource:
    case mojom::CoreServiceCommandKind::kRequestWorkspaceExport:
    case mojom::CoreServiceCommandKind::kSaveWorkspace:
    case mojom::CoreServiceCommandKind::kRenameWorkspace:
    case mojom::CoreServiceCommandKind::kDeleteWorkspace:
    case mojom::CoreServiceCommandKind::kDiscardWorkspace:
    case mojom::CoreServiceCommandKind::kSearchLibrary:
    case mojom::CoreServiceCommandKind::kSaveLibraryFact:
    case mojom::CoreServiceCommandKind::kRemoveLibraryEntry:
    case mojom::CoreServiceCommandKind::kRequestLibraryExport:
    case mojom::CoreServiceCommandKind::kSearchMemory:
    case mojom::CoreServiceCommandKind::kUpsertMemory:
    case mojom::CoreServiceCommandKind::kDeleteMemory:
    case mojom::CoreServiceCommandKind::kSaveProviderCredential:
    case mojom::CoreServiceCommandKind::kSetProviderCredentialState:
    case mojom::CoreServiceCommandKind::kProbeProviderCredential:
    case mojom::CoreServiceCommandKind::kForgetProviderCredential:
    case mojom::CoreServiceCommandKind::kStartProviderAuth:
    case mojom::CoreServiceCommandKind::kProviderAuthCallback:
    case mojom::CoreServiceCommandKind::kSaveCustomProvider:
    case mojom::CoreServiceCommandKind::kRemoveCustomProvider:
    case mojom::CoreServiceCommandKind::kCompleteHandover:
    case mojom::CoreServiceCommandKind::kExpireHandover:
    case mojom::CoreServiceCommandKind::kSupplyUserInput:
    case mojom::CoreServiceCommandKind::kFollowUp:
    case mojom::CoreServiceCommandKind::kAcceptTaskArtifact:
    case mojom::CoreServiceCommandKind::kExportTaskArtifact:
    // Kinds this projection does not carry. Listed rather than defaulted: a
    // switch with no default is how a new command kind becomes a compile error
    // in the one file that has to decide about it, instead of a silent refusal.
    case mojom::CoreServiceCommandKind::kSupplyFieldValues:
    case mojom::CoreServiceCommandKind::kSetProviderModelPreference:
    case mojom::CoreServiceCommandKind::kProbeCustomEndpoint:
    case mojom::CoreServiceCommandKind::kRequestComposerCompletion:
    case mojom::CoreServiceCommandKind::kCancelComposerCompletion:
    case mojom::CoreServiceCommandKind::kPauseTask:
    case mojom::CoreServiceCommandKind::kResumeTask:
    case mojom::CoreServiceCommandKind::kTakeOver:
    case mojom::CoreServiceCommandKind::kSetAssistantConfiguration:
    case mojom::CoreServiceCommandKind::kReplaceSavedDataSnapshot:
    case mojom::CoreServiceCommandKind::kMutateSkill:
    case mojom::CoreServiceCommandKind::kCancelProviderAuth:
      return std::nullopt;
  }
  return out;
}

std::optional<bridge::BridgeAssetReport> ToBridgeAssetReport(
    const mojom::EffectResult& result) {
  if (!result.operation || result.kind != mojom::EffectKind::kDeliverAsset ||
      !result.asset_delivery) {
    return std::nullopt;
  }
  const mojom::AssetDeliveryEffectResult& delivery = *result.asset_delivery;
  bridge::BridgeAssetReport out;
  out.operation = ToBridgeAssetOperation(*result.operation);
  out.effect_id = result.effect_id;
  out.operation_kind = static_cast<uint8_t>(delivery.operation_kind);
  switch (delivery.operation_kind) {
    case mojom::AssetDeliveryOperation::kFetchAsset:
      if (!delivery.transfer) {
        return std::nullopt;
      }
      out.asset_id = delivery.transfer->asset_id;
      out.asset_revision = delivery.transfer->asset_revision;
      out.outcome = static_cast<uint8_t>(delivery.transfer->outcome);
      out.written_bytes = delivery.transfer->written_bytes;
      out.observed_bytes = delivery.transfer->observed_bytes;
      // Mojo's fixed-size array is a size-validated `std::vector`; cxx's is a
      // `std::array`. The copy is explicit because the two are different types
      // for the same thirty-two bytes.
      if (delivery.transfer->observed_digest.size() !=
          out.observed_digest.size()) {
        return std::nullopt;
      }
      std::copy(delivery.transfer->observed_digest.begin(),
                delivery.transfer->observed_digest.end(),
                out.observed_digest.begin());
      break;
    case mojom::AssetDeliveryOperation::kRemoveAsset:
      if (!delivery.removal) {
        return std::nullopt;
      }
      out.asset_id = delivery.removal->asset_id;
      out.asset_revision = delivery.removal->asset_revision;
      out.reclaimed_bytes = delivery.removal->reclaimed_bytes;
      break;
  }
  return out;
}

mojom::EffectEnvelopePtr ToMojoAssetEffect(
    const bridge::BridgeAssetEffect& in) {
  auto effect = mojom::EffectEnvelope::New();
  effect->operation = mojom::OperationEnvelope::New();
  effect->operation->operation_id = std::string(in.operation.operation_id);
  effect->operation->service_generation = in.operation.service_generation;
  effect->operation->task_revision = in.operation.task_revision;
  effect->operation->deadline_monotonic_ms = in.operation.deadline_monotonic_ms;
  effect->operation->idempotency_key =
      std::string(in.operation.idempotency_key);
  effect->effect_id = std::string(in.effect_id);
  effect->kind = mojom::EffectKind::kDeliverAsset;
  // A fetch may be repeated: it resumes from what is on disk and the digest
  // decides. A removal of something already gone frees nothing and is not a
  // failure. Both are idempotent, which is what lets a recovery replay one.
  effect->retry_class = mojom::RetryClass::kIdempotent;
  effect->asset_delivery = mojom::AssetDeliveryEffect::New();
  effect->asset_delivery->operation_kind =
      static_cast<mojom::AssetDeliveryOperation>(in.operation_kind);
  if (effect->asset_delivery->operation_kind ==
      mojom::AssetDeliveryOperation::kFetchAsset) {
    effect->asset_delivery->fetch = mojom::AssetFetchRequest::New();
    effect->asset_delivery->fetch->asset_id = std::string(in.asset_id);
    effect->asset_delivery->fetch->asset_revision =
        std::string(in.asset_revision);
    effect->asset_delivery->fetch->origin_path = std::string(in.origin_path);
    effect->asset_delivery->fetch->offset_bytes = in.offset_bytes;
    effect->asset_delivery->fetch->total_bytes = in.total_bytes;
    effect->asset_delivery->fetch->expected_digest.assign(
        in.expected_digest.begin(), in.expected_digest.end());
    effect->asset_delivery->fetch->container =
        static_cast<mojom::AssetContainer>(in.container);
    return effect;
  }
  effect->asset_delivery->remove = mojom::AssetRemoveRequest::New();
  effect->asset_delivery->remove->asset_id = std::string(in.asset_id);
  effect->asset_delivery->remove->asset_revision =
      std::string(in.asset_revision);
  return effect;
}

}  // namespace taffy::core_service_internal
