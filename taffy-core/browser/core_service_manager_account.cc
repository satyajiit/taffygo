// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_service_manager.h"

#include <limits>
#include <ranges>
#include <utility>

#include "base/functional/bind.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "mojo/public/cpp/bindings/callback_helpers.h"
#include "taffy/browser/account/profile_account_broker.h"
#include "taffy/browser/providerauth/profile_provider_auth_broker.h"

namespace taffy {
namespace {

uint64_t NowMonotonicMillis() {
  const int64_t value = base::TimeTicks::Now().since_origin().InMilliseconds();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

core_service::mojom::CoreServiceCommandPtr BuildCallbackCommand(
    core_service::mojom::AuthCallbackCommandPtr receipt,
    uint64_t service_generation,
    uint64_t now_monotonic_ms) {
  if (!receipt) {
    return nullptr;
  }
  constexpr uint64_t kCallbackCommandLifetimeMs = 30'000u;
  const uint64_t deadline =
      now_monotonic_ms >
              std::numeric_limits<uint64_t>::max() - kCallbackCommandLifetimeMs
          ? std::numeric_limits<uint64_t>::max()
          : now_monotonic_ms + kCallbackCommandLifetimeMs;
  const std::string operation_id =
      "auth-callback-" + base::Uuid::GenerateRandomV4().AsLowercaseString();
  auto command = core_service::mojom::CoreServiceCommand::New();
  command->operation = core_service::mojom::OperationEnvelope::New(
      operation_id, service_generation, 0u, deadline,
      "auth-callback-key-" +
          base::Uuid::GenerateRandomV4().AsLowercaseString());
  command->kind = core_service::mojom::CoreServiceCommandKind::kAuthCallback;
  command->auth_callback = std::move(receipt);
  return command;
}

}  // namespace

void CoreServiceManager::BindPlatformAdapter(
    mojo::PendingRemote<
        browser::account::mojom::TaffyProfilePlatformAdapter> adapter) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (shutdown_started_ || !account_broker_) {
    return;
  }
  account_broker_->BindPlatformAdapter(std::move(adapter));
  EnsureStarted();
}

void CoreServiceManager::OnAccountFlowTerminated(
    uint64_t callback_generation,
    core_service::mojom::AuthCallbackCommandPtr receipt) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  RefreshIdleTeardown();
  if (!receipt || shutdown_started_ ||
      availability_ != Availability::kReady ||
      callback_generation != service_generation_) {
    return;
  }
  auto command = BuildCallbackCommand(std::move(receipt), service_generation_,
                                      NowMonotonicMillis());
  if (!command) {
    return;
  }
  Submit(std::move(command),
         base::BindOnce([](core_service::mojom::AdmissionPtr) {}));
}

bool CoreServiceManager::DeliverAuthCallback(std::string raw_uri) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (shutdown_started_ || !account_broker_) {
    return false;
  }
  // Nothing on Android reaches this function any more, and the provider
  // fall-through below could not fire even when something did.
  //
  // The comment that used to stand here said the two planes share one
  // custom-scheme intent filter and route by URI prefix. They never did.
  // Chromium patch 0022 registered exactly one filter,
  // `com.taffygo.browser://auth`, and `AndroidAuthRedirectAdapter` admitted
  // only `host == "auth"` — so a `com.taffygo.browser://provider-auth` URI
  // never arrived here, and `ParseProviderAuthCallback` requires that prefix.
  // A vendor's redirect arrives by a different road entirely:
  // `ProviderAuthRedirectThrottle` claims it mid-navigation through
  // `ClaimProviderRedirectNavigation`, which is why provider sign-in works
  // with no filter of its own.
  //
  // Both halves of that are now past tense. The adapter and the shell ingress
  // were deleted with the account intent ingress, and patch 0022 no longer
  // registers the filter, so this entry point has no Java caller and the URI
  // it was written for resolves to nothing. `DeliverProviderCallback` keeps
  // its one unreachable production caller here, and every other is its own
  // unit test; it leaves with the account plane rather than being removed
  // here, where the diff would be stranded from the rest of that removal.
  const bool account_claimed = account_broker_->DeliverAuthCallback(
      raw_uri,
      base::BindOnce(
          [](base::WeakPtr<CoreServiceManager> manager,
             uint64_t callback_generation,
             core_service::mojom::AuthCallbackCommandPtr receipt,
             ProfileAccountBroker::RedirectSubmissionCallback submitted) {
            if (!manager || !receipt || manager->shutdown_started_ ||
                manager->availability_ != Availability::kReady ||
                callback_generation != manager->service_generation_) {
              std::move(submitted).Run(false);
              return;
            }
            auto command = BuildCallbackCommand(std::move(receipt),
                                                manager->service_generation_,
                                                NowMonotonicMillis());
            if (!command) {
              std::move(submitted).Run(false);
              return;
            }
            manager->Submit(
                std::move(command),
                base::BindOnce(
                    [](ProfileAccountBroker::RedirectSubmissionCallback done,
                       core_service::mojom::AdmissionPtr admission) {
                      std::move(done).Run(
                          admission &&
                          admission->status ==
                              core_service::mojom::AdmissionStatus::kAccepted);
                    },
                    std::move(submitted)));
          },
          weak_factory_.GetWeakPtr()));
  if (account_claimed) {
    return true;
  }
  return provider_auth_broker_ &&
         provider_auth_broker_->DeliverProviderCallback(std::move(raw_uri));
}

// Lives beside the other account-plane glue rather than in the manager's
// core file: the token validator is the account broker's seam, and the
// zeroing rule on a late reply belongs where a reviewer reads that seam.
void CoreServiceManager::ValidateAccountTokenResponse(
    core_service::mojom::AccountTokenValidationRequestPtr request,
    base::OnceCallback<void(core_service::mojom::AccountTokenValidationResultPtr)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (shutdown_started_ || private_profile_ ||
      availability_ != Availability::kReady || !session_.is_bound() ||
      !request || !request->operation ||
      request->operation->service_generation != service_generation_) {
    std::move(callback).Run(nullptr);
    return;
  }
  const uint64_t generation = service_generation_;
  session_->ValidateAccountTokenResponse(
      std::move(request),
      mojo::WrapCallbackWithDefaultInvokeIfNotRun(
          base::BindOnce(
              [](base::WeakPtr<CoreServiceManager> manager,
                 uint64_t expected_generation,
                 base::OnceCallback<void(
                     core_service::mojom::AccountTokenValidationResultPtr)>
                     callback,
                 core_service::mojom::AccountTokenValidationResultPtr result) {
                if (!manager ||
                    manager->service_generation_ != expected_generation ||
                    manager->availability_ != Availability::kReady) {
                  if (result) {
                    std::ranges::fill(result->access_token, 0u);
                    std::ranges::fill(result->refresh_token, 0u);
                    result->access_token.clear();
                    result->refresh_token.clear();
                  }
                  result.reset();
                }
                std::move(callback).Run(std::move(result));
              },
              weak_factory_.GetWeakPtr(), generation, std::move(callback)),
          nullptr));
}

}  // namespace taffy
