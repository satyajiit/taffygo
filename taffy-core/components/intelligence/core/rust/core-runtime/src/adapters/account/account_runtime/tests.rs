// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Account operation admission and exactly-once completion tests.

use core_service_types as wire;

use crate::account::{DigestError, Sha256Port, AUTHORIZATION_ENTROPY_BYTES};
use crate::contract::ServiceGeneration;

use crate::adapters::account::ProductionAccount;
use crate::ports::AccountPort;

use super::{AccountOperationRuntime, AccountServiceError};

mod refresh;

struct TestDigest;

impl Sha256Port for TestDigest {
    fn sha256(&self, _: &[u8]) -> Result<[u8; 32], DigestError> {
        Ok(core::array::from_fn(|index| {
            u8::try_from(index.saturating_add(1)).unwrap_or_default()
        }))
    }
}

fn operation() -> wire::OperationEnvelope {
    wire::OperationEnvelope {
        operation_id: "account-op-1".to_owned(),
        service_generation: ServiceGeneration::INITIAL.value(),
        task_revision: 0,
        deadline_monotonic_ms: 100_000,
        idempotency_key: "account-key-1".to_owned(),
    }
}

fn start_auth() -> wire::CoreServiceCommand {
    wire::CoreServiceCommand {
        operation: operation(),
        kind: wire::CoreServiceCommandKind::StartAuth,
        start_task: None,
        cancel_task: None,
        user_decision: None,
        auth_callback: None,
        permission_result: None,
        start_auth: Some(wire::StartAuthCommand {
            flow_id: "flow-1".to_owned(),
            method: wire::AccountAuthMethod::Github,
            redirect_binding_id: "primary_auth_callback".to_owned(),
            scopes: vec![wire::AccountScope::OpenId, wire::AccountScope::Email],
            issued_at_monotonic_ms: 10,
        }),
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
        set_provider_credential_state: None,
        probe_provider_credential: None,
    }
}

fn entropy_result(effect: &wire::EffectEnvelope, flow_id: &str) -> wire::EffectResult {
    wire::EffectResult {
        operation: effect.operation.clone(),
        effect_id: effect.effect_id.clone(),
        status: wire::EffectStatus::Completed,
        kind: wire::EffectKind::SecureStore,
        storage: None,
        observation: None,
        model: None,
        network: None,
        browser_action: None,
        tool: None,
        secure_store: Some(wire::SecureStoreEffectResult {
            operation_kind: wire::SecureStoreOperation::GenerateEntropy,
            generated_entropy: Some(wire::GeneratedEntropyResult {
                flow_id: flow_id.to_owned(),
                entropy: core::array::from_fn::<_, AUTHORIZATION_ENTROPY_BYTES, _>(|index| {
                    if index < 32 {
                        1
                    } else {
                        2
                    }
                })
                .to_vec(),
            }),
            transient_write: None,
            deleted_handle: None,
        }),
        auth_surface: None,
        permission: None,
        asset_delivery: None,
        catalog: None,
        provider_listing: None,
        composer_completion: None,
        custom_endpoint_probe: None,
    }
}

#[test]
fn account_command_is_admitted_once_and_advances_only_after_typed_completion() {
    let mut runtime = AccountOperationRuntime::new();
    let mut account = ProductionAccount::new();
    let command = start_auth();
    let first = runtime
        .submit(
            &mut account,
            ServiceGeneration::INITIAL,
            &command,
            10,
            &TestDigest,
        )
        .unwrap_or_else(|_| unreachable!());
    assert_eq!(first.admission, wire::AdmissionStatus::Accepted);
    assert_eq!(first.effects.len(), 1);

    let duplicate = runtime
        .submit(
            &mut account,
            ServiceGeneration::INITIAL,
            &command,
            10,
            &TestDigest,
        )
        .unwrap_or_else(|_| unreachable!());
    assert_eq!(duplicate.admission, wire::AdmissionStatus::Duplicate);
    assert!(duplicate.effects.is_empty());

    let completion = entropy_result(&first.effects[0], "flow-1");
    let advanced = runtime
        .deliver(
            &mut account,
            ServiceGeneration::INITIAL,
            &completion,
            20,
            &TestDigest,
        )
        .unwrap_or_else(|_| unreachable!());
    assert_eq!(advanced.effects.len(), 1);
    assert_eq!(advanced.effects[0].kind, wire::EffectKind::SecureStore);
    assert_eq!(
        advanced.effects[0]
            .secure_store
            .as_ref()
            .map(|effect| effect.operation_kind),
        Some(wire::SecureStoreOperation::WriteTransient)
    );
}

#[test]
fn malformed_completion_does_not_consume_the_pending_stage() {
    let mut runtime = AccountOperationRuntime::new();
    let mut account = ProductionAccount::new();
    let admitted = runtime
        .submit(
            &mut account,
            ServiceGeneration::INITIAL,
            &start_auth(),
            10,
            &TestDigest,
        )
        .unwrap_or_else(|_| unreachable!());
    let mut malformed = entropy_result(&admitted.effects[0], "other-flow");
    assert_eq!(
        runtime.deliver(
            &mut account,
            ServiceGeneration::INITIAL,
            &malformed,
            20,
            &TestDigest,
        ),
        Err(AccountServiceError::InvalidCompletion)
    );
    if let Some(result) = malformed
        .secure_store
        .as_mut()
        .and_then(|result| result.generated_entropy.as_mut())
    {
        result.flow_id = "flow-1".to_owned();
    }
    assert!(runtime
        .deliver(
            &mut account,
            ServiceGeneration::INITIAL,
            &malformed,
            21,
            &TestDigest,
        )
        .is_ok());
}

#[test]
fn deadline_terminal_settles_at_the_exact_operation_deadline() {
    let mut runtime = AccountOperationRuntime::new();
    let mut account = ProductionAccount::new();
    let admitted = runtime
        .submit(
            &mut account,
            ServiceGeneration::INITIAL,
            &start_auth(),
            10,
            &TestDigest,
        )
        .unwrap_or_else(|_| unreachable!());
    let mut terminal = entropy_result(&admitted.effects[0], "flow-1");
    terminal.status = wire::EffectStatus::DeadlineExceeded;

    let settled = runtime
        .deliver(
            &mut account,
            ServiceGeneration::INITIAL,
            &terminal,
            terminal.operation.deadline_monotonic_ms,
            &TestDigest,
        )
        .unwrap_or_else(|_| unreachable!());

    assert!(settled.effects.is_empty());
    assert_eq!(account.pending_count(), 0);
    assert_eq!(runtime.pending.len(), 0);
}

#[test]
fn distinct_sign_in_operations_cannot_mutate_one_profile_session_concurrently() {
    let mut runtime = AccountOperationRuntime::new();
    let mut account = ProductionAccount::new();
    let first = start_auth();
    assert!(runtime
        .submit(
            &mut account,
            ServiceGeneration::INITIAL,
            &first,
            10,
            &TestDigest,
        )
        .is_ok());

    let mut second = start_auth();
    second.operation.operation_id = "account-op-2".to_owned();
    second.operation.idempotency_key = "account-key-2".to_owned();
    if let Some(body) = second.start_auth.as_mut() {
        body.flow_id = "flow-2".to_owned();
    }
    assert_eq!(
        runtime.submit(
            &mut account,
            ServiceGeneration::INITIAL,
            &second,
            10,
            &TestDigest,
        ),
        Err(AccountServiceError::Domain(
            crate::account::AccountError::SessionMutationInFlight,
        ))
    );
    assert_eq!(account.pending_count(), 1);
}
