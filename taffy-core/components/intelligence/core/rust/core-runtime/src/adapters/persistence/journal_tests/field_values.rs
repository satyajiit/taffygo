// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! What the journal keeps of one field-value request (decision 0238).

use bip_types::identity::SemanticNodeId;
use task_engine::{FieldNodeIds, FieldValueAskOutcome, FieldValueRequestId, SuppliedValueCount};

use super::*;

fn fields(ids: &[&str]) -> FieldNodeIds {
    FieldNodeIds::new(ids.iter().map(|id| SemanticNodeId::new(*id)).collect())
        .unwrap_or_else(|_| unreachable!())
}

fn request_id() -> FieldValueRequestId {
    task_engine::field_value_request_id_for_call(3, 0)
}

/// The companions a request asked about and the field each value was minted
/// for are advice for the fills that follow one answer. `TaskTransactionBatch`
/// is versioned by exact equality, so the persisted format does not grow to
/// hold them: a restored task knows the named line and the count, as it did
/// before, and nothing more.
#[test]
fn the_fields_of_one_ask_are_not_journalled_and_restore_as_none() {
    let request = Command::RequestFieldValues {
        request_id: request_id(),
        tab_id: TabId::new("tab-1"),
        node_id: SemanticNodeId::new("aadhaar-number"),
        companion_node_ids: fields(&["captcha-answer"]),
    };
    let persisted = command(&request).unwrap_or_else(|_| unreachable!());
    assert_eq!(
        persisted,
        wire::PersistedCommand::RequestFieldValues {
            request_id: request_id().as_str().to_owned(),
            tab_id: "tab-1".to_owned(),
            node_id: "aadhaar-number".to_owned(),
        }
    );
    assert_eq!(
        uncommand(persisted),
        Ok(Command::RequestFieldValues {
            request_id: request_id(),
            tab_id: TabId::new("tab-1"),
            node_id: SemanticNodeId::new("aadhaar-number"),
            companion_node_ids: FieldNodeIds::none(),
        })
    );

    let answer = Command::SupplyFieldValues {
        request_id: request_id(),
        supplied: SuppliedValueCount::new(2).unwrap_or_else(|_| unreachable!()),
        outcome: Some(FieldValueAskOutcome::Answered),
        field_node_ids: Some(fields(&["aadhaar-number", "captcha-answer"])),
    };
    let persisted = command(&answer).unwrap_or_else(|_| unreachable!());
    assert_eq!(
        persisted,
        wire::PersistedCommand::SupplyFieldValues {
            request_id: request_id().as_str().to_owned(),
            supplied: 2,
        }
    );
    assert_eq!(
        uncommand(persisted),
        Ok(Command::SupplyFieldValues {
            request_id: request_id(),
            supplied: SuppliedValueCount::new(2).unwrap_or_else(|_| unreachable!()),
            outcome: None,
            field_node_ids: None,
        })
    );
}

/// An ask re-issued after a restart asks about the named line alone, which is
/// what it did before this decision: the sheet is smaller, never wider.
#[test]
fn a_persisted_ask_is_reissued_about_its_named_line_alone() {
    let intent = Effect::RequestFieldValues {
        request_id: request_id(),
        tab_id: TabId::new("tab-1"),
        node_id: SemanticNodeId::new("aadhaar-number"),
        companion_node_ids: fields(&["captcha-answer"]),
    };
    assert_eq!(
        uneffect(effect(&intent)),
        Ok(Effect::RequestFieldValues {
            request_id: request_id(),
            tab_id: TabId::new("tab-1"),
            node_id: SemanticNodeId::new("aadhaar-number"),
            companion_node_ids: FieldNodeIds::none(),
        })
    );
}
