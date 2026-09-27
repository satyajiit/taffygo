// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The six structural rules of decision 0055 section 4, named one at a time.
//!
//! The unit suites inside the crate check each rule where it lives. This file
//! exists for a different reason: it is where a reviewer can read the six rules
//! against the refusals that enforce them without knowing the module layout,
//! and it fails by name when one of them stops being enforced. Every test name
//! begins with the rule number it is about; a rule with two ways to break it
//! has two tests rather than one with two halves, so a failure says which half.
//!
//! Validation point 3 of the record — "a stored procedure whose step count
//! exceeds the bound, whose argument holds an expression, or whose tool name is
//! absent from the compiled-in table is refused at load, and the refusal names
//! which rule it broke" — is asserted here in exactly that form: every case
//! checks the named rule and not only that something was refused.

#![allow(
    clippy::unwrap_used,
    clippy::expect_used,
    clippy::panic,
    clippy::indexing_slicing
)]

use bip_types::action::PostconditionKind;
use bip_types::snapshot::SemanticRole;
use procedure_engine::{
    disposition, narrow, validate, FieldPurpose, HandoverReason, MatchClause, MatchCondition,
    NarrowingRefusal, Procedure, ProcedureId, ProcedureProvenance, ProcedureScope, ProcedureStep,
    RefusalReason, StepArgument, StepDisposition, StepValue, StructuralRule, MAX_MATCH_CLAUSES,
    MAX_PROCEDURE_STEPS,
};
use task_engine::tool::{ArgumentRefusalReason, ArgumentValue, EffectiveToolSet, Milestone};

fn condition() -> MatchCondition {
    MatchCondition::new(vec![MatchClause::RolePresent(SemanticRole::SearchField)])
}

fn with(steps: Vec<ProcedureStep>) -> Procedure {
    Procedure::draft(
        ProcedureId::new("book.the.usual-table").unwrap(),
        ProcedureScope::for_origin("https://example.test").unwrap(),
        condition(),
        steps,
        ProcedureProvenance::RecordedFromTask,
    )
}

fn open_link() -> ProcedureStep {
    ProcedureStep::new("browser.link.open", PostconditionKind::CommittedNavigation).taking(vec![
        StepArgument::new("node", StepValue::FromEarlierStep { step: 0 }),
    ])
}

fn query() -> ProcedureStep {
    ProcedureStep::new("browser.dom.query", PostconditionKind::NoMutation).taking(vec![
        StepArgument::literal("text", ArgumentValue::Text("the results".to_owned())),
    ])
}

/// The refusal a procedure earns, or a failure naming what it was accepted as.
fn refused(procedure: &Procedure, milestone: Milestone) -> (StructuralRule, RefusalReason) {
    match validate(procedure, milestone) {
        Ok(()) => unreachable!("this procedure must be refused"),
        Err(refusal) => {
            assert_eq!(refusal.rule, refusal.reason.rule());
            (refusal.rule, refusal.reason)
        }
    }
}

#[test]
fn a_well_formed_procedure_is_accepted_so_the_refusals_below_mean_something() {
    // Without this every test in the file would pass against a validator that
    // refused everything.
    assert_eq!(
        validate(&with(vec![query(), open_link()]), Milestone::M3),
        Ok(())
    );
}

#[test]
fn rule_one_no_expressions_a_literal_written_for_substitution_is_refused() {
    let step = ProcedureStep::new("browser.dom.query", PostconditionKind::NoMutation).taking(vec![
        StepArgument::literal(
            "text",
            ArgumentValue::Text("results for {{step0.value}}".to_owned()),
        ),
    ]);
    assert_eq!(
        refused(&with(vec![step]), Milestone::M3),
        (
            StructuralRule::NoExpressions,
            RefusalReason::ExpressionInLiteral
        )
    );
}

#[test]
fn rule_one_no_expressions_a_request_to_the_person_must_be_a_named_one() {
    let step =
        ProcedureStep::new("browser.search", PostconditionKind::CommittedNavigation).taking(vec![
            StepArgument::new(
                "query",
                StepValue::FromPerson {
                    purpose: FieldPurpose::Unknown,
                },
            ),
        ]);
    assert_eq!(
        refused(&with(vec![step]), Milestone::M3),
        (
            StructuralRule::NoExpressions,
            RefusalReason::UnnamedRequestToPerson
        )
    );
    // Named, and the same step is fine.
    let named = ProcedureStep::new("browser.search", PostconditionKind::CommittedNavigation)
        .taking(vec![StepArgument::new(
            "query",
            StepValue::FromPerson {
                purpose: FieldPurpose::SearchTerms,
            },
        )]);
    assert_eq!(validate(&with(vec![named]), Milestone::M3), Ok(()));
}

