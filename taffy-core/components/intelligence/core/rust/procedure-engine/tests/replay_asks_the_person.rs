// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! A replayed step that needs a value only the person can give.
//!
//! Two refusals stood between a recorded fill and a replay of it, and neither
//! had a test: `UnservedVerb` for every dispatch that is not a browser action
//! — and the verb that asks a person for values reaches the person — and
//! `HandleBindingUnavailable` for every argument that is not a literal — and a
//! recorded fill's value is a request to the person. Relaxing one would have
//! left the other standing, so this file drives both halves: the case that is
//! now served, and every neighbouring case that must still refuse.
//!
//! # The trap, and what was decided about it
//!
//! `Procedure::verbs` returns only the verbs the recorded steps name, and a
//! recording cannot contain the ask: recording is built from the browser's
//! live **capability** ledger, and a call that reaches the person spends no
//! capability. So the ask is a verb the record needs and can never name, and a
//! narrowing built from `verbs()` alone removes it.
//!
//! It is not settled by making the ask unconditional. Decision 0055 section 8
//! draws that line between a capability and the exit, and puts `user.ask` on
//! the capability side for exactly this reason: narrowing it costs a
//! capability and not the exit. Asking somebody to fill a form in is on the
//! same side. It is settled instead by deriving the allowlist from what the
//! record needs — `narrowing::required_verbs` — so a procedure whose fill takes
//! a person's value keeps the one name without which none of its steps can
//! run, and a procedure that fills nothing keeps nothing extra.

#![allow(
    clippy::unwrap_used,
    clippy::expect_used,
    clippy::panic,
    clippy::indexing_slicing
)]

#[path = "common/reviewed_task.rs"]
mod reviewed_task;

use bip_types::action::PostconditionKind;
use bip_types::snapshot::SemanticRole;
use procedure_engine::field::{FieldPurpose, ObservedFields};
use procedure_engine::matching::{MatchClause, MatchCondition};
use procedure_engine::narrowing::REQUEST_VALUES_VERB;
use procedure_engine::record::{Procedure, ProcedureId, ProcedureProvenance, ProcedureScope};
use procedure_engine::replay::{next_procedure_command, ReplayRefusal};
use procedure_engine::rules::validate;
use procedure_engine::status::{transition, LifecycleActor, ProcedureStatus};
use procedure_engine::step::{ProcedureStep, StepArgument, StepValue};
use reviewed_task::{seed, Digest, Driver, FIXTURE_ORIGIN};
use task_engine::tool::ArgumentValue;
use task_engine::{Command, Milestone, TaskState};

/// The purpose the fixture's fill is about.
const PURPOSE: FieldPurpose = FieldPurpose::OneTimeCode;

/// A task that may fill a form and may ask the person for the values.
fn driver(allowlist: &[&str]) -> Driver {
    let mut seed = seed();
    seed.snapshot.milestone = Milestone::M5;
    seed.snapshot.tool_allowlist = allowlist.iter().map(|name| (*name).to_owned()).collect();
    let mut driver = Driver::from_seed(seed);
    driver.start();
    driver
}

/// The whole allowlist a filling procedure needs.
fn full_allowlist() -> Vec<&'static str> {
    vec!["browser.form.fill", REQUEST_VALUES_VERB, "user.handover"]
}

/// An `Active` one-step procedure over `step`, about the fixture's origin.
///
/// One step because the reviewed template's plan is two steps long and a
/// replayed procedure works through all but the last of them.
fn procedure_of(step: ProcedureStep) -> Procedure {
    let mut procedure = Procedure::draft(
        ProcedureId::new("fill.the.usual-form").unwrap(),
        ProcedureScope::for_origin(FIXTURE_ORIGIN).unwrap(),
        MatchCondition::new(vec![MatchClause::RolePresent(SemanticRole::Document)]),
        vec![step],
        ProcedureProvenance::RecordedFromTask,
    );
    procedure.status = transition(
        procedure.status,
        ProcedureStatus::Active,
        LifecycleActor::Person,
    )
    .unwrap();
    procedure
}

/// A fill whose field is named the way `field` says and whose value is the
/// person's.
fn fill(field: StepValue) -> ProcedureStep {
    ProcedureStep::new("browser.form.fill", PostconditionKind::NodeValueChanged)
        .taking(vec![
            StepArgument::new("field", field),
            StepArgument::new("value_from", StepValue::FromPerson { purpose: PURPOSE }),
        ])
        .filling(PURPOSE)
}

/// The page agrees with the record about the one field the step is about.
fn agreeing() -> [FieldPurpose; 1] {
    [PURPOSE]
}

