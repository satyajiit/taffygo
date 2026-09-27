// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/security/browser/value_reference_vault.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "base/containers/span.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/strings/strcat.h"
#include "base/uuid.h"
#include "content/public/browser/browser_thread.h"
#include "crypto/secure_util.h"

namespace taffy {

namespace {

// The prefix exists so a reference is recognizable as one in a log line that
// was never supposed to contain it. It carries no information about the value,
// and neither does the rest: base::Uuid draws from the platform's
// cryptographically secure generator, and nothing about the bytes is an input.
constexpr char kReferencePrefix[] = "val_";

}  // namespace

ValueReferenceVault::HeldValue::HeldValue() = default;
ValueReferenceVault::HeldValue::HeldValue(HeldValue&&) = default;
ValueReferenceVault::HeldValue& ValueReferenceVault::HeldValue::operator=(
    HeldValue&&) = default;
ValueReferenceVault::HeldValue::~HeldValue() = default;

ValueReferenceVault::ValueReferenceVault() = default;

ValueReferenceVault::~ValueReferenceVault() {
  for (auto& entry : held_) {
    Scrub(entry.second);
  }
}

// static
void ValueReferenceVault::Scrub(HeldValue& held) {
  crypto::SecureZeroBuffer(base::as_writable_byte_span(held.value));
  held.value.clear();
}

void ValueReferenceVault::Erase(
    std::map<ValueReference, HeldValue>::iterator it) {
  Scrub(it->second);
  held_.erase(it);
}

void ValueReferenceVault::PruneExpired() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  const base::TimeTicks now = base::TimeTicks::Now();
  for (auto it = held_.begin(); it != held_.end();) {
    if (now >= it->second.expires_at) {
      Scrub(it->second);
      it = held_.erase(it);
    } else {
      ++it;
    }
  }
  ScheduleNextExpiry();
}

void ValueReferenceVault::ScheduleNextExpiry() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (held_.empty()) {
    expiry_timer_.Stop();
    return;
  }
  // The map is keyed by reference rather than by deadline, so the earliest is
  // found by looking. That is a linear pass over a container that holds one
  // value per field a person has filled in for one errand - single digits -
  // and buying an index for it would add a second structure that could
  // disagree with the first about which values exist.
  base::TimeTicks earliest = held_.begin()->second.expires_at;
  for (const auto& entry : held_) {
    earliest = std::min(earliest, entry.second.expires_at);
  }
  expiry_timer_.Start(FROM_HERE, earliest, this,
                      &ValueReferenceVault::PruneExpired);
}

void ValueReferenceVault::BeginGeneration(std::string profile_id,
                                          uint64_t generation) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  // A new generation never inherits. Whatever the previous one was holding
  // belonged to tasks that no longer exist.
  for (auto& entry : held_) {
    Scrub(entry.second);
  }
  held_.clear();
  expiry_timer_.Stop();
  if (generation == 0u || profile_id.empty()) {
    active_profile_id_.clear();
    active_generation_ = 0u;
    return;
  }
  active_profile_id_ = std::move(profile_id);
  active_generation_ = generation;
}

void ValueReferenceVault::RevokeGeneration(uint64_t generation) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (generation != active_generation_) {
    return;
  }
  for (auto& entry : held_) {
    Scrub(entry.second);
  }
  held_.clear();
  expiry_timer_.Stop();
  active_profile_id_.clear();
  active_generation_ = 0u;
}

void ValueReferenceVault::RevokeTask(const TaskId& task_id) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  for (auto it = held_.begin(); it != held_.end();) {
    if (it->second.task_id == task_id) {
      Scrub(it->second);
      it = held_.erase(it);
    } else {
      ++it;
    }
  }
  ScheduleNextExpiry();
}

void ValueReferenceVault::RevokeReference(const ValueReference& reference) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  const auto it = held_.find(reference);
  if (it == held_.end() || it->second.generation != active_generation_) {
    return;
  }
  Erase(it);
  ScheduleNextExpiry();
}

ValueReference ValueReferenceVault::Mint(const TaskId& task_id,
                                         FillClearance clearance,
                                         std::string value,
                                         base::TimeTicks expires_at) {
  return MintUnder(ValueReference{base::StrCat(
                       {kReferencePrefix,
                        base::Uuid::GenerateRandomV4().AsLowercaseString()})},
                   task_id, clearance, std::move(value), expires_at);
}

