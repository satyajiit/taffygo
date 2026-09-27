// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! CXX projection for the ordered profile runtime.
//!
//! This file is intentionally a typed adapter, not a second runtime or wire
//! codec. C++ validates Mojo, these records preserve the generated contract
//! fields, and every conversion below constructs `core_service_types` before
//! entering the canonical `core_runtime` APIs.
//!
//! **This bridge defines no record. It names them.** Every struct that used to
//! stand here lives in a `service_bridge_<plane>_ffi.rs` module and is named
//! back through the `unsafe extern "C++"` block below, so `crate::ffi::X`
//! still resolves for the whole crate and `taffy::core_bridge::X` is still the
//! one C++ name. What stays here is the seam itself: the aliases, and the
//! `extern "Rust"` block, which is the entry list the browser calls and the
//! list `tools/check_publication_totality.py` reads.
//!
//! Where a new record goes, and why the rule is shaped this way:
//!
//! - A record that a published `BridgeState`, a `BridgeInitialization` or a
//!   `BridgeResponse` holds a `Vec` of goes in `service_bridge_state_ffi.rs`.
//!   Defining it beside the vector instantiates the vector once; a `Vec<T>`
//!   of a record another bridge defines needs `impl Vec<T> {}` in exactly one
//!   bridge, and a rule that says "define it here" needs nobody to remember
//!   that.
//! - A record that is only ever a by-value field, element or argument of
//!   another bridge's record may live in its own module and be named back
//!   through a `type X = crate::<module>::ffi::X;` alias in an
//!   `unsafe extern "C++"` block, the way this file names everything. cxx
//!   emits `ExternType<Kind = Trivial>` for every shared struct, and a trivial
//!   extern alias is by-value material — `BridgeOperation`, `BridgeTaskEffect`
//!   and the two delivery records are placed this way.
//! - A record only one plane names, and no published state carries, goes in
//!   that plane's own module, which mirrors the operation envelope under its
//!   own name — `BridgeAssetOperation` and its
//!   peers — and converts it exactly once in the plane's projection.
//! - The `include!` graph must be a DAG: operation, then task effect, then
//!   state, then the planes that wrap a `BridgeResponse`, then this root. A
//!   module never includes the header of a module that includes it.
//! - Nothing new goes here. A struct added to this file is a struct every
//!   plane must then route around.

include!("service_bridge_modules.rs");