#[test]
fn rule_two_no_backward_edges_a_step_may_not_name_itself_or_a_later_one() {
    // Step 0 naming step 0 is a loop of one: a recorded mistake repeating for
    // ever without anybody deciding it should.
    assert_eq!(
        refused(&with(vec![open_link()]), Milestone::M3),
        (
            StructuralRule::NoBackwardEdges,
            RefusalReason::StepReferenceNotEarlier
        )
    );
    // Step 0 naming step 1 is a jump: satisfying it means running a later step
    // first, which is the same rule wearing a different word.
    let forward = ProcedureStep::new("browser.link.open", PostconditionKind::CommittedNavigation)
        .taking(vec![StepArgument::new(
            "node",
            StepValue::FromEarlierStep { step: 1 },
        )]);
    assert_eq!(
        refused(&with(vec![forward, query()]), Milestone::M3),
        (
            StructuralRule::NoBackwardEdges,
            RefusalReason::StepReferenceNotEarlier
        )
    );
    // Strictly earlier is the only shape that stands.
    assert_eq!(
        validate(&with(vec![query(), open_link()]), Milestone::M3),
        Ok(())
    );
}

#[test]
fn rule_three_no_new_verbs_the_name_must_resolve_in_the_compiled_in_table() {
    for (verb, expected) in [
        ("skill.do.the.thing", RefusalReason::UnregisteredVerb),
        ("device.clipboard.read", RefusalReason::ExcludedVerb),
        ("python.execute", RefusalReason::VerbNotYetBuilt),
    ] {
        let step = ProcedureStep::new(verb, PostconditionKind::NoMutation);
        assert_eq!(
            refused(&with(vec![step]), Milestone::M3),
            (StructuralRule::NoNewVerbs, expected),
            "{verb}"
        );
    }
}

#[test]
fn rule_three_no_new_verbs_covers_the_signature_and_not_only_the_name() {
    // A parameter the table does not declare is a verb the build does not have,
    // arriving through the arguments instead of through the name.
    let invented =
        ProcedureStep::new("browser.dom.query", PostconditionKind::NoMutation).taking(vec![
            StepArgument::literal("text", ArgumentValue::Text("the results".to_owned())),
            StepArgument::literal("selector", ArgumentValue::Text("h1".to_owned())),
        ]);
    assert_eq!(
        refused(&with(vec![invented]), Milestone::M3),
        (
            StructuralRule::NoNewVerbs,
            RefusalReason::Arguments(ArgumentRefusalReason::UnknownArgument)
        )
    );
    // And a required argument left out is refused rather than defaulted.
    let bare = ProcedureStep::new("browser.search", PostconditionKind::CommittedNavigation);
    assert_eq!(
        refused(&with(vec![bare]), Milestone::M3),
        (
            StructuralRule::NoNewVerbs,
            RefusalReason::Arguments(ArgumentRefusalReason::MissingRequiredArgument)
        )
    );
}

#[test]
fn rule_four_catalogued_phrases_a_condition_that_claims_nothing_is_refused() {
    let mut procedure = with(vec![query()]);
    procedure.condition = MatchCondition::new(Vec::new());
    assert_eq!(
        refused(&procedure, Milestone::M3),
        (
            StructuralRule::CataloguedPhrases,
            RefusalReason::EmptyMatchCondition
        )
    );

    let clauses = vec![MatchClause::RolePresent(SemanticRole::Button); MAX_MATCH_CLAUSES + 1];
    procedure.condition = MatchCondition::new(clauses);
    assert_eq!(
        refused(&procedure, Milestone::M3),
        (
            StructuralRule::CataloguedPhrases,
            RefusalReason::TooManyClauses
        )
    );
}

#[test]
fn rule_five_the_step_bound_refuses_rather_than_truncating() {
    let at_bound: Vec<ProcedureStep> = (0..MAX_PROCEDURE_STEPS).map(|_| query()).collect();
    assert_eq!(validate(&with(at_bound), Milestone::M3), Ok(()));

    let over: Vec<ProcedureStep> = (0..=MAX_PROCEDURE_STEPS).map(|_| query()).collect();
    let procedure = with(over);
    assert_eq!(
        refused(&procedure, Milestone::M3),
        (StructuralRule::StepBound, RefusalReason::TooManySteps)
    );
    // Truncating would have produced a *different* procedure still claiming
    // this one's identity, so the record is left exactly as it was.
    assert_eq!(procedure.steps.len(), MAX_PROCEDURE_STEPS + 1);

    assert_eq!(
        refused(&with(Vec::new()), Milestone::M3),
        (StructuralRule::StepBound, RefusalReason::NoSteps)
    );
}

