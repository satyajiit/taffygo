// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_storage_effect_validation.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "taffy/components/storage/browser/core_storage_library_validation.h"
#include "taffy/components/storage/browser/core_storage_memory_validation.h"
#include "taffy/components/storage/browser/core_storage_task_seed.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

bool IsIdentifier(const std::string& value, size_t maximum) {
  return !value.empty() && value.size() <= maximum;
}

bool IsWorkspaceId(const std::string& value) {
  return value.size() == 32u &&
         std::all_of(value.begin(), value.end(), [](char character) {
           return (character >= '0' && character <= '9') ||
                  (character >= 'a' && character <= 'f');
         });
}

bool IsValidWorkspaceWrite(const mojom::WorkspacePersistEffect& workspace) {
  return IsWorkspaceId(workspace.workspace_id) &&
         workspace.resulting_revision > workspace.expected_revision &&
         workspace.resulting_revision == workspace.expected_revision + 1u &&
         !workspace.snapshot.empty() &&
         workspace.snapshot.size() <= mojom::kMaxWorkspaceSnapshotBytes;
}

bool IsConfirmationToken(const std::string& value) {
  return value.size() == mojom::kMaxWorkspaceConfirmationTokenBytes &&
         std::all_of(value.begin(), value.end(), [](char character) {
           return (character >= '0' && character <= '9') ||
                  (character >= 'a' && character <= 'f');
         });
}

bool IsValidWorkspaceDeletion(const mojom::WorkspaceDeletionEffect& deletion) {
  return IsWorkspaceId(deletion.workspace_id) &&
         deletion.expected_revision > 0u &&
         deletion.expected_revision <
             static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) &&
         deletion.resulting_revision == deletion.expected_revision + 1u &&
         deletion.artifact_metadata <= 2u && deletion.derived_indexes == 0u &&
         IsConfirmationToken(deletion.confirmation_token);
}

bool HasUsableSeed(const std::vector<uint8_t>& seed) {
  return seed.size() == 32u &&
         std::adjacent_find(seed.begin(), seed.end(), std::not_equal_to<>()) !=
             seed.end();
}

}  // namespace

