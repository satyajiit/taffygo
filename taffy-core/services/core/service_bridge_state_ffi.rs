// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The records a published state and an admitted response are made of.
//!
//! `BridgeState`, `BridgeInitialization`, `BridgeResponse` and every record
//! they hold a `Vec` of live here. Two records they hold by value are named
//! from other bridges instead: the `BridgeOperation` envelope every effect
//! carries (`service_bridge_operation_ffi.rs`) and the task-effect binding
//! (`service_bridge_task_effect_ffi.rs`). cxx allows that because it emits
//! `ExternType<Kind = Trivial>` for every shared struct, and a trivial extern
//! alias may be a by-value field of another bridge's record or the element of
//! its `Vec<T>` — which is the same mechanism the root bridge has always used
//! to return these records by value.
//!
//! The one rule the split adds is where a `Vec<T>` of an aliased record is
//! instantiated: in exactly one bridge, or cxx emits the `VecElement` impl and
//! the C++ `rust::Vec<T>` symbols twice. `Vec<BridgeTaskEffect>` is named here
//! by `impl Vec<BridgeTaskEffect> {}`, and its defining bridge never names it.
//! Every effect record that a `BridgeResponse` holds a `Vec` of stays defined
//! in this module for that reason: defining it here instantiates the vector
//! implicitly, and no second bridge has to remember not to.
//!
//! Where the rest go: a record that is only ever a by-value field or element
//! of another bridge's record may live in its own module and be aliased in;
//! a record only one plane names, and no published state carries, goes in that
//! plane's own `service_bridge_<plane>_ffi.rs`. The generated headers must
//! form a DAG — operation, then task effect, then this module, then the planes
//! that wrap a `BridgeResponse`, then the root — so a module never includes
//! the header of a module that includes it.

#[allow(non_snake_case)]
#[cxx::bridge(namespace = "taffy::core_bridge")]
pub(crate) mod ffi {
    unsafe extern "C++" {
        include!("taffy/services/core/service_bridge_operation_ffi.rs.h");
        include!("taffy/services/core/service_bridge_task_effect_ffi.rs.h");

        type BridgeOperation = crate::service_bridge_operation_ffi::ffi::BridgeOperation;
        type BridgeTaskEffect = crate::service_bridge_task_effect_ffi::ffi::BridgeTaskEffect;
    }

    // The one place `Vec<BridgeTaskEffect>` is instantiated. The record's own
    // bridge holds no vector of it, so this explicit impl is what gives
    // `BridgeState::task_effects` its element implementation on both sides.
    impl Vec<BridgeTaskEffect> {}

    struct BridgeTaskSettlement {
        task_id: String,
        service_generation: u64,
        task_revision: u64,
        kind: u8,
    }

    #[repr(u8)]
    enum BridgeInitializationStatus {
        Ready = 0,
        RuntimeConfigurationRefused = 1,
        AccountRestoreRefused = 2,
        TaskRestoreRefused = 3,
        WorkspaceRestoreRefused = 4,
        LibraryRestoreRefused = 5,
        MemoryRestoreRefused = 6,
        AssetRestoreRefused = 7,
        StateProjectionRefused = 8,
        ReviewedWorkflowRestoreRefused = 9,
        SkillRunRestoreRefused = 10,
    }

    struct BridgeInitialization {
        status: BridgeInitializationStatus,
        accepted_generation: u64,
        storage_effects: Vec<BridgeStorageEffect>,
        asset_effects: Vec<BridgeAssetEffect>,
        states: Vec<BridgeState>,
    }

    struct BridgeState {
        service_generation: u64,
        sequence: u64,
        core_status_schema_version: u32,
        payload: Vec<u8>,
        model_artifacts: Vec<BridgeModelArtifactRegistration>,
        task_revisions: Vec<BridgeTaskRevision>,
        pending_approvals: Vec<BridgePendingApproval>,
        task_settlements: Vec<BridgeTaskSettlement>,
        pending_permissions: Vec<BridgePendingPermission>,
        terminal_tasks: Vec<BridgeTerminalTask>,
        accepted_task_consents: Vec<BridgeAcceptedTaskConsent>,
        committed_action_approvals: Vec<BridgeCommittedActionApproval>,
        task_effects: Vec<BridgeTaskEffect>,
    }

    /// One installed catalog row the browser may open as a local-model input.
    ///
    /// The descriptor stays browser-owned and no path crosses this seam. The
    /// browser reopens the exact profile artifact read-only, checks this
    /// length and digest, then gives only the opened handle plus these facts
    /// to the sandboxed tool worker.
    struct BridgeModelArtifactRegistration {
        asset_id: String,
        asset_revision: String,
        asset_kind: u8,
        format: u8,
        adapter: bool,
        byte_length: u64,
        digest: [u8; 32],
    }

