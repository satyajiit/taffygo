// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The managed entitlement: minted with a session, and gone with it.

use std::rc::Rc;

use core_service_types as wire;

use super::{built_runtime, configuration};
use crate::account::crypto::ReferenceSha256;
use crate::codec::account_wire::decode_account_session;
use crate::composition::profile::create_profile_service_runtime;
use crate::contract::ServiceGeneration;

fn entitlement_summary(definitive_absent: bool) -> wire::EntitlementSummaryResult {
    wire::EntitlementSummaryResult {
        definitive_absent,
        plan_id: "plan-standard".to_owned(),
        model_ids: vec!["est-large".to_owned()],
        window_seconds: 3_600,
        requests_remaining: 9,
        credits_granted: 1_000,
        credits_remaining: 964,
        credit_unit_micros: 500,
        next_renewal_epoch_seconds: 1_788_912_000,
        valid_until_epoch_seconds: 0,
        minted_at_utc_ms: 1_000,
        worker_host: "edge.taffygo.invalid".to_owned(),
        gateway_host: "gateway.taffygo.invalid".to_owned(),
    }
}

#[test]
fn an_installed_entitlement_summary_reaches_the_published_auth_state() {
    use crate::entitlement_refresh::EntitlementFetchVerdict;

    let mut runtime = built_runtime();
    let effect = runtime
        .plan_entitlement_refresh(wire::EntitlementFetchReason::Bootstrap, 1_000)
        .unwrap_or_else(|| unreachable!());
    assert_eq!(effect.kind, wire::EffectKind::NetworkRequest);
    assert!(
        runtime
            .plan_entitlement_refresh(wire::EntitlementFetchReason::SignIn, 1_000)
            .is_none(),
        "one mint in flight, never two"
    );
    let verdict =
        runtime.deliver_entitlement_fetch_result(Some(&entitlement_summary(false)), 1_000);
    assert_eq!(
        verdict,
        EntitlementFetchVerdict::Installed {
            definitive_absent: false
        }
    );
    // The one status projection is how a surface hears the plan row.
    let status = runtime
        .project_core_status(core_api_types::CoreAvailability::Ready)
        .unwrap_or_else(|_| unreachable!());
    let entitlement = status
        .auth_state
        .and_then(|auth| auth.entitlement)
        .unwrap_or_else(|| unreachable!("an installed summary is published"));
    assert_eq!(entitlement.plan_id, "plan-standard");
    assert_eq!(entitlement.credits_granted, 1_000);
    assert_eq!(entitlement.credits_remaining, 964);
    assert_eq!(entitlement.next_renewal_epoch_seconds, 1_788_912_000);
    assert_eq!(entitlement.valid_until_epoch_seconds, 0);
}

#[test]
fn a_definitive_absence_installs_and_publishes_no_plan_row() {
    use crate::entitlement_refresh::EntitlementFetchVerdict;

    let mut runtime = built_runtime();
    assert!(runtime
        .plan_entitlement_refresh(wire::EntitlementFetchReason::Bootstrap, 1_000)
        .is_some());
    let verdict = runtime.deliver_entitlement_fetch_result(Some(&entitlement_summary(true)), 1_000);
    assert_eq!(
        verdict,
        EntitlementFetchVerdict::Installed {
            definitive_absent: true
        }
    );
    let status = runtime
        .project_core_status(core_api_types::CoreAvailability::Ready)
        .unwrap_or_else(|_| unreachable!());
    assert!(
        status
            .auth_state
            .is_some_and(|auth| auth.entitlement.is_none()),
        "\"you have no plan\" draws nothing"
    );
}

#[test]
fn a_private_profile_never_plans_a_mint() {
    let entropy = core::array::from_fn(|index| u8::try_from(index).unwrap_or_default());
    let mut runtime = create_profile_service_runtime(
        configuration(ServiceGeneration::INITIAL, entropy, 1_000, true, Vec::new()),
        Rc::new(ReferenceSha256),
    )
    .unwrap_or_else(|_| unreachable!());
    assert!(runtime
        .plan_entitlement_refresh(wire::EntitlementFetchReason::Bootstrap, 1_000)
        .is_none());
}

fn sign_out_command() -> wire::CoreServiceCommand {
    wire::CoreServiceCommand {
        operation: wire::OperationEnvelope {
            operation_id: "sign-out-operation-1".to_owned(),
            service_generation: ServiceGeneration::INITIAL.value(),
            task_revision: 0,
            deadline_monotonic_ms: 10_000,
            idempotency_key: "sign-out-key-1".to_owned(),
        },
        kind: wire::CoreServiceCommandKind::SignOut,
        start_task: None,
        cancel_task: None,
        user_decision: None,
        auth_callback: None,
        permission_result: None,
        start_auth: None,
        request_email_link: None,
        sign_out: Some(wire::SignOutCommand {
            account_subject: None,
        }),
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

#[test]
fn a_sign_out_takes_the_entitlement_with_the_session() {
    let session = decode_account_session(&wire::AccountSessionHandle {
        session_handle: "session-handle-1".to_owned(),
        account_subject: "account-subject-1".to_owned(),
        expires_at_monotonic_ms: 100_000,
        rotation: 1,
        auth_method: wire::AccountAuthMethod::Github,
        email: None,
        display_name: None,
    })
    .unwrap_or_else(|_| unreachable!());
    let mut runtime = built_runtime();
    runtime
        .restore_account_session(Some(session))
        .unwrap_or_else(|_| unreachable!());
    assert!(runtime
        .plan_entitlement_refresh(wire::EntitlementFetchReason::SignIn, 1_000)
        .is_some());
    runtime.deliver_entitlement_fetch_result(Some(&entitlement_summary(false)), 1_000);

    let step = runtime
        .submit_account_command(&sign_out_command(), 1_000)
        .unwrap_or_else(|_| unreachable!());
    let effect = step
        .effects
        .first()
        .unwrap_or_else(|| unreachable!("a signed-in sign-out plans a revocation"));
    let completion = wire::EffectResult {
        operation: effect.operation.clone(),
        effect_id: effect.effect_id.clone(),
        status: wire::EffectStatus::Completed,
        kind: wire::EffectKind::NetworkRequest,
        storage: None,
        observation: None,
        model: None,
        network: Some(wire::NetworkEffectResult {
            operation_kind: wire::AccountNetworkOperation::RevokeSession,
            authorization_code_session: None,
            native_credential_session: None,
            email_link: None,
            refreshed_session: None,
            revoked_session: Some(wire::RevokedSessionResult {
                session_handle: "session-handle-1".to_owned(),
                deleted: true,
            }),
            entitlement_summary: None,
        }),
        browser_action: None,
        tool: None,
        secure_store: None,
        auth_surface: None,
        permission: None,
        asset_delivery: None,
        catalog: None,
        provider_listing: None,
        composer_completion: None,
        custom_endpoint_probe: None,
    };
    assert!(runtime
        .deliver_account_effect_result(&completion, 1_000)
        .is_ok());
    // The session went and the entitlement went with it, in one delivery.
    let status = runtime
        .project_core_status(core_api_types::CoreAvailability::Ready)
        .unwrap_or_else(|_| unreachable!());
    assert!(status
        .auth_state
        .is_some_and(|auth| auth.account.is_none() && auth.entitlement.is_none()));
    // And the next sign-in starts from nothing, immediately.
    assert!(runtime
        .plan_entitlement_refresh(wire::EntitlementFetchReason::SignIn, 1_001)
        .is_some());
}