#[allow(non_snake_case)]
#[cxx::bridge(namespace = "taffy::core_bridge")]
mod ffi {
    unsafe extern "C++" {
        include!("taffy/services/core/service_bridge_cxx.h");
        include!("taffy/services/core/service_bridge_start_ffi.rs.h");
        include!("taffy/services/core/service_bridge_operation_ffi.rs.h");
        include!("taffy/services/core/service_bridge_task_effect_ffi.rs.h");
        include!("taffy/services/core/service_bridge_state_ffi.rs.h");
        include!("taffy/services/core/service_bridge_bootstrap_ffi.rs.h");
        include!("taffy/services/core/service_bridge_account_ffi.rs.h");
        include!("taffy/services/core/service_bridge_assets_ffi.rs.h");
        include!("taffy/services/core/service_bridge_backup_ffi.rs.h");
        include!("taffy/services/core/service_bridge_composer_ffi.rs.h");
        include!("taffy/services/core/service_bridge_configuration_ffi.rs.h");
        include!("taffy/services/core/service_bridge_entitlement_ffi.rs.h");
        include!("taffy/services/core/service_bridge_listing_ffi.rs.h");
        include!("taffy/services/core/service_bridge_model_stream_ffi.rs.h");
        include!("taffy/services/core/service_bridge_page_export_ffi.rs.h");
        include!("taffy/services/core/service_bridge_policy_ffi.rs.h");
        include!("taffy/services/core/service_bridge_probe_ffi.rs.h");
        include!("taffy/services/core/service_bridge_provider_ffi.rs.h");
        include!("taffy/services/core/service_bridge_saved_data_ffi.rs.h");
        include!("taffy/services/core/service_bridge_skill_match_ffi.rs.h");
        include!("taffy/services/core/service_bridge_skills_ffi.rs.h");
        include!("taffy/services/core/service_bridge_storage_completion_ffi.rs.h");
        include!("taffy/services/core/service_bridge_task_command_ffi.rs.h");
        include!("taffy/services/core/service_bridge_task_terminal_ffi.rs.h");
        include!("taffy/services/core/service_bridge_workspace_ffi.rs.h");

        // The shared vocabulary: the operation envelope, the published state
        // and everything a state or a response is made of.
        type BridgeOperation = crate::service_bridge_operation_ffi::ffi::BridgeOperation;
        type BridgeConsentSource = crate::service_bridge_state_ffi::ffi::BridgeConsentSource;
        type BridgeStartTask = crate::service_bridge_start_ffi::ffi::BridgeStartTask;
        type BridgeTaskSettlement = crate::service_bridge_state_ffi::ffi::BridgeTaskSettlement;
        type BridgeInitialization = crate::service_bridge_state_ffi::ffi::BridgeInitialization;
        type BridgeState = crate::service_bridge_state_ffi::ffi::BridgeState;
        type BridgeModelArtifactRegistration =
            crate::service_bridge_state_ffi::ffi::BridgeModelArtifactRegistration;
        type BridgeTaskRevision = crate::service_bridge_state_ffi::ffi::BridgeTaskRevision;
        type BridgePendingApproval = crate::service_bridge_state_ffi::ffi::BridgePendingApproval;
        type BridgePendingPermission =
            crate::service_bridge_state_ffi::ffi::BridgePendingPermission;
        type BridgeTerminalTask = crate::service_bridge_state_ffi::ffi::BridgeTerminalTask;
        type BridgeAcceptedTaskConsent =
            crate::service_bridge_state_ffi::ffi::BridgeAcceptedTaskConsent;
        type BridgeCommittedActionApproval =
            crate::service_bridge_state_ffi::ffi::BridgeCommittedActionApproval;
        type BridgeTaskEffect = crate::service_bridge_task_effect_ffi::ffi::BridgeTaskEffect;
        type BridgeAdmission = crate::service_bridge_state_ffi::ffi::BridgeAdmission;
        type BridgeStorageEffect = crate::service_bridge_state_ffi::ffi::BridgeStorageEffect;
        type BridgeWorkspaceEffect = crate::service_bridge_state_ffi::ffi::BridgeWorkspaceEffect;
        type BridgeLibrarySource = crate::service_bridge_state_ffi::ffi::BridgeLibrarySource;
        type BridgeAccountEffect = crate::service_bridge_state_ffi::ffi::BridgeAccountEffect;
        type BridgeAssetEffect = crate::service_bridge_state_ffi::ffi::BridgeAssetEffect;
        type BridgeProbeEffect = crate::service_bridge_state_ffi::ffi::BridgeProbeEffect;
        type BridgeListingEffect = crate::service_bridge_state_ffi::ffi::BridgeListingEffect;
        type BridgeEndpointProbeEffect =
            crate::service_bridge_state_ffi::ffi::BridgeEndpointProbeEffect;
        type BridgeTaskAnswerEvent = crate::service_bridge_state_ffi::ffi::BridgeTaskAnswerEvent;
        type BridgeModelStreamChunk =
            crate::service_bridge_model_stream_ffi::ffi::BridgeModelStreamChunk;
        type BridgeModelStreamAnswerEvent =
            crate::service_bridge_model_stream_ffi::ffi::BridgeModelStreamAnswerEvent;
        type BridgeModelStreamDelivery =
            crate::service_bridge_model_stream_ffi::ffi::BridgeModelStreamDelivery;
        type BridgeResponse = crate::service_bridge_state_ffi::ffi::BridgeResponse;
        type BridgeStorageCompletion =
            crate::service_bridge_storage_completion_ffi::ffi::BridgeStorageCompletion;
        type BridgeEntitlementDelivery =
            crate::service_bridge_entitlement_ffi::ffi::BridgeEntitlementDelivery;

        // Bootstrap.
        type BridgeAssetOnDisk = crate::service_bridge_bootstrap_ffi::ffi::BridgeAssetOnDisk;
        type BridgeBootstrap = crate::service_bridge_bootstrap_ffi::ffi::BridgeBootstrap;

        // One plane per block, each with its own mirrored operation record.
        type BridgeAccountCommand = crate::service_bridge_account_ffi::ffi::BridgeAccountCommand;
        type BridgeAccountCompletion =
            crate::service_bridge_account_ffi::ffi::BridgeAccountCompletion;
        type BridgeAssetOperation = crate::service_bridge_assets_ffi::ffi::BridgeAssetOperation;
        type BridgeAssetCommand = crate::service_bridge_assets_ffi::ffi::BridgeAssetCommand;
        type BridgeAssetReport = crate::service_bridge_assets_ffi::ffi::BridgeAssetReport;
        type BridgeComposerCancel = crate::service_bridge_composer_ffi::ffi::BridgeComposerCancel;
        type BridgeComposerCommand = crate::service_bridge_composer_ffi::ffi::BridgeComposerCommand;
        type BridgeComposerCompletion =
            crate::service_bridge_composer_ffi::ffi::BridgeComposerCompletion;
        type BridgeComposerSubmission =
            crate::service_bridge_composer_ffi::ffi::BridgeComposerSubmission;
        type BridgeComposerDelivery =
            crate::service_bridge_composer_ffi::ffi::BridgeComposerDelivery;
        type BridgeComposerTerminal =
            crate::service_bridge_composer_ffi::ffi::BridgeComposerTerminal;
        type BridgeConfigurationOperation =
            crate::service_bridge_configuration_ffi::ffi::BridgeConfigurationOperation;
        type BridgeAssistantConfigurationCommand =
            crate::service_bridge_configuration_ffi::ffi::BridgeAssistantConfigurationCommand;
        type BridgeEntitlementOperation =
            crate::service_bridge_entitlement_ffi::ffi::BridgeEntitlementOperation;
        type BridgeEntitlementFetchEffect =
            crate::service_bridge_entitlement_ffi::ffi::BridgeEntitlementFetchEffect;
        type BridgeEntitlementPlan =
            crate::service_bridge_entitlement_ffi::ffi::BridgeEntitlementPlan;
        type BridgeEntitlementSummary =
            crate::service_bridge_entitlement_ffi::ffi::BridgeEntitlementSummary;
        type BridgeEntitlementFetchResult =
            crate::service_bridge_entitlement_ffi::ffi::BridgeEntitlementFetchResult;
        type BridgeListingResult = crate::service_bridge_listing_ffi::ffi::BridgeListingResult;
        type BridgePageExportCommand =
            crate::service_bridge_page_export_ffi::ffi::BridgePageExportCommand;
        type BridgePageExportResult =
            crate::service_bridge_page_export_ffi::ffi::BridgePageExportResult;
        type BridgePolicyRequest = crate::service_bridge_policy_ffi::ffi::BridgePolicyRequest;
        type BridgePolicyResult = crate::service_bridge_policy_ffi::ffi::BridgePolicyResult;
        type BridgeProbeCompletion = crate::service_bridge_probe_ffi::ffi::BridgeProbeCompletion;
        type BridgeEndpointProbeResult =
            crate::service_bridge_provider_ffi::ffi::BridgeEndpointProbeResult;
        type BridgeProviderCommand = crate::service_bridge_provider_ffi::ffi::BridgeProviderCommand;
        type BridgeSavedDataCommand =
            crate::service_bridge_saved_data_ffi::ffi::BridgeSavedDataCommand;
        type BridgeSiteSkillMatchCommand =
            crate::service_bridge_skill_match_ffi::ffi::BridgeSiteSkillMatchCommand;
        type BridgeSiteSkillMatchResult =
            crate::service_bridge_skill_match_ffi::ffi::BridgeSiteSkillMatchResult;
        type BridgeSavedFlowQuery = crate::skill_ffi::BridgeSavedFlowQuery;
        type BridgeSavedFlowQueryResult = crate::skill_ffi::BridgeSavedFlowQueryResult;
        type BridgeSkillCommand = crate::service_bridge_skills_ffi::ffi::BridgeSkillCommand;
        type BridgeTaskCommand = crate::service_bridge_task_command_ffi::ffi::BridgeTaskCommand;
        type BridgeTaskTerminal = crate::service_bridge_task_terminal_ffi::ffi::BridgeTaskTerminal;
        type BridgeWorkspaceOperation =
            crate::service_bridge_workspace_ffi::ffi::BridgeWorkspaceOperation;
        type BridgeWorkspaceCommand =
            crate::service_bridge_workspace_ffi::ffi::BridgeWorkspaceCommand;
        type BridgeBackupRestorePlanRequest =
            crate::service_bridge_backup_ffi::ffi::BridgeBackupRestorePlanRequest;
        type BridgeBackupRestorePlanResult =
            crate::service_bridge_backup_ffi::ffi::BridgeBackupRestorePlanResult;
        type BridgeBackupRestorePlanConfirmationRequest =
            crate::service_bridge_backup_ffi::ffi::BridgeBackupRestorePlanConfirmationRequest;
        type BridgeBackupRestoreStageAuthorizationResult =
            crate::service_bridge_backup_ffi::ffi::BridgeBackupRestoreStageAuthorizationResult;
        type BridgeBackupRestoreStageVerificationRequest =
            crate::service_bridge_backup_ffi::ffi::BridgeBackupRestoreStageVerificationRequest;
        type BridgeBackupRestoreCommitAuthorizationResult =
            crate::service_bridge_backup_ffi::ffi::BridgeBackupRestoreCommitAuthorizationResult;
        type BridgeBackupRestoreCommitOutcomeReport =
            crate::service_bridge_backup_ffi::ffi::BridgeBackupRestoreCommitOutcomeReport;
        type BridgeBackupRestoreProtocolResult =
            crate::service_bridge_backup_ffi::ffi::BridgeBackupRestoreProtocolResult;
        type BridgeBackupRestoreResolutionRequest =
            crate::service_bridge_backup_ffi::ffi::BridgeBackupRestoreResolutionRequest;
        type BridgeBackupRestoreResolutionAuthorizationResult =
            crate::service_bridge_backup_ffi::ffi::BridgeBackupRestoreResolutionAuthorizationResult;
        type BridgeBackupRestoreResolutionOutcomeReport =
            crate::service_bridge_backup_ffi::ffi::BridgeBackupRestoreResolutionOutcomeReport;
        type BridgeBackupRestoreCancellationRequest =
            crate::service_bridge_backup_ffi::ffi::BridgeBackupRestoreCancellationRequest;
        type BridgeBackupRestoreRecoveryInspectionRequest =
            crate::service_bridge_backup_ffi::ffi::BridgeBackupRestoreRecoveryInspectionRequest;
        type BridgeBackupRestoreRecoveryInspectionResult =
            crate::service_bridge_backup_ffi::ffi::BridgeBackupRestoreRecoveryInspectionResult;
        type BridgeBackupRestoreRecoveryResolutionRequest =
            crate::service_bridge_backup_ffi::ffi::BridgeBackupRestoreRecoveryResolutionRequest;
        type BridgeBackupRestoreRecoveryResolutionAuthorizationResult =
            crate::service_bridge_backup_ffi::ffi::BridgeBackupRestoreRecoveryResolutionAuthorizationResult;
        type BridgeBackupRestoreRecoveryResolutionOutcomeReport =
            crate::service_bridge_backup_ffi::ffi::BridgeBackupRestoreRecoveryResolutionOutcomeReport;

        fn ChromiumSha256(input: &[u8]) -> Vec<u8>;
    }