    struct BridgeTaskRevision {
        task_id: String,
        service_generation: u64,
        task_revision: u64,
        allowed_controls: Vec<u8>,
    }

    struct BridgePendingApproval {
        task_id: String,
        action_id: String,
        proposal_digest: String,
        service_generation: u64,
        task_revision: u64,
    }

    struct BridgePendingPermission {
        task_id: String,
        request_id: String,
        permission: u8,
        service_generation: u64,
        task_revision: u64,
        deadline_monotonic_ms: u64,
        deadline_utc_ms: u64,
        browser_session_id: String,
    }

    struct BridgeTerminalTask {
        task_id: String,
        service_generation: u64,
        task_revision: u64,
        kind: u8,
    }

    struct BridgeConsentSource {
        source_id: String,
        tab_id: String,
        normalized_origin: String,
        has_canonical_locator: bool,
        canonical_locator: String,
    }

    struct BridgeAcceptedTaskConsent {
        task_id: String,
        service_generation: u64,
        current_task_revision: u64,
        accepted_revision: u64,
        browser_session_id: String,
        receipt_id: String,
        sources: Vec<BridgeConsentSource>,
        source_discovery_enabled: bool,
        new_source_cap: u32,
        provider_route: u8,
    }

    struct BridgeCommittedActionApproval {
        task_id: String,
        action_id: String,
        service_generation: u64,
        committed_revision: u64,
        receipt_id: String,
        proposal_digest: String,
        expires_at_monotonic_ms: u64,
        expires_at_utc_ms: u64,
        browser_session_id: String,
    }

    struct BridgeAdmission {
        operation_id: String,
        status: u8,
    }

    struct BridgeStorageEffect {
        operation: BridgeOperation,
        effect_id: String,
        operation_kind: u8,
        task_id: String,
        expected_revision: u64,
        resulting_revision: u64,
        transaction_batch: Vec<u8>,
        task_id_seed: [u8; 32],
        workspace_id: String,
        workspace_expected_revision: u64,
        workspace_resulting_revision: u64,
        workspace_snapshot: Vec<u8>,
        skill_id: String,
        skill_version: u32,
        skill_origin: String,
        skill_provenance: u8,
        skill_status: u8,
        skill_definition: Vec<u8>,
        skill_step_count: u32,
        skill_changed_at_utc_ms: u64,
        skill_task_id: String,
        skill_run_outcome: u8,
        skill_ran_at_utc_ms: u64,
        configuration_disabled_abilities: Vec<u8>,
        configuration_preset: u8,
        configuration_pace: u32,
        configuration_length: u32,
        configuration_check_in: u32,
    }

    struct BridgeWorkspaceEffect {
        operation: BridgeOperation,
        effect_id: String,
        operation_kind: u8,
        workspace_id: String,
        expected_revision: u64,
        resulting_revision: u64,
        snapshot: Vec<u8>,
        sources: u32,
        facts: u32,
        artifact_metadata: u32,
        derived_indexes: u32,
        confirmation_token: String,
        entry_id: String,
        expected_entry_revision: u64,
        resulting_entry_revision: u64,
        collection_id: String,
        collection_name: String,
        source_workspace_id: String,
        source_workspace_revision: u64,
        source_fact_id: String,
        field: String,
        original_value: String,
        has_correction: bool,
        correction: String,
        library_fact_kind: u8,
        library_sources: Vec<BridgeLibrarySource>,
        captured_at_epoch_ms: u64,
        last_checked_epoch_ms: u64,
        has_conflict: bool,
        removed_at_epoch_ms: u64,
        memory_id: String,
        memory_statement: String,
        memory_source_kind: u8,
        has_memory_source_task_id: bool,
        memory_source_task_id: String,
        has_memory_source_workspace: bool,
        memory_source_workspace_id: String,
        memory_source_workspace_name: String,
        memory_scope_kind: u8,
        has_memory_scope_workspace: bool,
        memory_scope_workspace_id: String,
        memory_scope_workspace_name: String,
        memory_sensitivity: u8,
        memory_created_at_epoch_ms: u64,
        memory_updated_at_epoch_ms: u64,
        memory_reviewed_at_epoch_ms: u64,
        memory_expires_at_epoch_ms: u64,
        memory_deleted_at_epoch_ms: u64,
    }

    struct BridgeLibrarySource {
        source_id: String,
        title: String,
        host: String,
        observed_at_epoch_ms: u64,
    }

