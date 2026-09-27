// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/check.h"
#include "base/logging.h"
#include "base/functional/bind.h"
#include "base/time/time.h"
#include "mojo/public/cpp/bindings/callback_helpers.h"
#include "taffy/browser/core_account_effect_terminal.h"
#include "taffy/browser/core_effect_broker_terminal.h"
#include "taffy/browser/core_deferred_task_surface_owner.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/model/profile_entitlement_cache.h"
#include "taffy/browser/model/profile_model_broker.h"
#include "taffy/browser/model/profile_provider_listing_fetcher.h"
#include "taffy/browser/policy_response_validation.h"
#include "taffy/browser/profile_model_register.h"
#include "taffy/browser/providerauth/provider_access_resolver.h"

namespace taffy {

namespace service_mojom = core_service::mojom;

namespace {

std::optional<core_api::mojom::PlatformPermission> ToCoreApiPermission(
    service_mojom::PlatformPermission permission) {
  switch (permission) {
    case service_mojom::PlatformPermission::kNotifications:
      return core_api::mojom::PlatformPermission::kNotifications;
    case service_mojom::PlatformPermission::kMicrophone:
      return core_api::mojom::PlatformPermission::kMicrophone;
    case service_mojom::PlatformPermission::kCamera:
      return core_api::mojom::PlatformPermission::kCamera;
    case service_mojom::PlatformPermission::kLocation:
      return core_api::mojom::PlatformPermission::kLocation;
  }
  return std::nullopt;
}

uint64_t NowMonotonicMillis() {
  const int64_t value = base::TimeTicks::Now().since_origin().InMilliseconds();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

}  // namespace

void CoreServiceManager::DeliverModelStreamChunk(
    service_mojom::ModelStreamChunkPtr chunk,
    base::OnceCallback<void(service_mojom::ModelStreamChunkStatus)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!chunk || !chunk->operation ||
      chunk->operation->service_generation != service_generation_ ||
      availability_ != Availability::kReady || !session_.is_bound()) {
    std::move(callback).Run(
        service_mojom::ModelStreamChunkStatus::kUnavailable);
    return;
  }
  session_->DeliverModelStreamChunk(
      std::move(chunk),
      mojo::WrapCallbackWithDefaultInvokeIfNotRun(
          std::move(callback),
          service_mojom::ModelStreamChunkStatus::kUnavailable));
}

void CoreServiceManager::NoteLateReply(const char* where) {
  ++late_reply_count_;
  // A label and two numbers: where it was dropped, which generation this
  // manager is on, and how many have been dropped. Never the reply itself.
  LOG(WARNING) << "[taffy_core_late_reply] at=" << where
               << " generation=" << service_generation_
               << " count=" << late_reply_count_;
}

void CoreServiceManager::OnEffectCompleted(
    uint64_t generation,
    service_mojom::EffectResultPtr result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (generation != service_generation_ || !session_.is_bound() || !result ||
      !result->operation ||
      result->operation->service_generation != service_generation_) {
    NoteLateReply("effect-completed");
    return;
  }
  if (result->kind == service_mojom::EffectKind::kStorageCommit) {
    accepted_approvals_.RecordStorageCompletion(*result);
  }
  const bool reconcile_account = RequiresAccountReconciliation(*result);
  // Two entitlement facts are read off the completion before it is moved
  // away (decision 0082). A completed sign-in exchange is the moment a mint
  // can first succeed, and the poke rides the same ordered pipe as the
  // delivery, so the core has stored the session before it decides. A
  // completed revocation is the moment the browser's own token custody must
  // forget what it holds — the cache's contents were the departed account's.
  const bool session_established =
      result->status == service_mojom::EffectStatus::kCompleted &&
      result->kind == service_mojom::EffectKind::kNetworkRequest &&
      result->network &&
      (result->network->authorization_code_session ||
       result->network->native_credential_session);
  const bool session_revoked =
      result->status == service_mojom::EffectStatus::kCompleted &&
      result->kind == service_mojom::EffectKind::kNetworkRequest &&
      result->network && result->network->revoked_session;
  session_->DeliverEffectResult(std::move(result));
  if (session_revoked && entitlement_cache_) {
    entitlement_cache_->Clear();
  }
  if (session_established) {
    PokeEntitlementRefresh(service_mojom::EntitlementFetchReason::kSignIn);
  }
  if (reconcile_account) {
    // The ambiguous terminal is already journalled. Retire the entire
    // generation before its core can propose another account mutation. The
    // next lazy start must clear and reconcile the vault/SQL halves before it
    // restores any account state; tabs and manual browsing remain untouched.
    teardown_reason_ = TeardownReason::kAccountReconciliation;
    HandleDisconnect(/*unexpected=*/false);
  }
}

void CoreServiceManager::OnLiveEffectCompleted(
    uint64_t generation,
    std::string effect_id,
    service_mojom::EffectResultPtr result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (generation != service_generation_ ||
      pending_live_effect_ids_.erase(effect_id) != 1u) {
    NoteLateReply("live-effect-completed");
    return;
  }
  RefreshIdleTeardown();
  OnEffectCompleted(generation, std::move(result));
}

bool CoreServiceManager::DeliverComposerCompletion(
    const std::string& request_id,
    const std::optional<std::string>& text) {
  // Counted rather than assumed. `observers_` is read elsewhere as "is any
  // Taffy surface watching this profile", and that is exactly the question the
  // core is asking with `delivered`: a completion pushed at nobody is not a
  // completion the person saw.
  bool delivered = false;
  for (Observer& observer : observers_) {
    observer.OnComposerCompletion(request_id, text);
    delivered = true;
  }
  return delivered;
}

void CoreServiceManager::EmitEffect(service_mojom::EffectEnvelopePtr effect) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!effect || !effect->operation ||
      effect->operation->service_generation != service_generation_ ||
      availability_ != Availability::kReady) {
    NoteLateReply("emit-effect");
    return;
  }
  // Ask for every kind: an operation staged as browser-held authority may
  // emit only its exact, unexpired storage commit. A reused operation identity
  // must not escape through one of the live effect paths below.
  if (accepted_approvals_.BindStorageCommit(*effect, NowMonotonicMillis()) ==
      AuthorityStorageBinding::kInvalid) {
    NoteLateReply("emit-effect/authority-binding");
    return;
  }
  if (effect->kind == service_mojom::EffectKind::kRequestPermission) {
    if (!effect->permission_request) {
      ++late_reply_count_;
      return;
    }
    const service_mojom::PermissionRequestEffect& request =
        *effect->permission_request;
    const std::optional<PendingPermissionLookup> pending =
        FindPendingPermission(request.request_id);
    const auto api_permission = ToCoreApiPermission(request.permission);
    if (!pending || !api_permission || pending->task_id != request.task_id ||
        pending->permission != request.permission ||
        pending->task_revision != request.task_revision ||
        pending->service_generation != service_generation_ ||
        effect->operation->task_revision != request.task_revision ||
        pending->deadline_monotonic_ms !=
            effect->operation->deadline_monotonic_ms ||
        NowMonotonicMillis() >= pending->deadline_monotonic_ms) {
      ++late_reply_count_;
      return;
    }
    if (!emitted_permission_requests_.insert(request.request_id).second) {
      return;
    }
    for (Observer& observer : observers_) {
      observer.OnCorePermissionRequest(request.request_id, *api_permission);
    }
    return;
  }
  if (effect->kind == service_mojom::EffectKind::kDeliverComposerCompletion) {
    if (!effect->composer_completion) {
      ++late_reply_count_;
      return;
    }
    // Answered here rather than through the effect broker, and the reason is
    // the same one the asset-progress push carries: this is a live push at a
    // surface, not durable work. Journalling an intent for it would make a
    // durable execution claim about a suggestion that is worthless a moment
    // later, and a replay after a restart would offer a completion for text
    // the person has already finished typing.
    auto result = service_mojom::EffectResult::New();
    result->operation = effect->operation.Clone();
    result->effect_id = effect->effect_id;
    result->status = service_mojom::EffectStatus::kCompleted;
    result->kind = service_mojom::EffectKind::kDeliverComposerCompletion;
    const bool delivered =
        DeliverComposerCompletion(effect->composer_completion->request_id,
                                  effect->composer_completion->text);
    result->composer_completion =
        service_mojom::ComposerCompletionEffectResult::New(
            effect->composer_completion->request_id, delivered);
    OnEffectCompleted(service_generation_, std::move(result));
    return;
  }
  if (effect->kind == service_mojom::EffectKind::kFetchProviderListing) {
    if (!effect->provider_listing_fetch) {
      ++late_reply_count_;
      return;
    }
    // A listing is a live, read-only question and is not journalled as durable
    // task work (decision 0100). The fetcher owns the whole transport rule:
    // the closed provider/family route, the exact cap and deadline, secret
    // placement, and cancellation. The manager contributes only the resolver
    // whose far end is Android's sealed store and correlates the terminal to
    // this service generation once more before it can cross back.
    ProfileProviderListingFetcher::CredentialResolver resolver;
    if (provider_access_resolver_) {
      resolver = base::BindRepeating(
          &ProviderAccessResolver::Resolve,
          base::Unretained(provider_access_resolver_.get()));
    }
    const uint64_t generation = service_generation_;
    const std::string effect_id = effect->effect_id;
    if (effect_id.empty() ||
        !pending_live_effect_ids_.insert(effect_id).second) {
      ++late_reply_count_;
      return;
    }
    RefreshIdleTeardown();
    provider_listing_fetcher_->Perform(
        std::move(effect), resolver,
        base::BindOnce(&CoreServiceManager::OnLiveEffectCompleted,
                       weak_factory_.GetWeakPtr(), generation, effect_id));
    return;
  }
  if (effect->kind == service_mojom::EffectKind::kProbeCustomEndpoint) {
    if (!effect->custom_endpoint_probe) {
      ++late_reply_count_;
      return;
    }
    // Answered here rather than through the effect broker, for the listing
    // fetch's reason and one of its own. A probe is a live question about an
    // address a person is still typing, so journalling a durable execution
    // intent for it would claim something only true for the moment it is asked
    // — and a durable claim would outlive the live question it represented.
    // The core still names browser session, generation and attempt so a late
    // callback cannot correlate with another live incarnation; that does not
    // make the question durable (decisions 0099 and 0100).
    //
    // Unlike the listing, this one is actually performed. The prober is in
    // taffy/browser/model/custom_endpoint_prober.h and the leg that resolves
    // the credential and shapes the verdict is
    // ProfileModelBroker::DispatchEndpointProbe.
    const std::string effect_id = effect->effect_id;
    if (effect_id.empty() ||
        !pending_live_effect_ids_.insert(effect_id).second) {
      ++late_reply_count_;
      return;
    }
    RefreshIdleTeardown();
    if (!model_broker_) {
      auto result = service_mojom::EffectResult::New();
      result->operation = effect->operation.Clone();
      result->effect_id = effect->effect_id;
      result->status = service_mojom::EffectStatus::kUnavailable;
      result->kind = service_mojom::EffectKind::kProbeCustomEndpoint;
      PopulateProviderPlaneTerminal(*effect, result.get());
      OnLiveEffectCompleted(service_generation_, effect_id, std::move(result));
      return;
    }
    model_broker_->DispatchEndpointProbe(
        std::move(effect),
        base::BindOnce(&CoreServiceManager::OnLiveEffectCompleted,
                       weak_factory_.GetWeakPtr(), service_generation_,
                       effect_id));
    return;
  }
  effect_broker_->Dispatch(
      std::move(effect),
      base::BindOnce(&CoreServiceManager::OnEffectCompleted,
                     weak_factory_.GetWeakPtr(), service_generation_));
}

