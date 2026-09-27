// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <limits>
#include <string>
#include <string_view>
#include <utility>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/core_task_effect.h"
#include "taffy/common/public/bip_identity.h"

namespace taffy {
namespace {

namespace service_mojom = core_service::mojom;

uint64_t NowMonotonicMillis() {
  const int64_t value = base::TimeTicks::Now().since_origin().InMilliseconds();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

std::string NewHandoverId(std::string_view domain) {
  return std::string(domain) + "-" +
         base::Uuid::GenerateRandomV4().AsLowercaseString();
}

std::string NewLeaseIdentity() {
  return base::Uuid::GenerateRandomV4().AsLowercaseString();
}

std::string DistinctLeaseIdentity(const std::string &lease_before,
                                  const HandoverEvidence &evidence) {
  for (;;) {
    std::string candidate = NewLeaseIdentity();
    if (candidate == lease_before) {
      continue;
    }
    bool reused = false;
    for (const ActorLeaseId &revoked : evidence.revoked) {
      if (revoked.value == candidate) {
        reused = true;
        break;
      }
    }
    if (!reused) {
      return candidate;
    }
  }
}

}  // namespace

void CoreServiceManager::ExecuteTaskHandover(
    service_mojom::TaskEffectBindingPtr effect,
    ExecuteTaskEffectCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!effect || !effect->handover || !effect->operation) {
    std::move(callback).Run(MakeTaskEffectCompletion(
        effect.get(), service_mojom::TaskEffectCompletionStatus::kRefused));
    return;
  }

  const auto existing = open_handovers_.find(effect->task_id);
  if (existing != open_handovers_.end()) {
    if (existing->second.handover_id == effect->handover->handover_id) {
      std::move(callback).Run(MakeTaskEffectCompletion(
          effect.get(),
          service_mojom::TaskEffectCompletionStatus::kSucceeded));
      return;
    }
    AbandonOpenHandover(effect->task_id);
  }

  OpenHandover window;
  window.handover_id = effect->handover->handover_id;
  window.window_ms = effect->handover->window_ms;
  const std::optional<std::string> tab_id =
      accepted_approvals_.FindSingleConsentedTab(effect->task_id,
                                                service_generation_);
  if (tab_id) {
    const TabId tab{*tab_id};
    if (tab.is_valid()) {
      actor_leases_.BeginHandover(tab);
      const ActorLeaseId revoked = actor_leases_.FirstHandoverRevokedLease(tab);
      if (revoked.is_valid()) {
        window.lease_before = revoked.value;
      }
      window.tab_id = *tab_id;
    }
  }
  if (window.lease_before.empty()) {
    window.lease_before = NewLeaseIdentity();
  }
  window.expiry = std::make_unique<base::OneShotTimer>();
  auto it = open_handovers_
                .insert_or_assign(effect->task_id, std::move(window))
                .first;
  RefreshIdleTeardown();
  it->second.expiry->Start(
      FROM_HERE, base::Milliseconds(it->second.window_ms),
      base::BindOnce(&CoreServiceManager::OnHandoverExpired,
                     weak_factory_.GetWeakPtr(), effect->task_id));
  std::move(callback).Run(MakeTaskEffectCompletion(
      effect.get(), service_mojom::TaskEffectCompletionStatus::kSucceeded));
}

std::optional<HandoverCompletionFacts>
CoreServiceManager::CloseHandoverForCompletion(const std::string &task_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto it = open_handovers_.find(task_id);
  if (it == open_handovers_.end()) {
    return std::nullopt;
  }
  OpenHandover window = std::move(it->second);
  open_handovers_.erase(it);
  RefreshIdleTeardown();

  HandoverEvidence evidence;
  if (window.tab_id) {
    evidence = actor_leases_.EndHandover(TabId{*window.tab_id});
  }
  HandoverCompletionFacts facts;
  facts.handover_id = std::move(window.handover_id);
  facts.lease_before = std::move(window.lease_before);
  facts.resumed_with = DistinctLeaseIdentity(facts.lease_before, evidence);
  facts.person_input = evidence.person_input;
  return facts;
}

void CoreServiceManager::OnHandoverExpired(std::string task_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto it = open_handovers_.find(task_id);
  if (it == open_handovers_.end()) {
    return;
  }
  OpenHandover window = std::move(it->second);
  open_handovers_.erase(it);
  RefreshIdleTeardown();
  if (window.tab_id) {
    actor_leases_.EndHandover(TabId{*window.tab_id});
  }
  if (shutdown_started_ || availability_ != Availability::kReady ||
      !session_.is_bound()) {
    return;
  }
  const std::optional<uint64_t> revision = FindTaskRevision(task_id);
  if (!revision) {
    return;
  }
  Submit(MakeExpireHandover(task_id, window.handover_id, *revision),
         base::DoNothing());
}

void CoreServiceManager::AbandonOpenHandover(const std::string &task_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto it = open_handovers_.find(task_id);
  if (it == open_handovers_.end()) {
    return;
  }
  if (it->second.tab_id) {
    actor_leases_.EndHandover(TabId{*it->second.tab_id});
  }
  open_handovers_.erase(it);
  RefreshIdleTeardown();
}

void CoreServiceManager::AbandonAllOpenHandovers() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  while (!open_handovers_.empty()) {
    AbandonOpenHandover(open_handovers_.begin()->first);
  }
}

service_mojom::CoreServiceCommandPtr CoreServiceManager::MakeExpireHandover(
    const std::string &task_id, const std::string &handover_id,
    uint64_t task_revision) const {
  const uint64_t now = NowMonotonicMillis();
  auto command = service_mojom::CoreServiceCommand::New();
  const uint64_t deadline =
      now > std::numeric_limits<uint64_t>::max() - 30'000
          ? std::numeric_limits<uint64_t>::max()
          : now + 30'000;
  command->operation = service_mojom::OperationEnvelope::New(
      NewHandoverId("operation"), service_generation_, task_revision, deadline,
      NewHandoverId("idempotency"));
  command->kind = service_mojom::CoreServiceCommandKind::kExpireHandover;
  command->expire_handover = service_mojom::ExpireHandoverCommand::New(
      task_id, handover_id, NewHandoverId("trace"));
  return command;
}

}  // namespace taffy
