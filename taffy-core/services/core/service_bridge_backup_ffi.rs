// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Backup CXX records. Paths and recovery keys have no representation here.
//! Only bounded selected SkillRecords cross transiently for full portable
//! admission before commit authority; other record payloads stay native.

pub(crate) mod implementation;

use implementation::{InspectBackupManifest, PrepareBackupManifest};

#[allow(non_snake_case)]
#[cxx::bridge(namespace = "taffy::core_bridge")]
pub(crate) mod ffi {
    unsafe extern "C++" {
        include!("taffy/services/core/service_bridge_bootstrap_ffi.rs.h");
        type BridgeSkillRecord = crate::service_bridge_bootstrap_ffi::ffi::BridgeSkillRecord;
    }

    // Vec<BridgeSkillRecord> is instantiated by its defining bootstrap bridge
    // alone. Do not add a second impl here.
    struct BridgeBackupOperation {
        operation_id: String,
        service_generation: u64,
        task_revision: u64,
        deadline_monotonic_ms: u64,
        idempotency_key: String,
    }

    struct BridgeBackupRestoreBinding {
        planning_operation: BridgeBackupOperation,
        owner_profile_id: String,
        target_kind: u8,
        target_profile_id: String,
        backup_id: String,
        snapshot_sha256: [u8; 32],
        confirmation_sha256: [u8; 32],
    }

    struct BridgeBackupRestoreStageAuthorization {
        binding: BridgeBackupRestoreBinding,
        decision_operation: BridgeBackupOperation,
    }

    struct BridgeBackupRestoreCommitAuthorization {
        binding: BridgeBackupRestoreBinding,
        decision_operation: BridgeBackupOperation,
    }

    struct BridgeBackupRestoreResolutionAuthorization {
        binding: BridgeBackupRestoreBinding,
        decision_operation: BridgeBackupOperation,
        choice: u8,
    }

    struct BridgeBackupRecordDescriptor {
        kind: u8,
        stable_id: String,
        revision: u64,
        schema_version: u32,
        state: u8,
        plaintext_bytes: u64,
        plaintext_sha256: [u8; 32],
    }

    struct BridgeBackupManifestPrepareRequest {
        operation: BridgeBackupOperation,
        backup_id: String,
        source_installation_id: String,
        created_at_utc: String,
        selection: Vec<u8>,
        records: Vec<BridgeBackupRecordDescriptor>,
    }

    struct BridgeBackupManifestPrepareResult {
        operation: BridgeBackupOperation,
        status: u8,
        manifest_plaintext: Vec<u8>,
        snapshot_sha256: [u8; 32],
        payload_plaintext_bytes: u64,
        source_order: Vec<u32>,
        expected_sealed_chunks: u32,
    }

    struct BridgeBackupPayloadLayoutEntry {
        state: u8,
        plaintext_bytes: u64,
    }

    struct BridgeBackupManifestInspectResult {
        operation: BridgeBackupOperation,
        status: u8,
        backup_id: String,
        source_installation_id: String,
        created_at_utc: String,
        selection: Vec<u8>,
        record_count: u32,
        snapshot_sha256: [u8; 32],
        payload_plaintext_bytes: u64,
        records: Vec<BridgeBackupPayloadLayoutEntry>,
    }

    struct BridgeStagedBackupRecord {
        plaintext_bytes: u64,
        plaintext_sha256: [u8; 32],
    }

    struct BridgeBackupRestorePlanRequest {
        operation: BridgeBackupOperation,
        staged_records: Vec<BridgeStagedBackupRecord>,
        current_records: Vec<BridgeBackupRecordDescriptor>,
        target_kind: u8,
        target_profile_id: String,
    }

    struct BridgeBackupRestorePlanEntry {
        kind: u8,
        stable_id: String,
        archive_revision: u64,
        action: u8,
        schema_version: u32,
        state: u8,
        plaintext_bytes: u64,
        plaintext_sha256: [u8; 32],
    }

    struct BridgeBackupRestorePlanResult {
        operation: BridgeBackupOperation,
        status: u8,
        backup_id: String,
        snapshot_sha256: [u8; 32],
        target_kind: u8,
        target_profile_id: String,
        entries: Vec<BridgeBackupRestorePlanEntry>,
        has_conflicts: bool,
        confirmation_sha256: [u8; 32],
        has_binding: bool,
        binding: BridgeBackupRestoreBinding,
    }

    struct BridgeBackupRestorePlanConfirmationRequest {
        operation: BridgeBackupOperation,
        binding: BridgeBackupRestoreBinding,
        confirmed_sha256: [u8; 32],
    }

    struct BridgeBackupRestoreStageAuthorizationResult {
        operation: BridgeBackupOperation,
        status: u8,
        has_authorization: bool,
        authorization: BridgeBackupRestoreStageAuthorization,
    }

    struct BridgeBackupRestoreStageVerificationRequest {
        operation: BridgeBackupOperation,
        authorization: BridgeBackupRestoreStageAuthorization,
        staged_snapshot_sha256: [u8; 32],
        skills: Vec<BridgeSkillRecord>,
    }

