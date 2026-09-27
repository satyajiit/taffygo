// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/services/core/core_publication_queue.h"

#include <limits>
#include <optional>
#include <utility>

#include "mojo/public/cpp/bindings/lib/serialization.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

struct MeasuredFootprint {
  size_t items = 0u;
  size_t bytes = 0u;
};

struct MeasuredState {
  MeasuredFootprint total;
  std::vector<MeasuredFootprint> task_effects;
};

struct MeasuredBatch {
  MeasuredFootprint total;
  std::vector<MeasuredState> states;
};

class FootprintBuilder final {
 public:
  bool Add(size_t items, size_t bytes) {
    if (items > std::numeric_limits<size_t>::max() - items_ ||
        bytes > std::numeric_limits<size_t>::max() - bytes_) {
      valid_ = false;
      return false;
    }
    items_ += items;
    bytes_ += bytes;
    return true;
  }

  std::optional<MeasuredFootprint> Finish() const {
    if (!valid_) {
      return std::nullopt;
    }
    return MeasuredFootprint{items_, bytes_};
  }

 private:
  size_t items_ = 0u;
  size_t bytes_ = 0u;
  bool valid_ = true;
};

template <typename MojomType, typename Pointer>
bool AddWireObject(Pointer& value, FootprintBuilder* builder) {
  const size_t bytes =
      value ? MojomType::SerializeAsMessage(&value).payload_num_bytes() : 0u;
  return builder->Add(1u, bytes);
}

bool AddEffect(mojom::EffectEnvelopePtr& effect, FootprintBuilder* builder) {
  return AddWireObject<mojom::EffectEnvelope>(effect, builder);
}

bool AddTaskEffect(mojom::TaskEffectBindingPtr& effect,
                   FootprintBuilder* builder) {
  return AddWireObject<mojom::TaskEffectBinding>(effect, builder);
}

std::optional<MeasuredFootprint> MeasureTaskEffect(
    mojom::TaskEffectBindingPtr& effect) {
  FootprintBuilder builder;
  if (!AddTaskEffect(effect, &builder)) {
    return std::nullopt;
  }
  return builder.Finish();
}

std::optional<MeasuredState> MeasureState(CoreStatePublication& state) {
  FootprintBuilder builder;
  if (!AddWireObject<mojom::CoreStateUpdate>(state.state, &builder) ||
      !AddWireObject<mojom::CoreStateBrowserBindings>(state.browser_bindings,
                                                      &builder)) {
    return std::nullopt;
  }
  for (auto& effect : state.effects) {
    if (!AddEffect(effect, &builder)) {
      return std::nullopt;
    }
  }
  std::vector<MeasuredFootprint> task_effects;
  task_effects.reserve(state.task_effects.size());
  for (auto& effect : state.task_effects) {
    const std::optional<MeasuredFootprint> measured = MeasureTaskEffect(effect);
    if (!measured || !builder.Add(measured->items, measured->bytes)) {
      return std::nullopt;
    }
    task_effects.push_back(*measured);
  }
  std::optional<MeasuredFootprint> total = builder.Finish();
  if (!total) {
    return std::nullopt;
  }
  return MeasuredState{*total, std::move(task_effects)};
}

std::optional<MeasuredBatch> MeasureBatch(CoreResponseBatch& batch) {
  FootprintBuilder builder;
  if (!builder.Add(1u, batch.superseded_effect_id.size()) ||
      !AddWireObject<mojom::Admission>(batch.admission, &builder)) {
    return std::nullopt;
  }
  for (auto& effect : batch.effects) {
    if (!AddEffect(effect, &builder)) {
      return std::nullopt;
    }
  }
  std::vector<MeasuredState> states;
  states.reserve(batch.states.size());
  for (CoreStatePublication& state : batch.states) {
    std::optional<MeasuredState> measured = MeasureState(state);
    if (!measured ||
        !builder.Add(measured->total.items, measured->total.bytes)) {
      return std::nullopt;
    }
    states.push_back(std::move(*measured));
  }
  for (auto& event : batch.task_answer_events) {
    if (!AddWireObject<mojom::TaskAnswerEvent>(event, &builder)) {
      return std::nullopt;
    }
  }
  std::optional<MeasuredFootprint> total = builder.Finish();
  if (!total) {
    return std::nullopt;
  }
  return MeasuredBatch{*total, std::move(states)};
}

}  // namespace

CorePublicationQueue::CorePublicationQueue() = default;
CorePublicationQueue::~CorePublicationQueue() = default;

bool CorePublicationQueue::TryReserveSubmission() {
  if (submission_reservations_ >= mojom::kMaxInFlightPerProfile) {
    return false;
  }
  const Footprint reservation{1u, kSubmissionReservationBytes};
  if (!CanReserve(reservation)) {
    return false;
  }
  Reserve(reservation);
  ++submission_reservations_;
  return true;
}

void CorePublicationQueue::ReleaseSubmissionReservation() {
  if (submission_reservations_ == 0u) {
    return;
  }
  --submission_reservations_;
  Release({1u, kSubmissionReservationBytes});
}

