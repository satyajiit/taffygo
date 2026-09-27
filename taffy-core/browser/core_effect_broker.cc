// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_effect_broker.h"

#include <cstddef>
#include <utility>

#include "base/functional/bind.h"
#include "base/logging.h"
#include "taffy/browser/core_account_effect_terminal.h"
#include "taffy/browser/core_effect_broker_terminal.h"
#include "taffy/browser/core_effect_validation.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy {

namespace mojom = core_service::mojom;
namespace {

bool BypassesEffectJournal(const mojom::EffectEnvelope& effect) {
  return effect.kind == mojom::EffectKind::kStorageCommit ||
         (effect.kind == mojom::EffectKind::kModelRequest &&
          effect.model_request && effect.model_request->task_id.empty() &&
          effect.retry_class == mojom::RetryClass::kNever);
}

} // namespace

CoreEffectBroker::CoreEffectBroker(Handlers handlers)
    : handlers_(std::move(handlers)) {}

CoreEffectBroker::~CoreEffectBroker() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (active_generation_ != 0) {
    OnGenerationDisconnected(active_generation_);
  }
}

void CoreEffectBroker::SetPendingChangedCallback(
    base::RepeatingClosure callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  pending_changed_callback_ = std::move(callback);
}

void CoreEffectBroker::SetActiveGeneration(uint64_t generation) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // A generation can advance only after the previous one was resolved.
  if (active_generation_ != 0 && active_generation_ != generation) {
    OnGenerationDisconnected(active_generation_);
  }
  active_generation_ = generation;
}

void CoreEffectBroker::LoadBootstrap(uint64_t generation, bool private_profile,
                                     BootstrapCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!handlers_.load_bootstrap || generation == 0) {
    std::move(callback).Run(nullptr);
    return;
  }
  handlers_.load_bootstrap.Run(generation, private_profile,
                               std::move(callback));
}

void CoreEffectBroker::Dispatch(mojom::EffectEnvelopePtr effect,
                                CompletionCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const char* refusal = nullptr;
  if (!effect) {
    refusal = "null";
  } else if (!ValidateEffect(*effect)) {
    refusal = "invalid";
  } else if (pending_.size() >= mojom::kMaxInFlightPerProfile) {
    refusal = "backpressure";
  } else if (pending_.contains(effect->effect_id)) {
    refusal = "duplicate-id";
  }
  if (refusal) {
    // The kind is an enumerator and the reason a compiled-in label; neither
    // names a value, a URL or a file. Without this line a storage commit that
    // carried a start died here and the sheet timed out with nothing to read.
    LOG(WARNING) << "[taffy_effect_refused] at=" << refusal << " kind="
                 << (effect ? static_cast<int>(effect->kind) : -1);
    if (effect && effect->operation) {
      std::move(callback).Run(
          MakeTerminal(*effect, mojom::EffectStatus::kInvalidResult));
    } else {
      std::move(callback).Run(nullptr);
    }
    return;
  }

  const std::string effect_id = effect->effect_id;
  const bool was_empty = pending_.empty();
  pending_.emplace(effect_id,
                   PendingEffect{std::move(effect), std::move(callback)});
  if (was_empty && pending_changed_callback_) {
    pending_changed_callback_.Run();
  }

  // STORAGE_COMMIT is itself the atomic command/events/audit/effect-intent
  // batch. A task-less, non-retryable model call is deliberately live: no
  // durable task can replay it, and journalling it would turn a composer
  // suggestion or credential probe into a permanent execution claim. Both
  // still use the ordinary typed adapter and exactly-one terminal path.
  auto it = pending_.find(effect_id);
  if (BypassesEffectJournal(*it->second.effect)) {
    DispatchHandler *handler = HandlerFor(it->second.effect->kind);
    if (!handler || !*handler) {
      LOG(WARNING) << "[taffy_effect_unavailable] at=no-live-handler kind="
                   << static_cast<int>(it->second.effect->kind);
      FinishUnavailable(effect_id);
      return;
    }
    handler->Run(it->second.effect.Clone(),
                 base::BindOnce(&CoreEffectBroker::OnAdapterCompleted,
                                weak_factory_.GetWeakPtr(), effect_id));
    return;
  }

  if (!handlers_.commit_intent) {
    // Dispatch is forbidden until a profile storage writer can prove the
    // intent durable. This is fail-closed unavailability, never an in-memory
    // journal or a second execution path.
    LOG(WARNING) << "[taffy_effect_unavailable] at=no-commit-intent kind="
                 << static_cast<int>(it->second.effect->kind);
    FinishUnavailable(effect_id);
    return;
  }

  handlers_.commit_intent.Run(
      *it->second.effect,
      base::BindOnce(&CoreEffectBroker::OnIntentCommitted,
                     weak_factory_.GetWeakPtr(), effect_id));
}

