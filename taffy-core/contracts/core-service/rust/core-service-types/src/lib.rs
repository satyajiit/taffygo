// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Generated, authority-neutral values for the Core Service boundary.
//!
//! The schema and generator live at `taffy-core/contracts/core-service/`.
//! This crate is deliberately only a source-owning wrapper: storage replay,
//! the profile runtime, and the utility bridge all consume the same generated
//! records and never define a second wire or persistence codec.
#![doc(html_no_source)]
#![cfg_attr(
    test,
    allow(
        clippy::unwrap_used,
        clippy::expect_used,
        clippy::panic,
        clippy::indexing_slicing
    )
)]

// Generated transaction unions mirror the versioned schema by value. Keep
// the wire shape generator-owned; runtime commands box their large outcome at
// the domain boundary instead of hand-editing generated bindings.
#[allow(clippy::large_enum_variant)]
#[rustfmt::skip]
#[path = "../../../generated/rust/core_service.rs"]
mod generated;

pub use generated::*;

#[cfg(test)]
mod tests {
    use super::{
        decode_transaction_batch, encode_transaction_batch, CoreServiceCommand,
        CoreServiceCommandKind, OperationEnvelope, PersistedEffectIntent, StartTaskCommand,
        TaskConsentPreview, TaskControlMode, TaskKind, TaskMilestone, TaskProviderRoute,
        TaskTemplateId, TaskTransactionBatch, TransactionBatchCodecError,
        TRANSACTION_BATCH_SCHEMA_VERSION,
    };

    fn operation() -> OperationEnvelope {
        OperationEnvelope {
            operation_id: "op".to_owned(),
            service_generation: 1,
            task_revision: 0,
            deadline_monotonic_ms: 2,
            idempotency_key: "create".to_owned(),
        }
    }

    fn start() -> StartTaskCommand {
        StartTaskCommand {
            task_id: "task".to_owned(),
            workspace_id: None,
            browser_profile_id: "profile".to_owned(),
            kind: TaskKind::Research,
            goal: "research".to_owned(),
            control_mode: TaskControlMode::Shared,
            provider_route_id: None,
            assistant_config_version: 1,
            policy_version: 1,
            skill_version_id: None,
            builtin_skill: None,
            tool_allowlist: Vec::new(),
            milestone: TaskMilestone::M3,
            budgets: Vec::new(),
            has_task_deadline: false,
            task_deadline_monotonic_ms: 0,
            predecessor_task_id: None,
            trace_id: "trace".to_owned(),
            task_id_seed: [7; 32],
            template_id: TaskTemplateId::CompareProducts,
            consent_preview: TaskConsentPreview {
                sources: Vec::new(),
                source_discovery_enabled: false,
                new_source_cap: 0,
                provider_route: TaskProviderRoute::NoModelRequired,
            },
            initial_consent_receipt_id: "consent".to_owned(),
            browser_session_id: "session".to_owned(),
            task_deadline_utc_ms: 0,
            library_refresh: None,
        }
    }

    #[test]
    fn tagged_command_requires_exact_matching_body() {
        let mut command = CoreServiceCommand {
            operation: operation(),
            kind: CoreServiceCommandKind::StartTask,
            start_task: Some(start()),
            cancel_task: None,
            user_decision: None,
            auth_callback: None,
            permission_result: None,
            start_auth: None,
            request_email_link: None,
            sign_out: None,
            auth_credential_result: None,
            correct_workspace_fact: None,
            exclude_workspace_source: None,
            request_workspace_export: None,
            set_asset_delivery_policy: None,
            request_asset: None,
            remove_asset: None,
            save_provider_credential: None,
            set_provider_credential_state: None,
            probe_provider_credential: None,
            forget_provider_credential: None,
            start_provider_auth: None,
            provider_auth_callback: None,
            save_custom_provider: None,
            remove_custom_provider: None,
            complete_handover: None,
            expire_handover: None,
            supply_user_input: None,
            follow_up: None,
            supply_field_values: None,
            set_provider_model_preference: None,
            probe_custom_endpoint: None,
            request_composer_completion: None,
            cancel_composer_completion: None,
            pause_task: None,
            resume_task: None,
            take_over: None,
            set_assistant_configuration: None,
            save_workspace: None,

            rename_workspace: None,

            delete_workspace: None,

            discard_workspace: None,
            search_library: None,
            save_library_fact: None,
            remove_library_entry: None,
            request_library_export: None,
            search_memory: None,
            upsert_memory: None,
            delete_memory: None,
            accept_task_artifact: None,
            export_task_artifact: None,
            replace_saved_data_snapshot: None,
            mutate_skill: None,
            cancel_provider_auth: None,
        };
        assert!(command.has_valid_body());
        command.start_task = None;
        assert!(!command.has_valid_body());
    }

