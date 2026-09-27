// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_CORE_EFFECT_BROKER_H_
#define TAFFY_BROWSER_CORE_EFFECT_BROKER_H_

#include <stdint.h>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "base/containers/flat_map.h"
#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "taffy/contracts/core-service/core_service.mojom.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy {

// Browser-authority broker for effects proposed by the isolated core.
//
// It enforces the ordering that recovery depends on:
//
//   durable intent -> dispatch -> durable terminal result -> callback
//
// Handlers are profile-owned adapters. They may use Chromium storage, network,
// WebContents, capability ledgers, platform UI, or isolated tool supervisors;
// the core process receives none of those objects or handles.
class CoreEffectBroker {
public:
  using CompletionCallback =
      base::OnceCallback<void(core_service::mojom::EffectResultPtr)>;
  using BootstrapCallback =
      base::OnceCallback<void(core_service::mojom::CoreBootstrapPtr)>;
  using JournalCallback = base::OnceCallback<void(bool)>;
  using CommitIntentHandler = base::RepeatingCallback<void(
      const core_service::mojom::EffectEnvelope &, JournalCallback)>;
  using CommitResultHandler = base::RepeatingCallback<void(
      const core_service::mojom::EffectResult &, JournalCallback)>;
  using DispatchHandler = base::RepeatingCallback<void(
      core_service::mojom::EffectEnvelopePtr, CompletionCallback)>;
  using ToolCompletionCallback =
      base::OnceCallback<void(core_service::mojom::ToolEffectResultPtr)>;
  using ToolDispatchHandler = base::RepeatingCallback<void(
      core_service::mojom::OperationEnvelopePtr, std::string,
      core_service::mojom::ToolJobEffectPtr, ToolCompletionCallback)>;
  using CancelGenerationHandler = base::RepeatingCallback<void(
      uint64_t, std::vector<std::string>)>;
  using CancelTaskHandler =
      base::RepeatingCallback<void(std::string_view, uint64_t)>;
  using RevokeCapabilitiesHandler = base::RepeatingCallback<void(uint64_t)>;
  using LoadBootstrapHandler =
      base::RepeatingCallback<void(uint64_t, bool, BootstrapCallback)>;

  struct Handlers {
    LoadBootstrapHandler load_bootstrap;
    CommitIntentHandler commit_intent;
    CommitResultHandler commit_result;
    DispatchHandler storage;
    DispatchHandler observation;
    DispatchHandler model;
    DispatchHandler network;
    DispatchHandler browser_action;
    ToolDispatchHandler tool;
    DispatchHandler secure_store;
    DispatchHandler auth_surface;
    DispatchHandler permission;
    DispatchHandler asset_delivery;
    CancelGenerationHandler cancel_generation;
    CancelTaskHandler cancel_task;
    RevokeCapabilitiesHandler revoke_capabilities;
  };

  explicit CoreEffectBroker(Handlers handlers);
  CoreEffectBroker(const CoreEffectBroker &) = delete;
  CoreEffectBroker &operator=(const CoreEffectBroker &) = delete;
  ~CoreEffectBroker();

  // Announces only empty/non-empty custody transitions. The profile manager
  // uses this to arm one idle deadline without polling this broker.
  void SetPendingChangedCallback(base::RepeatingClosure callback);

  void SetActiveGeneration(uint64_t generation);

  // Loads only bounded checkpoint and journal bytes. The storage adapter owns
  // the profile path; neither this broker nor the core service receives it.
  void LoadBootstrap(uint64_t generation, bool private_profile,
                     BootstrapCallback callback);

  // Dispatches at most once. Every accepted call receives exactly one terminal
  // callback, including journal failure, adapter absence, disconnect, and
  // malformed adapter output.
  void Dispatch(core_service::mojom::EffectEnvelopePtr effect,
                CompletionCallback callback);

  // Revokes browser authority and resolves every effect from `generation`.
  // Consequential dispatches become OUTCOME_UNKNOWN; all other work becomes
  // UNAVAILABLE. Returned results have already claimed their callback.
  void OnGenerationDisconnected(uint64_t generation);

  // Claims every exact task-owned callback. Consequential effects resolve as
  // OUTCOME_UNKNOWN; other work resolves as CANCELLED. Effects without a
  // typed task identity are never guessed into this set.
  void CancelTask(std::string_view task_id, uint64_t generation);
  // Runs `settled` only after every callback claimed by this cancellation has
  // received its journalled terminal result. An empty set settles
  // synchronously.
  void CancelTask(std::string_view task_id, uint64_t generation,
                  base::OnceClosure settled);

  // True while any dispatched effect is still owed its terminal result. The
  // isolated core blocks on that result, so this is half the proof that the
  // profile has no work in flight.
  bool HasPendingEffects() const;

  size_t pending_count_for_testing() const;
  uint64_t late_completion_count_for_testing() const {
    return late_completion_count_;
  }

private:
  friend class ComparisonVerticalDispatchGate;

  struct PendingEffect {
    core_service::mojom::EffectEnvelopePtr effect;
    CompletionCallback callback;
    bool terminal_claimed = false;
  };

  bool ValidateEffect(const core_service::mojom::EffectEnvelope &effect) const;
  DispatchHandler *HandlerFor(core_service::mojom::EffectKind kind);
  void OnIntentCommitted(std::string effect_id, bool committed);
  void OnAdapterCompleted(std::string effect_id,
                          core_service::mojom::EffectResultPtr result);
  void OnToolCompleted(std::string effect_id,
                       core_service::mojom::ToolEffectResultPtr result);
  void OnResultCommitted(std::string effect_id,
                         core_service::mojom::EffectResultPtr result,
                         bool committed);
  void Finish(std::string effect_id,
              core_service::mojom::EffectResultPtr result);
  void FinishUnavailable(std::string effect_id);
  core_service::mojom::EffectResultPtr
  MakeTerminal(const core_service::mojom::EffectEnvelope &effect,
               core_service::mojom::EffectStatus status) const;

  uint64_t active_generation_ = 0;
  Handlers handlers_;
  base::RepeatingClosure pending_changed_callback_;
  base::flat_map<std::string, PendingEffect> pending_;
  uint64_t late_completion_count_ = 0;

  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<CoreEffectBroker> weak_factory_{this};
};

} // namespace taffy

#endif // TAFFY_BROWSER_CORE_EFFECT_BROKER_H_
