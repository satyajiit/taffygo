// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The custom-provider half of the provider seam.
//
// Four builders, and what holds them together is that every one of them is
// about a provider a person configured themselves: the endpoint they typed,
// the models they declared on it, the probe that asks whether it answers, and
// the standing choice of which of those models to use.
//
// The field rules are not restated here. core_api_command_factory_provider.cc
// owns them and core_api_command_factory_provider_internal.h declares them,
// because they all reach the same store: a request one entry point accepts and
// another refuses is a provider a person can create and cannot delete.
//
// The endpoint rule both builders here apply is `CheckCustomProviderEndpoint`,
// which is decision 0096 section 3: the address is a person's own, so it keeps
// its port and its base path, and plain http is admitted to a literal local
// machine and to nothing else. It is not the catalog rule that sits beside it
// in that file, and it must not become one — the two answer different
// questions about addresses named by different authorities.
//
// The probe applies it too, and that is the property that keeps this pair
// honest: an address a probe may reach is an address the product is about to
// fetch from, so a probe that accepted more than a save would be a way to ask
// this browser to make a request nothing would ever have let it save. What the
// probe does *not* do is write the register — nothing is registered until
// something is saved, and the facade is where that difference lives.

#include "taffy/browser/core_api/core_api_command_factory.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "taffy/browser/core_api/core_api_command_factory_provider_internal.h"