    struct BridgeBackupRestoreCommitAuthorizationResult {
        operation: BridgeBackupOperation,
        status: u8,
        has_authorization: bool,
        authorization: BridgeBackupRestoreCommitAuthorization,
    }

    struct BridgeBackupRestoreCommitOutcomeReport {
        operation: BridgeBackupOperation,
        authorization: BridgeBackupRestoreCommitAuthorization,
        outcome: u8,
    }

    struct BridgeBackupRestoreResolutionRequest {
        operation: BridgeBackupOperation,
        binding: BridgeBackupRestoreBinding,
        choice: u8,
    }

    struct BridgeBackupRestoreResolutionAuthorizationResult {
        operation: BridgeBackupOperation,
        status: u8,
        has_authorization: bool,
        authorization: BridgeBackupRestoreResolutionAuthorization,
    }

    struct BridgeBackupRestoreResolutionOutcomeReport {
        operation: BridgeBackupOperation,
        authorization: BridgeBackupRestoreResolutionAuthorization,
        outcome: u8,
    }

    struct BridgeBackupRestoreCancellationRequest {
        operation: BridgeBackupOperation,
        binding: BridgeBackupRestoreBinding,
    }

    struct BridgeBackupRestoreProtocolResult {
        operation: BridgeBackupOperation,
        status: u8,
    }

    struct BridgeBackupRestoreRecoveryBinding {
        reservation_id: String,
        owner_profile_id: String,
        target_kind: u8,
        target_profile_id: String,
        backup_id: String,
        snapshot_sha256: [u8; 32],
        confirmation_sha256: [u8; 32],
        selection: Vec<u8>,
        record_count: u64,
        candidate_records_sha256: [u8; 32],
    }

    struct BridgeBackupRestoreRecoveryIntentFact {
        intent_id: String,
        intent: u8,
    }

    struct BridgeBackupRestoreRecoveryOutcomeFact {
        intent_id: String,
        outcome: u8,
    }

    struct BridgeBackupRestoreRecoveryRecord {
        format_version: u32,
        sequence: u64,
        binding: BridgeBackupRestoreRecoveryBinding,
        fact_kind: u8,
        has_intent: bool,
        intent: BridgeBackupRestoreRecoveryIntentFact,
        has_outcome: bool,
        outcome: BridgeBackupRestoreRecoveryOutcomeFact,
    }

    struct BridgeBackupRestoreRecoveryInspectionRequest {
        operation: BridgeBackupOperation,
        records: Vec<BridgeBackupRestoreRecoveryRecord>,
    }

    struct BridgeBackupRestoreRecoveryReconciliation {
        intent_id: String,
        intent: u8,
    }

    struct BridgeBackupRestoreRecoveryClassification {
        kind: u8,
        has_reconciliation: bool,
        reconciliation: BridgeBackupRestoreRecoveryReconciliation,
    }

    struct BridgeBackupRestoreRecoveryFailure {
        error: u8,
    }

    struct BridgeBackupRestoreRecoveryInspectionResult {
        operation: BridgeBackupOperation,
        status: u8,
        has_classification: bool,
        classification: BridgeBackupRestoreRecoveryClassification,
        has_failure: bool,
        failure: BridgeBackupRestoreRecoveryFailure,
    }

    struct BridgeBackupRestoreRecoveryResolutionRequest {
        operation: BridgeBackupOperation,
        history_prefix: Vec<BridgeBackupRestoreRecoveryRecord>,
        choice: u8,
        intent_id: String,
    }

    struct BridgeBackupRestoreRecoveryResolutionAuthorization {
        binding: BridgeBackupRestoreRecoveryBinding,
        decision_operation: BridgeBackupOperation,
        choice: u8,
        intent_id: String,
        history_prefix: Vec<BridgeBackupRestoreRecoveryRecord>,
    }

    struct BridgeBackupRestoreRecoveryResolutionAuthorizationResult {
        operation: BridgeBackupOperation,
        status: u8,
        has_authorization: bool,
        authorization: BridgeBackupRestoreRecoveryResolutionAuthorization,
    }

    struct BridgeBackupRestoreRecoveryResolutionOutcomeReport {
        operation: BridgeBackupOperation,
        authorization: BridgeBackupRestoreRecoveryResolutionAuthorization,
        durable_history: Vec<BridgeBackupRestoreRecoveryRecord>,
    }

    extern "Rust" {
        fn PrepareBackupManifest(
            request: BridgeBackupManifestPrepareRequest,
            expected_generation: u64,
            now_monotonic_ms: u64,
        ) -> BridgeBackupManifestPrepareResult;
        fn InspectBackupManifest(
            operation: BridgeBackupOperation,
            manifest_plaintext: &[u8],
            expected_generation: u64,
            now_monotonic_ms: u64,
        ) -> BridgeBackupManifestInspectResult;
    }
}
