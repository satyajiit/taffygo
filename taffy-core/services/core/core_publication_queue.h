// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_SERVICES_CORE_CORE_PUBLICATION_QUEUE_H_
#define TAFFY_SERVICES_CORE_CORE_PUBLICATION_QUEUE_H_

#include <stddef.h>

#include <deque>
#include <vector>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/services/core/rust_core.h"

namespace taffy {

// Owns every publication that is waiting on the browser and enforces one
// shared item/byte-pressure budget across submissions, batches, states, and
// task effects. Callers may move work between those lanes, but cannot bypass
// the budget.
class CorePublicationQueue final {
 public:
  static constexpr size_t kMaxRetainedItems =
      4u * core_service::mojom::kMaxInFlightPerProfile;
  static constexpr size_t kMaxRetainedBytes =
      core_service::mojom::kMaxQueuedBytesPerProfile;
  static constexpr size_t kSubmissionReservationBytes =
      kMaxRetainedBytes / core_service::mojom::kMaxInFlightPerProfile;

  CorePublicationQueue();
  CorePublicationQueue(const CorePublicationQueue&) = delete;
  CorePublicationQueue& operator=(const CorePublicationQueue&) = delete;
  ~CorePublicationQueue();

  // Reserves one fair share before a command can mutate the Rust runtime.
  // Its eventual batch atomically replaces this reservation.
  bool TryReserveSubmission();
  void ReleaseSubmissionReservation();
  bool TryPushSubmittedBatch(CoreResponseBatch batch);

  bool TryPushBatch(CoreResponseBatch batch);
  bool HasBatches() const;
  CoreResponseBatch& FrontBatch();
  CoreResponseBatch TakeBatchAndRetainStates();

  bool HasStates() const;
  CoreStatePublication& FrontState();
  CoreStatePublication TakeStateAndRetainTaskEffects();

  bool TryPushTaskEffects(
      std::vector<core_service::mojom::TaskEffectBindingPtr> effects);
  bool HasTaskEffects() const;
  const core_service::mojom::TaskEffectBinding* FrontTaskEffect() const;
  core_service::mojom::TaskEffectBindingPtr TakeTaskEffect();

  size_t retained_items() const { return retained_items_; }
  // Includes one virtual fair share per outstanding Rust submission.
  size_t retained_bytes() const { return retained_bytes_; }
  size_t submission_reservations() const { return submission_reservations_; }
  void Clear();

 private:
  struct Footprint {
    size_t items = 0u;
    size_t bytes = 0u;
  };
  struct StateFootprint {
    Footprint total;
    std::vector<Footprint> task_effects;
  };
  struct BatchEntry {
    CoreResponseBatch value;
    Footprint footprint;
    std::vector<StateFootprint> states;
  };
  struct StateEntry {
    CoreStatePublication value;
    Footprint footprint;
    std::vector<Footprint> task_effects;
  };
  struct TaskEffectEntry {
    core_service::mojom::TaskEffectBindingPtr value;
    Footprint footprint;
  };

  bool CanReserve(Footprint footprint) const;
  void Reserve(Footprint footprint);
  void Release(Footprint footprint);

  std::deque<BatchEntry> batches_;
  std::deque<StateEntry> states_;
  std::deque<TaskEffectEntry> task_effects_;
  size_t retained_items_ = 0u;
  size_t retained_bytes_ = 0u;
  size_t submission_reservations_ = 0u;
};

}  // namespace taffy

#endif  // TAFFY_SERVICES_CORE_CORE_PUBLICATION_QUEUE_H_
