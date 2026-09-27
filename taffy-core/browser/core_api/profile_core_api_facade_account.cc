// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_api/profile_core_api_facade.h"

#include <utility>

#include "base/time/time.h"

namespace taffy {
namespace {

uint64_t NowMonotonicMillis() {
  const int64_t value = base::TimeTicks::Now().since_origin().InMilliseconds();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

} // namespace

void ProfileCoreApiFacade::StartAuth(core_api::mojom::AuthProvider provider,
                                     StartAuthCallback callback) {
  CoreApiCommandFactory factory = NewFactory();
  SubmitProjected(factory.BuildStartAuth(
                      provider, manager_ ? manager_->service_generation() : 0u,
                      NowMonotonicMillis()),
                  std::move(callback));
}

void ProfileCoreApiFacade::RequestEmailLink(const std::string &email,
                                            RequestEmailLinkCallback callback) {
  CoreApiCommandFactory factory = NewFactory();
  SubmitProjected(factory.BuildRequestEmailLink(
                      email, manager_ ? manager_->service_generation() : 0u,
                      NowMonotonicMillis()),
                  std::move(callback));
}

void ProfileCoreApiFacade::SignOut(SignOutCallback callback) {
  CoreApiCommandFactory factory = NewFactory();
  SubmitProjected(
      factory.BuildSignOut(std::nullopt,
                           manager_ ? manager_->service_generation() : 0u,
                           NowMonotonicMillis()),
      std::move(callback));
}

void ProfileCoreApiFacade::DeliverCredentialResult(
    const std::string &flow_id, core_api::mojom::AuthProvider provider,
    core_api::mojom::AuthCredentialStatus credential_status,
    const std::optional<std::string> &credential_handle,
    DeliverCredentialResultCallback callback) {
  CoreApiCommandFactory factory = NewFactory();
  SubmitProjected(factory.BuildAuthCredentialResult(
                      flow_id, provider, credential_status, credential_handle,
                      manager_ ? manager_->service_generation() : 0u,
                      NowMonotonicMillis()),
                  std::move(callback));
}

} // namespace taffy
