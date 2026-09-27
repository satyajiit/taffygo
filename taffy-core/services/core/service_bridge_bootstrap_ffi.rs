// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Bootstrap CXX records kept outside the steady-state bridge surface.
//! The typed SkillRecord is also reused for isolated backup-stage admission;
//! that use restores no runtime and grants no execution authority.

use core_runtime::wire;

/// The one projection for stored and staged procedure rows. Unknown closed
/// values refuse the complete set; full metadata and definition admission is
/// still owned by the portable catalogue, not by this FFI representation.
pub(crate) fn skill_records_to_wire(
    records: Vec<ffi::BridgeSkillRecord>,
) -> Option<Vec<wire::SkillRecord>> {
    if records.len() > wire::MAX_SKILLS_PER_PROFILE {
        return None;
    }
    records
        .into_iter()
        .map(|record| {
            Some(wire::SkillRecord {
                skill_id: record.skill_id,
                origin: record.origin,
                provenance: wire::SkillProvenance::from_wire(u32::from(record.provenance))?,
                status: wire::SkillStatus::from_wire(u32::from(record.status))?,
                active_version: record.active_version,
                definition: record.definition,
                step_count: record.step_count,
                installed_at_utc_ms: record.installed_at_utc_ms,
                updated_at_utc_ms: record.updated_at_utc_ms,
            })
        })
        .collect()
}

#[allow(non_snake_case)]
#[cxx::bridge(namespace = "taffy::core_bridge")]
pub(crate) mod ffi {
    struct BridgeCommittedBatch {
        effect_id: String,
        expected_revision: u64,
        resulting_revision: u64,
        transaction_batch: Vec<u8>,
    }

    struct BridgeTaskRestore {
        task_id: String,
        task_id_seed: [u8; 32],
        batches: Vec<BridgeCommittedBatch>,
    }

    struct BridgeWorkspaceRestore {
        workspace_id: String,
        revision: u64,
        snapshot: Vec<u8>,
    }

    struct BridgeLibrarySourceRestore {
        source_id: String,
        title: String,
        host: String,
        observed_at_epoch_ms: u64,
    }

    struct BridgeLibraryEntryRestore {
        entry_id: String,
        revision: u64,
        collection_id: String,
        collection_name: String,
        source_workspace_id: String,
        source_workspace_revision: u64,
        source_fact_id: String,
        field: String,
        original_value: String,
        has_correction: bool,
        correction: String,
        kind: u8,
        sources: Vec<BridgeLibrarySourceRestore>,
        captured_at_epoch_ms: u64,
        last_checked_epoch_ms: u64,
        has_conflict: bool,
    }

    struct BridgeMemoryWorkspaceRestore {
        workspace_id: String,
        display_name: String,
    }

    struct BridgeMemoryRecordRestore {
        memory_id: String,
        revision: u64,
        statement: String,
        source_kind: u8,
        has_source_task_id: bool,
        source_task_id: String,
        has_source_workspace: bool,
        source_workspace: BridgeMemoryWorkspaceRestore,
        scope_kind: u8,
        has_scope_workspace: bool,
        scope_workspace: BridgeMemoryWorkspaceRestore,
        sensitivity: u8,
        created_at_epoch_ms: u64,
        updated_at_epoch_ms: u64,
        reviewed_at_epoch_ms: u64,
        expires_at_epoch_ms: u64,
    }

    /// One artifact the browser found in the profile's asset store.
    ///
    /// What is on disk and nothing more. What the artifact is for, how large
    /// it should be and how many attempts it has cost are the catalog's and
    /// the plane's answers, and the browser has neither.
    struct BridgeAssetOnDisk {
        asset_id: String,
        asset_revision: String,
        presence: u8,
        written_bytes: u64,
    }

    /// One immutable active-version row restored from the browser journal.
    struct BridgeSkillRecord {
        skill_id: String,
        origin: String,
        provenance: u8,
        status: u8,
        active_version: u32,
        definition: Vec<u8>,
        step_count: u32,
        installed_at_utc_ms: u64,
        updated_at_utc_ms: u64,
    }

    /// One content-free recent run, already ordered newest first by storage.
    struct BridgeSkillRunRecord {
        skill_id: String,
        version: u32,
        task_id: String,
        outcome: u8,
        ran_at_utc_ms: u64,
    }

    struct BridgeBootstrap {
        service_generation: u64,
        generation_capability_entropy: [u8; 32],
        initial_monotonic_millis: u64,
        initial_utc_millis: u64,
        private_profile: bool,
        browser_profile_id: String,
        browser_session_id: String,
        has_account_session: bool,
        session_handle: String,
        account_subject: String,
        account_expires_at_monotonic_ms: u64,
        account_rotation: u64,
        account_auth_method: u8,
        // Restored from the journal beside the subject, so a screen can say
        // who is signed in at the one moment it has nothing else to read:
        // the snapshot taken at bootstrap.
        has_account_email: bool,
        account_email: String,
        has_account_display_name: bool,
        account_display_name: String,
        available_account_methods: Vec<u8>,
        tasks: Vec<BridgeTaskRestore>,
        workspaces: Vec<BridgeWorkspaceRestore>,
        library_revision: u64,
        library_entries: Vec<BridgeLibraryEntryRestore>,
        memory_revision: u64,
        memory_records: Vec<BridgeMemoryRecordRestore>,
        asset_platform: u8,
        assets: Vec<BridgeAssetOnDisk>,
        skills: Vec<BridgeSkillRecord>,
        recall: Vec<BridgeSkillRunRecord>,
        has_assistant_configuration: bool,
        assistant_configuration_revision: u64,
        assistant_configuration_disabled_abilities: Vec<u8>,
        assistant_configuration_preset: u8,
        assistant_configuration_pace: u32,
        assistant_configuration_length: u32,
        assistant_configuration_check_in: u32,
    }
}
