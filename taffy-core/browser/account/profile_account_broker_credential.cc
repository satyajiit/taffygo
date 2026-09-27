// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/account/profile_account_broker.h"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "mojo/public/cpp/bindings/callback_helpers.h"

namespace taffy {
namespace {

namespace account_mojom = browser::account::mojom;
namespace service = core_service::mojom;

void ClearMaterial(account_mojom::SecretMaterialReadResult* result) {
  if (!result) {
    return;
  }
  std::ranges::fill(result->material, 0u);
  result->material.clear();
}

}  // namespace

void ProfileAccountBroker::ResolveProviderCredential(
    const std::string& provider_id, const std::string& credential_handle,
    base::OnceCallback<void(std::optional<std::string>)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!callback) {
    return;
  }
  if (private_profile_ || provider_id.empty() || credential_handle.empty() ||
      !platform_adapter_.is_bound()) {
    std::move(callback).Run(std::nullopt);
    return;
  }
  platform_adapter_->ResolveProviderCredential(
      provider_id, credential_handle,
      mojo::WrapCallbackWithDefaultInvokeIfNotRun(
          base::BindOnce(&ProfileAccountBroker::OnProviderCredentialResolved,
                         weak_factory_.GetWeakPtr(), std::move(callback)),
          service::EffectStatus::kUnavailable, nullptr));
}

void ProfileAccountBroker::OnProviderCredentialResolved(
    base::OnceCallback<void(std::optional<std::string>)> callback,
    service::EffectStatus status,
    account_mojom::SecretMaterialReadResultPtr result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::optional<std::string> credential;
  if (status == service::EffectStatus::kCompleted && result &&
      !result->material.empty()) {
    credential.emplace(result->material.begin(), result->material.end());
  }
  ClearMaterial(result.get());
  std::move(callback).Run(std::move(credential));
}

}  // namespace taffy
