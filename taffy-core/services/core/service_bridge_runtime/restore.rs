// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Bootstrap restore: the one place a browser-owned snapshot becomes a runtime.
//!
//! Every refusal here produces the same thing — a `ServiceBridge` with no
//! runtime — because a partially restored profile is worse than an unavailable
//! one: it would answer commands from a state the journal does not agree with.

use std::collections::BTreeMap;
use std::rc::Rc;

use core_runtime::wire;
use core_runtime::{
    create_profile_service_runtime, decode_account_session, DigestError,
    ProfileRuntimeConfiguration, ProfileServiceRuntime, ServiceGeneration, Sha256Port,
};

use super::{BridgeInitializationStatus, ServiceBridge};
use crate::ffi;
use crate::service_bridge_bootstrap_ffi::ffi as bootstrap_ffi;

mod tasks;

use tasks::restore_tasks;

pub(crate) struct ChromiumDigest;

impl Sha256Port for ChromiumDigest {
    fn sha256(&self, input: &[u8]) -> Result<[u8; 32], DigestError> {
        ffi::ChromiumSha256(input)
            .try_into()
            .map_err(|_| DigestError::Unavailable)
    }
}

#[allow(non_snake_case)]
pub(crate) fn CreateServiceBridge(bootstrap: bootstrap_ffi::BridgeBootstrap) -> Box<ServiceBridge> {
    let generation = ServiceGeneration::new(bootstrap.service_generation);
    let mut restored_task_effects = Vec::new();
    let available_account_methods = bootstrap
        .available_account_methods
        .iter()
        .copied()
        .map(|method| {
            wire::AccountAuthMethod::from_wire(u32::from(method)).map(|method| match method {
                wire::AccountAuthMethod::Google => core_runtime::AccountAuthMethod::Google,
                wire::AccountAuthMethod::EmailLink => core_runtime::AccountAuthMethod::EmailLink,
                wire::AccountAuthMethod::Github => core_runtime::AccountAuthMethod::Github,
                wire::AccountAuthMethod::Facebook => core_runtime::AccountAuthMethod::Facebook,
            })
        })
        .collect::<Option<Vec<_>>>();
    let skills = crate::service_bridge_bootstrap_ffi::skill_records_to_wire(bootstrap.skills);
    let recall = bootstrap
        .recall
        .iter()
        .map(|run| {
            Some(wire::SkillRunRecord {
                skill_id: run.skill_id.clone(),
                version: run.version,
                task_id: run.task_id.clone(),
                outcome: wire::SkillRunOutcome::from_wire(u32::from(run.outcome))?,
                ran_at_utc_ms: run.ran_at_utc_ms,
            })
        })
        .collect::<Option<Vec<_>>>();
    let assistant_configuration = if bootstrap.has_assistant_configuration {
        let disabled_abilities = bootstrap
            .assistant_configuration_disabled_abilities
            .iter()
            .copied()
            .map(|ability| wire::AssistantAbility::from_wire(u32::from(ability)))
            .collect::<Option<Vec<_>>>();
        match (
            disabled_abilities,
            wire::PersonalityPreset::from_wire(u32::from(bootstrap.assistant_configuration_preset)),
        ) {
            (Some(disabled_abilities), Some(preset)) => Some(Some(wire::AssistantConfiguration {
                revision: bootstrap.assistant_configuration_revision,
                disabled_abilities,
                preset,
                pace: bootstrap.assistant_configuration_pace,
                length: bootstrap.assistant_configuration_length,
                check_in: bootstrap.assistant_configuration_check_in,
            })),
            _ => None,
        }
    } else {
        Some(None)
    };
    let mut runtime = match (
        available_account_methods,
        skills,
        recall,
        assistant_configuration,
    ) {
        (Some(methods), Some(skills), Some(recall), Some(assistant_configuration)) => {
            create_profile_service_runtime(
                ProfileRuntimeConfiguration {
                    generation,
                    generation_capability_entropy: bootstrap.generation_capability_entropy,
                    initial_utc_millis: bootstrap.initial_utc_millis,
                    private_profile: bootstrap.private_profile,
                    browser_profile_id: bootstrap.browser_profile_id,
                    browser_session_id: bootstrap.browser_session_id,
                    available_account_methods: methods,
                    skills,
                    recall,
                    assistant_configuration,
                },
                Rc::new(ChromiumDigest),
            )
            .ok()
        }
        _ => None,
    };
    let mut restore_refusal = "";
    let mut initialization_status = if runtime.is_some() {
        BridgeInitializationStatus::Ready
    } else {
        BridgeInitializationStatus::RuntimeConfigurationRefused
    };
    if let Some(candidate) = runtime.as_mut() {
        let account = if bootstrap.has_account_session {
            wire::AccountAuthMethod::from_wire(u32::from(bootstrap.account_auth_method))
                .and_then(|auth_method| {
                    decode_account_session(&wire::AccountSessionHandle {
                        session_handle: bootstrap.session_handle,
                        account_subject: bootstrap.account_subject,
                        expires_at_monotonic_ms: bootstrap.account_expires_at_monotonic_ms,
                        rotation: bootstrap.account_rotation,
                        auth_method,
                        email: (bootstrap.has_account_email && !bootstrap.account_email.is_empty())
                            .then(|| bootstrap.account_email.clone()),
                        display_name: (bootstrap.has_account_display_name
                            && !bootstrap.account_display_name.is_empty())
                        .then(|| bootstrap.account_display_name.clone()),
                    })
                    .ok()
                })
                .map(Some)
        } else {
            Some(None)
        };
        let refusal = match account {
            Some(account) => candidate
                .restore_account_session(account)
                .err()
                .map(|_| BridgeInitializationStatus::AccountRestoreRefused),
            None => Some(BridgeInitializationStatus::AccountRestoreRefused),
        }
        .or_else(|| {
            restore_tasks(candidate, bootstrap.tasks, &mut restored_task_effects)
                .err()
                .map(|label| {
                    restore_refusal = label;
                    BridgeInitializationStatus::TaskRestoreRefused
                })
        })
        .or_else(|| {
            (!restore_workspaces(candidate, bootstrap.workspaces))
                .then_some(BridgeInitializationStatus::WorkspaceRestoreRefused)
        })
        .or_else(|| {
            (!restore_library(
                candidate,
                bootstrap.library_revision,
                bootstrap.library_entries,
            ))
            .then_some(BridgeInitializationStatus::LibraryRestoreRefused)
        })
        .or_else(|| {
            (!restore_memory(
                candidate,
                bootstrap.memory_revision,
                bootstrap.memory_records,
            ))
            .then_some(BridgeInitializationStatus::MemoryRestoreRefused)
        })
        .or_else(|| {
            (!crate::service_bridge_assets::restore_assets(
                candidate,
                bootstrap.asset_platform,
                bootstrap.assets,
            ))
            .then_some(BridgeInitializationStatus::AssetRestoreRefused)
        });
        if let Some(refusal) = refusal {
            initialization_status = refusal;
        }
    }
    if initialization_status != BridgeInitializationStatus::Ready {
        runtime = None;
        restored_task_effects.clear();
    }
    Box::new(ServiceBridge {
        runtime,
        initialization_status,
        generation,
        state_sequence: 0,
        pending_opens: BTreeMap::new(),
        pending_submits: BTreeMap::new(),
        pending_task_cancellations: BTreeMap::new(),
        pending_skill_runs: Default::default(),
        pending_assistant_configurations: Default::default(),
        pending_skill_mutations: Default::default(),
        page_snapshot_exporter: core_runtime::PageSnapshotExporter::new(),
        recorded_skill_run_tasks: Default::default(),
        pending_task_effects: BTreeMap::new(),
        pending_model_attempts: BTreeMap::new(),
        task_grants: BTreeMap::new(),
        restored_task_effects,
        initial_monotonic_millis: bootstrap.initial_monotonic_millis,
        initial_utc_millis: bootstrap.initial_utc_millis,
        last_refusal: restore_refusal,
    })
}

