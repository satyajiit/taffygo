// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/intelligence/content/memory_pressure_governor.h"

#include <utility>

#include "base/check.h"
#include "base/logging.h"
#include "base/memory_coordinator/traits.h"
#include "base/memory_coordinator/utils.h"

namespace taffy {
namespace {

// What stopping every delta stream in a tab actually reclaims, and what it
// costs. The protocol bounds it: max_delta_queue_depth is 64 messages and
// max_message_bytes is 256 KiB, so the worst case is roughly 16 MiB per tab —
// "tens of MBs" rather than "under 10".
//
// kLossless is the trait that matters most here, and it is the reason the
// protocol allows this degradation at all: a subscriber whose stream stops can
// resnapshot. No user state is lost, so the coordinator is free to prefer this
// consumer over one that would discard something a person typed.
constexpr base::MemoryConsumerTraits kTaffyGovernorTraits(
    base::MemoryConsumerTraits::EstimatedMemoryUsage::kMedium,
    base::MemoryConsumerTraits::ReleaseMemoryCost::kFreesPagesWithoutTraversal,
    base::MemoryConsumerTraits::InformationRetention::kLossless,
    base::MemoryConsumerTraits::ExecutionType::kSynchronous);

}  // namespace

MemoryPressureGovernor::MemoryPressureGovernor(Delegate* delegate)
    : delegate_(delegate),
      registration_("TaffyPageIntelligence", kTaffyGovernorTraits, this) {
  CHECK(delegate_);
  // Seeded here on purpose. A synchronous registration deliberately does NOT
  // call OnUpdateMemoryLimit(), to avoid re-entering a half-built object, so a
  // consumer that derives state from the limit has to read it once itself
  // (base/memory_coordinator/memory_consumer.h). Skipping this is the exact
  // failure the old code was left broken rather than commit: a governor that
  // starts at kNormal on a device already under pressure reports that
  // everything is fine until the next notification arrives.
  //
  // MoveTo is not used: it would announce a stage change to a delegate that is
  // still constructing this object.
  stage_ = StageForMemoryLimit(memory_limit());
  LOG(INFO) << "[taffy_bip_memory_budget] event=initial limit="
            << memory_limit() << " stage=" << static_cast<int>(stage_);
}

MemoryPressureGovernor::~MemoryPressureGovernor() = default;

// static
DegradationStage MemoryPressureGovernor::StageForMemoryLimit(int memory_limit) {
  // The three constants are base's own migration aids
  // (base/memory_coordinator/utils.h): 100 is "no pressure", 50 is "moderate",
  // 0 is "critical". Mapping through them keeps this ladder identical to the
  // level-based one at all three points the level-based one could express.
  //
  // Written as thresholds rather than equality because a limit is continuous
  // and may take values the old enumeration had no name for — and may exceed
  // 100. Any reduction below "no pressure" stops deltas, which is the first
  // thing protocol section 15 gives up; only the critical threshold refuses
  // observations. Erring toward the more restrictive stage on an intermediate
  // value costs a retry, which is what a recoverable error is for.
  if (memory_limit > base::kModerateMemoryPressureThreshold) {
    return memory_limit >= base::kNoMemoryPressureThreshold
               ? DegradationStage::kNormal
               : DegradationStage::kStreamsStopped;
  }
  return memory_limit > base::kCriticalMemoryPressureThreshold
             ? DegradationStage::kStreamsStopped
             : DegradationStage::kObservationsRefused;
}

void MemoryPressureGovernor::OnUpdateMemoryLimit() {
  MoveTo(StageForMemoryLimit(memory_limit()));
}

void MemoryPressureGovernor::OnReleaseMemory() {
  // A deliberate deviation from the usual split, and the reason is protocol
  // section 15's ordering. Upstream's advice is that OnUpdateMemoryLimit only
  // records a limit and OnReleaseMemory does the freeing, which suits a cache
  // that can be trimmed independently of the limit that sizes it. Here the
  // freeing *is* the degradation: stopping the streams and announcing the stage
  // are one event, and the protocol fixes their order — streams stop first, so
  // that by the time anything observes the new stage the memory is already
  // back. Splitting them would invert that.
  //
  // So MoveTo above already stops the streams when it crosses out of kNormal,
  // and this call is the coordinator's explicit demand, honoured
  // unconditionally. It is safe to repeat: the delegate's contract requires
  // StopDeltaStreamsForMemoryPressure to be a no-op when there are no streams.
  delegate_->StopDeltaStreamsForMemoryPressure();
}

void MemoryPressureGovernor::MoveTo(DegradationStage stage) {
  if (stage == stage_) {
    return;
  }
  const DegradationStage previous = stage_;
  stage_ = stage;

  // The order in protocol section 15 is "first stops deltas and optional
  // extraction, then returns a recoverable resource error". Stopping the
  // streams before announcing the stage is that order: by the time anything
  // observes the new stage, the memory the streams were holding is already
  // released.
  if (DeltasAllowedAt(previous) && !DeltasAllowedAt(stage_)) {
    delegate_->StopDeltaStreamsForMemoryPressure();
  }
  LOG(INFO) << "[taffy_bip_memory_budget] event=transition limit="
            << memory_limit() << " stage=" << static_cast<int>(stage_);
  delegate_->OnDegradationStageChanged(stage_);
}

void MemoryPressureGovernor::SetStageForTesting(DegradationStage stage) {
  MoveTo(stage);
}

std::optional<ObservationResultCode> MemoryPressureGovernor::AdmitObservation()
    const {
  if (ObservationsAllowedAt(stage_)) {
    return std::nullopt;
  }
  // Recoverable, and named as such. A caller that reads this is expected to
  // retry with a narrower scope later, not to fail the task; anything stronger
  // would make a transient device condition look like a permanent one.
  return ObservationResultCode::kResourcePressure;
}

ObservationScope MemoryPressureGovernor::NarrowScope(
    ObservationScope requested) const {
  if (stage_ == DegradationStage::kNormal) {
    return requested;
  }
  // The viewport is what a person can actually see, so it is the scope that
  // keeps an observation useful while costing the least. Requests already
  // narrower than it are left alone: narrowing is a clamp, never a change.
  return ObservationScopeBreadth(requested) <=
                 ObservationScopeBreadth(ObservationScope::kViewport)
             ? requested
             : ObservationScope::kViewport;
}

}  // namespace taffy
