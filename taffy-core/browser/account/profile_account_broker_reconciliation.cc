// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/account/profile_account_broker.h"

#include <algorithm>
#include <ranges>
#include <string_view>
#include <utility>

#include "base/functional/bind.h"
#include "mojo/public/cpp/bindings/callback_helpers.h"
#include "taffy/browser/account_plane_configuration.h"

namespace taffy {
namespace {

namespace account_mojom = browser::account::mojom;
namespace service = core_service::mojom;

bool IsOpaqueHandle(std::string_view value) {
  return !value.empty() && value.size() <= service::kMaxIdentifierBytes &&
         std::ranges::all_of(value, [](const char character) {
           return (character >= 'A' && character <= 'Z') ||
                  (character >= 'a' && character <= 'z') ||
                  (character >= '0' && character <= '9') || character == '-' ||
                  character == '_';
         });
}

}  // namespace

void ProfileAccountBroker::InspectCanonicalSession(
    SessionInspectionCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!AccountPlaneAllowsProfile(private_profile_) ||
      !platform_adapter_.is_bound()) {
    std::move(callback).Run(false, std::nullopt, 0u);
    return;
  }
  platform_adapter_->InspectCurrentSession(
      mojo::WrapCallbackWithDefaultInvokeIfNotRun(
          base::BindOnce(&ProfileAccountBroker::OnCanonicalSessionInspected,
                         weak_factory_.GetWeakPtr(), std::move(callback)),
          service::EffectStatus::kUnavailable, nullptr));
}

void ProfileAccountBroker::OnCanonicalSessionInspected(
    SessionInspectionCallback callback,
    service::EffectStatus status,
    account_mojom::SessionMaterialMetadataPtr metadata) {
  if (status != service::EffectStatus::kCompleted) {
    std::move(callback).Run(false, std::nullopt, 0u);
    return;
  }
  if (!metadata) {
    std::move(callback).Run(true, std::nullopt, 0u);
    return;
  }
  if (!IsOpaqueHandle(metadata->session_handle)) {
    std::move(callback).Run(false, std::nullopt, 0u);
    return;
  }
  std::move(callback).Run(true, std::move(metadata->session_handle),
                          metadata->rotation);
}


void ProfileAccountBroker::ReadCanonicalAccessToken(
    base::OnceCallback<void(std::optional<std::string>)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!AccountPlaneAllowsProfile(private_profile_) ||
      !platform_adapter_.is_bound()) {
    std::move(callback).Run(std::nullopt);
    return;
  }
  platform_adapter_->InspectCurrentSession(
      mojo::WrapCallbackWithDefaultInvokeIfNotRun(
          base::BindOnce(&ProfileAccountBroker::OnAccessTokenSessionInspected,
                         weak_factory_.GetWeakPtr(), std::move(callback)),
          service::EffectStatus::kUnavailable, nullptr));
}

void ProfileAccountBroker::OnAccessTokenSessionInspected(
    base::OnceCallback<void(std::optional<std::string>)> callback,
    service::EffectStatus status,
    account_mojom::SessionMaterialMetadataPtr metadata) {
  if (status != service::EffectStatus::kCompleted || !metadata ||
      !IsOpaqueHandle(metadata->session_handle)) {
    std::move(callback).Run(std::nullopt);
    return;
  }
  platform_adapter_->ReadCurrentSession(
      metadata->session_handle,
      mojo::WrapCallbackWithDefaultInvokeIfNotRun(
          base::BindOnce(&ProfileAccountBroker::OnAccessTokenSessionRead,
                         weak_factory_.GetWeakPtr(), std::move(callback)),
          service::EffectStatus::kUnavailable, nullptr));
}

void ProfileAccountBroker::OnAccessTokenSessionRead(
    base::OnceCallback<void(std::optional<std::string>)> callback,
    service::EffectStatus status,
    account_mojom::SessionMaterialReadResultPtr result) {
  if (status != service::EffectStatus::kCompleted || !result ||
      result->material.empty()) {
    if (result) {
      std::ranges::fill(result->material, 0u);
      result->material.clear();
    }
    std::move(callback).Run(std::nullopt);
    return;
  }
  std::optional<SessionPayload> payload = DecodeSessionPayload(result->material);
  std::ranges::fill(result->material, 0u);
  result->material.clear();
  if (!payload) {
    std::move(callback).Run(std::nullopt);
    return;
  }
  // Only the access token leaves; the payload's destructor zeroes what
  // remains — the refresh token above all — before the callback sees a byte.
  std::string access_token = std::move(payload->access_token);
  payload.reset();
  std::move(callback).Run(std::move(access_token));
}

void ProfileAccountBroker::ClearCanonicalSession(
    base::OnceCallback<void(bool)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!AccountPlaneAllowsProfile(private_profile_) ||
      !platform_adapter_.is_bound()) {
    std::move(callback).Run(false);
    return;
  }
  platform_adapter_->ClearSession(mojo::WrapCallbackWithDefaultInvokeIfNotRun(
      base::BindOnce(
          [](base::OnceCallback<void(bool)> done,
             service::EffectStatus status, bool) {
            std::move(done).Run(status == service::EffectStatus::kCompleted);
          },
          std::move(callback)),
      service::EffectStatus::kUnavailable, false));
}

}  // namespace taffy
