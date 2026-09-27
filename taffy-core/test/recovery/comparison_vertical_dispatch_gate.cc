// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/test/recovery/comparison_vertical_dispatch_gate.h"

#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"

namespace taffy {

namespace service = core_service::mojom;

ComparisonVerticalDispatchGate::ComparisonVerticalDispatchGate(
    CoreEffectBroker* broker)
    : broker_(broker) {
  CHECK(broker_);
  CHECK(!broker_->HasPendingEffects());
  downstream_ = broker_->handlers_.commit_intent;
  CHECK(downstream_);
  broker_->handlers_.commit_intent = base::BindRepeating(
      &ComparisonVerticalDispatchGate::CommitIntent, base::Unretained(this));
}

ComparisonVerticalDispatchGate::~ComparisonVerticalDispatchGate() {
  broker_->handlers_.commit_intent = downstream_;
  // Failed assertions must release custody. Successful tests always send the
  // held intent to the real writer; this cleanup explicitly refuses it.
  auto held = std::move(held_);
  for (auto& intent : held) {
    std::move(intent.callback).Run(false);
  }
}

void ComparisonVerticalDispatchGate::CommitIntent(
    const service::EffectEnvelope& effect,
    CoreEffectBroker::JournalCallback callback) {
  if (effect.kind == service::EffectKind::kModelRequest) {
    ++model_intents_seen_;
    unexpected_intent_seen_ |= !released_;
    if (!released_) {
      refusal_reason_ = "model-before-read-release";
    }
  }
  if (released_ || effect.kind != service::EffectKind::kPageObservation) {
    downstream_.Run(effect, std::move(callback));
    return;
  }
  if (!effect.page_observation || !effect.operation ||
      effect.page_observation->task_id.empty() ||
      effect.page_observation->tab_id.empty() || identities_.size() >= 2u) {
    unexpected_intent_seen_ = true;
    refusal_reason_ = identities_.size() >= 2u
                          ? "more-than-two-observations"
                          : "incomplete-task-observation-binding";
    std::move(callback).Run(false);
    return;
  }
  const auto& observation = *effect.page_observation;
  if (!identities_.empty() &&
      (identities_.front().task_id != observation.task_id ||
       identities_.front().tab_id == observation.tab_id ||
       identities_.front().generation != effect.operation->service_generation)) {
    unexpected_intent_seen_ = true;
    refusal_reason_ = "task-generation-or-distinct-tab-mismatch";
  }
  identities_.push_back(Identity{effect.effect_id, observation.task_id,
                                  observation.action_id, observation.tab_id,
                                  effect.operation->service_generation});
  held_.push_back(HeldIntent{effect.Clone(), std::move(callback)});
}

void ComparisonVerticalDispatchGate::ReleaseHeldIntents() {
  CHECK(!released_);
  CHECK_EQ(held_.size(), 2u);
  released_ = true;
  auto held = std::move(held_);
  for (auto& intent : held) {
    downstream_.Run(
        *intent.effect,
        base::BindOnce(
            [](base::WeakPtr<ComparisonVerticalDispatchGate> gate,
               CoreEffectBroker::JournalCallback original, bool committed) {
              if (gate) {
                gate->OnCommitted(committed);
              }
              std::move(original).Run(committed);
            },
            weak_factory_.GetWeakPtr(), std::move(intent.callback)));
  }
}

void ComparisonVerticalDispatchGate::OnCommitted(bool committed) {
  ++commit_callbacks_seen_;
  committed_count_ += committed ? 1u : 0u;
}

}  // namespace taffy
