// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <utility>

#include "base/time/time.h"
#include "taffy/browser/core_api/profile_core_api_facade.h"

namespace taffy {
namespace {

uint64_t MemoryNowMonotonicMillis() {
  const int64_t value = base::TimeTicks::Now().since_origin().InMilliseconds();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

uint64_t MemoryNowUtcMillis() {
  const int64_t value = base::Time::Now().InMillisecondsSinceUnixEpoch();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

}  // namespace

void ProfileCoreApiFacade::SearchMemory(const std::string& request_id,
                                        const std::string& query,
                                        uint32_t limit,
                                        SearchMemoryCallback callback) {
  CoreApiCommandFactory factory = NewFactory();
  SubmitProjected(
      factory.BuildSearchMemory(request_id, query, limit, MemoryNowUtcMillis(),
                                manager_ ? manager_->service_generation() : 0u,
                                MemoryNowMonotonicMillis()),
      std::move(callback));
}

void ProfileCoreApiFacade::UpsertMemory(
    const std::optional<std::string>& memory_id,
    const std::string& statement,
    core_api::mojom::MemoryScopeKind scope_kind,
    core_api::mojom::MemoryWorkspaceViewPtr scope_workspace,
    core_api::mojom::MemorySensitivity sensitivity,
    uint64_t expected_memory_revision,
    uint64_t expected_record_revision,
    uint64_t expires_at_epoch_ms,
    UpsertMemoryCallback callback) {
  CoreApiCommandFactory factory = NewFactory();
  SubmitProjected(
      factory.BuildUpsertMemory(
          memory_id, statement, scope_kind, std::move(scope_workspace),
          sensitivity, expected_memory_revision, expected_record_revision,
          expires_at_epoch_ms, MemoryNowUtcMillis(),
          manager_ ? manager_->service_generation() : 0u,
          MemoryNowMonotonicMillis()),
      std::move(callback));
}

void ProfileCoreApiFacade::DeleteMemory(const std::string& memory_id,
                                        uint64_t expected_memory_revision,
                                        uint64_t expected_record_revision,
                                        DeleteMemoryCallback callback) {
  CoreApiCommandFactory factory = NewFactory();
  SubmitProjected(
      factory.BuildDeleteMemory(memory_id, expected_memory_revision,
                                expected_record_revision, MemoryNowUtcMillis(),
                                manager_ ? manager_->service_generation() : 0u,
                                MemoryNowMonotonicMillis()),
      std::move(callback));
}

}  // namespace taffy
