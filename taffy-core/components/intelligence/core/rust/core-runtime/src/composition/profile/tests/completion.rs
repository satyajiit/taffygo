// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Composer suggestions: what one is spent on, and what cancels it.

use core_service_types as wire;

use super::{built_runtime, composer_command, save_provider_credential_command};

#[test]
fn a_composer_suggestion_needs_a_credential_the_person_supplied() {
    use crate::composition::profile::completion::CompletionCommandError;

    // The managed route is entered with a token the browser mints per
    // call, so it leaves no stored credential to find. A profile
    // configured only that way must therefore reach exactly this refusal
    // on every keystroke rather than spending an allowance on text nobody
    // sent (decision 0097).
    let mut runtime = built_runtime();
    assert_eq!(
        runtime
            .submit_composer_completion_command(&composer_command("request-1", "Compare the "))
            .err(),
        Some(CompletionCommandError::NoUsableCredential)
    );
}

#[test]
fn a_composer_suggestion_is_task_less_and_carries_the_persons_own_words() {
    let mut runtime = built_runtime();
    runtime
        .submit_provider_command(&save_provider_credential_command(
            "anthropic",
            "handle-composer",
        ))
        .unwrap_or_else(|_| unreachable!());

    let plan = runtime
        .submit_composer_completion_command(&composer_command("request-1", "Compare the "))
        .unwrap_or_else(|_| unreachable!());
    assert_eq!(plan.superseded_effect_id, None);
    assert_eq!(plan.effect.kind, wire::EffectKind::ModelRequest);
    assert_eq!(plan.effect.retry_class, wire::RetryClass::Never);
    let request = plan
        .effect
        .model_request
        .as_ref()
        .unwrap_or_else(|| unreachable!("a composer effect carries a model request"));
    assert!(request.task_id.is_empty(), "no task owns a suggestion");
    assert!(!request.probe, "a suggestion is not a probe");
    assert_eq!(request.provider_id, "anthropic");
    assert_eq!(
        request.disclosure,
        wire::DisclosureClass::UserSelectedContent,
        "the person's own words, disclosed as theirs"
    );
    assert_eq!(
        request.credential_handle.as_deref(),
        Some("handle-composer")
    );
    assert_eq!(
        request.endpoint_kind,
        wire::ModelEndpointKind::CatalogOrigin
    );

    // A newer request displaces the older one and names what to stop.
    let second = runtime
        .submit_composer_completion_command(&composer_command("request-2", "Compare the two "))
        .unwrap_or_else(|_| unreachable!());
    assert_eq!(
        second.superseded_effect_id.as_deref(),
        Some(plan.effect.effect_id.as_str())
    );

    // The answer to the displaced request is discarded rather than shown.
    assert!(runtime
        .deliver_composer_completion_result(
            &plan.effect.effect_id,
            b"flights.",
            &plan.effect.operation,
        )
        .is_none());

    let push = runtime
        .deliver_composer_completion_result(
            &second.effect.effect_id,
            b"flights and tell me which is cheaper.\n",
            &second.effect.operation,
        )
        .unwrap_or_else(|| unreachable!("the awaited request is delivered"));
    assert_eq!(push.kind, wire::EffectKind::DeliverComposerCompletion);
    let carried = push
        .composer_completion
        .as_ref()
        .unwrap_or_else(|| unreachable!());
    assert_eq!(carried.request_id, "request-2");
    assert_eq!(
        carried.text.as_deref(),
        Some("flights and tell me which is cheaper.")
    );
}

