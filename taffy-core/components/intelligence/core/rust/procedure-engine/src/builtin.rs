// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The procedures the product ships.
//!
//! # There is one, and this file will not invent a second
//!
//! `ProcedureProvenance::Authored` covers both a procedure a person wrote down
//! and one the product ships, so a built-in is not a different kind of record —
//! it is a record whose author is the build. Decision 0055 names exactly one
//! thing a shipped record has to reproduce today: the reviewed local
//! `BuildSourceTable` workflow, so that the conformance oracle can drive both
//! paths and the hard-coded half can then be deleted (section 7 and the first
//! consequence). Everything else a built-in could be is unwritten, and adding
//! one here would be adding product scope through a constant.
//!
//! # A built-in is minted for an origin, not shipped scoped to one
//!
//! A procedure carries exactly one origin ([`ProcedureScope`]), because a
//! record written for one origin has seen one origin's pages and a page that
//! merely resembles it is not evidence. A shipped sequence is about *how* to
//! do something and not about where, so it cannot carry a scope in the source
//! tree — it is minted for the origin the person actually consented to, at the
//! moment the task has one.
//!
//! # Why the identifier is the reviewed workflow's effect namespace
//!
//! `task_engine::proposal::plan_step_key` names an effect by the sequence it
//! belongs to, and a replayed procedure passes its own identifier there. If
//! this built-in's identifier differed from
//! [`task_engine::REVIEWED_EFFECT_NAMESPACE`], a task that had already read its
//! source under the hard-coded path and was then finished under the procedure
//! path would mint an identity the browser's effect journal had never seen —
//! and read the page a second time, having already been told it succeeded.
//! [`identifier_matches_the_reviewed_namespace`] is the assertion, so the
//! equality is checked rather than remembered.
//!
//! [`identifier_matches_the_reviewed_namespace`]: #
//! [`ProcedureScope`]: crate::record::ProcedureScope

use bip_types::action::PostconditionKind;
use bip_types::snapshot::SemanticRole;
use policy_engine::origin::NormalizedOrigin;

use crate::matching::{MatchClause, MatchCondition};
use crate::record::{Procedure, ProcedureId, ProcedureProvenance, ProcedureScope, RecordError};
use crate::status::{transition, LifecycleActor, ProcedureStatus};
use crate::step::ProcedureStep;

/// What the shipped source-table sequence is called.
///
/// It is also the effect namespace its proposals are keyed under, and it must
/// equal `task_engine::REVIEWED_EFFECT_NAMESPACE` — see the module header for
/// what a mismatch would cost.
pub const BUILD_SOURCE_TABLE_ID: &str = "source-table";

/// The shipped sequence behind the reviewed `BuildSourceTable` template, as a
/// procedure about `origin`.
///
/// One step: read the consented source. The table it produces is built from
/// what that read returned, which is the plan's second step and not an action
/// on the page.
///
/// It is returned `Active` rather than `Draft`, and that is the one place a
/// built-in differs from a recorded procedure: "the assistant did this once"
/// is evidence that it worked once, but a sequence the product ships has been
/// through the review the product ships under. The move is still made through
/// [`transition`] rather than by writing the field, so a lifecycle rule cannot
/// be skipped here without being skipped visibly.
pub fn build_source_table(origin: &NormalizedOrigin) -> Result<Procedure, RecordError> {
    let mut procedure = Procedure::draft(
        ProcedureId::new(BUILD_SOURCE_TABLE_ID)?,
        ProcedureScope::from_normalized(origin.clone())?,
        // The page is a document. This is the honest condition for a sequence
        // that reads whatever the person pointed at: it names a role the
        // semantic graph either carries or does not, and it claims nothing
        // about the page's subject that the product could not show them.
        MatchCondition::new(vec![MatchClause::RolePresent(SemanticRole::Document)]),
        vec![ProcedureStep::new(
            "browser.dom.read",
            PostconditionKind::NoMutation,
        )],
        ProcedureProvenance::Authored,
    );
    if let Ok(active) = transition(
        procedure.status,
        ProcedureStatus::Active,
        LifecycleActor::Product,
    ) {
        procedure.status = active;
    }
    Ok(procedure)
}

#[cfg(test)]
mod tests {
    use super::{build_source_table, BUILD_SOURCE_TABLE_ID};
    use crate::record::ProcedureProvenance;
    use crate::rules::validate;
    use crate::status::ProcedureStatus;
    use policy_engine::origin::{normalize_serialization, NormalizedOrigin};
    use task_engine::tool::Milestone;

    fn origin() -> NormalizedOrigin {
        let Ok(origin) = normalize_serialization("https://example.test") else {
            unreachable!("the fixture origin is an origin")
        };
        origin
    }

    #[test]
    fn identifier_matches_the_reviewed_namespace() {
        // A mismatch is not a naming inconsistency. It is a task that read its
        // source under the hard-coded path, was finished under this one, and
        // read the page again under an effect identity the journal had never
        // refused.
        assert_eq!(
            BUILD_SOURCE_TABLE_ID,
            task_engine::REVIEWED_EFFECT_NAMESPACE
        );
    }

    #[test]
    fn the_shipped_sequence_satisfies_every_structural_rule() {
        let Ok(procedure) = build_source_table(&origin()) else {
            unreachable!("the built-in is well formed")
        };
        assert_eq!(validate(&procedure, Milestone::M3), Ok(()));
        assert_eq!(procedure.status, ProcedureStatus::Active);
        assert!(procedure.is_runnable());
        assert_eq!(procedure.provenance, ProcedureProvenance::Authored);
    }

    #[test]
    fn it_names_the_tool_the_reviewed_workflow_proposes_and_nothing_else() {
        let Ok(procedure) = build_source_table(&origin()) else {
            unreachable!("the built-in is well formed")
        };
        assert_eq!(
            procedure.verbs(),
            vec![task_engine::REVIEWED_OBSERVATION_TOOL]
        );
    }

    #[test]
    fn it_is_about_the_origin_it_was_minted_for_and_no_neighbour() {
        let Ok(procedure) = build_source_table(&origin()) else {
            unreachable!("the built-in is well formed")
        };
        assert!(procedure.scope.covers(&origin()));
        for other in ["https://www.example.test", "http://example.test"] {
            let Ok(elsewhere) = normalize_serialization(other) else {
                unreachable!("the fixture origin is an origin")
            };
            assert!(!procedure.scope.covers(&elsewhere), "{other}");
        }
    }

    #[test]
    fn an_opaque_origin_produces_no_built_in() {
        // A sandboxed frame's origin is session-local, so a procedure scoped to
        // one is dead the moment the session ends and can never be shown to a
        // person either.
        assert!(build_source_table(&NormalizedOrigin::Opaque {
            opaque_id: "session-local".to_owned(),
        })
        .is_err());
    }
}