    struct BridgeAccountEffect {
        operation: BridgeOperation,
        effect_id: String,
        kind: u8,
        retry_class: u8,
        operation_kind: u8,
        flow_id: String,
        auth_method: u8,
        scopes: Vec<u8>,
        byte_count: u32,
        purpose: u8,
        material: Vec<u8>,
        secret_handle: String,
        email: String,
        pkce_verifier_handle: String,
        redirect_binding_id: String,
        pkce_challenge: String,
        state: String,
        authorization_code_handle: String,
        credential_handle: String,
        raw_nonce_handle: String,
        hashed_nonce: String,
        session_handle: String,
        account_subject: String,
        expected_rotation: u64,
        max_response_bytes: u32,
    }

    /// One thing the browser should do about one artifact.
    struct BridgeAssetEffect {
        operation: BridgeOperation,
        effect_id: String,
        operation_kind: u8,
        asset_id: String,
        asset_revision: String,
        origin_path: String,
        offset_bytes: u64,
        total_bytes: u64,
        expected_digest: [u8; 32],
        container: u8,
    }

    /// One key-probe model effect, flat (decision 0083). Flat because cxx has
    /// no sum type; the browser rebuilds the typed `ModelRequestEffect` from
    /// exactly these fields, and this record is deliberately probe-only — the
    /// task workflow's model calls never travel this channel.
    struct BridgeProbeEffect {
        operation: BridgeOperation,
        effect_id: String,
        retry_class: u8,
        route_id: String,
        model_id: String,
        disclosure: u8,
        request_body: Vec<u8>,
        max_output_bytes: u32,
        provider_id: String,
        wire_api: u8,
        endpoint: String,
        credential_handle: String,
    }

    /// One fetch of a provider's own model list (decision 0098).
    ///
    /// Here rather than in the listing plane's own module because a response
    /// holds a `Vec` of it, which is the same reason `BridgeProbeEffect` is
    /// here: defining it beside `BridgeResponse` instantiates that vector
    /// once, implicitly, with no second bridge to keep from naming it too.
    ///
    /// No path and no URL. The core names the origin the person's credential
    /// reaches and the family that origin speaks; the browser composes the
    /// listing path from the family, exactly as it composes the catalog
    /// fetch's, so nothing this record carries can move the fetch to another
    /// host. `credential_handle` is an opaque secure-store reference and never
    /// material (decision 0049); the flag beside it is how an endpoint that
    /// needs no key stays different from one whose key is the empty string.
    struct BridgeListingEffect {
        operation: BridgeOperation,
        effect_id: String,
        provider_id: String,
        endpoint: String,
        wire_api: u8,
        has_credential_handle: bool,
        credential_handle: String,
        max_response_bytes: u32,
    }

    /// One probe of an address a person typed (decision 0096).
    ///
    /// Here rather than in the endpoint-probe plane's own module for the
    /// reason `BridgeProbeEffect` and `BridgeListingEffect` are here: a
    /// response holds a `Vec` of it, and the vector is instantiated where the
    /// record is defined.
    ///
    /// Field for field with `BridgeListingEffect`, and a separate record all
    /// the same. The two ask different questions of different addresses — a
    /// listing asks a provider the catalog already carries for a catalog
    /// document, and this asks an unsaved address what it is (decision 0096's
    /// consequences) — so the browser must be able to tell them apart by the
    /// record it was handed. One record for both would leave the kind of the
    /// envelope to be guessed at the seam, and a guess in that direction sends
    /// a person's own endpoint down the catalog fetch's path.
    ///
    /// No path and no URL beyond the base a person typed: the browser composes
    /// the listing operation beneath it, exactly as it does for a catalog
    /// fetch. `credential_handle` is an opaque secure-store reference and never
    /// material (decision 0049); the flag beside it is how an endpoint that
    /// needs no key stays different from one whose key is the empty string.
    struct BridgeEndpointProbeEffect {
        operation: BridgeOperation,
        effect_id: String,
        provider_id: String,
        endpoint: String,
        wire_api: u8,
        has_credential_handle: bool,
        credential_handle: String,
        max_response_bytes: u32,
    }

    /// One transient visible answer event. Text has already been sanitized by
    /// the provider-family decoder and never enters a journal or state.
    struct BridgeTaskAnswerEvent {
        task_id: String,
        call_id: String,
        sequence: u32,
        has_text: bool,
        text: String,
        terminal: bool,
        complete: bool,
    }

    struct BridgeResponse {
        admission: BridgeAdmission,
        storage_effects: Vec<BridgeStorageEffect>,
        workspace_effects: Vec<BridgeWorkspaceEffect>,
        account_effects: Vec<BridgeAccountEffect>,
        asset_effects: Vec<BridgeAssetEffect>,
        probe_effects: Vec<BridgeProbeEffect>,
        listing_effects: Vec<BridgeListingEffect>,
        endpoint_probe_effects: Vec<BridgeEndpointProbeEffect>,
        task_answer_events: Vec<BridgeTaskAnswerEvent>,
        states: Vec<BridgeState>,
    }
}