namespace taffy {
namespace {

namespace api = core_api::mojom;
namespace service = core_service::mojom;

// A model identity as it is spelled on the wire, not as it is spelled into a
// route. The route table's own path rule is stricter and lives with the
// transport (`IsPathSafeModelId` in profile_model_broker_routes.cc), because
// only one wire family puts the model in a path and only that family may
// refuse one for it.
ProviderRequestRefusal CheckModelId(const std::string &value) {
  if (value.empty()) {
    return ProviderRequestRefusal::kEmptyModelId;
  }
  if (value.size() > api::kMaxModelIdBytes) {
    return ProviderRequestRefusal::kModelIdTooLong;
  }
  return ProviderRequestRefusal::kNone;
}

ProviderRequestRefusal CheckModelDisplayName(const std::string &value) {
  if (value.empty()) {
    return ProviderRequestRefusal::kEmptyModelDisplayName;
  }
  if (value.size() > api::kMaxModelDisplayNameBytes) {
    return ProviderRequestRefusal::kModelDisplayNameTooLong;
  }
  return ProviderRequestRefusal::kNone;
}

std::optional<service::ServerKind>
ProjectServerKind(api::ServerKindView server_kind) {
  switch (server_kind) {
  case api::ServerKindView::kOpenaiCompatible:
    return service::ServerKind::kOpenaiCompatible;
  case api::ServerKindView::kOllama:
    return service::ServerKind::kOllama;
  case api::ServerKindView::kLmStudio:
    return service::ServerKind::kLmStudio;
  case api::ServerKindView::kVllm:
    return service::ServerKind::kVllm;
  case api::ServerKindView::kLlamaCpp:
    return service::ServerKind::kLlamaCpp;
  }
  return std::nullopt;
}

std::optional<service::ThinkingLevel>
ProjectThinkingLevel(api::ThinkingLevelView level) {
  switch (level) {
  case api::ThinkingLevelView::kOff:
    return service::ThinkingLevel::kOff;
  case api::ThinkingLevelView::kMinimal:
    return service::ThinkingLevel::kMinimal;
  case api::ThinkingLevelView::kLow:
    return service::ThinkingLevel::kLow;
  case api::ThinkingLevelView::kMedium:
    return service::ThinkingLevel::kMedium;
  case api::ThinkingLevelView::kHigh:
    return service::ThinkingLevel::kHigh;
  case api::ThinkingLevelView::kXhigh:
    return service::ThinkingLevel::kXhigh;
  case api::ThinkingLevelView::kMax:
    return service::ThinkingLevel::kMax;
  }
  return std::nullopt;
}

// The declared roster, checked row by row and projected only once every row
// has passed. A roster half projected would be a provider offering models a
// person never declared beside ones they did.
ProviderRequestRefusal
CheckModelRoster(const std::vector<api::CustomModelSpecViewPtr> &models) {
  if (models.size() > api::kMaxCustomModelEntries) {
    return ProviderRequestRefusal::kTooManyModels;
  }
  for (const api::CustomModelSpecViewPtr &model : models) {
    if (!model) {
      return ProviderRequestRefusal::kMalformedCommand;
    }
    if (const ProviderRequestRefusal refusal = CheckModelId(model->model_id);
        refusal != ProviderRequestRefusal::kNone) {
      return refusal;
    }
    if (const ProviderRequestRefusal refusal =
            CheckModelDisplayName(model->display_name);
        refusal != ProviderRequestRefusal::kNone) {
      return refusal;
    }
  }
  return ProviderRequestRefusal::kNone;
}

std::vector<service::CustomModelSpecPtr>
ProjectModelRoster(const std::vector<api::CustomModelSpecViewPtr> &models) {
  std::vector<service::CustomModelSpecPtr> projected;
  projected.reserve(models.size());
  for (const api::CustomModelSpecViewPtr &model : models) {
    projected.push_back(service::CustomModelSpec::New(
        model->model_id, model->display_name, model->context_window,
        model->max_output_tokens, model->reasoning, model->tool_calling));
  }
  return projected;
}

} // namespace

ProviderCommandResult CoreApiCommandFactory::BuildSaveCustomProvider(
    std::string provider_id, std::string display_name, std::string endpoint,
    api::ProviderWireApiView wire_api,
    std::optional<std::string> credential_handle,
    std::vector<api::CustomModelSpecViewPtr> models,
    std::optional<api::ServerKindView> detected_server,
    uint64_t service_generation, uint64_t now_monotonic_ms) {
  if (const ProviderRequestRefusal refusal = CheckProviderId(provider_id);
      refusal != ProviderRequestRefusal::kNone) {
    return ProviderCommandResult::Refused(refusal);
  }
  if (const ProviderRequestRefusal refusal =
          CheckProviderDisplayName(display_name);
      refusal != ProviderRequestRefusal::kNone) {
    return ProviderCommandResult::Refused(refusal);
  }
  if (const ProviderRequestRefusal refusal =
          CheckCustomProviderEndpoint(endpoint);
      refusal != ProviderRequestRefusal::kNone) {
    return ProviderCommandResult::Refused(refusal);
  }
  if (credential_handle) {
    if (const ProviderRequestRefusal refusal =
            CheckProviderCredentialHandle(*credential_handle);
        refusal != ProviderRequestRefusal::kNone) {
      return ProviderCommandResult::Refused(refusal);
    }
  }
  if (const ProviderRequestRefusal refusal = CheckModelRoster(models);
      refusal != ProviderRequestRefusal::kNone) {
    return ProviderCommandResult::Refused(refusal);
  }
  const std::optional<service::ProviderWireApi> projected_wire_api =
      ProjectProviderWireApi(wire_api);
  if (!projected_wire_api) {
    return ProviderCommandResult::Refused(
        ProviderRequestRefusal::kMalformedCommand);
  }
  std::optional<service::ServerKind> projected_server;
  if (detected_server) {
    projected_server = ProjectServerKind(*detected_server);
    if (!projected_server) {
      return ProviderCommandResult::Refused(
          ProviderRequestRefusal::kMalformedCommand);
    }
  }

  // Projected before the request body is built, because building it consumes
  // the roster. Doing it the other way round means reading a moved-from vector
  // to fill the command that is actually submitted.
  std::vector<service::CustomModelSpecPtr> projected_models =
      ProjectModelRoster(models);

  auto core_command = api::CoreCommand::New();
  core_command->operation =
      NewCoreOperation(0u, service_generation, now_monotonic_ms);
  core_command->kind = api::CoreCommandKind::kSaveCustomProvider;
  core_command->save_custom_provider = api::SaveCustomProviderBody::New(
      provider_id, display_name, endpoint, wire_api, credential_handle,
      std::move(models),
      detected_server ? api::DetectedServerView::New(*detected_server)
                      : api::DetectedServerViewPtr());
  if (!HasValidGeneratedBody(*core_command)) {
    return ProviderCommandResult::Refused(
        ProviderRequestRefusal::kMalformedCommand);
  }

  auto service_command = service::CoreServiceCommand::New();
  service_command->operation = ProjectOperation(*core_command->operation);
  service_command->kind = service::CoreServiceCommandKind::kSaveCustomProvider;
  service_command->save_custom_provider =
      service::SaveCustomProviderCommand::New(
          std::move(provider_id), std::move(display_name), std::move(endpoint),
          *projected_wire_api, std::move(credential_handle),
          std::move(projected_models),
          projected_server ? service::DetectedServer::New(*projected_server)
                           : service::DetectedServerPtr());
  return ProviderCommandResult::Built(ProjectedCoreCommand{
      std::move(core_command), std::move(service_command)});
}

// The endpoint probe. It is the question a person asks before they save, and
// the core answers it with a probe verdict and — when the endpoint answered —
// what kind of server it is, how many models it offered, which of those models
// survived the bound, and the base the OpenAI-shaped API was actually proved
// at.
//
// The provider identity it carries is a draft rather than a record. No
// provider exists yet; what the identity does is name the row the verdict is
// filed under, and the save that follows reuses it so that a person who probes
// an address and then saves it gets one row rather than two. Before Core API
// 3.18 the probe named none, and the setup screen had to match its own verdict
// by "a row that was not there before I asked" — which holds for exactly as
// long as one probe is in flight.
//
// It is checked with `CheckProviderId`, the save's own rule, for the reason the
// endpoint is checked with the save's: a draft this builder accepted and a save
// would refuse is a verdict filed under a row that can never be created.
ProviderCommandResult CoreApiCommandFactory::BuildProbeCustomEndpoint(
    std::string provider_id, std::string endpoint,
    api::ProviderWireApiView wire_api,
    std::optional<std::string> credential_handle, uint64_t service_generation,
    uint64_t now_monotonic_ms) {
  if (const ProviderRequestRefusal refusal = CheckProviderId(provider_id);
      refusal != ProviderRequestRefusal::kNone) {
    return ProviderCommandResult::Refused(refusal);
  }
  if (const ProviderRequestRefusal refusal =
          CheckCustomProviderEndpoint(endpoint);
      refusal != ProviderRequestRefusal::kNone) {
    return ProviderCommandResult::Refused(refusal);
  }
  if (credential_handle) {
    if (const ProviderRequestRefusal refusal =
            CheckProviderCredentialHandle(*credential_handle);
        refusal != ProviderRequestRefusal::kNone) {
      return ProviderCommandResult::Refused(refusal);
    }
  }
  const std::optional<service::ProviderWireApi> projected_wire_api =
      ProjectProviderWireApi(wire_api);
  if (!projected_wire_api) {
    return ProviderCommandResult::Refused(
        ProviderRequestRefusal::kMalformedCommand);
  }

  auto core_command = api::CoreCommand::New();
  core_command->operation =
      NewCoreOperation(0u, service_generation, now_monotonic_ms);
  core_command->kind = api::CoreCommandKind::kProbeCustomEndpoint;
  core_command->probe_custom_endpoint = api::ProbeCustomEndpointBody::New(
      endpoint, wire_api, credential_handle, provider_id);
  if (!HasValidGeneratedBody(*core_command)) {
    return ProviderCommandResult::Refused(
        ProviderRequestRefusal::kMalformedCommand);
  }

  auto service_command = service::CoreServiceCommand::New();
  service_command->operation = ProjectOperation(*core_command->operation);
  service_command->kind = service::CoreServiceCommandKind::kProbeCustomEndpoint;
  service_command->probe_custom_endpoint =
      service::ProbeCustomEndpointCommand::New(
          std::move(endpoint), *projected_wire_api,
          std::move(credential_handle), std::move(provider_id));
  return ProviderCommandResult::Built(ProjectedCoreCommand{
      std::move(core_command), std::move(service_command)});
}

// One provider's standing model choice (decision 0093).
//
// Whole-state, never a change to apply: both fields absent is a request to
// clear the choice, and it is accepted rather than refused. That is why there
// is no "nothing to change" refusal here — the browser's own preference file
// removes the row, and the core is told the same thing.
ProviderCommandResult CoreApiCommandFactory::BuildSetProviderModelPreference(
    std::string provider_id, std::optional<std::string> model_id,
    std::optional<api::ThinkingLevelView> thinking_level,
    uint64_t service_generation, uint64_t now_monotonic_ms) {
  if (const ProviderRequestRefusal refusal = CheckProviderId(provider_id);
      refusal != ProviderRequestRefusal::kNone) {
    return ProviderCommandResult::Refused(refusal);
  }
  if (model_id) {
    if (const ProviderRequestRefusal refusal = CheckModelId(*model_id);
        refusal != ProviderRequestRefusal::kNone) {
      return ProviderCommandResult::Refused(refusal);
    }
  }
  std::optional<service::ThinkingLevel> projected_level;
  if (thinking_level) {
    projected_level = ProjectThinkingLevel(*thinking_level);
    if (!projected_level) {
      return ProviderCommandResult::Refused(
          ProviderRequestRefusal::kMalformedCommand);
    }
  }

  auto core_command = api::CoreCommand::New();
  core_command->operation =
      NewCoreOperation(0u, service_generation, now_monotonic_ms);
  core_command->kind = api::CoreCommandKind::kSetProviderModelPreference;
  core_command->set_provider_model_preference =
      api::SetProviderModelPreferenceBody::New(
          provider_id, model_id,
          thinking_level ? api::ThinkingPreferenceView::New(*thinking_level)
                         : api::ThinkingPreferenceViewPtr());
  if (!HasValidGeneratedBody(*core_command)) {
    return ProviderCommandResult::Refused(
        ProviderRequestRefusal::kMalformedCommand);
  }

  auto service_command = service::CoreServiceCommand::New();
  service_command->operation = ProjectOperation(*core_command->operation);
  service_command->kind =
      service::CoreServiceCommandKind::kSetProviderModelPreference;
  service_command->set_provider_model_preference =
      service::SetProviderModelPreferenceCommand::New(
          std::move(provider_id), std::move(model_id),
          projected_level ? service::ThinkingPreference::New(*projected_level)
                          : service::ThinkingPreferencePtr());
  return ProviderCommandResult::Built(ProjectedCoreCommand{
      std::move(core_command), std::move(service_command)});
}

} // namespace taffy
