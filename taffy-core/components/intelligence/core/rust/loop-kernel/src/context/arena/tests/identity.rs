// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! A page identity is frozen from canonical evidence and refuses anything else.

use super::evidence;
use crate::context::arena::{PageIdentity, PageIdentityError};
use bip_types::identity::GraphRevision;

#[test]
fn an_observation_becomes_the_page_a_handle_is_frozen_against() {
    let identity =
        PageIdentity::from_evidence(&evidence("https://example.test")).expect("an identity");
    assert_eq!(identity.graph_revision, GraphRevision(7));
    let handle = identity.node_handle("n-1");
    assert_eq!(handle.node_id.as_str(), "n-1");
    assert_eq!(
        handle.expected_origin.serialization.as_deref(),
        Some("https://example.test")
    );
}

#[test]
fn an_origin_this_core_would_not_have_written_is_refused() {
    // It parses. It is not canonical. Letting it through would put a
    // string nothing re-derived on the security side of every later
    // comparison against `expected_origin`.
    assert_eq!(
        PageIdentity::from_evidence(&evidence("HTTPS://Example.test")),
        Err(PageIdentityError::OriginNotCanonical)
    );
}

#[test]
fn a_url_is_not_an_origin_and_is_refused_as_one() {
    assert!(matches!(
        PageIdentity::from_evidence(&evidence("https://example.test/account")),
        Err(PageIdentityError::Origin(_))
    ));
}
