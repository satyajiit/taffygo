// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Recording a procedure from the live ledger, and the refusals that are the
//! point of it.
//!
//! Decision 0055 section 6 and its fourth validation item — "a recording that
//! loses a step produces no procedure. The negative case matters more than the
//! positive one here." This file is the negative case, twice over: once for
//! each of the six structural rules, checked at **record** time rather than at
//! replay, and once for a recording that could not describe everything the
//! ledger admitted.
//!
//! The unit suite inside `recording` checks the mapping from a descriptor to a
//! step. This file exists for a different reason: it is where a reviewer can
//! read "a recorded procedure is held to every rule an authored one is" against
//! the refusals that make it so, without knowing the module layout. Every test
//! for a rule names the rule number, and a rule with two ways to break it has
//! two tests rather than one with two halves, so a failure says which half.

#![allow(
    clippy::unwrap_used,
    clippy::expect_used,
    clippy::panic,
    clippy::indexing_slicing
)]

use bip_types::action::PostconditionKind;
use bip_types::snapshot::SemanticRole;
use policy_engine::origin::{normalize_serialization, NormalizedOrigin};
use procedure_engine::{
    narrow, record_procedure, validate, ArgumentDescriptor, FieldPurpose, LedgerEntry, MatchClause,
    PhraseId, ProcedureId, ProcedureProvenance, ProcedureStatus, RecordedValue, Recording,
    RefusalReason, SkillRecordError, StepDescriptor, StepValue, StructuralRule, UndescribedReason,
    MAX_MATCH_CLAUSES, MAX_PROCEDURE_STEPS,
};
use task_engine::tool::{ArgumentRefusalReason, EffectiveToolSet, Milestone};

fn origin() -> NormalizedOrigin {
    normalize_serialization("https://example.test").unwrap()
}

fn id() -> ProcedureId {
    ProcedureId::new("recorded.once").unwrap()
}

fn facts() -> Vec<MatchClause> {
    vec![
        MatchClause::RolePresent(SemanticRole::SearchField),
        MatchClause::PhraseAt {
            role: SemanticRole::Button,
            phrase: PhraseId::Search,
        },
    ]
}

/// An observation, which is the one step shape a task at M3 can actually
/// perform against a page without asking anybody for anything.
fn query() -> StepDescriptor {
    StepDescriptor::new("browser.dom.query", PostconditionKind::NoMutation)
}

/// A recording whose ledger count agrees with its entries, which is the
/// ordinary case; the disagreement has its own test.
fn recording(entries: Vec<LedgerEntry>) -> Recording {
    let admitted = entries.len();
    Recording::new(origin(), facts(), entries, admitted)
}

fn described(steps: Vec<StepDescriptor>) -> Recording {
    recording(steps.into_iter().map(LedgerEntry::Described).collect())
}

/// The refusal a recording earns, whatever kind it is. Used where the point is
/// that a shape has no accepted spelling, and the *reason* differs by which
/// door the shape is refused at.
fn refused(recording: &Recording, milestone: Milestone) -> SkillRecordError {
    match record_procedure(id(), recording, milestone) {
        Err(error) => error,
        Ok(_) => unreachable!("this recording must be refused"),
    }
}

/// The rule and reason a recording earns, or a failure naming what it produced
/// instead.
fn refused_by_rule(recording: &Recording, milestone: Milestone) -> (StructuralRule, RefusalReason) {
    match record_procedure(id(), recording, milestone) {
        Err(SkillRecordError::Structural(refusal)) => {
            assert_eq!(refusal.rule, refusal.reason.rule());
            (refusal.rule, refusal.reason)
        }
        Ok(_) => unreachable!("this recording must be refused"),
        Err(other) => unreachable!("expected a structural refusal, got {}", other.label()),
    }
}

#[test]
fn a_recording_of_a_finished_task_becomes_a_draft_so_the_refusals_below_mean_something() {
    // Without this every test in the file would pass against a recorder that
    // refused everything. It also pins the two facts section 2 of the record
    // is about: the provenance is written down, and it buys nothing — the
    // procedure enters at Draft exactly as an authored one does, because "the
    // assistant did this once" is evidence that it worked once.
    let procedure = record_procedure(id(), &described(vec![query()]), Milestone::M3).unwrap();
    assert_eq!(procedure.provenance, ProcedureProvenance::RecordedFromTask);
    assert_eq!(procedure.status, ProcedureStatus::Draft);
    assert!(!procedure.is_runnable());
}

