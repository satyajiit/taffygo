// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The definition blob of decision 0091 section 2, in both directions.
//!
//! `core_skill_version.definition` is a `BLOB` and the record format lives
//! inside it, so that adding a step kind costs a serializer change and its
//! round-trip test rather than a journal head-version bump. This file is that
//! round-trip test, and it is also the negative half: what a definition this
//! build cannot read does, which is refuse by name.
//!
//! # Why the refusals are patched into a real definition
//!
//! Each unknown-name case takes a definition this build wrote and replaces one
//! compiled-in name with a same-length name no member claims. Hand-assembling
//! the bytes instead would put a second encoder in this file, and a second
//! encoder is a thing that can agree with the test while disagreeing with the
//! one the product uses.

#![allow(
    clippy::unwrap_used,
    clippy::expect_used,
    clippy::panic,
    clippy::indexing_slicing
)]

use bip_types::action::PostconditionKind;
use bip_types::snapshot::{NodeState, SemanticRole};
use procedure_engine::definition::{
    decode, decode_beside, encode, format_version, step_count, DecodeError, Enumeration,
    DEFINITION_FORMAT_VERSION, MAX_DEFINITION_BYTES,
};
use procedure_engine::field::FieldPurpose;
use procedure_engine::matching::{MatchClause, MatchCondition, PhraseId};
use procedure_engine::record::{
    Procedure, ProcedureId, ProcedureProvenance, ProcedureScope, ProcedureVersion, RecordError,
};
use procedure_engine::status::ProcedureStatus;
use procedure_engine::step::{ProcedureStep, StepArgument, StepValue};
use task_engine::tool::ArgumentValue;

/// The origin every fixture is about.
const ORIGIN: &str = "https://example.test";

/// The identifier the whole-record fixture carries.
const IDENTIFIER: &str = "book.the.usual-table";

/// How many bytes of header sit in front of every definition: what it is, what
/// layout it is in, and how many steps it holds.
const HEADER_BYTES: usize = 8;

fn scope() -> ProcedureScope {
    ProcedureScope::for_origin(ORIGIN).unwrap()
}

fn one_clause() -> MatchCondition {
    MatchCondition::new(vec![MatchClause::RolePresent(SemanticRole::SearchField)])
}

/// A procedure naming every shape this format can write.
///
/// Three clause shapes, three value shapes, seven literal types, a step that
/// records what it fills and a step that does not, a version that is not the
/// first and a status that is not the one a draft enters at.
fn everything() -> Procedure {
    let mut procedure = Procedure::draft(
        ProcedureId::new(IDENTIFIER).unwrap(),
        scope(),
        MatchCondition::new(vec![
            MatchClause::RolePresent(SemanticRole::Document),
            MatchClause::PhraseAt {
                role: SemanticRole::Button,
                phrase: PhraseId::SignIn,
            },
            MatchClause::StateAt {
                role: SemanticRole::TextField,
                state: NodeState::Required,
            },
        ]),
        vec![
            ProcedureStep::new("browser.dom.query", PostconditionKind::NoMutation).taking(vec![
                StepArgument::literal("within", ArgumentValue::Handle(3)),
                StepArgument::literal("role", ArgumentValue::Choice("button".to_owned())),
                StepArgument::literal("text", ArgumentValue::Text("Rue de Rivoli".to_owned())),
                StepArgument::literal("limit", ArgumentValue::Count(7)),
                StepArgument::literal("new_tab", ArgumentValue::Flag(true)),
                StepArgument::literal("where", ArgumentValue::Address(ORIGIN.to_owned())),
                StepArgument::literal("which", ArgumentValue::SuppliedValue(2)),
            ]),
            ProcedureStep::new("browser.form.fill", PostconditionKind::NodeValueChanged)
                .taking(vec![
                    StepArgument::new("field", StepValue::FromEarlierStep { step: 0 }),
                    StepArgument::new(
                        "value_from",
                        StepValue::FromPerson {
                            purpose: FieldPurpose::EmailAddress,
                        },
                    ),
                ])
                .filling(FieldPurpose::EmailAddress),
        ],
        ProcedureProvenance::RecordedFromTask,
    );
    procedure.status = ProcedureStatus::Active;
    procedure.version = ProcedureVersion(7);
    procedure
}

