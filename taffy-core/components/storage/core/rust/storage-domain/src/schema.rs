// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The schema, one migration at a time.
//!
//! The tables are the domain model's aggregates, rendered as columns. Nothing
//! here mirrors a Chromium profile store: cookies, saved credentials, browsing
//! history, downloads, and site storage stay exclusively Chromium's, and this
//! database holds opaque references to browser objects rather than copies of
//! them. A test walks the finished schema and asserts that no table or column
//! has drifted into duplicating one.
//!
//! Foreign keys are declared for documentation and for any host that turns
//! enforcement on. Deletion does not lean on cascade: it is an explicit,
//! ordered, verified operation, because a cascade that half fired is
//! indistinguishable from one that never ran.
//!
//! Aggregates that belong to later milestones are absent rather than sketched.
//! See [`crate::deferred`] for the list and the reason for each.

/// Statements of migration 1.
pub const M0001_WORKSPACE_AND_SOURCE: &[&str] = &[
    "CREATE TABLE workspace (
        workspace_id         TEXT PRIMARY KEY NOT NULL,
        browser_profile_id   TEXT NOT NULL,
        owner_account_id     TEXT,
        title                TEXT NOT NULL,
        status               TEXT NOT NULL,
        revision             INTEGER NOT NULL,
        created_at_utc       TEXT NOT NULL,
        updated_at_utc       TEXT NOT NULL,
        retention_class      TEXT NOT NULL,
        sensitivity          TEXT NOT NULL,
        deletion_state       TEXT NOT NULL,
        accepted_artifact_id TEXT,
        schema_version       INTEGER NOT NULL
    )",
    "CREATE INDEX workspace_by_profile ON workspace (browser_profile_id, status)",
    "CREATE TABLE source (
        source_id            TEXT PRIMARY KEY NOT NULL,
        kind                 TEXT NOT NULL,
        canonical_locator    TEXT,
        display_locator      TEXT NOT NULL,
        origin               TEXT,
        title                TEXT,
        first_seen_at_utc    TEXT NOT NULL,
        last_observed_at_utc TEXT,
        ownership            TEXT NOT NULL,
        sensitivity          TEXT NOT NULL,
        retention_class      TEXT NOT NULL,
        deletion_state       TEXT NOT NULL,
        schema_version       INTEGER NOT NULL
    )",
    "CREATE TABLE workspace_source (
        workspace_id     TEXT NOT NULL REFERENCES workspace (workspace_id),
        source_id        TEXT NOT NULL REFERENCES source (source_id),
        membership_state TEXT NOT NULL,
        added_by         TEXT NOT NULL,
        added_at_utc     TEXT NOT NULL,
        scope_receipt    TEXT,
        excluded_at_utc  TEXT,
        removal_reason   TEXT,
        PRIMARY KEY (workspace_id, source_id)
    )",
    "CREATE INDEX workspace_source_by_source ON workspace_source (source_id)",
];

/// Statements of migration 2.
pub const M0002_OBSERVATION_AND_PROVENANCE: &[&str] = &[
    "CREATE TABLE observation (
        observation_id        TEXT PRIMARY KEY NOT NULL,
        source_id             TEXT NOT NULL REFERENCES source (source_id),
        task_id               TEXT,
        captured_at_utc       TEXT NOT NULL,
        bip_schema_version    TEXT,
        page_epoch            TEXT,
        graph_revision        TEXT,
        content_fingerprint   TEXT,
        scope                 TEXT NOT NULL,
        truncation            TEXT NOT NULL,
        redaction_summary     TEXT NOT NULL,
        provenance_root       TEXT NOT NULL,
        retention_class       TEXT NOT NULL,
        encrypted_payload_ref TEXT,
        schema_version        INTEGER NOT NULL
    )",
    "CREATE INDEX observation_by_source ON observation (source_id, captured_at_utc)",
    "CREATE TABLE provenance_locator (
        provenance_id          TEXT PRIMARY KEY NOT NULL,
        fact_id                TEXT NOT NULL,
        source_id              TEXT NOT NULL REFERENCES source (source_id),
        observation_id         TEXT REFERENCES observation (observation_id),
        source_kind            TEXT NOT NULL,
        location_descriptor    TEXT,
        extraction_rule_version TEXT,
        transformation_chain   TEXT NOT NULL,
        captured_at_utc        TEXT NOT NULL
    )",
    "CREATE INDEX provenance_by_fact ON provenance_locator (fact_id)",
    "CREATE INDEX provenance_by_source ON provenance_locator (source_id)",
];

