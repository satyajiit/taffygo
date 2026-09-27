// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! One record of every other projected family, as a fixture each.

use core_api_types::{
    ActionApprovalView, AssetDeliveryView, AssetKindView, AssetNetworkCostView, AssetPresenceView,
    AssetRefusal, AssetRefusalView, AssetViewState, AuthAccountView, AuthMethodAvailability,
    AuthMethodView, AuthPhase, AuthProvider, AuthViewState, EntitlementView, LibraryAvailability,
    LibraryEntryView, LibraryExportView, LibrarySearchHitView, LibrarySearchView,
    LibrarySourceView, LibraryViewState, MemoryAvailability, MemoryRecordView, MemoryScopeKind,
    MemorySearchHitView, MemorySearchView, MemorySensitivity, MemorySourceKind, MemoryViewState,
    MemoryWorkspaceView, SavedDataAvailability, SavedDetailView, SavedDetailsView, SavedSignInView,
    SavedSignInsView, TaskActivityKind, TaskActivityView, TaskArtifactKind, TaskArtifactView,
    TaskControlKind, TaskPhase, TaskTemplateId, TaskViewState, WorkspaceDeletionPreviewView,
    WorkspaceExportFormat, WorkspaceFactKind, WorkspaceFactView, WorkspacePhase,
    WorkspaceSourceView, WorkspaceViewState,
};

pub(super) fn saved_sign_ins_fixture() -> SavedSignInsView {
    SavedSignInsView {
        availability: SavedDataAvailability::Ready,
        revision: 5,
        records: vec![SavedSignInView {
            id: "sign-in-opaque-1".to_owned(),
            site: "accounts.example".to_owned(),
            username: "person@example.test".to_owned(),
            last_used_epoch_ms: 1_700_000_002_000,
        }],
    }
}

pub(super) fn saved_details_fixture() -> SavedDetailsView {
    SavedDetailsView {
        availability: SavedDataAvailability::Ready,
        revision: 3,
        people: vec![SavedDetailView {
            id: "detail-opaque-1".to_owned(),
            given_name: "Priya".to_owned(),
            family_name: "Rao".to_owned(),
            email: "person@example.test".to_owned(),
            phone: "+91 90000 00000".to_owned(),
            address: "42 Example Road".to_owned(),
            postcode: "560001".to_owned(),
            country: "IN".to_owned(),
        }],
    }
}

pub(super) fn task_fixture() -> TaskViewState {
    TaskViewState {
        task_id: "task-fixture-17".to_owned(),
        revision: 4,
        phase: TaskPhase::WaitingForUser,
        progress_basis_points: 3_750,
        status_message_key: Some("task.waiting_for_approval".to_owned()),
        failure: None,
        goal: "Compare the verified evidence".to_owned(),
        template_id: TaskTemplateId::CompareProducts,
        workspace_id: Some("workspace-fixture-2".to_owned()),
        pending_ask_prompt: None,
        pending_field_value_request: None,
        allowed_controls: vec![
            TaskControlKind::Pause,
            TaskControlKind::TakeOver,
            TaskControlKind::Stop,
        ],
        pending_action: Some(ActionApprovalView {
            action_id: "action-fixture-3".to_owned(),
            host: Some("shop.example.invalid".to_owned()),
            item_count: 2,
            summary_message_key: "action.compare_items".to_owned(),
        }),
        artifacts: vec![TaskArtifactView {
            artifact_id: "artifact-fixture-1".to_owned(),
            kind: TaskArtifactKind::Xlsx,
            workspace_revision: 4,
            accepted: true,
        }],
        activity: vec![
            TaskActivityView {
                sequence: 1,
                kind: TaskActivityKind::OpenedPage,
                host: Some("shop.example.invalid".to_owned()),
                count: 0,
                at_epoch_ms: 1_788_911_100_000,
            },
            TaskActivityView {
                sequence: 2,
                kind: TaskActivityKind::ReadPage,
                host: Some("shop.example.invalid".to_owned()),
                count: 0,
                at_epoch_ms: 1_788_911_160_000,
            },
            TaskActivityView {
                sequence: 3,
                kind: TaskActivityKind::PageUnavailable,
                host: Some("reviews.example.invalid".to_owned()),
                count: 0,
                at_epoch_ms: 1_788_911_220_000,
            },
            TaskActivityView {
                sequence: 4,
                kind: TaskActivityKind::AskedYou,
                host: None,
                count: 0,
                at_epoch_ms: 1_788_911_280_000,
            },
        ],
    }
}

pub(super) fn memory_fixture() -> MemoryViewState {
    MemoryViewState {
        availability: MemoryAvailability::Available,
        revision: 3,
        records: vec![MemoryRecordView {
            memory_id: "memory-1".to_owned(),
            revision: 2,
            statement: "Prefer short comparison tables".to_owned(),
            source_kind: MemorySourceKind::TaffySuggested,
            source_task_id: Some("task-41".to_owned()),
            source_workspace: Some(MemoryWorkspaceView {
                workspace_id: "workspace-123".to_owned(),
                display_name: "TV research".to_owned(),
            }),
            scope_kind: MemoryScopeKind::AllTasks,
            scope_workspace: None,
            sensitivity: MemorySensitivity::Standard,
            created_at_epoch_ms: 1_700_000_001_000,
            updated_at_epoch_ms: 1_700_000_002_000,
            reviewed_at_epoch_ms: 1_700_000_002_000,
            expires_at_epoch_ms: 0,
        }],
        search: Some(MemorySearchView {
            request_id: "memory-search-1".to_owned(),
            query: "comparison".to_owned(),
            memory_revision: 3,
            hits: vec![MemorySearchHitView {
                memory_id: "memory-1".to_owned(),
            }],
        }),
    }
}

