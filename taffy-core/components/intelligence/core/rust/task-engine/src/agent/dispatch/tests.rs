// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use super::{turn_call_key, turn_call_of};
use crate::ids::IdempotencyKey;
use crate::proposal::plan_step_key;
use crate::records::SourceId;
use crate::task::ConsentedSource;
use bip_types::identity::TabId;

#[test]
fn a_minted_key_reads_back_as_the_turn_and_call_it_names() {
    for (ordinal, sequence) in [(0, 0), (1, 2), (10, 0), (u64::MAX, u32::MAX)] {
        let key = turn_call_key(ordinal, sequence);
        assert_eq!(turn_call_of(&key), Some((ordinal, sequence)), "{key:?}");
    }
}

#[test]
fn a_key_from_another_minting_names_no_turn() {
    let source = ConsentedSource {
        source_id: SourceId::from_bytes([3; 16]),
        tab_id: TabId::new("tab-1"),
        normalized_origin: "https://example.test".to_owned(),
        canonical_locator: None,
    };
    let step = plan_step_key(
        crate::workflow::REVIEWED_EFFECT_NAMESPACE,
        &source,
        &crate::ids::PlanStepId::new("step-1"),
        1,
    );
    assert_eq!(turn_call_of(&step), None);
    for text in [
        "turn-1",
        "turn--call-1",
        "turn-1-call-",
        "turn-one-call-two",
        "turn-1-call-2-call-3",
        "returns-1-call-2",
    ] {
        assert_eq!(
            turn_call_of(&IdempotencyKey::new(text.to_owned())),
            None,
            "{text}"
        );
    }
}
