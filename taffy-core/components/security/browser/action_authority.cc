// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/security/browser/action_authority.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>

#include "base/check.h"
#include "base/uuid.h"
#include "content/public/browser/browser_thread.h"

namespace taffy {

namespace {

MonotonicMillis ToMonotonicMs(base::TimeTicks ticks) {
  const int64_t delta = (ticks - base::TimeTicks()).InMilliseconds();
  return delta < 0 ? 0 : static_cast<MonotonicMillis>(delta);
}

bool IsDirectIntentId(std::string_view value) {
  constexpr std::string_view kPrefix = "direct-intent-";
  return value.starts_with(kPrefix) && value.size() <= 128u;
}

}  // namespace

ActionResultCode AdmissionToResultCode(CapabilityAdmission admission) {
  switch (admission) {
    case CapabilityAdmission::kAdmitted:
      // Not a terminal code. Callers check for kAdmitted before mapping.
      return ActionResultCode::kInternalError;
    case CapabilityAdmission::kAlreadySpent:
    case CapabilityAdmission::kExpired:
      return ActionResultCode::kCapabilityExpired;
    case CapabilityAdmission::kDigestMismatch:
    case CapabilityAdmission::kMalformed:
      return ActionResultCode::kDeniedByPolicy;
    case CapabilityAdmission::kLeaseMissing:
    case CapabilityAdmission::kLeaseNotForThisTab:
      return ActionResultCode::kActorLeaseMissing;
  }
  // Fail closed on a value this build does not recognize.
  return ActionResultCode::kDeniedByPolicy;
}

// --- HandoverEvidence -------------------------------------------------------

HandoverEvidence::HandoverEvidence() = default;
HandoverEvidence::HandoverEvidence(const HandoverEvidence&) = default;
HandoverEvidence::HandoverEvidence(HandoverEvidence&&) = default;
HandoverEvidence& HandoverEvidence::operator=(const HandoverEvidence&) = default;
HandoverEvidence& HandoverEvidence::operator=(HandoverEvidence&&) = default;
HandoverEvidence::~HandoverEvidence() = default;

// --- ActorLeaseRegistry -----------------------------------------------------

ActorLeaseRegistry::ActorLeaseRegistry() = default;
ActorLeaseRegistry::~ActorLeaseRegistry() = default;

void ActorLeaseRegistry::BeginGeneration(std::string profile_id,
                                         uint64_t generation) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (generation == 0u || profile_id.empty()) {
    leases_.clear();
    active_profile_id_.clear();
    active_generation_ = 0u;
    return;
  }
  if (active_generation_ != generation || active_profile_id_ != profile_id) {
    leases_.clear();
  }
  active_profile_id_ = std::move(profile_id);
  active_generation_ = generation;
}

void ActorLeaseRegistry::RevokeGeneration(uint64_t generation) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (generation != active_generation_) {
    return;
  }
  leases_.clear();
  active_profile_id_.clear();
  active_generation_ = 0u;
}

void ActorLeaseRegistry::RevokeTask(std::string_view task_id,
                                    uint64_t generation) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (generation != active_generation_ || task_id.empty()) {
    return;
  }
  for (auto& [lease_id, record] : leases_) {
    if (record.generation == generation &&
        record.subject_kind ==
            core_service::mojom::AuthoritySubjectKind::kTask &&
        record.authority_subject_id == task_id) {
      record.revoked = true;
    }
  }
}

