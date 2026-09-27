// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/services/core/rust_core_command_conversions.h"

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <utility>

#include "base/time/time.h"
#include "taffy/services/core/rust_core_skill_records.h"

namespace taffy::core_service_internal {

namespace {

template <size_t Size>
std::optional<std::array<uint8_t, Size>> ToFixedBytes(
    const std::vector<uint8_t>& input) {
  if (input.size() != Size) {
    return std::nullopt;
  }
  std::array<uint8_t, Size> output{};
  std::copy(input.begin(), input.end(), output.begin());
  return output;
}

void CopyToRustVec(const std::vector<uint8_t>& input,
                   rust::Vec<uint8_t>* output) {
  output->reserve(input.size());
  for (uint8_t byte : input) {
    output->push_back(byte);
  }
}

core_bridge::BridgeStartOperation ToBridgeStartOperation(
    const core_service::mojom::OperationEnvelope& operation) {
  core_bridge::BridgeStartOperation projected;
  projected.operation_id = operation.operation_id;
  projected.service_generation = operation.service_generation;
  projected.task_revision = operation.task_revision;
  projected.deadline_monotonic_ms = operation.deadline_monotonic_ms;
  projected.idempotency_key = operation.idempotency_key;
  return projected;
}

}  // namespace

core_bridge::BridgeOperation ToBridgeOperation(
    const core_service::mojom::OperationEnvelope& operation) {
  core_bridge::BridgeOperation projected;
  projected.operation_id = operation.operation_id;
  projected.service_generation = operation.service_generation;
  projected.task_revision = operation.task_revision;
  projected.deadline_monotonic_ms = operation.deadline_monotonic_ms;
  projected.idempotency_key = operation.idempotency_key;
  return projected;
}

core_bridge::BridgeStorageCompletionOperation
ToBridgeStorageCompletionOperation(
    const core_service::mojom::OperationEnvelope& operation) {
  core_bridge::BridgeStorageCompletionOperation projected;
  projected.operation_id = operation.operation_id;
  projected.service_generation = operation.service_generation;
  projected.task_revision = operation.task_revision;
  projected.deadline_monotonic_ms = operation.deadline_monotonic_ms;
  projected.idempotency_key = operation.idempotency_key;
  return projected;
}

std::optional<core_bridge::BridgeStartTask> ToBridgeStart(
    const core_service::mojom::CoreServiceCommand& command) {
  core_bridge::BridgeStartTask projected;
  projected.operation = ToBridgeStartOperation(*command.operation);
  const core_service::mojom::StartTaskCommand& start = *command.start_task;
  projected.task_id = start.task_id;
  projected.has_workspace_id = start.workspace_id.has_value();
  projected.workspace_id = start.workspace_id.value_or(std::string());
  projected.browser_profile_id = start.browser_profile_id;
  projected.browser_session_id = start.browser_session_id;
  projected.kind = static_cast<uint8_t>(start.kind);
  projected.goal = start.goal;
  projected.control_mode = static_cast<uint8_t>(start.control_mode);
  projected.has_provider_route_id = start.provider_route_id.has_value();
  projected.provider_route_id = start.provider_route_id.value_or(std::string());
  projected.assistant_config_version = start.assistant_config_version;
  projected.policy_version = start.policy_version;
  projected.has_skill_version_id = start.skill_version_id.has_value();
  projected.skill_version_id = start.skill_version_id.value_or(std::string());
  projected.has_builtin_skill = !!start.builtin_skill;
  if (start.builtin_skill) {
    projected.builtin_skill_id =
        static_cast<uint8_t>(start.builtin_skill->skill_id);
    projected.builtin_skill_version = start.builtin_skill->version;
  }
  for (const std::string& tool : start.tool_allowlist) {
    projected.tool_allowlist.emplace_back(tool);
  }
  projected.milestone = static_cast<uint8_t>(start.milestone);
  for (const auto& budget : start.budgets) {
    if (!budget) {
      continue;
    }
    core_bridge::BridgeBudget bridge_budget;
    bridge_budget.kind = static_cast<uint8_t>(budget->kind);
    bridge_budget.limit = budget->limit;
    projected.budgets.push_back(std::move(bridge_budget));
  }
  projected.has_task_deadline = start.has_task_deadline;
  projected.task_deadline_monotonic_ms = start.task_deadline_monotonic_ms;
  projected.task_deadline_utc_ms = start.task_deadline_utc_ms;
  projected.has_predecessor_task_id = start.predecessor_task_id.has_value();
  projected.predecessor_task_id =
      start.predecessor_task_id.value_or(std::string());
  projected.trace_id = start.trace_id;
  std::optional<std::array<uint8_t, 32>> task_id_seed =
      ToFixedBytes<32>(start.task_id_seed);
  if (!task_id_seed) {
    return std::nullopt;
  }
  projected.task_id_seed = *task_id_seed;
  projected.template_id = static_cast<uint8_t>(start.template_id);
  if (!start.consent_preview) {
    return std::nullopt;
  }
  for (const auto& source : start.consent_preview->sources) {
    if (!source) {
      return std::nullopt;
    }
    core_bridge::BridgeStartConsentSource bridge_source;
    bridge_source.source_id = source->source_id;
    bridge_source.tab_id = source->tab_id;
    bridge_source.normalized_origin = source->normalized_origin;
    bridge_source.has_canonical_locator = source->canonical_locator.has_value();
    bridge_source.canonical_locator =
        source->canonical_locator.value_or(std::string());
    projected.consent_sources.push_back(std::move(bridge_source));
  }
  projected.source_discovery_enabled =
      start.consent_preview->source_discovery_enabled;
  projected.new_source_cap = start.consent_preview->new_source_cap;
  projected.consent_provider_route =
      static_cast<uint8_t>(start.consent_preview->provider_route);
  projected.initial_consent_receipt_id = start.initial_consent_receipt_id;
  projected.has_library_refresh = !!start.library_refresh;
  if (start.library_refresh) {
    if (!start.library_refresh->sources.empty()) {
      return std::nullopt;
    }
    projected.library_refresh_preview_id = start.library_refresh->preview_id;
    projected.library_refresh_library_revision =
        start.library_refresh->library_revision;
    projected.library_refresh_collection_id =
        start.library_refresh->collection_id;
    projected.library_refresh_workspace_revision =
        start.library_refresh->source_workspace_revision;
  }
  return projected;
}

std::optional<core_bridge::BridgeBootstrap> ToBridgeBootstrap(
    const core_service::mojom::CoreBootstrap& bootstrap) {
  core_bridge::BridgeBootstrap projected;
  projected.service_generation = bootstrap.service_generation;
  std::optional<std::array<uint8_t, 32>> generation_entropy =
      ToFixedBytes<32>(bootstrap.generation_capability_entropy);
  if (!generation_entropy) {
    return std::nullopt;
  }
  projected.generation_capability_entropy = *generation_entropy;
  const int64_t monotonic_millis =
      base::TimeTicks::Now().since_origin().InMilliseconds();
  projected.initial_monotonic_millis =
      monotonic_millis < 0 ? 0u : monotonic_millis;
  const int64_t utc_millis = base::Time::Now().InMillisecondsSinceUnixEpoch();
  projected.initial_utc_millis = utc_millis < 0 ? 0u : utc_millis;
  projected.private_profile = bootstrap.private_profile;
  projected.browser_profile_id = bootstrap.browser_profile_id;
  projected.browser_session_id = bootstrap.browser_session_id;
  projected.has_account_session = !!bootstrap.account_session;
  if (bootstrap.account_session) {
    projected.session_handle = bootstrap.account_session->session_handle;
    projected.account_subject = bootstrap.account_session->account_subject;
    projected.account_expires_at_monotonic_ms =
        bootstrap.account_session->expires_at_monotonic_ms;
    projected.account_rotation = bootstrap.account_session->rotation;
    projected.account_auth_method =
        static_cast<uint8_t>(bootstrap.account_session->auth_method);
    projected.has_account_email = bootstrap.account_session->email.has_value();
    if (bootstrap.account_session->email) {
      projected.account_email = *bootstrap.account_session->email;
    }
    projected.has_account_display_name =
        bootstrap.account_session->display_name.has_value();
    if (bootstrap.account_session->display_name) {
      projected.account_display_name = *bootstrap.account_session->display_name;
    }
  }
  for (core_service::mojom::AccountAuthMethod method :
       bootstrap.available_account_methods) {
    projected.available_account_methods.push_back(static_cast<uint8_t>(method));
  }
  for (const auto& task : bootstrap.tasks) {
    if (!task) {
      continue;
    }
    core_bridge::BridgeTaskRestore restored;
    restored.task_id = task->task_id;
    std::optional<std::array<uint8_t, 32>> task_id_seed =
        ToFixedBytes<32>(task->task_id_seed);
    if (!task_id_seed) {
      return std::nullopt;
    }
    restored.task_id_seed = *task_id_seed;
    for (const auto& batch : task->batches) {
      if (!batch) {
        continue;
      }
      core_bridge::BridgeCommittedBatch committed;
      committed.effect_id = batch->effect_id;
      committed.expected_revision = batch->expected_revision;
      committed.resulting_revision = batch->resulting_revision;
      CopyToRustVec(batch->transaction_batch, &committed.transaction_batch);
      restored.batches.push_back(std::move(committed));
    }
    projected.tasks.push_back(std::move(restored));
  }
  for (const auto& workspace : bootstrap.workspaces) {
    if (!workspace) {
      return std::nullopt;
    }
    core_bridge::BridgeWorkspaceRestore restored;
    restored.workspace_id = workspace->workspace_id;
    restored.revision = workspace->revision;
    CopyToRustVec(workspace->snapshot, &restored.snapshot);
    projected.workspaces.push_back(std::move(restored));
  }
  projected.library_revision = bootstrap.library_revision;
  for (const auto& entry : bootstrap.library_entries) {
    if (!entry) {
      return std::nullopt;
    }
    core_bridge::BridgeLibraryEntryRestore restored;
    restored.entry_id = entry->entry_id;
    restored.revision = entry->revision;
    restored.collection_id = entry->collection_id;
    restored.collection_name = entry->collection_name;
    restored.source_workspace_id = entry->source_workspace_id;
    restored.source_workspace_revision = entry->source_workspace_revision;
    restored.source_fact_id = entry->source_fact_id;
    restored.field = entry->field;
    restored.original_value = entry->original_value;
    restored.has_correction = entry->correction.has_value();
    restored.correction = entry->correction.value_or(std::string());
    restored.kind = static_cast<uint8_t>(entry->kind);
    restored.captured_at_epoch_ms = entry->captured_at_epoch_ms;
    restored.last_checked_epoch_ms = entry->last_checked_epoch_ms;
    restored.has_conflict = entry->has_conflict;
    for (const auto& source : entry->sources) {
      if (!source) {
        return std::nullopt;
      }
      core_bridge::BridgeLibrarySourceRestore restored_source;
      restored_source.source_id = source->source_id;
      restored_source.title = source->title;
      restored_source.host = source->host;
      restored_source.observed_at_epoch_ms = source->observed_at_epoch_ms;
      restored.sources.push_back(std::move(restored_source));
    }
    projected.library_entries.push_back(std::move(restored));
  }
  projected.memory_revision = bootstrap.memory_revision;
  for (const auto& record : bootstrap.memory_records) {
    if (!record) {
      return std::nullopt;
    }
    core_bridge::BridgeMemoryRecordRestore restored;
    restored.memory_id = record->memory_id;
    restored.revision = record->revision;
    restored.statement = record->statement;
    restored.source_kind = static_cast<uint8_t>(record->source_kind);
    restored.has_source_task_id = record->source_task_id.has_value();
    restored.source_task_id = record->source_task_id.value_or(std::string());
    restored.has_source_workspace = static_cast<bool>(record->source_workspace);
    if (record->source_workspace) {
      restored.source_workspace.workspace_id =
          record->source_workspace->workspace_id;
      restored.source_workspace.display_name =
          record->source_workspace->display_name;
    }
    restored.scope_kind = static_cast<uint8_t>(record->scope_kind);
    restored.has_scope_workspace = static_cast<bool>(record->scope_workspace);
    if (record->scope_workspace) {
      restored.scope_workspace.workspace_id =
          record->scope_workspace->workspace_id;
      restored.scope_workspace.display_name =
          record->scope_workspace->display_name;
    }
    restored.sensitivity = static_cast<uint8_t>(record->sensitivity);
    restored.created_at_epoch_ms = record->created_at_epoch_ms;
    restored.updated_at_epoch_ms = record->updated_at_epoch_ms;
    restored.reviewed_at_epoch_ms = record->reviewed_at_epoch_ms;
    restored.expires_at_epoch_ms = record->expires_at_epoch_ms;
    projected.memory_records.push_back(std::move(restored));
  }
  projected.asset_platform = static_cast<uint8_t>(bootstrap.asset_platform);
  for (const auto& asset : bootstrap.assets) {
    if (!asset) {
      return std::nullopt;
    }
    core_bridge::BridgeAssetOnDisk found;
    found.asset_id = asset->asset_id;
    found.asset_revision = asset->asset_revision;
    found.presence = static_cast<uint8_t>(asset->presence);
    found.written_bytes = asset->written_bytes;
    projected.assets.push_back(std::move(found));
  }
  auto skills = ToBridgeSkillRecords(bootstrap.skills);
  if (!skills) {
    return std::nullopt;
  }
  projected.skills = std::move(*skills);
  for (const auto& run : bootstrap.recall) {
    if (!run) {
      return std::nullopt;
    }
    core_bridge::BridgeSkillRunRecord restored;
    restored.skill_id = run->skill_id;
    restored.version = run->version;
    restored.task_id = run->task_id;
    restored.outcome = static_cast<uint8_t>(run->outcome);
    restored.ran_at_utc_ms = run->ran_at_utc_ms;
    projected.recall.push_back(std::move(restored));
  }
  projected.has_assistant_configuration = !!bootstrap.assistant_configuration;
  if (bootstrap.assistant_configuration) {
    projected.assistant_configuration_revision =
        bootstrap.assistant_configuration->revision;
    for (core_service::mojom::AssistantAbility ability :
         bootstrap.assistant_configuration->disabled_abilities) {
      projected.assistant_configuration_disabled_abilities.push_back(
          static_cast<uint8_t>(ability));
    }
    projected.assistant_configuration_preset =
        static_cast<uint8_t>(bootstrap.assistant_configuration->preset);
    projected.assistant_configuration_pace =
        bootstrap.assistant_configuration->pace;
    projected.assistant_configuration_length =
        bootstrap.assistant_configuration->length;
    projected.assistant_configuration_check_in =
        bootstrap.assistant_configuration->check_in;
  }
  return projected;
}

std::optional<core_bridge::BridgeAssistantConfigurationCommand>
ToBridgeAssistantConfiguration(
    const core_service::mojom::CoreServiceCommand& command) {
  if (!command.operation || !command.set_assistant_configuration) {
    return std::nullopt;
  }
  core_bridge::BridgeAssistantConfigurationCommand projected;
  projected.operation.operation_id = command.operation->operation_id;
  projected.operation.service_generation =
      command.operation->service_generation;
  projected.operation.task_revision = command.operation->task_revision;
  projected.operation.deadline_monotonic_ms =
      command.operation->deadline_monotonic_ms;
  projected.operation.idempotency_key = command.operation->idempotency_key;
  projected.expected_revision =
      command.set_assistant_configuration->expected_revision;
  for (core_service::mojom::AssistantAbility ability :
       command.set_assistant_configuration->disabled_abilities) {
    projected.disabled_abilities.push_back(static_cast<uint8_t>(ability));
  }
  projected.preset =
      static_cast<uint8_t>(command.set_assistant_configuration->preset);
  projected.pace = command.set_assistant_configuration->pace;
  projected.length = command.set_assistant_configuration->length;
  projected.check_in = command.set_assistant_configuration->check_in;
  return projected;
}

}  // namespace taffy::core_service_internal
