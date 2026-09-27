// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/storage/browser/core_storage_skill_shape.h"

#include <limits>

#include "taffy/components/storage/browser/core_storage_task_seed.h"

namespace taffy::skill_internal {

namespace mojom = core_service::mojom;

bool IsSkillIdentifier(const std::string& value) {
  return !value.empty() && value.size() <= mojom::kMaxSkillIdBytes;
}

bool IsOrigin(const std::string& value) {
  return !value.empty() && value.size() <= mojom::kMaxNormalizedOriginBytes;
}

bool IsVersion(int64_t value) {
  return value > 0 &&
         value <= static_cast<int64_t>(mojom::kMaxSkillVersionsPerSkill);
}

bool IsStepCount(int64_t value) {
  return value > 0 && value <= static_cast<int64_t>(mojom::kMaxSkillSteps);
}

bool IsDefinition(const std::vector<uint8_t>& value) {
  return !value.empty() && value.size() <= mojom::kMaxSkillDefinitionBytes;
}

bool IsTimestamp(uint64_t value) {
  return value <= static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
}

bool HasNoTaskFields(const mojom::StorageCommitEffect& body) {
  return body.task_id.empty() && body.transaction_batch.empty() &&
         storage_internal::IsNeutralTaskIdSeed(body.task_id_seed) &&
         body.expected_revision == 0u &&
         body.resulting_revision == 0u;
}

bool HasOnlySkillBody(const mojom::StorageCommitEffect& body,
                      mojom::StorageOperation expected) {
  const bool install = body.install_skill.is_null();
  const bool status = body.skill_status.is_null();
  const bool run = body.skill_run.is_null();
  const bool forget = body.forget_skill.is_null();
  if (!body.workspace.is_null() || !body.source_deletion.is_null() ||
      !body.assistant_configuration.is_null() ||
      !body.workspace_deletion.is_null() || !body.library_entry.is_null() ||
      !body.library_deletion.is_null() || !body.memory_record.is_null() ||
      !body.memory_deletion.is_null()) {
    return false;
  }
  switch (expected) {
    case mojom::StorageOperation::kInstallSkill:
      return !install && status && run && forget;
    case mojom::StorageOperation::kSetSkillStatus:
      return install && !status && run && forget;
    case mojom::StorageOperation::kRecordSkillRun:
      return install && status && !run && forget;
    case mojom::StorageOperation::kForgetSkill:
      return install && status && run && !forget;
    case mojom::StorageOperation::kAppendTaskCommit:
    case mojom::StorageOperation::kQueryWorkspace:
    case mojom::StorageOperation::kDeleteSource:
    case mojom::StorageOperation::kUpsertWorkspace:
    case mojom::StorageOperation::kSetAssistantConfiguration:
    case mojom::StorageOperation::kDeleteWorkspace:
    case mojom::StorageOperation::kUpsertLibraryEntry:
    case mojom::StorageOperation::kRemoveLibraryEntry:
    case mojom::StorageOperation::kUpsertMemory:
    case mojom::StorageOperation::kDeleteMemory:
      return false;
  }
  return false;
}

}  // namespace taffy::skill_internal
