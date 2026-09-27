// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use bip_types::identity::{ApprovalReceiptReference, ProfileId, TaskId};
use task_engine::action::{ActionIntent, BrowserIntent};
use task_engine::{
    ActionProposal, ActionState, BrowserSessionId, BudgetDefaults, Command, CommandEnvelope,
    ControlMode, Effect, IdSource, IdempotencyKey, ManualClock, Milestone, PauseCause,
    PolicyVersion, Reducer, ScopePreview, SequentialIds, SourceScope, StepState, TaskBudgets,
    TaskKind, TaskSeed, TaskSnapshot, TaskTemplateId, TraceId,
};

mod discovery;
mod field_values;
mod transcript;

use super::{classify_action_state, is_projected_finished_step, ActionProjectionClass};
use super::{
    InitialConsentAdmission, PortError, ReducerFactory, TaskEngineFactory, TaskEngineLoad,
    TaskEnginePort, TaskIdEntropy,
};

fn entropy() -> TaskIdEntropy {
    TaskIdEntropy::new(core::array::from_fn(|index| {
        u8::try_from(index).unwrap_or_default()
    }))
    .unwrap_or_else(|_| unreachable!())
}

fn seed() -> TaskSeed {
    TaskSeed {
        task_id: TaskId::new("task-replay"),
        workspace_id: None,
        browser_profile_id: ProfileId::new("profile-1"),
        kind: TaskKind::Research,
        user_goal: "goal that must not appear in factory debug".to_owned(),
        control_mode: ControlMode::Assistant,
        snapshot: TaskSnapshot {
            template_id: TaskTemplateId::WebErrand,
            assistant_config_version: 1,
            skill_version_id: None,
            builtin_skill: None,
            tool_allowlist: Vec::new(),
            capability_policy_version: PolicyVersion(1),
            provider_route: None,
            consented_sources: Vec::new(),
            source_discovery_enabled: false,
            remaining_new_source_cap: 0,
            discovery_tab_id: None,
            library_refresh: None,
            browser_session_id: BrowserSessionId::new("browser-session-1")
                .unwrap_or_else(|_| unreachable!()),
            milestone: Milestone::M3,
        },
        budgets: TaskBudgets::none(),
        deadline: None,
        deadline_utc: None,
        predecessor_task_id: None,
    }
}

fn initial_consent() -> InitialConsentAdmission {
    InitialConsentAdmission {
        preview: ScopePreview {
            scope: SourceScope::new(),
            sources: Vec::new(),
            source_discovery_enabled: true,
            new_source_cap: 1,
            provider_route: None,
            budgets: TaskBudgets::none().with(task_engine::BudgetKind::MaxSources, 1),
        },
        receipt: ApprovalReceiptReference::new("initial-consent"),
        start_key: IdempotencyKey::new("initial-start"),
        start_trace: TraceId::new("initial-start-trace"),
        consent_key: IdempotencyKey::new("initial-accept"),
        consent_trace: TraceId::new("initial-accept-trace"),
    }
}

fn ids(task_id: &TaskId) -> Result<Box<dyn IdSource>, PortError> {
    if task_id.as_str().is_empty() {
        return Err(PortError::InvalidInput);
    }
    Ok(Box::new(SequentialIds::new()))
}

#[test]
fn reducer_factory_uses_one_typed_seed_for_creation_and_replay() {
    let seed = seed();
    let defaults = BudgetDefaults::uniform(8);
    let clock = ManualClock::at(1_000);
    let creation_key = IdempotencyKey::new("create-task");
    let trace_id = TraceId::new("trace-task");
    let original = Reducer::create(
        seed.clone(),
        defaults.clone(),
        clock,
        SequentialIds::new(),
        creation_key.clone(),
        trace_id.clone(),
    );
    let journal = original.journal().clone();
    let mut factory = ReducerFactory::new(defaults, clock, ids);

    let fresh = factory
        .open(TaskEngineLoad::fresh(
            seed.clone(),
            creation_key,
            trace_id,
            entropy(),
            initial_consent(),
        ))
        .unwrap_or_else(|_| unreachable!());
    let (_, recovery, initial_effects) = fresh.into_parts();
    assert!(recovery.is_none());
    assert_eq!(
        initial_effects,
        vec![Effect::PrepareDiscoveryTab {
            browser_session_id: BrowserSessionId::new("browser-session-1")
                .unwrap_or_else(|_| unreachable!()),
            remaining_new_source_cap: 1,
        }]
    );

    let replayed = factory.open(TaskEngineLoad::replay(seed, journal, entropy()));
    assert!(matches!(
        replayed,
        Ok(opened)
            if opened.recovery.as_ref().is_some_and(|recovery| recovery.revision == 1)
    ));
}