    fn transaction_with_one_closed_effect() -> TaskTransactionBatch {
        TaskTransactionBatch {
            schema_version: TRANSACTION_BATCH_SCHEMA_VERSION,
            seed: None,
            journal_entries: Vec::new(),
            effect_intents: vec![PersistedEffectIntent::ReleaseTaskTabs],
            audit_records: Vec::new(),
        }
    }

    #[test]
    fn transaction_codec_round_trips_the_closed_batch() {
        let batch = transaction_with_one_closed_effect();
        let bytes = encode_transaction_batch(&batch).unwrap_or_default();
        assert!(!bytes.is_empty());
        assert_eq!(decode_transaction_batch(&bytes), Ok(batch));
    }

    fn hex_bytes(value: &str) -> Vec<u8> {
        value
            .trim()
            .as_bytes()
            .chunks_exact(2)
            .map(|pair| {
                let text = core::str::from_utf8(pair).unwrap_or_default();
                u8::from_str_radix(text, 16).unwrap_or_default()
            })
            .collect()
    }

    fn frozen_batch() -> Vec<u8> {
        // The frozen bytes under golden/ are an input, never regenerated output.
        // Decoding them here is the promise a device with an existing journal is
        // relying on: today's reader still understands yesterday's file.
        hex_bytes(include_str!("../../../golden/task-transaction-v14.hex"))
    }

    #[test]
    fn the_frozen_journal_payload_still_decodes_and_re_encodes() {
        let bytes = frozen_batch();
        let batch = decode_transaction_batch(&bytes).expect("the frozen batch must decode");
        assert_eq!(batch.schema_version, TRANSACTION_BATCH_SCHEMA_VERSION);
        assert_eq!(encode_transaction_batch(&batch), Ok(bytes));
    }

    #[test]
    fn the_version_twenty_nine_payload_is_refused_on_its_version_and_kept() {
        // Version 30 appended the errand task kind, two pause causes, four
        // failure reasons, two state reasons, the follow-up command with its
        // kind, and the follow-up event (decisions 0136 and 0137). Appended
        // only, and refused all the same, for the reason every entry below
        // gives.
        let bytes = hex_bytes(include_str!("../../../golden/task-transaction-v29.hex"));
        assert_eq!(
            decode_transaction_batch(&bytes),
            Err(TransactionBatchCodecError::UnsupportedVersion)
        );
    }

    #[test]
    fn the_version_twenty_eight_payload_is_refused_on_its_version_and_kept() {
        // Version 29 appended one persisted action class, the read of a
        // person's attached store (decision 0133). Appended only, and refused
        // all the same, for the reason every entry below gives.
        let bytes = hex_bytes(include_str!("../../../golden/task-transaction-v28.hex"));
        assert_eq!(
            decode_transaction_batch(&bytes),
            Err(TransactionBatchCodecError::UnsupportedVersion)
        );
    }

    #[test]
    fn the_version_twenty_seven_payload_is_refused_on_its_version_and_kept() {
        // Version 28 appended three reducer events — a recorded model reply,
        // a recorded model-turn gap and a context eviction — and two audit
        // event types beside them. Before it, those commands were journalled
        // with no event, so their commit never advanced the revision and
        // storage refused it: the first live model answer was the commit that
        // took the core down. Appended only, and refused all the same, for
        // the reason every entry below gives.
        let bytes = hex_bytes(include_str!("../../../golden/task-transaction-v27.hex"));
        assert_eq!(
            decode_transaction_batch(&bytes),
            Err(TransactionBatchCodecError::UnsupportedVersion)
        );
    }

    #[test]
    fn the_version_thirteen_payload_is_refused_on_its_version_and_kept() {
        // Version 14 appended the artifact identity and exact workspace
        // revision to artifact commands and effects. Appended only, and
        // refused all the same: a version-13 artifact effect has no durable
        // identity or evidence revision, so reconstructing one would guess
        // which bytes were accepted.
        let bytes = hex_bytes(include_str!("../../../golden/task-transaction-v13.hex"));
        assert_eq!(
            decode_transaction_batch(&bytes),
            Err(TransactionBatchCodecError::UnsupportedVersion)
        );
    }

