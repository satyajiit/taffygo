// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <cstdint>
#include <optional>

#include "taffy/test/recovery/core_api_status_reader.h"

namespace taffy::test::internal {

bool CoreStatusWireReader::SkipOptionalAssetDelivery() {
  const std::optional<bool> present = ReadBool();
  return present && (!*present || SkipAssetDelivery());
}

bool CoreStatusWireReader::SkipAssetDelivery() {
  if (!ReadBool().has_value() ||
      !ReadClosedEnum(api::AssetNetworkCostView::kOffline,
                      api::AssetNetworkCostView::kUnmetered) ||
      !ReadBool().has_value()) {
    return false;
  }
  const std::optional<uint32_t> count = ReadLength(api::kMaxAssets);
  if (!count) {
    return false;
  }
  for (uint32_t index = 0u; index < *count; ++index) {
    if (!SkipAsset()) {
      return false;
    }
  }
  return true;
}

bool CoreStatusWireReader::SkipAsset() {
  if (!ReadString(api::kMaxIdentifierBytes) ||
      !ReadString(api::kMaxIdentifierBytes) ||
      !ReadClosedEnum(api::AssetKindView::kPythonStdlib,
                      api::AssetKindView::kStartScenes) ||
      !ReadClosedEnum(api::AssetPresenceView::kAbsent,
                      api::AssetPresenceView::kInstalled) ||
      !ReadU64() || !ReadU64() || !ReadU32()) {
    return false;
  }
  const std::optional<bool> refusal_present = ReadBool();
  if (!refusal_present ||
      (*refusal_present &&
       (!ReadClosedEnum(api::AssetRefusalView::kUnknownAsset,
                        api::AssetRefusalView::kDeclinedByPerson) ||
        !ReadBool().has_value()))) {
    return false;
  }
  return ReadU64().has_value();
}

bool CoreStatusWireReader::SkipProviderRoster() {
  const std::optional<uint32_t> count =
      ReadLength(api::kMaxProviderRosterEntries);
  if (!count) {
    return false;
  }
  for (uint32_t index = 0u; index < *count; ++index) {
    if (!SkipProviderRosterEntry()) {
      return false;
    }
  }
  return true;
}

bool CoreStatusWireReader::SkipProviderRosterEntry() {
  if (!ReadString(api::kMaxProviderIdBytes) ||
      !ReadString(api::kMaxProviderDisplayNameBytes) ||
      !ReadClosedEnum(api::ProviderOriginView::kCatalog,
                      api::ProviderOriginView::kCustom)) {
    return false;
  }
  const std::optional<uint32_t> auth_count = ReadLength(api::kMaxAuthMethods);
  if (!auth_count) {
    return false;
  }
  for (uint32_t index = 0u; index < *auth_count; ++index) {
    if (!ReadClosedEnum(api::ProviderAuthMethodView::kApiKey,
                        api::ProviderAuthMethodView::kOauth)) {
      return false;
    }
  }
  const std::optional<bool> stored_present = ReadBool();
  if (!stored_present || (*stored_present && !SkipStoredCredential()) ||
      !ReadBool().has_value() || !ReadBool().has_value() ||
      !SkipOptionalString(api::kMaxSourceHostBytes) ||
      !ReadBool().has_value() || !ReadBool().has_value() ||
      !ReadClosedEnum(api::CatalogLayerView::kEmbeddedBaseline,
                      api::CatalogLayerView::kUserOverride) ||
      !SkipOptionalString(api::kMaxModelIdBytes)) {
    return false;
  }
  const std::optional<bool> thinking_present = ReadBool();
  if (!thinking_present ||
      (*thinking_present && !ReadClosedEnum(api::ThinkingLevelView::kOff,
                                            api::ThinkingLevelView::kMax))) {
    return false;
  }
  const std::optional<bool> presentation_present = ReadBool();
  if (!presentation_present ||
      (*presentation_present && !SkipProviderPresentation()) ||
      !SkipOptionalString(api::kMaxProviderEndpointBytes)) {
    return false;
  }
  const std::optional<bool> refusal_present = ReadBool();
  if (!refusal_present ||
      (*refusal_present &&
       (!ReadClosedEnum(api::ProviderRefusalView::kRateLimit,
                        api::ProviderRefusalView::kOverloaded) ||
        !ReadU64()))) {
    return false;
  }
  return ReadU32().has_value() && ReadBool().has_value() &&
         SkipOptionalString(api::kMaxSourceHostBytes);
}

bool CoreStatusWireReader::SkipStoredCredential() {
  return ReadClosedEnum(api::ProviderAuthMethodView::kApiKey,
                        api::ProviderAuthMethodView::kOauth)
             .has_value() &&
         ReadClosedEnum(api::ProviderCredentialStateView::kUsable,
                        api::ProviderCredentialStateView::kRefreshFailed)
             .has_value() &&
         ReadBool().has_value() &&
         SkipOptionalString(api::kMaxAuthDisplayNameBytes) &&
         SkipOptionalString(api::kMaxProviderDisplayNameBytes);
}

bool CoreStatusWireReader::SkipProviderPresentation() {
  return SkipOptionalString(api::kMaxProviderPresentationBytes) &&
         SkipOptionalString(api::kMaxProviderPresentationBytes) &&
         SkipOptionalString(api::kMaxProviderPresentationBytes);
}

bool CoreStatusWireReader::SkipProviderProbes() {
  const std::optional<uint32_t> count =
      ReadLength(api::kMaxProviderRosterEntries);
  if (!count) {
    return false;
  }
  for (uint32_t index = 0u; index < *count; ++index) {
    if (!SkipProviderProbe()) {
      return false;
    }
  }
  return true;
}

bool CoreStatusWireReader::SkipProviderProbe() {
  if (!ReadString(api::kMaxProviderIdBytes) ||
      !ReadClosedEnum(api::ProviderProbeVerdictView::kUsable,
                      api::ProviderProbeVerdictView::kNoModelListed) ||
      !ReadU64()) {
    return false;
  }
  const std::optional<bool> endpoint_present = ReadBool();
  return endpoint_present && (!*endpoint_present || SkipProbeEndpoint());
}

bool CoreStatusWireReader::SkipProbeEndpoint() {
  if (!ReadClosedEnum(api::ServerKindView::kOpenaiCompatible,
                      api::ServerKindView::kLlamaCpp) ||
      !ReadU32()) {
    return false;
  }
  const std::optional<uint32_t> count = ReadLength(api::kMaxCustomModelEntries);
  if (!count) {
    return false;
  }
  for (uint32_t index = 0u; index < *count; ++index) {
    if (!SkipCustomModel()) {
      return false;
    }
  }
  return SkipOptionalString(api::kMaxProviderEndpointBytes);
}

bool CoreStatusWireReader::SkipCustomModel() {
  return ReadString(api::kMaxModelIdBytes).has_value() &&
         ReadString(api::kMaxModelDisplayNameBytes).has_value() &&
         ReadU32().has_value() && ReadU32().has_value() &&
         ReadBool().has_value() && ReadBool().has_value();
}

bool CoreStatusWireReader::SkipProviderModels() {
  const std::optional<uint32_t> count =
      ReadLength(api::kMaxProviderModelEntries);
  if (!count) {
    return false;
  }
  for (uint32_t index = 0u; index < *count; ++index) {
    if (!SkipProviderModel()) {
      return false;
    }
  }
  return true;
}

bool CoreStatusWireReader::SkipProviderModel() {
  if (!ReadString(api::kMaxProviderIdBytes) ||
      !ReadString(api::kMaxModelIdBytes) ||
      !ReadString(api::kMaxModelDisplayNameBytes) || !ReadU64() || !ReadU64() ||
      !ReadBool().has_value() || !ReadBool().has_value()) {
    return false;
  }
  const std::optional<uint32_t> role_count = ReadLength(api::kMaxModelRoles);
  if (!role_count) {
    return false;
  }
  for (uint32_t index = 0u; index < *role_count; ++index) {
    if (!ReadClosedEnum(api::ModelRoleView::kPrimaryReasoning,
                        api::ModelRoleView::kEmbedding)) {
      return false;
    }
  }
  const std::optional<uint32_t> modality_count =
      ReadLength(api::kMaxModelInputModalities);
  if (!modality_count) {
    return false;
  }
  for (uint32_t index = 0u; index < *modality_count; ++index) {
    if (!ReadClosedEnum(api::InputModalityView::kText,
                        api::InputModalityView::kImage)) {
      return false;
    }
  }
  const std::optional<uint32_t> thinking_count =
      ReadLength(api::kMaxModelThinkingLevels);
  if (!thinking_count) {
    return false;
  }
  for (uint32_t index = 0u; index < *thinking_count; ++index) {
    if (!ReadClosedEnum(api::ThinkingLevelView::kOff,
                        api::ThinkingLevelView::kMax)) {
      return false;
    }
  }
  return true;
}

bool CoreStatusWireReader::SkipAssistantConfiguration() {
  if (!ReadU64()) {
    return false;
  }
  const std::optional<uint32_t> ability_count =
      ReadLength(api::kMaxAssistantAbilities);
  if (!ability_count) {
    return false;
  }
  for (uint32_t index = 0u; index < *ability_count; ++index) {
    if (!ReadClosedEnum(api::AssistantAbilityView::kPagesLookup,
                        api::AssistantAbilityView::kKeep)) {
      return false;
    }
  }
  return ReadClosedEnum(api::PersonalityPresetView::kCarefulResearcher,
                        api::PersonalityPresetView::kTripPlanner)
             .has_value() &&
         ReadBoundedU32(api::kMaxPersonalityScale).has_value() &&
         ReadBoundedU32(api::kMaxPersonalityScale).has_value() &&
         ReadBoundedU32(api::kMaxPersonalityScale).has_value();
}

}  // namespace taffy::test::internal
