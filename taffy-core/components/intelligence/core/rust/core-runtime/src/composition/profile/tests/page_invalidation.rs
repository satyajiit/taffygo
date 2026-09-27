// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! A settled page mutation retires bytes, not conversation or other tabs.

use bip_types::identity::{ContentDigest, DigestAlgorithm, DispatchId, MonotonicMillis, TabId};
use task_engine::action::{ActionIntent, BrowserIntent, DisclosureState, ScrollDirection};
use task_engine::{
    ActionOutcome, ActionProposal, Authorization, CapabilityId, Command, Effect, IdempotencyKey,
    ProposalDecision,
};

use super::recording::support;
use crate::contract::Completion;
use crate::ports::TaskEngineLoad;

fn driver() -> crate::ProfileServiceRuntime {
    let mut load = support::load();
    let TaskEngineLoad::Fresh { seed, .. } = &mut load else {
        panic!("fresh fixture")
    };
    seed.snapshot.tool_allowlist.extend(
        [
            "browser.dom.click",
            "browser.dom.focus",
            "browser.dom.scroll",
            "browser.form.fill",
            "browser.form.select",
            "browser.form.toggle",
            "browser.form.submit",
            "browser.tabs",
        ]
        .map(str::to_owned),
    );
    support::driver_for(load)
}

fn stage_outcome(
    runtime: &mut crate::ProfileServiceRuntime,
    intent: BrowserIntent,
    outcome_code: bip_types::ActionResultCode,
) -> crate::contract::OperationEnvelope<crate::contract::EffectRequest> {
    let proposal = ActionProposal::new(
        ActionIntent::Browser(intent),
        None,
        IdempotencyKey::new("mutate-page"),
        true,
        None,
        ContentDigest {
            algorithm: DigestAlgorithm::Sha256,
            value: "a".repeat(64),
        },
    );
    let accepted = support::apply(runtime, Command::ProposeAction(Box::new(proposal)));
    let Effect::AskPolicy { action_id } = accepted.effects.first().unwrap() else {
        panic!("mutation uses ordinary policy")
    };
    let dispatch = DispatchId::new("mutation-dispatch");
    support::apply(
        runtime,
        Command::RecordPolicyDecision {
            action_id: action_id.clone(),
            decision: Box::new(ProposalDecision::Authorize(Authorization {
                capability_id: CapabilityId::new("mutation-capability"),
            })),
            dispatch_id: Some(dispatch.clone()),
        },
    );
    support::begin(
        runtime,
        Command::RecordActionOutcome {
            action_id: action_id.clone(),
            outcome: Box::new(ActionOutcome {
                code: outcome_code,
                dispatch_id: Some(dispatch),
                observed_at: MonotonicMillis(1),
                observation: None,
                discovered_source: None,
            }),
        },
    )
}

fn source_ids(runtime: &crate::ProfileServiceRuntime) -> Vec<task_engine::SourceId> {
    runtime
        .core()
        .loop_state(support::task_id().as_str())
        .unwrap()
        .page
        .source_ids()
        .collect::<Vec<_>>()
}

fn check_mutation(
    make_intent: impl FnOnce(task_engine::action::ObservedNodeHandle) -> BrowserIntent,
    outcome_code: bip_types::ActionResultCode,
) {
    check_mutation_with_other_tab(make_intent, outcome_code, false);
}

fn check_mutation_with_other_tab(
    make_intent: impl FnOnce(task_engine::action::ObservedNodeHandle) -> BrowserIntent,
    outcome_code: bip_types::ActionResultCode,
    changes_other_tab: bool,
) {
    let mut runtime = driver();
    let (evidence, handle) = support::install_page(&mut runtime);
    support::action(
        &mut runtime,
        BrowserIntent::DomRead {
            tab: support::source().tab_id,
            target: None,
        },
        Some(evidence),
    );
    let other_source = task_engine::ConsentedSource {
        source_id: task_engine::SourceId::from_bytes([4; 16]),
        tab_id: TabId::new("other-tab"),
        normalized_origin: "https://other.test".to_owned(),
        canonical_locator: None,
    };
    support::install_page_for(
        &mut runtime,
        &other_source,
        "other-epoch",
        task_engine::ObservationCompleteness::Complete,
    );
    let state = runtime
        .core_mut()
        .loop_state_mut(support::task_id().as_str());
    state.person_answer = Some("keep going".to_owned());
    state.ask_prompt = Some("keep this question".to_owned());
    let preview = state.page.preview();
    let issued = preview.handles.issued();
    let original_handles = preview.handles.clone();
    assert!(issued > 0);
    state.page.commit_handles(preview.handles);

    let pending = stage_outcome(&mut runtime, make_intent(handle), outcome_code);
    assert_eq!(
        source_ids(&runtime).len(),
        2,
        "storage acknowledgment is pending"
    );
    let wrong = pending.with_body(Completion::StorageCommitted {
        committed_revision: pending.task_revision + 1,
    });
    assert!(runtime
        .core_mut()
        .complete_commit(&support::task_id(), wrong, 2)
        .is_err());
    assert_eq!(source_ids(&runtime).len(), 2);
    support::commit(&mut runtime, &pending);
    let (changed_source, unchanged_source) = if changes_other_tab {
        (&other_source, support::source())
    } else {
        (&support::source(), other_source.clone())
    };
    assert_eq!(source_ids(&runtime), [unchanged_source.source_id]);
    let state = runtime
        .core()
        .loop_state(support::task_id().as_str())
        .unwrap();
    assert_eq!(state.person_answer.as_deref(), Some("keep going"));
    assert_eq!(state.ask_prompt.as_deref(), Some("keep this question"));
    let retained_handles = state.page.preview().handles;
    for number in 0..issued {
        assert_eq!(
            retained_handles.resolve_value(number),
            original_handles.resolve_value(number)
        );
    }
    assert!(state
        .page
        .matching_observations(core::slice::from_ref(changed_source))
        .is_empty());
    if !changes_other_tab {
        let task = runtime.core().task(&support::task_id()).unwrap();
        assert!(matches!(
            loop_kernel::walk::next_pre_model_observation_envelope(
                task,
                &state.page.matching_observations(&[support::source()]),
                &crate::account::crypto::ReferenceSha256,
            ),
            Ok(loop_kernel::walk::PreModelObservationEnvelope::Command(_))
        ));
    }
    support::install_page_for(
        &mut runtime,
        changed_source,
        "after-mutation",
        task_engine::ObservationCompleteness::Complete,
    );
    let state = runtime
        .core()
        .loop_state(support::task_id().as_str())
        .unwrap();
    assert!(state.page.preview().handles.issued() > issued);
}