/// Drives the task to the point where the step itself is what happens next.
///
/// Executor started, the plan set, and the first plan step moved to running:
/// three commands neither half of this file is about.
fn up_to_the_step(driver: &mut Driver, procedure: &Procedure, observed: &[FieldPurpose]) {
    for _ in 0..3 {
        let command = next_procedure_command(
            &driver.reducer,
            procedure,
            ObservedFields::per_step(observed),
            &Digest,
        )
        .expect("the procedure can say what happens next")
        .expect("the procedure is not waiting yet");
        driver.apply(command);
    }
}

fn next(
    driver: &Driver,
    procedure: &Procedure,
    observed: &[FieldPurpose],
) -> Result<Option<Command>, ReplayRefusal> {
    next_procedure_command(
        &driver.reducer,
        procedure,
        ObservedFields::per_step(observed),
        &Digest,
    )
}

#[test]
fn the_ask_comes_first_and_the_fill_still_needs_a_live_node_binding() {
    // The served case, end to end. The record names a field's *purpose* and
    // never what was typed into it, so there is nothing stale to bind: the
    // person is asked again and the position is minted at replay, in the
    // browser, out of an answer this process never sees.
    let procedure = procedure_of(fill(StepValue::Literal(ArgumentValue::Handle(1))));
    assert_eq!(validate(&procedure, Milestone::M5), Ok(()));
    let mut driver = driver(&full_allowlist());
    up_to_the_step(&mut driver, &procedure, &agreeing());

    // The ask, and not the fill.
    assert_eq!(
        next(&driver, &procedure, &agreeing()),
        Ok(Some(Command::RequestUserInput))
    );
    driver.apply(Command::RequestUserInput);
    assert_eq!(driver.reducer.task().state(), TaskState::WaitingUser);
    // While the person has it, the procedure has nothing to say.
    assert_eq!(next(&driver, &procedure, &agreeing()), Ok(None));

    driver.apply(Command::SupplyUserInput);
    assert_eq!(driver.reducer.task().state(), TaskState::Running);
    // The person's answer does not mint a DOM node binding. A literal number
    // from the record must not be treated as one, so replay stops at the
    // distinct handle seam instead of proposing a fill against an invented
    // target. It also does not ask a second time.
    assert_eq!(
        next(&driver, &procedure, &agreeing()),
        Err(ReplayRefusal::HandleBindingUnavailable)
    );
}

#[test]
fn the_ask_is_still_made_for_a_step_whose_other_half_will_refuse() {
    // The recorded shape: a field named by an earlier step's handle, and a
    // value from the person. Opening one of the two refusals must not read as
    // opening the other, so this drives past the ask and lands on the handle —
    // which is still shut, because the table that would bind one is a model
    // turn's and a replay has none.
    let procedure = procedure_of(fill(StepValue::FromEarlierStep { step: 0 }));
    let mut driver = driver(&full_allowlist());
    up_to_the_step(&mut driver, &procedure, &agreeing());

    assert_eq!(
        next(&driver, &procedure, &agreeing()),
        Ok(Some(Command::RequestUserInput))
    );
    driver.apply(Command::RequestUserInput);
    driver.apply(Command::SupplyUserInput);
    assert_eq!(
        next(&driver, &procedure, &agreeing()),
        Err(ReplayRefusal::HandleBindingUnavailable)
    );
}

#[test]
fn a_persons_value_at_a_parameter_that_takes_bytes_is_refused_by_its_own_name() {
    // A person can be asked for two different things and they are not
    // interchangeable. A search query is text the assistant then reads; a
    // field value is minted in the browser's vault and the assistant only ever
    // names its position. A replay has no model turn to read text back to, and
    // the fields whose values only a person can give are the ones whose bytes
    // never enter the AI data plane at all.
    let procedure = procedure_of(
        ProcedureStep::new("browser.search", PostconditionKind::SearchResultState).taking(vec![
            StepArgument::new(
                "query",
                StepValue::FromPerson {
                    purpose: FieldPurpose::SearchTerms,
                },
            ),
        ]),
    );
    assert_eq!(validate(&procedure, Milestone::M5), Ok(()));
    let mut driver = driver(&["browser.search", REQUEST_VALUES_VERB]);
    up_to_the_step(&mut driver, &procedure, &[FieldPurpose::Unknown]);
    assert_eq!(
        next(&driver, &procedure, &[FieldPurpose::Unknown]),
        Err(ReplayRefusal::PersonValueIsNotAPosition)
    );
}