/// Statements of migration 3.
pub const M0003_FACT_CLAIM_CONFLICT: &[&str] = &[
    "CREATE TABLE fact (
        fact_id                 TEXT PRIMARY KEY NOT NULL,
        workspace_id            TEXT NOT NULL REFERENCES workspace (workspace_id),
        subject_key             TEXT NOT NULL,
        predicate               TEXT NOT NULL,
        typed_value             TEXT NOT NULL,
        unit                    TEXT,
        classification          TEXT NOT NULL,
        confidence_basis_points INTEGER,
        observation_time_utc    TEXT NOT NULL,
        validity_start_utc      TEXT,
        validity_end_utc        TEXT,
        sensitivity             TEXT NOT NULL,
        status                  TEXT NOT NULL,
        supersedes_fact_id      TEXT REFERENCES fact (fact_id),
        retention_class         TEXT NOT NULL,
        schema_version          INTEGER NOT NULL
    )",
    "CREATE INDEX fact_by_workspace ON fact (workspace_id, subject_key, predicate)",
    "CREATE TABLE claim (
        claim_id         TEXT PRIMARY KEY NOT NULL,
        task_id          TEXT NOT NULL,
        artifact_id      TEXT,
        body             TEXT NOT NULL,
        classification   TEXT NOT NULL,
        validation_state TEXT NOT NULL,
        generated_by     TEXT,
        schema_version   INTEGER NOT NULL
    )",
    "CREATE TABLE claim_support (
        claim_id TEXT NOT NULL REFERENCES claim (claim_id),
        fact_id  TEXT NOT NULL REFERENCES fact (fact_id),
        PRIMARY KEY (claim_id, fact_id)
    )",
    "CREATE INDEX claim_support_by_fact ON claim_support (fact_id)",
    "CREATE TABLE conflict (
        conflict_id        TEXT PRIMARY KEY NOT NULL,
        workspace_id       TEXT NOT NULL REFERENCES workspace (workspace_id),
        subject_key        TEXT NOT NULL,
        predicate          TEXT NOT NULL,
        reason             TEXT NOT NULL,
        resolution_state   TEXT NOT NULL,
        resolution_fact_id TEXT REFERENCES fact (fact_id),
        resolution_rule    TEXT,
        explanation        TEXT,
        schema_version     INTEGER NOT NULL
    )",
    "CREATE TABLE conflict_fact (
        conflict_id TEXT NOT NULL REFERENCES conflict (conflict_id),
        fact_id     TEXT NOT NULL REFERENCES fact (fact_id),
        PRIMARY KEY (conflict_id, fact_id)
    )",
    "CREATE INDEX conflict_fact_by_fact ON conflict_fact (fact_id)",
];

/// Statements of migration 4.
pub const M0004_TASK_AND_JOURNAL: &[&str] = &[
    "CREATE TABLE task (
        task_id               TEXT PRIMARY KEY NOT NULL,
        workspace_id          TEXT REFERENCES workspace (workspace_id),
        browser_profile_id    TEXT NOT NULL,
        kind                  TEXT NOT NULL,
        state                 TEXT NOT NULL,
        execution_phase       TEXT,
        state_reason          TEXT,
        revision              INTEGER NOT NULL,
        user_goal             TEXT NOT NULL,
        created_by            TEXT NOT NULL,
        assistant_snapshot    TEXT NOT NULL,
        control_mode          TEXT NOT NULL,
        source_scope          TEXT NOT NULL,
        data_policy_snapshot  TEXT NOT NULL,
        provider_route_snapshot TEXT NOT NULL,
        budgets               TEXT NOT NULL,
        created_at_utc        TEXT NOT NULL,
        updated_at_utc        TEXT NOT NULL,
        deadline_utc          TEXT,
        retention_class       TEXT NOT NULL,
        schema_version        INTEGER NOT NULL
    )",
    "CREATE INDEX task_by_workspace ON task (workspace_id, state)",
    "CREATE TABLE task_event (
        event_id           TEXT PRIMARY KEY NOT NULL,
        aggregate_type     TEXT NOT NULL,
        aggregate_id       TEXT NOT NULL,
        aggregate_revision INTEGER NOT NULL,
        event_type         TEXT NOT NULL,
        schema_version     INTEGER NOT NULL,
        occurred_at_utc    TEXT NOT NULL,
        monotonic_sequence INTEGER NOT NULL,
        actor              TEXT NOT NULL,
        task_id            TEXT,
        trace_id           TEXT NOT NULL,
        causation_event_id TEXT,
        correlation_id     TEXT,
        redaction_class    TEXT NOT NULL,
        payload            TEXT NOT NULL
    )",
    "CREATE UNIQUE INDEX task_event_expected_revision
        ON task_event (aggregate_type, aggregate_id, aggregate_revision)",
    "CREATE INDEX task_event_by_sequence ON task_event (monotonic_sequence)",
    "CREATE TABLE journal_source_projection (
        event_id        TEXT NOT NULL REFERENCES task_event (event_id),
        source_id       TEXT NOT NULL REFERENCES source (source_id),
        workspace_id    TEXT,
        occurred_at_utc TEXT NOT NULL,
        summary         TEXT NOT NULL,
        PRIMARY KEY (event_id, source_id)
    )",
    "CREATE INDEX journal_source_projection_by_source ON journal_source_projection (source_id)",
    "CREATE TABLE task_state_projection (
        task_id         TEXT PRIMARY KEY NOT NULL,
        state           TEXT NOT NULL,
        execution_phase TEXT,
        revision        INTEGER NOT NULL,
        updated_at_utc  TEXT NOT NULL,
        last_event_id   TEXT NOT NULL
    )",
    "CREATE TABLE projection_checkpoint (
        projection_name TEXT PRIMARY KEY NOT NULL,
        last_sequence   INTEGER NOT NULL,
        rebuilt_at_utc  TEXT NOT NULL
    )",
];