#[test]
fn a_pinned_model_is_looked_up_under_the_provider_that_pinned_it() {
    // Two providers publish a model under the same identity, which is
    // ordinary for open-weights models. A pin names a model *of a
    // provider*, so resolving it by identity alone would spend one
    // person's key at a provider they did not choose.
    let mut runtime = built_runtime();
    runtime
        .submit_provider_command(&save_provider_credential_command(
            "together",
            "handle-together",
        ))
        .unwrap_or_else(|_| unreachable!());

    let mut pin = save_provider_credential_command("together", "unused");
    pin.operation.operation_id = "pin-1".to_owned();
    pin.operation.idempotency_key = "pin-1-key".to_owned();
    pin.kind = wire::CoreServiceCommandKind::SetProviderModelPreference;
    pin.save_provider_credential = None;
    pin.set_provider_model_preference = Some(wire::SetProviderModelPreferenceCommand {
        provider_id: "together".to_owned(),
        model_id: Some("zai-org/GLM-5.3".to_owned()),
        thinking: None,
    });
    runtime
        .submit_provider_command(&pin)
        .unwrap_or_else(|_| unreachable!());

    let plan = runtime
        .submit_composer_completion_command(&composer_command("request-1", "The flight "))
        .unwrap_or_else(|_| unreachable!());
    let request = plan
        .effect
        .model_request
        .as_ref()
        .unwrap_or_else(|| unreachable!());
    assert_eq!(request.model_id, "zai-org/GLM-5.3");
    assert_eq!(
        request.provider_id, "together",
        "baseten publishes the same identity, and the pin names together's"
    );
}

#[test]
fn a_composer_answer_that_says_nothing_pushes_nothing() {
    let mut runtime = built_runtime();
    runtime
        .submit_provider_command(&save_provider_credential_command(
            "anthropic",
            "handle-composer",
        ))
        .unwrap_or_else(|_| unreachable!());
    let plan = runtime
        .submit_composer_completion_command(&composer_command("request-1", "Done."))
        .unwrap_or_else(|_| unreachable!());
    assert!(
        runtime
            .deliver_composer_completion_result(
                &plan.effect.effect_id,
                b"   \n",
                &plan.effect.operation,
            )
            .is_none(),
        "an already-complete sentence gets no suggestion, and the surface is told nothing rather than shown an empty one"
    );
}

fn cancel_command(request_id: &str) -> wire::CoreServiceCommand {
    let mut command = save_provider_credential_command("anthropic", "unused");
    command.operation.operation_id = "composer-cancel-1".to_owned();
    command.operation.idempotency_key = "composer-cancel-1-key".to_owned();
    command.kind = wire::CoreServiceCommandKind::CancelComposerCompletion;
    command.save_provider_credential = None;
    command.cancel_composer_completion = Some(wire::CancelComposerCompletionCommand {
        request_id: request_id.to_owned(),
    });
    command
}

#[test]
fn cancelling_a_suggestion_names_the_dispatch_the_browser_must_stop() {
    let mut runtime = built_runtime();
    runtime
        .submit_provider_command(&save_provider_credential_command(
            "anthropic",
            "handle-composer",
        ))
        .unwrap_or_else(|_| unreachable!());
    let plan = runtime
        .submit_composer_completion_command(&composer_command("request-1", "Compare the "))
        .unwrap_or_else(|_| unreachable!());

    let cancelled = runtime
        .submit_composer_cancel_command(&cancel_command("request-1"))
        .unwrap_or_else(|_| unreachable!());
    assert_eq!(
        cancelled.withdrawn_effect_id.as_deref(),
        Some(plan.effect.effect_id.as_str()),
        "an effect nobody stops is an effect that still bills"
    );

    // Cancelling twice, or cancelling a request that was already answered,
    // withdraws nothing and is not a refusal: a surface that asked a moment
    // too late is not a surface that sent nonsense.
    let again = runtime
        .submit_composer_cancel_command(&cancel_command("request-1"))
        .unwrap_or_else(|_| unreachable!());
    assert_eq!(again.withdrawn_effect_id, None);
}

#[test]
fn a_cancelled_request_stops_the_answer_it_was_waiting_for() {
    let mut runtime = built_runtime();
    runtime
        .submit_provider_command(&save_provider_credential_command(
            "anthropic",
            "handle-composer",
        ))
        .unwrap_or_else(|_| unreachable!());
    let plan = runtime
        .submit_composer_completion_command(&composer_command("request-1", "Compare the "))
        .unwrap_or_else(|_| unreachable!());
    let _cancelled = runtime
        .submit_composer_cancel_command(&cancel_command("request-1"))
        .unwrap_or_else(|_| unreachable!());

    assert!(
        runtime
            .deliver_composer_completion_result(
                &plan.effect.effect_id,
                b"two options",
                &plan.effect.operation,
            )
            .is_none(),
        "a withdrawn request has nobody waiting, so an answer that arrives \
         anyway is discarded rather than pushed under a sentence that moved on"
    );
}