#[test]
fn form_submission_withdraws_old_context_only_after_exact_commit() {
    check_mutation(
        |_| BrowserIntent::FormSubmit {
            tab: support::source().tab_id,
            control: bip_types::identity::SemanticNodeId::new("submit"),
        },
        bip_types::ActionResultCode::Verified,
    );
}

#[test]
fn disclosure_click_withdraws_old_context_only_after_exact_commit() {
    check_mutation(
        |target| BrowserIntent::DomClick {
            target,
            expected_state: Some(DisclosureState::Expanded),
        },
        bip_types::ActionResultCode::Verified,
    );
}

#[test]
fn focus_scroll_and_form_edits_need_new_page_context() {
    for operation in 0..5 {
        check_mutation(
            |target| match operation {
                0 => BrowserIntent::DomFocus { target },
                1 => BrowserIntent::DomScroll {
                    tab: support::source().tab_id,
                    direction: ScrollDirection::ToNode,
                    target: Some(bip_types::identity::SemanticNodeId::new("download")),
                },
                2 => BrowserIntent::FormToggle {
                    tab: support::source().tab_id,
                    field: bip_types::identity::SemanticNodeId::new("toggle"),
                    checked: true,
                },
                3 => BrowserIntent::FormFill {
                    tab: support::source().tab_id,
                    field: bip_types::identity::SemanticNodeId::new("field"),
                    value_request: task_engine::FieldValueRequestId::new("values").unwrap(),
                    value_from: 0,
                },
                _ => BrowserIntent::FormSelect {
                    tab: support::source().tab_id,
                    field: bip_types::identity::SemanticNodeId::new("choice"),
                    value_request: task_engine::FieldValueRequestId::new("values").unwrap(),
                    value_from: 0,
                },
            },
            bip_types::ActionResultCode::Verified,
        );
    }
}

#[test]
fn uncertain_mutation_outcome_also_retires_old_page_context() {
    check_mutation(
        |target| BrowserIntent::DomClick {
            target,
            expected_state: Some(DisclosureState::Expanded),
        },
        bip_types::ActionResultCode::OutcomeUnknown,
    );
}

#[test]
fn tab_activation_and_close_retire_the_target_instead_of_the_context() {
    use bip_types::identity::{FrameId, GraphRevision, PageEpoch};
    use task_engine::action::TaskTabTarget;

    for close in [false, true] {
        check_mutation_with_other_tab(
            |_| {
                let context = support::source().tab_id;
                let target = TaskTabTarget::new(
                    task_engine::BrowserSessionId::new("browser-session-1").unwrap(),
                    TabId::new("other-tab"),
                    FrameId::new("frame-1"),
                    PageEpoch::new("other-epoch"),
                    GraphRevision(1),
                );
                if close {
                    BrowserIntent::TabsClose { context, target }
                } else {
                    BrowserIntent::TabsActivate { context, target }
                }
            },
            bip_types::ActionResultCode::Verified,
            true,
        );
    }
}

#[test]
fn read_and_download_outcomes_keep_the_existing_observation() {
    let mut runtime = support::driver_for(support::load());
    let (evidence, target) = support::install_page(&mut runtime);
    support::action(
        &mut runtime,
        BrowserIntent::DomRead {
            tab: support::source().tab_id,
            target: None,
        },
        Some(evidence.clone()),
    );
    support::action(
        &mut runtime,
        BrowserIntent::DownloadFromLink {
            target,
            browser_session_id: task_engine::BrowserSessionId::new("browser-session-1").unwrap(),
        },
        None,
    );
    let state = runtime
        .core()
        .loop_state(support::task_id().as_str())
        .unwrap();
    assert!(state
        .page
        .has_exact_observation(&support::source(), &evidence));
}
