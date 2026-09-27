// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The encodings a decoder refuses and the intents an encoder refuses.

use super::*;

#[test]
fn malformed_tags_prefix_lengths_and_enums_fail_closed() {
    let intent = browser(BrowserIntent::HistoryBack { tab: tab("t") });
    let bytes = intent.encode_canonical().expect("valid intent");

    let mut bad_prefix = bytes.clone();
    bad_prefix[0] ^= 1;
    assert_eq!(
        ActionIntent::decode_canonical(&bad_prefix),
        Err(ActionIntentCodecError::InvalidPrefix)
    );

    let mut truncated = bytes.clone();
    truncated.pop();
    assert_eq!(
        ActionIntent::decode_canonical(&truncated),
        Err(ActionIntentCodecError::Truncated)
    );

    let mut duplicate = bytes.clone();
    append_field(&mut duplicate, 1, b"x");
    assert_eq!(
        ActionIntent::decode_canonical(&duplicate),
        Err(ActionIntentCodecError::DuplicateOrOutOfOrderTag)
    );

    let mut unknown = bytes.clone();
    append_field(&mut unknown, 2, b"x");
    assert_eq!(
        ActionIntent::decode_canonical(&unknown),
        Err(ActionIntentCodecError::UnknownTag)
    );

    let mut closed_enum = bytes;
    closed_enum[encoding::PREFIX.len() + 9] = 42;
    assert_eq!(
        ActionIntent::decode_canonical(&closed_enum),
        Err(ActionIntentCodecError::InvalidValue)
    );

    let too_large = vec![0; MAX_CANONICAL_ACTION_INTENT_BYTES + 1];
    assert_eq!(
        ActionIntent::decode_canonical(&too_large),
        Err(ActionIntentCodecError::TooLarge)
    );
}

#[test]
fn encoding_refuses_controls_oversize_values_and_wrong_opaque_kinds() {
    let control = browser(BrowserIntent::HistoryBack { tab: tab("bad\n") });
    assert_eq!(
        control.encode_canonical(),
        Err(ActionIntentCodecError::InvalidValue)
    );
    let huge = browser(BrowserIntent::Navigate {
        tab: tab("t"),
        address: "x".repeat(crate::tool::MAX_ARGUMENT_VALUE_BYTES + 1),
        new_tab: false,
    });
    assert_eq!(
        huge.encode_canonical(),
        Err(ActionIntentCodecError::InvalidValue)
    );
    let wrong_kind = browser(BrowserIntent::Search {
        tab: tab("t"),
        query: opaque(OpaqueOperandKind::DomQueryText, 1),
    });
    assert_eq!(
        wrong_kind.encode_canonical(),
        Err(ActionIntentCodecError::InvalidValue)
    );

    let whitespace_id = browser(BrowserIntent::HistoryBack {
        tab: tab("two words"),
    });
    assert_eq!(
        whitespace_id.encode_canonical(),
        Err(ActionIntentCodecError::InvalidValue)
    );

    for address in ["x", "http://example.test", "https://"] {
        let invalid_address = browser(BrowserIntent::Navigate {
            tab: tab("t"),
            address: address.to_owned(),
            new_tab: false,
        });
        assert_eq!(
            invalid_address.encode_canonical(),
            Err(ActionIntentCodecError::InvalidValue),
            "{address}"
        );
    }

    for limit in [0, crate::tool::MAX_DOM_QUERY_RESULTS + 1] {
        let invalid_limit = browser(BrowserIntent::DomQuery {
            tab: tab("t"),
            within: None,
            role: None,
            text: None,
            limit: Some(limit),
        });
        assert_eq!(
            invalid_limit.encode_canonical(),
            Err(ActionIntentCodecError::InvalidValue),
            "{limit}"
        );
    }

    let invalid_value_position = browser(BrowserIntent::FormFill {
        tab: tab("t"),
        field: node("n"),
        value_request: value_request("values-1"),
        value_from: crate::field_values::MAX_REQUESTED_FIELDS,
    });
    assert_eq!(
        invalid_value_position.encode_canonical(),
        Err(ActionIntentCodecError::InvalidValue)
    );

    let noncanonical_reference = browser(BrowserIntent::Search {
        tab: tab("t"),
        query: OpaqueOperandRef {
            handle: "turn-03-call-4-search-query".to_owned(),
            kind: OpaqueOperandKind::SearchQuery,
            digest: [1; 32],
        },
    });
    assert_eq!(
        noncanonical_reference.encode_canonical(),
        Err(ActionIntentCodecError::InvalidValue)
    );
}

fn append_field(bytes: &mut Vec<u8>, tag: u8, value: &[u8]) {
    bytes.push(tag);
    bytes.extend_from_slice(&(value.len() as u64).to_le_bytes());
    bytes.extend_from_slice(value);
}