#[test]
fn rule_one_no_expressions_a_request_to_the_person_must_be_a_named_one() {
    // Asking somebody for a value without saying what it is for is not a
    // request, it is a prompt. A recorder reaches this shape honestly: the
    // step needed text, the text was the person's, and the classifier could
    // not name what it was for — so the descriptor says `Unknown` rather than
    // guessing, and the record is refused rather than kept as a step that will
    // one day ask somebody for nothing in particular.
    let search = StepDescriptor::new("browser.search", PostconditionKind::CommittedNavigation)
        .taking(vec![ArgumentDescriptor::new(
            0,
            RecordedValue::FromPerson {
                purpose: FieldPurpose::Unknown,
            },
        )]);
    assert_eq!(
        refused_by_rule(&described(vec![search]), Milestone::M3),
        (
            StructuralRule::NoExpressions,
            RefusalReason::UnnamedRequestToPerson
        )
    );
}

#[test]
fn rule_one_no_expressions_a_recording_cannot_spell_a_substitution_at_all() {
    // The other half of rule 1 is unreachable from a recording, and that is
    // the point rather than a gap in the test file: `RecordedValue` has no
    // variant carrying text, so `{{step0.value}}` — a perfectly good `String`
    // and a perfectly bad procedure — has no spelling the browser could send.
    // The property is asserted where it is carried, by building every variant
    // and reading back what each one becomes.
    let every_shape = StepDescriptor::new("browser.dom.query", PostconditionKind::NoMutation)
        .taking(vec![
            ArgumentDescriptor::new(1, RecordedValue::Choice { index: 0 }),
            ArgumentDescriptor::new(3, RecordedValue::Count(4)),
        ]);
    let procedure = record_procedure(id(), &described(vec![every_shape]), Milestone::M3).unwrap();
    for argument in &procedure.steps[0].arguments {
        assert!(
            !argument.value.carries_a_substitution_sigil(),
            "{}",
            argument.name
        );
        // A literal that carries text carries one of the build's own names for
        // that parameter, and never anything a page or a model wrote.
        if let StepValue::Literal(value) = &argument.value {
            if let Some(text) = value.text() {
                assert_eq!(text, "link", "{}", argument.name);
            }
        }
    }
}

#[test]
fn rule_two_no_backward_edges_a_recorded_step_may_not_name_itself_or_a_later_one() {
    // A reference to the same step is a self-loop; a reference to a later one
    // is a jump wearing a different word, since satisfying it would mean
    // running that step first.
    for named in [1_usize, 2] {
        let open = StepDescriptor::new("browser.link.open", PostconditionKind::CommittedNavigation)
            .taking(vec![ArgumentDescriptor::new(
                0,
                RecordedValue::FromEarlierStep { step: named },
            )]);
        assert_eq!(
            refused_by_rule(&described(vec![query(), open]), Milestone::M3),
            (
                StructuralRule::NoBackwardEdges,
                RefusalReason::StepReferenceNotEarlier
            ),
            "{named}"
        );
    }
}

#[test]
fn rule_three_no_new_verbs_a_recording_cannot_name_one_this_build_does_not_have() {
    assert_eq!(
        refused_by_rule(
            &described(vec![StepDescriptor::new(
                "nope.nope",
                PostconditionKind::NoMutation
            )]),
            Milestone::M3
        ),
        (StructuralRule::NoNewVerbs, RefusalReason::UnregisteredVerb)
    );
    // Registered and excluded by requirement: the name exists so the refusal
    // is enumerable, and no milestone enables it.
    assert_eq!(
        refused_by_rule(
            &described(vec![StepDescriptor::new(
                "device.clipboard.read",
                PostconditionKind::NoMutation
            )]),
            Milestone::M3
        ),
        (StructuralRule::NoNewVerbs, RefusalReason::ExcludedVerb)
    );
    // Registered and owned by a later milestone. A task cannot have performed
    // it, so a recording claiming to have watched it happen is wrong about its
    // own past.
    assert_eq!(
        refused_by_rule(
            &described(vec![StepDescriptor::new(
                "browser.form.fill",
                PostconditionKind::NodeValueChanged
            )
            .filling(FieldPurpose::EmailAddress)]),
            Milestone::M3
        ),
        (StructuralRule::NoNewVerbs, RefusalReason::VerbNotYetBuilt)
    );
}

