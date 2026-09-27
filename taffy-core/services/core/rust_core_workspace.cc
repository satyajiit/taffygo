// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/services/core/rust_core_workspace.h"

#include <string>

#include "taffy/contracts/core-service/generated/cpp/core_service_enums.h"
#include "taffy/services/core/rust_core_command_conversions.h"

namespace taffy::core_service_internal {

namespace mojom = core_service::mojom;
namespace wire = core_service::wire;

namespace {

constexpr size_t kTaskIdSeedBytes = 32u;

// The workspace plane mirrors the operation envelope under its own name, the
// same way the provider plane does: two cxx bridge modules cannot share a
// by-value struct without an include cycle between their generated headers.
core_bridge::BridgeWorkspaceOperation ToBridgeWorkspaceOperation(
    const mojom::OperationEnvelope& in) {
  core_bridge::BridgeWorkspaceOperation out;
  out.operation_id = in.operation_id;
  out.service_generation = in.service_generation;
  out.task_revision = in.task_revision;
  out.deadline_monotonic_ms = in.deadline_monotonic_ms;
  out.idempotency_key = in.idempotency_key;
  return out;
}

}  // namespace

std::optional<core_bridge::BridgeWorkspaceCommand> ToBridgeWorkspaceCommand(
    const mojom::CoreServiceCommand& command) {
  core_bridge::BridgeWorkspaceCommand projected;
  projected.operation = ToBridgeWorkspaceOperation(*command.operation);
  projected.kind = static_cast<uint8_t>(command.kind);
  if (command.correct_workspace_fact) {
    projected.workspace_id = command.correct_workspace_fact->workspace_id;
    projected.expected_revision =
        command.correct_workspace_fact->expected_revision;
    projected.fact_id = command.correct_workspace_fact->fact_id;
    projected.value = command.correct_workspace_fact->value;
  } else if (command.exclude_workspace_source) {
    projected.workspace_id = command.exclude_workspace_source->workspace_id;
    projected.expected_revision =
        command.exclude_workspace_source->expected_revision;
    projected.source_id = command.exclude_workspace_source->source_id;
  } else if (command.request_workspace_export) {
    projected.request_id = command.request_workspace_export->request_id;
    projected.workspace_id = command.request_workspace_export->workspace_id;
    projected.expected_revision =
        command.request_workspace_export->expected_revision;
    projected.format =
        static_cast<uint8_t>(command.request_workspace_export->format);
  } else if (command.save_workspace) {
    projected.workspace_id = command.save_workspace->workspace_id;
    projected.expected_revision = command.save_workspace->expected_revision;
  } else if (command.rename_workspace) {
    projected.workspace_id = command.rename_workspace->workspace_id;
    projected.expected_revision = command.rename_workspace->expected_revision;
    projected.display_name = command.rename_workspace->display_name;
  } else if (command.delete_workspace) {
    projected.workspace_id = command.delete_workspace->workspace_id;
    projected.expected_revision = command.delete_workspace->expected_revision;
    projected.confirmation_token = command.delete_workspace->confirmation_token;
  } else if (command.discard_workspace) {
    projected.workspace_id = command.discard_workspace->workspace_id;
    projected.expected_revision = command.discard_workspace->expected_revision;
  } else if (command.search_library) {
    projected.request_id = command.search_library->request_id;
    projected.query = command.search_library->query;
    projected.limit = command.search_library->limit;
    projected.requested_at_epoch_ms =
        command.search_library->requested_at_epoch_ms;
  } else if (command.save_library_fact) {
    projected.workspace_id = command.save_library_fact->workspace_id;
    projected.expected_workspace_revision =
        command.save_library_fact->expected_workspace_revision;
    projected.fact_id = command.save_library_fact->fact_id;
    projected.expected_library_revision =
        command.save_library_fact->expected_library_revision;
    projected.expected_entry_revision =
        command.save_library_fact->expected_entry_revision;
    projected.approved_at_epoch_ms =
        command.save_library_fact->approved_at_epoch_ms;
  } else if (command.remove_library_entry) {
    projected.entry_id = command.remove_library_entry->entry_id;
    projected.expected_library_revision =
        command.remove_library_entry->expected_library_revision;
    projected.expected_entry_revision =
        command.remove_library_entry->expected_entry_revision;
    projected.removed_at_epoch_ms =
        command.remove_library_entry->removed_at_epoch_ms;
  } else if (command.request_library_export) {
    projected.request_id = command.request_library_export->request_id;
    projected.expected_library_revision =
        command.request_library_export->expected_library_revision;
    projected.has_collection_id =
        command.request_library_export->collection_id.has_value();
    projected.collection_id =
        command.request_library_export->collection_id.value_or(std::string());
    projected.format =
        static_cast<uint8_t>(command.request_library_export->format);
  } else if (command.search_memory) {
    projected.request_id = command.search_memory->request_id;
    projected.query = command.search_memory->query;
    projected.limit = command.search_memory->limit;
    projected.requested_at_epoch_ms =
        command.search_memory->requested_at_epoch_ms;
  } else if (command.upsert_memory) {
    projected.has_memory_id = command.upsert_memory->memory_id.has_value();
    projected.memory_id =
        command.upsert_memory->memory_id.value_or(std::string());
    projected.memory_statement = command.upsert_memory->statement;
    projected.memory_scope_kind =
        static_cast<uint8_t>(command.upsert_memory->scope_kind);
    projected.has_memory_scope_workspace =
        static_cast<bool>(command.upsert_memory->scope_workspace);
    if (command.upsert_memory->scope_workspace) {
      projected.memory_scope_workspace_id =
          command.upsert_memory->scope_workspace->workspace_id;
      projected.memory_scope_workspace_name =
          command.upsert_memory->scope_workspace->display_name;
    }
    projected.memory_sensitivity =
        static_cast<uint8_t>(command.upsert_memory->sensitivity);
    projected.expected_memory_revision =
        command.upsert_memory->expected_memory_revision;
    projected.expected_record_revision =
        command.upsert_memory->expected_record_revision;
    projected.memory_expires_at_epoch_ms =
        command.upsert_memory->expires_at_epoch_ms;
    projected.memory_approved_at_epoch_ms =
        command.upsert_memory->approved_at_epoch_ms;
  } else if (command.delete_memory) {
    projected.memory_id = command.delete_memory->memory_id;
    projected.expected_memory_revision =
        command.delete_memory->expected_memory_revision;
    projected.expected_record_revision =
        command.delete_memory->expected_record_revision;
    projected.memory_deleted_at_epoch_ms =
        command.delete_memory->deleted_at_epoch_ms;
  } else {
    return std::nullopt;
  }
  return projected;
}

mojom::EffectEnvelopePtr ToMojoWorkspaceEffect(
    const core_bridge::BridgeWorkspaceEffect& in) {
  auto effect = mojom::EffectEnvelope::New();
  effect->operation = mojom::OperationEnvelope::New();
  effect->operation->operation_id = std::string(in.operation.operation_id);
  effect->operation->service_generation = in.operation.service_generation;
  effect->operation->task_revision = in.operation.task_revision;
  effect->operation->deadline_monotonic_ms = in.operation.deadline_monotonic_ms;
  effect->operation->idempotency_key =
      std::string(in.operation.idempotency_key);
  effect->effect_id = std::string(in.effect_id);
  effect->kind = mojom::EffectKind::kStorageCommit;
  effect->retry_class = mojom::RetryClass::kIdempotent;
  effect->storage_commit = mojom::StorageCommitEffect::New();
  // Every StorageCommitEffect crosses a fixed-size Mojo field, including
  // workspace, Library, and Memory operations that carry no task identity.
  // An empty generated default is not a valid serialization of bytes32.
  effect->storage_commit->task_id_seed.assign(kTaskIdSeedBytes, 0u);
  effect->storage_commit->operation_kind =
      static_cast<mojom::StorageOperation>(in.operation_kind);
  if (effect->storage_commit->operation_kind ==
      mojom::StorageOperation::kUpsertWorkspace) {
    effect->storage_commit->workspace = mojom::WorkspacePersistEffect::New();
    effect->storage_commit->workspace->workspace_id =
        std::string(in.workspace_id);
    effect->storage_commit->workspace->expected_revision = in.expected_revision;
    effect->storage_commit->workspace->resulting_revision =
        in.resulting_revision;
    effect->storage_commit->workspace->snapshot.assign(in.snapshot.begin(),
                                                       in.snapshot.end());
  } else if (effect->storage_commit->operation_kind ==
             mojom::StorageOperation::kDeleteWorkspace) {
    effect->storage_commit->workspace_deletion =
        mojom::WorkspaceDeletionEffect::New();
    auto& deletion = effect->storage_commit->workspace_deletion;
    deletion->workspace_id = std::string(in.workspace_id);
    deletion->expected_revision = in.expected_revision;
    deletion->resulting_revision = in.resulting_revision;
    deletion->sources = in.sources;
    deletion->facts = in.facts;
    deletion->artifact_metadata = in.artifact_metadata;
    deletion->derived_indexes = in.derived_indexes;
    deletion->confirmation_token = std::string(in.confirmation_token);
  } else if (effect->storage_commit->operation_kind ==
             mojom::StorageOperation::kUpsertLibraryEntry) {
    const std::optional<mojom::LibraryFactKind> kind =
        wire::LibraryFactKindFromWire(in.library_fact_kind);
    if (!kind) {
      return nullptr;
    }
    effect->storage_commit->expected_revision = in.expected_revision;
    effect->storage_commit->resulting_revision = in.resulting_revision;
    effect->storage_commit->library_entry = mojom::LibraryPersistEffect::New();
    auto& persisted = effect->storage_commit->library_entry;
    persisted->expected_entry_revision = in.expected_entry_revision;
    persisted->entry = mojom::LibraryEntryRecord::New();
    auto& entry = persisted->entry;
    entry->entry_id = std::string(in.entry_id);
    entry->revision = in.resulting_entry_revision;
    entry->collection_id = std::string(in.collection_id);
    entry->collection_name = std::string(in.collection_name);
    entry->source_workspace_id = std::string(in.source_workspace_id);
    entry->source_workspace_revision = in.source_workspace_revision;
    entry->source_fact_id = std::string(in.source_fact_id);
    entry->field = std::string(in.field);
    entry->original_value = std::string(in.original_value);
    if (in.has_correction) {
      entry->correction = std::string(in.correction);
    }
    entry->kind = *kind;
    entry->captured_at_epoch_ms = in.captured_at_epoch_ms;
    entry->last_checked_epoch_ms = in.last_checked_epoch_ms;
    entry->has_conflict = in.has_conflict;
    for (const auto& source : in.library_sources) {
      auto projected_source = mojom::LibrarySourceRecord::New();
      projected_source->source_id = std::string(source.source_id);
      projected_source->title = std::string(source.title);
      projected_source->host = std::string(source.host);
      projected_source->observed_at_epoch_ms = source.observed_at_epoch_ms;
      entry->sources.push_back(std::move(projected_source));
    }
  } else if (effect->storage_commit->operation_kind ==
             mojom::StorageOperation::kRemoveLibraryEntry) {
    effect->storage_commit->expected_revision = in.expected_revision;
    effect->storage_commit->resulting_revision = in.resulting_revision;
    effect->storage_commit->library_deletion =
        mojom::LibraryDeletionEffect::New();
    auto& deletion = effect->storage_commit->library_deletion;
    deletion->entry_id = std::string(in.entry_id);
    deletion->expected_entry_revision = in.expected_entry_revision;
    deletion->resulting_entry_revision = in.resulting_entry_revision;
    deletion->removed_at_epoch_ms = in.removed_at_epoch_ms;
  } else if (effect->storage_commit->operation_kind ==
             mojom::StorageOperation::kUpsertMemory) {
    const std::optional<mojom::MemorySourceKind> source_kind =
        wire::MemorySourceKindFromWire(in.memory_source_kind);
    const std::optional<mojom::MemoryScopeKind> scope_kind =
        wire::MemoryScopeKindFromWire(in.memory_scope_kind);
    const std::optional<mojom::MemorySensitivity> sensitivity =
        wire::MemorySensitivityFromWire(in.memory_sensitivity);
    if (!source_kind || !scope_kind || !sensitivity) {
      return nullptr;
    }
    effect->storage_commit->expected_revision = in.expected_revision;
    effect->storage_commit->resulting_revision = in.resulting_revision;
    effect->storage_commit->memory_record = mojom::MemoryPersistEffect::New();
    auto& persisted = effect->storage_commit->memory_record;
    persisted->expected_record_revision = in.expected_entry_revision;
    persisted->record = mojom::MemoryRecord::New();
    auto& record = persisted->record;
    record->memory_id = std::string(in.memory_id);
    record->revision = in.resulting_entry_revision;
    record->statement = std::string(in.memory_statement);
    record->source_kind = *source_kind;
    if (in.has_memory_source_task_id) {
      record->source_task_id = std::string(in.memory_source_task_id);
    }
    if (in.has_memory_source_workspace) {
      record->source_workspace = mojom::MemoryWorkspaceRecord::New(
          std::string(in.memory_source_workspace_id),
          std::string(in.memory_source_workspace_name));
    }
    record->scope_kind = *scope_kind;
    if (in.has_memory_scope_workspace) {
      record->scope_workspace = mojom::MemoryWorkspaceRecord::New(
          std::string(in.memory_scope_workspace_id),
          std::string(in.memory_scope_workspace_name));
    }
    record->sensitivity = *sensitivity;
    record->created_at_epoch_ms = in.memory_created_at_epoch_ms;
    record->updated_at_epoch_ms = in.memory_updated_at_epoch_ms;
    record->reviewed_at_epoch_ms = in.memory_reviewed_at_epoch_ms;
    record->expires_at_epoch_ms = in.memory_expires_at_epoch_ms;
  } else if (effect->storage_commit->operation_kind ==
             mojom::StorageOperation::kDeleteMemory) {
    effect->storage_commit->expected_revision = in.expected_revision;
    effect->storage_commit->resulting_revision = in.resulting_revision;
    effect->storage_commit->memory_deletion =
        mojom::MemoryDeletionEffect::New();
    auto& deletion = effect->storage_commit->memory_deletion;
    deletion->memory_id = std::string(in.memory_id);
    deletion->expected_record_revision = in.expected_entry_revision;
    deletion->resulting_record_revision = in.resulting_entry_revision;
    deletion->deleted_at_epoch_ms = in.memory_deleted_at_epoch_ms;
  } else {
    return nullptr;
  }
  return effect;
}

}  // namespace taffy::core_service_internal
