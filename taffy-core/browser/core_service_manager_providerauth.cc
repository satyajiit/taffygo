// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "components/prefs/pref_service.h"
#include "taffy/browser/core_api/core_api_command_factory.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/model/custom_provider_endpoint_store.h"
#include "taffy/browser/model/provider_credential_announcement_store.h"
#include "taffy/browser/model/provider_model_preference_store.h"
#include "taffy/browser/providerauth/profile_provider_auth_broker.h"
#include "url/gurl.h"

// The manager's provider-sign-in glue (decision 0081), beside the account
// plane's twin in core_service_manager_account.cc: the facade starts an
// admitted flow here, and the broker's one terminal per flow comes back here
// to be wrapped into the PROVIDER_AUTH_CALLBACK command that clears the
// core's pending marker.
//
// What a person set up is replayed from here too, and for the reason decision
// 0080 gives about the served catalog: the browser file is the authority, the
// core holds only what it needs to decide, and so the core is told at every
// generation rather than asked to remember across one.
//
// It began as the standing model choice alone (decision 0093). Decision 0117
// is the rest of the same subject — a person's own providers, and the
// credentials this browser has announced — because the core's provider plane
// is empty in every new generation and nothing else fills it. That is a fact
// no gate can see: it exists only in a second generation, and no host lane
// starts one.

namespace taffy {

namespace {

namespace service_mojom = core_service::mojom;

uint64_t ProviderAuthNowMonotonicMillis() {
  const int64_t value = base::TimeTicks::Now().since_origin().InMilliseconds();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

service_mojom::CoreServiceCommandPtr BuildProviderCallbackCommand(
    service_mojom::ProviderAuthCallbackCommandPtr terminal, uint64_t generation,
    uint64_t now_monotonic_ms) {
  if (!terminal) {
    return nullptr;
  }
  constexpr uint64_t kCallbackCommandLifetimeMs = 30'000u;
  const uint64_t deadline =
      now_monotonic_ms >
              std::numeric_limits<uint64_t>::max() - kCallbackCommandLifetimeMs
          ? std::numeric_limits<uint64_t>::max()
          : now_monotonic_ms + kCallbackCommandLifetimeMs;
  auto command = service_mojom::CoreServiceCommand::New();
  const std::string operation_id =
      "provider-callback-" + base::Uuid::GenerateRandomV4().AsLowercaseString();
  command->operation = service_mojom::OperationEnvelope::New(
      operation_id, generation, 0u, deadline,
      "provider-callback-key-" +
          base::Uuid::GenerateRandomV4().AsLowercaseString());
  command->kind =
      service_mojom::CoreServiceCommandKind::kProviderAuthCallback;
  command->provider_auth_callback = std::move(terminal);
  return command;
}

}  // namespace

void CoreServiceManager::StartProviderAuthFlow(
    const std::string &provider_id, const std::string &flow_id,
    const std::string &redirect_binding_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (shutdown_started_ || !provider_auth_broker_) {
    return;
  }
  if (provider_id.empty() || flow_id.empty() || redirect_binding_id.empty()) {
    return;
  }
  provider_auth_broker_->StartFlow(provider_id, flow_id, redirect_binding_id,
                                   service_generation_);
  RefreshIdleTeardown();
}

void CoreServiceManager::RevokeProviderCredential(
    const std::string &provider_id, std::string token) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (shutdown_started_ || !provider_auth_broker_) {
    std::fill(token.begin(), token.end(), '\0');
    return;
  }
  provider_auth_broker_->RevokeBestEffort(provider_id, std::move(token));
}

bool CoreServiceManager::ClaimProviderRedirectNavigation(const GURL &url) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (shutdown_started_ || !provider_auth_broker_) {
    return false;
  }
  return provider_auth_broker_->ClaimInterceptedRedirect(url);
}

bool CoreServiceManager::HasInterceptableProviderRedirect() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return !shutdown_started_ && provider_auth_broker_ &&
         provider_auth_broker_->HasInterceptableRedirect();
}

bool CoreServiceManager::SubmitProviderAuthCode(const std::string &flow_id,
                                                std::string code) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (shutdown_started_ || !provider_auth_broker_) {
    std::fill(code.begin(), code.end(), '\0');
    return false;
  }
  return provider_auth_broker_->SubmitManualCode(flow_id, std::move(code));
}

bool CoreServiceManager::CancelProviderAuthFlow(const std::string &flow_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (shutdown_started_ || !provider_auth_broker_) {
    return false;
  }
  return provider_auth_broker_->CancelFlow(flow_id);
}