/// Statements of migration 5.
pub const M0005_INVOCATION_AND_ARTIFACT: &[&str] = &[
    "CREATE TABLE model_invocation (
        model_invocation_id      TEXT PRIMARY KEY NOT NULL,
        task_id                  TEXT NOT NULL REFERENCES task (task_id),
        purpose                  TEXT NOT NULL,
        provider_route_snapshot  TEXT NOT NULL,
        model_identifier         TEXT NOT NULL,
        route_version            TEXT,
        context_manifest         TEXT NOT NULL,
        redaction_policy_version TEXT NOT NULL,
        request_digest           TEXT NOT NULL,
        response_digest          TEXT,
        input_units              INTEGER,
        output_units             INTEGER,
        cost_class               TEXT,
        started_at_utc           TEXT NOT NULL,
        ended_at_utc             TEXT,
        result_code              TEXT NOT NULL,
        retry_of                 TEXT REFERENCES model_invocation (model_invocation_id),
        retention_class          TEXT NOT NULL,
        schema_version           INTEGER NOT NULL
    )",
    "CREATE INDEX model_invocation_by_task ON model_invocation (task_id, started_at_utc)",
    "CREATE TABLE artifact (
        artifact_id        TEXT PRIMARY KEY NOT NULL,
        task_id            TEXT NOT NULL REFERENCES task (task_id),
        workspace_id       TEXT REFERENCES workspace (workspace_id),
        kind               TEXT NOT NULL,
        state              TEXT NOT NULL,
        lineage_state      TEXT NOT NULL,
        generation_method  TEXT NOT NULL,
        validation_result  TEXT,
        content_digest     TEXT,
        encrypted_blob_ref TEXT,
        size_bytes         INTEGER,
        created_at_utc     TEXT NOT NULL,
        accepted_at_utc    TEXT,
        exported_copies    INTEGER NOT NULL,
        retention_class    TEXT NOT NULL,
        schema_version     INTEGER NOT NULL
    )",
    "CREATE TABLE artifact_lineage (
        artifact_id  TEXT NOT NULL REFERENCES artifact (artifact_id),
        related_type TEXT NOT NULL,
        related_id   TEXT NOT NULL,
        PRIMARY KEY (artifact_id, related_type, related_id)
    )",
    "CREATE INDEX artifact_lineage_by_related ON artifact_lineage (related_type, related_id)",
];

/// Statements of migration 6.
pub const M0006_ASSISTANT_CONFIGURATION: &[&str] = &["CREATE TABLE assistant_config (
        config_version   INTEGER PRIMARY KEY NOT NULL,
        display_name     TEXT NOT NULL,
        avatar_ref       TEXT,
        model_policy     TEXT NOT NULL,
        skill_versions   TEXT NOT NULL,
        tool_allowlist   TEXT NOT NULL,
        limits           TEXT NOT NULL,
        audit_identity   TEXT NOT NULL,
        created_at_utc   TEXT NOT NULL,
        retention_class  TEXT NOT NULL,
        schema_version   INTEGER NOT NULL
    )"];

/// Statements of migration 7.
pub const M0007_RETENTION_CLASSES: &[&str] = &["CREATE TABLE retention_class (
        name           TEXT PRIMARY KEY NOT NULL,
        durable        INTEGER NOT NULL,
        bound_kind     TEXT NOT NULL,
        bound_scope    TEXT,
        bound_millis   INTEGER,
        register_entry TEXT,
        description    TEXT NOT NULL
    )"];