#[test]
fn typed_load_debug_redacts_the_user_goal() {
    let load = TaskEngineLoad::fresh(
        seed(),
        IdempotencyKey::new("create-task"),
        TraceId::new("trace-task"),
        entropy(),
        initial_consent(),
    );
    assert!(!format!("{load:?}").contains("goal that must not appear"));
}

#[test]
fn every_action_state_has_an_explicit_projection_class() {
    let expected = [
        ActionProjectionClass::NoUiOverride,
        ActionProjectionClass::WaitingApproval,
        ActionProjectionClass::NoUiOverride,
        ActionProjectionClass::NoUiOverride,
        ActionProjectionClass::NoUiOverride,
        ActionProjectionClass::NoUiOverride,
        ActionProjectionClass::NoUiOverride,
        ActionProjectionClass::NoUiOverride,
        ActionProjectionClass::NoUiOverride,
        ActionProjectionClass::OutcomeUnknown,
    ];
    assert_eq!(ActionState::ALL.len(), expected.len());
    for (state, projection) in ActionState::ALL.iter().zip(expected) {
        assert_eq!(classify_action_state(*state), projection);
    }
}

#[test]
fn every_step_state_is_counted_as_active_or_finished() {
    let expected = [false, false, false, false, true, true, true, true, true];
    assert_eq!(StepState::ALL.len(), expected.len());
    for (state, finished) in StepState::ALL.iter().zip(expected) {
        assert_eq!(is_projected_finished_step(*state), finished);
    }
}

#[test]
fn approval_required_is_immediately_a_projectable_consent_state() {
    let mut reducer = running_reducer();
    propose_read(&mut reducer, "approval-call", false);
    let action_id = reducer
        .actions()
        .find(|action| action.proposal().idempotency_key.as_str() == "approval-call")
        .map(|action| action.action_id().clone())
        .expect("the proposal minted an action");
    apply(
        &mut reducer,
        Command::RecordPolicyDecision {
            action_id: action_id.clone(),
            decision: Box::new(task_engine::ProposalDecision::RequireApproval),
            dispatch_id: None,
        },
        "approval-required",
    );

    let facts = reducer.view_facts().expect("approval state projects");
    assert_eq!(facts.state, task_engine::TaskState::AwaitingConsent);
    assert_eq!(
        facts.pending_action.map(|pending| pending.action_id),
        Some(action_id.as_str().to_owned())
    );
}

#[test]
fn pausing_a_pending_approval_projects_one_inert_settlement_state() {
    let mut reducer = running_reducer();
    propose_read(&mut reducer, "approval-then-pause", false);
    let action_id = reducer
        .actions()
        .find(|action| action.proposal().idempotency_key.as_str() == "approval-then-pause")
        .map(|action| action.action_id().clone())
        .expect("the proposal minted an action");
    apply(
        &mut reducer,
        Command::RecordPolicyDecision {
            action_id: action_id.clone(),
            decision: Box::new(task_engine::ProposalDecision::RequireApproval),
            dispatch_id: None,
        },
        "approval-required-before-pause",
    );
    apply(
        &mut reducer,
        Command::PauseTask {
            cause: PauseCause::User,
        },
        "pause-pending-approval",
    );

    let facts = reducer
        .view_facts()
        .expect("pausing state remains projectable");
    assert_eq!(facts.state, task_engine::TaskState::Pausing);
    assert!(facts.pending_action.is_none());
    assert!(facts.committed_action_approvals.is_empty());
    assert_eq!(
        reducer
            .action(&action_id)
            .map(task_engine::ActionRecord::state),
        Some(ActionState::Cancelled)
    );
}