#[test]
fn rule_three_no_new_verbs_covers_the_signature_and_not_only_the_name() {
    // A registered verb whose recorded arguments do not satisfy the row's
    // compiled-in schema is naming a signature this build does not have, which
    // is introducing a verb through a second door. `browser.link.open` takes a
    // node, and a count is not one.
    let open = StepDescriptor::new("browser.link.open", PostconditionKind::CommittedNavigation)
        .taking(vec![ArgumentDescriptor::new(0, RecordedValue::Count(7))]);
    assert_eq!(
        refused_by_rule(&described(vec![open]), Milestone::M3),
        (
            StructuralRule::NoNewVerbs,
            RefusalReason::Arguments(ArgumentRefusalReason::TypeMismatch)
        )
    );
}

#[test]
fn rule_four_catalogued_phrases_a_recording_that_observed_nothing_claims_every_page() {
    let nothing_observed = Recording::new(
        origin(),
        Vec::new(),
        vec![LedgerEntry::Described(query())],
        1,
    );
    assert_eq!(
        refused_by_rule(&nothing_observed, Milestone::M3),
        (
            StructuralRule::CataloguedPhrases,
            RefusalReason::EmptyMatchCondition
        )
    );
}

#[test]
fn rule_four_catalogued_phrases_more_facts_than_a_person_will_check_is_refused() {
    // The bound is about reviewability: a condition is read by a person
    // deciding whether to keep a procedure, and a recorder that dumped every
    // node it saw would produce a claim nobody reads.
    let mut too_many = facts();
    while too_many.len() <= MAX_MATCH_CLAUSES {
        too_many.push(MatchClause::RolePresent(SemanticRole::Button));
    }
    let crowded = Recording::new(origin(), too_many, vec![LedgerEntry::Described(query())], 1);
    assert_eq!(
        refused_by_rule(&crowded, Milestone::M3),
        (
            StructuralRule::CataloguedPhrases,
            RefusalReason::TooManyClauses
        )
    );
}

#[test]
fn rule_five_the_step_bound_refuses_a_long_recording_rather_than_truncating_it() {
    // Truncating produces a *different* procedure that still claims the
    // original's identity, which is the worst of the available failures — and
    // a recording is exactly where the temptation to trim arrives, because the
    // ledger's length is not the recorder's to choose.
    let long: Vec<StepDescriptor> = (0..=MAX_PROCEDURE_STEPS).map(|_| query()).collect();
    assert_eq!(long.len(), MAX_PROCEDURE_STEPS + 1);
    assert_eq!(
        refused_by_rule(&described(long), Milestone::M3),
        (StructuralRule::StepBound, RefusalReason::TooManySteps)
    );
}

#[test]
fn rule_five_the_step_bound_refuses_a_recording_of_a_task_that_did_nothing() {
    // A procedure that does nothing is not a shorter procedure: it narrows
    // nothing and it matches pages in order to do nothing to them.
    assert_eq!(
        refused_by_rule(&described(Vec::new()), Milestone::M3),
        (StructuralRule::StepBound, RefusalReason::NoSteps)
    );
}

#[test]
fn rule_six_a_recorded_fill_must_say_what_it_filled() {
    // Rule 6 hands an unclassified field to the person at replay, and the
    // shape that makes the downgrade possible is checked here: a fill with no
    // recorded purpose would reach the disposition with nothing to compare,
    // and the honest answer there would have to be invented.
    let fill = StepDescriptor::new("browser.form.fill", PostconditionKind::NodeValueChanged)
        .taking(vec![
            ArgumentDescriptor::new(0, RecordedValue::FromEarlierStep { step: 0 }),
            ArgumentDescriptor::new(
                1,
                RecordedValue::FromPerson {
                    purpose: FieldPurpose::EmailAddress,
                },
            ),
        ]);
    assert_eq!(fill.fills, None);
    assert_eq!(
        refused_by_rule(&described(vec![query(), fill]), Milestone::M5),
        (
            StructuralRule::UnclassifiedFieldHandedOver,
            RefusalReason::FillRecordsNoPurpose
        )
    );
}