void CoreServiceManager::OnProviderFlowTerminated(
    uint64_t callback_generation,
    service_mojom::ProviderAuthCallbackCommandPtr terminal,
    base::OnceCallback<void(bool)> submitted) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  RefreshIdleTeardown();
  if (!terminal || shutdown_started_ ||
      availability_ != Availability::kReady ||
      callback_generation != service_generation_) {
    std::move(submitted).Run(false);
    return;
  }
  auto command =
      BuildProviderCallbackCommand(std::move(terminal), service_generation_,
                                   ProviderAuthNowMonotonicMillis());
  if (!command) {
    std::move(submitted).Run(false);
    return;
  }
  Submit(std::move(command),
         base::BindOnce(
             [](base::OnceCallback<void(bool)> done,
                service_mojom::AdmissionPtr admission) {
               std::move(done).Run(admission &&
                                   admission->status ==
                                       service_mojom::AdmissionStatus::kAccepted);
             },
             std::move(submitted)));
}

void CoreServiceManager::ReplayProviderSetup() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (shutdown_started_ || !profile_prefs_) {
    return;
  }
  const std::vector<CustomProviderDefinition> defined =
      ReadCustomProviderDefinitions(*profile_prefs_);
  const std::vector<ProviderCredentialAnnouncement> announced =
      ReadProviderCredentialAnnouncements(*profile_prefs_);
  const std::vector<ProviderModelPreference> chosen =
      ReadProviderModelPreferences(*profile_prefs_);
  if (defined.empty() && announced.empty() && chosen.empty()) {
    return;
  }
  // The same factory the surface's own request goes through, so a replayed
  // provider and a saved one are the same command built the same way. A row
  // the factory refuses is dropped rather than repaired: the file is the
  // browser's own, but it is still a file, and a provider nobody defined must
  // not reach the core because a torn value happened to parse.
  CoreApiCommandFactory factory(browser_profile_id_,
                                CreateCoreApiEntropySource());
  const uint64_t now_monotonic_ms = ProviderAuthNowMonotonicMillis();
  for (const CustomProviderDefinition &definition : defined) {
    std::vector<core_api::mojom::CustomModelSpecViewPtr> models;
    models.reserve(definition.models.size());
    for (const CustomProviderModel &model : definition.models) {
      models.push_back(core_api::mojom::CustomModelSpecView::New(
          model.model_id, model.display_name, model.context_window,
          model.max_output_tokens, model.reasoning, model.tool_calling));
    }
    ProviderCommandResult result = factory.BuildSaveCustomProvider(
        definition.provider_id, definition.display_name, definition.endpoint,
        definition.wire_api, definition.credential_handle, std::move(models),
        definition.detected_server, service_generation_, now_monotonic_ms);
    if (!result.command || !result.command->core_service_command) {
      continue;
    }
    Submit(std::move(result.command->core_service_command), base::DoNothing());
  }
  for (const ProviderCredentialAnnouncement &credential : announced) {
    ProviderCommandResult saved = factory.BuildSaveProviderCredential(
        credential.provider_id, credential.auth_method, credential.handle,
        service_generation_, now_monotonic_ms);
    if (!saved.command || !saved.command->core_service_command) {
      continue;
    }
    Submit(std::move(saved.command->core_service_command), base::DoNothing());
    if (credential.state ==
        core_api::mojom::ProviderCredentialStateView::kUsable) {
      continue;
    }
    // The save above files every credential as usable, because that is what a
    // save means. A record this browser knows is not usable takes one more
    // command to say so, and it is sent after rather than instead: the state
    // is a report about a credential the core has to hold first.
    ProviderCommandResult reported = factory.BuildSetProviderCredentialState(
        credential.provider_id, credential.state, service_generation_,
        now_monotonic_ms);
    if (!reported.command || !reported.command->core_service_command) {
      continue;
    }
    Submit(std::move(reported.command->core_service_command),
           base::DoNothing());
  }
  // Last, because a choice names a provider and a provider that does not exist
  // yet cannot be chosen for.
  for (const ProviderModelPreference &preference : chosen) {
    ProviderCommandResult result = factory.BuildSetProviderModelPreference(
        preference.provider_id, preference.model_id, preference.thinking_level,
        service_generation_, now_monotonic_ms);
    if (!result.command || !result.command->core_service_command) {
      continue;
    }
    Submit(std::move(result.command->core_service_command), base::DoNothing());
  }
}

}  // namespace taffy
