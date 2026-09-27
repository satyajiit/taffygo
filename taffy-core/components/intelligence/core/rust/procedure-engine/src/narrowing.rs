// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Taking authority away, and never the way out.
//!
//! # Why the function is here and not on `EffectiveToolSet`
//!
//! Decision 0055 section 3 writes this as `EffectiveToolSet::narrow_by(&Procedure)`,
//! which reads well and would put a `procedure-engine` type into `task-engine`.
//! The dependency runs the other way — a procedure is built out of tool names
//! and `task-engine` knows nothing about procedures — so the operation lives
//! here as a free function over the set. Nothing about the guarantee changes:
//! [`narrow`] calls `EffectiveToolSet::narrow_by`, which filters the set it is
//! given and never consults the registry again, so there is no expression of
//! "and also allow" anywhere in the path.
//!
//! That is the whole safety argument for letting a stored record touch the tool
//! surface. A name a procedure could introduce would be an `ActionClass` it
//! could introduce, and an `ActionClass` is a permission (record 0054).
//!
//! # Narrowing is also what makes a procedure worth having
//!
//! Beyond speed: a procedure for one site can hold the assistant to three tools
//! on that site, which is a smaller surface than the task had before it
//! matched.
//!
//! # Section 8: a narrowing may remove a capability and never the exit
//!
//! `user.handover` is not a capability the task spends on the page; it is how
//! the task stops and gives the page back to the person, and record 0054 makes
//! it what remains once everything else has been refused. Taffy never learns to
//! recognise a challenge meant to prove a person is present — the action class
//! is prohibited — so refusal by exhaustion is the mechanism, and exhaustion
//! only works while something is left at the end of it.
//!
//! A procedure that simply omitted the handover would therefore strand every
//! task it ran on: refused each remaining tool, reaching for the exit, refused
//! that too, with no way to say so to the person whose page it is. That is not
//! a narrowing. `task_engine::tool::is_unconditional` is where the two are told
//! apart, and `EffectiveToolSet::narrow_by` already honours it — so this module
//! adds nothing to reach the guarantee and has a test that would notice if the
//! guarantee left.
//!
//! `user.ask` is deliberately not protected. Narrowing it costs a capability
//! and not the exit: a task that may not ask can still hand back.
//!
//! # The ask a fill cannot run without
//!
//! [`REQUEST_VALUES_VERB`] is a third case and it is neither of the two above.
//! A recorded fill's value is a position in what the person supplies
//! ([`crate::step::ArgumentSupply::PersonsPosition`]), so the step cannot run
//! until the person has been asked — and the ask is a verb the record does not
//! name, because it cannot: recording is built from the browser's live
//! **capability** ledger, a call that reaches the person spends no capability,
//! and `UndescribedReason::NotARecordableVerb` is the honest answer for one.
//! So a procedure whose fill needs a person's value names every verb it uses
//! except the one without which none of them runs.
//!
//! Two ways to settle that were available and only one of them is a narrowing.
//!
//! **Joining `task_engine::tool::UNCONDITIONAL_TOOLS`** would protect the ask
//! for every procedure, including every procedure that fills nothing. That is
//! the argument `user.handover` is there under and it does not carry over.
//! Section 8 is explicit about where the line is: the handover is unconditional
//! because it is how a task *stops*, and `user.ask` is deliberately not,
//! because narrowing it costs a capability and not the exit. Asking a person to
//! fill a form in is on the second side of that line — it is a way to get
//! something from the person, not a way to end — so making it unconditional
//! would hand every narrowed set a standing licence to interrupt somebody, on
//! the strength of records that never mentioned it.
//!
//! **Deriving the allowlist from what the record needs** applies section 8's
//! actual argument at the scope it holds at: a fill that cannot ask for its
//! value is a step that cannot run, exactly as a task that cannot hand over is
//! a task that cannot end. [`required_verbs`] is that derivation, and it stays
//! a narrowing in the only sense that matters — it is handed to
//! `EffectiveToolSet::narrow_by`, which filters the set it is called on and
//! never consults the registry, so nothing here can produce a name the task
//! did not already have. A task whose own allowlist never admitted the ask
//! still refuses, at [`crate::replay::ReplayRefusal::VerbNotAdmitted`], and
//! that is the correct answer rather than a gap: the record cannot grant what
//! the task was not given.
//!
//! [`crate::replay::next_procedure_command`] reads the same list, so the set a
//! consent screen is built from and the set replay checks itself against are
//! one list with one derivation.
//!
//! # The empty list is the trap
//!
//! `EffectiveToolSet` reads an **empty** allowlist as "everything this
//! milestone has", which is the reading the reducer's own guard has always
//! used and the right one for a task that named no restriction. A procedure
//! with no verbs would produce exactly that list — and the widest possible set
//! would be handed back by the operation whose entire purpose is to make the
//! set smaller. [`narrow`] refuses instead, because there is no narrowing to
//! express and returning the input unchanged would be a lie about what
//! happened.

