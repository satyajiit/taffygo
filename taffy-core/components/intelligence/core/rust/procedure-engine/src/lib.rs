// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! One record family for an authored skill and a learned procedure.
//!
//! Authoritative specification: decision
//! `docs/decisions/0055-a-skill-and-a-learned-procedure-are-one-record.md`.
//! Everything here implements that record; nothing here re-decides it.
//!
//! # Why there is one type and not two
//!
//! A skill is written down by a person or by the product. A procedure is
//! arrived at from the other side — the assistant did the task once and the
//! second time should not cost eight model calls over the same eight pages.
//! Their *origins* differ and nothing else does: both are a stored sequence of
//! steps that will later run against a live page with a real person's
//! authority attached, both go stale when a site changes, both can be replayed
//! against a page that merely resembles the one they were written for, and
//! both must narrow what a task may do rather than widen it.
//!
//! Two types with one set of hazards means every rule is written twice and
//! enforced once — and the half that is enforced is whichever one the reviewer
//! was looking at. So [`Procedure`] is the only record, [`ProcedureProvenance`]
//! is a field on it, and [`ProcedureStatus`] is the only lifecycle.
//!
//! # What this crate deliberately does not do
//!
//! It stores nothing. There is no database here and no journal: [`recording`]
//! decides whether what the browser observed *is* a procedure, and handing the
//! result to a durable store is a separate change. It reads no ledger either —
//! it cannot, being in the wrong process for one — which is why the browser
//! builds the step descriptors and this crate only judges them.
//!
//! It also executes nothing: a matched procedure changes which command is
//! *proposed*, and every step still goes through `policy-engine`, a minted
//! capability, a lease, its postcondition and `audit-engine`, exactly as an
//! unmatched task's step does. Replay is a fast path and never a trusted one —
//! the saving is the model call, and the saving is emphatically not the checks.
//!
//! There is no dependency on `model-router`, and that is a property rather
//! than an accident: a procedure that has matched needs no model, which is the
//! whole point of having it.
//!
//! # The six structural rules
//!
//! [`rules::validate`] checks all six, when a procedure is stored and again
//! when it is loaded, and its refusal names which one broke
//! ([`rules::StructuralRule`]):
//!
//! | Rule | Where it lives |
//! |---|---|
//! | 1. No expressions | [`step::StepValue`] has no computing variant, and a literal carrying a substitution sigil is refused |
//! | 2. No backward edges | [`step::StepValue::FromEarlierStep`] must name a *strictly* earlier step |
//! | 3. No new verbs | every verb resolves in `task_engine::tool::REGISTRY`, arguments included |
//! | 4. Catalogued phrases, never a pattern | [`matching`] holds a closed catalogue matched by equality; no regular-expression engine ships |
//! | 5. [`step::MAX_PROCEDURE_STEPS`] | refused at storage, never truncated at replay |
//! | 6. An unclassified field is handed over | [`field::disposition`] downgrades a fill to [`field::StepDisposition::HandToUser`] |
//!
//! Recording is a storage, so [`recording::record`] runs all six before it
//! returns a record and refuses the whole recording when one breaks.
//!
//! # How this crate is laid out
//!
//! | Module | Owns |
//! |---|---|
//! | [`record`] | The record itself: identity, scope, provenance |
//! | [`definition`] | The record as the bytes a database column holds, and back |
//! | [`status`] | The one lifecycle, and who may move it |
//! | [`step`] | A step: a verb, its arguments, and its postcondition |
//! | [`field`] | What a step will actually do to a field in front of it now |
//! | [`rules`] | The six structural rules and the refusal that names one |
//! | [`matching`] | The phrase catalogue and the conditions built from it |
//! | [`narrowing`] | Taking authority away, and never the way out |
//! | [`recording`] | What the browser observed of a finished task, and whether it is a record |
//! | [`replay`] | What a matched procedure proposes next, which is a command and never an action |
//! | [`builtin`] | The one sequence the product ships, minted for the origin a person consented to |
#![doc(html_no_source)]
#![cfg_attr(
    test,
    allow(
        clippy::unwrap_used,
        clippy::expect_used,
        clippy::panic,
        clippy::indexing_slicing
    )
)]

pub mod builtin;
pub mod definition;
pub mod field;
pub mod matching;
pub mod narrowing;
pub mod record;
pub mod recording;
pub mod replay;
pub mod rules;
pub mod status;
pub mod step;

pub use crate::builtin::{build_source_table, BUILD_SOURCE_TABLE_ID};
pub use crate::definition::{
    decode, decode_beside, encode, format_version, step_count, DecodeError, EncodeError,
    Enumeration, DEFINITION_FORMAT_VERSION, MAX_DEFINITION_BYTES,
};
pub use crate::field::{
    disposition, FieldPurpose, HandoverReason, ObservedFields, StepDisposition,
};
pub use crate::matching::{
    classify_phrase, normalize_label, phrase_forms, MatchClause, MatchCondition, MatchVerdict,
    PageFacts, PageNode, PhraseId, MAX_LABEL_BYTES, MAX_MATCH_CLAUSES,
};
pub use crate::narrowing::{narrow, NarrowingRefusal};
pub use crate::record::{
    Procedure, ProcedureId, ProcedureProvenance, ProcedureScope, ProcedureVersion, RecordError,
    MAX_PROCEDURE_ID_BYTES,
};
pub use crate::recording::{
    record as record_procedure, ArgumentDescriptor, LedgerEntry, RecordedValue, Recording,
    SkillRecordError, StepDescriptor, UndescribedReason,
};
pub use crate::replay::{
    next_procedure_command, next_procedure_command_with_page, ReplayNode, ReplayPage, ReplayRefusal,
};
pub use crate::rules::{validate, Refusal, RefusalReason, StructuralRule};
pub use crate::status::{transition, LifecycleActor, LifecycleRefusal, ProcedureStatus};
pub use crate::step::{ProcedureStep, StepArgument, StepValue, FILL_VERBS, MAX_PROCEDURE_STEPS};