/// One step of `kind`, and nothing else that could vary with it.
fn one_step_of(kind: PostconditionKind) -> Procedure {
    Procedure::draft(
        ProcedureId::new("one").unwrap(),
        scope(),
        one_clause(),
        vec![ProcedureStep::new("browser.dom.read", kind)],
        ProcedureProvenance::Authored,
    )
}

/// `bytes` with every occurrence of `token` replaced by a name of the same
/// length that no member of anything claims.
fn with_unknown(bytes: &[u8], token: &str) -> Vec<u8> {
    swap(bytes, token, &"z".repeat(token.len()))
}

/// `bytes` with every occurrence of `from` replaced by `to`, which must be the
/// same length so that every length prefix around it still holds.
fn swap(bytes: &[u8], from: &str, to: &str) -> Vec<u8> {
    assert_eq!(
        from.len(),
        to.len(),
        "{from} and {to} are different lengths"
    );
    let needle = from.as_bytes();
    let mut out = bytes.to_vec();
    let mut replaced = 0_usize;
    let mut index = 0_usize;
    while index + needle.len() <= out.len() {
        if &out[index..index + needle.len()] == needle {
            out[index..index + needle.len()].copy_from_slice(to.as_bytes());
            replaced += 1;
            index += needle.len();
        } else {
            index += 1;
        }
    }
    assert!(replaced > 0, "{from} is not in this definition");
    out
}

#[test]
fn one_record_survives_being_written_down_and_read_back() {
    let procedure = everything();
    let bytes = encode(&procedure).unwrap();
    assert_eq!(decode(&bytes), Ok(procedure));
}

#[test]
fn every_member_of_every_closed_vocabulary_survives_the_round_trip() {
    // Walked rather than sampled, and walked over each enumeration's own `ALL`
    // rather than over a list written here, so a member added to any of them
    // is covered without anybody remembering to come back. A member that
    // failed to round-trip would be a saved skill this build stores and cannot
    // read — which closes a bootstrap rather than losing a feature.
    for status in ProcedureStatus::ALL {
        let mut procedure = one_step_of(PostconditionKind::NoMutation);
        procedure.status = *status;
        assert_eq!(decode(&encode(&procedure).unwrap()), Ok(procedure));
    }
    for provenance in ProcedureProvenance::ALL {
        let mut procedure = one_step_of(PostconditionKind::NoMutation);
        procedure.provenance = *provenance;
        assert_eq!(decode(&encode(&procedure).unwrap()), Ok(procedure));
    }
    for purpose in FieldPurpose::ALL {
        let mut procedure = one_step_of(PostconditionKind::NodeValueChanged);
        procedure.steps =
            vec![
                ProcedureStep::new("browser.form.fill", PostconditionKind::NodeValueChanged)
                    .taking(vec![StepArgument::new(
                        "value_from",
                        StepValue::FromPerson { purpose: *purpose },
                    )])
                    .filling(*purpose),
            ];
        assert_eq!(decode(&encode(&procedure).unwrap()), Ok(procedure));
    }
    for role in SemanticRole::ALL {
        for clause in [
            MatchClause::RolePresent(*role),
            MatchClause::PhraseAt {
                role: *role,
                phrase: PhraseId::Checkout,
            },
            MatchClause::StateAt {
                role: *role,
                state: NodeState::Visible,
            },
        ] {
            let mut procedure = one_step_of(PostconditionKind::NoMutation);
            procedure.condition = MatchCondition::new(vec![clause]);
            assert_eq!(decode(&encode(&procedure).unwrap()), Ok(procedure));
        }
    }
    for state in NodeState::ALL {
        let mut procedure = one_step_of(PostconditionKind::NoMutation);
        procedure.condition = MatchCondition::new(vec![MatchClause::StateAt {
            role: SemanticRole::Button,
            state: *state,
        }]);
        assert_eq!(decode(&encode(&procedure).unwrap()), Ok(procedure));
    }
    for phrase in PhraseId::ALL {
        let mut procedure = one_step_of(PostconditionKind::NoMutation);
        procedure.condition = MatchCondition::new(vec![MatchClause::PhraseAt {
            role: SemanticRole::Button,
            phrase: *phrase,
        }]);
        assert_eq!(decode(&encode(&procedure).unwrap()), Ok(procedure));
    }
}

