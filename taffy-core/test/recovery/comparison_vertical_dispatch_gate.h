// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_TEST_RECOVERY_COMPARISON_VERTICAL_DISPATCH_GATE_H_
#define TAFFY_TEST_RECOVERY_COMPARISON_VERTICAL_DISPATCH_GATE_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "taffy/browser/core_effect_broker.h"

namespace taffy {

// Holds actual observation intents before the profile's durable writer. All
// other effects pass through; release forwards the original envelopes and the
// writer's actual success or refusal. This measures admitted overlap, without
// replacing policy, renderer observations, or storage outcomes.
class ComparisonVerticalDispatchGate final {
 public:
  struct Identity {
    std::string effect_id;
    std::string task_id;
    std::string action_id;
    std::string tab_id;
    uint64_t generation = 0u;
  };

  explicit ComparisonVerticalDispatchGate(CoreEffectBroker* broker);
  ComparisonVerticalDispatchGate(const ComparisonVerticalDispatchGate&) = delete;
  ComparisonVerticalDispatchGate& operator=(
      const ComparisonVerticalDispatchGate&) = delete;
  ~ComparisonVerticalDispatchGate();

  const std::vector<Identity>& identities() const { return identities_; }
  size_t held_count() const { return held_.size(); }
  bool unexpected_intent_seen() const { return unexpected_intent_seen_; }
  const char* refusal_reason() const { return refusal_reason_; }
  uint32_t model_intents_seen() const { return model_intents_seen_; }
  uint32_t commit_callbacks_seen() const { return commit_callbacks_seen_; }
  uint32_t committed_count() const { return committed_count_; }
  void ReleaseHeldIntents();

 private:
  struct HeldIntent {
    core_service::mojom::EffectEnvelopePtr effect;
    CoreEffectBroker::JournalCallback callback;
  };

  void CommitIntent(const core_service::mojom::EffectEnvelope& effect,
                    CoreEffectBroker::JournalCallback callback);
  void OnCommitted(bool committed);

  const raw_ptr<CoreEffectBroker> broker_;
  CoreEffectBroker::CommitIntentHandler downstream_;
  std::vector<HeldIntent> held_;
  std::vector<Identity> identities_;
  bool released_ = false;
  bool unexpected_intent_seen_ = false;
  const char* refusal_reason_ = "none";
  uint32_t model_intents_seen_ = 0u;
  uint32_t commit_callbacks_seen_ = 0u;
  uint32_t committed_count_ = 0u;
  base::WeakPtrFactory<ComparisonVerticalDispatchGate> weak_factory_{this};
};

}  // namespace taffy

#endif  // TAFFY_TEST_RECOVERY_COMPARISON_VERTICAL_DISPATCH_GATE_H_