#[test]
fn pausing_a_field_value_wait_projects_without_the_revoked_request() {
    let mut reducer = running_reducer();
    let request_id = task_engine::field_value_request_id_for_call(0, 0);
    apply(
        &mut reducer,
        Command::RequestFieldValues {
            request_id,
            tab_id: bip_types::identity::TabId::new("tab-1"),
            node_id: bip_types::identity::SemanticNodeId::new("node-1"),
            companion_node_ids: task_engine::FieldNodeIds::none(),
        },
        "request-values-before-pause",
    );
    apply(
        &mut reducer,
        Command::PauseTask {
            cause: PauseCause::User,
        },
        "pause-field-value-wait",
    );

    let facts = reducer
        .view_facts()
        .expect("pausing field-value wait remains projectable");
    assert_eq!(facts.state, task_engine::TaskState::Pausing);
    assert!(facts.pending_field_values.is_none());
}

#[test]
fn stopping_before_initial_consent_projects_without_inventing_authority() {
    let mut reducer = draft_reducer();
    apply(
        &mut reducer,
        Command::StartTask(initial_consent().preview),
        "preview-before-stop",
    );
    apply(&mut reducer, Command::CancelTask, "stop-before-consent");
    let facts = reducer.view_facts().expect("unconsented stop projects");
    assert_eq!(facts.state, task_engine::TaskState::Cancelling);
    assert!(facts.accepted_consent.is_none());
}

fn draft_reducer() -> Reducer<ManualClock, SequentialIds> {
    Reducer::create(
        seed(),
        BudgetDefaults::uniform(8),
        ManualClock::at(1_000),
        SequentialIds::new(),
        IdempotencyKey::new("create-task"),
        TraceId::new("trace-task"),
    )
}

/// A task in `RUNNING`, built the way the runtime builds one.
fn running_reducer() -> Reducer<ManualClock, SequentialIds> {
    let mut reducer = draft_reducer();
    super::apply_initial_consent(&mut reducer, initial_consent()).expect("consent");
    apply(&mut reducer, Command::ExecutorStarted, "executor-started");
    reducer
}

fn apply(reducer: &mut Reducer<ManualClock, SequentialIds>, command: Command, key: &str) {
    let revision = reducer.task().revision();
    reducer
        .apply(CommandEnvelope::new(
            IdempotencyKey::new(key),
            revision,
            TraceId::new(key),
            command,
        ))
        .unwrap_or_else(|refusal| panic!("{key} refused: {refusal:?}"));
}

/// Proposes one observation under `key` and, when `outcome` is given, settles
/// it.
fn propose_read(reducer: &mut Reducer<ManualClock, SequentialIds>, key: &str, settle: bool) {
    let proposal = ActionProposal::new(
        ActionIntent::Browser(BrowserIntent::DomRead {
            tab: bip_types::identity::TabId::new("tab-1"),
            target: None,
        }),
        None,
        IdempotencyKey::new(key),
        false,
        None,
        bip_types::identity::ContentDigest {
            algorithm: bip_types::identity::DigestAlgorithm::Sha256,
            value: "0".repeat(64),
        },
    );
    apply(
        reducer,
        Command::ProposeAction(Box::new(proposal)),
        &format!("propose-{key}"),
    );
    if !settle {
        return;
    }
    let action_id = reducer
        .actions()
        .find(|action| action.proposal().idempotency_key.as_str() == key)
        .map(|action| action.action_id().clone())
        .expect("the proposal minted an action");
    let dispatch = bip_types::identity::DispatchId::new(format!("dispatch-{key}"));
    apply(
        reducer,
        Command::RecordPolicyDecision {
            action_id: action_id.clone(),
            decision: Box::new(task_engine::ProposalDecision::Authorize(
                task_engine::Authorization {
                    capability_id: task_engine::CapabilityId::new("cap-0"),
                },
            )),
            dispatch_id: Some(dispatch.clone()),
        },
        &format!("authorize-{key}"),
    );
    apply(
        reducer,
        Command::RecordActionOutcome {
            action_id,
            // A refusal rather than a verified read, because a verified
            // observation carries evidence this test has no business
            // inventing — and a failed call is the half of the transcript
            // worth asserting anyway.
            outcome: Box::new(task_engine::ActionOutcome {
                code: bip_types::ActionResultCode::NodeGone,
                dispatch_id: Some(dispatch),
                observed_at: bip_types::identity::MonotonicMillis(10),
                observation: None,
                discovered_source: None,
            }),
        },
        &format!("outcome-{key}"),
    );
}