use task_engine::tool::{EffectiveToolSet, Milestone};

use crate::record::Procedure;

/// The registered name that asks the person to fill a form's fields in.
///
/// A constant here rather than one read from `task_engine::tool`, which names
/// the handover, the ask and the loop tools it has its own reasons to name and
/// does not name this one. `the_ask_is_a_registered_name_that_reaches_the_person`
/// checks the spelling against the registry, so a renamed row fails here
/// rather than turning this into a name nothing resolves.
pub const REQUEST_VALUES_VERB: &str = "user.request_values";

/// Why a procedure could not narrow a tool set.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum NarrowingRefusal {
    /// The procedure names no verb.
    ///
    /// Its allowlist would be empty, and an empty allowlist means "everything"
    /// — so the narrowing would widen. `validate` refuses a procedure with no
    /// steps for the same reason from the other side; this refusal is what
    /// stops an unvalidated one reaching the tool surface anyway.
    NoVerbs,
}

impl NarrowingRefusal {
    /// A short, compiled-in name, safe to record in an audit event.
    pub const fn label(self) -> &'static str {
        match self {
            Self::NoVerbs => "no_verbs",
        }
    }
}

/// Every name `procedure` cannot run without, on a build at `milestone`.
///
/// The verbs its steps name, in step order and without repeats, plus
/// [`REQUEST_VALUES_VERB`] when some step's argument is a position in what the
/// person supplies. See the module header for why the second is a derivation
/// from the record rather than a name protected for every record.
///
/// The order is the record's own, and the ask goes last, so the list a person
/// is shown reads as the procedure's steps followed by the one thing it has to
/// ask them for.
pub fn required_verbs(procedure: &Procedure, milestone: Milestone) -> Vec<String> {
    let mut names = procedure.verbs();
    if names
        .iter()
        .any(|name| name == "browser.download.from_link")
        && !names.iter().any(|name| name == "browser.download.list")
    {
        names.push("browser.download.list".to_owned());
    }
    if procedure.steps.iter().any(|step| {
        step.arguments
            .iter()
            .any(|argument| matches!(argument.value, crate::StepValue::SemanticTarget { .. }))
    }) && !names.iter().any(|name| name == "browser.dom.read")
    {
        names.push("browser.dom.read".to_owned());
    }
    let asks = procedure
        .steps
        .iter()
        .any(|step| step.asks_for_a_supplied_value(milestone));
    if asks && !names.iter().any(|name| name == REQUEST_VALUES_VERB) {
        names.push(REQUEST_VALUES_VERB.to_owned());
    }
    names
}

/// The subset of `set` that `procedure` admits.
///
/// A filter over what the set already holds. Nothing here can produce a name
/// the set did not have, whatever the record says — and the one name the record
/// cannot take away is the handover, because removing an escape is not a
/// narrowing.
///
/// The refusal is decided on the verbs the record *names*, before the ask is
/// derived, so a procedure with no steps refuses rather than narrowing a set
/// down to one name it never asked for.
pub fn narrow(
    set: &EffectiveToolSet,
    procedure: &Procedure,
) -> Result<EffectiveToolSet, NarrowingRefusal> {
    if procedure.verbs().is_empty() {
        return Err(NarrowingRefusal::NoVerbs);
    }
    Ok(set.narrow_by(&required_verbs(procedure, set.milestone())))
}

#[cfg(test)]
mod tests {
    use super::{narrow, required_verbs, NarrowingRefusal, REQUEST_VALUES_VERB};
    use crate::field::FieldPurpose;
    use crate::matching::{MatchClause, MatchCondition};
    use crate::record::{Procedure, ProcedureId, ProcedureProvenance, ProcedureScope};
    use crate::step::{ProcedureStep, StepArgument, StepValue};
    use bip_types::action::PostconditionKind;
    use bip_types::snapshot::SemanticRole;
    use task_engine::tool::{EffectiveToolSet, Milestone, ToolDispatch, ToolLookup};

    fn procedure(verbs: &[&str]) -> Procedure {
        let steps = verbs
            .iter()
            .map(|verb| ProcedureStep::new(*verb, PostconditionKind::NoMutation))
            .collect();
        Procedure::draft(
            ProcedureId::new("one").unwrap(),
            ProcedureScope::for_origin("https://example.test").unwrap(),
            MatchCondition::new(vec![MatchClause::RolePresent(SemanticRole::SearchField)]),
            steps,
            ProcedureProvenance::Authored,
        )
    }