    extern "Rust" {
        type ServiceBridge;

        fn CreateServiceBridge(bootstrap: BridgeBootstrap) -> Box<ServiceBridge>;
        fn Initialization(bridge: &mut ServiceBridge) -> BridgeInitialization;
        fn LastRefusal(bridge: &ServiceBridge) -> String;
        fn SubmitStartTask(
            bridge: &mut ServiceBridge,
            command: BridgeStartTask,
            now_monotonic_ms: u64,
            now_utc_millis: u64,
        ) -> BridgeResponse;
        fn SubmitTask(
            bridge: &mut ServiceBridge,
            command: BridgeTaskCommand,
            now_monotonic_ms: u64,
            now_utc_millis: u64,
        ) -> BridgeResponse;
        fn CompleteTaskSettlement(
            bridge: &mut ServiceBridge,
            settlement: BridgeTaskSettlement,
            now_monotonic_ms: u64,
            now_utc_millis: u64,
        ) -> BridgeResponse;
        fn CompleteTaskEffect(
            bridge: &mut ServiceBridge,
            terminal: BridgeTaskTerminal,
            now_monotonic_ms: u64,
        ) -> BridgeResponse;
        fn DeliverModelStreamChunk(
            bridge: &mut ServiceBridge,
            chunk: BridgeModelStreamChunk,
        ) -> BridgeModelStreamDelivery;
        fn SubmitAccount(
            bridge: &mut ServiceBridge,
            command: BridgeAccountCommand,
            now_monotonic_ms: u64,
            now_utc_millis: u64,
        ) -> BridgeResponse;
        fn SubmitWorkspace(
            bridge: &mut ServiceBridge,
            command: BridgeWorkspaceCommand,
            now_monotonic_ms: u64,
            now_utc_millis: u64,
        ) -> BridgeResponse;
        fn SubmitSavedData(
            bridge: &mut ServiceBridge,
            command: BridgeSavedDataCommand,
            now_monotonic_ms: u64,
        ) -> BridgeResponse;
        fn SubmitAssistantConfiguration(
            bridge: &mut ServiceBridge,
            command: BridgeAssistantConfigurationCommand,
            now_monotonic_ms: u64,
        ) -> BridgeResponse;
        fn SubmitSkillMutation(
            bridge: &mut ServiceBridge,
            command: BridgeSkillCommand,
            now_monotonic_ms: u64,
        ) -> BridgeResponse;
        fn SubmitAsset(
            bridge: &mut ServiceBridge,
            command: BridgeAssetCommand,
            now_monotonic_ms: u64,
            now_utc_millis: u64,
        ) -> BridgeResponse;
        fn SubmitProvider(
            bridge: &mut ServiceBridge,
            command: BridgeProviderCommand,
            now_monotonic_ms: u64,
            now_utc_millis: u64,
        ) -> BridgeResponse;
        fn SubmitComposer(
            bridge: &mut ServiceBridge,
            command: BridgeComposerCommand,
            now_monotonic_ms: u64,
            now_utc_millis: u64,
        ) -> BridgeComposerSubmission;
        fn DeliverComposerCompletion(
            bridge: &mut ServiceBridge,
            completion: BridgeComposerCompletion,
            now_utc_millis: u64,
        ) -> BridgeComposerDelivery;
        fn CancelComposer(
            bridge: &mut ServiceBridge,
            command: BridgeComposerCancel,
            now_monotonic_ms: u64,
            now_utc_millis: u64,
        ) -> BridgeComposerSubmission;
        fn RecordComposerDelivery(bridge: &mut ServiceBridge, terminal: BridgeComposerTerminal);
        fn DeliverAssetReport(
            bridge: &mut ServiceBridge,
            report: BridgeAssetReport,
            now_monotonic_ms: u64,
            now_utc_millis: u64,
        ) -> BridgeResponse;
        fn PlanEntitlementRefresh(
            bridge: &mut ServiceBridge,
            reason: u8,
            now_utc_millis: u64,
        ) -> BridgeEntitlementPlan;
        fn DeliverEntitlementFetchResult(
            bridge: &mut ServiceBridge,
            result: BridgeEntitlementFetchResult,
            now_utc_millis: u64,
        ) -> BridgeEntitlementDelivery;
        fn DeliverProviderListingResult(
            bridge: &mut ServiceBridge,
            result: BridgeListingResult,
            now_utc_millis: u64,
        ) -> BridgeResponse;
        fn DeliverProbeCompletion(
            bridge: &mut ServiceBridge,
            completion: BridgeProbeCompletion,
            now_monotonic_ms: u64,
            now_utc_millis: u64,
        ) -> BridgeResponse;
        fn ExportPageSnapshot(
            bridge: &mut ServiceBridge,
            command: BridgePageExportCommand,
            graph_payload: &[u8],
            now_monotonic_ms: u64,
        ) -> BridgePageExportResult;
        fn CancelPageSnapshotExport(
            bridge: &mut ServiceBridge,
            operation_id: String,
            idempotency_key: String,
        ) -> bool;
        fn QuerySavedFlows(
            bridge: &mut ServiceBridge,
            command: BridgeSavedFlowQuery,
            now_monotonic_ms: u64,
        ) -> BridgeSavedFlowQueryResult;
        fn MatchSiteSkills(
            bridge: &mut ServiceBridge,
            command: BridgeSiteSkillMatchCommand,
            now_monotonic_ms: u64,
        ) -> BridgeSiteSkillMatchResult;
        fn PlanBackupRestore(
            bridge: &mut ServiceBridge,
            request: BridgeBackupRestorePlanRequest,
            manifest_plaintext: &[u8],
            now_monotonic_ms: u64,
        ) -> BridgeBackupRestorePlanResult;
        fn ConfirmBackupRestorePlan(
            bridge: &mut ServiceBridge,
            request: BridgeBackupRestorePlanConfirmationRequest,
            now_monotonic_ms: u64,
        ) -> BridgeBackupRestoreStageAuthorizationResult;
        fn ReportBackupRestoreStageVerified(
            bridge: &mut ServiceBridge,
            request: BridgeBackupRestoreStageVerificationRequest,
            now_monotonic_ms: u64,
        ) -> BridgeBackupRestoreCommitAuthorizationResult;
        fn ReportBackupRestoreCommitOutcome(
            bridge: &mut ServiceBridge,
            report: BridgeBackupRestoreCommitOutcomeReport,
            now_monotonic_ms: u64,
        ) -> BridgeBackupRestoreProtocolResult;
        fn ChooseBackupRestoreResolution(
            bridge: &mut ServiceBridge,
            request: BridgeBackupRestoreResolutionRequest,
            now_monotonic_ms: u64,
        ) -> BridgeBackupRestoreResolutionAuthorizationResult;
        fn ReportBackupRestoreResolutionOutcome(
            bridge: &mut ServiceBridge,
            report: BridgeBackupRestoreResolutionOutcomeReport,
            now_monotonic_ms: u64,
        ) -> BridgeBackupRestoreProtocolResult;
        fn CancelBackupRestoreBeforeCommit(
            bridge: &mut ServiceBridge,
            request: BridgeBackupRestoreCancellationRequest,
            now_monotonic_ms: u64,
        ) -> BridgeBackupRestoreProtocolResult;
        fn InspectBackupRestoreRecovery(
            bridge: &ServiceBridge,
            request: BridgeBackupRestoreRecoveryInspectionRequest,
            now_monotonic_ms: u64,
        ) -> BridgeBackupRestoreRecoveryInspectionResult;
        fn ChooseBackupRestoreRecoveryResolution(
            bridge: &mut ServiceBridge,
            request: BridgeBackupRestoreRecoveryResolutionRequest,
            now_monotonic_ms: u64,
        ) -> BridgeBackupRestoreRecoveryResolutionAuthorizationResult;
        fn ReportBackupRestoreRecoveryResolutionOutcome(
            bridge: &mut ServiceBridge,
            report: BridgeBackupRestoreRecoveryResolutionOutcomeReport,
            now_monotonic_ms: u64,
        ) -> BridgeBackupRestoreProtocolResult;
        fn DeliverEndpointProbeResult(
            bridge: &mut ServiceBridge,
            result: BridgeEndpointProbeResult,
            now_monotonic_ms: u64,
            now_utc_millis: u64,
        ) -> BridgeResponse;
        fn DeliverStorageCompletion(
            bridge: &mut ServiceBridge,
            completion: BridgeStorageCompletion,
            now_monotonic_ms: u64,
            now_utc_millis: u64,
        ) -> BridgeResponse;
        fn DeliverAccountCompletion(
            bridge: &mut ServiceBridge,
            completion: BridgeAccountCompletion,
            now_monotonic_ms: u64,
            now_utc_millis: u64,
        ) -> BridgeResponse;
        fn CancelOperation(
            bridge: &mut ServiceBridge,
            operation: BridgeOperation,
        ) -> BridgeResponse;
        fn ExpireDueOperations(bridge: &mut ServiceBridge, now_monotonic_ms: u64)
            -> BridgeResponse;
        fn NextOperationDeadline(bridge: &ServiceBridge) -> u64;
        fn EvaluatePolicy(
            bridge: &mut ServiceBridge,
            request: BridgePolicyRequest,
        ) -> BridgePolicyResult;
        fn PrepareForShutdown(bridge: &mut ServiceBridge);
    }
}