#[test]
fn rule_six_a_step_that_filled_nothing_may_not_claim_a_purpose() {
    // The other direction, and it is not symmetry for its own sake: a purpose
    // on a step that touches no field is a claim about a field the step never
    // saw, and it is the shape a recorder produces when it attaches the last
    // classification it happened to be holding.
    assert_eq!(
        refused_by_rule(
            &described(vec![query().filling(FieldPurpose::EmailAddress)]),
            Milestone::M3
        ),
        (
            StructuralRule::UnclassifiedFieldHandedOver,
            RefusalReason::PurposeOnAStepThatFillsNothing
        )
    );
}

#[test]
fn a_recording_that_lost_a_step_produces_no_procedure() {
    // Decision 0055's fourth validation item. The middle action is one the
    // browser could not describe — its argument was text a page authored, and
    // a record holds no such thing — and what comes back is nothing at all,
    // rather than the two steps around it.
    //
    // The two describable steps are a real sequence: query, then click what it
    // found. A recorder that saved them would produce a procedure that reads
    // correctly, replays cleanly, and does something the person never watched
    // happen, because the step between them is gone.
    let click =
        StepDescriptor::new("browser.dom.click", PostconditionKind::NodeStateChanged).taking(vec![
            ArgumentDescriptor::new(0, RecordedValue::FromEarlierStep { step: 0 }),
        ]);
    let with_a_gap = recording(vec![
        LedgerEntry::Described(query()),
        LedgerEntry::Undescribed(UndescribedReason::ValueIsNotAReference),
        LedgerEntry::Described(click.clone()),
    ]);
    assert_eq!(
        record_procedure(id(), &with_a_gap, Milestone::M3),
        Err(SkillRecordError::StepNotDescribed {
            step: 1,
            reason: UndescribedReason::ValueIsNotAReference
        })
    );
    // And the same three actions with the gap simply left out of the list is
    // refused too, by the other guard: the ledger admitted three capabilities
    // and the descriptor builder produced two.
    let silently_dropped = Recording::new(
        origin(),
        facts(),
        vec![
            LedgerEntry::Described(query()),
            LedgerEntry::Described(click),
        ],
        3,
    );
    assert_eq!(
        record_procedure(id(), &silently_dropped, Milestone::M3),
        Err(SkillRecordError::LedgerNotAccountedFor {
            admitted: 3,
            entries: 2
        })
    );
    // What the accounting is *not*, stated where a reviewer will look for it:
    // lowering the admitted count to match records the two steps, because a
    // browser that misreports its own ledger is not something the core can
    // check. The browser is the privileged side of this boundary. The guard is
    // against the failure that actually happens — a descriptor builder that
    // hit an error on one action and carried on round the loop — and that one
    // it catches, because the count and the entries come from different places.
    let believed = Recording::new(origin(), facts(), vec![LedgerEntry::Described(query())], 1);
    assert!(record_procedure(id(), &believed, Milestone::M3).is_ok());
}

#[test]
fn a_recording_that_claims_more_than_the_ledger_admitted_produces_no_procedure() {
    // The same guard from the other side. A descriptor list longer than the
    // ledger is a step nobody watched happen, which is the same defect as a
    // missing one wearing the opposite sign.
    let invented = Recording::new(
        origin(),
        facts(),
        vec![
            LedgerEntry::Described(query()),
            LedgerEntry::Described(query()),
        ],
        1,
    );
    assert_eq!(
        record_procedure(id(), &invented, Milestone::M3),
        Err(SkillRecordError::LedgerNotAccountedFor {
            admitted: 1,
            entries: 2
        })
    );
}

#[test]
fn every_reason_a_step_cannot_be_described_refuses_the_whole_recording() {
    // The vocabulary rather than the member somebody happened to write a test
    // for. A reason added later that produced a partial record would fail
    // here, which is the only place the "whole" in "refuses as a whole" is
    // checked against the whole enumeration.
    for reason in UndescribedReason::ALL {
        let with_a_gap = recording(vec![
            LedgerEntry::Described(query()),
            LedgerEntry::Undescribed(*reason),
        ]);
        assert_eq!(
            record_procedure(id(), &with_a_gap, Milestone::M3),
            Err(SkillRecordError::StepNotDescribed {
                step: 1,
                reason: *reason
            }),
            "{}",
            reason.label()
        );
    }
}