void CoreServiceManager::PublishState(
    service_mojom::CoreStateUpdatePtr update) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!update || state_bindings_.service_generation() != service_generation_ ||
      state_bindings_.state_sequence() != update->sequence) {
    model_register_->Withdraw();
    ResolvePendingTaskSurfacesUnavailable();
    state_bindings_.Reset();
    accepted_approvals_.Reset();
    NoteLateReply("publish-state/bindings");
    return;
  }
  std::optional<ProfileModelRegister::PreparedSnapshot> prepared_models =
      model_register_->PrepareSnapshot(update->model_artifacts);
  if (!prepared_models ||
      !state_cache_.Accept(std::move(update), service_generation_)) {
    model_register_->Withdraw();
    ResolvePendingTaskSurfacesUnavailable();
    state_bindings_.Reset();
    accepted_approvals_.Reset();
    NoteLateReply("publish-state/accept");
    return;
  }
  const service_mojom::CoreStateUpdate* state = state_cache_.latest();
  CHECK(state);
  model_register_->CommitPreparedSnapshot(std::move(*prepared_models));
  // Retire an admitted guard before a synchronous observer can answer a new
  // surface from this state. The initial surface publication cannot release a
  // held command because its sequence is not strictly newer; the post-observer
  // pass below still catches answers submitted from this callback.
  CoreDeferredTaskSurfaceOwner::Reconcile(*this);
  for (Observer& observer : observers_) {
    observer.OnCoreState(*state);
  }
  CoreDeferredTaskSurfaceOwner::CompletePublished(*this, state->sequence);
  // A command accepted during an observer callback above remains browser-held
  // until a later publication proves that the surface effect itself committed.
  // Reconciliation runs after both public hand-offs, so ownership can never be
  // mistaken for Core admission or an unpublished binding revision.
  CoreDeferredTaskSurfaceOwner::Reconcile(*this);
}

void CoreServiceManager::ResolvePendingPolicyUnavailable() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto pending = std::move(pending_policy_evaluations_);
  pending_policy_evaluations_.clear();
  RefreshIdleTeardown();
  for (auto& [operation_id, request] : pending) {
    auto result = service_mojom::PolicyEvaluationResult::New();
    result->operation_id = operation_id;
    result->status = service_mojom::PolicyEvaluationStatus::kCoreUnavailable;
    std::move(request.callback).Run(std::move(result));
  }
}

}  // namespace taffy