fn restore_library(
    runtime: &mut ProfileServiceRuntime,
    revision: u64,
    entries: Vec<bootstrap_ffi::BridgeLibraryEntryRestore>,
) -> bool {
    let entries = entries
        .into_iter()
        .map(|entry| {
            Some(wire::LibraryEntryRecord {
                entry_id: entry.entry_id,
                revision: entry.revision,
                collection_id: entry.collection_id,
                collection_name: entry.collection_name,
                source_workspace_id: entry.source_workspace_id,
                source_workspace_revision: entry.source_workspace_revision,
                source_fact_id: entry.source_fact_id,
                field: entry.field,
                original_value: entry.original_value,
                correction: entry.has_correction.then_some(entry.correction),
                kind: wire::LibraryFactKind::from_wire(u32::from(entry.kind))?,
                sources: entry
                    .sources
                    .into_iter()
                    .map(|source| wire::LibrarySourceRecord {
                        source_id: source.source_id,
                        title: source.title,
                        host: source.host,
                        observed_at_epoch_ms: source.observed_at_epoch_ms,
                    })
                    .collect(),
                captured_at_epoch_ms: entry.captured_at_epoch_ms,
                last_checked_epoch_ms: entry.last_checked_epoch_ms,
                has_conflict: entry.has_conflict,
            })
        })
        .collect::<Option<Vec<_>>>();
    entries.is_some_and(|entries| {
        runtime
            .core_mut()
            .restore_library(revision, entries)
            .is_ok()
    })
}