#[test]
fn rule_six_an_unclassified_field_is_handed_to_the_person_and_never_guessed_at() {
    let fill = ProcedureStep::new("browser.form.fill", PostconditionKind::NodeValueChanged)
        .taking(vec![
            StepArgument::new("field", StepValue::FromEarlierStep { step: 0 }),
            StepArgument::new(
                "value_from",
                StepValue::FromPerson {
                    purpose: FieldPurpose::PostalCode,
                },
            ),
        ])
        .filling(FieldPurpose::PostalCode);
    let procedure = with(vec![query(), fill.clone()]);
    // The record is valid at the milestone that owns the verb: the downgrade is
    // a replay-time judgement about the page, not a defect in the record.
    assert_eq!(validate(&procedure, Milestone::M5), Ok(()));

    assert_eq!(
        disposition(&fill, FieldPurpose::Unknown),
        StepDisposition::HandToUser(HandoverReason::Unclassified)
    );
    assert_eq!(
        disposition(&fill, FieldPurpose::TelephoneNumber),
        StepDisposition::HandToUser(HandoverReason::PurposeChanged)
    );
    assert_eq!(
        disposition(&fill, FieldPurpose::PostalCode),
        StepDisposition::Replay
    );
}

#[test]
fn rule_six_a_fill_that_records_nothing_is_refused_before_it_can_be_replayed() {
    // The load-time half. A fill with no recorded purpose would reach the
    // replay-time judgement with nothing to compare against, and the answer
    // there would have to be invented.
    let fill =
        ProcedureStep::new("browser.form.fill", PostconditionKind::NodeValueChanged).taking(vec![
            StepArgument::new("field", StepValue::FromEarlierStep { step: 0 }),
            StepArgument::literal("value", ArgumentValue::Text("SW1A 1AA".to_owned())),
        ]);
    assert_eq!(
        refused(&with(vec![query(), fill]), Milestone::M5),
        (
            StructuralRule::UnclassifiedFieldHandedOver,
            RefusalReason::FillRecordsNoPurpose
        )
    );

    let not_a_fill = query().filling(FieldPurpose::SearchTerms);
    assert_eq!(
        refused(&with(vec![not_a_fill]), Milestone::M3),
        (
            StructuralRule::UnclassifiedFieldHandedOver,
            RefusalReason::PurposeOnAStepThatFillsNothing
        )
    );
}

#[test]
fn a_procedure_narrows_the_tool_set_and_can_never_widen_it() {
    let full = EffectiveToolSet::for_task(Milestone::M3, &[]);
    let procedure = with(vec![query(), open_link()]);
    let narrowed = narrow(&full, &procedure).unwrap();
    for name in narrowed.names() {
        assert!(full.names().contains(&name), "{name} was not in the set");
    }
    // Section 8: the exit survives a record that never mentions it.
    assert!(narrowed.admits("user.handover"));
    assert_eq!(
        narrowed.names(),
        vec!["browser.dom.query", "browser.link.open", "user.handover"]
    );
    // And the widening shape refuses rather than handing back everything.
    assert_eq!(
        narrow(&full, &with(Vec::new())),
        Err(NarrowingRefusal::NoVerbs)
    );
}

#[test]
fn where_a_procedure_came_from_never_changes_what_it_may_do() {
    // Decision 0055 section 2. The same steps under both implemented
    // provenances are validated the same way and narrow to the same set — so a
    // rule that ever started reading provenance fails here rather than in
    // review.
    let steps = vec![query(), open_link()];
    let mut authored = with(steps.clone());
    authored.provenance = ProcedureProvenance::Authored;
    let mut recorded = with(steps);
    recorded.provenance = ProcedureProvenance::RecordedFromTask;

    assert_eq!(
        validate(&authored, Milestone::M3),
        validate(&recorded, Milestone::M3)
    );
    let full = EffectiveToolSet::for_task(Milestone::M3, &[]);
    assert_eq!(
        narrow(&full, &authored).unwrap().names(),
        narrow(&full, &recorded).unwrap().names()
    );
    assert!(ProcedureProvenance::InstalledFromPack.is_reserved());
}
