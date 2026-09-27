// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/account/profile_account_broker.h"

#include <algorithm>
#include <utility>

#include "base/time/time.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/storage_partition.h"
#include "mojo/public/cpp/bindings/callback_helpers.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "taffy/browser/account_plane_configuration.h"
#include "taffy/browser/core_account_effect_terminal.h"

namespace taffy {
namespace {

namespace account_mojom = browser::account::mojom;
namespace service = core_service::mojom;

uint64_t NowMonotonicMillisForSecureStore() {
  const int64_t value = base::TimeTicks::Now().since_origin().InMilliseconds();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

void ClearTransientWriteMaterial(service::EffectEnvelope *effect) {
  if (!effect || !effect->secure_store ||
      !effect->secure_store->write_transient) {
    return;
  }
  std::ranges::fill(effect->secure_store->write_transient->material, 0u);
  effect->secure_store->write_transient->material.clear();
}

} // namespace

// The broker takes the browser context rather than the Profile deliberately.
// Everything it needs — whether the context is off the record, and its default
// storage partition's browser-process loader factory — is //content API, so the
// account plane names no //chrome type and this directory's DEPS file allows
// the profile header only in the four JNI bridges.
ProfileAccountBroker::ProfileAccountBroker(
    content::BrowserContext *browser_context)
    : browser_context_(browser_context),
      private_profile_(browser_context && browser_context->IsOffTheRecord()),
      url_loader_factory_(
          browser_context ? browser_context->GetDefaultStoragePartition()
                                ->GetURLLoaderFactoryForBrowserProcess()
                          : nullptr) {
  CHECK(browser_context_);
  CHECK(url_loader_factory_);
}

ProfileAccountBroker::~ProfileAccountBroker() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

void ProfileAccountBroker::BindPlatformAdapter(
    mojo::PendingRemote<account_mojom::TaffyProfilePlatformAdapter> adapter) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!adapter.is_valid()) {
    return;
  }
  platform_adapter_.reset();
  platform_adapter_.Bind(std::move(adapter));
  InstallPlatformDisconnectHandler();
  platform_adapter_->ClearTransient(
      base::BindOnce([](service::EffectStatus, uint32_t) {}));
}

void ProfileAccountBroker::SetTokenValidator(TokenValidator validator) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  token_validator_ = std::move(validator);
}

void ProfileAccountBroker::SetFlowTerminalCallback(
    FlowTerminalCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  flow_terminal_callback_ = std::move(callback);
}

service::EffectResultPtr
ProfileAccountBroker::MakeResult(const service::EffectEnvelope &effect,
                                 service::EffectStatus status) const {
  auto result = service::EffectResult::New();
  result->operation = effect.operation.Clone();
  result->effect_id = effect.effect_id;
  result->status = status;
  result->kind = effect.kind;
  if (!PopulateCoreAccountTerminal(effect, result.get())) {
    return nullptr;
  }
  return result;
}

