// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_MEMORY_PRESSURE_GOVERNOR_H_
#define TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_MEMORY_PRESSURE_GOVERNOR_H_

#include <stdint.h>

#include <memory>
#include <optional>

#include "base/memory_coordinator/memory_consumer.h"
#include "base/memory/raw_ptr.h"
#include "taffy/common/public/bip_budget.h"
#include "taffy/common/public/bip_result.h"

// What page intelligence gives up, and in what order, when the device runs
// short of memory (protocol section 15).
//
// "Memory pressure first stops deltas and optional extraction, then returns a
// recoverable resource error."
//
// Two words in that sentence are the whole design. *First* means the order is
// normative, not advisory: a build that refused observations while still
// running delta streams would be spending memory on an optimization while
// denying the thing the optimization exists to accelerate. *Recoverable* means
// the error says so — a resource error is a reason to retry later with a
// narrower scope, not a reason for a task to fail.
//
// The order is encoded as a stage rather than as a sequence of if-statements,
// because a stage can be asserted about. Every stage is strictly more
// restrictive than the one before it, the static assertions below pin that,
// and a caller asks "what may I do at this stage" rather than re-deriving the
// ladder.
//
// One thing this class does not do: block manual browsing. Nothing here
// touches navigation, rendering, or any browser surface. Page intelligence
// degrading is invisible to somebody who is only reading a page, which is what
// makes "BIP failure never blocks manual browsing" true at this layer as well.
//
// UI thread only.

namespace taffy {

// Strictly increasing restriction. The numeric order is the policy order.
enum class DegradationStage : uint8_t {
  // Everything runs.
  kNormal = 0,
  // Delta streams stop and optional adapters are dropped. Observations still
  // run, at a narrowed scope.
  kStreamsStopped = 1,
  // Observations are refused with a recoverable resource error. Actions
  // already authorized still settle: abandoning an action in flight would
  // leave a side effect nobody could reconcile, which costs more than the
  // memory it saves.
  kObservationsRefused = 2,
};

constexpr bool DeltasAllowedAt(DegradationStage stage) {
  return stage == DegradationStage::kNormal;
}

constexpr bool OptionalAdaptersAllowedAt(DegradationStage stage) {
  return stage == DegradationStage::kNormal;
}

constexpr bool ObservationsAllowedAt(DegradationStage stage) {
  return stage != DegradationStage::kObservationsRefused;
}

// Actions never stop for memory pressure. An in-flight dispatch has either
// already had its capability consumed or is about to, and dropping it would
// produce exactly the ambiguous outcome the journal exists to avoid.
constexpr bool ActionsAllowedAt(DegradationStage stage) {
  return true;
}

static_assert(DeltasAllowedAt(DegradationStage::kNormal) &&
                  !DeltasAllowedAt(DegradationStage::kStreamsStopped),
              "Deltas are the first thing given up.");
static_assert(ObservationsAllowedAt(DegradationStage::kStreamsStopped) &&
                  !ObservationsAllowedAt(DegradationStage::kObservationsRefused),
              "Observations are refused only after deltas have already "
              "stopped: protocol section 15 fixes that order.");
static_assert(static_cast<uint8_t>(DegradationStage::kNormal) <
                      static_cast<uint8_t>(DegradationStage::kStreamsStopped) &&
                  static_cast<uint8_t>(DegradationStage::kStreamsStopped) <
                      static_cast<uint8_t>(
                          DegradationStage::kObservationsRefused),
              "The numeric order of the stages is the policy order, and code "
              "compares them.");

// A base::MemoryConsumer, not a base::MemoryPressureListener.
//
// RESOLVED AT SP-01. The listener API this class was written against is gone in
// the form it assumed: at the pin base::MemoryPressureListener is an abstract
// CheckedObserver whose notifications require a separate
// MemoryPressureListenerRegistration carrying a MemoryPressureListenerTag from
// a closed enum in base/, and that enum has no value for a downstream component
// and no generic one. Registering as a pressure listener would therefore have
// meant patching a centrally-owned upstream enum, on an API upstream has
// already deprecated.
//
// base::MemoryConsumer is the successor that deprecation comment names, and it
// identifies consumers by a free-form string, so this migration costs the fork
// nothing. The shape it trades for that is a *limit percentage* rather than a
// discrete level; base/memory_coordinator/utils.h supplies the three
// thresholds that map the legacy levels onto it, and those exist precisely to
// carry migrations like this one, so the ladder below is unchanged at every
// point the old one defined.
class MemoryPressureGovernor : public base::MemoryConsumer {
 public:
  // What the governor turns off. Implemented by the service, which owns the
  // things being turned off.
  class Delegate {
   public:
    virtual ~Delegate() = default;

    // Stops every delta stream in this tab and tells each subscriber why. Must
    // be safe to call when there are none.
    virtual void StopDeltaStreamsForMemoryPressure() = 0;

    // The stage changed. The service reports it so a degraded result is
    // explicable rather than mysterious.
    virtual void OnDegradationStageChanged(DegradationStage stage) = 0;
  };

  explicit MemoryPressureGovernor(Delegate* delegate);
  MemoryPressureGovernor(const MemoryPressureGovernor&) = delete;
  MemoryPressureGovernor& operator=(const MemoryPressureGovernor&) = delete;
  // `override`: base::MemoryConsumer derives from CheckedObserver, whose
  // destructor is virtual.
  ~MemoryPressureGovernor() override;

  DegradationStage stage() const { return stage_; }

  // The result code an observation gets at the current stage, or nullopt when
  // it may proceed. Recoverable by construction: the caller is expected to
  // retry with a narrower scope rather than to fail the task.
  std::optional<ObservationResultCode> AdmitObservation() const;

  // The scope an observation runs at once the stage has narrowed it. Never
  // wider than what was requested.
  ObservationScope NarrowScope(ObservationScope requested) const;

  // Whether an optional adapter may still be requested. A required adapter is
  // not dropped here: dropping one would make a result that is labelled
  // complete incomplete, and protocol section 6.2 requires UNSUPPORTED
  // instead.
  bool AllowsOptionalAdapters() const {
    return OptionalAdaptersAllowedAt(stage_);
  }

  // Drives the ladder directly. Used by the tests, and by any embedder that
  // learns about pressure through a channel other than the memory coordinator.
  void SetStageForTesting(DegradationStage stage);

  // base::MemoryConsumer:
  void OnUpdateMemoryLimit() override;
  void OnReleaseMemory() override;

 private:
  // The stage a given limit percentage puts the governor in. Total, so there is
  // no unmapped limit to fail open on.
  static DegradationStage StageForMemoryLimit(int memory_limit);

  void MoveTo(DegradationStage stage);

  const raw_ptr<Delegate> delegate_;
  DegradationStage stage_ = DegradationStage::kNormal;

  // Declared last: registration makes `this` reachable from the coordinator, so
  // every other member must already be initialised when it runs.
  base::MemoryConsumerRegistration registration_;
};

}  // namespace taffy

#endif  // TAFFY_COMPONENTS_INTELLIGENCE_CONTENT_MEMORY_PRESSURE_GOVERNOR_H_