pub(super) fn auth_fixture() -> AuthViewState {
    AuthViewState {
        phase: AuthPhase::SignedIn,
        account: Some(AuthAccountView {
            account_id: "account-fixture-1".to_owned(),
            display_name: Some("Taffy Tester".to_owned()),
            email: Some("tester@example.invalid".to_owned()),
            method: AuthProvider::Google,
        }),
        pending_email: None,
        failure: None,
        methods: [
            AuthProvider::Google,
            AuthProvider::EmailLink,
            AuthProvider::Github,
            AuthProvider::Facebook,
        ]
        .into_iter()
        .map(|provider| AuthMethodView {
            provider,
            availability: AuthMethodAvailability::Available,
        })
        .collect(),
        entitlement: Some(EntitlementView {
            plan_id: "plan-standard".to_owned(),
            credits_granted: 1_000,
            credits_remaining: 964,
            next_renewal_epoch_seconds: 1_788_912_000,
            valid_until_epoch_seconds: 0,
        }),
    }
}

pub(super) fn workspace_fixture() -> WorkspaceViewState {
    WorkspaceViewState {
        workspace_id: "workspace-fixture-2".to_owned(),
        revision: 7,
        goal: "Compare the verified evidence".to_owned(),
        phase: WorkspacePhase::PartlyDone,
        last_updated_epoch_ms: 1_787_337_000_000,
        template_id: TaskTemplateId::CompareProducts,
        sources: vec![WorkspaceSourceView {
            source_id: "source-fixture-1".to_owned(),
            title: "Independent comparison".to_owned(),
            host: "reviews.example.invalid".to_owned(),
            read_at_epoch_ms: 1_787_336_900_000,
            fact_count: 1,
            excluded: false,
        }],
        facts: vec![WorkspaceFactView {
            fact_id: "fact-fixture-1".to_owned(),
            field: "warranty".to_owned(),
            value: "two years".to_owned(),
            kind: WorkspaceFactKind::FromPage,
            sources: vec!["source-fixture-1".to_owned()],
            correction: None,
            has_conflict: false,
            needs_new_source: false,
        }],
        saved: true,
        display_name: "Verified evidence comparison".to_owned(),
        deletion_preview: Some(WorkspaceDeletionPreviewView {
            sources: 1,
            facts: 1,
            artifact_metadata: 0,
            derived_indexes: 0,
            confirmation_token: "a".repeat(64),
        }),
    }
}

pub(super) fn library_fixture() -> LibraryViewState {
    LibraryViewState {
        availability: LibraryAvailability::Available,
        revision: 4,
        entries: vec![LibraryEntryView {
            entry_id: "library-entry-1".to_owned(),
            revision: 2,
            collection_id: "workspace-123".to_owned(),
            collection_name: "TV research".to_owned(),
            source_workspace_id: "workspace-123".to_owned(),
            source_workspace_revision: 7,
            source_fact_id: "fact-warranty".to_owned(),
            field: "warranty".to_owned(),
            original_value: "one year".to_owned(),
            correction: Some("two years".to_owned()),
            kind: WorkspaceFactKind::Summarized,
            sources: vec![LibrarySourceView {
                source_id: "source-maker".to_owned(),
                title: "Manufacturer specifications".to_owned(),
                host: "maker.example".to_owned(),
                observed_at_epoch_ms: 1_700_000_000_000,
            }],
            captured_at_epoch_ms: 1_700_000_001_000,
            last_checked_epoch_ms: 1_700_000_000_000,
            has_conflict: false,
        }],
        search: Some(LibrarySearchView {
            request_id: "library-search-1".to_owned(),
            query: "warranty".to_owned(),
            library_revision: 4,
            hits: vec![LibrarySearchHitView {
                entry_id: "library-entry-1".to_owned(),
                age_ms: 1_000,
            }],
        }),
        refresh_previews: Vec::new(),
        refresh_results: Vec::new(),
    }
}

pub(super) fn library_export_fixture() -> LibraryExportView {
    LibraryExportView {
        request_id: "library-export-1".to_owned(),
        library_revision: 4,
        collection_id: Some("workspace-123".to_owned()),
        format: WorkspaceExportFormat::Markdown,
        content: "# TV research\n\n## warranty\n\ntwo years\n".to_owned(),
    }
}

pub(super) fn asset_fixture() -> AssetDeliveryView {
    AssetDeliveryView {
        platform_supported: true,
        network_cost: AssetNetworkCostView::Metered,
        metered_permitted: false,
        assets: vec![
            AssetViewState {
                asset_id: "python-stdlib".to_owned(),
                asset_revision: "3.14.1-1".to_owned(),
                kind: AssetKindView::PythonStdlib,
                presence: AssetPresenceView::Installed,
                written_bytes: 14_680_064,
                total_bytes: 14_680_064,
                attempts: 1,
                refusal: None,
                waiting_until_monotonic_ms: 0,
            },
            AssetViewState {
                asset_id: "python-packages".to_owned(),
                asset_revision: "2026-08-1".to_owned(),
                kind: AssetKindView::PythonPackages,
                presence: AssetPresenceView::Partial,
                written_bytes: 262_144,
                total_bytes: 1_048_576,
                attempts: 3,
                refusal: Some(AssetRefusal {
                    reason: AssetRefusalView::NetworkNotPermitted,
                    retryable: true,
                }),
                waiting_until_monotonic_ms: 90_000,
            },
        ],
    }
}