void ProfileAccountBroker::DispatchSecureStore(
    service::EffectEnvelopePtr effect, EffectCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!AccountPlaneAllowsProfile(private_profile_)) {
    ClearTransientWriteMaterial(effect.get());
    std::move(callback).Run(
        effect ? MakeResult(*effect, service::EffectStatus::kDenied) : nullptr);
    return;
  }
  if (!effect || !effect->operation || !effect->secure_store ||
      !AcceptGeneration(effect->operation->service_generation) ||
      !platform_adapter_.is_bound()) {
    ClearTransientWriteMaterial(effect.get());
    std::move(callback).Run(
        effect ? MakeResult(*effect, service::EffectStatus::kUnavailable)
               : nullptr);
    return;
  }
  switch (effect->secure_store->operation_kind) {
  case service::SecureStoreOperation::kGenerateEntropy:
    if (!effect->secure_store->generate_entropy) {
      break;
    }
    platform_adapter_->GenerateEntropy(
        effect->secure_store->generate_entropy.Clone(),
        mojo::WrapCallbackWithDefaultInvokeIfNotRun(
            base::BindOnce(&ProfileAccountBroker::OnGeneratedEntropy,
                           weak_factory_.GetWeakPtr(), std::move(effect),
                           std::move(callback)),
            service::EffectStatus::kUnavailable, nullptr));
    return;
  case service::SecureStoreOperation::kWriteTransient:
    if (!effect->secure_store->write_transient) {
      break;
    }
    {
      auto request = effect->secure_store->write_transient.Clone();
      ClearTransientWriteMaterial(effect.get());
      platform_adapter_->WriteTransient(
          std::move(request),
          mojo::WrapCallbackWithDefaultInvokeIfNotRun(
              base::BindOnce(&ProfileAccountBroker::OnTransientWritten,
                             weak_factory_.GetWeakPtr(), std::move(effect),
                             std::move(callback)),
              service::EffectStatus::kUnavailable, nullptr));
      return;
    }
  case service::SecureStoreOperation::kDeleteHandle:
    if (!effect->secure_store->delete_handle) {
      break;
    }
    platform_adapter_->DeleteSecret(
        effect->secure_store->delete_handle.Clone(),
        mojo::WrapCallbackWithDefaultInvokeIfNotRun(
            base::BindOnce(&ProfileAccountBroker::OnSecretDeleted,
                           weak_factory_.GetWeakPtr(), std::move(effect),
                           std::move(callback)),
            service::EffectStatus::kUnavailable, nullptr));
    return;
  }
  ClearTransientWriteMaterial(effect.get());
  std::move(callback).Run(
      MakeResult(*effect, service::EffectStatus::kInvalidResult));
}

void ProfileAccountBroker::OnGeneratedEntropy(
    service::EffectEnvelopePtr effect, EffectCallback callback,
    service::EffectStatus status, service::GeneratedEntropyResultPtr body) {
  if (status == service::EffectStatus::kCompleted && effect->operation &&
      effect->operation->deadline_monotonic_ms <=
          NowMonotonicMillisForSecureStore()) {
    if (body) {
      std::ranges::fill(body->entropy, 0u);
      body->entropy.clear();
    }
    status = service::EffectStatus::kDeadlineExceeded;
  }
  auto result = MakeResult(*effect, status);
  if (result && status == service::EffectStatus::kCompleted && body) {
    result->secure_store->generated_entropy = std::move(body);
  } else if (status == service::EffectStatus::kCompleted) {
    result->status = service::EffectStatus::kInvalidResult;
  }
  std::move(callback).Run(std::move(result));
}

void ProfileAccountBroker::OnTransientWritten(
    service::EffectEnvelopePtr effect, EffectCallback callback,
    service::EffectStatus status, service::TransientSecretWriteResultPtr body) {
  if (status == service::EffectStatus::kCompleted && effect->operation &&
      effect->operation->deadline_monotonic_ms <=
          NowMonotonicMillisForSecureStore()) {
    if (body) {
      DeleteTransientBestEffort(body->secret_handle);
    }
    status = service::EffectStatus::kDeadlineExceeded;
  }
  auto result = MakeResult(*effect, status);
  if (result && status == service::EffectStatus::kCompleted && body) {
    result->secure_store->transient_write = std::move(body);
  } else if (status == service::EffectStatus::kCompleted) {
    result->status = service::EffectStatus::kInvalidResult;
  }
  std::move(callback).Run(std::move(result));
}

void ProfileAccountBroker::OnSecretDeleted(
    service::EffectEnvelopePtr effect, EffectCallback callback,
    service::EffectStatus status, service::DeletedSecretHandleResultPtr body) {
  if (status == service::EffectStatus::kCompleted && effect->operation &&
      effect->operation->deadline_monotonic_ms <=
          NowMonotonicMillisForSecureStore()) {
    status = service::EffectStatus::kDeadlineExceeded;
  }
  auto result = MakeResult(*effect, status);
  if (result && status == service::EffectStatus::kCompleted && body) {
    result->secure_store->deleted_handle = std::move(body);
  } else if (status == service::EffectStatus::kCompleted) {
    result->status = service::EffectStatus::kInvalidResult;
  }
  std::move(callback).Run(std::move(result));
}

} // namespace taffy