    /// A one-step procedure whose fill takes the value from the person.
    fn filling_procedure() -> Procedure {
        let mut record = procedure(&["browser.form.fill"]);
        record.steps =
            vec![
                ProcedureStep::new("browser.form.fill", PostconditionKind::NodeValueChanged)
                    .taking(vec![StepArgument::new(
                        "value_from",
                        StepValue::FromPerson {
                            purpose: FieldPurpose::OneTimeCode,
                        },
                    )])
                    .filling(FieldPurpose::OneTimeCode),
            ];
        record
    }

    #[test]
    fn the_ask_is_a_registered_name_that_reaches_the_person() {
        // The spelling is compiled in here, so it is checked against the table
        // rather than remembered: a renamed row must fail here rather than
        // leave this crate deriving a name nothing resolves.
        match task_engine::tool::resolve(REQUEST_VALUES_VERB, Milestone::M3) {
            ToolLookup::Available(entry) => {
                assert_eq!(entry.name, REQUEST_VALUES_VERB);
                assert_eq!(entry.dispatch, ToolDispatch::Person);
            }
            other => unreachable!("{REQUEST_VALUES_VERB} resolved to {other:?}"),
        }
    }

    #[test]
    fn a_record_that_needs_a_persons_value_keeps_the_one_verb_that_can_ask_for_it() {
        // Section 8's argument at the scope it actually holds at: a fill that
        // cannot ask for its value is a step that cannot run. The ask is
        // derived from the record rather than protected for every record, so a
        // procedure that fills nothing does not get it.
        let asking = filling_procedure();
        assert_eq!(
            required_verbs(&asking, Milestone::M5),
            vec!["browser.form.fill", REQUEST_VALUES_VERB]
        );
        assert_eq!(
            required_verbs(&procedure(&["browser.dom.read"]), Milestone::M5),
            vec!["browser.dom.read"]
        );
        let set = EffectiveToolSet::for_task(
            Milestone::M5,
            &[
                "browser.form.fill".to_owned(),
                REQUEST_VALUES_VERB.to_owned(),
                "browser.dom.read".to_owned(),
            ],
        );
        let narrowed = narrow(&set, &asking).unwrap();
        assert!(narrowed.admits(REQUEST_VALUES_VERB));
        assert!(narrowed.admits("browser.form.fill"));
        // Still a narrowing: a name the record does not need is gone.
        assert!(!narrowed.admits("browser.dom.read"));
    }

    #[test]
    fn the_ask_is_not_a_name_a_record_can_introduce() {
        // The derivation is handed to `narrow_by`, which filters. A task whose
        // own allowlist never admitted the ask does not gain it by holding a
        // record that needs one — the record cannot grant what the task was
        // not given.
        let set = EffectiveToolSet::for_task(Milestone::M5, &["browser.form.fill".to_owned()]);
        let narrowed = narrow(&set, &filling_procedure()).unwrap();
        assert!(!narrowed.admits(REQUEST_VALUES_VERB));
        assert_eq!(narrowed.names(), vec!["browser.form.fill", "user.handover"]);
    }

    #[test]
    fn a_procedure_that_fills_nothing_never_gains_the_ask() {
        // The counterweight to the case above, and the reason the ask is not
        // in `UNCONDITIONAL_TOOLS`: it is a way to get something from the
        // person rather than a way for the task to end, so a record that never
        // needed it must not carry a standing licence to interrupt somebody.
        let full = EffectiveToolSet::for_task(Milestone::M5, &[]);
        assert!(full.admits(REQUEST_VALUES_VERB));
        let narrowed = narrow(&full, &procedure(&["browser.dom.read"])).unwrap();
        assert!(!narrowed.admits(REQUEST_VALUES_VERB));
        for name in task_engine::tool::UNCONDITIONAL_TOOLS {
            assert_ne!(*name, REQUEST_VALUES_VERB);
        }
    }

    #[test]
    fn a_narrowing_can_only_take_names_away() {
        let full = EffectiveToolSet::for_task(Milestone::M3, &[]);
        let narrowed =
            narrow(&full, &procedure(&["browser.dom.read", "browser.navigate"])).unwrap();
        for name in narrowed.names() {
            assert!(full.names().contains(&name), "{name} was not in the set");
        }
        assert!(narrowed.len() < full.len());
        assert!(narrowed.admits("browser.dom.read"));
        assert!(narrowed.admits("browser.navigate"));
        assert!(!narrowed.admits("browser.search"));
    }