/// Statements of migration 8.
pub const M0008_SEARCH_AND_DELETION_LEDGER: &[&str] = &[
    "CREATE TABLE search_document (
        document_id     TEXT PRIMARY KEY NOT NULL,
        record_type     TEXT NOT NULL,
        record_id       TEXT NOT NULL,
        workspace_id    TEXT,
        source_id       TEXT,
        retention_class TEXT NOT NULL,
        indexed_at_utc  TEXT NOT NULL
    )",
    "CREATE INDEX search_document_by_source ON search_document (source_id)",
    "CREATE INDEX search_document_by_record ON search_document (record_type, record_id)",
    "CREATE VIRTUAL TABLE search_index USING fts5 (document_id UNINDEXED, title, body)",
    "CREATE TABLE deletion_tombstone (
        record_type     TEXT NOT NULL,
        record_id       TEXT NOT NULL,
        deleted_at_utc  TEXT NOT NULL,
        revision        INTEGER NOT NULL,
        retention_class TEXT NOT NULL,
        PRIMARY KEY (record_type, record_id)
    )",
    "CREATE TABLE deletion_receipt (
        receipt_id                  TEXT PRIMARY KEY NOT NULL,
        subject_type                TEXT NOT NULL,
        subject_id                  TEXT NOT NULL,
        completed_at_utc            TEXT NOT NULL,
        derived_policy              TEXT NOT NULL,
        observations_removed        INTEGER NOT NULL,
        provenance_removed          INTEGER NOT NULL,
        facts_removed               INTEGER NOT NULL,
        facts_relabeled             INTEGER NOT NULL,
        claims_relabeled            INTEGER NOT NULL,
        artifacts_removed           INTEGER NOT NULL,
        artifacts_relabeled         INTEGER NOT NULL,
        index_entries_removed       INTEGER NOT NULL,
        journal_events_relabeled    INTEGER NOT NULL,
        journal_projections_removed INTEGER NOT NULL,
        memberships_removed         INTEGER NOT NULL,
        external_copies             INTEGER NOT NULL,
        cloud_deletion_state        TEXT NOT NULL,
        verified                    INTEGER NOT NULL
    )",
];

/// The secret store and the served catalog cache.
///
/// Two tables that were deliberately absent until now, and the reason each was
/// absent is worth keeping because it is not the reason a reader would guess.
///
/// `provider_credential` was not waiting on the credential *shape* — the
/// provider registry has specified it since it was written: one record per
/// provider keyed by `provider_id`, an auth type, an envelope-encrypted blob,
/// and non-secret linkage parameters beside it. It was waiting on the wrapping
/// key policy, which is [OD-039], and that question is about where the key
/// lives and whether it is bound to device unlock — not about what the row
/// holds. The blob is opaque to this schema either way, so the table can land
/// and OD-039 can still be open. What this service must never do is look
/// inside the blob, which is why there is no column that could tempt it.
///
/// `catalog_cache` holds the remote overlay between refreshes so that first
/// run and offline operation work from the embedded baseline and a warm cache
/// rather than from a network call. It stores the validator the fetch sends
/// back (a version cursor or an entity tag) so an unchanged catalog costs a
/// header exchange, and the generation time so an overlay older than the
/// embedded baseline can be ignored rather than downgraded to.
///
/// [OD-039]: ../../../../../../../docs/open-decisions.md
pub const M0009_CREDENTIAL_AND_CATALOG_CACHE: &[&str] = &[
    "CREATE TABLE provider_credential (
        provider_id     TEXT PRIMARY KEY NOT NULL,
        auth_type       TEXT NOT NULL,
        secret_blob     BLOB NOT NULL,
        parameters      TEXT,
        created_at_utc  TEXT NOT NULL,
        rotated_at_utc  TEXT,
        schema_version  INTEGER NOT NULL
    )",
    "CREATE TABLE catalog_cache (
        catalog_version TEXT PRIMARY KEY NOT NULL,
        validator       TEXT,
        generated_at_utc TEXT NOT NULL,
        fetched_at_utc  TEXT NOT NULL,
        document        BLOB NOT NULL,
        schema_version  INTEGER NOT NULL
    )",
    "CREATE INDEX catalog_cache_by_generation ON catalog_cache (generated_at_utc)",
];

/// Content-free replay identity for exact workspace deletion.
pub const M0010_WORKSPACE_LIFECYCLE: &[&str] = &["CREATE TABLE workspace_deletion_receipt (
        operation_id       TEXT PRIMARY KEY NOT NULL,
        workspace_id       TEXT UNIQUE NOT NULL,
        expected_revision  INTEGER NOT NULL,
        resulting_revision INTEGER NOT NULL,
        confirmation_token TEXT NOT NULL,
        source_count       INTEGER NOT NULL,
        fact_count         INTEGER NOT NULL,
        artifact_count     INTEGER NOT NULL,
        index_count        INTEGER NOT NULL,
        external_copies    INTEGER NOT NULL,
        completed_at_utc   TEXT NOT NULL,
        verified           INTEGER NOT NULL
    )"];