#[test]
fn adding_a_step_kind_costs_no_schema_version() {
    // Decision 0091's fourth validation item, expressed over the codec rather
    // than left as a comment: the step kind is entirely inside the body, so
    // every kind this build has produces a byte-identical header — the only
    // place a version lives — and every one of them is read back under one
    // `DEFINITION_FORMAT_VERSION`. A kind added to the enumeration is
    // therefore a change to this crate's serializer and its round trip, and to
    // nothing that a stored definition would have to be migrated for.
    let mut headers: Vec<Vec<u8>> = Vec::new();
    for kind in PostconditionKind::ALL {
        let procedure = one_step_of(*kind);
        let bytes = encode(&procedure).unwrap();
        assert_eq!(format_version(&bytes), Ok(DEFINITION_FORMAT_VERSION));
        assert_eq!(step_count(&bytes), Ok(1));
        assert_eq!(decode(&bytes), Ok(procedure));
        headers.push(bytes[..HEADER_BYTES].to_vec());
    }
    assert_eq!(headers.len(), PostconditionKind::ALL.len());
    let Some(first) = headers.first() else {
        panic!("the protocol has step kinds")
    };
    for header in &headers {
        assert_eq!(header, first, "a step kind reached the header");
    }
}

#[test]
fn an_unknown_name_is_refused_by_the_vocabulary_it_should_have_been_in() {
    // Every enumeration a definition names, one at a time, each in a
    // definition this build wrote. An unknown name is never a default, never
    // the nearest member and never a skipped field: it is a refusal that says
    // which vocabulary the stored name is not in, because "something in here
    // is not a member" is not a thing anybody can act on.
    let bytes = encode(&everything()).unwrap();
    let cases = [
        (Enumeration::ProcedureStatus, "active"),
        (Enumeration::ProcedureProvenance, "recorded_from_task"),
        (Enumeration::MatchClauseShape, "phrase_at"),
        (Enumeration::SemanticRole, "BUTTON"),
        (Enumeration::Phrase, "sign_in"),
        (Enumeration::NodeState, "REQUIRED"),
        (Enumeration::PostconditionKind, "NO_MUTATION"),
        (Enumeration::ArgumentValueType, "count"),
        (Enumeration::StepValueShape, "from_earlier_step"),
        (Enumeration::FieldPurpose, "email_address"),
    ];
    for (enumeration, token) in cases {
        assert_eq!(
            decode(&with_unknown(&bytes, token)),
            Err(DecodeError::UnknownMember { enumeration }),
            "{token}"
        );
    }
    // And the ten are all of them, so a vocabulary added to the format cannot
    // arrive without a case here.
    let covered: Vec<Enumeration> = cases.iter().map(|(enumeration, _)| *enumeration).collect();
    for enumeration in Enumeration::ALL {
        assert!(covered.contains(enumeration), "{}", enumeration.label());
    }
    assert_eq!(covered.len(), Enumeration::ALL.len());
}