#[test]
fn where_a_recording_came_from_never_changes_what_it_may_do() {
    // Section 2. The recorded procedure and an authored one built from the
    // same steps are the same record but for the field, so a rule that ever
    // started reading provenance would make these two differ.
    let recorded = record_procedure(id(), &described(vec![query()]), Milestone::M3).unwrap();
    let mut authored = recorded.clone();
    authored.provenance = ProcedureProvenance::Authored;
    assert_eq!(recorded.steps, authored.steps);
    assert_eq!(recorded.status, authored.status);
    assert_eq!(recorded.version, authored.version);
    assert_eq!(
        validate(&recorded, Milestone::M3),
        validate(&authored, Milestone::M3)
    );
    // And the authority half, which is the one the field is tempting about: a
    // recorded procedure *feels* more trustworthy, because the assistant is
    // known to have run those exact steps. That feeling is about the page it
    // ran on, and the next page may be a different site shaped to resemble it.
    // So the two narrow the same set to the same names — and to a smaller set
    // than the task had, because narrowing is the only direction there is.
    let full = EffectiveToolSet::for_task(Milestone::M3, &[]);
    let by_recorded = narrow(&full, &recorded).unwrap();
    let by_authored = narrow(&full, &authored).unwrap();
    assert_eq!(by_recorded.names(), by_authored.names());
    assert_eq!(
        by_recorded.names(),
        vec!["browser.dom.query", "user.handover"]
    );
    assert!(by_recorded.len() < full.len());
}

#[test]
fn a_recorded_fill_can_only_be_a_node_it_found_and_a_value_the_person_gives() {
    // The sharpest statement of "a value is a reference", and it falls out of
    // two vocabularies meeting rather than out of a check: `browser.form.fill`
    // declares a handle and text, and the only `RecordedValue` that stands for
    // text is a named request to the person. So the *sole* recordable fill is
    // "put what the person gives for this classification into the node an
    // earlier step found" — the bytes stay in the browser's vault, and there is
    // no spelling at all for a recorder that wanted to bake in what was typed
    // last time.
    let recordable = StepDescriptor::new("browser.form.fill", PostconditionKind::NodeValueChanged)
        .taking(vec![
            ArgumentDescriptor::new(0, RecordedValue::FromEarlierStep { step: 0 }),
            ArgumentDescriptor::new(
                1,
                RecordedValue::FromPerson {
                    purpose: FieldPurpose::EmailAddress,
                },
            ),
        ])
        .filling(FieldPurpose::EmailAddress);
    assert!(record_procedure(id(), &described(vec![query(), recordable]), Milestone::M5).is_ok());

    // Every other shape, in each of the two positions, has no accepted
    // spelling. Which door it is refused at differs and both are named: a
    // choice index against a parameter that declares no choice set is refused
    // where the descriptor is read, and everything else is refused by rule 3,
    // because the record would be naming a signature this build does not have.
    let person = RecordedValue::FromPerson {
        purpose: FieldPurpose::EmailAddress,
    };
    let earlier = RecordedValue::FromEarlierStep { step: 0 };
    let others = [
        RecordedValue::Choice { index: 0 },
        RecordedValue::Count(1),
        RecordedValue::Flag(true),
    ];
    for (node, value) in others
        .iter()
        .map(|shape| (shape, &person))
        .chain(others.iter().map(|shape| (&earlier, shape)))
        .chain([(&person, &person), (&earlier, &earlier)])
    {
        let step = StepDescriptor::new("browser.form.fill", PostconditionKind::NodeValueChanged)
            .taking(vec![
                ArgumentDescriptor::new(0, node.clone()),
                ArgumentDescriptor::new(1, value.clone()),
            ])
            .filling(FieldPurpose::EmailAddress);
        let why = refused(&described(vec![query(), step]), Milestone::M5);
        let expected_at_the_door = matches!(node, RecordedValue::Choice { .. })
            || matches!(value, RecordedValue::Choice { .. });
        match why {
            SkillRecordError::NoSuchChoice { step: 1, .. } => {
                assert!(expected_at_the_door, "{} {}", node.label(), value.label());
            }
            SkillRecordError::Structural(refusal) => {
                assert!(!expected_at_the_door, "{} {}", node.label(), value.label());
                assert_eq!(
                    refusal.rule,
                    StructuralRule::NoNewVerbs,
                    "{} {}",
                    node.label(),
                    value.label()
                );
            }
            other => unreachable!("{} {}: {}", node.label(), value.label(), other.label()),
        }
    }
}