    #[test]
    fn the_version_twelve_payload_is_refused_on_its_version_and_kept() {
        // Version 13 appended the canonical typed action intent to each
        // persisted proposal. A version-12 proposal has no typed operation to
        // validate against its legacy projections, so reconstruction refuses
        // rather than guessing at authority.
        let bytes = hex_bytes(include_str!("../../../golden/task-transaction-v12.hex"));
        assert_eq!(
            decode_transaction_batch(&bytes),
            Err(TransactionBatchCodecError::UnsupportedVersion)
        );
    }

    #[test]
    fn the_version_eleven_payload_is_refused_on_its_version_and_kept() {
        // Version 12 appended the field-value vocabulary of decision 0088: the
        // two commands that ask a person to fill a form in and record that
        // they answered, and the effect that opens the surface. Appended only,
        // and refused all the same, for the reason every entry below gives —
        // a version-11 file that happens to contain none of them is
        // indistinguishable from one truncated before them.
        let bytes = hex_bytes(include_str!("../../../golden/task-transaction-v11.hex"));
        assert_eq!(
            decode_transaction_batch(&bytes),
            Err(TransactionBatchCodecError::UnsupportedVersion)
        );
    }

    #[test]
    fn the_version_ten_payload_is_refused_on_its_version_and_kept() {
        // Version 11 appended the tool-job vocabulary: the dispatched effect,
        // its outcome command, and the ExecuteToolJob action class. Appended
        // only, and refused all the same — a version-10 file that happens to
        // contain no tool job is indistinguishable from one truncated before
        // it, and guessing is what the refusal exists to prevent.
        let bytes = hex_bytes(include_str!("../../../golden/task-transaction-v10.hex"));
        assert_eq!(
            decode_transaction_batch(&bytes),
            Err(TransactionBatchCodecError::UnsupportedVersion)
        );
    }

    #[test]
    fn the_version_nine_payload_is_refused_on_its_version_and_kept() {
        // Version 10 appended the context-eviction command to the persisted
        // vocabulary. Nothing already written moved, but a version-9 reader
        // has no way to know that, and a version-10 reader refusing version 9
        // is decision 0058's section 3 again: today's behaviour is refusal,
        // and when the supported version becomes a set this assertion becomes
        // a decode of the same file.
        let bytes = hex_bytes(include_str!("../../../golden/task-transaction-v9.hex"));
        assert_eq!(
            decode_transaction_batch(&bytes),
            Err(TransactionBatchCodecError::UnsupportedVersion)
        );
    }

    #[test]
    fn the_version_seven_payload_is_refused_on_its_version_and_kept() {
        // Version 7 joined version 6 when the handover vocabulary was appended:
        // the members are new, so nothing already written moved, but a reader
        // has no way to tell a version-7 file that happens to contain no
        // handover from one that was truncated before it, and guessing is the
        // thing this refusal exists to prevent.
        let bytes = hex_bytes(include_str!("../../../golden/task-transaction-v7.hex"));
        assert_eq!(
            decode_transaction_batch(&bytes),
            Err(TransactionBatchCodecError::UnsupportedVersion)
        );
    }

    #[test]
    fn the_version_six_payload_is_refused_on_its_version_and_kept() {
        // Version 6's bytes cannot be produced again once the layout moved, so
        // they stay under golden/ and stay under test. What this build does
        // with them is refuse them by version, which is decision 0058's
        // section 3 describing today's behaviour rather than the behaviour it
        // asks for: when the supported version becomes a set, this assertion
        // becomes a decode and the same file is what proves it.
        let bytes = hex_bytes(include_str!("../../../golden/task-transaction-v6.hex"));
        assert_eq!(
            decode_transaction_batch(&bytes),
            Err(TransactionBatchCodecError::UnsupportedVersion)
        );
    }

    #[test]
    fn transaction_codec_rejects_unknown_tags_and_trailing_bytes() {
        let mut bytes =
            encode_transaction_batch(&transaction_with_one_closed_effect()).unwrap_or_default();
        let tag_offset = bytes.len().saturating_sub(8);
        if let Some(tag) = bytes.get_mut(tag_offset..tag_offset.saturating_add(4)) {
            tag.copy_from_slice(&u32::MAX.to_le_bytes());
        }
        assert_eq!(
            decode_transaction_batch(&bytes),
            Err(TransactionBatchCodecError::InvalidEnum)
        );

        let mut trailing =
            encode_transaction_batch(&transaction_with_one_closed_effect()).unwrap_or_default();
        trailing.push(0);
        assert_eq!(
            decode_transaction_batch(&trailing),
            Err(TransactionBatchCodecError::TrailingBytes)
        );
    }
}