bool CoreEffectBroker::HasPendingEffects() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return !pending_.empty();
}

size_t CoreEffectBroker::pending_count_for_testing() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return pending_.size();
}

bool CoreEffectBroker::ValidateEffect(
    const mojom::EffectEnvelope &effect) const {
  return IsValidCoreEffectEnvelope(effect, active_generation_,
                                   mojom::kMaxIdentifierBytes,
                                   mojom::kMaxEffectBytes);
}

CoreEffectBroker::DispatchHandler *
CoreEffectBroker::HandlerFor(mojom::EffectKind kind) {
  switch (kind) {
  case mojom::EffectKind::kStorageCommit:
    return &handlers_.storage;
  case mojom::EffectKind::kPageObservation:
    return &handlers_.observation;
  case mojom::EffectKind::kModelRequest:
    return &handlers_.model;
  case mojom::EffectKind::kNetworkRequest:
    return &handlers_.network;
  case mojom::EffectKind::kBrowserAction:
    return &handlers_.browser_action;
  case mojom::EffectKind::kToolJob:
    // Not absent - differently shaped. The tool adapter takes the operation
    // envelope and the typed job and answers with a ToolEffectResult, so it
    // cannot be one of these and is dispatched by OnIntentCommitted directly.
    return nullptr;
  case mojom::EffectKind::kSecureStore:
    return &handlers_.secure_store;
  case mojom::EffectKind::kOpenAuthSurface:
    return &handlers_.auth_surface;
  case mojom::EffectKind::kRequestPermission:
    return &handlers_.permission;
  case mojom::EffectKind::kDeliverAsset:
    return &handlers_.asset_delivery;
  case mojom::EffectKind::kFetchCatalog:
    // Unroutable, and now unreachable as well. This was the served catalog's
    // fetch, planned and delivered through a pair of `CoreSession` methods
    // rather than through `EmitEffect`; decision 0200 removed the host that
    // served it, the protocol that judged it and both of those methods. The
    // member stays because a frozen wire value leaves only at the contract
    // reset, and nothing emits one, so an envelope claiming this kind is
    // refused rather than dispatched — which is what it always was.
    return nullptr;
  case mojom::EffectKind::kFetchProviderListing:
  case mojom::EffectKind::kDeliverComposerCompletion:
  case mojom::EffectKind::kProbeCustomEndpoint:
    // Unroutable here for a different reason than the catalog fetch: all three
    // are answered where they arrive, in CoreServiceManager::EmitEffect,
    // because none of them is durable work. The listing and the probe are
    // questions about a person's own endpoint and the completion is one push at
    // a surface that may not be attached; journalling an intent for any of them
    // would make a durable claim about something that only matters live.
    //
    // Probe identities now include the browser session, service generation
    // and attempt. That correlation prevents a late live callback from
    // matching another incarnation; it does not make live work durable.
    return nullptr;
  }
  return nullptr;
}

