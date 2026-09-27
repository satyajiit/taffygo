// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <utility>

#include "base/functional/bind.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "content/public/browser/browser_context.h"
#include "mojo/public/cpp/bindings/callback_helpers.h"
#include "taffy/browser/core_service_manager.h"

namespace taffy {
namespace service = core_service::mojom;

void CoreServiceManager::QuerySavedFlows(
    service::SavedFlowQueryKind kind,
    const std::string& goal,
    const std::string& skill_id,
    uint32_t expected_version,
    base::OnceCallback<void(service::SavedFlowQueryResultPtr)> callback) {
  if (availability_ != Availability::kReady || !session_.is_bound() ||
      goal.size() > core_api::mojom::kMaxTaskGoalBytes ||
      skill_id.size() > service::kMaxSkillIdBytes) {
    std::move(callback).Run(nullptr);
    return;
  }
  const std::string id = base::Uuid::GenerateRandomV4().AsLowercaseString();
  const uint64_t deadline =
      static_cast<uint64_t>(
          base::TimeTicks::Now().since_origin().InMilliseconds()) +
      30'000u;
  auto operation = service::OperationEnvelope::New(id, service_generation_, 0u,
                                                   deadline, id);
  auto command = service::SavedFlowQueryCommand::New(
      operation.Clone(), kind, goal, skill_id, expected_version);
  auto reply = base::BindOnce(
      [](base::WeakPtr<CoreServiceManager> manager,
         service::OperationEnvelopePtr expected,
         base::OnceCallback<void(service::SavedFlowQueryResultPtr)> callback,
         service::SavedFlowQueryResultPtr result) {
        if (!manager || manager->availability() != Availability::kReady ||
            manager->service_generation() != expected->service_generation ||
            !result || !result->operation ||
            !result->operation->Equals(*expected) ||
            static_cast<uint64_t>(
                base::TimeTicks::Now().since_origin().InMilliseconds()) >
                expected->deadline_monotonic_ms) {
          std::move(callback).Run(nullptr);
          return;
        }
        std::move(callback).Run(std::move(result));
      },
      weak_factory_.GetWeakPtr(), std::move(operation), std::move(callback));
  session_->QuerySavedFlows(
      std::move(command),
      mojo::WrapCallbackWithDefaultInvokeIfNotRun(
          std::move(reply), service::SavedFlowQueryResultPtr()));
}
}  // namespace taffy
