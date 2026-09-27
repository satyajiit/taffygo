// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The exact-lifetime subscription sign-in seam. Portable admission always
// precedes native start or cancellation, so a late native event can never
// recreate a flow the ordered core has already removed.

#include "taffy/browser/core_api/profile_core_api_facade.h"

#include <optional>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/time/time.h"

namespace taffy {
namespace {

uint64_t ProviderAuthNowMonotonicMillis() {
  const int64_t value = base::TimeTicks::Now().since_origin().InMilliseconds();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

}  // namespace

void ProfileCoreApiFacade::StartProviderAuth(
    const std::string& provider_id,
    StartProviderAuthCallback callback) {
  // Compatibility-only adapter. New surfaces need the exact returned flow id
  // and call StartProviderAuthFlow; both entrypoints share the same authority.
  StartProviderAuthImpl(
      provider_id,
      base::BindOnce(
          [](StartProviderAuthCallback callback, SubmissionStatus status,
             const std::optional<std::string>&) {
            std::move(callback).Run(status);
          },
          std::move(callback)));
}

void ProfileCoreApiFacade::StartProviderAuthFlow(
    const std::string& provider_id,
    StartProviderAuthFlowCallback callback) {
  StartProviderAuthImpl(provider_id, std::move(callback));
}

void ProfileCoreApiFacade::StartProviderAuthImpl(
    const std::string& provider_id,
    StartProviderAuthFlowCallback callback) {
  CoreApiCommandFactory factory = NewFactory();
  ProviderCommandResult result = factory.BuildStartProviderAuth(
      provider_id, manager_ ? manager_->service_generation() : 0u,
      ProviderAuthNowMonotonicMillis());
  std::string flow_id;
  std::string redirect_binding_id;
  if (result.command && result.command->core_service_command &&
      result.command->core_service_command->start_provider_auth) {
    const auto& start =
        *result.command->core_service_command->start_provider_auth;
    flow_id = start.flow_id;
    redirect_binding_id = start.redirect_binding_id;
  }
  if (!result.command) {
    std::move(callback).Run(SubmissionStatus::kInvalidRequest, std::nullopt);
    return;
  }
  SubmitProjected(
      std::move(result.command),
      base::BindOnce(
          [](base::WeakPtr<ProfileCoreApiFacade> facade,
             std::string provider_id, std::string flow_id,
             std::string redirect_binding_id,
             StartProviderAuthFlowCallback callback,
             SubmissionStatus status) {
            std::optional<std::string> accepted_flow;
            if (facade && facade->manager_ &&
                status == SubmissionStatus::kAccepted) {
              facade->manager_->StartProviderAuthFlow(
                  provider_id, flow_id, redirect_binding_id);
              accepted_flow = std::move(flow_id);
            }
            std::move(callback).Run(status, accepted_flow);
          },
          weak_factory_.GetWeakPtr(), provider_id, std::move(flow_id),
          std::move(redirect_binding_id), std::move(callback)));
}

void ProfileCoreApiFacade::CancelProviderAuth(
    const std::string& flow_id,
    CancelProviderAuthCallback callback) {
  CoreApiCommandFactory factory = NewFactory();
  SubmitProviderResult(
      factory.BuildCancelProviderAuth(
          flow_id, manager_ ? manager_->service_generation() : 0u,
          ProviderAuthNowMonotonicMillis()),
      base::BindOnce(
          [](base::WeakPtr<ProfileCoreApiFacade> facade, std::string flow_id,
             CancelProviderAuthCallback callback, SubmissionStatus status) {
            // The portable record was removed first. A false native answer now
            // means the browser terminal won the callback gap; either way the
            // exact flow is already settled and must not be resurrected.
            if (facade && facade->manager_ &&
                status == SubmissionStatus::kAccepted) {
              facade->manager_->CancelProviderAuthFlow(flow_id);
            }
            std::move(callback).Run(status);
          },
          weak_factory_.GetWeakPtr(), flow_id, std::move(callback)));
}

}  // namespace taffy