void CoreEffectBroker::OnIntentCommitted(std::string effect_id,
                                         bool committed) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto it = pending_.find(effect_id);
  if (it == pending_.end()) {
    return;
  }
  // A disconnect or task cancellation may claim the terminal while the
  // durable-intent callback is still crossing back to this sequence. That
  // terminal owns the effect; a late successful insert is not permission to
  // dispatch work for a generation that has already been revoked.
  if (it->second.terminal_claimed) {
    return;
  }
  if (!committed) {
    const mojom::EffectStatus status =
        it->second.effect->retry_class == mojom::RetryClass::kConsequential
            ? mojom::EffectStatus::kOutcomeUnknown
            : mojom::EffectStatus::kUnavailable;
    Finish(effect_id, MakeTerminal(*it->second.effect, status));
    return;
  }

  // A tool job takes the other dispatch shape. Its adapter answers with a
  // typed ToolEffectResult rather than an EffectResult, because a worker's
  // terminal carries ordered chunks and progress this broker has to keep
  // beside the status, and the envelope is what the supervisor needs to reach
  // the isolated process. Everything before and after it - the journalled
  // intent, the single terminal, the cancellation claim - is identical.
  if (it->second.effect->kind == mojom::EffectKind::kToolJob) {
    if (!handlers_.tool || !it->second.effect->tool_job) {
      OnAdapterCompleted(
          effect_id,
          MakeTerminal(*it->second.effect, mojom::EffectStatus::kUnavailable));
      return;
    }
    handlers_.tool.Run(it->second.effect->operation.Clone(), effect_id,
                       it->second.effect->tool_job.Clone(),
                       base::BindOnce(&CoreEffectBroker::OnToolCompleted,
                                      weak_factory_.GetWeakPtr(), effect_id));
    return;
  }

  DispatchHandler *handler = HandlerFor(it->second.effect->kind);
  if (!handler || !*handler) {
    OnAdapterCompleted(
        effect_id,
        MakeTerminal(*it->second.effect, mojom::EffectStatus::kUnavailable));
    return;
  }

  mojom::EffectEnvelopePtr dispatch = it->second.effect.Clone();
  handler->Run(std::move(dispatch),
               base::BindOnce(&CoreEffectBroker::OnAdapterCompleted,
                              weak_factory_.GetWeakPtr(), effect_id));
}

void CoreEffectBroker::OnToolCompleted(std::string effect_id,
                                       mojom::ToolEffectResultPtr tool_result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto it = pending_.find(effect_id);
  if (it == pending_.end()) {
    ++late_completion_count_;
    return;
  }
  if (!tool_result) {
    OnAdapterCompleted(
        effect_id,
        MakeTerminal(*it->second.effect, mojom::EffectStatus::kInvalidResult));
    return;
  }
  auto result = mojom::EffectResult::New();
  result->operation = it->second.effect->operation.Clone();
  result->effect_id = effect_id;
  result->status = EffectStatusFor(tool_result->status);
  result->kind = mojom::EffectKind::kToolJob;
  result->tool = std::move(tool_result);
  OnAdapterCompleted(std::move(effect_id), std::move(result));
}

void CoreEffectBroker::OnAdapterCompleted(std::string effect_id,
                                          mojom::EffectResultPtr result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto it = pending_.find(effect_id);
  if (it == pending_.end()) {
    ++late_completion_count_;
    return;
  }
  if (it->second.terminal_claimed) {
    ++late_completion_count_;
    return;
  }
  it->second.terminal_claimed = true;

  const mojom::EffectEnvelope &effect = *it->second.effect;
  if (!result ||
      !IsValidCoreEffectResult(effect, *result, mojom::kMaxEffectBytes)) {
    result = MakeTerminal(
        effect, effect.retry_class == mojom::RetryClass::kConsequential
                    ? mojom::EffectStatus::kOutcomeUnknown
                    : mojom::EffectStatus::kInvalidResult);
  }

  if (BypassesEffectJournal(effect)) {
    Finish(std::move(effect_id), std::move(result));
    return;
  }

  if (!handlers_.commit_result) {
    // A consequential adapter may already have acted. Losing the result journal
    // is therefore ambiguous rather than ordinary unavailability.
    const mojom::EffectStatus status =
        effect.retry_class == mojom::RetryClass::kConsequential
            ? mojom::EffectStatus::kOutcomeUnknown
            : mojom::EffectStatus::kUnavailable;
    Finish(effect_id, MakeTerminal(effect, status));
    return;
  }

  mojom::EffectResultPtr journal_copy = result.Clone();
  handlers_.commit_result.Run(
      *journal_copy,
      base::BindOnce(&CoreEffectBroker::OnResultCommitted,
                     weak_factory_.GetWeakPtr(), effect_id, std::move(result)));
}