ActorLeaseResult ActorLeaseRegistry::Issue(const ActorLeaseRequest& request,
                                           base::TimeTicks now) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  PruneExpired(now);

  ActorLeaseResult result;
  if (active_generation_ == 0u || active_profile_id_.empty() ||
      !request.tab_id.is_valid() || !request.task_id.is_valid()) {
    result.code = ActorLeaseResultCode::kInternalError;
    return result;
  }

  // At most one active mutating lease per tab. A second request is refused
  // rather than queued: two actors believing they hold mutation authority
  // over one tab is the state this invariant exists to make impossible.
  if (request.mutating) {
    // An open handover window is the person holding this tab, and it refuses
    // mutation authority here rather than only in the isolated core's state
    // machine. The core is sandboxed and its reducer is the thing an attacker
    // would have to talk out of a refusal; this registry is the side that
    // actually holds the leases, and the whole point of BeginHandover
    // revoking before it counts is lost if the next request can mint the
    // authority straight back while the person is still typing.
    //
    // Closing the window is therefore a precondition of resuming, not a
    // formality after it: EndHandover returns the revoked identities in its
    // evidence, so a resumption can still check that its fresh lease is not
    // one of them.
    if (handovers_.find(request.tab_id) != handovers_.end()) {
      result.code = ActorLeaseResultCode::kAlreadyHeld;
      return result;
    }
    if (HasMutatingLease(request.tab_id, now)) {
      result.code = ActorLeaseResultCode::kAlreadyHeld;
      return result;
    }
    for (const auto& entry : leases_) {
      const LeaseRecord& record = entry.second;
      if (record.tab_id == request.tab_id && !record.revoked &&
          record.expires_at > now &&
          record.subject_kind ==
              core_service::mojom::AuthoritySubjectKind::kDirectUserIntent) {
        result.code = ActorLeaseResultCode::kAlreadyHeld;
        return result;
      }
    }
  }

  // Lease lifetime is an authority bound, not an observation/rendering
  // deadline. Keep its ceiling local to the only lease registry so a content
  // component can never widen authority by changing its own resource budget.
  constexpr uint32_t kMaximumLeaseDurationMillis = 30'000;
  const uint32_t duration_ms =
      std::min(request.requested_duration_ms, kMaximumLeaseDurationMillis);

  LeaseRecord record;
  record.subject_kind = core_service::mojom::AuthoritySubjectKind::kTask;
  record.authority_subject_id = request.task_id.value;
  record.tab_id = request.tab_id;
  record.mutating = request.mutating;
  record.expires_at = now + base::Milliseconds(duration_ms);
  record.generation = active_generation_;
  record.profile_id = active_profile_id_;

  const ActorLeaseId lease_id{
      base::Uuid::GenerateRandomV4().AsLowercaseString()};
  leases_.emplace(lease_id, record);

  result.code = ActorLeaseResultCode::kIssued;
  result.lease_id = lease_id;
  result.expires_at_monotonic_ms = ToMonotonicMs(record.expires_at);
  return result;
}

ActorLeaseResult ActorLeaseRegistry::IssueDirectObservation(
    std::string direct_intent_id,
    const TabId& tab_id,
    uint32_t requested_duration_ms,
    base::TimeTicks now) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  PruneExpired(now);

  ActorLeaseResult result;
  if (active_generation_ == 0u || active_profile_id_.empty() ||
      !tab_id.is_valid() || !IsDirectIntentId(direct_intent_id) ||
      requested_duration_ms == 0u || requested_duration_ms > 2'000u) {
    result.code = ActorLeaseResultCode::kInternalError;
    return result;
  }

  // A direct read cannot silently preempt a task mutation. Revoking the lease
  // here would orphan the task-side cancellation and settlement path because
  // the registry has no authority to synthesize either event. Refuse the read
  // and let the browser command owner explicitly cancel and settle the task
  // before retrying if user intent requires preemption.
  if (HasMutatingLease(tab_id, now)) {
    result.code = ActorLeaseResultCode::kAlreadyHeld;
    return result;
  }

  LeaseRecord record;
  record.subject_kind =
      core_service::mojom::AuthoritySubjectKind::kDirectUserIntent;
  record.authority_subject_id = std::move(direct_intent_id);
  record.tab_id = tab_id;
  record.mutating = false;
  record.expires_at = now + base::Milliseconds(requested_duration_ms);
  record.generation = active_generation_;
  record.profile_id = active_profile_id_;

  const ActorLeaseId lease_id{
      base::Uuid::GenerateRandomV4().AsLowercaseString()};
  leases_.emplace(lease_id, record);
  result.code = ActorLeaseResultCode::kIssued;
  result.lease_id = lease_id;
  result.expires_at_monotonic_ms = ToMonotonicMs(record.expires_at);
  return result;
}

bool ActorLeaseRegistry::IsValidFor(const ActorLeaseId& lease_id,
                                    const TabId& tab_id,
                                    base::TimeTicks now) const {
  auto it = leases_.find(lease_id);
  if (it == leases_.end()) {
    return false;
  }
  const LeaseRecord& record = it->second;
  return !record.revoked && record.tab_id == tab_id && record.expires_at > now;
}

bool ActorLeaseRegistry::IsValidForGrant(
    const ActorLeaseId& lease_id,
    core_service::mojom::AuthoritySubjectKind subject_kind,
    std::string_view authority_subject_id,
    const TabId& tab_id,
    std::string_view profile_id,
    uint64_t generation,
    base::TimeTicks grant_expires_at,
    base::TimeTicks now) const {
  auto it = leases_.find(lease_id);
  if (it == leases_.end()) {
    return false;
  }
  const LeaseRecord& record = it->second;
  return !record.revoked && record.subject_kind == subject_kind &&
         record.authority_subject_id == authority_subject_id &&
         record.tab_id == tab_id && record.profile_id == profile_id &&
         record.generation == generation && active_generation_ == generation &&
         active_profile_id_ == profile_id && record.expires_at > now &&
         record.expires_at >= grant_expires_at;
}

void ActorLeaseRegistry::Release(const ActorLeaseId& lease_id) {
  leases_.erase(lease_id);
}