    #[test]
    fn a_procedure_naming_a_tool_the_set_never_had_gains_nothing() {
        let set = EffectiveToolSet::for_task(Milestone::M3, &["browser.dom.read".to_owned()]);
        let narrowed = narrow(&set, &procedure(&["browser.dom.read", "python.execute"])).unwrap();
        assert!(!narrowed.admits("python.execute"));
        assert_eq!(narrowed.names(), set.names());
    }

    #[test]
    fn the_handover_survives_a_procedure_that_never_mentions_it() {
        // Section 8. A procedure that forgot the exit would otherwise strand
        // every task it ran on: refused each remaining tool, reaching for the
        // way out, refused that too.
        let full = EffectiveToolSet::for_task(Milestone::M3, &[]);
        let narrowed = narrow(&full, &procedure(&["browser.dom.read"])).unwrap();
        assert!(narrowed.admits("user.handover"));
        assert_eq!(narrowed.names(), vec!["browser.dom.read", "user.handover"]);
    }

    #[test]
    fn every_unconditional_name_survives_a_narrowing_that_omits_it() {
        // Read from `UNCONDITIONAL_TOOLS` rather than written out, so a name
        // added to that list is covered here without anybody remembering to
        // come back. The list is where a capability and an escape are told
        // apart; this is the assertion that a procedure cannot take the second
        // away.
        let full = EffectiveToolSet::for_task(Milestone::M3, &[]);
        let narrowed = narrow(&full, &procedure(&["browser.dom.read"])).unwrap();
        for name in task_engine::tool::UNCONDITIONAL_TOOLS {
            assert!(!procedure(&["browser.dom.read"])
                .verbs()
                .contains(&(*name).to_owned()));
            assert_eq!(full.admits(name), narrowed.admits(name), "{name}");
        }
    }

    #[test]
    fn asking_is_a_capability_and_is_narrowed_away_like_any_other() {
        let full = EffectiveToolSet::for_task(Milestone::M3, &[]);
        let narrowed = narrow(&full, &procedure(&["browser.dom.read"])).unwrap();
        assert!(!narrowed.admits("user.ask"));
        // And a procedure that does name it keeps it, so the difference is the
        // record's and not a second protected name.
        let asking = narrow(&full, &procedure(&["browser.dom.read", "user.ask"])).unwrap();
        assert!(asking.admits("user.ask"));
    }

    #[test]
    fn a_procedure_with_no_verbs_refuses_to_narrow_rather_than_widening() {
        // An empty allowlist reads as "everything this milestone has", so
        // delegating an empty verb list would hand back the widest set the
        // build has from the operation whose whole purpose is to shrink one.
        let set = EffectiveToolSet::for_task(Milestone::M3, &["browser.dom.read".to_owned()]);
        assert_eq!(
            narrow(&set, &procedure(&[])),
            Err(NarrowingRefusal::NoVerbs)
        );
        assert_eq!(NarrowingRefusal::NoVerbs.label(), "no_verbs");
    }

    #[test]
    fn a_narrowing_down_to_nothing_still_leaves_the_way_out() {
        // The set-to-nothing case, which is the one a reviewer should ask
        // about: a procedure naming only verbs this build does not have takes
        // every capability away, and what is left is not an empty set but the
        // exit. An empty one would be a task that cannot act and cannot say so
        // either — refused each remaining tool, reaching for the way out,
        // refused that too.
        let full = EffectiveToolSet::for_task(Milestone::M3, &[]);
        let narrowed = narrow(&full, &procedure(&["browser.form.fill", "python.execute"])).unwrap();
        assert!(!narrowed.is_empty());
        assert_eq!(narrowed.names(), vec!["user.handover"]);
        // And narrowing *that* again cannot empty it either, however many
        // times it is asked.
        let again = narrow(&narrowed, &procedure(&["browser.dom.read"])).unwrap();
        assert_eq!(again.names(), vec!["user.handover"]);
    }

    #[test]
    fn narrowing_twice_is_the_intersection_and_never_a_restoration() {
        let full = EffectiveToolSet::for_task(Milestone::M3, &[]);
        let once = narrow(&full, &procedure(&["browser.dom.read", "browser.navigate"])).unwrap();
        let twice = narrow(&once, &procedure(&["browser.navigate", "browser.search"])).unwrap();
        assert_eq!(twice.names(), vec!["browser.navigate", "user.handover"]);
    }

    #[test]
    fn the_milestone_filter_still_has_no_exception() {
        // A record cannot grant what the build does not have, and that holds
        // for the one unconditional name too.
        let set = EffectiveToolSet::for_task(Milestone::M2, &[]);
        let narrowed = narrow(&set, &procedure(&["user.handover"])).unwrap();
        assert!(narrowed.is_empty());
    }
}