#[test]
fn every_other_verb_that_does_not_reach_the_page_is_still_unserved() {
    // Text asks and loop tools still need a model turn. Explicit handover is
    // served separately as a replay step and has no model-visible answer.
    for (verb, argument, postcondition) in [
        (
            "user.ask",
            StepArgument::literal("subject", ArgumentValue::Text("which one".to_owned())),
            PostconditionKind::NoMutation,
        ),
        (
            "tool.search",
            StepArgument::literal("query", ArgumentValue::Text("a tool".to_owned())),
            PostconditionKind::NoMutation,
        ),
    ] {
        let procedure =
            procedure_of(ProcedureStep::new(verb, postcondition).taking(vec![argument]));
        assert_eq!(validate(&procedure, Milestone::M5), Ok(()), "{verb}");
        let mut driver = driver(&[verb, REQUEST_VALUES_VERB]);
        up_to_the_step(&mut driver, &procedure, &[FieldPurpose::Unknown]);
        assert_eq!(
            next(&driver, &procedure, &[FieldPurpose::Unknown]),
            Err(ReplayRefusal::UnservedVerb),
            "{verb}"
        );
    }
}

#[test]
fn a_task_that_may_not_ask_refuses_before_it_proposes_anything() {
    // The trap, from the other side. Every verb this record *names* is
    // admitted, and the one it cannot name is not — so a replay that went
    // ahead would ask with a verb the narrowing had removed. The refusal is
    // the narrowing's own, and it happens before any command is emitted.
    let procedure = procedure_of(fill(StepValue::Literal(ArgumentValue::Handle(1))));
    let filler = driver(&["browser.form.fill"]);
    assert_eq!(
        next(&filler, &procedure, &agreeing()),
        Err(ReplayRefusal::VerbNotAdmitted)
    );
    // And a record that needs no ask replays on the same allowlist, so the
    // refusal is about what this record needs rather than about the list.
    let reading = procedure_of(ProcedureStep::new(
        "browser.dom.read",
        PostconditionKind::NoMutation,
    ));
    let reader = driver(&["browser.dom.read"]);
    assert_eq!(
        next(&reader, &reading, &[FieldPurpose::Unknown]),
        Ok(Some(Command::ExecutorStarted))
    );
}

#[test]
fn a_field_the_page_and_the_record_disagree_about_is_handed_to_the_person() {
    // Rule 6, reached from the replay path rather than from its own unit
    // tests: an unclassified field is handed to the person, and so is one that
    // is now something else. It is decided before the ask, because a step the
    // page disagrees about must not interrupt somebody for a value and *then*
    // hand them the page.
    let procedure = procedure_of(fill(StepValue::Literal(ArgumentValue::Handle(1))));
    let mut driver = driver(&full_allowlist());
    up_to_the_step(&mut driver, &procedure, &agreeing());

    // Nothing was classified: the page half of the comparison is missing, and
    // a missing half is not a match.
    let Ok(Some(Command::RequestHandover {
        handover_id: unclassified,
    })) = next_procedure_command(&driver.reducer, &procedure, ObservedFields::none(), &Digest)
    else {
        panic!("an unclassified field is handed to the person");
    };
    // The field is now something else.
    let changed = [FieldPurpose::EmailAddress];
    let Ok(Some(Command::RequestHandover { handover_id: moved })) =
        next(&driver, &procedure, &changed)
    else {
        panic!("a field that became something else is handed to the person");
    };
    // Derived rather than minted, so asking twice re-opens the same handover
    // — and the two reasons are two different things to tell a person, so they
    // are two different handovers.
    assert_eq!(
        next(&driver, &procedure, &changed),
        Ok(Some(Command::RequestHandover {
            handover_id: moved.clone()
        }))
    );
    assert_ne!(unclassified, moved);
    assert!(unclassified.as_str().contains("unclassified"));
    assert!(moved.as_str().contains("purpose_changed"));

    // And the page agreeing is what lets the step run at all.
    assert_eq!(
        next(&driver, &procedure, &agreeing()),
        Ok(Some(Command::RequestUserInput))
    );
}

#[test]
fn a_step_that_fills_nothing_is_unaffected_by_what_was_classified() {
    // Rule 6 is about a fill. A read is proposed whatever the classifier says
    // about a field it is not about, and `ObservedFields::none` — which is
    // what every caller passes today — must not turn every replay into a
    // handover.
    let procedure = procedure_of(ProcedureStep::new(
        "browser.dom.read",
        PostconditionKind::NoMutation,
    ));
    let mut driver = driver(&["browser.dom.read"]);
    up_to_the_step(&mut driver, &procedure, &[]);
    let Ok(Some(Command::ProposeAction(proposal))) =
        next_procedure_command(&driver.reducer, &procedure, ObservedFields::none(), &Digest)
    else {
        panic!("a read is proposed with nothing classified");
    };
    assert_eq!(proposal.tool_name(), "browser.dom.read");
}