std::vector<ActorLeaseId> ActorLeaseRegistry::PreemptTab(const TabId& tab_id) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  std::vector<ActorLeaseId> revoked;
  for (auto& [lease_id, record] : leases_) {
    if (record.tab_id == tab_id && !record.revoked) {
      // Marked rather than erased: a lease that was revoked and a lease that
      // never existed both fail IsValidFor, but only the first can be
      // reported to the core service as a preemption.
      record.revoked = true;
      revoked.push_back(lease_id);
    }
  }
  return revoked;
}

std::vector<ActorLeaseId> ActorLeaseRegistry::PreemptDirectObservations(
    const TabId& tab_id) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  std::vector<ActorLeaseId> revoked;
  for (auto& [lease_id, record] : leases_) {
    if (record.tab_id == tab_id && !record.revoked &&
        record.subject_kind ==
            core_service::mojom::AuthoritySubjectKind::kDirectUserIntent) {
      record.revoked = true;
      revoked.push_back(lease_id);
    }
  }
  return revoked;
}

bool ActorLeaseRegistry::HasMutatingLease(const TabId& tab_id,
                                          base::TimeTicks now) const {
  for (const auto& [lease_id, record] : leases_) {
    if (record.tab_id == tab_id && record.mutating && !record.revoked &&
        record.expires_at > now) {
      return true;
    }
  }
  return false;
}

std::vector<ActorLeaseId> ActorLeaseRegistry::BeginHandover(const TabId& tab_id) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  // Revoke first, always. The person is about to type into this tab, and the
  // assistant must not be holding mutation authority over it while they do.
  // Opening the window before the revocation would leave a gap, however short,
  // in which a queued action could land on the field they had just focused.
  std::vector<ActorLeaseId> revoked = PreemptTab(tab_id);

  // A second BeginHandover on the same tab keeps the window that is already
  // open and adds whatever it just revoked. Replacing it would throw away the
  // evidence the person has already produced, and a person who has typed half
  // a code has produced exactly the evidence that matters.
  HandoverEvidence& window = handovers_[tab_id];
  for (const ActorLeaseId& lease_id : revoked) {
    window.revoked.push_back(lease_id);
  }
  // RevokeTask marks leases revoked without erasing them. A handover that
  // opens after that would otherwise record an empty `lease_before`, which the
  // reducer refuses. Already-revoked leases still on this tab are the
  // authority the person is taking over.
  for (const auto& [lease_id, record] : leases_) {
    if (record.tab_id != tab_id || !record.revoked) {
      continue;
    }
    if (std::find(window.revoked.begin(), window.revoked.end(), lease_id) ==
        window.revoked.end()) {
      window.revoked.push_back(lease_id);
    }
  }
  return revoked;
}

void ActorLeaseRegistry::NoteHandoverInput(const TabId& tab_id) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  auto it = handovers_.find(tab_id);
  if (it == handovers_.end()) {
    // No window is open on this tab, so there is nothing this event is
    // evidence of. Counting it anywhere else would let input that arrived
    // before a handover stand in for input during one.
    return;
  }
  if (it->second.person_input < kMaxCountedHandoverInput) {
    ++it->second.person_input;
  }
}

uint32_t ActorLeaseRegistry::HandoverInputCount(const TabId& tab_id) const {
  auto it = handovers_.find(tab_id);
  return it == handovers_.end() ? 0u : it->second.person_input;
}

HandoverEvidence ActorLeaseRegistry::EndHandover(const TabId& tab_id) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  auto it = handovers_.find(tab_id);
  if (it == handovers_.end()) {
    // Closing a window that was never open answers with empty evidence rather
    // than inventing any. Zero input and no revoked lease is the truthful
    // answer to "what did that handover observe" when there was no handover.
    return HandoverEvidence();
  }
  HandoverEvidence evidence = std::move(it->second);
  handovers_.erase(it);
  return evidence;
}

ActorLeaseId ActorLeaseRegistry::FirstHandoverRevokedLease(
    const TabId& tab_id) const {
  auto it = handovers_.find(tab_id);
  if (it == handovers_.end() || it->second.revoked.empty()) {
    return ActorLeaseId();
  }
  return it->second.revoked.front();
}

bool ActorLeaseRegistry::WasRevokedByHandover(
    const TabId& tab_id,
    const ActorLeaseId& lease_id) const {
  auto it = handovers_.find(tab_id);
  if (it == handovers_.end()) {
    return false;
  }
  return std::find(it->second.revoked.begin(), it->second.revoked.end(),
                   lease_id) != it->second.revoked.end();
}

void ActorLeaseRegistry::PruneExpired(base::TimeTicks now) {
  std::erase_if(leases_, [now](const auto& entry) {
    return entry.second.expires_at <= now;
  });
}

}  // namespace taffy