#[test]
fn a_definition_from_another_layout_is_refused_by_its_version() {
    // Decision 0091's second validation item. The version is read and reported
    // rather than swallowed, so the whole bootstrap closes with a reason that
    // names the layout — and not with a profile quietly missing one standing
    // arrangement.
    let mut bytes = encode(&everything()).unwrap();
    let older = DEFINITION_FORMAT_VERSION.wrapping_add(1);
    bytes[4..6].copy_from_slice(&older.to_be_bytes());
    assert_eq!(
        decode(&bytes),
        Err(DecodeError::UnsupportedFormatVersion { found: older })
    );
    assert_eq!(
        step_count(&bytes),
        Err(DecodeError::UnsupportedFormatVersion { found: older })
    );
    // Reported, because a caller has to be able to say which layout it was.
    assert_eq!(format_version(&bytes), Ok(older));
    // And bytes that were never a definition are told apart from a layout.
    assert_eq!(
        decode(b"not a definition"),
        Err(DecodeError::NotADefinition)
    );
}

#[test]
fn a_definition_that_stops_early_is_refused_at_every_length_it_could_stop_at() {
    // Truncation is checked at every prefix rather than at one, because a
    // reader that ran past the end would do it at exactly one offset and a
    // sampled test would miss it. Every prefix is `Truncated`: a length that
    // is declared is always read before the bytes it counts.
    let bytes = encode(&everything()).unwrap();
    for length in 0..bytes.len() {
        assert_eq!(
            decode(&bytes[..length]),
            Err(DecodeError::Truncated),
            "{length}"
        );
    }
    assert!(decode(&bytes).is_ok());
}

#[test]
fn a_definition_with_anything_after_it_is_refused_rather_than_read_up_to_the_end() {
    let mut bytes = encode(&everything()).unwrap();
    bytes.push(0);
    assert_eq!(decode(&bytes), Err(DecodeError::TrailingBytes));
    // Including bytes that would themselves have been a definition: two
    // records in one column is not a longer record.
    let mut two = encode(&everything()).unwrap();
    two.extend_from_slice(&encode(&everything()).unwrap());
    assert_eq!(decode(&two), Err(DecodeError::TrailingBytes));
}

#[test]
fn a_step_count_that_is_not_the_columns_is_refused_naming_both_numbers() {
    // The count is beside the blob because a bound has to be enforceable by
    // the database that holds it. Two answers to how many steps a procedure
    // has is one answer too many, and the caller is handed both rather than
    // whichever one this crate happened to prefer.
    let bytes = encode(&everything()).unwrap();
    assert_eq!(step_count(&bytes), Ok(2));
    assert!(decode_beside(&bytes, 2).is_ok());
    for column in [0, 1, 3, u32::MAX] {
        assert_eq!(
            decode_beside(&bytes, column),
            Err(DecodeError::StepCountNotTheColumns {
                column,
                definition: 2
            }),
            "{column}"
        );
    }
}

#[test]
fn a_blob_larger_than_the_column_could_hold_is_refused_before_it_is_read() {
    let oversized = vec![0_u8; MAX_DEFINITION_BYTES + 1];
    let refusal = DecodeError::TooLarge {
        bytes: oversized.len(),
    };
    assert_eq!(decode(&oversized), Err(refusal));
    assert_eq!(step_count(&oversized), Err(refusal));
    assert_eq!(format_version(&oversized), Err(refusal));
}

#[test]
fn a_stored_part_this_build_would_not_have_written_is_refused_at_load() {
    // The load half of decision 0091 section 2: the database cannot check
    // anything inside a definition, so the identifier and the scope go back
    // through their own constructors rather than into the struct. A scope that
    // is a second spelling of one origin is how two spellings become two
    // origins, and it is refused here exactly as it was at first writing.
    let bytes = encode(&everything()).unwrap();
    assert_eq!(
        decode(&swap(&bytes, ORIGIN, "https://Example.test")),
        Err(DecodeError::Record(RecordError::ScopeOriginNotCanonical))
    );
    assert_eq!(
        decode(&swap(&bytes, IDENTIFIER, "BOOK.THE.USUAL-TABLE")),
        Err(DecodeError::Record(RecordError::IdNotWellFormed))
    );
}
