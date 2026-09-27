// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_service_command_validation_internal.h"

#include <optional>

namespace taffy {

namespace mojom = core_service::mojom;

// Every provider body, plus the browser-originated redirect.
//
// Split out because `CommandByteSize` was already at the length where one more
// body makes it unreadable, and because these share a property none of the
// others has: every one of them is bounded by a provider limit rather than by
// the generic identifier bound. `credential_handle` is measured, never read —
// it is an opaque secure-store reference and the contract has no field that
// could hold key material (decision 0049).
std::optional<size_t> ProviderCommandByteSize(
    const mojom::CoreServiceCommand &command, size_t ceiling) {
  size_t total = 0;
  if (command.save_provider_credential) {
    const auto &provider = *command.save_provider_credential;
    return AddProviderId(provider.provider_id, ceiling, &total) &&
                   AddIdentifier(provider.credential_handle, ceiling, &total)
               ? std::optional<size_t>(total)
               : std::nullopt;
  }
  if (command.forget_provider_credential) {
    return AddProviderId(command.forget_provider_credential->provider_id,
                         ceiling, &total)
               ? std::optional<size_t>(total)
               : std::nullopt;
  }
  if (command.set_provider_credential_state) {
    // The state itself is an enum mojo already refused if it was out of
    // range; the identity is the only measured field.
    return AddProviderId(command.set_provider_credential_state->provider_id,
                         ceiling, &total)
               ? std::optional<size_t>(total)
               : std::nullopt;
  }
  if (command.probe_provider_credential) {
    // The same two fields as a save, and the same posture: the handle is an
    // opaque reference — durable when it names the provider itself, one-shot
    // transient otherwise — and either way it is measured, never read.
    const auto &provider = *command.probe_provider_credential;
    return AddProviderId(provider.provider_id, ceiling, &total) &&
                   AddIdentifier(provider.credential_handle, ceiling, &total)
               ? std::optional<size_t>(total)
               : std::nullopt;
  }
  if (command.start_provider_auth) {
    const auto &provider = *command.start_provider_auth;
    return AddProviderId(provider.provider_id, ceiling, &total) &&
                   AddIdentifier(provider.flow_id, ceiling, &total) &&
                   AddIdentifier(provider.redirect_binding_id, ceiling, &total)
               ? std::optional<size_t>(total)
               : std::nullopt;
  }
  if (command.cancel_provider_auth) {
    return AddIdentifier(command.cancel_provider_auth->flow_id, ceiling,
                         &total)
               ? std::optional<size_t>(total)
               : std::nullopt;
  }
  if (command.provider_auth_callback) {
    const auto &provider = *command.provider_auth_callback;
    const bool expects_code =
        provider.status == mojom::AuthCallbackStatus::kAuthorizationCode;
    return provider.authorization_code_handle.has_value() == expects_code &&
                   AddIdentifier(provider.flow_id, ceiling, &total) &&
                   AddIdentifier(provider.redirect_binding_id, ceiling,
                                 &total) &&
                   AddIdentifier(provider.returned_state, ceiling, &total) &&
                   AddOptionalIdentifier(provider.authorization_code_handle,
                                         ceiling, &total)
               ? std::optional<size_t>(total)
               : std::nullopt;
  }
  if (command.save_custom_provider) {
    const auto &provider = *command.save_custom_provider;
    if (!AddProviderId(provider.provider_id, ceiling, &total) ||
        provider.display_name.empty() ||
        provider.display_name.size() > mojom::kMaxProviderDisplayNameBytes ||
        !AddBounded(provider.display_name.size(), ceiling, &total) ||
        provider.endpoint.empty() ||
        provider.endpoint.size() > mojom::kMaxProviderEndpointBytes ||
        !AddBounded(provider.endpoint.size(), ceiling, &total) ||
        !AddOptionalIdentifier(provider.credential_handle, ceiling, &total) ||
        provider.models.size() > mojom::kMaxCustomModelEntries) {
      return std::nullopt;
    }
    // Every declared row is measured, so a roster inside the entry count but
    // over the byte ceiling is refused here rather than at the far end.
    for (const mojom::CustomModelSpecPtr &model : provider.models) {
      if (!model || model->model_id.empty() ||
          model->model_id.size() > mojom::kMaxModelIdBytes ||
          !AddBounded(model->model_id.size(), ceiling, &total) ||
          model->display_name.empty() ||
          model->display_name.size() > mojom::kMaxModelDisplayNameBytes ||
          !AddBounded(model->display_name.size(), ceiling, &total)) {
        return std::nullopt;
      }
    }
    return total;
  }
  if (command.set_provider_model_preference) {
    // Whole-state: both fields absent is the request to clear the choice, so
    // there is nothing to require here beyond the identity.
    const auto &preference = *command.set_provider_model_preference;
    return AddProviderId(preference.provider_id, ceiling, &total) &&
                   (!preference.model_id ||
                    (!preference.model_id->empty() &&
                     preference.model_id->size() <=
                         mojom::kMaxModelIdBytes &&
                     AddBounded(preference.model_id->size(), ceiling, &total)))
               ? std::optional<size_t>(total)
               : std::nullopt;
  }
  if (command.probe_custom_endpoint) {
    // The provider identity here is a *draft* one (decision 0096 section 5):
    // no provider record exists yet, and this is the row the verdict is filed
    // under and the row the save that follows will create. It is bounded by
    // the same provider rule as a real identity, because it is the identity a
    // real provider will be created with.
    const auto &probe = *command.probe_custom_endpoint;
    return AddProviderId(probe.provider_id, ceiling, &total) &&
                   !probe.endpoint.empty() &&
                   probe.endpoint.size() <=
                       mojom::kMaxProviderEndpointBytes &&
                   AddBounded(probe.endpoint.size(), ceiling, &total) &&
                   AddOptionalIdentifier(probe.credential_handle, ceiling,
                                         &total)
               ? std::optional<size_t>(total)
               : std::nullopt;
  }
  if (command.remove_custom_provider) {
    return AddProviderId(command.remove_custom_provider->provider_id, ceiling,
                         &total)
               ? std::optional<size_t>(total)
               : std::nullopt;
  }
  return std::nullopt;
}

}  // namespace taffy