bool CorePublicationQueue::TryPushSubmittedBatch(CoreResponseBatch batch) {
  if (submission_reservations_ == 0u) {
    return false;
  }
  ReleaseSubmissionReservation();
  return TryPushBatch(std::move(batch));
}

bool CorePublicationQueue::TryPushBatch(CoreResponseBatch batch) {
  std::optional<MeasuredBatch> measured = MeasureBatch(batch);
  if (!measured) {
    return false;
  }
  const Footprint footprint{measured->total.items, measured->total.bytes};
  if (!CanReserve(footprint)) {
    return false;
  }
  std::vector<StateFootprint> states;
  states.reserve(measured->states.size());
  for (MeasuredState& measured_state : measured->states) {
    std::vector<Footprint> task_effects;
    task_effects.reserve(measured_state.task_effects.size());
    for (MeasuredFootprint measured_effect : measured_state.task_effects) {
      task_effects.push_back({measured_effect.items, measured_effect.bytes});
    }
    states.push_back({{measured_state.total.items, measured_state.total.bytes},
                      std::move(task_effects)});
  }
  Reserve(footprint);
  batches_.push_back({std::move(batch), footprint, std::move(states)});
  return true;
}

bool CorePublicationQueue::HasBatches() const {
  return !batches_.empty();
}

CoreResponseBatch& CorePublicationQueue::FrontBatch() {
  return batches_.front().value;
}

CoreResponseBatch CorePublicationQueue::TakeBatchAndRetainStates() {
  BatchEntry entry = std::move(batches_.front());
  batches_.pop_front();
  Footprint retained;
  for (size_t index = 0u; index < entry.value.states.size(); ++index) {
    retained.items += entry.states[index].total.items;
    retained.bytes += entry.states[index].total.bytes;
    states_.push_back({std::move(entry.value.states[index]),
                       entry.states[index].total,
                       std::move(entry.states[index].task_effects)});
  }
  entry.value.states.clear();
  Release({entry.footprint.items - retained.items,
           entry.footprint.bytes - retained.bytes});
  return std::move(entry.value);
}

bool CorePublicationQueue::HasStates() const {
  return !states_.empty();
}

CoreStatePublication& CorePublicationQueue::FrontState() {
  return states_.front().value;
}

CoreStatePublication CorePublicationQueue::TakeStateAndRetainTaskEffects() {
  StateEntry entry = std::move(states_.front());
  states_.pop_front();
  Footprint retained;
  for (size_t index = 0u; index < entry.value.task_effects.size(); ++index) {
    retained.items += entry.task_effects[index].items;
    retained.bytes += entry.task_effects[index].bytes;
    task_effects_.push_back({std::move(entry.value.task_effects[index]),
                             entry.task_effects[index]});
  }
  entry.value.task_effects.clear();
  Release({entry.footprint.items - retained.items,
           entry.footprint.bytes - retained.bytes});
  return std::move(entry.value);
}

bool CorePublicationQueue::TryPushTaskEffects(
    std::vector<mojom::TaskEffectBindingPtr> effects) {
  Footprint total;
  std::vector<Footprint> footprints;
  footprints.reserve(effects.size());
  for (auto& effect : effects) {
    const std::optional<MeasuredFootprint> measured = MeasureTaskEffect(effect);
    if (!measured ||
        measured->items > std::numeric_limits<size_t>::max() - total.items ||
        measured->bytes > std::numeric_limits<size_t>::max() - total.bytes) {
      return false;
    }
    Footprint footprint{measured->items, measured->bytes};
    total.items += footprint.items;
    total.bytes += footprint.bytes;
    footprints.push_back(footprint);
  }
  if (!CanReserve(total)) {
    return false;
  }
  Reserve(total);
  for (size_t index = 0u; index < effects.size(); ++index) {
    task_effects_.push_back({std::move(effects[index]), footprints[index]});
  }
  return true;
}

bool CorePublicationQueue::HasTaskEffects() const {
  return !task_effects_.empty();
}

const mojom::TaskEffectBinding* CorePublicationQueue::FrontTaskEffect() const {
  return task_effects_.empty() ? nullptr : task_effects_.front().value.get();
}

mojom::TaskEffectBindingPtr CorePublicationQueue::TakeTaskEffect() {
  TaskEffectEntry entry = std::move(task_effects_.front());
  task_effects_.pop_front();
  Release(entry.footprint);
  return std::move(entry.value);
}

bool CorePublicationQueue::CanReserve(Footprint footprint) const {
  return retained_items_ <= kMaxRetainedItems &&
         retained_bytes_ <= kMaxRetainedBytes &&
         footprint.items <= kMaxRetainedItems - retained_items_ &&
         footprint.bytes <= kMaxRetainedBytes - retained_bytes_;
}

void CorePublicationQueue::Reserve(Footprint footprint) {
  retained_items_ += footprint.items;
  retained_bytes_ += footprint.bytes;
}

void CorePublicationQueue::Release(Footprint footprint) {
  retained_items_ -= footprint.items;
  retained_bytes_ -= footprint.bytes;
}

void CorePublicationQueue::Clear() {
  batches_.clear();
  states_.clear();
  task_effects_.clear();
  retained_items_ = 0u;
  retained_bytes_ = 0u;
  submission_reservations_ = 0u;
}

}  // namespace taffy