bool IsValidCoreStorageCommit(const mojom::StorageCommitEffect& body,
                              size_t max_identifier_bytes,
                              size_t max_effect_bytes) {
  const bool task_fields_neutral =
      body.task_id.empty() && body.transaction_batch.empty() &&
      storage_internal::IsNeutralTaskIdSeed(body.task_id_seed) &&
      body.expected_revision == 0u &&
      body.resulting_revision == 0u;
  const int bodies = static_cast<int>(!body.workspace.is_null()) +
                     static_cast<int>(!body.source_deletion.is_null()) +
                     static_cast<int>(!body.install_skill.is_null()) +
                     static_cast<int>(!body.skill_status.is_null()) +
                     static_cast<int>(!body.skill_run.is_null()) +
                     static_cast<int>(!body.forget_skill.is_null()) +
                     static_cast<int>(!body.assistant_configuration.is_null()) +
                     static_cast<int>(!body.workspace_deletion.is_null()) +
                     static_cast<int>(!body.library_entry.is_null()) +
                     static_cast<int>(!body.library_deletion.is_null()) +
                     static_cast<int>(!body.memory_record.is_null()) +
                     static_cast<int>(!body.memory_deletion.is_null());
  switch (body.operation_kind) {
    case mojom::StorageOperation::kAppendTaskCommit:
      return bodies <= 1 && !body.source_deletion && !body.install_skill &&
             !body.skill_status && !body.skill_run && !body.forget_skill &&
             !body.assistant_configuration && !body.workspace_deletion &&
             (!body.workspace || IsValidWorkspaceWrite(*body.workspace)) &&
             IsIdentifier(body.task_id, max_identifier_bytes) &&
             body.resulting_revision > body.expected_revision &&
             !body.transaction_batch.empty() &&
             body.transaction_batch.size() <= max_effect_bytes &&
             HasUsableSeed(body.task_id_seed);
    case mojom::StorageOperation::kUpsertWorkspace:
      return bodies == 1 && !body.workspace.is_null() && task_fields_neutral &&
             IsValidWorkspaceWrite(*body.workspace);
    case mojom::StorageOperation::kDeleteWorkspace:
      return bodies == 1 && !body.workspace_deletion.is_null() &&
             task_fields_neutral &&
             IsValidWorkspaceDeletion(*body.workspace_deletion);
    case mojom::StorageOperation::kUpsertLibraryEntry:
    case mojom::StorageOperation::kRemoveLibraryEntry:
      return bodies == 1 && IsValidLibraryStorageCommitBody(body);
    case mojom::StorageOperation::kUpsertMemory:
    case mojom::StorageOperation::kDeleteMemory:
      return bodies == 1 && IsValidMemoryStorageCommitBody(body);
    case mojom::StorageOperation::kDeleteSource:
      // Two bodies, and both are required: the snapshot the deletion produced
      // and the site whose skills go with it, so one transaction carries both.
      return bodies == 2 && !body.workspace.is_null() &&
             !body.source_deletion.is_null() && task_fields_neutral &&
             IsIdentifier(body.source_deletion->source_id,
                          max_identifier_bytes) &&
             !body.source_deletion->origin.empty() &&
             body.source_deletion->origin.size() <=
                 mojom::kMaxNormalizedOriginBytes &&
             IsValidWorkspaceWrite(*body.workspace);
    case mojom::StorageOperation::kInstallSkill:
      return bodies == 1 && !body.install_skill.is_null() &&
             task_fields_neutral &&
             IsIdentifier(body.install_skill->skill_id,
                          mojom::kMaxSkillIdBytes) &&
             !body.install_skill->origin.empty() &&
             body.install_skill->origin.size() <=
                 mojom::kMaxNormalizedOriginBytes &&
             !body.install_skill->definition.empty() &&
             body.install_skill->definition.size() <=
                 mojom::kMaxSkillDefinitionBytes &&
             body.install_skill->step_count > 0u &&
             body.install_skill->step_count <= mojom::kMaxSkillSteps &&
             body.install_skill->version > 0u &&
             body.install_skill->version <= mojom::kMaxSkillVersionsPerSkill;
    case mojom::StorageOperation::kSetSkillStatus:
      return bodies == 1 && !body.skill_status.is_null() &&
             task_fields_neutral &&
             IsIdentifier(body.skill_status->skill_id,
                          mojom::kMaxSkillIdBytes) &&
             body.skill_status->version > 0u &&
             body.skill_status->version <= mojom::kMaxSkillVersionsPerSkill;
    case mojom::StorageOperation::kRecordSkillRun:
      return bodies == 1 && !body.skill_run.is_null() &&
             task_fields_neutral &&
             IsIdentifier(body.skill_run->skill_id, mojom::kMaxSkillIdBytes) &&
             IsIdentifier(body.skill_run->task_id, max_identifier_bytes) &&
             body.skill_run->version > 0u &&
             body.skill_run->version <= mojom::kMaxSkillVersionsPerSkill;
    case mojom::StorageOperation::kForgetSkill:
      return bodies == 1 && !body.forget_skill.is_null() &&
             task_fields_neutral &&
             IsIdentifier(body.forget_skill->skill_id, mojom::kMaxSkillIdBytes);
    case mojom::StorageOperation::kSetAssistantConfiguration: {
      if (bodies != 1 || body.assistant_configuration.is_null() ||
          !body.task_id.empty() || !body.transaction_batch.empty() ||
          !storage_internal::IsNeutralTaskIdSeed(body.task_id_seed) ||
          body.expected_revision == std::numeric_limits<uint64_t>::max() ||
          body.resulting_revision != body.expected_revision + 1u ||
          body.assistant_configuration->disabled_abilities.size() >
              mojom::kMaxAssistantAbilities ||
          body.assistant_configuration->pace > mojom::kMaxPersonalityScale ||
          body.assistant_configuration->length > mojom::kMaxPersonalityScale ||
          body.assistant_configuration->check_in >
              mojom::kMaxPersonalityScale ||
          static_cast<int>(body.assistant_configuration->preset) < 0 ||
          static_cast<int>(body.assistant_configuration->preset) > 2) {
        return false;
      }
      std::optional<int> previous;
      for (mojom::AssistantAbility ability :
           body.assistant_configuration->disabled_abilities) {
        const int wire = static_cast<int>(ability);
        if (wire < 0 || wire >= 16 || (previous && wire <= *previous)) {
          return false;
        }
        previous = wire;
      }
      return true;
    }
    case mojom::StorageOperation::kQueryWorkspace:
      // There is no query surface and there is no plan for one; what a shipping
      // durable store may be asked is OD-104.
      return false;
  }
  return false;
}

}  // namespace taffy