fn restore_memory(
    runtime: &mut ProfileServiceRuntime,
    revision: u64,
    records: Vec<bootstrap_ffi::BridgeMemoryRecordRestore>,
) -> bool {
    let records = records
        .into_iter()
        .map(|record| {
            Some(wire::MemoryRecord {
                memory_id: record.memory_id,
                revision: record.revision,
                statement: record.statement,
                source_kind: wire::MemorySourceKind::from_wire(u32::from(record.source_kind))?,
                source_task_id: record.has_source_task_id.then_some(record.source_task_id),
                source_workspace: record.has_source_workspace.then(|| {
                    wire::MemoryWorkspaceRecord {
                        workspace_id: record.source_workspace.workspace_id,
                        display_name: record.source_workspace.display_name,
                    }
                }),
                scope_kind: wire::MemoryScopeKind::from_wire(u32::from(record.scope_kind))?,
                scope_workspace: record
                    .has_scope_workspace
                    .then(|| wire::MemoryWorkspaceRecord {
                        workspace_id: record.scope_workspace.workspace_id,
                        display_name: record.scope_workspace.display_name,
                    }),
                sensitivity: wire::MemorySensitivity::from_wire(u32::from(record.sensitivity))?,
                created_at_epoch_ms: record.created_at_epoch_ms,
                updated_at_epoch_ms: record.updated_at_epoch_ms,
                reviewed_at_epoch_ms: record.reviewed_at_epoch_ms,
                expires_at_epoch_ms: record.expires_at_epoch_ms,
            })
        })
        .collect::<Option<Vec<_>>>();
    records.is_some_and(|records| runtime.core_mut().restore_memory(revision, records).is_ok())
}

fn restore_workspaces(
    runtime: &mut ProfileServiceRuntime,
    workspaces: Vec<bootstrap_ffi::BridgeWorkspaceRestore>,
) -> bool {
    let records = workspaces
        .into_iter()
        .map(|workspace| {
            (
                workspace.workspace_id,
                workspace.revision,
                workspace.snapshot,
            )
        })
        .collect::<Vec<_>>();
    runtime
        .core_mut()
        .restore_workspace_records(&records)
        .is_ok()
}
