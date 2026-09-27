// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Crate assembly for the Core Service bridge. This file is included at the
// crate root. It owns the module inventory and binds each plane's
// implementation entry into the root CXX interface; the interface itself
// stays in `service_bridge.rs`.

mod service_bridge_account;
mod service_bridge_account_ffi;
mod service_bridge_assets;
mod service_bridge_assets_ffi;
mod service_bridge_backup;
mod service_bridge_backup_ffi;
mod service_bridge_bootstrap_ffi;
mod service_bridge_composer;
mod service_bridge_composer_ffi;
mod service_bridge_configuration;
mod service_bridge_configuration_ffi;
mod service_bridge_deadline;
mod service_bridge_endpoint_probe;
mod service_bridge_entitlement;
mod service_bridge_entitlement_ffi;
mod service_bridge_listing;
mod service_bridge_listing_ffi;
mod service_bridge_model_stream_ffi;
mod service_bridge_operation_ffi;
mod service_bridge_page_export;
mod service_bridge_page_export_ffi;
mod service_bridge_policy;
mod service_bridge_policy_ffi;
mod service_bridge_probe;
mod service_bridge_probe_ffi;
mod service_bridge_provider;
mod service_bridge_provider_ffi;
mod service_bridge_runtime;
mod service_bridge_saved_data;
mod service_bridge_saved_data_ffi;
mod service_bridge_saved_flows;
mod service_bridge_skill_match;
mod service_bridge_skill_match_ffi;
mod service_bridge_skills;
mod service_bridge_skills_ffi;
mod service_bridge_start;
mod service_bridge_start_ffi;
mod service_bridge_state_ffi;
mod service_bridge_status;
mod service_bridge_storage_completion_ffi;
mod service_bridge_task;
mod service_bridge_task_command;
mod service_bridge_task_command_ffi;
mod service_bridge_task_completion;
mod service_bridge_task_effect;
mod service_bridge_task_effect_ffi;
mod service_bridge_task_submit;
mod service_bridge_task_support;
mod service_bridge_task_terminal_ffi;
mod service_bridge_trace;
mod service_bridge_trace_ffi;
mod service_bridge_workflow;
mod service_bridge_workspace;
mod service_bridge_workspace_ffi;

use service_bridge_account::{DeliverAccountCompletion, SubmitAccount};
use service_bridge_assets::{DeliverAssetReport, SubmitAsset};
use service_bridge_backup::{
    CancelBackupRestoreBeforeCommit, ChooseBackupRestoreRecoveryResolution,
    ChooseBackupRestoreResolution, ConfirmBackupRestorePlan, InspectBackupRestoreRecovery,
    PlanBackupRestore, ReportBackupRestoreCommitOutcome,
    ReportBackupRestoreRecoveryResolutionOutcome, ReportBackupRestoreResolutionOutcome,
    ReportBackupRestoreStageVerified,
};
use service_bridge_composer::{
    CancelComposer, DeliverComposerCompletion, RecordComposerDelivery, SubmitComposer,
};
use service_bridge_configuration::SubmitAssistantConfiguration;
use service_bridge_deadline::{CancelOperation, ExpireDueOperations, NextOperationDeadline};
use service_bridge_endpoint_probe::DeliverEndpointProbeResult;
use service_bridge_entitlement::{DeliverEntitlementFetchResult, PlanEntitlementRefresh};
use service_bridge_listing::DeliverProviderListingResult;
use service_bridge_page_export::{CancelPageSnapshotExport, ExportPageSnapshot};
use service_bridge_policy::EvaluatePolicy;
use service_bridge_probe::DeliverProbeCompletion;
use service_bridge_provider::SubmitProvider;
use service_bridge_runtime::{
    CreateServiceBridge, DeliverStorageCompletion, Initialization, LastRefusal, PrepareForShutdown,
    ServiceBridge,
};
use service_bridge_saved_data::SubmitSavedData;
use service_bridge_saved_flows::QuerySavedFlows;
use service_bridge_skill_match::MatchSiteSkills;
use service_bridge_skills::SubmitSkillMutation;
use service_bridge_start::SubmitStartTask;
use service_bridge_task::{CompleteTaskSettlement, SubmitTask};
use service_bridge_task_effect::{CompleteTaskEffect, DeliverModelStreamChunk};
use service_bridge_workspace::SubmitWorkspace;

use service_bridge_skills_ffi::ffi as skill_ffi;