ValueReference ValueReferenceVault::MintNamed(ValueReference reference,
                                              const TaskId& task_id,
                                              FillClearance clearance,
                                              std::string value,
                                              base::TimeTicks expires_at) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!reference.is_valid()) {
    // A name nobody could have derived correctly. Refused before anything is
    // held, and the bytes go with the refusal rather than into a record whose
    // name the other side will never say.
    crypto::SecureZeroBuffer(base::as_writable_byte_span(value));
    return ValueReference();
  }
  return MintUnder(std::move(reference), task_id, clearance, std::move(value),
                   expires_at);
}

ValueReference ValueReferenceVault::MintUnder(ValueReference reference,
                                              const TaskId& task_id,
                                              FillClearance clearance,
                                              std::string value,
                                              base::TimeTicks expires_at) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (active_generation_ == 0u || !task_id.is_valid() || value.empty()) {
    // An empty reference. Refusing here rather than minting a reference that
    // resolves to nothing keeps "the browser holds a value for this" and "the
    // browser has a name for it" the same statement.
    crypto::SecureZeroBuffer(base::as_writable_byte_span(value));
    return ValueReference();
  }

  HeldValue held;
  held.task_id = task_id;
  held.field_class = clearance.field_class();
  held.expires_at = expires_at;
  held.generation = active_generation_;
  held.value = std::move(value);

  // try_emplace rather than insert_or_assign. A repeated identifier is not
  // reachable for a minted name, and if it ever were, replacing the record
  // would silently destroy a value some other part of the same task is still
  // holding a name for. A refused mint is a person retyping; a replaced record
  // is a reference that stops resolving with nothing to explain it. For a
  // derived name it is reachable and is the point: the second answer to one
  // request finds the first one's record already there and is refused.
  const bool minted = held_.try_emplace(reference, std::move(held)).second;
  // Unconditional, and it is the success path that needs saying. On failure
  // try_emplace did not move, so the bytes are still whole in the local. On
  // success the local was moved from - but a value short enough to live in a
  // std::string's own inline storage, which is most of what goes into a form
  // field, is moved by copying those bytes, and whether the source is left
  // holding them is the standard library's business rather than a guarantee.
  // Overwriting costs nothing and removes the question.
  Scrub(held);
  if (!minted) {
    return ValueReference();
  }
  // A record with a deadline nobody is waiting on is a record that outlives
  // its deadline, so arming the timer is part of minting rather than a
  // follow-up somebody could forget.
  ScheduleNextExpiry();
  return reference;
}

ValueResolution ValueReferenceVault::Resolve(const ValueReference& reference,
                                             const TaskId& task_id,
                                             Sensitivity target_field_class,
                                             base::TimeTicks now,
                                             std::string& out) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  // First, and before the reference is even looked up: may this field carry a
  // value Taffy holds at all? A permanent refusal is reported in preference to
  // a temporary one, so a credential field reads as a credential field whether
  // or not the reference beside it was also stale, expired, or somebody
  // else's. The classification is the browser's own re-read of the node, not
  // anything the proposal asserted about it.
  const std::optional<FillClearance> target =
      FillClearance::For(target_field_class);
  if (!target.has_value()) {
    return ValueResolution::kFieldMayNotBeFilled;
  }

  const auto it = held_.find(reference);
  if (it == held_.end() || it->second.generation != active_generation_) {
    return ValueResolution::kUnknownReference;
  }
  if (it->second.task_id != task_id) {
    return ValueResolution::kNotThisTask;
  }
  if (now >= it->second.expires_at) {
    // Pruned rather than spent. The record can never produce bytes again, so
    // leaving it in the map would only widen the window in which the value is
    // in memory. Reached when a resolution arrives between the deadline and
    // the timer's own pass, which is a real ordering rather than a
    // hypothetical one: `now` is the caller's reading of the clock.
    Erase(it);
    ScheduleNextExpiry();
    return ValueResolution::kExpired;
  }
  if (it->second.field_class != target->field_class()) {
    return ValueResolution::kFieldClassMismatch;
  }

  // Spent. The copy out and the erase are one step with nothing between them:
  // the record does not survive the resolution, so a second Resolve() with the
  // same reference finds nothing and answers kUnknownReference. That is what
  // makes a reference a reference - one that could be presented twice would be
  // a value with extra steps.
  out = it->second.value;
  Erase(it);
  ScheduleNextExpiry();
  return ValueResolution::kResolved;
}

size_t ValueReferenceVault::HeldCount() const {
  return held_.size();
}

bool ValueReferenceVault::Holds(const ValueReference& reference) const {
  const auto it = held_.find(reference);
  return it != held_.end() && it->second.generation == active_generation_;
}

}  // namespace taffy