void CoreEffectBroker::OnResultCommitted(std::string effect_id,
                                         mojom::EffectResultPtr result,
                                         bool committed) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto it = pending_.find(effect_id);
  if (it == pending_.end()) {
    ++late_completion_count_;
    return;
  }
  if (!committed) {
    const mojom::EffectStatus status =
        it->second.effect->retry_class == mojom::RetryClass::kConsequential
            ? mojom::EffectStatus::kOutcomeUnknown
            : mojom::EffectStatus::kUnavailable;
    result = MakeTerminal(*it->second.effect, status);
  }
  Finish(std::move(effect_id), std::move(result));
}

void CoreEffectBroker::Finish(std::string effect_id,
                              mojom::EffectResultPtr result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto it = pending_.find(effect_id);
  if (it == pending_.end()) {
    ++late_completion_count_;
    return;
  }
  CompletionCallback callback = std::move(it->second.callback);
  pending_.erase(it);
  if (pending_.empty() && pending_changed_callback_) {
    pending_changed_callback_.Run();
  }
  std::move(callback).Run(std::move(result));
}

void CoreEffectBroker::FinishUnavailable(std::string effect_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto it = pending_.find(effect_id);
  if (it == pending_.end()) {
    return;
  }
  Finish(effect_id,
         MakeTerminal(*it->second.effect, mojom::EffectStatus::kUnavailable));
}

mojom::EffectResultPtr
CoreEffectBroker::MakeTerminal(const mojom::EffectEnvelope &effect,
                               mojom::EffectStatus status) const {
  auto result = mojom::EffectResult::New();
  result->operation = effect.operation.Clone();
  result->effect_id = effect.effect_id;
  result->status = status;
  result->kind = effect.kind;
  switch (effect.kind) {
  case mojom::EffectKind::kStorageCommit:
    result->storage = mojom::StorageEffectResult::New();
    break;
  case mojom::EffectKind::kPageObservation:
    result->observation = mojom::ObservationEffectResult::New();
    break;
  case mojom::EffectKind::kModelRequest:
    result->model = mojom::ModelEffectResult::New();
    break;
  case mojom::EffectKind::kNetworkRequest:
    PopulateCoreAccountTerminal(effect, result.get());
    break;
  case mojom::EffectKind::kBrowserAction:
    result->browser_action = mojom::BrowserActionEffectResult::New();
    result->browser_action->outcome =
        status == mojom::EffectStatus::kOutcomeUnknown
            ? mojom::BrowserActionOutcome::kOutcomeUnknown
            : mojom::BrowserActionOutcome::kRefused;
    break;
  case mojom::EffectKind::kToolJob:
    result->tool = mojom::ToolEffectResult::New();
    if (effect.tool_job) {
      result->tool->job_id = effect.tool_job->job_id;
    }
    result->tool->status = ToolTerminalStatusFor(status);
    break;
  case mojom::EffectKind::kSecureStore:
    PopulateCoreAccountTerminal(effect, result.get());
    break;
  case mojom::EffectKind::kOpenAuthSurface:
    PopulateCoreAccountTerminal(effect, result.get());
    break;
  case mojom::EffectKind::kRequestPermission:
    result->permission = mojom::PermissionEffectResult::New();
    if (effect.permission_request) {
      result->permission->request_id = effect.permission_request->request_id;
      result->permission->permission = effect.permission_request->permission;
    }
    result->permission->decision = mojom::PermissionDecision::kUnavailable;
    break;
  case mojom::EffectKind::kDeliverAsset:
    PopulateAssetDeliveryTerminal(effect, result.get());
    break;
  case mojom::EffectKind::kFetchCatalog:
    // MakeTerminal is the unavailable/refused path. A catalog fetch that
    // never left this broker still has a catalog body so the core can
    // name the disposition instead of a missing-body protocol error.
    result->catalog = mojom::CatalogFetchEffectResult::New();
    result->catalog->operation_kind =
        effect.catalog_fetch ? effect.catalog_fetch->operation_kind
                             : mojom::CatalogNetworkOperation::kFetchPublishedCatalog;
    result->catalog->disposition = mojom::CatalogFetchDisposition::kUnavailable;
    break;
  case mojom::EffectKind::kFetchProviderListing:
  case mojom::EffectKind::kDeliverComposerCompletion:
  case mojom::EffectKind::kProbeCustomEndpoint:
    PopulateProviderPlaneTerminal(effect, result.get());
    break;
  }
  return result;
}

} // namespace taffy
